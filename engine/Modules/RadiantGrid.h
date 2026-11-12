#pragma once

#include "Buffer.h"
#include "OverlayFeature.h"

#include <atomic>
#include <mutex>
#include <shared_mutex>

#include <RE/S/ShadowSceneNode.h>

struct RadiantGrid : OverlayFeature
{
	static constexpr uint MAX_LIGHTS = 1024;

	struct ParticleLightConfig
	{
		bool cull = false;
	};

	struct ParticleLightConfigStore
	{
		ankerl::unordered_dense::map<std::string, ParticleLightConfig> configs;

		void Load();
	};

	struct ResolvedParticleLight
	{
		enum class Source : std::uint8_t
		{
			Particle,
			GlowMappedGeometry
		};

		RE::NiPoint3 position;
		RE::NiColorA color;
		float radius;
		float flickerStrength = 0.0f;
		float flickerPhase = 0.0f;
		Source source = Source::Particle;
	};

	struct PersistedParticleLight
	{
		ResolvedParticleLight light{};
		double lastSeenTime = 0.0;
	};

	struct VertexColorCacheEntry
	{
		bool valid = false;
		bool applyEffectMaterialTint = true;
		// Explicit fire/candle/torch/ember texture evidence can safely survive
		// modern effect replacers changing the vanilla node/alpha layout.
		bool practicalIncandescent = false;
		ParticleLightConfig config{};
		RE::NiColorA baseColor{ 1.0f, 1.0f, 1.0f, 1.0f };
		float radiusScale = 0.5f;
		float minimumRadius = 0.0f;
		float intensityScale = 1.0f;
		RE::NiColor incandescentTint{ 1.0f, 1.0f, 1.0f };
		float flickerStrength = 0.0f;
	};

	struct IncandescentGeometryCacheEntry
	{
		const RE::BSShaderMaterial* material = nullptr;
		VertexColorCacheEntry light{};
	};

	ParticleLightConfigStore particleLightConfigs;

	eastl::hash_map<RE::BSGeometry*, VertexColorCacheEntry> vertexColorCache;
	eastl::hash_map<RE::BSGeometry*, IncandescentGeometryCacheEntry> incandescentGeometryCache;
	eastl::vector<ResolvedParticleLight> queuedParticleLights;
	eastl::hash_map<RE::NiAVObject*, std::size_t> queuedParticleLightOwners;
	eastl::hash_map<RE::NiAVObject*, PersistedParticleLight> persistedParticleLights;
	double particleLightTime = 0.0;
	std::uintptr_t particleLightScene = 0;
	std::shared_mutex particleLightCacheMutex;
	std::mutex particleLightQueueMutex;
	std::mutex particleLightStateMutex;
	std::atomic<std::uint64_t> particleLightSceneGeneration{ 0 };
	std::uint32_t particleEmitterLightCount = 0;
	std::uint32_t glowMappedEmitterLightCount = 0;
	// Contiguous range occupied by particle/glow emitters in the uploaded light
	// buffer. HybridGI consumes this bounded range to seed persistent world
	// irradiance when transparent flames are absent from its opaque scene input.
	std::uint32_t particleLightBufferStart = 0;
	std::uint32_t particleLightBufferCount = 0;

	bool CheckParticleLights(RE::BSRenderPass* a_pass, uint32_t a_technique);

public:
	// True only when the 1.7.104 point-light helper callsite passed its byte
	// signature check and was replaced. The vfunc path uses this to avoid
	// running the strict-light collector twice, while retaining a fallback if
	// the executable's callsite differs.
	bool pointLightSetupCallsiteHooked = false;

	virtual inline std::string GetName() override { return "Radiant Grid"; }
	virtual std::string GetDisplayName() override { return T("feature.radiant_grid.name", "Radiant Grid"); }
	virtual inline std::string GetShortName() override { return "RadiantGrid"; }
	virtual inline std::string_view GetShaderDefineName() override { return "RADIANT_GRID"; }
	virtual std::string_view GetCategory() const override { return ModuleGroups::kLighting; }

	/** @brief Returns a localized description and list of key features for the UI summary panel. */
	virtual std::pair<std::string, std::vector<std::string>> GetModuleSummary() override
	{
		return { T("feature.radiant_grid.description", "Radiant Grid removes the vanilla game's 4-light limit, allowing unlimited dynamic lights in scenes.\nThis dramatically improves lighting quality and enables more realistic illumination scenarios."),
			{ T("feature.radiant_grid.key_feature_1", "Removes 4-light limit"),
				T("feature.radiant_grid.key_feature_2", "Unlimited dynamic lights"),
				T("feature.radiant_grid.key_feature_3", "Improved lighting quality"),
				T("feature.radiant_grid.key_feature_4", "Enhanced visual realism") } };
	};

	bool HasShaderDefine(RE::BSShader::Type) override { return true; };
	bool AffectsCachedShader(RE::BSShader::Type shaderType, std::uint32_t, CachedShaderStage stage) override
	{
		// Local-shadow sampling changed only the pixel paths that evaluate clustered
		// local lights. Preserve unrelated cache families when RadiantGrid's version
		// advances so the first launch recompiles only affected shader stages.
		return stage == CachedShaderStage::Pixel &&
			(shaderType == RE::BSShader::Type::Lighting ||
				shaderType == RE::BSShader::Type::Effect ||
				shaderType == RE::BSShader::Type::Particle ||
				shaderType == RE::BSShader::Type::Grass ||
				shaderType == RE::BSShader::Type::DistantTree ||
				shaderType == RE::BSShader::Type::Water);
	}

	/** @brief Flags describing light properties for clustered rendering. */
	enum class LightFlags : std::uint32_t
	{
		PortalStrict = (1 << 0),
		Shadow = (1 << 1),
		Simple = (1 << 2),
		ShadowCaster = (1 << 3),
		LocalShadow = (1 << 4),

		Initialised = (1 << 8),
		Disabled = (1 << 9),
		InverseSquare = (1 << 10),
		Linear = (1 << 11),
	};

	struct PositionOpt
	{
		float3 data;
		uint pad0;
	};

	struct alignas(16) LightData
	{
		float3 color;
		float fade = 1.0f;
		float radius;
		float invRadius;
		float fadeZone;
		float sizeBias;
		PositionOpt positionWS;
		uint128_t roomFlags = uint32_t(0);
		stl::enumeration<LightFlags> lightFlags;
		uint32_t shadowMaskIndex = 0;
		uint32_t localShadowIndex = 0;
		uint pad1;
	};
	STATIC_ASSERT_ALIGNAS_16(LightData);
	static_assert(sizeof(LightData) == 80);

	static constexpr uint32_t SHADOW_MASK_CHANNEL_COUNT = 4;
	static constexpr uint32_t NO_SHADOW_MASK_INDEX = 255;
	static constexpr uint32_t ENGINE_SHADOW_MAP_SLICES = 8;
	static constexpr uint32_t MIN_LOCAL_SHADOW_SLOTS = 4;
	static constexpr uint32_t MAX_LOCAL_SHADOW_SLOTS = 64;
	static constexpr uint32_t LOCAL_SHADOW_COPY_GROUP_SIZE = 8;
	static constexpr float LOCAL_SHADOW_DEPTH_BIAS = 0.00025f;
	static constexpr float LOCAL_SHADOW_DEFAULT_SPOT_FALLOFF = 2.0f;
	static constexpr uint32_t LOCAL_SHADOW_TYPE_SPOT = 0;
	static constexpr uint32_t LOCAL_SHADOW_TYPE_HEMISPHERE = 1;
	static constexpr uint32_t LOCAL_SHADOW_TYPE_OMNI = 2;
	static constexpr uint32_t LOCAL_SHADOW_MIN_RESOLUTION = 128;
	static constexpr uint64_t LOCAL_SHADOW_CACHE_MAX_BYTES = 512ull * 1024ull * 1024ull;
	static constexpr float LOCAL_SHADOW_FILTER_SCALE_MIN = 0.25f;
	static constexpr float LOCAL_SHADOW_FILTER_SCALE_MAX = 4.0f;
	static constexpr float LOCAL_SHADOW_MAX_POISSON_RADIUS = 16.0f;
	static constexpr float LOCAL_SHADOW_DEFAULT_POISSON_RADIUS = 4.0f;
	static constexpr uint32_t LOCAL_SHADOW_LOG_INTERVAL_FRAMES = 600;
	static constexpr uint32_t LOCAL_SHADOW_ALLOC_RETRY_FRAMES = 600;
	static constexpr uint32_t LOCAL_SHADOW_SWEEP_INTERVAL = 30;
	static constexpr uint32_t LOCAL_SHADOW_EVICT_AGE = 120;
	static constexpr uint32_t LOCAL_SHADOW_REJECT_MAX_FRAMES = 120;
	static constexpr uint32_t LOCAL_SHADOW_CAMERA_HOLD_FRAMES = 60;
	static constexpr uint32_t LOCAL_SHADOW_SCREEN_HOLD_FRAMES = 30;
	static constexpr uint32_t LOCAL_SHADOW_SELECTION_HOLD_FRAMES = 120;
	static constexpr uint32_t LOCAL_SHADOW_FIRST_MAP_RETRY_FRAMES = 30;
	static constexpr float LOCAL_SHADOW_VIEW_CULL_MARGIN = 24.0f;
	static constexpr float LOCAL_SHADOW_FADE_IN_FRAMES = 10.0f;
	static constexpr uint32_t LOCAL_SHADOW_STATIC_STARVE_FRAMES = 60;
	static constexpr float LOCAL_SHADOW_AGE_URGENCY = 64.0f;
	static constexpr float LOCAL_SHADOW_ACTOR_SCORE = 1000.0f;
	static constexpr float LOCAL_SHADOW_ACTOR_EXTENT = 96.0f;
	static constexpr float LOCAL_SHADOW_ACTOR_PROXIMITY_DISTANCE = 512.0f;
	static constexpr float LOCAL_SHADOW_ACTOR_EDGE_WEIGHT = 0.25f;
	static constexpr float LOCAL_SHADOW_ACTOR_STALENESS_WEIGHT = 0.15f;
	static constexpr float LOCAL_SHADOW_ACTOR_STICKY_BONUS = 0.5f;
	static constexpr float LOCAL_SHADOW_NEWCOMER_SCORE = 1000000.0f;
	static constexpr float LOCAL_SHADOW_NEWCOMER_WAIT_SCORE = 1000.0f;
	static constexpr float LOCAL_SHADOW_MOVED_SCORE = 100000.0f;
	static constexpr float LOCAL_SHADOW_MOVE_THRESHOLD = 12.0f;
	static constexpr float LOCAL_SHADOW_MOVE_RADIUS_FRACTION = 0.02f;
	static constexpr float LOCAL_SHADOW_STATIC_IMPORTANCE_BASE = 0.25f;
	static constexpr float LOCAL_SHADOW_TELEPORT_DISTANCE = 128.0f;
	static constexpr float LOCAL_SHADOW_TELEPORT_RADIUS_FRACTION = 0.25f;
	static constexpr float LOCAL_SHADOW_ACTOR_MAX_SPEED = 64.0f;
	static constexpr float LOCAL_SHADOW_ACTOR_REST_SPEED = 0.5f;
	static constexpr float LOCAL_SHADOW_ANIMATION_SPEED = 90.0f;
	static constexpr float LOCAL_SHADOW_MAX_SLACK = 24.0f;
	static constexpr float LOCAL_SHADOW_INTERVAL_EMA_WEIGHT = 0.3f;
	static constexpr float LOCAL_SHADOW_INTERVAL_EMA_MAX = 60.0f;
	static constexpr uint32_t LOCAL_SHADOW_REJECT_BASE_FRAMES = 15;
	static constexpr uint32_t LOCAL_SHADOW_REJECT_MAX_STREAK = 4;
	static constexpr float LOCAL_SHADOW_MAX_FRAME_TIME = 0.1f;
	static constexpr std::array<uint32_t, 3> LOCAL_SHADOW_RESOLUTION_OPTIONS = { 512, 1024, 2048 };
	static constexpr std::array<uint32_t, 3> LOCAL_SHADOW_SAMPLE_OPTIONS = { 1, 4, 8 };

	struct alignas(16) LocalShadowData
	{
		float4x4 ShadowProj;
		float4x4 ShadowProj2;
		float4 Params;
		float4 Params2;
		float4 Origin;
	};
	STATIC_ASSERT_ALIGNAS_16(LocalShadowData);
	static_assert(sizeof(LocalShadowData) == 176);
	static_assert(offsetof(LocalShadowData, ShadowProj2) == 64);
	static_assert(offsetof(LocalShadowData, Params) == 128);
	static_assert(offsetof(LocalShadowData, Params2) == 144);
	static_assert(offsetof(LocalShadowData, Origin) == 160);

	struct alignas(16) LocalShadowCopyCB
	{
		uint SourceSlice;
		uint TargetSlice;
		uint Scale;
		uint TargetSize;
	};
	STATIC_ASSERT_ALIGNAS_16(LocalShadowCopyCB);

	struct LocalShadowCaster
	{
		RE::BSShadowLight* light = nullptr;
		RE::NiLight* niLight = nullptr;
		int32_t slice = -1;
		int32_t secondarySlice = -1;
		uint32_t lastSeenFrame = 0;
		uint32_t lastEvaluatedFrame = 0;
		uint32_t lastEligibleFrame = 0;
		uint32_t lastSelectedFrame = 0;
		uint32_t selectionStartFrame = 0;
		uint32_t lastRenderedFrame = 0;
		uint32_t lastOnScreenFrame = 0;
		uint32_t shadowReadyFrame = 0;
		uint32_t slotDeniedFrame = 0;
		uint32_t rejectUntilFrame = 0;
		uint32_t rejectStreak = 0;
		RE::NiPoint3 position{};
		RE::NiPoint3 renderedPosition{};
		RE::NiMatrix3 rotation{};
		RE::NiMatrix3 renderedRotation{};
		float radius = 0.0f;
		float renderedRadius = 0.0f;
		float importance = 0.0f;
		float score = -1.0f;
		float actorImportance = 0.0f;
		float actorSpeed = 0.0f;
		float intervalEma = 1.0f;
		bool hidden = false;
		bool dynamic = false;
		float4x4 shadowProj{};
		float4x4 shadowProj2{};
		float4 shadowParams{};
		float4 shadowParams2{};
	};

	void AddParticleLightsToBuffer(eastl::vector<LightData>& a_lightsData,
		std::array<std::uint64_t, MAX_LIGHTS>& a_emitterIdentities,
		std::array<std::uint64_t, MAX_LIGHTS>& a_shadowRevisions);

	struct ClusterAABB
	{
		float4 minPoint;
		float4 maxPoint;
	};
	static_assert(sizeof(ClusterAABB) == 32, "RadiantGrid ClusterAABB must match its structured shader buffer stride.");

	struct alignas(16) LightGrid
	{
		uint offset;
		uint lightCount;
		uint contactLight0;
		uint contactLight1;
		uint contactLight2;
		uint contactLight3;
		// Independent bounded set for geometric SDF visibility. Unlike screen-space
		// contact rays, these may include lights that already have shadow maps.
		uint sdfLight0;
		uint sdfLight1;
		uint sdfLight2;
		uint sdfLight3;
		uint pad0[2];
	};
	STATIC_ASSERT_ALIGNAS_16(LightGrid);
	static_assert(sizeof(LightGrid) == 48, "RadiantGrid LightGrid must match its structured shader buffer stride.");
	static_assert(offsetof(LightGrid, sdfLight0) == 24 && offsetof(LightGrid, pad0) == 40,
		"RadiantGrid LightGrid shader fields must retain their structured-buffer offsets.");

	struct alignas(16) LightBuildingCB
	{
		float LightsNear;
		float LightsFar;
		uint pad0[2];
		uint ClusterSize[4];
	};
	STATIC_ASSERT_ALIGNAS_16(LightBuildingCB);

	struct alignas(16) LightCullingCB
	{
		uint LightCount;
		uint pad[3];
		uint ClusterSize[4];
	};
	STATIC_ASSERT_ALIGNAS_16(LightCullingCB);

	struct alignas(16) PerFrame
	{
		uint EnableLightsVisualisation;
		uint LightsVisualisationMode;
		uint EnableScreenSpaceLocalShadows;
		uint pad0;
		uint ClusterSize[4];
		uint EnableLocalShadows;
		uint LocalShadowSamples;
		float LocalShadowFilterRadius;
		float LocalShadowTexelSize;
	};
	STATIC_ASSERT_ALIGNAS_16(PerFrame);
	static_assert(sizeof(PerFrame) == 48);
	static_assert(offsetof(PerFrame, EnableScreenSpaceLocalShadows) == 8);
	static_assert(offsetof(PerFrame, EnableLocalShadows) == 32);

	/** @brief Populates and returns the per-frame constant buffer data for light visualization settings. */
	PerFrame GetCommonBufferData();

	struct alignas(16) StrictLightDataCB
	{
		uint NumStrictLights;
		int RoomIndex;
		uint ShadowBitMask;
		uint FirstPerson;
		float4 WorldEyePosition;
		LightData StrictLights[15];
	};
	STATIC_ASSERT_ALIGNAS_16(StrictLightDataCB);
	static_assert(offsetof(StrictLightDataCB, StrictLights) == 32);
	static_assert(sizeof(StrictLightDataCB) == 1232);

	StrictLightDataCB strictLightDataTemp;

	ConstantBuffer* strictLightDataCB = nullptr;

	bool previousEnableLightsVisualisation = settings.EnableLightsVisualisation;
	bool currentEnableLightsVisualisation = settings.EnableLightsVisualisation;

	ID3D11ComputeShader* clusterBuildingCS = nullptr;
	ID3D11ComputeShader* clusterCullingCS = nullptr;
	ID3D11ComputeShader* localShadowCopyCS = nullptr;
	bool localShadowCopyCSCompiled = false;

	ConstantBuffer* lightBuildingCB = nullptr;
	ConstantBuffer* lightCullingCB = nullptr;

	eastl::unique_ptr<Buffer> lights = nullptr;
	eastl::unique_ptr<Buffer> clusters = nullptr;
	eastl::unique_ptr<Buffer> lightIndexCounter = nullptr;
	eastl::unique_ptr<Buffer> lightIndexList = nullptr;
	eastl::unique_ptr<Buffer> lightGrid = nullptr;
	eastl::unique_ptr<Texture2D> localShadowCache = nullptr;
	eastl::unique_ptr<Buffer> localShadowBuffer = nullptr;
	ConstantBuffer* localShadowCopyCB = nullptr;
	uint32_t localShadowCacheSlots = 0;
	uint32_t localShadowCacheLightCapacity = 0;
	uint32_t localShadowRequestedSlots = 0;
	uint32_t localShadowRequestedResolution = 0;
	uint32_t localShadowCacheResolution = 0;
	uint32_t localShadowEngineResolution = 0;
	DXGI_FORMAT localShadowCacheFormat = DXGI_FORMAT_UNKNOWN;
	uint32_t localShadowEngineMipLevels = 1;
	uint32_t localShadowEngineSlices = 0;
	bool localShadowDirectCopy = false;
	bool localShadowAllocationFailed = false;
	uint32_t localShadowAllocFailedFrame = 0;
	eastl::vector<LocalShadowCaster> localShadowCasters;
	ankerl::unordered_dense::map<RE::BSShadowLight*, uint32_t> localShadowCasterLookup;
	eastl::vector<RE::BSShadowLight*> localShadowAllowed;
	eastl::vector<RE::BSShadowLight*> localShadowSliceOwner;
	struct LocalShadowActor
	{
		RE::NiPoint3 position{};
		float speed = 0.0f;
	};
	eastl::vector<LocalShadowActor> localShadowActors;
	ankerl::unordered_dense::map<RE::FormID, RE::NiPoint3> localShadowActorHistory;
	ankerl::unordered_dense::map<RE::FormID, RE::NiPoint3> localShadowActorHistoryNext;
	bool localShadowSelecting = false;
	bool localShadowHooksInstalled = false;
	bool localShadowFirstPersonPass = false;
	uint32_t localShadowFrame = 0;
	uint32_t localShadowLastSchedulerFrame = UINT32_MAX;
	uint32_t localShadowEngineBudget = SHADOW_MASK_CHANNEL_COUNT - 1;
	RE::NiPoint3 localShadowCameraPosition{};
	uint32_t localShadowStatTracked = 0;
	uint32_t localShadowStatOwnedLights = 0;
	uint32_t localShadowStatActiveShadowRefs = 0;
	uint32_t localShadowStatMissingNiLight = 0;
	uint32_t localShadowStatCached = 0;
	uint32_t localShadowStatRendered = 0;
	uint32_t localShadowLastCopyDiagnosticFrame = 0;
	uint32_t localShadowLastLightDiagnosticFrame = 0;
	uint32_t particleShadowMatchCount = 0;
	uint32_t glowShadowMatchCount = 0;
	bool localShadowEmitterMatchLogged = false;

	std::uint32_t lightCount = 0;
	float lightsNear = 1;
	float lightsFar = 16384;

	RE::NiPoint3 eyePositionCached{};
	bool wasEmpty = false;
	bool wasWorld = false;
	int previousRoomIndex = -1;
	uint previousShadowBitMask = 0;
	uint previousFirstPerson = 0;
	uint previousWorldFallbackLightCount = 0;
	uint64_t previousWorldFallbackLightSignature = 0;
	uint64_t worldFallbackLightSignature = 0;
	bool worldFallbackLightDiagnosticLogged = false;
	RE::NiPoint3 previousWorldEyePosition{};

	Util::FrameChecker frameChecker;

	/** @brief Creates GPU buffers, compute shaders, and constant buffers for clustered lighting. */
	virtual void SetupResources() override;
	virtual void Reset() override;

	virtual void SaveSettings(json& o_json) override;
	virtual void LoadSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;

	/** @brief Draws the ImGui settings UI for light limit fix configuration and debug visualization. */
	virtual void DrawSettings() override;
	/** @brief Draws the debug overlay warning when light visualization is enabled. */
	virtual void DrawOverlay() override;
	/** @brief Returns whether the debug overlay should be displayed. */
	virtual bool IsOverlayVisible() const override { return settings.EnableLightsVisualisation; }

	/** @brief Installs shader setup geometry hooks for lighting, effect, and water shaders. */
	virtual void PostPostLoad() override;
	/** @brief Unlocks the vanilla magic light limit on data load. */
	virtual void DataLoaded() override;
	/** @brief Recompiles the cluster building and culling compute shaders. */
	virtual void ClearShaderCache() override;
	virtual void EarlyPrepass() override;
	void CopyLocalShadowMaps();
	void BindLocalShadowResources();
	void EnsureLocalShadowResources(REX::W32::ID3D11Texture2D* a_engineShadowMaps);
	void ReleaseLocalShadowResources();
	bool EnsureLocalShadowCopyShader();
	void ScheduleLocalShadowCasters();
	bool FilterLocalShadowCaster(RE::BSShadowLight* a_light, const RE::NiCamera* a_camera, bool a_result);
	LocalShadowCaster* FindLocalShadowCaster(RE::BSShadowLight* a_light);
	static uint32_t GetEngineLocalShadowBudget();
	static bool IsLocalShadowCasterInView(const LocalShadowCaster& a_caster, uint32_t a_frame);
	static bool IsLocalShadowCasterOnScreen(const LocalShadowCaster& a_caster, uint32_t a_frame);
	static float GetLocalShadowFadeIn(const LocalShadowCaster& a_caster, uint32_t a_frame);
	static bool IsLocalShadowSliceReclaimable(const LocalShadowCaster* a_owner, uint32_t a_frame);
	int32_t AcquireLocalShadowSlice(RE::BSShadowLight* a_light, uint32_t a_frame, bool a_dualMap);
	bool IsLocalShadowsEnabled() const { return settings.EnableLocalShadows && localShadowHooksInstalled; }
	bool IsLocalShadowCacheActive() const { return IsLocalShadowsEnabled() && localShadowCache; }

	/**
	 * @brief Calculates the distance from the camera to a light for culling purposes.
	 * @param a_lightPosition World-space position of the light.
	 * @param a_radius The light's effective radius.
	 * @return The effective distance for sorting/culling.
	 */
	float CalculateLightDistance(float3 a_lightPosition, float a_radius);
	/**
	 * @brief Sets the world-space position of a light relative to the cached eye position.
	 * @param a_light The light data struct to update.
	 * @param a_initialPosition The light's world-space position.
	 * @param a_cached Whether to use the cached eye position or recompute it.
	 */
	void SetLightPosition(RadiantGrid::LightData& a_light, RE::NiPoint3 a_initialPosition, bool a_cached = true);
	/** @brief Gathers all active scene lights and uploads them to the GPU light buffer. */
	void UpdateLights();
	/** @brief Clears per-draw light data after rejecting a malformed engine pass. */
	void ResetGeometryLightData();
	/** @brief Rebuilds the light cluster structure and performs GPU light culling. */
	void UpdateStructure();
	/** @brief Runs the light update and binds clustered light SRVs for the frame. */
	virtual void Prepass() override;

	/** @brief Adjusts the saturation of an RGB color value. */
	static inline float3 Saturation(float3 color, float saturation);
	/**
	 * @brief Checks whether a BSLight is valid (non-null and not hidden).
	 * @param a_light The light to validate.
	 * @return True if the light is valid for processing.
	 */
	static inline bool IsValidLight(RE::BSLight* a_light);
	/**
	 * @brief Checks whether a BSLight is a global (non-portal-strict) light.
	 * @param a_light The light to check.
	 * @return True if the light is global and not restricted to a portal.
	 */
	static inline bool IsGlobalLight(RE::BSLight* a_light);

	struct Settings
	{
		bool EnableParticleLights = true;
		bool EnableParticleLightsCulling = true;
		bool EnableLightsVisualisation = false;
		uint LightsVisualisationMode = 0;
		bool EnableScreenSpaceLocalShadows = true;
		// The clustered light-limit path uses Skyrim's native shadow maps. PIXL's
		// experimental local-shadow cache is disabled pending a stable redesign.
		bool EnableLocalShadows = false;
		bool PromoteLocalLightsToShadowCasters = false;
		uint LocalShadowSlots = 16;
		uint LocalShadowResolution = 0;
		uint LocalShadowSamples = 8;
		float LocalShadowFilterScale = 2.0f;
	};

	uint clusterSize[3] = { 16 };

	Settings settings;

	/** @brief Pre-geometry setup: initializes strict light data and determines the room index for the pass. */
	void BSLightingShader_SetupGeometry_Before(RE::BSRenderPass* a_pass);

	/** @brief Collects portal-strict point lights from the render pass into the strict light constant buffer. */
	void BSLightingShader_SetupGeometry_GeometrySetupConstantPointLights(RE::BSRenderPass* a_pass);

	/** @brief Post-geometry setup: uploads the strict light constant buffer to the GPU if changed. */
	void BSLightingShader_SetupGeometry_After(RE::BSRenderPass* a_pass);

	eastl::hash_map<RE::NiNode*, uint8_t> roomNodes;

	/** @brief Contains vtable hooks for BSLightingShader, BSEffectShader, and BSWaterShader geometry setup. */
	struct Hooks
	{
		struct BSLightingShader_SetupGeometry
		{
			static void thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSEffectShader_SetupGeometry
		{
			static void thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSWaterShader_SetupGeometry
		{
			static void thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		template <int N>
		struct ValidLight
		{
			static bool thunk(RE::BSShaderProperty* a_property, RE::BSLight* a_light)
			{
				return func(a_property, a_light) && (a_light->portalStrict || !a_light->portalGraph || a_light->IsShadowLight());
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		using ValidLight1 = ValidLight<1>;
		using ValidLight2 = ValidLight<2>;
		using ValidLight3 = ValidLight<3>;

		template <int N>
		struct BSBatchRenderer_RenderPassImmediately
		{
			static void thunk(RE::BSRenderPass* a_pass, uint32_t a_technique, bool a_alphaTest, uint32_t a_renderFlags);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		using RenderPass1 = BSBatchRenderer_RenderPassImmediately<1>;
		using RenderPass2 = BSBatchRenderer_RenderPassImmediately<2>;
		using RenderPass3 = BSBatchRenderer_RenderPassImmediately<3>;

		struct BSGeometry_Destroy
		{
			static void thunk(RE::BSGeometry* This);
			static inline REL::Relocation<decltype(thunk)> func;
		};
	struct CalculateActiveShadowCasterLights
	{
		static void thunk();
		static inline REL::Relocation<decltype(thunk)> func;
	};
	struct ShadowSceneNode_AddLight
	{
		static RE::BSLight* thunk(RE::ShadowSceneNode* This, RE::NiLight* a_light,
			const RE::ShadowSceneNode::LIGHT_CREATE_PARAMS& a_params);
		static inline REL::Relocation<decltype(thunk)> func;
	};
	struct ShadowSceneNode_AddLightConvenience
	{
		static void thunk(RE::ShadowSceneNode* This, RE::NiLight* a_light);
		static inline REL::Relocation<decltype(thunk)> func;
	};
	struct Main_RenderFirstPersonView
	{
		static void thunk(bool a1, bool a2);
		static inline REL::Relocation<decltype(thunk)> func;
	};
	struct BSShadowParabolicLight_UpdateCamera
	{
		static bool thunk(RE::BSShadowLight* This, const RE::NiCamera* a_camera);
		static inline REL::Relocation<decltype(thunk)> func;
	};
	struct BSShadowFrustumLight_UpdateCamera
	{
		static bool thunk(RE::BSShadowLight* This, const RE::NiCamera* a_camera);
		static inline REL::Relocation<decltype(thunk)> func;
	};

		static void Install();
	static void InstallLocalShadows();
	};

	virtual bool IsCore() const override { return true; }

private:
	VertexColorCacheEntry GetParticleLightConfig(RE::BSRenderPass* a_pass);
	VertexColorCacheEntry GetIncandescentGeometryLightConfig(RE::BSRenderPass* a_pass);
	bool QueueIncandescentGeometryLight(RE::BSRenderPass* a_pass);
	bool QueueResolvedEmitterLight(RE::NiAVObject* a_owner, const ResolvedParticleLight& a_resolved);
	bool QueueParticleLight(RE::BSRenderPass* a_pass, VertexColorCacheEntry& a_reference);
};

template <>
struct fmt::formatter<RadiantGrid::LightData>
{
	// Presentation format: 'f' - fixed.
	char presentation = 'f';

	// Parses format specifications of the form ['f'].
	constexpr auto parse(format_parse_context& ctx) -> format_parse_context::iterator
	{
		auto it = ctx.begin(), end = ctx.end();
		if (it != end && (*it == 'f'))
			presentation = *it++;

		// Check if reached the end of the range:
		if (it != end && *it != '}')
			throw format_error("invalid format");

		// Return an iterator past the end of the parsed range:
		return it;
	}

	// Formats the point p using the parsed format specification (presentation)
	// stored in this formatter.
	auto format(const RadiantGrid::LightData& l, format_context& ctx) const -> format_context::iterator
	{
		// ctx.out() is an output iterator to write to.
		return fmt::format_to(ctx.out(), "{{address {:x} color {} radius {} posWS {}}}",
			reinterpret_cast<uintptr_t>(&l),
			(Vector3)l.color,
			l.radius,
			(Vector3)l.positionWS.data);
	}
};
