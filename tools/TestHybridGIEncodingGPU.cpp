#include "HybridGIGPUTestSupport.h"
#include <cmath>

static std::string Section(const std::string& text,const char* first,const char* last)
{
    const auto a=text.find(first),b=text.find(last,a);
    if(a==std::string::npos || b==std::string::npos || b<=a) throw std::runtime_error("Production helper extraction changed");
    return text.substr(a,b-a);
}
int main(int argc,char** argv)
{
    try {
        const bool hardware=argc==2 && std::string(argv[1])=="--hardware";
        ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
        Check(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context));
        auto source=std::string("namespace Color {\n")+
            Section(Read("distribution/PIXL-Shaders/Common/Color.hlsli"),"float3 RGBToYCoCg(","float3 Saturation(")+"}\n"+
            Section(Read("pipeline/Hybrid GI/Kernels/HybridGI/common.hlsli"),"#define ISNAN(","// [Optimization] Half")+
            Section(Read("pipeline/Hybrid GI/Kernels/HybridGI/worldCache.hlsli"),"float2 EncodeWorldOctahedron(","float WorldCacheCellSize(")+
            Read("tools/shaders/HybridGICacheEncoding.cs.hlsl");
        ComPtr<ID3DBlob> code,errors;
        auto hr=D3DCompile(source.data(),source.size(),"production-sh-encoding-comparison",nullptr,nullptr,"main","cs_5_0",
            D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_WARNINGS_ARE_ERRORS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
        if(FAILED(hr) && errors) std::cerr<<static_cast<const char*>(errors->GetBufferPointer());
        Check(hr); ComPtr<ID3D11ComputeShader> shader;
        Check(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader));
        Buffer output; output.Create(device.Get(),16384,16);
        auto* uav=output.uav.Get(); context->CSSetUnorderedAccessViews(0,1,&uav,nullptr);
        context->CSSetShader(shader.Get(),nullptr,0); context->Dispatch(64,1,1);
        uav=nullptr; context->CSSetUnorderedAccessViews(0,1,&uav,nullptr);
        D3D11_BUFFER_DESC desc{}; desc.ByteWidth=16384*16; desc.Usage=D3D11_USAGE_STAGING; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Buffer> staging; Check(device->CreateBuffer(&desc,nullptr,&staging));
        context->CopyResource(staging.Get(),output.resource.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{}; Check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped));
        auto* rows=static_cast<const std::array<float,4>*>(mapped.pData);
        constexpr std::array<const char*,4> names{"legacy-L2-FP16 (24B)","L1-FP16 (16B)","two-RGB-lobes+residual (24B aligned)","L2-SNORM10+chroma8 (16B)"};
        for(unsigned mode=0;mode<4;++mode) {
            double squared=0,maximum=0;
            for(unsigned sample=0;sample<4096;++sample) {
                const auto& row=rows[mode*4096+sample];
                for(float component:row) if(!std::isfinite(component) || component<0) throw std::runtime_error("Nonfinite/negative encoding output");
                if(sample<256 && (row[0]!=0 || row[1]!=0 || row[2]!=0)) throw std::runtime_error("Valid darkness gained radiance");
                squared+=double(row[3])*row[3]; maximum=std::max(maximum,double(row[3]));
            }
            const double rms=std::sqrt(squared/4096);
            std::cout<<names[mode]<<": relative_rms="<<rms<<" maximum="<<maximum<<" quality_gate="<<(rms<=0.02 && maximum<=0.15?"PASS":"FAIL")<<'\n';
            // At HDR lobe zeros, half-coefficient cancellation can exceed a
            // 5% per-query relative error despite tiny source-relative error.
            // Apply the declared corpus gate consistently to the legacy path.
            if(mode==0 && (rms>0.02 || maximum>0.15)) throw std::runtime_error("Legacy FP16 reference exceeds corpus gate");
        }
        context->Unmap(staging.Get(),0);
        std::cout<<"PASS: 16 dark/HDR/colored/multilobe cases, 256 directional queries each; no layout activated and no decode-performance claim\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
