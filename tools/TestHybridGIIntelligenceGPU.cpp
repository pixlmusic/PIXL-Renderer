#define NOMINMAX
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
using Microsoft::WRL::ComPtr;
static void Check(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("D3D failure: " + std::to_string(hr)); }
static void Require(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
int main()
{
    try {
        std::ifstream file("pipeline/Hybrid GI/Kernels/HybridGI/worldCache.hlsli", std::ios::binary);
        Require(bool(file), "Missing production helpers");
        std::string source{std::istreambuf_iterator<char>(file), {}};
        auto begin = source.find("#define WORLD_CACHE_DIM");
        auto end = source.find("uint WorldCacheHash(");
        Require(begin != std::string::npos && end > begin, "Helper extraction boundary changed");
        source = "#define CACHE_INTELLIGENCE\n" + source.substr(begin, end-begin) + R"(
RWStructuredBuffer<uint4> results : register(u0);
[numthreads(64,1,1)] void main(uint index : SV_DispatchThreadID) {
    float variance = exp2(float(index) * (16.0f / 4095.0f)) - 1.0f;
    uint state = (index / 16u) % 16u;
    uint age = (index / 288u) % 6u;
    bool matching = ((index / 144u) & 1u) != 0u;
    uint packed = PackWorldIntelligence(variance, 0.0f, index & 15u, state);
    uint resolved = WorldResolveEvidenceState(index & 15u, state, matching, age);
    results[index] = uint4(packed, asuint(WorldIntelligenceVariance(packed)), resolved,
        asuint(WorldIntelligenceReliability(packed)));
    float distance = float(index) / 4095.0f * 32.0f;
    float blend = float(index) / 4095.0f;
    results[index+4096] = asuint(float4(
        WorldCacheBlendDistance(float3(0,0,-distance), 32.0f * 0.47f),
        WorldCacheBlendDistance(float3(distance,0,0), 32.0f * 0.47f),
        WorldCascadeRadianceBlend(blend, 0.25f, 0.75f),
        WorldReceiverPlaneConfidence(float3(0,0,-distance), int3(0,0,0), float3(0,0,1), 16.0f, 1.0f)));
    uint timestamp = 251u;
    uint metadata = (timestamp << 24) | ((index & 1u) != 0u ? 1u : 0u);
    uint clock = timestamp + (index & 255u);
    uint material = WorldObservedMaterial(float2((index & 2u) != 0u ? 0.5f : 0.0f,
        (index & 4u) != 0u ? 0.5f : 0.0f));
    results[index+8192] = uint4(WorldCacheExpired(metadata, packed, clock, 48u),
        material, asuint(WorldMaterialResponse(material)), 0u);
    uint scheduleWord = (state << 28);
    uint scheduleAge = (index / 256u) % 3u;
    uint scheduleClock = (index / 64u) & 3u;
    uint hash = index & 63u;
    results[index+12288] = uint4(WorldCacheScheduleCell(hash, scheduleAge, 1.0f, scheduleWord, scheduleClock, 0.25f),
        WorldCacheScheduleCell(hash, 0u, 1.0f, scheduleWord | (1u<<16), scheduleClock, 0.25f),
        WorldCacheScheduleCell(hash, 0u, 1.0f, scheduleWord, scheduleClock, 1.0f),
        WorldCacheScheduleCell(hash, 0u, 0.2f, scheduleWord, scheduleClock, 0.25f));
    uint maximum = (index & 7u) + 1u;
    float demand = (index & 8u) != 0u ? 1.0f : 0.0f;
    uint rays = WorldCacheRayBudget(maximum, demand, 0.35f, index >> 4u, scheduleClock);
    uint mask = 0u;
    [loop] for (uint ray = 0u; ray < rays; ++ray)
        mask |= 1u << WorldCacheRayIndex(ray, rays, maximum);
    results[index+16384] = uint4(rays, mask,
        WorldCacheRayBudget(maximum, 1.0f, 0.35f, index >> 4u, scheduleClock), maximum);
})";
        ComPtr<ID3DBlob> bytecode, errors;
        auto hr = D3DCompile(source.data(), source.size(), "production-cache-intelligence", nullptr, nullptr,
            "main", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
            0, &bytecode, &errors);
        if (FAILED(hr) && errors) std::cerr << static_cast<const char*>(errors->GetBufferPointer());
        Check(hr);
        ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
        Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context));
        ComPtr<ID3D11ComputeShader> shader;
        Check(device->CreateComputeShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, &shader));
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = 20480 * 16; desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS; desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; desc.StructureByteStride = 16;
        ComPtr<ID3D11Buffer> output, staging;
        Check(device->CreateBuffer(&desc,nullptr,&output));
        D3D11_UNORDERED_ACCESS_VIEW_DESC view{};
        view.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; view.Buffer.NumElements = 20480;
        ComPtr<ID3D11UnorderedAccessView> uav;
        Check(device->CreateUnorderedAccessView(output.Get(),&view,&uav));
        desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ; desc.MiscFlags=0;
        Check(device->CreateBuffer(&desc,nullptr,&staging));
        auto* raw=uav.Get(); context->CSSetUnorderedAccessViews(0,1,&raw,nullptr); context->CSSetShader(shader.Get(),nullptr,0);
        context->Dispatch(64,1,1); raw=nullptr; context->CSSetUnorderedAccessViews(0,1,&raw,nullptr);
        context->CopyResource(staging.Get(),output.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{}; Check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped));
        const auto* data=static_cast<const std::array<std::uint32_t,4>*>(mapped.pData);
        for (std::uint32_t i=0;i<4096;++i) {
            const auto state=(i/16)%16; const auto age=(i/288)%6; const bool matching=((i/144)&1)!=0;
            Require((data[i][0]>>28)==state && ((data[i][0]>>24)&15)==(i&15), "GPU packed field overlap");
            float variance{}, reliability{}; memcpy(&variance,&data[i][1],4); memcpy(&reliability,&data[i][3],4);
            const double expected=std::exp2(double(i)*16/4095)-1;
            Require(std::abs((variance+1)/(expected+1)-1)<0.00010, "GPU quantization error");
            if (state==0 || state==1 || state>=6) Require(reliability==0, "Non-radiant state emitted confidence");
            else Require(reliability>0 && std::isfinite(reliability), "Dark/occupied state lost reliability");
            const auto bits=i&15;
            std::uint32_t resolved=0xffffffffu;
            if (!(matching && state>=2 && state<=5) && !(matching && age==0)) {
                const bool hit=(bits&12)==12, empty=(bits&3)==3 && (bits&12)==0;
                if(hit || empty) {
                    const std::uint32_t pending=hit?8u:7u, confirmed=hit?6u:1u;
                    resolved=matching && age<=4 && (state==pending || state==confirmed)?confirmed:pending;
                }
            }
            Require(data[i][2]==resolved, "GPU evidence state differs from oracle");
            std::array<float,4> spatial{};
            memcpy(spatial.data(), data[i+4096].data(), sizeof(spatial));
            const float blend = float(i)/4095.0f;
            const float distance = blend * 32.0f;
            Require(spatial[0] <= spatial[1]+1e-6f, "Vertical detail handed off before horizontal detail");
            if (distance >= 16.0f) Require(spatial[0] == 1 && spatial[1] == 1, "Near window edge not fully handed off");
            Require(std::abs(spatial[2] - 3*blend/(1+2*blend)) < 2e-6f, "Cascade reliability normalization differs from oracle");
            Require(spatial[3] >= 0 && spatial[3] <= 1, "Receiver visibility out of range");
            if (distance == 0) Require(spatial[3]==1, "Cell-edge plane falsely rejected");
            if (distance >= 8) Require(spatial[3]==0, "Contradictory plane not rejected");
            const auto lifetime = state==1 || (state>=6 && state<=8) ? 4u : 48u;
            Require(data[i+8192][0]==std::uint32_t((i&1) && (i&255)>lifetime), "Expiry/sentinel/wrap differs from oracle");
            const auto material = (i&2) ? 2u : (i&4) ? 1u : 0u;
            Require(data[i+8192][1]==material, "Opaque material classification differs from producer semantics");
            float response{}; memcpy(&response,&data[i+8192][2],4);
            Require(response == (material==1 ? 0.28f : material==2 ? 0.18f : 0.0f), "Neutral material altered response");
            const bool scheduled = state!=2 && state!=4 || ((i/256)%3)>=2 || (((i&63)+((i/64)&3)*4)&15)<4;
            Require(data[i+12288][0]==std::uint32_t(scheduled), "World-space schedule differs from oracle");
            Require(data[i+12288][1]==1 && data[i+12288][2]==1 && data[i+12288][3]==1, "Changed/uncertain/full-budget priority lost");
            const unsigned maximum=(i&7)+1;
            const bool exploration=(((i>>4)+((i/64)&3))&3)==0;
            const unsigned rays=(i&8)||exploration?maximum:(maximum+1)/2;
            Require(data[i+16384][0]==rays && data[i+16384][2]==maximum, "Adaptive cache ray budget or exploration violated");
            unsigned mask=0;
            for(unsigned ray=0;ray<rays;++ray) mask|=1u<<((2*ray+1)*maximum/(2*rays));
            Require(data[i+16384][1]==mask, "Adaptive subset changed reference ray indexing");
        }
        context->Unmap(staging.Get(),0);
        std::cout << "PASS: 4096 WARP production-helper quantization, state, confidence, evidence, cascade and receiver-plane cases\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
