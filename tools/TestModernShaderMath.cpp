// Execute production PIXL BRDF and homogeneous-medium helpers on DX11 WARP.
#define NOMINMAX
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
using Microsoft::WRL::ComPtr;
void Check(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("DX11 failure " + std::to_string(hr)); }
std::string Read(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot read shader source");
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
std::string Function(const std::string& text, const std::string& name) {
    const auto start = text.find("float " + name + "(");
    if (start == std::string::npos) throw std::runtime_error("Missing " + name);
    auto pos = text.find('{', start);
    int depth = 0;
    do { if (text[pos] == '{') ++depth; if (text[pos] == '}') --depth; ++pos; }
    while (pos < text.size() && depth);
    if (depth) throw std::runtime_error("Unbalanced function body");
    return text.substr(start, pos - start);
}
int wmain(int argc, wchar_t** argv) {
    try {

        const std::filesystem::path root = argc == 2 ? std::filesystem::path(argv[1]) : std::filesystem::current_path();
        const auto pixl = Read(root / "distribution/Shaders/Common/BRDF.hlsli");
        const auto fog = Read(root / "pipeline/Atmosphere/Kernels/Atmosphere/VolumetricFogIntegrationCS.hlsl");
        std::string shader = Read(root / "distribution/Shaders/Common/Math.hlsli") + "\n";
        for (const char* name : {"D_GGX", "Vis_SmithJointApprox", "Vis_SmithJoint"}) shader += Function(pixl, name) + "\n";
        shader += Function(fog, "HomogeneousScatteringWeight");
        shader += R"(
RWStructuredBuffer<float4> Result : register(u0);
[numthreads(64,1,1)] void main(uint3 id:SV_DispatchThreadID) {
    uint i=id.x;
    if (i >= 6144) return;
    const float rs[6] = {0.1,0.15,0.25,0.5,0.8,1.0};
    const float es[7] = {0.0,1e-9,1e-7,1e-6,1e-5,0.01,100.0};
    float r=rs[i/1024];
    float a2=r*r*r*r;
    float u=(float(i%1024)+0.5)/1024.0;
    float denom=1.0-(1.0-a2)*u;
    float w=a2*u/denom;
    float n=sqrt(saturate(1.0-w));
    float jacobian=a2/(denom*denom);
    Result[i]=float4(D_GGX(r,n)*Math::PI*jacobian,Vis_SmithJointApprox(r,n,n),Vis_SmithJoint(r,n,n),HomogeneousScatteringWeight(es[i%7],100));
    if (i < 7) {
        const float peaks[7]={0,0.01,0.04,0.1,0.15,0.5,1};
        float p=peaks[i];
        Result[6144+i]=float4(D_GGX(p,1),Vis_SmithJointApprox(p,1,1),Vis_SmithJoint(p,1,1),Vis_SmithJoint(p,0,0));
    }
})";
        ComPtr<ID3DBlob> blob, error;
        const auto hr = D3DCompile(shader.data(), shader.size(), "verbatim-boundary-fixture", nullptr, nullptr,
            "main", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &error);
        if (error) std::cerr.write(static_cast<const char*>(error->GetBufferPointer()), error->GetBufferSize());
        Check(hr);
        ComPtr<ID3D11Device> dev; ComPtr<ID3D11DeviceContext> ctx;
        Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev, nullptr, &ctx));
        ComPtr<ID3D11ComputeShader> cs;
        Check(dev->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &cs));
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = 6151 * 16; bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; bd.StructureByteStride = 16;
        ComPtr<ID3D11Buffer> output, readback; Check(dev->CreateBuffer(&bd, nullptr, &output));
        D3D11_UNORDERED_ACCESS_VIEW_DESC uv{}; uv.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; uv.Buffer.NumElements = 6151;
        ComPtr<ID3D11UnorderedAccessView> uav; Check(dev->CreateUnorderedAccessView(output.Get(), &uv, &uav));
        bd.Usage = D3D11_USAGE_STAGING; bd.BindFlags = 0; bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        Check(dev->CreateBuffer(&bd, nullptr, &readback));
        ctx->CSSetShader(cs.Get(), nullptr, 0); auto* raw = uav.Get(); ctx->CSSetUnorderedAccessViews(0, 1, &raw, nullptr);
        ctx->Dispatch(96, 1, 1); raw = nullptr; ctx->CSSetUnorderedAccessViews(0, 1, &raw, nullptr); ctx->CopyResource(readback.Get(), output.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{}; Check(ctx->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped));
        const auto* data = static_cast<const float*>(mapped.pData);
        constexpr double pi = 3.14159265358979323846;
        const double extinctions[7]{0,1e-9,1e-7,1e-6,1e-5,0.01,100};
        for (unsigned group=0; group<6; ++group) {
            double integral=0;
            for (unsigned sample=0; sample<1024; ++sample) {
                const unsigned i=group*1024+sample;
                const auto* p=data+i*4;
                for (unsigned k=0;k<4;++k) if (!std::isfinite(p[k]) || p[k]<0) throw std::runtime_error("Nonfinite/negative transport");
                integral+=p[0]/1024.0;
                const double e=extinctions[i%7];
                const double exact=e==0?100.0:-std::expm1(-e*100.0)/e;
                if (std::abs(p[3]-exact)>std::max(1e-7,exact*2e-5)) throw std::runtime_error("Homogeneous integral mismatch");
            }
            if (std::abs(integral-1)>0.003) throw std::runtime_error("GGX projected integral is not normalized");
            std::cout << "GGX projected integral " << group << ": " << integral << '\n';
        }
        const double roughness[7]{0,0.01,0.04,0.1,0.15,0.5,1};
        for (unsigned i=0;i<7;++i) {
            const auto* p=data+(6144+i)*4;
            const double alpha=std::max(roughness[i]*roughness[i],1e-4);
            const double expected=1/(pi*alpha*alpha);
            if (!std::isfinite(p[0]) || std::abs(p[0]/expected-1)>1e-5) throw std::runtime_error("Smooth GGX peak shape mismatch");
            if (std::abs(p[1]-0.25f)>1e-6 || std::abs(p[2]-0.25f)>1e-6 || !std::isfinite(p[3])) throw std::runtime_error("Smith visibility boundary mismatch");
        }
        ctx->Unmap(readback.Get(), 0);
        std::cout << "PASS: actual HLSL on DX11 WARP, 6144 projected GGX/fog samples plus 7 smooth/grazing boundaries.\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
