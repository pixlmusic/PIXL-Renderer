#include "HybridGIGPUTestSupport.h"
int main(int argc, char** argv)
{
    try {
        ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
        const bool hardware=argc==2 && std::string(argv[1])=="--hardware";
        Check(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context));
        ComPtr<IDXGIDevice> dxgi; ComPtr<IDXGIAdapter> adapter; DXGI_ADAPTER_DESC adapterDesc{};
        Check(device.As(&dxgi)); Check(dxgi->GetAdapter(&adapter)); Check(adapter->GetDesc(&adapterDesc));
        std::wcout<<L"Adapter: "<<adapterDesc.Description<<L"; hardware="<<hardware<<L'\n';
        ComPtr<ID3D11Query> disjoint, start, sparseEnd, referenceEnd;
        D3D11_QUERY_DESC qd{D3D11_QUERY_TIMESTAMP_DISJOINT,0}; Check(device->CreateQuery(&qd,&disjoint));
        qd.Query=D3D11_QUERY_TIMESTAMP;
        Check(device->CreateQuery(&qd,&start));
        Check(device->CreateQuery(&qd,&sparseEnd));
        Check(device->CreateQuery(&qd,&referenceEnd));
        std::vector<double> referenceMs, prototypeMs;
        auto helpers=Read("pipeline/Hybrid GI/Kernels/HybridGI/worldCache.hlsli");
        const auto begin=helpers.find("#define WORLD_CACHE_DIM"), end=helpers.find("uint WorldCacheHash(");
        if(begin==std::string::npos || end<=begin) throw std::runtime_error("Helper boundary changed");
        const auto prefix=std::string("#define CACHE_INTELLIGENCE\n")+helpers.substr(begin,end-begin)+
            "\ncbuffer TestParameters : register(b0) { uint WorldCacheClock; uint WorldCacheMaxAge; uint ActiveCapacity; uint padding; };\n";
        auto compile=[&](std::string source) {
            ComPtr<ID3DBlob> code, errors; ComPtr<ID3D11ComputeShader> shader;
            auto hr=D3DCompile(source.data(),source.size(),"cache-decay-prototype",nullptr,nullptr,"main","cs_5_0",
                D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_WARNINGS_ARE_ERRORS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
            if(FAILED(hr) && errors) std::cerr<<static_cast<const char*>(errors->GetBufferPointer());
            Check(hr); Check(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader)); return shader;
        };
        auto referenceSource=Read("pipeline/Hybrid GI/Kernels/HybridGI/worldCacheDecay.cs.hlsl");
        const auto kernel=referenceSource.find("RWTexture2D");
        if(kernel==std::string::npos) throw std::runtime_error("Decay kernel boundary changed");
        auto reference=compile(prefix+referenceSource.substr(kernel));
        const auto experiment=Read("tools/shaders/HybridGICacheActive.cs.hlsl");
        auto build=compile("#define ACTIVE_BUILD\n"+prefix+experiment);
        auto args=compile("#define ACTIVE_ARGUMENTS\n"+prefix+experiment);
        auto sparse=compile(prefix+experiment);
        Texture refMeta, refIntel, testMeta, testIntel, generations;
        for(auto* t:{&refMeta,&refIntel,&testMeta,&testIntel,&generations}) t->Create(device.Get());
        Buffer active, control; active.Create(device.Get(),65536,8); control.Create(device.Get(),2,4);
        ComPtr<ID3D11Buffer> constants, indirect;
        D3D11_BUFFER_DESC bd{}; bd.ByteWidth=16; bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        Check(device->CreateBuffer(&bd,nullptr,&constants));
        bd.BindFlags=D3D11_BIND_UNORDERED_ACCESS; bd.MiscFlags=D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS|D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
        Check(device->CreateBuffer(&bd,nullptr,&indirect));
        D3D11_UNORDERED_ACCESS_VIEW_DESC vd{}; vd.Format=DXGI_FORMAT_R32_TYPELESS; vd.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;
        vd.Buffer.NumElements=4; vd.Buffer.Flags=D3D11_BUFFER_UAV_FLAG_RAW;
        ComPtr<ID3D11UnorderedAccessView> indirectUav; Check(device->CreateUnorderedAccessView(indirect.Get(),&vd,&indirectUav));
        auto* cb=constants.Get(); context->CSSetConstantBuffers(0,1,&cb);
        auto reset=[&] { ID3D11ShaderResourceView* s[3]{}; ID3D11UnorderedAccessView* u[2]{};
            context->CSSetShaderResources(0,3,s); context->CSSetUnorderedAccessViews(0,2,u,nullptr); };
        auto bind=[&](ID3D11ComputeShader* shader, std::array<ID3D11ShaderResourceView*,3> s, std::array<ID3D11UnorderedAccessView*,2> u) {
            reset(); context->CSSetShaderResources(0,3,s.data()); context->CSSetUnorderedAccessViews(0,2,u.data(),nullptr); context->CSSetShader(shader,nullptr,0); };
        std::vector<std::uint32_t> meta(65536), intel(65536), generation(65536,1);
        for(unsigned i=0;i<65536;++i) { meta[i]=((i&255u)<<24)|(i+1); intel[i]=((i%9u)<<28)|123u; }
        unsigned overflows=0;
        for(unsigned clock=0;clock<520;++clock) {
            // Dense/sparse occupancy, expiry wrap and deliberate
            // capacity overflow all compare against the actual production kernel.
            const unsigned capacity=clock%3==0 ? 127u : 65536u;
            const std::array<unsigned,4> params{clock,48,capacity,0};
            context->UpdateSubresource(constants.Get(),0,nullptr,params.data(),0,0);
            if(clock==128 || clock==519) { std::fill(meta.begin(),meta.end(),0); std::fill(intel.begin(),intel.end(),0); }
            for(unsigned i=clock%7;clock!=519 && i<65536;i+=997) {
                meta[i]=((clock&255u)<<24)|(i+1); intel[i]=((i%9u)<<28)|123u; ++generation[i];
            }
            for(auto* t:{&refMeta,&testMeta}) context->UpdateSubresource(t->resource.Get(),0,nullptr,meta.data(),4096,0);
            for(auto* t:{&refIntel,&testIntel}) context->UpdateSubresource(t->resource.Get(),0,nullptr,intel.data(),4096,0);
            context->UpdateSubresource(generations.resource.Get(),0,nullptr,generation.data(),4096,0);
            const unsigned zero[4]{}; context->ClearUnorderedAccessViewUint(control.uav.Get(),zero);
            context->Begin(disjoint.Get()); context->End(start.Get());
            bind(build.Get(),{testMeta.srv.Get(),generations.srv.Get(),nullptr},{active.uav.Get(),control.uav.Get()});
            context->Dispatch(128,8,1);
            reset();
            if(clock%7==0 && clock!=519) {
                // A publication between collection and expiry invalidates the
                // captured generation without letting a queue erase new data.
                const unsigned index=clock%7;
                ++generation[index]; meta[index]=((clock&255u)<<24)|(index+1);
                D3D11_BOX box{index,0,0,index+1,1,1};
                context->UpdateSubresource(generations.resource.Get(),0,&box,&generation[index],4,0);
                for(auto* t:{&refMeta,&testMeta}) context->UpdateSubresource(t->resource.Get(),0,&box,&meta[index],4,0);
            }
            bind(args.Get(),{control.srv.Get(),nullptr,nullptr},{indirectUav.Get(),nullptr}); context->Dispatch(1,1,1);
            bind(sparse.Get(),{active.srv.Get(),control.srv.Get(),generations.srv.Get()},{testMeta.uav.Get(),testIntel.uav.Get()});
            context->DispatchIndirect(indirect.Get(),0);
            context->End(sparseEnd.Get());
            bind(reference.Get(),{nullptr,nullptr,nullptr},{refMeta.uav.Get(),refIntel.uav.Get()}); context->Dispatch(128,8,1);
            context->End(referenceEnd.Get()); context->End(disjoint.Get()); reset();
            auto expectedMeta=refMeta.Readback(context.Get()), expectedIntel=refIntel.Readback(context.Get());
            if(testMeta.Readback(context.Get())!=expectedMeta || testIntel.Readback(context.Get())!=expectedIntel)
                throw std::runtime_error("Sparse/full-sweep mismatch at clock "+std::to_string(clock));
            meta=std::move(expectedMeta); intel=std::move(expectedIntel); overflows+=capacity==127;
            // The correctness readback already completed these offline tests.
            // Never spin on queries; unavailable/disjoint measurements are skipped.
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT frequency{}; UINT64 a{}, b{}, c{};
            if(clock>15 && context->GetData(disjoint.Get(),&frequency,sizeof(frequency),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK &&
                !frequency.Disjoint && frequency.Frequency &&
                context->GetData(start.Get(),&a,sizeof(a),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK &&
                context->GetData(sparseEnd.Get(),&b,sizeof(b),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK &&
                context->GetData(referenceEnd.Get(),&c,sizeof(c),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK) {
                prototypeMs.push_back(double(b-a)*1000.0/double(frequency.Frequency));
                referenceMs.push_back(double(c-b)*1000.0/double(frequency.Frequency));
            }
        }
        if(hardware) {
            auto report=[](const char* name,std::vector<double>& samples) {
                std::sort(samples.begin(),samples.end());
                if(samples.empty()) { std::cout<<name<<": NO VALID TIMESTAMPS\n"; return; }
                std::cout<<name<<": samples="<<samples.size()<<" p50_ms="<<samples[samples.size()/2]
                    <<" p95_ms="<<samples[(samples.size()-1)*95/100]<<" max_ms="<<samples.back()<<'\n';
            };
            report("full-sweep",referenceMs); report("collect+indirect+sparse",prototypeMs);
        }
        std::cout<<"PASS: 520 GPU cache ticks, 65536 cells, sparse/reference equality, "<<overflows<<" bounded-capacity recovery scenarios. Offline correctness/microbenchmark only; no game performance claim.\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
