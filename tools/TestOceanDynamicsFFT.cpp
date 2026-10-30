#define NOMINMAX
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
#include "../engine/Modules/OceanDynamicsShore.h"
using Microsoft::WRL::ComPtr;
namespace {
constexpr UINT kResolution = 128;
constexpr UINT kCascades = 4;
void Check(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("DX11 failure " + std::to_string(hr)); }
std::string Read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot open Ocean Dynamics shader: " + path.string());
    return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
}
ComPtr<ID3D11ComputeShader> Compile(ID3D11Device* device, const std::string& source, const char* entry) {
    ComPtr<ID3DBlob> code, errors;
    const HRESULT hr = D3DCompile(source.data(), source.size(), "OceanDynamics WARP validation", nullptr, nullptr,
        entry, "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS |
            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    if (errors) std::cerr.write(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
    Check(hr);
    ComPtr<ID3D11ComputeShader> result;
    Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &result));
    return result;
}
struct Texture {
    ComPtr<ID3D11Texture2D> resource;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11UnorderedAccessView> uav;
};
Texture MakeTexture(ID3D11Device* device, UINT bindFlags) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = desc.Height = kResolution;
    desc.MipLevels = 1;
    desc.ArraySize = kCascades;
    desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = bindFlags;
    Texture out;
    Check(device->CreateTexture2D(&desc, nullptr, &out.resource));
    if (bindFlags & D3D11_BIND_SHADER_RESOURCE) Check(device->CreateShaderResourceView(out.resource.Get(), nullptr, &out.srv));
    if (bindFlags & D3D11_BIND_UNORDERED_ACCESS) Check(device->CreateUnorderedAccessView(out.resource.Get(), nullptr, &out.uav));
    return out;
}
struct SimulationConstants {
    float time, windSpeed, windDirection, swellHeight, swellPeriod, swellDirection, choppiness, resolution, inverseResolution;
    float deltaSeconds, foamLifetime, foamStrength;
    float cascadeExtent[4];
    float weatherEnergy;
    float padding[3];
};
static_assert(sizeof(SimulationConstants) == 80);
struct FFTConstants { UINT resolution, vertical, padding[2]; };
static_assert(sizeof(FFTConstants) == 16);
ComPtr<ID3D11Buffer> MakeBuffer(ID3D11Device* device, const void* data, UINT bytes) {
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = bytes; desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA init{ data, 0, 0 };
    ComPtr<ID3D11Buffer> buffer;
    Check(device->CreateBuffer(&desc, &init, &buffer));
    return buffer;
}
std::vector<float> ReadbackArray(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* texture) {
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    Check(device->CreateTexture2D(&desc, nullptr, &staging));
    context->CopyResource(staging.Get(), texture);
    std::vector<float> values(std::size_t(kResolution) * kResolution * kCascades * 4);
    for (UINT slice = 0; slice < kCascades; ++slice) {
        D3D11_MAPPED_SUBRESOURCE map{};
        Check(context->Map(staging.Get(), D3D11CalcSubresource(0, slice, 1), D3D11_MAP_READ, 0, &map));
        for (UINT y = 0; y < kResolution; ++y) {
            auto* dst = values.data() + ((std::size_t(slice) * kResolution + y) * kResolution * 4);
            const auto* src = static_cast<const float*>(static_cast<const void*>(static_cast<const char*>(map.pData) + y * map.RowPitch));
            std::copy_n(src, kResolution * 4, dst);
        }
        context->Unmap(staging.Get(), D3D11CalcSubresource(0, slice, 1));
    }
    return values;
}
void BindUAVs(ID3D11DeviceContext* context, ID3D11UnorderedAccessView* a, ID3D11UnorderedAccessView* b = nullptr) {
    ID3D11UnorderedAccessView* views[2]{ a, b };
    UINT counts[2]{ 0xffffffffu, 0xffffffffu };
    context->CSSetUnorderedAccessViews(0, b ? 2 : 1, views, counts);
}
void BindSRVs(ID3D11DeviceContext* context, ID3D11ShaderResourceView* a, ID3D11ShaderResourceView* b = nullptr) {
    ID3D11ShaderResourceView* views[2]{ a, b };
    context->CSSetShaderResources(0, b ? 2 : 1, views);
}
void ClearCSBindings(ID3D11DeviceContext* context) {
    ID3D11UnorderedAccessView* uavs[2]{};
    ID3D11ShaderResourceView* srvs[2]{};
    context->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
    context->CSSetShaderResources(0, 2, srvs);
    context->CSSetShader(nullptr, nullptr, 0);
}
}
#include "OceanGeometryValidation.h"
int main() {
    try {
        const float deepRatio = OceanShore::DeepWaterRatio(12000.0f, 8.5f);
        const float shallowRatio = OceanShore::DeepWaterRatio(80.0f, 8.5f);
        const float deepScale = OceanShore::ShoalingScale(12000.0f, 8.5f, 1.0f);
        const float shallowScale = OceanShore::ShoalingScale(80.0f, 8.5f, 1.0f);
        if (!std::isfinite(deepRatio) || !std::isfinite(shallowRatio) ||
            deepRatio < 0.99f || !(shallowRatio < deepRatio) ||
            std::abs(deepScale - 1.0f) > 1.0e-4f ||
            !(shallowScale > deepScale && shallowScale <= 1.45f))
            throw std::runtime_error("Finite-depth dispersion/shoaling bounds failed");
        const auto trough = OceanShore::BreakerActivation(120.0f, -20.0f, 0.08f, 0.1f, 1.0f);
        const auto deepCrest = OceanShore::BreakerActivation(4000.0f, 30.0f, 0.08f, 0.1f, 1.0f);
        const auto shallowCrest = OceanShore::BreakerActivation(120.0f, 30.0f, 0.08f, 0.1f, 1.0f);
        if (trough != 0.0f || deepCrest != 0.0f || !(shallowCrest > 0.0f && shallowCrest <= 1.0f))
            throw std::runtime_error("Crest/depth breaker activation failed");
        for (const float h : { -1000.0f, -42.0f, 0.0f, 42.0f, 1000.0f }) {
            const float transformed = OceanShore::TransformHeight(h, 60.0f, 8.5f, 1.0f, 1.0f);
            const float depthBound = std::clamp(60.0f * 0.14f + 2.0f, 2.0f, 42.0f);
            if (!std::isfinite(transformed) || std::abs(transformed) > depthBound)
                throw std::runtime_error("Shore transformation exceeded safe displacement bounds");
        }
        const float deepWave = std::abs(OceanShore::TransformHeight(30.0f, 5000.0f, 8.5f, 1.0f, 1.0f));
        const float shallowWave = std::abs(OceanShore::TransformHeight(30.0f, 80.0f, 8.5f, 1.0f, 1.0f));
        const float surfWave = std::abs(OceanShore::TransformHeight(30.0f, 500.0f, 8.5f, 1.0f, 1.0f));
        const float wetEdgeWave = std::abs(OceanShore::TransformHeight(30.0f, 0.0f, 8.5f, 1.0f, 1.0f));
        if (!(wetEdgeWave < shallowWave && shallowWave < surfWave && surfWave <= 42.0f &&
            surfWave < deepWave * 1.5f))
            throw std::runtime_error("Shallow shorebreak did not attenuate the offshore swell");
        const float shallowTrough = OceanShore::TransformHeight(-30.0f, 80.0f, 8.5f, 1.0f, 1.0f);
        const float deepTrough = OceanShore::TransformHeight(-30.0f, 5000.0f, 8.5f, 1.0f, 1.0f);
        if (!(shallowTrough < 0.0f && std::abs(shallowTrough) < std::abs(deepTrough)) ||
            OceanShore::TransformHeight(30.0f, -50.0f, 8.5f, 1.0f, 0.0f) != 0.0f ||
            OceanShore::TransformHeight(30.0f, 80.0f, 8.5f, 1.5f, 1.0f) < 0.0f)
            throw std::runtime_error("Phase-locked shorebreak energy or dry-land bounds failed");
        // Shoreline phase remains world anchored in the shipping water shader.
        // Terrain gradients orient the fitted displacement cage and breaker foam,
        // but do not refract the FFT sampling coordinates because that caused
        // visible crest jumps at heightmap tile and gradient transitions.
        std::cout << "Shore dispersion, shoaling, crest gating and displacement bounds passed\n";
        const auto shaderSource = Read("pipeline/Ocean Dynamics/Kernels/OceanDynamics/OceanDynamics.hlsl");
        ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
        Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
            &device, nullptr, &context));
        auto initCS = Compile(device.Get(), shaderSource, "SpectrumInit");
        auto updateCS = Compile(device.Get(), shaderSource, "SpectrumUpdate");
        auto fftCS = Compile(device.Get(), shaderSource, "ComputeFFT");
        auto packCS = Compile(device.Get(), shaderSource, "PackDisplacement");
        auto foamCS = Compile(device.Get(), shaderSource, "UpdateFoam");
        auto initial = MakeTexture(device.Get(), D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);
        auto spectrumA = MakeTexture(device.Get(), D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);
        auto spectrumB = MakeTexture(device.Get(), D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);
        auto scratchA = MakeTexture(device.Get(), D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);
        auto scratchB = MakeTexture(device.Get(), D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);
        auto displacement = MakeTexture(device.Get(), D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);
        auto foamA = MakeTexture(device.Get(), D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);
        auto foamB = MakeTexture(device.Get(), D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);
        SimulationConstants sim{ 0.0f, 8.0f, 0.72f, 1.15f, 8.5f, 0.72f, 0.7f,
            float(kResolution), 1.0f / float(kResolution), 0.0f, 5.5f, 0.65f,
            { 256.0f, 1024.0f, 4096.0f, 32768.0f }, 1.0f, {} };
        auto simBuffer = MakeBuffer(device.Get(), &sim, sizeof(sim));
        ID3D11Buffer* rawSim = simBuffer.Get();
        context->CSSetConstantBuffers(0, 1, &rawSim);
        context->CSSetShader(initCS.Get(), nullptr, 0); BindUAVs(context.Get(), initial.uav.Get());
        context->Dispatch(kResolution / 8, kResolution / 8, kCascades);
        ClearCSBindings(context.Get());

        struct SimulationOutput {
            std::vector<float> displacement;
            std::vector<float> evolvedSpectrum;
        };
        const auto simulate = [&](float time) {
            sim.time = time;
            context->UpdateSubresource(simBuffer.Get(), 0, nullptr, &sim, 0, 0);
            context->CSSetConstantBuffers(0, 1, &rawSim);
            context->CSSetShader(updateCS.Get(), nullptr, 0);
            BindSRVs(context.Get(), initial.srv.Get());
            BindUAVs(context.Get(), spectrumA.uav.Get(), spectrumB.uav.Get());
            context->Dispatch(kResolution / 8, kResolution / 8, kCascades);
            ClearCSBindings(context.Get());
            // Hermitian symmetry is a frequency-domain invariant. Capture the
            // evolved field before either inverse FFT transforms it to space.
            auto evolvedSpectrum = ReadbackArray(device.Get(), context.Get(), spectrumA.resource.Get());

            FFTConstants fft{ kResolution, 0, {} };
            auto fftBuffer = MakeBuffer(device.Get(), &fft, sizeof(fft));
            ID3D11Buffer* rawFFT = fftBuffer.Get();
            for (UINT vertical = 0; vertical < 2; ++vertical) {
                fft.vertical = vertical;
                context->UpdateSubresource(fftBuffer.Get(), 0, nullptr, &fft, 0, 0);
                context->CSSetConstantBuffers(1, 1, &rawFFT);
                context->CSSetShader(fftCS.Get(), nullptr, 0);
                BindSRVs(context.Get(), vertical == 0 ? spectrumA.srv.Get() : scratchA.srv.Get(),
                    vertical == 0 ? spectrumB.srv.Get() : scratchB.srv.Get());
                BindUAVs(context.Get(), vertical == 0 ? scratchA.uav.Get() : spectrumA.uav.Get(),
                    vertical == 0 ? scratchB.uav.Get() : spectrumB.uav.Get());
                context->Dispatch(1, kResolution, kCascades);
                ClearCSBindings(context.Get());
            }
            fft.vertical = 0;
            context->UpdateSubresource(fftBuffer.Get(), 0, nullptr, &fft, 0, 0);
            context->CSSetConstantBuffers(0, 1, &rawSim);
            context->CSSetConstantBuffers(1, 1, &rawFFT);
            context->CSSetShader(packCS.Get(), nullptr, 0);
            BindSRVs(context.Get(), spectrumA.srv.Get(), spectrumB.srv.Get());
            BindUAVs(context.Get(), displacement.uav.Get());
            context->Dispatch(kResolution / 8, kResolution / 8, kCascades);
            ClearCSBindings(context.Get());
            return SimulationOutput{ ReadbackArray(device.Get(), context.Get(), displacement.resource.Get()), std::move(evolvedSpectrum) };
        };

        const auto at = [](const std::vector<float>& values, UINT cascade, UINT x, UINT y, UINT channel) {
            return values[((std::size_t(cascade) * kResolution + y) * kResolution + x) * 4 + channel];
        };
        const auto t0 = simulate(0.0f);
        double mean = 0.0, squareMean = 0.0;
        float maxHeight = 0.0f;
        for (UINT c = 0; c < kCascades; ++c) for (UINT y = 0; y < kResolution; ++y) for (UINT x = 0; x < kResolution; ++x) {
            const float h = at(t0.displacement, c, x, y, 2);
            if (!std::isfinite(h)) throw std::runtime_error("FFT produced NaN/Infinity height");
            mean += h; squareMean += double(h) * h; maxHeight = std::max(maxHeight, std::abs(h));
        }
        const double count = double(kCascades) * kResolution * kResolution;
        mean /= count; squareMean /= count;
        const double variance = squareMean - mean * mean;
        std::cout << "FFT diagnostics: max height=" << maxHeight << " variance=" << variance << '\n';
        for (UINT c = 0; c < kCascades; ++c) {
            float cascadeMax = 0.0f;
            double cascadeSquare = 0.0;
            for (UINT y = 0; y < kResolution; ++y) for (UINT x = 0; x < kResolution; ++x) {
                const float h = at(t0.displacement, c, x, y, 2);
                cascadeMax = std::max(cascadeMax, std::abs(h));
                cascadeSquare += double(h) * h;
            }
            std::cout << "Cascade " << c << ": max=" << cascadeMax << " RMS=" << std::sqrt(cascadeSquare / (kResolution * kResolution)) << '\n';
        }
        if (!(variance > 1e-5 && maxHeight > 0.01f && maxHeight < 100.0f))
            throw std::runtime_error("FFT output is flat or outside safe wave-height bounds");

        const auto& spectrum = t0.evolvedSpectrum;
        for (UINT c = 0; c < kCascades; ++c) for (UINT y = 0; y < kResolution; ++y) for (UINT x = 0; x < kResolution; ++x) {
            const UINT mx = (kResolution - x) % kResolution, my = (kResolution - y) % kResolution;
            const float hx = at(spectrum, c, x, y, 0), hy = at(spectrum, c, x, y, 1);
            const float mirrorX = at(spectrum, c, mx, my, 0), mirrorY = at(spectrum, c, mx, my, 1);
            const float error = std::max(std::abs(hx - mirrorX), std::abs(hy + mirrorY));
            if (error > std::max(1e-3f, maxHeight * 1e-3f)) throw std::runtime_error("Evolved spectrum lost Hermitian symmetry");
        }
        const auto later = simulate(12.0f);
        float temporalDelta = 0.0f;
        for (std::size_t i = 0; i < t0.displacement.size(); i += 4)
            temporalDelta = std::max(temporalDelta, std::abs(t0.displacement[i + 2] - later.displacement[i + 2]));
        if (!(temporalDelta > 1e-4f)) throw std::runtime_error("Ocean phase did not evolve over elapsed time");
        sim.deltaSeconds = 1.0f / 30.0f;
        sim.foamLifetime = 5.5f;
        sim.foamStrength = 0.65f;
        context->UpdateSubresource(simBuffer.Get(), 0, nullptr, &sim, 0, 0);
        context->CSSetConstantBuffers(0, 1, &rawSim);
        ID3D11ShaderResourceView* foamInputs[]{ foamA.srv.Get(), displacement.srv.Get(), displacement.srv.Get() };
        context->CSSetShaderResources(0, 3, foamInputs);
        ID3D11UnorderedAccessView* foamUAV = foamB.uav.Get();
        context->CSSetUnorderedAccessViews(0, 1, &foamUAV, nullptr);
        D3D11_SAMPLER_DESC samplerDesc{};
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
        ComPtr<ID3D11SamplerState> sampler;
        Check(device->CreateSamplerState(&samplerDesc, &sampler));
        ID3D11SamplerState* rawSampler = sampler.Get();
        context->CSSetSamplers(0, 1, &rawSampler);
        context->CSSetShader(foamCS.Get(), nullptr, 0);
        context->Dispatch(kResolution / 8, kResolution / 8, kCascades);
        ClearCSBindings(context.Get());
        ID3D11SamplerState* noSampler = nullptr;
        context->CSSetSamplers(0, 1, &noSampler);
        const auto foam = ReadbackArray(device.Get(), context.Get(), foamB.resource.Get());
        float maximumFoam = 0.0f;
        for (std::size_t i = 0; i < foam.size(); i += 4) {
            if (!std::isfinite(foam[i]) || foam[i] < 0.0f || foam[i] > 1.0f)
                throw std::runtime_error("Persistent ocean foam produced invalid or unbounded values");
            maximumFoam = std::max(maximumFoam, foam[i]);
        }
        if (!(maximumFoam > 1.0e-4f))
            throw std::runtime_error("Persistent ocean foam remained empty on the validation sea state");
        std::cout << "WARP FFT: Hermitian symmetry, finite bounded height, non-flat displacement, and time evolution passed; max height="
                  << maxHeight << " temporal delta=" << temporalDelta << '\n';
        std::cout << "WARP foam: UpdateFoam compiled and produced finite values in [0,1]; maximum=" << maximumFoam << '\n';
        ValidateOceanGeometry(device.Get(), context.Get(), displacement.srv.Get());
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
