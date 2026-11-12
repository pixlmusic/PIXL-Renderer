#include "HybridGIGPUTestSupport.h"

static std::string ExtractFunction(const std::string& source, const std::string& signature)
{
	const auto start = source.find(signature);
	if (start == std::string::npos)
		throw std::runtime_error("Missing production shader function: " + signature);
	const auto open = source.find('{', start);
	if (open == std::string::npos)
		throw std::runtime_error("Missing function body: " + signature);
	int depth = 0;
	for (size_t i = open; i < source.size(); ++i) {
		if (source[i] == '{') ++depth;
		else if (source[i] == '}' && --depth == 0)
			return source.substr(start, i - start + 1);
	}
	throw std::runtime_error("Unclosed function body: " + signature);
}

int main()
{
	try {
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
			D3D11_SDK_VERSION, &device, nullptr, &context));

		const auto reflection = Read("pipeline/Hybrid GI/Kernels/HybridGI/hybridReflection.cs.hlsl");
		const auto common = Read("pipeline/Hybrid GI/Kernels/HybridGI/common.hlsli");
		const auto gbuffer = Read("distribution/PIXL-Shaders/Common/GBuffer.hlsli");
		std::string harness = R"HLSL(
Texture2D<float2> srcReflectionHiZ : register(t11);
Texture2D<float4> srcNormalRoughness : register(t1);
SamplerState samplerPointClamp : register(s0);
RWStructuredBuffer<float4> TestResult : register(u0);
cbuffer TestCB : register(b0) {
    float2 FrameDim; float2 TestPadding0;
    float4 NDCToViewMul; float4 NDCToViewAdd;
    float FP_Z; float ReflectionMaxDistance; float ReflectionRayBias; float ReflectionThickness;
    float ReflectionMaxRoughness; float TestPadding1; uint ReflectionSteps; float TestPadding2;
    float3 TestOrigin; float TestPadding3;
    float3 TestDirection; float TestPadding4;
}
float2 FullFrameTextureUV(float2 uv) { return uv; }
namespace GBuffer {
)HLSL";
		harness += ExtractFunction(gbuffer, "half3 DecodeNormal(half2 f)");
		harness += "\n}\n";
		harness += ExtractFunction(common, "float3 ToTraceView(float3 engineView)");
		harness += ExtractFunction(common, "float2 ViewToScreenPosition(const float3 viewPosition)");
		harness += ExtractFunction(reflection, "float ReflectionRayDepth(float2 inverseDepth, float s)");
		harness += ExtractFunction(reflection, "inline bool TraceScreenReflection(float3 originVS, float3 directionVS, float roughness,");
		harness += R"HLSL(
[numthreads(1,1,1)]
void main(uint3 id : SV_DispatchThreadID)
{
    float2 hitUV = 0.0f;
    float confidence = 0.0f;
    bool hit = TraceScreenReflection(TestOrigin, TestDirection, 0.1f, hitUV, confidence);
    TestResult[0] = float4(hit ? 1.0f : 0.0f, hitUV, confidence);
}
)HLSL";
		ComPtr<ID3DBlob> code, errors;
		const auto hr = D3DCompile(harness.data(), harness.size(), "production-hybrid-reflection-trace",
			nullptr, nullptr, "main", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS,
			0, &code, &errors);
		if (FAILED(hr) && errors) std::cerr << static_cast<const char*>(errors->GetBufferPointer());
		Check(hr);
		ComPtr<ID3D11ComputeShader> shader;
		Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader));

		D3D11_TEXTURE2D_DESC hizDesc{};
		hizDesc.Width = hizDesc.Height = 32; hizDesc.MipLevels = 5; hizDesc.ArraySize = 1;
		hizDesc.Format = DXGI_FORMAT_R32G32_FLOAT; hizDesc.SampleDesc.Count = 1;
		hizDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		std::array<std::vector<float>, 5> depthMips;
		std::array<D3D11_SUBRESOURCE_DATA, 5> init{};
		for (UINT mip = 0; mip < 5; ++mip) {
			const UINT dim = std::max(32u >> mip, 1u);
			depthMips[mip].resize(static_cast<size_t>(dim) * dim * 2u);
			init[mip] = { depthMips[mip].data(), dim * 2u * sizeof(float), 0 };
		}
		ComPtr<ID3D11Texture2D> hiz;
		Check(device->CreateTexture2D(&hizDesc, init.data(), &hiz));
		D3D11_SHADER_RESOURCE_VIEW_DESC hizViewDesc{};
		hizViewDesc.Format = hizDesc.Format; hizViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		hizViewDesc.Texture2D.MipLevels = 5;
		ComPtr<ID3D11ShaderResourceView> hizSrv;
		Check(device->CreateShaderResourceView(hiz.Get(), &hizViewDesc, &hizSrv));

		D3D11_TEXTURE2D_DESC normalDesc{};
		normalDesc.Width = normalDesc.Height = 32; normalDesc.MipLevels = normalDesc.ArraySize = 1;
		normalDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; normalDesc.SampleDesc.Count = 1;
		normalDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		std::vector<float> normals(32u * 32u * 4u);
		for (size_t i = 0; i < normals.size(); i += 4) {
			normals[i] = 1.0f; normals[i + 1] = 1.0f; normals[i + 2] = 0.5f; normals[i + 3] = 1.0f;
		}
		D3D11_SUBRESOURCE_DATA normalInit{ normals.data(), 32u * 4u * sizeof(float), 0 };
		ComPtr<ID3D11Texture2D> normalTexture;
		Check(device->CreateTexture2D(&normalDesc, &normalInit, &normalTexture));
		ComPtr<ID3D11ShaderResourceView> normalSrv;
		Check(device->CreateShaderResourceView(normalTexture.Get(), nullptr, &normalSrv));

		D3D11_BUFFER_DESC resultDesc{};
		resultDesc.ByteWidth = sizeof(float) * 4; resultDesc.Usage = D3D11_USAGE_DEFAULT;
		resultDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
		resultDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		resultDesc.StructureByteStride = sizeof(float) * 4;
		ComPtr<ID3D11Buffer> result;
		Check(device->CreateBuffer(&resultDesc, nullptr, &result));
		D3D11_UNORDERED_ACCESS_VIEW_DESC resultViewDesc{};
		resultViewDesc.Format = DXGI_FORMAT_UNKNOWN; resultViewDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		resultViewDesc.Buffer.NumElements = 1;
		ComPtr<ID3D11UnorderedAccessView> resultUav;
		Check(device->CreateUnorderedAccessView(result.Get(), &resultViewDesc, &resultUav));
		D3D11_BUFFER_DESC readDesc{};
		readDesc.ByteWidth = resultDesc.ByteWidth; readDesc.Usage = D3D11_USAGE_STAGING;
		readDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		ComPtr<ID3D11Buffer> readback;
		Check(device->CreateBuffer(&readDesc, nullptr, &readback));

		D3D11_SAMPLER_DESC samplerDesc{};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		ComPtr<ID3D11SamplerState> sampler;
		Check(device->CreateSamplerState(&samplerDesc, &sampler));
		struct alignas(16) TestConstants {
			float frameDim[2]{ 32.0f, 32.0f }; float padding0[2]{};
			float ndcMul[4]{ 2.0f, -2.0f, 0.0f, 0.0f };
			float ndcAdd[4]{ -1.0f, 1.0f, 0.0f, 0.0f };
			float fpZ{ 0.01f }, maxDistance{ 64.0f }, rayBias{ 1.0f }, thickness{ 1.0f };
			float maxRoughness{ 0.8f }, padding1{}; UINT steps{ 64u }; float padding2{};
			float origin[3]{ 0.0f, 0.0f, 20.0f }; float padding3{};
			float direction[3]{ 0.0f, 0.0f, 1.0f }; float padding4{};
	};
		static_assert(sizeof(TestConstants) % 16 == 0);
		D3D11_BUFFER_DESC cbDesc{};
		cbDesc.ByteWidth = sizeof(TestConstants); cbDesc.Usage = D3D11_USAGE_DEFAULT;
		cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		TestConstants testConstants{};
		D3D11_SUBRESOURCE_DATA cbInitial{ &testConstants, 0, 0 };
		ComPtr<ID3D11Buffer> testCB;
		Check(device->CreateBuffer(&cbDesc, &cbInitial, &testCB));

		auto evaluate = [&](float sceneDepth, bool backFace) {
			for (UINT mip = 0; mip < 5; ++mip)
				for (size_t i = 0; i < depthMips[mip].size(); i += 2) {
					depthMips[mip][i] = sceneDepth; depthMips[mip][i + 1] = sceneDepth;
				}
			for (UINT mip = 0; mip < 5; ++mip) {
				const UINT dim = std::max(32u >> mip, 1u);
				D3D11_BOX box{ 0, 0, 0, dim, dim, 1 };
				context->UpdateSubresource(hiz.Get(), D3D11CalcSubresource(mip, 0, 5), &box,
					depthMips[mip].data(), dim * 2u * sizeof(float), 0);
			}
			if (backFace) {
				for (size_t i = 0; i < normals.size(); i += 4) {
					normals[i] = 0.5f; normals[i + 1] = 0.5f;
				}
				context->UpdateSubresource(normalTexture.Get(), 0, nullptr, normals.data(), 32u * 4u * sizeof(float), 0);
			}
			ID3D11ShaderResourceView* srvs[12]{}; srvs[1] = normalSrv.Get(); srvs[11] = hizSrv.Get();
			ID3D11UnorderedAccessView* uav = resultUav.Get();
			ID3D11SamplerState* samplerRaw = sampler.Get();
			ID3D11Buffer* cb = testCB.Get();
			context->CSSetShaderResources(0, 12, srvs); context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
			context->CSSetSamplers(0, 1, &samplerRaw); context->CSSetConstantBuffers(0, 1, &cb);
			context->CSSetShader(shader.Get(), nullptr, 0);
			context->Dispatch(1, 1, 1);
			ID3D11UnorderedAccessView* nullUav = nullptr;
			ID3D11ShaderResourceView* nullSrvs[12]{};
			context->CSSetUnorderedAccessViews(0, 1, &nullUav, nullptr);
			context->CSSetShaderResources(0, 12, nullSrvs);
			context->CopyResource(readback.Get(), result.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{}; Check(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			std::array<float, 4> out{}; std::memcpy(out.data(), mapped.pData, sizeof(out));
			context->Unmap(readback.Get(), 0);
			return out;
		};
		const auto hit = evaluate(40.0f, false);
		const auto behind = evaluate(10.0f, false);
		const auto clear = evaluate(10000.0f, false);
		const auto backFace = evaluate(40.0f, true);
		if (!(hit[0] > 0.5f && hit[3] > 0.5f && std::abs(hit[1] - 0.5f) < 0.02f && std::abs(hit[2] - 0.5f) < 0.02f))
			throw std::runtime_error("Production SSR tracer missed a visible, front-facing screen surface.");
		if (behind[0] > 0.5f || clear[0] > 0.5f || backFace[0] > 0.5f)
			throw std::runtime_error("Production SSR tracer accepted a behind-ray, sky, or back-facing sample.");
		std::cout << "PASS: production Hi-Z reflection trace hits visible geometry (confidence=" << hit[3]
			<< ") and rejects behind-ray, sky and back-facing samples.\n";
	} catch (const std::exception& e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
