#pragma once

#include <DirectXMath.h>

#include "Buffer.h"
#include "RE/B/BSShadowDirectionalLight.h"

#define ALBEDO RE::RENDER_TARGETS::kINDIRECT
#define SPECULAR RE::RENDER_TARGETS::kINDIRECT_DOWNSCALED
#define REFLECTANCE RE::RENDER_TARGETS::kRAWINDIRECT
#define NORMALROUGHNESS RE::RENDER_TARGETS::kRAWINDIRECT_DOWNSCALED
#define MASKS RE::RENDER_TARGETS::kRAWINDIRECT_PREVIOUS
#define MASKS2 RE::RENDER_TARGETS::kRAWINDIRECT_PREVIOUS_DOWNSCALED

class Deferred
{
public:
	/** @brief Gets the singleton instance. */
	static Deferred* GetSingleton()
	{
		static Deferred singleton;
		return &singleton;
	}

	struct alignas(16) DirectionalShadowLightData
	{
		float4x4 ShadowProj[2];
		float4x4 InvShadowProj[2];
		float2 EndSplitDistances;
		float2 StartSplitDistances;
	};
	STATIC_ASSERT_ALIGNAS_16(DirectionalShadowLightData);
	static_assert(sizeof(DirectionalShadowLightData) == 272, "Directional shadow t98 buffer must match GPU layout");

	/** @brief Creates render targets, samplers, and the directional shadow structured buffer. */
	void SetupResources();

	/** @brief Runs feature reflection prepasses with render targets unbound. */
	void ReflectionsPrepasses();

	/** @brief Runs early feature prepasses and uploads shadow light data after shadow map rendering. */
	void EarlyPrepasses();

	/** @brief Begins deferred rendering by binding GBuffer targets and overriding blend states. */
	void StartDeferred();

	/** @brief Replaces engine blend states with deferred-compatible variants for GBuffer output. */
	void OverrideBlendStates();

	/** @brief Restores original forward blend states after deferred pass completes. */
	void ResetBlendStates();

	/** @brief Dispatches the deferred composite compute shader and post-deferred feature passes. */
	void DeferredPasses();

	/** @brief Ends deferred rendering, restores forward targets, and triggers DeferredPasses. */
	void EndDeferred();

	/** @brief Runs feature prepasses between StartDeferred and geometry rendering. */
	void PrepassPasses();

	/** @brief Releases cached composite compute shaders, forcing recompilation on next use. */
	void ClearShaderCache();

	/**
	 * @brief Gets or compiles the exterior deferred composite compute shader.
	 * @return Cached or freshly compiled compute shader with feature-dependent defines.
	 */
	ID3D11ComputeShader* GetComputeMainComposite();

	/**
	 * @brief Gets or compiles the interior deferred composite compute shader.
	 * @return Cached or freshly compiled compute shader with INTERIOR and feature-dependent defines.
	 */
	ID3D11ComputeShader* GetComputeMainCompositeInterior();

	/**
	 * @brief Uploads directional shadow parameters from BSShadowDirectionalLight to the t98 structured buffer.
	 *
	 * Reads cascade splits and world-to-shadow projections. Called during EarlyPrepasses
	 * once shadow maps have been rendered.
	 */
	void CopyShadowLightData();

	ID3D11BlendState* deferredBlendStates[7][2][13][2];
	ID3D11BlendState* forwardBlendStates[7][2][13][2];

	RE::RENDER_TARGET forwardRenderTargets[4];

	ID3D11ComputeShader* mainCompositeCS = nullptr;
	ID3D11ComputeShader* mainCompositeInteriorCS = nullptr;

	// Directional shadow structured buffer (t98): cascade splits and projections.
	Buffer* directionalShadowLights = nullptr;

	struct alignas(16) PixlGIDebugData
	{
		uint mode = 0;
		float gain = 1.0f;
		uint specularOcclusionEnabled = 1;
		float specularOcclusionStrength = 0.9f;
	};
	STATIC_ASSERT_ALIGNAS_16(PixlGIDebugData);
	eastl::unique_ptr<ConstantBuffer> pixlGIDebugCB;

	bool deferredPass = false;

	ID3D11SamplerState* linearSampler = nullptr;
	ID3D11SamplerState* pointSampler = nullptr;

private:
	template <typename T>
	void SetShadowCascadeParameters(T& lightData, DirectionalShadowLightData& dd);

public:
	struct Hooks
	{
		struct Main_RenderShadowMaps
		{
			static void thunk();
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Main_RenderWorld
		{
			static void thunk(bool a1);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Main_RenderPlayerView
		{
			static void thunk(void* a1, bool a2, bool a3);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Main_RenderWorld_Start
		{
			static void thunk(RE::BSBatchRenderer* This, uint32_t StartRange, uint32_t EndRanges, uint32_t RenderFlags, int GeometryGroup);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Main_RenderWorld_BlendedDecals
		{
			static void thunk(RE::BSShaderAccumulator* This, uint32_t RenderFlags);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSCubeMapCamera_RenderCubemap
		{
			static void thunk(RE::NiAVObject* camera, int a2, bool a3, bool a4, bool a5);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Main_RenderFirstPersonView
		{
			static void thunk(bool a1, bool a2);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Renderer_ResetState
		{
			static void thunk(void* This);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		/** @brief Installs all deferred rendering hooks into the game's vtables and call sites. */
		static void Install()
		{
			stl::write_vfunc<0x35, BSCubeMapCamera_RenderCubemap>(RE::VTABLE_BSCubeMapCamera[0]);

			// These offsets are not valid call boundaries in Steam 1.7.104's
			// Main::RenderPlayerView. In particular, +0x2EC lands inside an
			// instruction and overwrites bytes executed at +0x2F0 during world
			// startup. Leave the engine path intact until verified 1.7.104 sites
			// are available.
			const bool hasUnverified117104Sites =
				REL::Module::get().version() == SKSE::RUNTIME_SSE_1_7_104;
			if (hasUnverified117104Sites) {
				logger::warn("[Deferred] Skipping unverified Main::RenderPlayerView call-site hooks on Skyrim 1.7.104.");
				// Own the full player-view entry so reconstruction can configure the
				// viewport before any depth, shadow, or world draw in this runtime.
				stl::detour_thunk<Main_RenderPlayerView>(REL::RelocationID(35560, 36559));
				// FrameAnnotations normally owns this safe whole-function detour.
				// Deferred takes it on 1.7.104 to restore the in-world frame bracket.
				stl::detour_thunk<Main_RenderWorld>(REL::RelocationID(100424, 107142));
			} else {
				stl::write_thunk_call<Main_RenderShadowMaps>(REL::RelocationID(35560, 36559).address() + REL::Relocate(0x2EC, 0x2EC));
				stl::write_thunk_call<Main_RenderWorld>(REL::RelocationID(35560, 36559).address() + REL::Relocate(0x831, 0x841));
				stl::write_thunk_call<Main_RenderFirstPersonView>(REL::RelocationID(35560, 36559).address() + REL::Relocate(0x944, 0x954));
			}

			stl::write_thunk_call<Main_RenderWorld_Start>(REL::RelocationID(99938, 106583).address() + REL::Relocate(0x8E, 0x84));
			stl::write_thunk_call<Main_RenderWorld_BlendedDecals>(REL::RelocationID(99938, 106583).address() + REL::Relocate(0x319, 0x308));

			stl::detour_thunk<Renderer_ResetState>(REL::RelocationID(75570, 77371));

			logger::info("[Deferred] Installed hooks");
		}
	};
};
