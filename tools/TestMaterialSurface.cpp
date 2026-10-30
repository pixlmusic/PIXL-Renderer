// Executes the production material helpers on DX11 WARP, including the CPU/HLSL profile contract.
#define NOMINMAX
#include <d3d11.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <wrl/client.h>
#include "MaterialForge/MaterialSurfaceProfile.h"
#include "MaterialForge/PhysicalMaterial.h"
#include "MaterialForge/FurEligibility.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <vector>
using Microsoft::WRL::ComPtr;
namespace
{
    void Check(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("DX11 failure " + std::to_string(hr)); }
    void Require(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
    std::string Read(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        Require(bool(file), "Missing production shader");
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }
    std::string Function(const std::string& source, const std::string& name) {
        const auto start = source.find("float " + name + "(");
        Require(start != std::string::npos, "Missing production math helper");
        auto end = source.find('{', start);
        int depth = 0;
        do { if (source[end] == '{') ++depth; if (source[end] == '}') --depth; ++end; }
        while (end < source.size() && depth);
        Require(depth == 0, "Unbalanced helper");
        return source.substr(start, end - start);
    }
    bool Near(float a, float b) { return std::abs(a - b) < 2e-5f; }
}
int main()
{
    try {
        const auto root = std::filesystem::current_path() / "distribution/PIXL-Shaders/Common";
        std::vector<std::uint32_t> profiles;
        for (const auto* layout : {"RMAOS", "ORM", "MRA", "RMA", "MaskMap"}) {
            MaterialSurface::Profile p; p.packedMapLayout = layout;
            profiles.push_back(MaterialSurface::Pack(p, true));
        }
        MaterialSurface::Profile custom;
        for (const auto* name : {"KhajiitEyeLeft", "TEETH", "MouthInterior", "Cornea", "textures/actors/canine/tongue.dds"})
            Require(PhysicalMaterial::IsNonFurAnatomy(name), "Anatomical fur veto missed");
        for (const auto* name : {"KhajiitBody", "WolfFur", "BearSkin", "FoxTail", ""})
            Require(!PhysicalMaterial::IsNonFurAnatomy(name), "Fur body incorrectly excluded");
        custom.receiveSnow = false;
        Require((MaterialSurface::Pack(custom, true) ^ profiles[0]) == 0x8000u, "Snow opt-out changed another field");
        custom.receiveSnow = true;
        profiles.push_back(MaterialSurface::Pack(custom, false));
        custom.wetResponse = 0; custom.porosity = 0;
        profiles.push_back(MaterialSurface::Pack(custom, true));
        custom.wetResponse = 2; custom.porosity = 1;
        profiles.push_back(MaterialSurface::Pack(custom, true));
        custom.wetResponse = std::numeric_limits<float>::quiet_NaN();
        custom.porosity = std::numeric_limits<float>::infinity();
        Require(MaterialSurface::Pack(custom, true) == profiles[0], "Nonfinite profile fallback");
        bool rejected = false;
        try { (void)MaterialSurface::LayoutIndex("unknown"); } catch (const std::invalid_argument&) { rejected = true; }
        Require(rejected, "Unknown layout was accepted");
        for (std::uint32_t wet = 0; wet <= 30; ++wet) {
            for (std::uint32_t pore = 0; pore <= 30; ++pore) {
                custom.wetResponse = static_cast<float>(wet) / 15.f;
                custom.porosity = static_cast<float>(pore) / 30.f;
                const auto packed = MaterialSurface::Pack(custom, true);
                Require(((packed >> 4) & 31) == wet && ((packed >> 9) & 31) == pore, "Profile quantization");
                const auto pair = packed | (profiles[4] << 16);
                Require((pair & 0xffffu) == packed && (pair >> 16) == profiles[4], "Terrain profile packing");
            }
        }
        std::string shader = Read(root / "MaterialSurface.hlsli") + "\n";
        shader += Function(Read(root / "BRDF.hlsli"), "RoughnessWithMaterialVariance") + "\n";
        auto physical = Read(root / "PhysicalMaterial.hlsli");
        const std::string include = "#include \"Common/MaterialForgeTuning.hlsli\"";
        const auto at = physical.find(include);
        Require(at != std::string::npos, "Missing tuning include");
        physical.replace(at, include.size(), Read(root / "MaterialForgeTuning.hlsli"));
        shader += physical + "\nstatic const uint Profiles[8] = {";
        for (const auto profile : profiles) shader += std::to_string(profile) + "u,";
        shader.pop_back(); shader += "};\n";
        shader += R"(
RWStructuredBuffer<float4> Result : register(u0);
[numthreads(64,1,1)] void main(uint3 tid:SV_DispatchThreadID) {
    uint i=tid.x;
    float t=float(i & 255u)/255.0;
    float4 value=0;
    if(i<8) value=MaterialSurface::DecodePackedMap(float4(.2,.4,.6,.8),Profiles[i]);
    else if(i<16) value=float4(MaterialSurface::WetProperties(Profiles[i-8],.5,0),0,0);
    else if(i<128) {
        float3 pos=float3(t*31-15.5,t*17-8.5,t*13-6.5);
        float4 n=MaterialSurface::NoiseGradient(pos);
        float step=.001;
        float3 fd=float3(MaterialSurface::NoiseGradient(pos+float3(step,0,0)).w-MaterialSurface::NoiseGradient(pos-float3(step,0,0)).w,
            MaterialSurface::NoiseGradient(pos+float3(0,step,0)).w-MaterialSurface::NoiseGradient(pos-float3(0,step,0)).w,
            MaterialSurface::NoiseGradient(pos+float3(0,0,step)).w-MaterialSurface::NoiseGradient(pos-float3(0,0,step)).w)/(2*step);
        value=float4(abs(n.xyz-fd),n.w);
    }
    else if(i<256) value=float4(MaterialSurface::SnowMask(0,1,1,t,0,0),MaterialSurface::SnowMask(1,1,0,t,0,0),MaterialSurface::SnowMask(1,1,1,t,0,0),MaterialSurface::SnowMask(t,1,1,.5,.1,0));
    else if(i<512) value=float4(MaterialSurface::FilmCoverage(t,0),MaterialSurface::FilmCoverage(t,1),MaterialSurface::FilmCoverage(t,.5),t);
    else if(i<768) value=float4(RoughnessWithMaterialVariance(t,0,0,1,.18),RoughnessWithMaterialVariance(t,1,1,0,.18),RoughnessWithMaterialVariance(t,.01,.02,1,.18),RoughnessWithMaterialVariance(t,100,100,1,.18));
    else if(i>=768 && i<772) {
        if(i==768) value.x=MaterialSurface::StableSnowNormalZ(.9,.2,1);
        if(i==769) value.x=MaterialSurface::StableSnowNormalZ(.9,.9,1);
        if(i==770) value.x=MaterialSurface::StableSnowNormalZ(.2,.9,1);
        if(i==771) value.x=MaterialSurface::StableSnowNormalZ(.9,.2,0);
        if(i==768) value.y=MaterialSurface::SurfaceMotionWeight(0.0.xxx,0.0.xxx,0);
        if(i==769) value.y=MaterialSurface::SurfaceMotionWeight(float3(.45,0,0),0.0.xxx,0);
        if(i==770) value.y=MaterialSurface::SurfaceMotionWeight(float3(.9,0,0),0.0.xxx,0);
        if(i==771) value.y=MaterialSurface::SurfaceMotionWeight(float3(.1,0,0),0.0.xxx,0);
        value.z=MaterialSurface::GroundSnowCompaction(.8,.5);
        value.w=i==769 ? MaterialSurface::GroundSnowCompaction(1,1) : MaterialSurface::GroundSnowCompaction(0,1);
    }
    else {
        PhysicalMaterial::Surface s=PhysicalMaterial::FromLegacy(.5.xxx,t*1024,1.0.xxx,1);
        value=float4(s.Roughness,s.F0.x,
            PhysicalMaterial::InferLegacyMetallic(.5.xxx,0.0.xxx,0,.2,0,1,0,1),
            MaterialSurface::AutomaticPorosity(t,1));
    }
    Result[i]=value;
})";
        ComPtr<ID3DBlob> bytecode, errors;
        const auto result = D3DCompile(shader.data(), shader.size(), "material-surface-contract", nullptr, nullptr,
            "main", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &bytecode, &errors);
        if (errors) std::cerr.write(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
        Check(result);
        ComPtr<ID3D11ShaderReflection> reflection;
        Check(D3DReflect(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), IID_PPV_ARGS(&reflection)));
        auto* tuning = reflection->GetConstantBufferByName("PIXLMaterialForgeLegacyTuningCB");
        D3D11_SHADER_BUFFER_DESC tuningDesc{};
        Check(tuning->GetDesc(&tuningDesc));
        Require(tuningDesc.Size == 176, "Material tuning cbuffer size mismatch");
        const char* fields[] = {"PIXLMF_EnableWeatherSnow", "PIXLMF_WeatherSnowCoverage", "PIXLMF_ProceduralDetailScale", "PIXLMF_IndoorDustStrength"};
        for (unsigned i = 0; i < 4; ++i) {
            D3D11_SHADER_VARIABLE_DESC variable{};
            Check(tuning->GetVariableByName(fields[i])->GetDesc(&variable));
            Require(variable.StartOffset == 112 + i * 16, "Material tuning field offset mismatch");
        }
        ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
        Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context));
        ComPtr<ID3D11ComputeShader> cs;
        Check(device->CreateComputeShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, &cs));
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = 1024 * 16; bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; bd.StructureByteStride = 16;
        ComPtr<ID3D11Buffer> output, readback; Check(device->CreateBuffer(&bd, nullptr, &output));
        D3D11_UNORDERED_ACCESS_VIEW_DESC ud{}; ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; ud.Buffer.NumElements = 1024;
        ComPtr<ID3D11UnorderedAccessView> uav; Check(device->CreateUnorderedAccessView(output.Get(), &ud, &uav));
        bd.Usage = D3D11_USAGE_STAGING; bd.BindFlags = 0; bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        Check(device->CreateBuffer(&bd, nullptr, &readback));
        context->CSSetShader(cs.Get(), nullptr, 0); auto* raw = uav.Get(); context->CSSetUnorderedAccessViews(0, 1, &raw, nullptr);
        context->Dispatch(16, 1, 1); raw = nullptr; context->CSSetUnorderedAccessViews(0, 1, &raw, nullptr);
        context->CopyResource(readback.Get(), output.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{}; Check(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped));
        const auto* data = static_cast<const float*>(mapped.pData);
        const float expected[6][4]={{.2f,.4f,.6f,.8f},{.4f,.6f,.2f,1},{.4f,.2f,.6f,1},{.2f,.4f,.6f,1},{.2f,.2f,.4f,1},{1,0,1,1}};
        for (unsigned i=0;i<1024;++i) {
            const auto* p=data+4*i;
            for(unsigned c=0;c<4;++c) Require(std::isfinite(p[c]) && p[c]>=0 && p[c]<=1.00001f + (i==15 ? 1.f : 0.f), "Unbounded material output");
            if(i<6) for(unsigned c=0;c<4;++c) Require(Near(p[c],expected[i][c]), "Packed map layout decode");
            if(i>=8 && i<14) Require(Near(p[0],1) && Near(p[1],.515f), "Default profile response");
            if(i==14) Require(Near(p[0],0) && Near(p[1],0), "Sealed dry override");
            if(i==15) Require(Near(p[0],2) && Near(p[1],1), "Porous wet override");
            if(i>=16 && i<128) Require(p[0]<.001f && p[1]<.001f && p[2]<.001f, "Procedural gradient differs from finite difference");
            if(i>=128 && i<256) {
                Require(Near(p[0],0) && Near(p[1],0) && Near(p[2],1), "Snow dry/roof/full-coverage contract");
                if(i>128) Require(p[3]>=data[4*(i-1)+3], "Nonmonotonic snow accumulation");
            }
            if(i>=768 && i<772) {
                const float expectedSlope[] = {.2f,.9f,.2f,.9f};
                const float expectedMotion[] = {0.0f,.5f,1.0f,0.0f};
                Require(Near(p[0], expectedSlope[i-768]), "Moving snow face stability");
                Require(Near(p[1], expectedMotion[i-768]), "Snow motion threshold");
                Require(Near(p[2], .4f) && Near(p[3], i==769 ? 1.0f : 0.0f), "Snow compaction coupling");
            }
            if(i>=256 && i<512) {
                Require(Near(p[0],p[3]) && p[1]<=p[2]+1e-5f && p[2]<=p[0]+1e-5f, "Absorption ordering");
                if(i>256) Require(p[1]>=data[4*(i-1)+1], "Nonmonotonic water film");
            }
            if(i>=512 && i<768) {
                const float t=static_cast<float>(i & 255u)/255.f;
                Require(Near(p[0],t) && Near(p[1],t), "AA changed a uniform or disabled surface");
                Require(p[2]>=t-1e-5f && p[3]>=p[2]-1e-5f, "AA variance ordering");
            }
            if(i>=772) Require(Near(p[1],.04f) && Near(p[2],0) && Near(p[3],0), "Legacy dielectric / conductor guard");
        }
        context->Unmap(readback.Get(),0);
        std::cout << "PASS: 961 CPU profile combinations and 1024 production HLSL samples on DX11 WARP.\n";
        return 0;
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
