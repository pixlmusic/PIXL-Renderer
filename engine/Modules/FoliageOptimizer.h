// PIXL Renderer - Foliage Optimizer
// Derived from Community Shaders 1.9.1 Grass Optimizations and substantially adapted for PIXL Renderer.
// Upstream and contributor copyrights remain with their respective authors.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <d3d11_1.h>

#include "Buffer.h"
#include "FoliageOptimizer/GrassBucketStore.h"
#include "Utils/VersionedRelocation.h"

/** @brief Rewrites vanilla grass rendering with a bucket based system utilizing indirect draws and compute shader per instance culling. */
struct FoliageOptimizer : RenderModule
{
public:
	virtual inline std::string GetName() override { return "Foliage Optimizer"; }
	virtual std::string GetDisplayName() override { return T("feature.foliage_optimizer.name", "Foliage Optimizer"); }
	virtual inline std::string GetShortName() override { return "FoliageOptimizer"; }
	virtual inline std::string_view GetShaderDefineName() override { return "FOLIAGE_OPTIMIZER"; }
	virtual std::string_view GetCategory() const override { return ModuleGroups::kGrass; }

	/** @brief Returns true only for the Grass shader type. */
	bool HasShaderDefine(RE::BSShader::Type shaderType) override;

	/** @brief Returns a description and list of key features for the UI summary. */
	virtual std::pair<std::string, std::vector<std::string>> GetModuleSummary() override
	{
		return { T("feature.grass_optimizations.description", "Rewrites grass rendering around GPU-driven culling and instancing, consolidating thousands of engine draw calls into a handful of indirect draws and removing hidden grass before it costs anything."),
			{ T("feature.grass_optimizations.key_feature_1", "Consolidates per-shape grass draws into one instanced indirect draw per grass type"),
				T("feature.grass_optimizations.key_feature_2", "GPU compute culling of individual instances by frustum, distance and projected size"),
				T("feature.grass_optimizations.key_feature_3", "Hi-Z occlusion culling skips grass hidden behind objects before the vertex shader runs"),
				T("feature.grass_optimizations.key_feature_4", "Configurable render distance beyond the vanilla INI cap with density scaling"),
				T("feature.grass_optimizations.key_feature_5", "Optional mesh-swap LOD and simplified shading for distant grass") } };
	};

	struct Settings
	{
		float MinPixelSize = 6.8f;
		float FullDetailPixelSize = 45.6f;
		float MinDensity = 0.079f;
		float MeshCostBias = 0.429f;
		float CostBiasStartDistance = 2587.0f;
		float InvisibleFadeCull = 0.0f;
		float RenderDistanceOverride = 100000.0f;
		float EdgeFadeStart = 0.625f;
		bool EnableOcclusionCulling = true;
		float SimpleShadingPixelSize = 4.0f;
		float OcclusionBias = 0.001f;
		float CollisionDistance = 2048.0f;
		bool EnableMeshLOD = true;
		bool EnableMidLOD = true;
		float MidLODPixelSize = 8.0f;
		bool EnableFarLOD = true;
		float FarLODPixelSize = 4.0f;
		float MeshLODBandPixels = 3.0f;
		// Director overrides are evaluated transiently and never mutate the saved
		// gameplay values above. This makes entering/leaving Photo or Video Mode
		// deterministic even if a capture is interrupted.
		bool EnableDirectorBoost = true;
		float PhotoDensityScale = 1.71f;
		float PhotoRangeScale = 1.77f;
		float PhotoFadeScale = 1.20f;
		float VideoDensityScale = 1.15f;
		float VideoRangeScale = 1.10f;
		float VideoFadeScale = 1.10f;
		float FrustumGuardBand = 128.0f;
		float DirectorFrustumGuardBand = 384.0f;
	};

	Settings settings;

	/** @brief Draws the ImGui settings panel for grass optimizations configuration. */
	virtual void DrawSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;

	/** @brief Creates the constant buffers, bucket store resources and deferred context. */
	virtual void SetupResources() override;

	/** @brief Releases the cached compute shaders so they recompile on next use. */
	virtual void ClearShaderCache() override;

	/** @brief Installs the grass capture, culling and draw hooks after all plugins have loaded. */
	virtual void PostPostLoad() override;

	/** @brief Returns the instance culling compute shader, compiling it on first use. */
	ID3D11ComputeShader* GetCullCS();

	struct alignas(16) CullParamsCB
	{
		float frustumPlanes[6][4];

		float minPixelSize;
		float fullDetailPixelSize;
		float lodMinKeep;
		float lodFadeBand;

		float meshCostBias;
		float projScale;
		float maxDistSq;
		float edgeFadeStart;

		float alphaParam1;
		float alphaParam2;
		float fadeNow;
		float fadeInTimeRcp;

		float invisibleFadeCull;
		float simpleShadingPixelSize;
		float collisionDistSq;
		float midLODPixelSize;

		float meshLODBandPx;
		float hiZEnabled;
		float hiZSizeX;
		float hiZSizeY;

		float hiZTexelPixels;
		float hiZMipCount;
		float occlusionBias;
		float costBiasStartDist;

		float farLODPixelSize;
		float pad0[3];
	};
	STATIC_ASSERT_ALIGNAS_16(CullParamsCB);

	struct alignas(16) CullBucketCB
	{
		uint32_t instanceCount;
		float wavePeriod;
		float timeBase;
		float prevTimeBase;
		float boundCenter[3];
		float modelRadius;
		float distScale;
		float minPixelScale;
		float isComplex;
		float midLODEnabled;
		uint32_t sliceTableOffset;
		uint32_t sliceCount;
		float farLODEnabled;
		float pad;
	};
	STATIC_ASSERT_ALIGNAS_16(CullBucketCB);

	/** @brief The six frustum planes transposed to a structure of arrays, with two padding slots to fit optimized SSE/AVX instructions  */
	struct FrustumSoA
	{
		__m128 nx[2], ny[2], nz[2], d[2];
	};

	/** @brief Transposes the frustum once per frame. Inactive planes become always-pass slots. */
	static void BuildFrustumSoA(FrustumSoA& out, const RE::NiFrustumPlanes& f);

	/** @brief AABB vs frustum, corners passed as SIMD vectors (xyz in lanes 0-2). */
	static bool AabbVisible(const FrustumSoA& f, __m128 lo, __m128 hi);

	/** @brief Derives world-space frustum planes from the camera frustum and transform. */
	void ComputeFrustumPlanes(RE::NiFrustumPlanes& out, const RE::NiFrustum& viewFrustum, const RE::NiTransform& transform);

	/** @brief Once-per-frame grass update called in BSGrassShader::SetupGeometry: applies staged captures/removals, uploads dirty buckets, builds the Hi-Z pyramid and issues the culling dispatches. */
	void UpdateGrass();

	/** @brief Merges this bucket's slices into runs of contiguous buffer ranges that share a cell, for the per-bucket slice table. */
	void MergeSlicesIntoRuns(GrassBucket& b);

	/** @brief Appends this bucket's visible slice runs to sliceTableCPU and records the window in the bucket. */
	void CullBucketSlices(GrassBucket& b, const FrustumSoA& frustumSoA, __m128 camPosV);

	/** @brief Fills the per-bucket cull constant buffer, uploads the slice table and issues the cull dispatches. */
	void UploadCullState(ID3D11Device* device, ID3D11DeviceContext* ctx, uint32_t visibleBuckets);

	/** @brief Binds a bucket's resources and dispatches the instance culling compute shader. */
	void CullBucket(GrassBucket& b, ID3D11DeviceContext* ctx);

	/** @brief Grows the slotted per-bucket constant buffer to hold at least `slots` entries. */
	bool EnsureCullBucketCapacity(uint32_t slots, ID3D11Device* device);

	GrassBucketStore bucketStore;
	uint32_t lastFrame = UINT32_MAX;

	ID3D11DeviceContext1* ctx1 = nullptr;

	ID3D11ComputeShader* cullCS = nullptr;
	bool cullCompileAttempted = false;
	bool runtimeReady = false;
	bool hooksInstalled = false;

	std::unique_ptr<ConstantBuffer> cullParamsCB;
	// Slotted per-bucket constants bound via CSSetConstantBuffers1: one 256-byte slot per visible
	// bucket, one map fills them all, recreated when the bucket count outgrows it.
	std::unique_ptr<ConstantBuffer> cullBucketCB;
	uint32_t cullBucketCBSlots = 0;
	static constexpr uint32_t kSlotBytes = 256;

	// Shared per-frame table of visible slice ranges, indexed by each bucket's window.
	std::unique_ptr<Buffer> sliceTable;
	uint32_t sliceTableCapacity = 0;
	std::vector<std::pair<uint32_t, uint32_t>> sliceTableCPU;

	float timeAccum = 0.0f;
	float fadeInTimeRcp = 0.0f;
	float timeBase = 0.0f;
	float prevTimeBase = 0.0f;
	float grassStartFadeDistance = 0.0f;
	float vanillaMaxDistance = 0.0f;
	float maxGrassDistance = 0.0f;
	float maxDistSq = 0.0f;
	float runtimeFrustumGuardBand = 128.0f;

	// Culling ownership can move from Skyrim's gameplay camera to a CameraSuite
	// capture camera without rebuilding the grass buckets. Keep a small amount of
	// camera history so a Hi-Z pyramid produced for the old view is never trusted
	// for the new view. This is the PIXL equivalent of per-camera culling state.
	RE::NiCamera* lastCullCamera = nullptr;
	RE::NiPoint3 lastCullCameraPosition{};
	bool haveCullCameraState = false;
	bool lastCullWasDirector = false;
	uint32_t cameraTransitionFrames = 0;

	struct Hooks
	{
		struct BSMultiStreamInstanceTriShape_dtor
		{
			static void thunk(RE::BSMultiStreamInstanceTriShape* This);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSMultiStreamInstanceTriShape_OnVisible
		{
			static void thunk(RE::BSMultiStreamInstanceTriShape* This, RE::NiCullingProcess* process, std::int32_t alphaGroupIndex);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct DoneAddingInstances
		{
			static void thunk(RE::BSMultiStreamInstanceTriShape* geometry, RE::BSTArray<std::uint32_t>& a_instances);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSGrassShader_SetupGeometry
		{
			static void thunk(RE::BSShader* This, RE::BSRenderPass* a2, std::uint32_t flags);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// The instance allocation is populated through Skyrim's GID/group stream
		// readers. Without these capture points the optimizer sees no usable source
		// records and every setting silently falls back to vanilla grass.
		struct AddQueuedGroupGIDBuffer
		{
			static std::uint32_t thunk(RE::BSMultiStreamInstanceTriShape*, PIXLGrassGroupHeader*, std::uint16_t*, RE::BSTArray<std::uint32_t>&);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct AddGroupGIDBuffer
		{
			static std::uint32_t thunk(RE::BSMultiStreamInstanceTriShape*, PIXLGrassGroupHeader*, std::uint16_t*);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ReadGroupHeaderStreamTraits
		{
			static void thunk(RE::BSStreamHeader*, PIXLGrassGroupHeader*, uint32_t);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ReadInstanceGroupStreamTraits
		{
			static void thunk(RE::BSStreamHeader*, uint16_t*, uint32_t);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct AddGroupQueuedGIDFile
		{
			static void thunk(RE::BSMultiStreamInstanceTriShape*, RE::BSStream*, RE::BSTArray<std::uint32_t>&);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct AddGroupGIDFile
		{
			static void thunk(RE::BSMultiStreamInstanceTriShape*, RE::BSStream*);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct DrawInstanceTriShape
		{
			static void thunk(RE::BSRenderPass* curPass, RE::BSMultiStreamInstanceTriShape* geometry);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct LoadGrassType
		{
			static RE::BSMultiStreamInstanceTriShape* thunk(RE::BGSGrassManager* grassManager, RE::GrassParam* a_param, uint32_t CellXDivided, uint32_t CellYDivided, uint64_t* typeKey, RE::BSFixedString* modelPath);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		static void Install(bool installGIDHooks)
		{
			auto& trampoline = SKSE::GetTrampoline();

			stl::write_vfunc<0x0, BSMultiStreamInstanceTriShape_dtor>(RE::VTABLE_BSMultiStreamInstanceTriShape[0]);
			stl::write_vfunc<0x34, BSMultiStreamInstanceTriShape_OnVisible>(RE::VTABLE_BSMultiStreamInstanceTriShape[0]);
			stl::write_vfunc<0x3A, DoneAddingInstances>(RE::VTABLE_BSMultiStreamInstanceTriShape[0]);

			stl::write_vfunc<0x6, BSGrassShader_SetupGeometry>(RE::VTABLE_BSGrassShader[0]);

		if (installGIDHooks) {
			stl::write_thunk_call<AddQueuedGroupGIDBuffer>(REL::RelocationID(15205, 15373).address() + Util::VersionedRelocation::Select(0x7FF, 0x756, 0x768));
			stl::write_thunk_call<AddGroupGIDBuffer>(REL::RelocationID(15205, 15373).address() + Util::VersionedRelocation::Select(0x806, 0x75D, 0x76F));
			stl::write_thunk_call<ReadGroupHeaderStreamTraits>(REL::RelocationID(74599, 76327).address() + Util::VersionedRelocation::Select(0x36, 0x36, 0x45));
			stl::write_thunk_call<ReadGroupHeaderStreamTraits>(REL::RelocationID(74596, 76324).address() + Util::VersionedRelocation::Select(0x2F, 0x33, 0x42));
			stl::write_thunk_call<ReadInstanceGroupStreamTraits>(REL::RelocationID(74607, 76339).address() + REL::Relocate(0xCF, 0xCF));
			stl::write_thunk_call<AddGroupQueuedGIDFile>(REL::RelocationID(15206, 15374).address() + REL::Relocate(0x394, 0x384));
			stl::write_thunk_call<AddGroupGIDFile>(REL::RelocationID(15206, 15374).address() + REL::Relocate(0x39B, 0x38B));
		}

			// Record each grass type's source .nif path alongside its shape.
			stl::write_thunk_call<LoadGrassType>(REL::RelocationID(15204, 15372).address() + Util::VersionedRelocation::Select(0x2F5, 0x2F5, 0x305));
			stl::write_thunk_call<LoadGrassType>(REL::RelocationID(15205, 15373).address() + Util::VersionedRelocation::Select(0x62B, 0x597, 0x590));
			stl::write_thunk_call<LoadGrassType>(REL::RelocationID(15206, 15374).address() + REL::Relocate(0x25C, 0x25C));

			std::uint8_t patch[] = { 0x4C, 0x89, 0xF2 };  // mov rdx, r14
			REL::safe_write(REL::RelocationID(100847, 107637).address() + REL::Relocate(0x660, 0x648), patch, sizeof(patch));
			stl::write_thunk_call<DrawInstanceTriShape>(REL::RelocationID(100847, 107637).address() + REL::Relocate(0x663, 0x64B));
			trampoline.write_branch<5>(REL::RelocationID(100847, 107637).address() + REL::Relocate(0x668, 0x650), REL::RelocationID(100847, 107637).address() + REL::Relocate(0x759, 0x73A));

		// Preserve Skyrim's dynamic fade upload. Optimized permutations do not
		// consume cb7/cb8, but fallback draws still require the original ABI.
			logger::info("[PIXL] Foliage Optimizer hooks installed");
		}
	};
};
