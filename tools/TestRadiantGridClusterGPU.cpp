#include <d3d11.h>
#include <d3dcompiler.h>
#include <winrt/base.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <new>

#ifndef PIXL_SOURCE_DIR
#error PIXL_SOURCE_DIR must identify the repository root.
#endif

namespace
{
	class SourceIncludes final : public ID3DInclude
	{
	public:
		explicit SourceIncludes(std::filesystem::path root) : root_(std::move(root)) {}

		HRESULT __stdcall Open(D3D_INCLUDE_TYPE, LPCSTR name, LPCVOID,
			LPCVOID* data, UINT* size) override
		{
			if (!name || !data || !size)
				return E_INVALIDARG;
			const std::filesystem::path relative{name};
			if (relative.is_absolute() || relative.string().find("..") != std::string::npos)
				return E_INVALIDARG;
			const auto distribution = root_ / "distribution" / "PIXL-Shaders" / relative;
			const auto pipeline = root_ / "pipeline" / "Radiant Grid" / "Kernels" / relative;
			std::ifstream input(distribution, std::ios::binary | std::ios::ate);
			if (!input)
				input.open(pipeline, std::ios::binary | std::ios::ate);
			if (!input)
				return E_FAIL;
			const auto length = input.tellg();
			if (length < 0 || static_cast<std::uint64_t>(length) > UINT_MAX)
				return E_FAIL;
			const auto count = static_cast<std::size_t>(length);
			auto* bytes = new (std::nothrow) char[count ? count : 1u];
			if (!bytes)
				return E_OUTOFMEMORY;
			input.seekg(0);
			if (count && !input.read(bytes, static_cast<std::streamsize>(count))) {
				delete[] bytes;
				return E_FAIL;
			}
			*data = bytes;
			*size = static_cast<UINT>(count);
			return S_OK;
		}

		HRESULT __stdcall Close(LPCVOID data) override
		{
			delete[] static_cast<const char*>(data);
			return S_OK;
		}

	private:
		std::filesystem::path root_;
	};

	struct alignas(16) ClusterParameters
	{
		float nearDistance{1.0f};
		float farDistance{10.0f};
		std::uint32_t padding[2]{};
		std::uint32_t clusterSize[4]{1, 1, 1, 0};
	};
	static_assert(sizeof(ClusterParameters) == 32);

	struct ClusterBounds
	{
		float minimum[4];
		float maximum[4];
	};
	static_assert(sizeof(ClusterBounds) == 32);

	struct alignas(16) Light
	{
		float color[3]{};
		float fade{};
		float radius{2.0f};
		float invRadius{};
		float fadeZone{};
		float sizeBias{};
		float position[4]{};
		std::uint32_t roomFlags[4]{};
		std::uint32_t lightFlags{};
		std::uint32_t shadowIndex{};
		std::uint32_t localShadowIndex{};
		std::uint32_t padding{};
	};
	static_assert(sizeof(Light) == 80);

	struct alignas(16) CullingParameters
	{
		std::uint32_t lightCount{2};
		std::uint32_t padding[3]{};
		std::uint32_t clusterSize[4]{1, 1, 1, 0};
	};
	static_assert(sizeof(CullingParameters) == 32);
}

int main(int argc, char** argv)
{
	const bool positiveForward = argc == 2 && std::string(argv[1]) == "--positive-z";
	winrt::com_ptr<ID3D11Device> device;
	winrt::com_ptr<ID3D11DeviceContext> context;
	D3D_FEATURE_LEVEL featureLevel{};
	if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
		nullptr, 0, D3D11_SDK_VERSION, device.put(), &featureLevel, context.put())) ||
		featureLevel < D3D_FEATURE_LEVEL_11_0)
		return 1;

	const auto root = std::filesystem::path(PIXL_SOURCE_DIR);
	const auto shaderPath = root / "pipeline" / "Radiant Grid" / "Kernels" /
		"RadiantGrid" / "ClusterBuildingCS.hlsl";
	SourceIncludes includes{root};
	winrt::com_ptr<ID3DBlob> bytecode;
	winrt::com_ptr<ID3DBlob> diagnostics;
	if (FAILED(D3DCompileFromFile(shaderPath.c_str(), nullptr, &includes, "main", "cs_5_0",
		D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS,
		0, bytecode.put(), diagnostics.put()))) {
		if (diagnostics)
			std::cerr.write(static_cast<const char*>(diagnostics->GetBufferPointer()), diagnostics->GetBufferSize());
		return 2;
	}
	winrt::com_ptr<ID3D11ComputeShader> shader;
	if (FAILED(device->CreateComputeShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(),
		nullptr, shader.put())))
		return 3;

	const ClusterParameters parameters{};
	D3D11_BUFFER_DESC constantDesc{};
	constantDesc.ByteWidth = sizeof(parameters);
	constantDesc.Usage = D3D11_USAGE_IMMUTABLE;
	constantDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	D3D11_SUBRESOURCE_DATA initial{};
	initial.pSysMem = &parameters;
	winrt::com_ptr<ID3D11Buffer> clusterConstants;
	if (FAILED(device->CreateBuffer(&constantDesc, &initial, clusterConstants.put())))
		return 4;

	// FrameBuffer::PerFrame is 45 float4 registers. CameraProjInverse begins
	// at c36. Exercise both forward conventions with an asymmetric perspective
	// projection (near=1, far=10), rather than an identity inverse.
	std::array<float, 45 * 4> frame{};
	for (std::uint32_t index = 0; index < 4; ++index) {
		frame[index * 5] = 1.0f;
	}
	const float forwardSign = positiveForward ? 1.0f : -1.0f;
	const std::array<float, 16> inverseProjection{
		1, 0, 0, -0.17f, 0, 1, 0, 0.11f,
		0, 0, 0, forwardSign, 0, 0, -0.9f, 1};
	std::memcpy(frame.data() + 36 * 4, inverseProjection.data(), sizeof(inverseProjection));
	constantDesc.ByteWidth = static_cast<UINT>(sizeof(frame));
	initial.pSysMem = frame.data();
	winrt::com_ptr<ID3D11Buffer> frameConstants;
	if (FAILED(device->CreateBuffer(&constantDesc, &initial, frameConstants.put())))
		return 5;

	D3D11_BUFFER_DESC boundsDesc{};
	boundsDesc.ByteWidth = sizeof(ClusterBounds);
	boundsDesc.Usage = D3D11_USAGE_DEFAULT;
	boundsDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
	boundsDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	boundsDesc.StructureByteStride = sizeof(ClusterBounds);
	winrt::com_ptr<ID3D11Buffer> bounds;
	if (FAILED(device->CreateBuffer(&boundsDesc, nullptr, bounds.put())))
		return 6;
	D3D11_UNORDERED_ACCESS_VIEW_DESC viewDesc{};
	viewDesc.Format = DXGI_FORMAT_UNKNOWN;
	viewDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	viewDesc.Buffer.NumElements = 1;
	winrt::com_ptr<ID3D11UnorderedAccessView> boundsView;
	if (FAILED(device->CreateUnorderedAccessView(bounds.get(), &viewDesc, boundsView.put())))
		return 7;

	ID3D11Buffer* constants[1]{clusterConstants.get()};
	ID3D11Buffer* frameBuffer[1]{frameConstants.get()};
	ID3D11UnorderedAccessView* outputs[1]{boundsView.get()};
	context->CSSetConstantBuffers(0, 1, constants);
	context->CSSetConstantBuffers(12, 1, frameBuffer);
	context->CSSetUnorderedAccessViews(0, 1, outputs, nullptr);
	context->CSSetShader(shader.get(), nullptr, 0);
	context->Dispatch(1, 1, 1);
	context->CSSetShader(nullptr, nullptr, 0);
	outputs[0] = nullptr;
	context->CSSetUnorderedAccessViews(0, 1, outputs, nullptr);

	D3D11_BUFFER_DESC readbackDesc = boundsDesc;
	readbackDesc.Usage = D3D11_USAGE_STAGING;
	readbackDesc.BindFlags = 0;
	readbackDesc.MiscFlags = 0;
	readbackDesc.StructureByteStride = 0;
	readbackDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	winrt::com_ptr<ID3D11Buffer> readback;
	if (FAILED(device->CreateBuffer(&readbackDesc, nullptr, readback.put())))
		return 8;
	context->CopyResource(readback.get(), bounds.get());
	D3D11_MAPPED_SUBRESOURCE mapped{};
	if (FAILED(context->Map(readback.get(), 0, D3D11_MAP_READ, 0, &mapped)))
		return 9;
	ClusterBounds result{};
	std::memcpy(&result, mapped.pData, sizeof(result));
	context->Unmap(readback.get(), 0);
	const float expectedMinZ = positiveForward ? 1.0f : -10.0f;
	const float expectedMaxZ = positiveForward ? 10.0f : -1.0f;
	if (std::abs(result.minimum[2] - expectedMinZ) > 0.01f ||
		std::abs(result.maximum[2] - expectedMaxZ) > 0.01f ||
		std::abs(result.minimum[0] + 11.7f) > 0.01f || std::abs(result.maximum[0] - 8.3f) > 0.01f) {
		std::cerr << "Unexpected clustered view bounds: z=" << result.minimum[2] << ".."
			<< result.maximum[2] << ", x=" << result.minimum[0] << ".." << result.maximum[0] << '\n';
		return 10;
	}

	const auto cullingPath = root / "pipeline" / "Radiant Grid" / "Kernels" /
		"RadiantGrid" / "ClusterCullingCS.hlsl";
	bytecode = nullptr;
	diagnostics = nullptr;
	if (FAILED(D3DCompileFromFile(cullingPath.c_str(), nullptr, &includes, "main", "cs_5_0",
		D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS,
		0, bytecode.put(), diagnostics.put()))) {
		if (diagnostics)
			std::cerr.write(static_cast<const char*>(diagnostics->GetBufferPointer()), diagnostics->GetBufferSize());
		return 11;
	}
	winrt::com_ptr<ID3D11ComputeShader> cullingShader;
	if (FAILED(device->CreateComputeShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(),
		nullptr, cullingShader.put())))
		return 12;
	const CullingParameters cullingParameters{};
	constantDesc.ByteWidth = sizeof(cullingParameters);
	initial.pSysMem = &cullingParameters;
	winrt::com_ptr<ID3D11Buffer> cullingConstants;
	if (FAILED(device->CreateBuffer(&constantDesc, &initial, cullingConstants.put())))
		return 13;

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.NumElements = 1;
	winrt::com_ptr<ID3D11ShaderResourceView> boundsSRV;
	if (FAILED(device->CreateShaderResourceView(bounds.get(), &srvDesc, boundsSRV.put())))
		return 14;
	std::array<Light, 2> testLights{};
	testLights[0].position[2] = positiveForward ? 5.0f : -5.0f;
	testLights[0].lightFlags = 1u << 1;  // Native shadow map: still eligible for contact refinement.
	testLights[1].position[2] = -testLights[0].position[2];
	D3D11_BUFFER_DESC lightDesc{};
	lightDesc.ByteWidth = sizeof(testLights);
	lightDesc.Usage = D3D11_USAGE_IMMUTABLE;
	lightDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	lightDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	lightDesc.StructureByteStride = sizeof(Light);
	initial.pSysMem = testLights.data();
	winrt::com_ptr<ID3D11Buffer> lightBuffer;
	if (FAILED(device->CreateBuffer(&lightDesc, &initial, lightBuffer.put())))
		return 15;
	srvDesc.Buffer.NumElements = 2;
	winrt::com_ptr<ID3D11ShaderResourceView> lightSRV;
	if (FAILED(device->CreateShaderResourceView(lightBuffer.get(), &srvDesc, lightSRV.put())))
		return 16;

	const auto makeOutput = [&](UINT bytes, UINT stride,
		winrt::com_ptr<ID3D11Buffer>& buffer,
		winrt::com_ptr<ID3D11UnorderedAccessView>& uav) {
		D3D11_BUFFER_DESC desc{};
		desc.ByteWidth = bytes;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = stride;
		D3D11_UNORDERED_ACCESS_VIEW_DESC view{};
		view.Format = DXGI_FORMAT_UNKNOWN;
		view.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		view.Buffer.NumElements = bytes / stride;
		return SUCCEEDED(device->CreateBuffer(&desc, nullptr, buffer.put())) &&
			SUCCEEDED(device->CreateUnorderedAccessView(buffer.get(), &view, uav.put()));
	};
	winrt::com_ptr<ID3D11Buffer> counter, lightIndices, lightGrid;
	winrt::com_ptr<ID3D11UnorderedAccessView> counterUAV, lightIndicesUAV, lightGridUAV;
	if (!makeOutput(4, 4, counter, counterUAV) ||
		!makeOutput(8, 4, lightIndices, lightIndicesUAV) ||
		!makeOutput(48, 48, lightGrid, lightGridUAV))
		return 17;
	const UINT clear[4]{};
	context->ClearUnorderedAccessViewUint(counterUAV.get(), clear);
	ID3D11Buffer* cullCB[1]{cullingConstants.get()};
	ID3D11ShaderResourceView* cullSRVs[2]{boundsSRV.get(), lightSRV.get()};
	ID3D11UnorderedAccessView* cullUAVs[3]{counterUAV.get(), lightIndicesUAV.get(), lightGridUAV.get()};
	context->CSSetConstantBuffers(0, 1, cullCB);
	context->CSSetShaderResources(0, 2, cullSRVs);
	context->CSSetUnorderedAccessViews(0, 3, cullUAVs, nullptr);
	context->CSSetShader(cullingShader.get(), nullptr, 0);
	context->Dispatch(1, 1, 1);
	context->CSSetShader(nullptr, nullptr, 0);
	ID3D11ShaderResourceView* emptySRVs[2]{};
	ID3D11UnorderedAccessView* emptyUAVs[3]{};
	context->CSSetShaderResources(0, 2, emptySRVs);
	context->CSSetUnorderedAccessViews(0, 3, emptyUAVs, nullptr);

	const auto readUInt = [&](ID3D11Buffer* source, UINT bytes, UINT word) -> std::uint32_t {
		D3D11_BUFFER_DESC desc{};
		desc.ByteWidth = bytes;
		desc.Usage = D3D11_USAGE_STAGING;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		winrt::com_ptr<ID3D11Buffer> staging;
		if (FAILED(device->CreateBuffer(&desc, nullptr, staging.put())))
			return UINT32_MAX;
		context->CopyResource(staging.get(), source);
		D3D11_MAPPED_SUBRESOURCE data{};
		if (FAILED(context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &data)))
			return UINT32_MAX;
		const auto value = static_cast<const std::uint32_t*>(data.pData)[word];
		context->Unmap(staging.get(), 0);
		return value;
	};
	const auto count = readUInt(lightGrid.get(), 48, 1);
	const auto contactCandidate = readUInt(lightGrid.get(), 48, 2);
	const auto selected = readUInt(lightIndices.get(), 8, 0);
	const auto total = readUInt(counter.get(), 4, 0);
	if (count != 1 || contactCandidate != 0 || selected != 0 || total != 1) {
		std::cerr << "Unexpected cluster culling: count=" << count << " selected="
			<< selected << " contact=" << contactCandidate << " total=" << total << '\n';
		return 18;
	}
	std::cout << "Radiant Grid WARP cluster: visible light selected, behind-camera light excluded (forward sign "
		<< (positiveForward ? 1 : -1) << ")\n";

	// End-to-end multi-cell regression: each projected receiver has a tiny
	// co-located light. Its grid cell must contain that light at every depth,
	// including an asymmetric frustum. Wrong forward-Z mirrors entire tiles.
	constexpr UINT cellCount = 4 * 3 * 4;
	ClusterParameters gridParameters{};
	gridParameters.clusterSize[0] = 4;
	gridParameters.clusterSize[1] = 3;
	gridParameters.clusterSize[2] = 4;
	constantDesc.ByteWidth = sizeof(gridParameters);
	initial.pSysMem = &gridParameters;
	winrt::com_ptr<ID3D11Buffer> gridBuildConstants;
	if (FAILED(device->CreateBuffer(&constantDesc, &initial, gridBuildConstants.put()))) return 25;
	boundsDesc.ByteWidth = cellCount * sizeof(ClusterBounds);
	winrt::com_ptr<ID3D11Buffer> gridBounds;
	winrt::com_ptr<ID3D11UnorderedAccessView> gridBoundsUAV;
	winrt::com_ptr<ID3D11ShaderResourceView> gridBoundsSRV;
	viewDesc.Buffer.NumElements = cellCount;
	srvDesc.Buffer.NumElements = cellCount;
	if (FAILED(device->CreateBuffer(&boundsDesc, nullptr, gridBounds.put())) ||
		FAILED(device->CreateUnorderedAccessView(gridBounds.get(), &viewDesc, gridBoundsUAV.put())) ||
		FAILED(device->CreateShaderResourceView(gridBounds.get(), &srvDesc, gridBoundsSRV.put()))) return 26;
	constants[0] = gridBuildConstants.get();
	outputs[0] = gridBoundsUAV.get();
	context->CSSetConstantBuffers(0, 1, constants);
	context->CSSetUnorderedAccessViews(0, 1, outputs, nullptr);
	context->CSSetShader(shader.get(), nullptr, 0);
	context->Dispatch(4, 3, 4);
	context->CSSetUnorderedAccessViews(0, 3, emptyUAVs, nullptr);
	std::array<Light, cellCount> receiverLights{};
	for (UINT z = 0; z < 4; ++z) for (UINT y = 0; y < 3; ++y) for (UINT x = 0; x < 4; ++x) {
		auto& light = receiverLights[x + y * 4 + z * 12];
		const float depth = std::pow(10.0f, (z + 0.5f) / 4.0f);
		light.position[0] = ((x + 0.5f) / 4.0f * 2.0f - 1.0f - 0.17f) * depth;
		light.position[1] = (1.0f - (y + 0.5f) / 3.0f * 2.0f + 0.11f) * depth;
		light.position[2] = forwardSign * depth;
		light.radius = 0.001f;
	}
	lightDesc.ByteWidth = sizeof(receiverLights);
	initial.pSysMem = receiverLights.data();
	winrt::com_ptr<ID3D11Buffer> receiverBuffer;
	winrt::com_ptr<ID3D11ShaderResourceView> receiverSRV;
	if (FAILED(device->CreateBuffer(&lightDesc, &initial, receiverBuffer.put())) ||
		FAILED(device->CreateShaderResourceView(receiverBuffer.get(), &srvDesc, receiverSRV.put()))) return 27;
	CullingParameters gridCullParameters{};
	gridCullParameters.lightCount = cellCount;
	std::memcpy(gridCullParameters.clusterSize, gridParameters.clusterSize, sizeof(gridParameters.clusterSize));
	constantDesc.ByteWidth = sizeof(gridCullParameters);
	initial.pSysMem = &gridCullParameters;
	winrt::com_ptr<ID3D11Buffer> gridCullConstants, cellIndices, cellGrid;
	winrt::com_ptr<ID3D11UnorderedAccessView> cellIndicesUAV, cellGridUAV;
	if (FAILED(device->CreateBuffer(&constantDesc, &initial, gridCullConstants.put())) ||
		!makeOutput(cellCount * cellCount * 4, 4, cellIndices, cellIndicesUAV) ||
		!makeOutput(cellCount * 48, 48, cellGrid, cellGridUAV)) return 28;
	cullCB[0] = gridCullConstants.get();
	cullSRVs[0] = gridBoundsSRV.get(); cullSRVs[1] = receiverSRV.get();
	cullUAVs[1] = cellIndicesUAV.get(); cullUAVs[2] = cellGridUAV.get();
	context->ClearUnorderedAccessViewUint(counterUAV.get(), clear);
	context->CSSetConstantBuffers(0, 1, cullCB);
	context->CSSetShaderResources(0, 2, cullSRVs);
	context->CSSetUnorderedAccessViews(0, 3, cullUAVs, nullptr);
	context->CSSetShader(cullingShader.get(), nullptr, 0);
	context->Dispatch(1, 1, 1);
	context->CSSetShaderResources(0, 2, emptySRVs);
	context->CSSetUnorderedAccessViews(0, 3, emptyUAVs, nullptr);
	for (UINT cell = 0; cell < cellCount; ++cell) {
		const UINT offset = readUInt(cellGrid.get(), cellCount * 48, cell * 12);
		const UINT lightCount = readUInt(cellGrid.get(), cellCount * 48, cell * 12 + 1);
		if (lightCount > cellCount || offset > cellCount * cellCount - lightCount) return 29;
		bool found = false;
		for (UINT i = 0; i < lightCount; ++i)
			found |= readUInt(cellIndices.get(), cellCount * cellCount * 4, offset + i) == cell;
		if (!found) {
			std::cerr << "Projected receiver lost its co-located light in cell " << cell << '\n';
			return 30;
		}
	}
	std::cout << "PASS: all 48 asymmetric perspective cells retain their receiver lights\n";

	// Execute the production pixel-light selection block with two different
	// cluster-centre shortlists at the same receiver. A light contributing at
	// that receiver must not lose its shadow when only the tile shortlist changes.
	std::ifstream lightingFile(root / "distribution/PIXL-Shaders/Lighting.hlsl", std::ios::binary);
	std::string lighting{std::istreambuf_iterator<char>(lightingFile), {}};
	if (argc == 2 && std::string(argv[1]) == "--legacy-shortlist") {
		const std::string countExpression = "? numClusteredLights : 0u;";
		const std::string listExpression = "RadiantGrid::lightList[lightOffset +";
		const auto countAt = lighting.find(countExpression);
		const auto listAt = lighting.find(listExpression);
		if (countAt == std::string::npos || listAt == std::string::npos) return 24;
		lighting.replace(listAt, listExpression.size(), "clusteredContactCandidates[");
		lighting.replace(countAt, countExpression.size(), "? 4u : 0u;");
	}
	const auto selectionBegin = lighting.find("\tconst uint clusteredContactCandidateCount =");
	const auto selectionEnd = lighting.find("\t[loop] for (uint lightIndex = 0;", selectionBegin);
	if (selectionBegin == std::string::npos || selectionEnd == std::string::npos)
		return 19;
	const std::string selectionSource = R"(
namespace RadiantGrid {
#include "RadiantGrid/Common.hlsli"
static const uint NumStrictLights = 0;
static Light StrictLights[15];
static Light lights[5];
static uint lightList[5];
bool IsLightIgnored(Light light) { return false; }
}
namespace Color {
float RGBToLuminance(float3 c) { return dot(c, float3(0.2126,0.7152,0.0722)); }
float3 PointLight(float3 c, bool isLinear) { return c; }
}
float GetLocalLightAttenuation(float distance, float radius, float fadeZone, float sizeBias) { return 1; }
RWStructuredBuffer<uint4> result : register(u0);
[numthreads(2,1,1)] void main(uint id : SV_DispatchThreadID) {
    [unroll] for (uint i=0;i<5;++i) {
        RadiantGrid::Light light = (RadiantGrid::Light)0;
        light.color = float(i+1).xxx; light.fade=1; light.radius=100;
        light.positionWS=float4(0,0,-10,1);
        RadiantGrid::lights[i]=light; RadiantGrid::lightList[i]=i;
    }
    const bool localContactShadowsActive=true, screenSpaceLocalShadowsActive=true, sdfShadowsActive=false;
    const bool useStrictLightFallback=false;
    const uint numClusteredLights=5, worldFallbackLightCount=0, lightOffset=0, localContactLightCount=4, sdfLightCount=0;
    uint4 clusteredContactCandidates = id == 0 ? uint4(0,1,2,3) : uint4(1,2,3,4);
    uint4 clusteredSDFCandidates = 0xffffffffu;
    uint4 localContactLightIndices=0xffffffffu, screenSpaceLocalLightIndices=0xffffffffu, localSDFLightIndices=0xffffffffu;
    struct Receiver { float4 WorldPosition; }; Receiver input; input.WorldPosition=0;
#if 1
)" + lighting.substr(selectionBegin, selectionEnd-selectionBegin) + R"(
    result[id]=screenSpaceLocalLightIndices;
}
)";
	bytecode = nullptr; diagnostics = nullptr;
	if (FAILED(D3DCompile(selectionSource.data(), selectionSource.size(), "production-shadow-selection", nullptr,
		&includes, "main", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS |
			(argc == 2 ? 0u : D3DCOMPILE_WARNINGS_ARE_ERRORS),
		0, bytecode.put(), diagnostics.put()))) {
		if (diagnostics) std::cerr.write(static_cast<const char*>(diagnostics->GetBufferPointer()), diagnostics->GetBufferSize());
		return 20;
	}
	winrt::com_ptr<ID3D11ComputeShader> selectionShader;
	if (FAILED(device->CreateComputeShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, selectionShader.put()))) return 21;
	winrt::com_ptr<ID3D11Buffer> selectedLights;
	winrt::com_ptr<ID3D11UnorderedAccessView> selectedLightsUAV;
	if (!makeOutput(32,16,selectedLights,selectedLightsUAV)) return 22;
	ID3D11UnorderedAccessView* selectedOutput=selectedLightsUAV.get();
	context->CSSetUnorderedAccessViews(0,1,&selectedOutput,nullptr);
	context->CSSetShader(selectionShader.get(),nullptr,0); context->Dispatch(1,1,1);
	selectedOutput=nullptr; context->CSSetUnorderedAccessViews(0,1,&selectedOutput,nullptr);
	for (UINT word=0; word<8; ++word) {
		const auto expected=0x80000000u | (4u-word%4u);
		if (readUInt(selectedLights.get(),32,word)!=expected) {
			std::cerr<<"Cluster shortlist changed per-pixel shadow selection at word "<<word<<'\n';
			return 23;
		}
	}
	std::cout<<"PASS: production local-shadow ranking is independent of cluster-centre shortlists\n";
	return 0;
}
