#pragma once

#include "Buffer.h"
#include "Renderer/RendererMetadata.h"

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

struct HybridGI : RenderModule
{
private:
	static constexpr std::string_view MOD_ID = "130375";

public:
	virtual inline std::string GetName() override { return "Hybrid GI"; }
	virtual std::string GetDisplayName() override { return "PIXL Radiance Weave"; }
	virtual inline std::string GetShortName() override { return "HybridGI"; }
	virtual inline std::string GetModuleSupportLink() override { return MakeNexusModURL(MOD_ID); }
	virtual std::string_view GetCategory() const override { return ModuleGroups::kLighting; }

	/** @brief Returns a localized description and list of key features for the UI summary panel. */
	virtual std::pair<std::string, std::vector<std::string>> GetModuleSummary() override
	{
		std::string desc = T("feature.hybrid_gi.pixl_description",
			"PIXL Radiance Weave combines high-detail screen-space indirect lighting with a persistent world-space irradiance cache for more stable lighting while the camera moves.");
		return { desc,
			{ T("feature.hybrid_gi.pixl_key_feature_1", "Screen-space diffuse and specular indirect lighting"),
				T("feature.hybrid_gi.pixl_key_feature_2", "Persistent two-cascade world irradiance cache"),
				T("feature.hybrid_gi.pixl_key_feature_3", "Secondary bounce and directional occlusion"),
				T("feature.hybrid_gi.pixl_key_feature_4", "Temporal accumulation with adaptive edge-aware denoising"),
				T("feature.hybrid_gi.pixl_key_feature_5", "Quality presets, advanced tuning and diagnostic views") } };
	}

	/** @brief Resets all settings to their default values and flags shaders for recompilation. */
	virtual void RestoreDefaultSettings() override;
	/** @brief Draws the user-facing PIXL Hybrid GI panel with presets, lighting, world cache, reflections, stability, advanced tuning and diagnostics. */
	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	/** @brief Registers the loading-screen listener that resets temporal history. */
	virtual void PostPostLoad() override;
	/** @brief Creates GPU textures, samplers, constant buffers, and compiles compute shaders. */
	virtual void SetupResources() override;
	/** @brief Releases and recompiles all HybridGI compute shaders. */
	virtual void ClearShaderCache() override;
	/** @brief Compiles all HybridGI compute shaders with current resolution and feature defines. */
	void CompileComputeShaders(std::uint32_t a_shaderMask = 0xffffffffu);
	/** @brief Checks whether all required compute shaders and the noise texture loaded successfully. */
	bool ShadersOK() const;

	/** @brief Executes the full HybridGI pipeline: depth prefilter, radiance fetch, GI, blur, and upsample. */
	void DrawHybridGI();
	/** @brief Updates the HybridGI constant buffer with current camera, resolution, and settings data. */
	void UpdateSB();
	/** @brief Starts an asynchronous fixed-camera capture of every PIXL GI and PBR diagnostic view. */
	void RequestDiagnosticCapture();
	/** @brief Advances the diagnostic screenshot state machine without blocking the render thread. */
	void UpdateDiagnosticCapture();
	/** @brief Returns the full-resolution radiance input used by the screen-space and voxel GI paths. */
	ID3D11ShaderResourceView* GetInputRadianceSRV() const { return texRadiance ? texRadiance->srv.get() : nullptr; }
	[[nodiscard]] bool IsDiagnosticCaptureActive() const { return diagnosticCaptureActive; }

	//////////////////////////////////////////////////////////////////////////////////

	bool recompileFlag = false;
	uint outputAoIdx = 0;
	uint outputIlIdx = 0;
	std::atomic_bool queuedResetHistory{ true };
	std::atomic_bool queuedResetTemporalHistory{ false };
	std::atomic_bool queuedResetWorldHistory{ false };

	class MenuOpenCloseEventHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		virtual RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event,
			RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override;
		static bool Register();
	};

	struct Settings
	{
		bool Enabled = true;
		bool EnableGI = true;
		bool EnableExperimentalSpecularGI = false;
		bool EnableVanillaSSAO = false;
		// performance/quality
		uint NumSlices = 4u;
		uint NumSteps = 8u;
		int ResolutionMode = 0;  // PIXL production invariant: full game-render resolution
		// Experimental Task 09 foundation. Disabled by default so the known-good
		// fixed-work GI permutation remains the shipped behaviour until live-tested.
		bool EnableAdaptiveRayAllocation = false;
		float AdaptiveRayMinimum = 0.50f;
		// visual
		float MinScreenRadius = 0.01f;
		float AORadius = 256.f;
		float GIRadius = 256.f;
		float Thickness = 32.f;
		float2 DepthFadeRange = { 4e4, 5e4 };
		// gi
		float GISaturation = 0.8f;
		float GIDistanceCompensation = 0.f;
		// mix
		float AOPower = 1.0f;
		float GIStrength = 1.0f;
		// PIXL Contact Depth: a restrained, near-field HBAO-style term reused
		// from the GI horizon samples. It adds no additional depth fetch pass.
		bool EnableContactDepth = true;
		float ContactDepthStrength = 0.35f;
		float ContactDepthRadius = 96.0f;
		float ContactDepthBias = 0.08f;
		// denoise
		bool EnableTemporalDenoiser = true;
		bool EnableBlur = true;
		bool EnableAdaptiveDenoiser = true;
		float DepthDisocclusion = .1f;
		float NormalDisocclusion = .1f;
		uint MaxAccumFrames = 24;
		float BlurRadius = 2.f;
		float DistanceNormalisation = 2.f;
		// PIXL hybrid world-radiance cache. This augments, rather than replaces,
		// the horizon-bitmask screen-space result.
		bool EnableWorldCache = true;
		uint WorldCacheMaxAge = 72;
		uint WorldCacheSampleCount = 8;
		float WorldCacheStrength = static_cast<float>(PIXL::Metadata::Settings::HybridWorldCacheStrength.defaultValue);
		float WorldCacheCellSizeNear = 96.f;
		float WorldCacheCellSizeFar = 384.f;
		float WorldCacheRadius = 1536.f;
		float WorldCacheLeakReduction = 0.82f;
		float WorldCacheTemporalResponse = 0.10f;
		bool EnableEmitterInjection = true;
		float EmitterInjectionStrength = 0.65f;
		bool EnableWorldCacheSecondBounce = true;
		float WorldCacheSecondBounceStrength = 0.20f;
		uint WorldCacheInjectionStride = 2;
		uint WorldCacheTraceSteps = 4;
		bool EnableDirectionalOcclusion = true;
		float DirectionalOcclusionStrength = 0.30f;
		bool EnableVoxelReflections = false;
		float VoxelReflectionStrength = 0.30f;
		float VoxelReflectionRoughnessCutoff = 0.80f;

		// PIXL Rendering vNext visibility/reflection controls. The legacy
		// EnableExperimentalSpecularGI field remains the persisted master switch
		// for backwards compatibility and is presented as Hybrid Reflections.
		bool EnableBentNormalLighting = true;
		float BentNormalStrength = 0.85f;
		bool EnableSpecularOcclusion = true;
		float SpecularOcclusionStrength = 0.90f;
		float RadianceFireflyClamp = 8.0f;
		float UpsampleEdgeThreshold = 0.10f;
		float ReflectionIntensity = 1.0f;
		float ReflectionMaxRoughness = 0.82f;
		float ReflectionMaxDistance = 4096.0f;
		float ReflectionThickness = 28.0f;
		uint ReflectionSteps = 32u;
		float ReflectionTemporalResponse = 0.14f;
		float ReflectionFireflyClamp = 10.0f;
		float ReflectionWorldFallbackStrength = 0.75f;
		float ReflectionRayBias = 6.0f;
		float ReflectionRoughnessJitter = 1.0f;
		// Full-screen diagnostic view. Zero is the normal composite.
		uint DebugView = 0;
		float DebugGain = 1.0f;
	} settings;

	struct alignas(16) HybridGICB
	{
		float4x4 PrevInvViewMat;
		float4 NDCToViewMul;
		float4 NDCToViewAdd;

		float2 TexDim;
		float2 RcpTexDim;  //
		float2 FrameDim;
		float2 RcpFrameDim;  //
		uint FrameIndex;

		uint NumSlices;
		uint NumSteps;

		float MinScreenRadius;  //
		float AORadius;
		float GIRadius;
		float EffectRadius;
		float Thickness;  //
		float2 DepthFadeRange;
		float DepthFadeScaleConst;

		float GISaturation;  //
		float GIDistanceCompensation;
		float GICompensationMaxDist;
		float pad1;

		float AOPower;  //
		float GIStrength;

		float DepthDisocclusion;
		float NormalDisocclusion;
		uint MaxAccumFrames;  //

		float BlurRadius;
		float DistanceNormalisation;

		uint WorldCacheEnabled;
		uint WorldCacheMaxAge;
		uint WorldCacheSampleCount;
		uint WorldCacheTraceSteps;

		float WorldCacheStrength;
		float WorldCacheCellSizeNear;
		float WorldCacheCellSizeFar;
		float WorldCacheRadius;

		float WorldCacheLeakReduction;
		uint WorldCacheInjectionStride;
		uint WorldCacheDirectionalOcclusionEnabled;
		float WorldCacheDirectionalOcclusionStrength;

		uint DebugView;
		float DebugGain;
		float WorldCacheTemporalResponse;
		uint WorldCacheReflectionEnabled;
		float WorldCacheReflectionStrength;
		float WorldCacheReflectionRoughnessCutoff;
		uint WorldCacheSecondBounceEnabled;
		float WorldCacheSecondBounceStrength;
		float2 pad2;

		// vNext fields are append-only. Original fields keep their historical offsets.
		uint BentNormalEnabled;
		float BentNormalStrength;
		uint SpecularOcclusionEnabled;
		float SpecularOcclusionStrength;

		float RadianceFireflyClamp;
		float UpsampleEdgeThreshold;
		float ReflectionIntensity;
		float ReflectionMaxRoughness;

		float ReflectionMaxDistance;
		float ReflectionThickness;
		uint ReflectionSteps;
		float ReflectionTemporalResponse;

		float ReflectionFireflyClamp;
		float ReflectionWorldFallbackStrength;
		float ReflectionRayBias;
		float ReflectionRoughnessJitter;

		// Append-only PIXL Contact Depth controls. Earlier offsets remain ABI-stable.
		uint ContactDepthEnabled;
		float ContactDepthStrength;
		float ContactDepthRadius;
		float ContactDepthBias;

		// Append-only 8x8 adaptive ray-classification controls. Enablement is a
		// shader permutation; the CB only carries the retained-work floor.
		float AdaptiveRayMinimum;
		// Fixed-rate cache clock. This occupies former padding, preserving the
		// complete constant-buffer ABI while decoupling cache lifetime from FPS.
		uint WorldCacheClock;
		float WorldCacheDeltaTime;
		float pad3;

		// Authoritative RadiantGrid emitter range used to fill transparent
		// torch/fire energy missing from the opaque screen-radiance source.
		uint WorldCacheEmitterInjectionEnabled;
		uint RadiantParticleLightStart;
		uint RadiantParticleLightCount;
		float WorldCacheEmitterInjectionStrength;
	};
	STATIC_ASSERT_ALIGNAS_16(HybridGICB);
	static_assert(sizeof(HybridGICB) == PIXL::Metadata::ABI::HybridGI.sizeBytes, "HybridGICB must match the PIXL Rendering vNext Shader Model 5 layout.");
	static_assert(offsetof(HybridGICB, WorldCacheEnabled) == 216);
	static_assert(offsetof(HybridGICB, WorldCacheTraceSteps) == 228);
	static_assert(offsetof(HybridGICB, WorldCacheDirectionalOcclusionEnabled) == 256);
	static_assert(offsetof(HybridGICB, DebugView) == 264);
	static_assert(offsetof(HybridGICB, WorldCacheTemporalResponse) == 272);
	static_assert(offsetof(HybridGICB, WorldCacheSecondBounceEnabled) == 288);
	static_assert(offsetof(HybridGICB, BentNormalEnabled) == 304);
	static_assert(offsetof(HybridGICB, ReflectionIntensity) == 328);
	static_assert(offsetof(HybridGICB, ReflectionFireflyClamp) == 352);
	static_assert(offsetof(HybridGICB, ContactDepthEnabled) == 368);
	static_assert(offsetof(HybridGICB, AdaptiveRayMinimum) == 384);
	static_assert(offsetof(HybridGICB, WorldCacheClock) == 388);
	static_assert(offsetof(HybridGICB, WorldCacheDeltaTime) == 392);
	static_assert(offsetof(HybridGICB, WorldCacheEmitterInjectionEnabled) == 400);
	eastl::unique_ptr<ConstantBuffer> ssgiCB;

	eastl::unique_ptr<Texture2D> texNoise = nullptr;
	eastl::unique_ptr<Texture2D> texWorkingDepth = nullptr;
	winrt::com_ptr<ID3D11UnorderedAccessView> uavWorkingDepth[5] = { nullptr };
	eastl::unique_ptr<Texture2D> texPrevGeo = nullptr;
	eastl::unique_ptr<Texture2D> texRadiance = nullptr;
	eastl::unique_ptr<Texture2D> texRadianceTemp = nullptr;
	winrt::com_ptr<ID3D11UnorderedAccessView> uavRadiance[5] = { nullptr };
	eastl::unique_ptr<Texture2D> texNormal = nullptr;
	winrt::com_ptr<ID3D11UnorderedAccessView> uavNormal[5] = { nullptr };
	eastl::unique_ptr<Texture2D> texAccumFrames[2] = { nullptr };
	eastl::unique_ptr<Texture2D> texAo[2] = { nullptr };
	eastl::unique_ptr<Texture2D> texIlY[2] = { nullptr };
	eastl::unique_ptr<Texture2D> texIlCoCg[2] = { nullptr };
	eastl::unique_ptr<Texture2D> texGiSpecular[2] = { nullptr };
	eastl::unique_ptr<Texture2D> texBentVisibility[2] = { nullptr };
	uint outputSpecIdx = 0;
	uint outputBentIdx = 0;
	eastl::unique_ptr<Texture2D> texWorldCacheMetadata = nullptr;
	eastl::unique_ptr<Texture2D> texWorldCacheWinners = nullptr;
	eastl::unique_ptr<Texture2D> texWorldCacheSH0 = nullptr;
	eastl::unique_ptr<Texture2D> texWorldCacheSH1 = nullptr;
	eastl::unique_ptr<Texture2D> texWorldCacheSH2 = nullptr;
	eastl::unique_ptr<Texture2D> texWorldCacheNormal = nullptr;
	// Immutable previous-frame snapshot used by secondary-bounce injection.
	// Separating reads from current UAV writes prevents order-dependent cache flicker.
	eastl::unique_ptr<Texture2D> texWorldCachePreviousMetadata = nullptr;
	eastl::unique_ptr<Texture2D> texWorldCachePreviousSH0 = nullptr;
	eastl::unique_ptr<Texture2D> texWorldCachePreviousSH1 = nullptr;
	eastl::unique_ptr<Texture2D> texWorldCachePreviousSH2 = nullptr;
	eastl::unique_ptr<Texture2D> texWorldCachePreviousNormal = nullptr;

	/** @brief Returns AO, diffuse SH Y/CoCg, hybrid reflection and bent-normal visibility SRVs. */
	inline auto GetOutputTextures()
	{
		return (loaded && settings.Enabled && ShadersOK()) ?
		           std::make_tuple(
					   texAo[outputAoIdx]->srv.get(),
					   texIlY[outputIlIdx]->srv.get(),
					   texIlCoCg[outputIlIdx]->srv.get(),
					   texGiSpecular[outputSpecIdx]->srv.get(),
					   texBentVisibility[outputBentIdx]->srv.get()) :
		           std::make_tuple(nullptr, nullptr, nullptr, nullptr, nullptr);
	}

	winrt::com_ptr<ID3D11SamplerState> linearClampSampler = nullptr;
	winrt::com_ptr<ID3D11SamplerState> pointClampSampler = nullptr;

	winrt::com_ptr<ID3D11ComputeShader> prefilterDepthsCompute = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> prefilterRadianceCompute = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> prefilterNormalCompute = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> radianceDisoccCompute = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> giCompute = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> blurCompute = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> blurAtrousCompute = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> upsampleCompute = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> worldCacheInjectCompute = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> worldCacheSelectCompute = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> worldCacheDecayCompute = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> hybridReflectionCompute = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> hybridReflectionDenoiseCompute = nullptr;

private:
	void RecompileChangedShaders();
	[[nodiscard]] bool CoreShadersOK() const;
	[[nodiscard]] bool WorldCacheShadersOK() const;
	[[nodiscard]] bool BlurShadersOK() const;
	[[nodiscard]] bool ReflectionShadersOK() const;
	[[nodiscard]] bool UpsampleShaderOK() const;

	std::uint64_t temporalHistoryId = 0;
	std::uint64_t worldHistoryId = 0;
	float worldCacheClockAccumulator = 0.0f;
	uint worldCacheClock = 0u;
	// Decay is defined in fixed-rate world-cache ticks, not render frames. Keep
	// the last swept tick so high-FPS sessions do not repeatedly dispatch the
	// same 65,536-entry atlas sweep between clock advances.
	uint lastWorldCacheDecayClock = 0xffffffffu;
	// Runtime semantic/permutation snapshot. These settings change the meaning
	// of temporal/cache history and therefore require deterministic invalidation.
	bool runtimeConfigInitialized = false;
	bool lastEnableGI = true;
	bool lastEnableTemporalDenoiser = true;
	bool lastEnableExperimentalSpecularGI = false;
	bool lastEnableAdaptiveRayAllocation = false;
	bool lastEnableWorldCache = true;
	int lastResolutionMode = 0;
	float lastWorldCacheCellSizeNear = 0.0f;
	float lastWorldCacheCellSizeFar = 0.0f;
	bool compiledShaderConfigInitialized = false;
	int compiledResolutionMode = 0;
	bool compiledTemporalDenoiser = true;
	bool compiledGI = true;
	bool compiledHybridReflections = false;
	bool compiledAdaptiveRayAllocation = false;

	struct DiagnosticRecord
	{
		std::string name;
		std::filesystem::path file;
		uint debugView = 0;
		uint materialDebugMode = 0;
		uint frameIndex = 0;
	};

	std::atomic_bool diagnosticCaptureRequested{ false };
	bool diagnosticCaptureActive = false;
	bool diagnosticWaitingForScreenshot = false;
	bool diagnosticSavedMenuEnabled = false;
	uint diagnosticSavedDebugView = 0;
	uint diagnosticSavedMaterialDebugMode = 0;
	uint diagnosticSettleFrames = 0;
	uint diagnosticWaitFrames = 0;
	// Diagnostics deliberately freeze the persistent-cache clock. A complete
	// capture spans more than the cache's 8-bit timestamp period; allowing the
	// normal frame counter to advance while injection is isolated made old cells
	// fade, wrap and reappear part-way through one diagnostic set.
	uint diagnosticFrozenFrameIndex = 0;
	size_t diagnosticCaptureIndex = 0;
	std::filesystem::path diagnosticCaptureDirectory;
	std::filesystem::path diagnosticCurrentScreenshot;
	std::vector<DiagnosticRecord> diagnosticRecords;
	uint lastRuntimeDebugView = 0;
	uint lastRuntimeMaterialDebugMode = 0;

	void ApplyDiagnosticView(size_t index);
	void FinishDiagnosticCapture(bool complete);
	void WriteDiagnosticManifest(bool complete) const;
	static void NotifyDiagnostic(std::string message);

};
