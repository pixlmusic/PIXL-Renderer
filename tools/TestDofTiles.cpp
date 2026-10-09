#define NOMINMAX
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <filesystem>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;
static void Check(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("DX11 failure " + std::to_string(hr)); }
static std::string Read(const std::filesystem::path& path) { std::ifstream file(path); if (!file) throw std::runtime_error("Missing shader"); return {std::istreambuf_iterator<char>(file),{}}; }
int main()
{
    try {
        const auto physicalCoCDiameterPixels = [](float depthMm, float focusMm, float focalMm,
                                                   float fStop, float sensorHeightMm, float renderHeight) {
            const float aperture = focalMm / fStop;
            const float circleMm = aperture * focalMm * (depthMm - focusMm) /
                (depthMm * (focusMm - focalMm));
            return circleMm / sensorHeightMm * renderHeight;
        };
        constexpr float focalMm = 50.0f, focusMm = 3000.0f, fStop = 2.8f;
        const float nearCoC = physicalCoCDiameterPixels(1800.0f, focusMm, focalMm, fStop, 24.0f, 1080.0f);
        const float focusCoC = physicalCoCDiameterPixels(focusMm, focusMm, focalMm, fStop, 24.0f, 1080.0f);
        const float farCoC = physicalCoCDiameterPixels(6000.0f, focusMm, focalMm, fStop, 24.0f, 1080.0f);
        if (!(nearCoC < 0.0f && std::abs(focusCoC) < 1.0e-6f && farCoC > 0.0f))
            throw std::runtime_error("Thin-lens CoC sign/focus fixture failed");
        const float farCoC4K = physicalCoCDiameterPixels(6000.0f, focusMm, focalMm, fStop, 24.0f, 2160.0f);
        if (std::abs(farCoC4K - 2.0f * farCoC) > 1.0e-5f)
            throw std::runtime_error("Thin-lens CoC did not scale with render height");
        if (std::abs((farCoC * 0.5f) * 2.0f - farCoC) > 1.0e-6f)
            throw std::runtime_error("Full-resolution CoC diameter to half-resolution gather radius conversion failed");
        const std::filesystem::path shaders = "pipeline/Camera Suite/Kernels/CameraSuite";
        auto source = Read(shaders / "DOFTileClassifyCS.hlsl");
        const std::string include = "#include \"CameraSuite/DofControl.hlsli\"";
        source.replace(source.find(include), include.size(), Read(shaders / "DofControl.hlsli") + "\n");
        ComPtr<ID3DBlob> code, errors;
        const auto compiled = D3DCompile(source.data(), source.size(), "DOF tile fixture", nullptr, nullptr, "main", "cs_5_0",
            D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_WARNINGS_ARE_ERRORS,0,&code,&errors);
        if (errors) std::cerr << static_cast<const char*>(errors->GetBufferPointer());
        Check(compiled);
        ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
        Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context));
        ComPtr<ID3D11ComputeShader> shader; Check(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader));
        constexpr UINT width=33,height=17;
        std::vector<float> coc(width*height,0);
        coc[3*width+3]=-12; coc[15*width+15]=8; coc[16*width+32]=-7;
        D3D11_TEXTURE2D_DESC desc{}; desc.Width=width; desc.Height=height; desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
        desc.Format=DXGI_FORMAT_R32_FLOAT; desc.Usage=D3D11_USAGE_DEFAULT; desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA initial{coc.data(),width*sizeof(float),0};
        ComPtr<ID3D11Texture2D> input; Check(device->CreateTexture2D(&desc,&initial,&input));
        ComPtr<ID3D11ShaderResourceView> srv; Check(device->CreateShaderResourceView(input.Get(),nullptr,&srv));
        desc.Width=3;desc.Height=2;desc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;desc.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
        ComPtr<ID3D11Texture2D> output,readback;Check(device->CreateTexture2D(&desc,nullptr,&output));
        ComPtr<ID3D11UnorderedAccessView> uav;Check(device->CreateUnorderedAccessView(output.Get(),nullptr,&uav));
        desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        Check(device->CreateTexture2D(&desc,nullptr,&readback));
        float controls[32]{};controls[7]=20;
        D3D11_BUFFER_DESC cbDesc{};cbDesc.ByteWidth=sizeof(controls);cbDesc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;cbDesc.Usage=D3D11_USAGE_DEFAULT;
        D3D11_SUBRESOURCE_DATA cbData{controls,0,0};ComPtr<ID3D11Buffer> cb;Check(device->CreateBuffer(&cbDesc,&cbData,&cb));
        auto* rawCB=cb.Get();context->CSSetConstantBuffers(1,1,&rawCB);
        auto* rawSRV=srv.Get();context->CSSetShaderResources(0,1,&rawSRV);
        auto* rawUAV=uav.Get();context->CSSetUnorderedAccessViews(0,1,&rawUAV,nullptr);
        context->CSSetShader(shader.Get(),nullptr,0);context->Dispatch(3,2,1);
        rawUAV=nullptr;context->CSSetUnorderedAccessViews(0,1,&rawUAV,nullptr);context->CopyResource(readback.Get(),output.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};Check(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped));
        for(UINT y=0;y<2;++y) for(UINT x=0;x<3;++x) {
            const auto* value=reinterpret_cast<const float*>(static_cast<const char*>(mapped.pData)+y*mapped.RowPitch)+x*4;
            const float expectedMin=(x==0&&y==0)?-12.f:((x==2&&y==1)?-7.f:0.f);
            const float expectedMax=(x==0&&y==0)?8.f:((x==2&&y==1)?-7.f:0.f);
            if(value[0]!=expectedMin||value[1]!=expectedMax)throw std::runtime_error("Full-tile or partial-edge coverage failed");
        }
        context->Unmap(readback.Get(),0);
        std::cout << "WARP: full CoC tile coverage and partial viewport edges retained; thin-lens focus/sign/resolution fixtures passed\n";
        return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
