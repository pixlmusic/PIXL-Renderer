#include "HybridGIGPUTestSupport.h"

int main(int argc, char** argv)
{
    try {
        const bool hardware=argc==2 && std::string(argv[1])=="--hardware";
        ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
        Check(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,
            nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context));
        ComPtr<IDXGIDevice> dxgi; ComPtr<IDXGIAdapter> adapter; DXGI_ADAPTER_DESC description{};
        Check(device.As(&dxgi)); Check(dxgi->GetAdapter(&adapter)); Check(adapter->GetDesc(&description));
        std::wcout<<L"Adapter: "<<description.Description<<L"; hardware="<<hardware<<L'\n';
        const auto source=Read("tools/shaders/HybridGICacheSnapshot.cs.hlsl");
        auto compile=[&](const char* define) {
            auto text=std::string(define)+source;
            ComPtr<ID3DBlob> code, errors; ComPtr<ID3D11ComputeShader> shader;
            auto hr=D3DCompile(text.data(),text.size(),"cache-snapshot-prototype",nullptr,nullptr,"main","cs_5_0",
                D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_WARNINGS_ARE_ERRORS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
            if(FAILED(hr) && errors) std::cerr<<static_cast<const char*>(errors->GetBufferPointer());
            Check(hr); Check(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader)); return shader;
        };
        auto mutate=compile("#define SNAPSHOT_MUTATE\n"), collect=compile("#define SNAPSHOT_JOURNAL\n"), sync=compile("");
        std::array<Texture,7> current, snapshot, reference;
        Texture dirty; dirty.Create(device.Get());
        const unsigned zero[4]{};
        for(unsigned i=0;i<7;++i) for(auto* set:{&current,&snapshot,&reference}) {
            (*set)[i].Create(device.Get(),i<4?DXGI_FORMAT_R32_UINT:DXGI_FORMAT_R16G16B16A16_FLOAT);
            context->ClearUnorderedAccessViewUint((*set)[i].uav.Get(),zero);
        }
        Buffer journal, control; journal.Create(device.Get(),65536,8); control.Create(device.Get(),4,4);
        D3D11_BUFFER_DESC cd{}; cd.ByteWidth=16; cd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        ComPtr<ID3D11Buffer> constants; Check(device->CreateBuffer(&cd,nullptr,&constants));
        auto* cb=constants.Get(); context->CSSetConstantBuffers(0,1,&cb);
        auto reset=[&] { ID3D11ShaderResourceView* s[9]{}; ID3D11UnorderedAccessView* u[8]{};
            context->CSSetShaderResources(0,9,s); context->CSSetUnorderedAccessViews(0,8,u,nullptr); };
        ComPtr<ID3D11Query> disjoint, start, middle, end;
        D3D11_QUERY_DESC qd{D3D11_QUERY_TIMESTAMP_DISJOINT,0}; Check(device->CreateQuery(&qd,&disjoint));
        qd.Query=D3D11_QUERY_TIMESTAMP; Check(device->CreateQuery(&qd,&start)); Check(device->CreateQuery(&qd,&middle)); Check(device->CreateQuery(&qd,&end));
        std::vector<double> fullMs,journalMs;
        for(unsigned epoch=1;epoch<=64;++epoch) {
            const unsigned capacity=epoch%4==0?127u:65536u;
            const std::array<unsigned,4> params{epoch,capacity,epoch%3==0?1u:97u,0};
            context->UpdateSubresource(constants.Get(),0,nullptr,params.data(),0,0);
            // Save the immutable generation before any current-cache writes.
            std::array<std::vector<std::uint32_t>,7> previous;
            for(unsigned i=0;i<7;++i) previous[i]=snapshot[i].Readback(context.Get());
            std::array<ID3D11UnorderedAccessView*,8> uavs{};
            for(unsigned i=0;i<7;++i) uavs[i]=current[i].uav.Get(); uavs[7]=dirty.uav.Get();
            context->CSSetUnorderedAccessViews(0,8,uavs.data(),nullptr); context->CSSetShader(mutate.Get(),nullptr,0);
            context->Dispatch(128,8,1); reset();
            for(unsigned i=0;i<7;++i) if(snapshot[i].Readback(context.Get())!=previous[i])
                throw std::runtime_error("Current writes mutated the previous snapshot");
            // Missing journal/epoch mismatch exercise full recovery, including
            // hash tombstones, without reading partially synchronized payloads.
            const std::array<unsigned,4> initialControl{0,epoch%11==0?1u:0u,epoch%13==0?epoch-1:epoch,0};
            context->UpdateSubresource(control.resource.Get(),0,nullptr,initialControl.data(),0,0);
            context->Begin(disjoint.Get()); context->End(start.Get());
            auto* dirtyView=dirty.srv.Get(); context->CSSetShaderResources(0,1,&dirtyView);
            ID3D11UnorderedAccessView* collectUavs[]{journal.uav.Get(),control.uav.Get()};
            context->CSSetUnorderedAccessViews(0,2,collectUavs,nullptr); context->CSSetShader(collect.Get(),nullptr,0);
            context->Dispatch(128,8,1); reset();
            std::array<ID3D11ShaderResourceView*,9> sources{};
            for(unsigned i=0;i<7;++i) { sources[i]=current[i].srv.Get(); uavs[i]=snapshot[i].uav.Get(); }
            sources[7]=journal.srv.Get(); sources[8]=control.srv.Get(); uavs[7]=nullptr;
            context->CSSetShaderResources(0,9,sources.data()); context->CSSetUnorderedAccessViews(0,8,uavs.data(),nullptr);
            context->CSSetShader(sync.Get(),nullptr,0); context->Dispatch(1024,1,1); reset();
            context->End(middle.Get());
            for(unsigned i=0;i<7;++i) context->CopyResource(reference[i].resource.Get(),current[i].resource.Get());
            context->End(end.Get()); context->End(disjoint.Get());
            for(unsigned i=0;i<7;++i) if(snapshot[i].Readback(context.Get())!=reference[i].Readback(context.Get()))
                throw std::runtime_error("Journal/full-copy bit mismatch, epoch "+std::to_string(epoch)+", payload "+std::to_string(i));
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT f{}; UINT64 a{},b{},c{};
            if(epoch>8 && context->GetData(disjoint.Get(),&f,sizeof(f),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK && !f.Disjoint && f.Frequency &&
                context->GetData(start.Get(),&a,sizeof(a),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK &&
                context->GetData(middle.Get(),&b,sizeof(b),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK &&
                context->GetData(end.Get(),&c,sizeof(c),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK) {
                journalMs.push_back(double(b-a)*1000.0/double(f.Frequency)); fullMs.push_back(double(c-b)*1000.0/double(f.Frequency));
            }
        }
        if(hardware) {
            auto report=[](const char* label,std::vector<double>& times) {
                if(times.empty()) throw std::runtime_error("No valid hardware timestamps");
                std::sort(times.begin(),times.end());
                std::cout<<label<<": samples="<<times.size()<<" p50_ms="<<times[times.size()/2]<<" p95_ms="<<times[(times.size()-1)*95/100]<<" max_ms="<<times.back()<<'\n';
            };
            report("seven full copies",fullMs); report("journal collection+sync (excludes dirty marking)",journalMs);
        }
        std::cout<<"PASS: 64 GPU generations, immutable reads, seven format-exact payloads, tombstones, overflow and missing-epoch recovery\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
