#include "HybridGIGPUTestSupport.h"

int main(int argc,char** argv)
{
    try {
        bool hardware=argc==2 && std::string(argv[1])=="--hardware";
        ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
        Check(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context));
        ComPtr<IDXGIDevice> dxgi; ComPtr<IDXGIAdapter> adapter; DXGI_ADAPTER_DESC description{};
        Check(device.As(&dxgi)); Check(dxgi->GetAdapter(&adapter)); Check(adapter->GetDesc(&description));
        std::wcout<<L"Adapter: "<<description.Description<<L"; hardware="<<hardware<<L'\n';
        const auto source=Read("tools/shaders/HybridGICacheQueue.cs.hlsl");
        auto compile=[&](const char* define) {
            auto text=std::string(define)+source; ComPtr<ID3DBlob> code,errors; ComPtr<ID3D11ComputeShader> shader;
            auto hr=D3DCompile(text.data(),text.size(),"bounded-cache-queue",nullptr,nullptr,"main","cs_5_0",
                D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_WARNINGS_ARE_ERRORS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
            if(FAILED(hr) && errors) std::cerr<<static_cast<const char*>(errors->GetBufferPointer());
            Check(hr); Check(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader)); return shader;
        };
        auto classify=compile("#define QUEUE_CLASSIFY\n"),prefix=compile("#define QUEUE_PREFIX\n"),scatter=compile("");
        Buffer candidates,counts,keys,offsets,control,queue;
        candidates.Create(device.Get(),65536,16); counts.Create(device.Get(),2048,4); keys.Create(device.Get(),65536,8);
        offsets.Create(device.Get(),2048,4); control.Create(device.Get(),2,4); queue.Create(device.Get(),65536,4);
        ComPtr<ID3D11Buffer> constants,readback,readControl;
        D3D11_BUFFER_DESC d{}; d.ByteWidth=16; d.BindFlags=D3D11_BIND_CONSTANT_BUFFER; Check(device->CreateBuffer(&d,nullptr,&constants));
        d.ByteWidth=65536*4; d.Usage=D3D11_USAGE_STAGING; d.BindFlags=0; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        Check(device->CreateBuffer(&d,nullptr,&readback)); d.ByteWidth=8; Check(device->CreateBuffer(&d,nullptr,&readControl));
        auto* cb=constants.Get(); context->CSSetConstantBuffers(0,1,&cb);
        auto reset=[&] { ID3D11ShaderResourceView* s[4]{}; ID3D11UnorderedAccessView* u[2]{};
            context->CSSetShaderResources(0,4,s); context->CSSetUnorderedAccessViews(0,2,u,nullptr); };
        ComPtr<ID3D11Query> disjoint,start,end; D3D11_QUERY_DESC q{D3D11_QUERY_TIMESTAMP_DISJOINT,0};
        Check(device->CreateQuery(&q,&disjoint)); q.Query=D3D11_QUERY_TIMESTAMP; Check(device->CreateQuery(&q,&start)); Check(device->CreateQuery(&q,&end));
        std::vector<std::array<unsigned,4>> input(65536);
        std::vector<unsigned> lastService(65536),order(65536);
        std::vector<double> timings;
        unsigned maxWait=0;
        unsigned queueClock=0,queueReservation=0;
        auto priority=[&](const std::array<unsigned,4>& c) {
            if(!c[0]) return 8u;
            if(queueReservation && ((c[0]-1-queueClock*queueReservation)&65535)<queueReservation) return 0u;
            if(c[1]>=8) return 1u;
            unsigned state=c[2]>>28;
            if(state==5 || ((c[2]>>16)&255)) return 2u;
            if(state<2 || state>5) return 3u;
            if(state==3 || c[3]<204 || (c[2]&65535)>1319) return 4u;
            if(c[1]>=2) return 5u;
            if((c[2]>>24)&15) return 6u;
            return 7u;
        };
        for(unsigned clock=0;clock<132;++clock) {
            unsigned budget=4096,capacity=65536;
            if(clock==128) capacity=17;
            if(clock==129) capacity=0;
            if(clock==130) capacity=65537;
            if(clock==131) budget=0;
            queueClock=clock; queueReservation=std::min(65536u,std::min(budget,capacity))/2;
            for(unsigned i=0;i<65536;++i) {
                // Continually changing nearby emitters compete with old surfaces.
                unsigned state=i%7==0?5u:4u;
                input[i]={i+1,clock-lastService[i],state<<28,255};
                order[i]=(i+clock*1021u)&65535u;
                if(clock<128) maxWait=std::max(maxWait,input[i][1]);
            }
            const std::array<unsigned,4> params{clock,budget,capacity,0};
            context->UpdateSubresource(constants.Get(),0,nullptr,params.data(),0,0);
            context->UpdateSubresource(candidates.resource.Get(),0,nullptr,input.data(),0,0);
            context->Begin(disjoint.Get()); context->End(start.Get());
            auto* candidateView=candidates.srv.Get(); context->CSSetShaderResources(0,1,&candidateView);
            ID3D11UnorderedAccessView* u[]{counts.uav.Get(),keys.uav.Get()};
            context->CSSetUnorderedAccessViews(0,2,u,nullptr); context->CSSetShader(classify.Get(),nullptr,0); context->Dispatch(256,1,1); reset();
            auto* countView=counts.srv.Get(); context->CSSetShaderResources(1,1,&countView);
            u[0]=offsets.uav.Get(); u[1]=control.uav.Get(); context->CSSetUnorderedAccessViews(0,2,u,nullptr);
            context->CSSetShader(prefix.Get(),nullptr,0); context->Dispatch(1,1,1); reset();
            ID3D11ShaderResourceView* s[]{candidates.srv.Get(),keys.srv.Get(),offsets.srv.Get(),control.srv.Get()};
            context->CSSetShaderResources(0,4,s); u[0]=queue.uav.Get(); u[1]=nullptr; context->CSSetUnorderedAccessViews(0,2,u,nullptr);
            context->CSSetShader(scatter.Get(),nullptr,0); context->Dispatch(256,1,1); reset();
            context->End(end.Get()); context->End(disjoint.Get());
            context->CopyResource(readback.Get(),queue.resource.Get()); context->CopyResource(readControl.Get(),control.resource.Get());
            D3D11_MAPPED_SUBRESOURCE m{}; Check(context->Map(readControl.Get(),0,D3D11_MAP_READ,0,&m));
            const auto got=*static_cast<const std::array<unsigned,2>*>(m.pData); context->Unmap(readControl.Get(),0);
            bool invalid=capacity==0 || capacity>65536;
            unsigned expected=invalid?0:std::min(budget,capacity);
            if(got[0]!=expected || got[1]!=unsigned(invalid)) throw std::runtime_error("Queue bound/recovery violated");
            std::stable_sort(order.begin(),order.end(),[&](unsigned a,unsigned b){return priority(input[a])<priority(input[b]);});
            Check(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&m)); auto* values=static_cast<const unsigned*>(m.pData);
            for(unsigned i=0;i<expected;++i) {
                if(values[i]!=order[i]+1) throw std::runtime_error("GPU queue ordering differs from stable priority oracle");
                lastService[values[i]-1]=clock;
            }
            context->Unmap(readback.Get(),0);
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT f{}; UINT64 a{},b{};
            if(clock>8 && clock<128 && context->GetData(disjoint.Get(),&f,sizeof(f),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK && !f.Disjoint && f.Frequency &&
                context->GetData(start.Get(),&a,sizeof(a),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK && context->GetData(end.Get(),&b,sizeof(b),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK)
                timings.push_back(double(b-a)*1000.0/double(f.Frequency));
        }
        if(maxWait>32) throw std::runtime_error("Queue starved a visible cell past its reserved 32-tick service bound");
        if(hardware && !timings.empty()) { std::sort(timings.begin(),timings.end()); std::cout<<"queue-only p50_ms="<<timings[timings.size()/2]<<" p95_ms="<<timings[(timings.size()-1)*95/100]<<" max_ms="<<timings.back()<<'\n'; }
        std::cout<<"PASS: 132 GPU queues, exact CPU priority equivalence, capacity bounds/recovery, max service gap="<<maxWait<<" ticks; no runtime queue activation\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
