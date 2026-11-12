#include "HybridGIGPUTestSupport.h"
#include <d3d11shader.h>
#include <filesystem>
#include <cmath>
#include <tuple>

// Execute the shipped kernel, including its real buffer layout and includes.
// A front-facing plane must not occlude itself; a nearby lit foreground
// surface must produce a finite bounce on the background receiver.
struct Includes final : ID3DInclude {
    HRESULT __stdcall Open(D3D_INCLUDE_TYPE, LPCSTR name, LPCVOID, LPCVOID* data, UINT* size) override {
        for (const auto* root : {"pipeline/Hybrid GI/Kernels/", "distribution/PIXL-Shaders/"}) {
            std::ifstream file(std::string(root) + name, std::ios::binary);
            if (!file) continue;
            std::string content{std::istreambuf_iterator<char>(file), {}};
            auto bytes = new char[content.size()];
            std::memcpy(bytes, content.data(), content.size());
            *data = bytes; *size = static_cast<UINT>(content.size()); return S_OK;
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
        Includes includes;
        auto source = Read("pipeline/Hybrid GI/Kernels/HybridGI/ssgi.cs.hlsl");
        const bool legacy = argc == 2 && std::string(argv[1]) == "--legacy-normal-control";
        if (legacy) {
            const std::string corrected = "ToTraceView(GBuffer::DecodeNormal(encN))";
            const auto offset = source.find(corrected);
            if (offset == std::string::npos) throw std::runtime_error("Normal boundary changed");
            source.replace(offset, corrected.size(), "GBuffer::DecodeNormal(encN)");
        }
        D3D_SHADER_MACRO defines[]{{"GI", "1"}, {nullptr, nullptr}};
        ComPtr<ID3DBlob> code, errors;
        auto hr = D3DCompile(source.data(), source.size(), "ssgi.cs.hlsl", defines, &includes,
            "main", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &code, &errors);
        if (FAILED(hr) && errors) std::cerr << static_cast<const char*>(errors->GetBufferPointer());
        Check(hr);
        ComPtr<ID3D11ComputeShader> shader;
        Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader));
        D3D_SHADER_MACRO adaptiveDefines[]{{"GI", "1"}, {"TEMPORAL_DENOISER", "1"}, {"ADAPTIVE_RAY_ALLOCATION", "1"},
            {"PIXL_TEST_ADAPTIVE_RAYS", "1"}, {nullptr, nullptr}};
        ComPtr<ID3DBlob> adaptiveCode;
        hr = D3DCompile(source.data(), source.size(), "ssgi-adaptive-test.cs.hlsl", adaptiveDefines, &includes,
            "main", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &adaptiveCode, &errors);
        if (FAILED(hr) && errors) std::cerr << static_cast<const char*>(errors->GetBufferPointer());
        Check(hr);
        ComPtr<ID3D11ComputeShader> adaptiveShader;
        Check(device->CreateComputeShader(adaptiveCode->GetBufferPointer(), adaptiveCode->GetBufferSize(), nullptr, &adaptiveShader));
        ComPtr<ID3D11ShaderReflection> reflection;
        Check(D3DReflect(adaptiveCode->GetBufferPointer(), adaptiveCode->GetBufferSize(), IID_PPV_ARGS(&reflection)));
        D3D11_SHADER_DESC sd{}; Check(reflection->GetDesc(&sd));
        std::vector<ComPtr<ID3D11Buffer>> constants;
        std::vector<unsigned char> hybridGIConstantData;
        ComPtr<ID3D11Buffer> hybridGIConstantBuffer;
        for (UINT i = 0; i < sd.ConstantBuffers; ++i) {
            auto cb = reflection->GetConstantBufferByIndex(i);
            D3D11_SHADER_BUFFER_DESC desc{}; Check(cb->GetDesc(&desc));
            D3D11_SHADER_INPUT_BIND_DESC binding{}; Check(reflection->GetResourceBindingDescByName(desc.Name, &binding));
            std::vector<unsigned char> data(desc.Size, 0);
            auto set = [&](const char* name, auto value) {
                D3D11_SHADER_VARIABLE_DESC v{};
                if (SUCCEEDED(cb->GetVariableByName(name)->GetDesc(&v))) std::memcpy(data.data() + v.StartOffset, &value, sizeof(value));
            };
            set("FrameDim", std::array<float,2>{64,64}); set("TexDim", std::array<float,2>{64,64});
            set("RcpFrameDim", std::array<float,2>{1.f/64,1.f/64}); set("RcpTexDim", std::array<float,2>{1.f/64,1.f/64});
            set("NDCToViewMul", std::array<float,4>{2,-2,0,0}); set("NDCToViewAdd", std::array<float,4>{-1,1,0,0});
            set("NumSlices", 8u); set("NumSteps", 24u); set("GIRadius", 128.f); set("AORadius", 64.f);
            set("AdaptiveRayMinimum", 0.5f); set("MaxAccumFrames", 32u);
            set("EffectRadius", 1.f); set("Thickness", 16.f); set("GIStrength", 1.f); set("AOPower", 1.f);
            set("DepthFadeRange", std::array<float,2>{500,1000}); set("DepthFadeScaleConst", 1.f/500);
            set("FrameBuffer::CameraViewInverse", std::array<float,16>{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1});
            D3D11_BUFFER_DESC bd{}; bd.ByteWidth = desc.Size; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            D3D11_SUBRESOURCE_DATA initial{data.data(),0,0};
            ComPtr<ID3D11Buffer> buffer; Check(device->CreateBuffer(&bd,&initial,&buffer));
            if (std::string(desc.Name) == "HybridGICB") {
                hybridGIConstantData = data;
                hybridGIConstantBuffer = buffer;
            }
            auto raw = buffer.Get(); context->CSSetConstantBuffers(binding.BindPoint,1,&raw); constants.push_back(buffer);
        }
        if (!hybridGIConstantBuffer)
            throw std::runtime_error("SSGI constant buffer was not reflected");
        auto setHybridGIConstant = [&](const char* name, auto value) {
            auto cb = reflection->GetConstantBufferByName("HybridGICB");
            D3D11_SHADER_VARIABLE_DESC variable{};
            Check(cb->GetVariableByName(name)->GetDesc(&variable));
            if (variable.Size < sizeof(value))
                throw std::runtime_error(std::string("HybridGI constant is too small: ") + name);
            std::memcpy(hybridGIConstantData.data() + variable.StartOffset, &value, sizeof(value));
            context->UpdateSubresource(hybridGIConstantBuffer.Get(), 0, nullptr, hybridGIConstantData.data(), 0, 0);
        };
        setHybridGIConstant("ContactDepthEnabled", 0u);
        struct Tex { ComPtr<ID3D11Texture2D> resource; ComPtr<ID3D11ShaderResourceView> srv; ComPtr<ID3D11UnorderedAccessView> uav; };
        auto texture = [&](DXGI_FORMAT format, bool output) {
            Tex t; D3D11_TEXTURE2D_DESC d{}; d.Width=64; d.Height=64; d.MipLevels=1; d.ArraySize=1;
            d.Format=format; d.SampleDesc.Count=1; d.BindFlags=output?D3D11_BIND_UNORDERED_ACCESS:D3D11_BIND_SHADER_RESOURCE;
            Check(device->CreateTexture2D(&d,nullptr,&t.resource));
            if(output) Check(device->CreateUnorderedAccessView(t.resource.Get(),nullptr,&t.uav));
            else Check(device->CreateShaderResourceView(t.resource.Get(),nullptr,&t.srv));
            return t;
        };
        auto depth=texture(DXGI_FORMAT_R32_FLOAT,false), normal=texture(DXGI_FORMAT_R32G32_FLOAT,false);
        auto hitNormal=texture(DXGI_FORMAT_R32G32_FLOAT,false);
        auto bentHistory=texture(DXGI_FORMAT_R32G32B32A32_FLOAT,false);
        auto accum=texture(DXGI_FORMAT_R8_UNORM,false);
        auto radiance=texture(DXGI_FORMAT_R32G32B32A32_FLOAT,false);
        auto ao=texture(DXGI_FORMAT_R32_FLOAT,true), y=texture(DXGI_FORMAT_R32G32B32A32_FLOAT,true);
        auto chroma=texture(DXGI_FORMAT_R32G32_FLOAT,true), geo=texture(DXGI_FORMAT_R32_UINT,true);
        auto bent=texture(DXGI_FORMAT_R32G32B32A32_FLOAT,true);
        std::vector<float> depths(4096,100.f), normals(8192,1.f), light(16384,1.f);
        context->UpdateSubresource(normal.resource.Get(),0,nullptr,normals.data(),64*8,0);
        ID3D11ShaderResourceView* inputs[17]{}; inputs[0]=depth.srv.Get(); inputs[2]=radiance.srv.Get();
        inputs[1]=hitNormal.srv.Get(); inputs[15]=bentHistory.srv.Get();
        std::vector<float> priorBent(16384,1.f);
        for (unsigned i=0;i<4096;++i) priorBent[i*4+2]=0.25f;
        context->UpdateSubresource(bentHistory.resource.Get(),0,nullptr,priorBent.data(),64*16,0);
        inputs[4]=accum.srv.Get(); inputs[8]=normal.srv.Get();
        ID3D11UnorderedAccessView* outputs[]{ao.uav.Get(),y.uav.Get(),chroma.uav.Get(),nullptr,geo.uav.Get(),bent.uav.Get()};
        D3D11_SAMPLER_DESC sam{}; sam.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
        sam.AddressU=sam.AddressV=sam.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP; sam.MaxLOD=D3D11_FLOAT32_MAX;
        ComPtr<ID3D11SamplerState> point,linear; Check(device->CreateSamplerState(&sam,&point));
        sam.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR; Check(device->CreateSamplerState(&sam,&linear));
        for (UINT i=0;i<sd.BoundResources;++i) {
            D3D11_SHADER_INPUT_BIND_DESC b{}; Check(reflection->GetResourceBindingDesc(i,&b));
            if(b.Type==D3D_SIT_SAMPLER) { auto p=std::string(b.Name).find("Linear")!=std::string::npos?linear.Get():point.Get(); context->CSSetSamplers(b.BindPoint,1,&p); }
        }
        D3D11_TEXTURE2D_DESC stage{}; y.resource->GetDesc(&stage); stage.Usage=D3D11_USAGE_STAGING;
        stage.BindFlags=0; stage.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> readback; Check(device->CreateTexture2D(&stage,nullptr,&readback));
        D3D11_TEXTURE2D_DESC bentStage{}; bent.resource->GetDesc(&bentStage); bentStage.Usage=D3D11_USAGE_STAGING;
        bentStage.BindFlags=0; bentStage.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> bentReadback; Check(device->CreateTexture2D(&bentStage,nullptr,&bentReadback));
        D3D11_TEXTURE2D_DESC aoStage{}; ao.resource->GetDesc(&aoStage); aoStage.Usage=D3D11_USAGE_STAGING;
        aoStage.BindFlags=0; aoStage.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> aoReadback; Check(device->CreateTexture2D(&aoStage,nullptr,&aoReadback));
        auto run=[&](bool occluder, bool illuminated, float historyFrames, bool facingEmitter = true) {
            for(unsigned row=0;row<64;++row) for(unsigned col=0;col<64;++col) depths[row*64+col]=occluder && col<32?70.f:100.f;
            // The production kernel validates emitter facing using the full
            // normal buffer (t1), independently of receiver normals (t8).
            std::vector<float> hitNormals(8192,1.f);
            for(unsigned row=0;row<64;++row) for(unsigned col=0;col<32;++col)
                if (occluder && facingEmitter) hitNormals[(row*64+col)*2]=hitNormals[(row*64+col)*2+1]=0.5f;
            context->UpdateSubresource(hitNormal.resource.Get(),0,nullptr,hitNormals.data(),64*8,0);
            std::fill(light.begin(),light.end(),illuminated?1.f:0.f);
            context->UpdateSubresource(depth.resource.Get(),0,nullptr,depths.data(),64*4,0);
            context->UpdateSubresource(radiance.resource.Get(),0,nullptr,light.data(),64*16,0);
            std::vector<std::uint8_t> history(4096, static_cast<std::uint8_t>(historyFrames));
            context->UpdateSubresource(accum.resource.Get(),0,nullptr,history.data(),64,0);
            context->CSSetShaderResources(0,17,inputs); context->CSSetUnorderedAccessViews(0,6,outputs,nullptr);
            context->CSSetShader(adaptiveShader.Get(),nullptr,0); context->Dispatch(8,8,1);
            ID3D11UnorderedAccessView* nulls[6]{}; context->CSSetUnorderedAccessViews(0,6,nulls,nullptr);
            context->CopyResource(readback.Get(),y.resource.Get());
            D3D11_MAPPED_SUBRESOURCE mapped{}; Check(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped));
            double sum=0;
            for(unsigned row=0;row<64;++row) {
                auto values=reinterpret_cast<const float*>(static_cast<const unsigned char*>(mapped.pData)+row*mapped.RowPitch);
                for(unsigned col=32;col<64;++col) { float value=values[col*4]; if(!std::isfinite(value)||value<0) throw std::runtime_error("Invalid GI output"); sum+=value; }
            }
            context->Unmap(readback.Get(),0);
            context->CopyResource(bentReadback.Get(),bent.resource.Get());
            context->CopyResource(aoReadback.Get(),ao.resource.Get());
            D3D11_MAPPED_SUBRESOURCE aoMapped{}; Check(context->Map(aoReadback.Get(),0,D3D11_MAP_READ,0,&aoMapped));
            double aoSum=0, edgeAoSum=0, edgeFarAoSum=0;
            float maxAO = 0.0f;
            for(unsigned row=0;row<64;++row) {
                auto values=reinterpret_cast<const float*>(static_cast<const unsigned char*>(aoMapped.pData)+row*aoMapped.RowPitch);
                for(unsigned col=32;col<64;++col) {
                    if (!std::isfinite(values[col]) || values[col] < 0.0f || values[col] > 1.0f)
                        throw std::runtime_error("SSAO output was non-finite or outside [0,1]");
                    aoSum+=values[col];
                    maxAO = std::max(maxAO, values[col]);
                    if (row >= 16 && row < 48 && col >= 32 && col < 40) edgeAoSum += values[col];
                    if (row >= 16 && row < 48 && col >= 56 && col < 64) edgeFarAoSum += values[col];
                }
            }
            context->Unmap(aoReadback.Get(),0);
            D3D11_MAPPED_SUBRESOURCE bentMapped{}; Check(context->Map(bentReadback.Get(),0,D3D11_MAP_READ,0,&bentMapped));
            const auto* center = reinterpret_cast<const float*>(static_cast<const unsigned char*>(bentMapped.pData)+32*bentMapped.RowPitch)+32*4;
            for (unsigned component=0;component<4;++component)
                if (!std::isfinite(center[component])) throw std::runtime_error("Nonfinite bent visibility output");
            const float rayFraction=center[3], visibility=center[2]; context->Unmap(bentReadback.Get(),0);
            return std::tuple{sum, rayFraction, aoSum / (64.0*32.0),
                edgeAoSum / (32.0*8.0), edgeFarAoSum / (32.0*8.0), maxAO, visibility};
        };
        auto plane=run(false,true,0), dark=run(true,false,0), lit=run(true,true,0);
        std::cout<<"SSGI receiver energy: plane="<<std::get<0>(plane)<<" dark="<<std::get<0>(dark)
            <<" lit occluder="<<std::get<0>(lit)<<"; AO plane="<<std::get<2>(plane)
            <<" occluded="<<std::get<2>(lit)<<"; contact band="<<std::get<3>(lit)
            <<" far band="<<std::get<4>(lit)<<'\n';
        if(std::get<0>(plane)>1e-5 || std::get<0>(dark)>1e-5 || std::get<0>(lit)<1.0)
            throw std::runtime_error("SSGI scene contract failed");
        if (!(std::get<2>(lit) > std::get<2>(plane) + 1.0e-4))
            throw std::runtime_error("SSAO screen-space occluder did not increase AO on the receiver");
        // A flat depth plane remains unoccluded when normal-map texels point
        // sideways. The visibility trace must follow depth geometry here.
        for (unsigned row=0;row<64;++row) for (unsigned col=0;col<64;++col) {
            const auto idx=(row*64+col)*2;
            normals[idx]=(col&1u)?1.0f:0.5f;
            normals[idx+1]=(col&1u)?0.5f:1.0f;
        }
        context->UpdateSubresource(normal.resource.Get(),0,nullptr,normals.data(),64*8,0);
        const auto bumpyPlane=run(false,true,0);
        if (std::get<2>(bumpyPlane) > 0.02f)
            throw std::runtime_error("Normal-map detail produced geometric AO on a flat plane");
        std::fill(normals.begin(),normals.end(),1.f);
        context->UpdateSubresource(normal.resource.Get(),0,nullptr,normals.data(),64*8,0);
        std::cout << "PASS: geometric AO ignores tangent-space detail on a flat depth plane\n";
        const auto backFacing = run(true,true,0,false);
        if (std::abs(std::get<2>(backFacing) - std::get<2>(lit)) > 1.0e-5 ||
            std::get<0>(backFacing) > 1.0e-5)
            throw std::runtime_error("AO must retain depth blockers rejected as GI emitters");
        std::cout << "PASS: AO occludes independently of GI emitter facing\n";
        auto converged=run(true,true,32);
        const auto stablePlane=run(false,true,32);
        std::cout<<"Specular visibility: fresh="<<std::get<6>(plane)<<" history="<<std::get<6>(stablePlane)<<'\n';
        if (std::abs(std::get<6>(plane)-1.f)>1e-5f ||
            !(std::get<6>(stablePlane)>0.25f && std::get<6>(stablePlane)<0.5f))
            throw std::runtime_error("Specular visibility did not use validated history or reset on fresh pixels");
        std::cout<<"PASS: specular visibility filters valid history and rejects it on fresh pixels\n";
        if (std::abs(std::get<1>(plane)-1.0f)>1e-5f || !(std::get<1>(converged) < std::get<1>(plane)))
            throw std::runtime_error("Adaptive ray allocation failed: fresh pixels must retain full rays and converged history must reduce work");
        std::cout<<"PASS: adaptive ray allocation preserves full fresh-pixel work and reduces converged pixels (fresh="
            <<std::get<1>(plane)*8.0f<<", converged="<<std::get<1>(converged)*8.0f<<" rays)\n";

        setHybridGIConstant("ContactDepthEnabled", 1u);
        setHybridGIConstant("ContactDepthStrength", 0.75f);
        setHybridGIConstant("ContactDepthRadius", 96.0f);
        setHybridGIConstant("ContactDepthBias", 0.08f);
        const auto contactEnabled = run(true,true,0);
        if (!(std::get<3>(contactEnabled) > std::get<3>(lit) + 1e-4) ||
            !(std::get<3>(contactEnabled) > std::get<4>(contactEnabled)))
            throw std::runtime_error("Contact Depth settings did not add bounded near-field crevice occlusion");
        std::cout<<"PASS: Contact Depth controls affect near-field SSGI occlusion without darkening the far receiver band\n";

        setHybridGIConstant("AOPower", 4.0f);
        const auto highStrength = run(true,true,0);
        if (std::get<5>(highStrength) > 0.72001f)
            throw std::runtime_error("High AO strength exceeded the diffuse visibility safety ceiling");
        std::cout<<"PASS: high AO strength retains the 0.72 maximum occlusion cap (maximum="
            <<std::get<5>(highStrength)<<")\n";
        std::cout<<"PASS: shipped SSGI kernel, negative-Z engine normals, finite bounce and no self-lighting\n";

        // An asymmetric 2x2 depth map detects a vertically inverted PCF gather.
        const auto shadowSource=Read("pipeline/Radiant Grid/Kernels/RadiantGrid/LocalShadowSampling.hlsli");
        const auto start=shadowSource.find("float SampleLocalShadowTap("), end=shadowSource.find("float SampleLocalShadowPCF(");
        if(start==std::string::npos || end<=start) throw std::runtime_error("Shadow tap boundary changed");
        const auto shadowTest=std::string("Texture2DArray<float> LocalShadowMaps : register(t0); SamplerState tapSampler : register(s0); RWStructuredBuffer<float> result : register(u0);\n")+
            shadowSource.substr(start,end-start)+"\n[numthreads(1,1,1)] void main() { result[0]=SampleLocalShadowTap(tapSampler,0,float2(0.375,0.375),0.5,0.5); }";
        code.Reset(); errors.Reset();
        hr=D3DCompile(shadowTest.data(),shadowTest.size(),"production-shadow-tap",nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&errors);
        if(FAILED(hr)&&errors) std::cerr<<static_cast<const char*>(errors->GetBufferPointer());
        Check(hr); ComPtr<ID3D11ComputeShader> shadowShader;
        Check(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shadowShader));
        D3D11_TEXTURE2D_DESC mapDesc{}; mapDesc.Width=mapDesc.Height=2; mapDesc.MipLevels=mapDesc.ArraySize=1;
        mapDesc.Format=DXGI_FORMAT_R32_FLOAT; mapDesc.SampleDesc.Count=1; mapDesc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        const float mapData[]{0.25f,0.75f,0.75f,0.75f}; D3D11_SUBRESOURCE_DATA mapInitial{mapData,8,0};
        ComPtr<ID3D11Texture2D> map; Check(device->CreateTexture2D(&mapDesc,&mapInitial,&map));
        D3D11_SHADER_RESOURCE_VIEW_DESC mapView{}; mapView.Format=mapDesc.Format; mapView.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        mapView.Texture2DArray.MipLevels=mapView.Texture2DArray.ArraySize=1;
        ComPtr<ID3D11ShaderResourceView> mapSrv; Check(device->CreateShaderResourceView(map.Get(),&mapView,&mapSrv));
        Buffer tapOutput; tapOutput.Create(device.Get(),1,4);
        auto tapUav=tapOutput.uav.Get(); auto tapSrv=mapSrv.Get(); auto sampler=point.Get();
        context->CSSetShaderResources(0,1,&tapSrv); context->CSSetUnorderedAccessViews(0,1,&tapUav,nullptr);
        context->CSSetSamplers(0,1,&sampler); context->CSSetShader(shadowShader.Get(),nullptr,0); context->Dispatch(1,1,1);
        tapUav=nullptr; context->CSSetUnorderedAccessViews(0,1,&tapUav,nullptr);
        D3D11_BUFFER_DESC readDesc{}; readDesc.ByteWidth=4; readDesc.Usage=D3D11_USAGE_STAGING; readDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Buffer> tapReadback; Check(device->CreateBuffer(&readDesc,nullptr,&tapReadback));
        context->CopyResource(tapReadback.Get(),tapOutput.resource.Get());
        D3D11_MAPPED_SUBRESOURCE tapMapped{}; Check(context->Map(tapReadback.Get(),0,D3D11_MAP_READ,0,&tapMapped));
        const float tap=*static_cast<const float*>(tapMapped.pData); context->Unmap(tapReadback.Get(),0);
        if(!std::isfinite(tap)||std::abs(tap-0.4375f)>1e-5f) throw std::runtime_error("Shadow gather interpolation failed");
        std::cout<<"PASS: production shadow tap interpolates asymmetric depth footprint (visibility="<<tap<<")\n";

        // Execute the production point-light screen-space ray against a small
        // synthetic linear-depth target: an empty segment must stay lit, while
        // a blocker between receiver and light must reduce visibility even
        // when fixed ray steps land well behind its front depth.
        const auto gridSource=Read("pipeline/Radiant Grid/Kernels/RadiantGrid/RadiantGrid.hlsli");
        const auto screenStart=gridSource.find("float GetScreenSpaceLocalShadow(");
        const auto screenEnd=gridSource.find("return visibility;",screenStart);
        if(screenStart==std::string::npos||screenEnd==std::string::npos) throw std::runtime_error("Screen-space shadow helper boundary changed");
        const std::string screenTest=
            "Texture2D<float> TestDepth : register(t0); RWStructuredBuffer<float> TestResult : register(u0);\n"
            "namespace FrameBuffer { float2 ViewToUV(float3 p) { return p.xy * 0.001f + 0.5f; } }\n"
            "namespace SharedData { float GetDepth(float2 uv) { uint2 p=min((uint2)(saturate(uv)*64.0f),63u); return TestDepth.Load(int3(p,0)); } float GetScreenDepth(float d) { return d < 0.9f ? 200.0f : 1000.0f; } }\n"+
            gridSource.substr(screenStart,screenEnd+std::string("return visibility;").size()-screenStart)+"\n\t}\n"+
            "\n[numthreads(1,1,1)] void main() { "
            "TestResult[0]=GetScreenSpaceLocalShadow(float2(0.5f,0.5f),float3(0,0,-100),float3(0,0,-400),100); "
            "TestResult[1]=GetScreenSpaceLocalShadow(float2(0.5f,0.5f),float3(0,0,-100),float3(0,0,-400),10000); }";
        code.Reset(); errors.Reset();
        hr=D3DCompile(screenTest.data(),screenTest.size(),"production-local-screen-shadow",nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&errors);
        if(FAILED(hr)&&errors) std::cerr<<static_cast<const char*>(errors->GetBufferPointer());
        Check(hr); ComPtr<ID3D11ComputeShader> screenShader;
        Check(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&screenShader));
        D3D11_TEXTURE2D_DESC screenDepthDesc{}; screenDepthDesc.Width=screenDepthDesc.Height=64;
        screenDepthDesc.MipLevels=screenDepthDesc.ArraySize=1; screenDepthDesc.Format=DXGI_FORMAT_R32_FLOAT;
        screenDepthDesc.SampleDesc.Count=1; screenDepthDesc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> screenDepth; Check(device->CreateTexture2D(&screenDepthDesc,nullptr,&screenDepth));
        ComPtr<ID3D11ShaderResourceView> screenDepthSrv; Check(device->CreateShaderResourceView(screenDepth.Get(),nullptr,&screenDepthSrv));
        Buffer screenResult; screenResult.Create(device.Get(),2,4);
        auto screenUav=screenResult.uav.Get(); auto screenSrv=screenDepthSrv.Get();
        context->CSSetShaderResources(0,1,&screenSrv); context->CSSetUnorderedAccessViews(0,1,&screenUav,nullptr);
        context->CSSetShader(screenShader.Get(),nullptr,0);
        auto evaluateScreenShadow=[&](float blockerDepth) {
            std::vector<float> values(4096,blockerDepth);
            context->UpdateSubresource(screenDepth.Get(),0,nullptr,values.data(),64*4,0);
            context->Dispatch(1,1,1); screenUav=nullptr;
            context->CSSetUnorderedAccessViews(0,1,&screenUav,nullptr); screenUav=screenResult.uav.Get();
            D3D11_BUFFER_DESC d{}; d.ByteWidth=8; d.Usage=D3D11_USAGE_STAGING; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Buffer> read; Check(device->CreateBuffer(&d,nullptr,&read));
            context->CopyResource(read.Get(),screenResult.resource.Get()); D3D11_MAPPED_SUBRESOURCE mapped{};
            Check(context->Map(read.Get(),0,D3D11_MAP_READ,0,&mapped));
            const auto* valuesOut=static_cast<const float*>(mapped.pData);
            const std::pair<float,float> value{valuesOut[0],valuesOut[1]};
            context->Unmap(read.Get(),0); screenUav=screenResult.uav.Get();
            context->CSSetUnorderedAccessViews(0,1,&screenUav,nullptr); return value;
        };
        const auto unblocked=evaluateScreenShadow(1.0f);
        const auto blocked=evaluateScreenShadow(0.5f);
        context->CSSetUnorderedAccessViews(0,1,&screenUav,nullptr);
        std::cout<<"Screen-space point shadow visibility: clear="<<unblocked.first
            <<" blocked="<<blocked.first<<" softened="<<blocked.second<<'\n';
        if(!std::isfinite(unblocked.first)||!std::isfinite(unblocked.second)||
            !std::isfinite(blocked.first)||!std::isfinite(blocked.second)||
            std::abs(unblocked.first-1.0f)>1e-5f||std::abs(unblocked.second-1.0f)>1e-5f||
            blocked.first>=0.8f||blocked.second<=0.02f||blocked.second>=0.98f)
            throw std::runtime_error("Screen-space point-light shadow contract failed");
        std::cout<<"PASS: production point-light screen-space shadow preserves clear rays, detects blockers, and returns a penumbra (clear="
            <<unblocked.first<<", blocked="<<blocked.first<<", softened="<<blocked.second<<")\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
