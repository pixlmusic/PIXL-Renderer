#include "HybridGIGPUTestSupport.h"
#include <d3d11shader.h>
#include <cmath>

struct BlurIncludes final : ID3DInclude {
    HRESULT __stdcall Open(D3D_INCLUDE_TYPE, LPCSTR name, LPCVOID, LPCVOID* data, UINT* size) override {
        for (auto root : {"pipeline/Hybrid GI/Kernels/", "distribution/PIXL-Shaders/"}) {
            std::ifstream file(std::string(root) + name, std::ios::binary);
            if (!file) continue;
            std::string text{std::istreambuf_iterator<char>(file), {}};
            auto bytes = new char[text.size()];
            std::memcpy(bytes, text.data(), text.size()); *data = bytes; *size = UINT(text.size()); return S_OK;
        }
        return E_FAIL;
    }
    HRESULT __stdcall Close(LPCVOID data) override { delete[] static_cast<const char*>(data); return S_OK; }
};

int main(int argc, char** argv)
{
    try {
        ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
        Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &device, nullptr, &context));
        auto source = Read("pipeline/Hybrid GI/Kernels/HybridGI/blur.cs.hlsl");
        if (argc == 2 && std::string(argv[1]) == "--legacy-projection") {
            const std::string expression = "ViewToScreenPosition(viewSamplePos)";
            auto offset = source.find(expression);
            if (offset == std::string::npos) throw std::runtime_error("Missing projection control");
            source.replace(offset, expression.size(), "FrameBuffer::ViewToUV(viewSamplePos)");
        }
        BlurIncludes includes;
        ComPtr<ID3DBlob> code, errors;
        auto hr = D3DCompile(source.data(), source.size(), "production-blur", nullptr, &includes, "main", "cs_5_0",
            D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS, 0, &code, &errors);
        if (FAILED(hr) && errors) std::cerr << static_cast<const char*>(errors->GetBufferPointer());
        Check(hr);
        ComPtr<ID3D11ComputeShader> shader;
        Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader));
        ComPtr<ID3D11ShaderReflection> reflection;
        Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(&reflection)));
        D3D11_SHADER_DESC desc{}; Check(reflection->GetDesc(&desc));
        std::vector<ComPtr<ID3D11Buffer>> buffers;
        for (UINT i = 0; i < desc.BoundResources; ++i) {
            D3D11_SHADER_INPUT_BIND_DESC binding{}; Check(reflection->GetResourceBindingDesc(i, &binding));
            if (binding.Type != D3D_SIT_CBUFFER) continue;
            auto cb = reflection->GetConstantBufferByName(binding.Name);
            D3D11_SHADER_BUFFER_DESC cd{}; Check(cb->GetDesc(&cd));
            std::vector<char> data(cd.Size);
            auto set = [&](const char* name, auto value) {
                D3D11_SHADER_VARIABLE_DESC v{};
                if (SUCCEEDED(cb->GetVariableByName(name)->GetDesc(&v)))
                    std::memcpy(data.data() + v.StartOffset, &value, sizeof(value));
            };
            set("FrameDim", std::array<float,2>{64,64}); set("RcpFrameDim", std::array<float,2>{1.f/64,1.f/64});
            set("TexDim", std::array<float,2>{64,64}); set("RcpTexDim", std::array<float,2>{1.f/64,1.f/64});
            set("NDCToViewMul", std::array<float,4>{2,-2,0,0}); set("NDCToViewAdd", std::array<float,4>{-1,1,0,0});
            set("DepthFadeRange", std::array<float,2>{500,1000}); set("BlurRadius", 2.f); set("DistanceNormalisation", 1.f);
            set("FrameBuffer::CameraProj", std::array<float,16>{1,0,0,0,0,1,0,0,0,0,-1,-1,0,0,-1,0});
            D3D11_BUFFER_DESC bd{}; bd.ByteWidth=cd.Size; bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
            D3D11_SUBRESOURCE_DATA initial{data.data(),0,0}; ComPtr<ID3D11Buffer> buffer;
            Check(device->CreateBuffer(&bd,&initial,&buffer)); auto raw=buffer.Get();
            context->CSSetConstantBuffers(binding.BindPoint,1,&raw); buffers.push_back(buffer);
        }
        struct Tex { ComPtr<ID3D11Texture2D> resource; ComPtr<ID3D11ShaderResourceView> srv; ComPtr<ID3D11UnorderedAccessView> uav; };
        auto texture = [&](DXGI_FORMAT format, bool output) {
            Tex t; D3D11_TEXTURE2D_DESC d{}; d.Width=d.Height=64; d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;
            d.Format=format; d.BindFlags=output?D3D11_BIND_UNORDERED_ACCESS:D3D11_BIND_SHADER_RESOURCE;
            Check(device->CreateTexture2D(&d,nullptr,&t.resource));
            if (output) Check(device->CreateUnorderedAccessView(t.resource.Get(),nullptr,&t.uav));
            else Check(device->CreateShaderResourceView(t.resource.Get(),nullptr,&t.srv));
            return t;
        };
        auto depth=texture(DXGI_FORMAT_R32_FLOAT,false), normal=texture(DXGI_FORMAT_R32G32B32A32_FLOAT,false);
        auto input=texture(DXGI_FORMAT_R32G32B32A32_FLOAT,false), ao=texture(DXGI_FORMAT_R32_FLOAT,false);
        auto bent=texture(DXGI_FORMAT_R32G32B32A32_FLOAT,false);
        auto output=texture(DXGI_FORMAT_R32G32B32A32_FLOAT,true), chroma=texture(DXGI_FORMAT_R32G32_FLOAT,true);
        auto outputAO=texture(DXGI_FORMAT_R32_FLOAT,true);
        auto outputBent=texture(DXGI_FORMAT_R32G32B32A32_FLOAT,true);
        std::vector<float> depths(4096,100.f), normals(16384,0.f), values(16384,0.f), occlusion(4096,0.f), bentValues(16384,0.f);
        // Isolated impulse away from the screen centre: a mirrored footprint
        // cannot accidentally overlap it. Oct (1,1) is engine-view +Z.
        for (unsigned i=0;i<4096;++i) normals[i*4]=normals[i*4+1]=1.f;
        values[(16*64+16)*4]=1.f; occlusion[16*64+16]=0.6f;
        for (unsigned i=0;i<4096;++i) { bentValues[i*4]=1.f; bentValues[i*4+1]=1.f; bentValues[i*4+2]=1.f; }
        bentValues[(16*64+16)*4+2]=0.2f;
        context->UpdateSubresource(depth.resource.Get(),0,nullptr,depths.data(),64*4,0);
        context->UpdateSubresource(normal.resource.Get(),0,nullptr,normals.data(),64*16,0);
        context->UpdateSubresource(input.resource.Get(),0,nullptr,values.data(),64*16,0);
        context->UpdateSubresource(ao.resource.Get(),0,nullptr,occlusion.data(),64*4,0);
        context->UpdateSubresource(bent.resource.Get(),0,nullptr,bentValues.data(),64*16,0);
        ID3D11ShaderResourceView* srvs[]{depth.srv.Get(),normal.srv.Get(),nullptr,input.srv.Get(),nullptr,ao.srv.Get(),bent.srv.Get()};
        ID3D11UnorderedAccessView* uavs[]{nullptr,output.uav.Get(),chroma.uav.Get(),outputAO.uav.Get(),outputBent.uav.Get()};
        D3D11_SAMPLER_DESC sd{}; sd.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
        sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD=D3D11_FLOAT32_MAX;
        ComPtr<ID3D11SamplerState> sampler; Check(device->CreateSamplerState(&sd,&sampler));
        ID3D11SamplerState* samplers[]{sampler.Get(),sampler.Get()}; context->CSSetSamplers(0,2,samplers);
        context->CSSetShaderResources(0,7,srvs); context->CSSetUnorderedAccessViews(0,5,uavs,nullptr);
        context->CSSetShader(shader.Get(),nullptr,0); context->Dispatch(8,8,1);
        ID3D11UnorderedAccessView* nulls[5]{}; context->CSSetUnorderedAccessViews(0,5,nulls,nullptr);
        D3D11_TEXTURE2D_DESC rd{}; output.resource->GetDesc(&rd); rd.Usage=D3D11_USAGE_STAGING;
        rd.BindFlags=0; rd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> readback; Check(device->CreateTexture2D(&rd,nullptr,&readback));
        context->CopyResource(readback.Get(),output.resource.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{}; Check(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped));
        float neighbors=0;
        for (unsigned y=15;y<=17;++y) for (unsigned x=15;x<=17;++x) {
            auto row=reinterpret_cast<const float*>(static_cast<const char*>(mapped.pData)+y*mapped.RowPitch);
            if (!std::isfinite(row[x*4])) throw std::runtime_error("Nonfinite blur result");
            if (x!=16 || y!=16) neighbors+=row[x*4];
        }
        context->Unmap(readback.Get(),0);
        std::cout << "Nearby GI response: " << neighbors << '\n';
        if (neighbors<0.01f) throw std::runtime_error("Blur failed to filter the local neighborhood");
        context->CopyResource(readback.Get(),outputBent.resource.Get());
        Check(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped));
        auto centerBent=reinterpret_cast<const float*>(static_cast<const char*>(mapped.pData)+16*mapped.RowPitch);
        const float centerVisibility=centerBent[16*4+2];
        const float adjacentVisibility=centerBent[17*4+2];
        context->Unmap(readback.Get(),0);
        if (!std::isfinite(centerVisibility) || !std::isfinite(adjacentVisibility) ||
            centerVisibility<=0.2f || adjacentVisibility>=1.f)
            throw std::runtime_error("Bent visibility was not filtered across nearby receiver pixels");
        std::cout << "PASS: production blur filters nearby pixels in trace-view coordinates\n";
        std::cout << "PASS: production blur filters bent visibility (center=" << centerVisibility
                  << ", adjacent=" << adjacentVisibility << ")\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
