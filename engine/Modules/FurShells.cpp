#include "FurShells.h"

#include "Globals.h"
#include "MaterialForge/MaterialForgeTessellation.h"
#include "Modules/OceanDynamics.h"
#include "Util.h"

#include <utility>
#include <atomic>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <cmath>

namespace
{
	winrt::com_ptr<ID3D11GeometryShader> furShellShader;
winrt::com_ptr<ID3D11GeometryShader> furShellModelSpaceShader;
	winrt::com_ptr<ID3D11GeometryShader> furShellSkinnedModelSpaceShader;
	winrt::com_ptr<ID3D11Buffer> furShellSettingsBuffer;
	std::atomic<float> furShellLength{ 1.8f };
	struct FurShellSettingsData { float length; float padding[3]{}; };
	thread_local bool furShellDrawPending = false;
thread_local bool furShellUseModelSpaceNormals = false;
	thread_local bool furShellUseSkinnedInterface = false;
std::atomic_flag furShellDrawLogged = ATOMIC_FLAG_INIT;
std::atomic_flag furShellDepthLogged = ATOMIC_FLAG_INIT;

	struct FurDepthVariant
	{
		winrt::com_ptr<ID3D11Device> device;
		winrt::com_ptr<ID3D11DepthStencilState> source;
		winrt::com_ptr<ID3D11DepthStencilState> shells;
	};
	struct FurDepthKey
	{
		ID3D11Device* device{};
		ID3D11DepthStencilState* source{};
		bool operator==(const FurDepthKey&) const = default;
	};
	struct FurDepthKeyHash
	{
		std::size_t operator()(const FurDepthKey& key) const noexcept
		{
			const auto deviceHash = std::hash<ID3D11Device*>{}(key.device);
			const auto stateHash = std::hash<ID3D11DepthStencilState*>{}(key.source);
			return deviceHash ^ (stateHash + 0x9E3779B9u + (deviceHash << 6u) + (deviceHash >> 2u));
		}
	};
	std::mutex furDepthVariantsMutex;
	std::unordered_map<FurDepthKey, FurDepthVariant, FurDepthKeyHash> furDepthVariants;

	ID3D11DepthStencilState* GetFurDepthVariant(
		ID3D11Device* device,
		ID3D11DepthStencilState* source)
	{
		if (!device)
			return nullptr;
		std::scoped_lock lock(furDepthVariantsMutex);
		const FurDepthKey key{ device, source };
		if (const auto it = furDepthVariants.find(key); it != furDepthVariants.end())
			return it->second.shells.get();

		D3D11_DEPTH_STENCIL_DESC desc{};
		if (source) {
			source->GetDesc(&desc);
			if (!desc.DepthEnable)
				return nullptr;
		} else {
			desc.DepthEnable = TRUE;
			desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
			desc.DepthFunc = D3D11_COMPARISON_LESS;
			desc.StencilEnable = FALSE;
			desc.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
			desc.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
			desc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
			desc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
			desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
			desc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
			desc.BackFace = desc.FrontFace;
		}

		// The base mesh has already populated Skyrim's depth prepass. Its ordinary
		// color pass may use EQUAL, which rejects every normal-extruded fur layer.
		// Keep the original stencil contract, but let nearer shell fragments pass
		// and write depth so the fur participates correctly in subsequent draws.
		desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
		desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		winrt::com_ptr<ID3D11DepthStencilState> shellState;
		if (FAILED(device->CreateDepthStencilState(&desc, shellState.put())))
			return nullptr;

		FurDepthVariant variant{};
		variant.device.copy_from(device);
		if (source)
			variant.source.copy_from(source);
		variant.shells = std::move(shellState);
		auto [it, inserted] = furDepthVariants.emplace(key, std::move(variant));
		(void)inserted;
		return it->second.shells.get();
	}

	struct FurStageState
	{
		ID3D11DeviceContext* context = nullptr;
		ID3D11GeometryShader* previousShader = nullptr;
		ID3D11ClassInstance* previousInstances[D3D11_SHADER_MAX_INTERFACES]{};
		UINT previousInstanceCount = D3D11_SHADER_MAX_INTERFACES;
		ID3D11Buffer* previousGSFrame = nullptr;
		ID3D11Buffer* vertexFrame = nullptr;
		ID3D11Buffer* previousSettings = nullptr;
		ID3D11DepthStencilState* previousDepthState = nullptr;
		UINT previousStencilReference = 0;
		bool active = false;

		explicit FurStageState(ID3D11DeviceContext* context)
		{
			ID3D11GeometryShader* selectedShader = furShellUseModelSpaceNormals
				? (furShellUseSkinnedInterface ? furShellSkinnedModelSpaceShader.get() : furShellModelSpaceShader.get())
				: furShellShader.get();
			if (!context || !selectedShader)
				return;
			this->context = context;

			context->GSGetShader(&previousShader, previousInstances, &previousInstanceCount);
			context->GSGetConstantBuffers(12, 1, &previousGSFrame);
			context->VSGetConstantBuffers(12, 1, &vertexFrame);
			context->GSGetConstantBuffers(13, 1, &previousSettings);
			context->OMGetDepthStencilState(&previousDepthState, &previousStencilReference);
			ID3D11DepthStencilState* shellDepthState =
				GetFurDepthVariant(globals::d3d::device, previousDepthState);
		if (!vertexFrame || !shellDepthState || !furShellSettingsBuffer)
				return;

			ID3D11ClassInstance* const* noInstances = nullptr;
			context->GSSetConstantBuffers(12, 1, &vertexFrame);
			ID3D11Buffer* shellSettings = furShellSettingsBuffer.get();
			context->GSSetConstantBuffers(13, 1, &shellSettings);
			context->OMSetDepthStencilState(shellDepthState, previousStencilReference);
			context->GSSetShader(selectedShader, noInstances, 0);
			active = true;
		}

		~FurStageState()
		{
			if (active) {
				context->GSSetShader(previousShader, previousInstances, previousInstanceCount);
				context->GSSetConstantBuffers(12, 1, &previousGSFrame);
				context->GSSetConstantBuffers(13, 1, &previousSettings);
				context->OMSetDepthStencilState(previousDepthState, previousStencilReference);
			}
			if (previousShader)
				previousShader->Release();
			if (previousGSFrame)
				previousGSFrame->Release();
			if (vertexFrame)
				vertexFrame->Release();
			if (previousSettings)
				previousSettings->Release();
			if (previousDepthState)
				previousDepthState->Release();
			for (UINT i = 0; i < previousInstanceCount; ++i) {
				if (previousInstances[i])
					previousInstances[i]->Release();
			}
		}
	};

	template <class Draw>
	void InvokeDraw(ID3D11DeviceContext* context, Draw&& draw, bool allowTessellation = false)
	{
		const bool useFurShells = std::exchange(furShellDrawPending, false);
		MaterialForgeTessellation::DrawState tessellationState(context, allowTessellation && !useFurShells);
		if (!useFurShells) {
			draw();
			return;
		}
		FurStageState state(context);
		if (state.active && !furShellDrawLogged.test_and_set())
			logger::info("[FurShells] First classified draw reached D3D11; procedural shell geometry is active");
		if (state.active && !furShellDepthLogged.test_and_set())
			logger::info("[FurShells] Shell draws use a cached LESS_EQUAL depth variant so extruded layers survive Skyrim depth-equal passes");
		draw();
	}

	struct DrawIndexedHook
	{
		static void thunk(ID3D11DeviceContext* self, UINT indexCount, UINT startIndex, INT baseVertex)
		{
			auto& ocean = globals::pipeline::oceanDynamics;
			const bool replaced = ocean.BeginWaterDraw(self, true);
			InvokeDraw(self, [&] { func(self, indexCount, startIndex, baseVertex); }, true);
			if (replaced) {
				ocean.EndWaterDraw(self);
			}
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
	struct DrawHook
	{
		static void thunk(ID3D11DeviceContext* self, UINT vertexCount, UINT startVertex)
		{
			InvokeDraw(self, [&] { func(self, vertexCount, startVertex); });
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
	struct DrawIndexedInstancedHook
	{
		static void thunk(ID3D11DeviceContext* self, UINT indexCount, UINT instanceCount, UINT startIndex, INT baseVertex, UINT startInstance)
		{
			auto& ocean = globals::pipeline::oceanDynamics;
			const bool eligible = instanceCount == 1 && startInstance == 0;
			const bool replaced = eligible && ocean.BeginWaterDraw(self, true);
			InvokeDraw(self, [&] {
				func(self, indexCount, instanceCount, startIndex, baseVertex, startInstance);
			}, eligible);
			if (replaced)
				ocean.EndWaterDraw(self);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
	struct DrawInstancedHook
	{
		static void thunk(ID3D11DeviceContext* self, UINT vertexCount, UINT instanceCount, UINT startVertex, UINT startInstance)
		{
			InvokeDraw(self, [&] { func(self, vertexCount, instanceCount, startVertex, startInstance); });
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
	struct DrawAutoHook
	{
		static void thunk(ID3D11DeviceContext* self)
		{
			InvokeDraw(self, [&] { func(self); });
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
	struct DrawIndexedInstancedIndirectHook
	{
		static void thunk(ID3D11DeviceContext* self, ID3D11Buffer* args, UINT offset)
		{
			InvokeDraw(self, [&] { func(self, args, offset); });
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
	struct DrawInstancedIndirectHook
	{
		static void thunk(ID3D11DeviceContext* self, ID3D11Buffer* args, UINT offset)
		{
			InvokeDraw(self, [&] { func(self, args, offset); });
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
}

namespace FurShells
{
	void Initialize()
	{
		if (!globals::d3d::device)
			return;
		if (!furShellSettingsBuffer) {
			FurShellSettingsData initial{ furShellLength.load(std::memory_order_relaxed), {} };
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = sizeof(FurShellSettingsData);
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			D3D11_SUBRESOURCE_DATA data{};
			data.pSysMem = &initial;
			globals::d3d::device->CreateBuffer(&desc, &data, furShellSettingsBuffer.put());
		}
		if (furShellShader && furShellModelSpaceShader && furShellSkinnedModelSpaceShader)
			return;
		auto compile = [](const std::vector<std::pair<const char*, const char*>>& defines) {
			return reinterpret_cast<ID3D11GeometryShader*>(
				Util::CompileShader(L"Data\\PIXL-Shaders\\FurShellGS.hlsl", defines, "gs_5_0"));
		};
		if (!furShellShader) {
			auto* compiled = compile({});
			if (compiled) {
				furShellShader.copy_from(compiled);
				compiled->Release();
			}
		}
		if (!furShellModelSpaceShader) {
			auto* compiled = compile({ { "MODELSPACENORMALS", "1" } });
			if (compiled) {
				furShellModelSpaceShader.copy_from(compiled);
				compiled->Release();
			}
		}
		if (!furShellSkinnedModelSpaceShader) {
			auto* compiled = compile({ { "MODELSPACENORMALS", "1" }, { "SKINNED", "1" } });
			if (compiled) {
				furShellSkinnedModelSpaceShader.copy_from(compiled);
				compiled->Release();
			}
		}
		if (furShellShader && furShellModelSpaceShader && furShellSkinnedModelSpaceShader)
			logger::info("[FurShells] Smooth, static model-space, and skinned model-space shell shaders initialized");
		else
			logger::error("[FurShells] One or more geometry shader variants failed to compile");
	}

	void SetShellLength(float length) noexcept
	{
		if (!std::isfinite(length))
			return;
		length = std::clamp(length, 0.2f, 4.0f);
		furShellLength.store(length, std::memory_order_relaxed);
		if (furShellSettingsBuffer && globals::d3d::context) {
			const FurShellSettingsData data{ length, {} };
			globals::d3d::context->UpdateSubresource(furShellSettingsBuffer.get(), 0, nullptr, &data, 0, 0);
		}
	}

	void InstallD3DContextHooks(ID3D11DeviceContext* context)
	{
		if (!context)
			return;
		stl::detour_vfunc<12, DrawIndexedHook>(context);
		stl::detour_vfunc<13, DrawHook>(context);
		stl::detour_vfunc<20, DrawIndexedInstancedHook>(context);
		stl::detour_vfunc<21, DrawInstancedHook>(context);
		stl::detour_vfunc<38, DrawAutoHook>(context);
		stl::detour_vfunc<39, DrawIndexedInstancedIndirectHook>(context);
		stl::detour_vfunc<40, DrawInstancedIndirectHook>(context);
	}

	void ArmForNextDraw(bool enabled, bool modelSpaceNormals, bool skinned)
	{
		const auto* selectedShader = modelSpaceNormals
			? (skinned ? furShellSkinnedModelSpaceShader.get() : furShellModelSpaceShader.get())
			: furShellShader.get();
		furShellDrawPending = enabled && selectedShader;
		furShellUseModelSpaceNormals = modelSpaceNormals;
		furShellUseSkinnedInterface = skinned;
	}

	ID3D11GeometryShader* GetShader() noexcept
	{
		return furShellShader.get();
	}
}
