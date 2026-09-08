[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$VcVars)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$vcVarsPath = (Resolve-Path -LiteralPath $VcVars).Path
$output = Join-Path $repo ('build\rcas-shader-tests-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null
$shader = Join-Path $repo 'pipeline\ImageReconstruction\Kernels\ImageReconstruction\RCAS\RCAS.hlsl'
# WARP is a local D3D11 software device. No game launch, live writes or timing claims.
$source = @'
#define NOMINMAX
#include <d3d11.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;
void Check(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("D3D call failed: " + std::to_string(hr)); }
struct Pixel { float r, g, b, a; };
struct Config { float sharpness; uint32_t useConfidence; float width, height; };
static_assert(sizeof(Config) == 16);
int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    ComPtr<ID3DBlob> code, errors;
    const HRESULT compiled = D3DCompileFromFile(argv[1], nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "main", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
        0, &code, &errors);
    if (errors) std::cerr << static_cast<const char*>(errors->GetBufferPointer());
    Check(compiled);
    ComPtr<ID3D11ShaderReflection> reflection;
    Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(&reflection)));
    D3D11_SHADER_BUFFER_DESC cbDesc{};
    auto* reflectedCB = reflection->GetConstantBufferByName("RCASConfig");
    Check(reflectedCB->GetDesc(&cbDesc));
    assert(cbDesc.Size == sizeof(Config) && cbDesc.Variables == 3);
    const char* names[] = {"sharpness", "useConfidence", "inputDimensions"};
    const UINT offsets[] = {0, 4, 8};
    const UINT sizes[] = {4, 4, 8};
    for (unsigned i = 0; i < 3; ++i) {
        D3D11_SHADER_VARIABLE_DESC variable{};
        Check(reflectedCB->GetVariableByName(names[i])->GetDesc(&variable));
        assert(variable.StartOffset == offsets[i] && variable.Size == sizes[i]);
    }
    const char* bindings[] = {"Source", "ReactiveMask", "TransparencyMask", "MotionVectors", "Dest", "RCASConfig"};
    const UINT slots[] = {0, 1, 2, 3, 0, 0};
    for (unsigned i = 0; i < 6; ++i) {
        D3D11_SHADER_INPUT_BIND_DESC binding{};
        Check(reflection->GetResourceBindingDescByName(bindings[i], &binding));
        assert(binding.BindPoint == slots[i] && binding.BindCount == 1);
    }
    UINT groupX = 0, groupY = 0, groupZ = 0;
    reflection->GetThreadGroupSize(&groupX, &groupY, &groupZ);
    assert(groupX == 8 && groupY == 8 && groupZ == 1);
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL level{};
    Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &device, &level, &context));
    assert(level >= D3D_FEATURE_LEVEL_11_0);
    ComPtr<ID3D11ComputeShader> shader;
    Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader));
    D3D11_BUFFER_DESC bufferDesc{};
    bufferDesc.ByteWidth = sizeof(Config); bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ComPtr<ID3D11Buffer> cb;
    Check(device->CreateBuffer(&bufferDesc, nullptr, &cb));
    constexpr UINT width = 17, height = 9, maskWidth = 8, maskHeight = 4;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width; desc.Height = height; desc.ArraySize = 1; desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> source;
    Check(device->CreateTexture2D(&desc, nullptr, &source));
    ComPtr<ID3D11ShaderResourceView> sourceView;
    Check(device->CreateShaderResourceView(source.Get(), nullptr, &sourceView));
    desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    ComPtr<ID3D11Texture2D> dest;
    Check(device->CreateTexture2D(&desc, nullptr, &dest));
    ComPtr<ID3D11UnorderedAccessView> destView;
    Check(device->CreateUnorderedAccessView(dest.Get(), nullptr, &destView));
    desc.BindFlags = 0; desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> readback;
    Check(device->CreateTexture2D(&desc, nullptr, &readback));
    desc.Width = maskWidth; desc.Height = maskHeight; desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE; desc.CPUAccessFlags = 0;
    // Float4 mask storage is legal for scalar/float2 shader loads; only declared components are read.
    std::array<ComPtr<ID3D11Texture2D>, 3> masks;
    std::array<ComPtr<ID3D11ShaderResourceView>, 3> maskViews;
    for (unsigned i = 0; i < 3; ++i) {
        Check(device->CreateTexture2D(&desc, nullptr, &masks[i]));
        Check(device->CreateShaderResourceView(masks[i].Get(), nullptr, &maskViews[i]));
    }
    std::vector<Pixel> input(width * height), mask(maskWidth * maskHeight);
    unsigned cases = 0;
    auto run = [&](const Config& config, float reactive, float transparency, float motion, bool identity) {
        context->UpdateSubresource(source.Get(), 0, nullptr, input.data(), width * sizeof(Pixel), 0);
        const float values[] = {reactive, transparency, motion};
        for (unsigned i = 0; i < 3; ++i) {
            std::fill(mask.begin(), mask.end(), Pixel{values[i], values[i], 0, 0});
            context->UpdateSubresource(masks[i].Get(), 0, nullptr, mask.data(), maskWidth * sizeof(Pixel), 0);
        }
        context->UpdateSubresource(cb.Get(), 0, nullptr, &config, 0, 0);
        ID3D11Buffer* buffers[] = {cb.Get()};
        context->CSSetConstantBuffers(0, 1, buffers);
        ID3D11ShaderResourceView* views[] = {sourceView.Get(), maskViews[0].Get(), maskViews[1].Get(), maskViews[2].Get()};
        context->CSSetShaderResources(0, 4, views);
        ID3D11UnorderedAccessView* outputs[] = {destView.Get()};
        const float sentinel[] = {-999, -999, -999, -999};
        context->ClearUnorderedAccessViewFloat(destView.Get(), sentinel);
        context->CSSetUnorderedAccessViews(0, 1, outputs, nullptr);
        context->CSSetShader(shader.Get(), nullptr, 0);
        context->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
        ID3D11UnorderedAccessView* noOutput = nullptr;
        context->CSSetUnorderedAccessViews(0, 1, &noOutput, nullptr);
        ID3D11ShaderResourceView* noViews[4]{};
        context->CSSetShaderResources(0, 4, noViews);
        context->CopyResource(readback.Get(), dest.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Check(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped));
        for (UINT y = 0; y < height; ++y) {
            const auto* row = reinterpret_cast<const Pixel*>(static_cast<const char*>(mapped.pData) + y * mapped.RowPitch);
            for (UINT x = 0; x < width; ++x) {
                const Pixel result = row[x], expected = input[y * width + x];
                assert(std::isfinite(result.r) && std::isfinite(result.g) && std::isfinite(result.b) && result.a == 1);
                if (identity) {
                    const float tolerance = 1e-4f * std::max(1.0f, expected.b);
                    assert(std::abs(result.r - expected.r) < tolerance);
                    assert(std::abs(result.g - expected.g) < tolerance);
                    assert(std::abs(result.b - expected.b) < tolerance);
                }
            }
        }
        context->Unmap(readback.Get(), 0);
        ++cases;
    };
    for (float value : {0.0f, 0.25f, 1.0f, 2.0f, 128.0f}) {
        std::fill(input.begin(), input.end(), Pixel{value, value, value, 1});
        for (float attenuation : {0.0f, 0.5f, 1.0f}) run({attenuation, 0, 0, 0}, 0, 0, 0, true);
    }
    for (UINT i = 0; i < input.size(); ++i) {
        const float value = static_cast<float>((i * 17) % 31) / 31.0f;
        input[i] = {value, value * 2, value * 8, 1};
    }
    run({0, 0, 0, 0}, 0, 0, 0, true);
    run({1, 1, maskWidth, maskHeight}, 1, 0, 0, true);
    run({1, 1, maskWidth, maskHeight}, 0, 1, 0, true);
    run({1, 1, maskWidth, maskHeight}, 0, 0, 0, false);
    run({1, 1, maskWidth, maskHeight}, 0, 0, 100, true);
    std::cout << "PASS D3D11 WARP: " << cases << " cases, 17x9 edge/dispatch coverage, flat black/white/HDR, mask and motion rejection; reflected ABI/registers/group size match\n";
}
'@
$sourcePath = Join-Path $output 'rcas-shader-tests.cpp'
$source | Set-Content -LiteralPath $sourcePath -Encoding UTF8
Push-Location $output
try {
    $command = 'call "{0}" >nul && cl.exe /nologo /std:c++latest /EHsc /W4 /WX /MD "{1}" /Fe:rcas-shader-tests.exe /link d3d11.lib d3dcompiler.lib dxguid.lib && rcas-shader-tests.exe "{2}"' -f $vcVarsPath, $sourcePath, $shader
    & cmd.exe /d /c $command
    if ($LASTEXITCODE -ne 0) { throw "RCAS shader tests failed ($LASTEXITCODE)." }
} finally { Pop-Location }
Write-Host "Shader SHA256: $((Get-FileHash -LiteralPath $shader -Algorithm SHA256).Hash)"
Write-Host "WARP test artifacts: $output"
