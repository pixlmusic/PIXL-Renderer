#include "HybridGIGPUTestSupport.h"

#include "Modules/HybridGI/SceneVoxelizer.h"

#include <limits>

namespace
{
	constexpr std::uint32_t kDimension = 256;

	struct SceneVoxelizeCB
	{
		float worldMin[3]{};
		float voxelSize{};
	std::uint32_t dimension{};
	std::uint32_t triangleCount{};
	std::uint32_t padding[2]{};
	std::int32_t scrollOffset[3]{};
	std::uint32_t updateMode{};
	};
	static_assert(sizeof(SceneVoxelizeCB) == 48);

	struct Volume
	{
		ComPtr<ID3D11Texture3D> texture;
		ComPtr<ID3D11ShaderResourceView> srv;
		ComPtr<ID3D11UnorderedAccessView> uav;
	};

	Volume CreateVolume(ID3D11Device* device)
	{
		D3D11_TEXTURE3D_DESC desc{};
		desc.Width = kDimension;
		desc.Height = kDimension;
		desc.Depth = kDimension;
		desc.MipLevels = 1;
		desc.Format = DXGI_FORMAT_R32_UINT;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		Volume result;
		Check(device->CreateTexture3D(&desc, nullptr, &result.texture));
		Check(device->CreateShaderResourceView(result.texture.Get(), nullptr, &result.srv));
		Check(device->CreateUnorderedAccessView(result.texture.Get(), nullptr, &result.uav));
		return result;
	}
}

int main()
{
	try {
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
			D3D11_SDK_VERSION, &device, nullptr, &context));

		const auto source = Read("pipeline/Hybrid GI/Kernels/HybridGI/sceneVoxelize.cs.hlsl");
		ComPtr<ID3DBlob> bytecode, errors;
		const auto compileResult = D3DCompile(source.data(), source.size(), "sceneVoxelize.cs.hlsl", nullptr, nullptr,
			"main", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS |
				D3DCOMPILE_OPTIMIZATION_LEVEL3,
			0, &bytecode, &errors);
		if (FAILED(compileResult) && errors)
			std::cerr << static_cast<const char*>(errors->GetBufferPointer());
		Check(compileResult);
		ComPtr<ID3D11ComputeShader> shader;
		Check(device->CreateComputeShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, &shader));

		// This right triangle covers the lower-left half of a 256x256 projected
		// footprint. The sample at row 200 is after candidate 16,384, the old
		// hard stop, so this dispatch fails if large surfaces are truncated.
		PIXL::HybridGIInternal::GpuSceneTriangle triangle{};
		triangle.a = { 0.0f, 0.0f, 128.0f };
		triangle.b = { 256.0f, 0.0f, 128.0f };
		triangle.c = { 0.0f, 256.0f, 128.0f };
		triangle.projectedCellCount = kDimension * kDimension;
		triangle.textureIndex = std::numeric_limits<std::uint32_t>::max();

		D3D11_BUFFER_DESC triangleDesc{};
		triangleDesc.ByteWidth = sizeof(triangle);
		triangleDesc.StructureByteStride = sizeof(triangle);
		triangleDesc.Usage = D3D11_USAGE_DEFAULT;
		triangleDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		triangleDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		D3D11_SUBRESOURCE_DATA triangleData{ &triangle, 0, 0 };
		ComPtr<ID3D11Buffer> triangleBuffer;
		Check(device->CreateBuffer(&triangleDesc, &triangleData, &triangleBuffer));
		ComPtr<ID3D11ShaderResourceView> triangleSrv;
		Check(device->CreateShaderResourceView(triangleBuffer.Get(), nullptr, &triangleSrv));

		SceneVoxelizeCB constants{};
		constants.voxelSize = 1.0f;
		constants.dimension = kDimension;
		constants.triangleCount = 1;
		D3D11_BUFFER_DESC cbDesc{};
		cbDesc.ByteWidth = sizeof(constants);
		cbDesc.Usage = D3D11_USAGE_DEFAULT;
		cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		D3D11_SUBRESOURCE_DATA cbData{ &constants, 0, 0 };
		ComPtr<ID3D11Buffer> constantBuffer;
		Check(device->CreateBuffer(&cbDesc, &cbData, &constantBuffer));

		Volume occupancy = CreateVolume(device.Get());
		Volume surface = CreateVolume(device.Get());
		Volume material = CreateVolume(device.Get());
		const UINT clear[4]{};
		context->ClearUnorderedAccessViewUint(occupancy.uav.Get(), clear);
		context->ClearUnorderedAccessViewUint(surface.uav.Get(), clear);
		context->ClearUnorderedAccessViewUint(material.uav.Get(), clear);

		std::array<ID3D11ShaderResourceView*, 17> srvs{};
		srvs[0] = triangleSrv.Get();
		ID3D11UnorderedAccessView* uavs[] = { occupancy.uav.Get(), surface.uav.Get(), material.uav.Get() };
		ID3D11Buffer* cb = constantBuffer.Get();
		context->CSSetShaderResources(0, static_cast<UINT>(srvs.size()), srvs.data());
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);
		context->CSSetConstantBuffers(4, 1, &cb);
		context->CSSetShader(shader.Get(), nullptr, 0);
		context->Dispatch(1, 1, 1);

		ID3D11ShaderResourceView* nullSrvs[17]{};
		ID3D11UnorderedAccessView* nullUavs[3]{};
		ID3D11Buffer* nullBuffer = nullptr;
		context->CSSetShaderResources(0, ARRAYSIZE(nullSrvs), nullSrvs);
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(nullUavs), nullUavs, nullptr);
		context->CSSetConstantBuffers(4, 1, &nullBuffer);

		D3D11_TEXTURE3D_DESC stagingDesc{};
		stagingDesc.Width = kDimension;
		stagingDesc.Height = kDimension;
		stagingDesc.Depth = kDimension;
		stagingDesc.MipLevels = 1;
		stagingDesc.Format = DXGI_FORMAT_R32_UINT;
		stagingDesc.Usage = D3D11_USAGE_STAGING;
		stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		ComPtr<ID3D11Texture3D> staging;
		Check(device->CreateTexture3D(&stagingDesc, nullptr, &staging));
		context->CopyResource(staging.Get(), occupancy.texture.Get());
		D3D11_MAPPED_SUBRESOURCE mapped{};
		Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
		const auto* sample = reinterpret_cast<const std::uint32_t*>(
			static_cast<const std::byte*>(mapped.pData) + 128u * mapped.DepthPitch + 200u * mapped.RowPitch + 8u * sizeof(std::uint32_t));
		const auto* outside = reinterpret_cast<const std::uint32_t*>(
			static_cast<const std::byte*>(mapped.pData) + 128u * mapped.DepthPitch + 200u * mapped.RowPitch + 200u * sizeof(std::uint32_t));
		const bool coveredLateRow = *sample == 1u;
		const bool rejectedOutside = *outside == 0u;
		context->Unmap(staging.Get(), 0);
		if (!coveredLateRow)
			throw std::runtime_error("late projected row was not voxelized; broad triangle coverage is truncated");
		if (!rejectedOutside)
			throw std::runtime_error("voxelizer marked a cell outside the triangle footprint");

		std::cout << "PASS: 256x256 dominant-axis triangle reaches row 200; outside sample remains empty (WARP).\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
