#pragma once

#include "Buffer.h"

#include <string>
#include <vector>

struct GroundResponse : RenderModule
{
public:
	virtual inline std::string GetName() override { return "Ground Response"; }
	virtual std::string GetDisplayName() override { return T("feature.ground_response.name", "Grass & Ground Interaction"); }
	virtual inline std::string GetShortName() override { return "GroundResponse"; }
	virtual inline std::string_view GetShaderDefineName() override { return "GROUND_RESPONSE"; }
	virtual std::string_view GetCategory() const override { return ModuleGroups::kGrass; }

	virtual std::pair<std::string, std::vector<std::string>> GetModuleSummary() override
	{
		return { T("feature.ground_response.description", "Drives one persistent actor interaction field for grass bending plus one shared raised/compressible landscape surface for exact layer-classified snow and wet mud."),
			{ T("feature.ground_response.key_feature_1", "Real-time grass deformation from actor movement"),
				T("feature.ground_response.key_feature_2", "Skyrim-authored six-layer snow classification, including Material Forge terrain"),
				T("feature.ground_response.key_feature_3", "Adaptive geometric snow and mud surface integrated into the real deferred terrain Lighting pass"),
				T("feature.ground_response.key_feature_4", "Persistent snow tracks and thin raised mud that compresses back toward the original terrain"),
				T("feature.ground_response.key_feature_5", "Depth/G-buffer/motion-vector coherent terrain replay plus fail-closed runtime diagnostics") } };
	};

	/** @brief Enables Ground Response code for Grass and Lighting (landscape) shaders. */
	bool HasShaderDefine(RE::BSShader::Type shaderType) override;

	/** @brief Dispatches the collision update compute shader to write actor collision data into the collision texture. */
	void UpdateCollisionTexture();

	/** @brief Dispatches the persistent absolute-world XY snow/mud surface deformation update. */
	void UpdateSurfaceDeformationTexture();

	struct Settings
	{
		bool EnableGroundResponse = 1;
		bool TrackRagdolls = 1;
		bool EnableBlur = 1;
		bool EnableDeformableGround = true;
		bool EnableSnowDeformation = true;
		bool EnableGeometricSnow = true;
		bool EnableMudDeformation = true;
		bool MudRequiresWetness = true;
		float SnowMaximumDepth = 18.0f;
		float SnowSurfaceThickness = 10.0f;
		bool EnableWeatherSnowAccumulation = true;
		float WeatherSnowMaximumRaise = 14.0f;
		float WeatherSnowAccumulationRate = 0.060f;
		float WeatherSnowMeltRate = 0.035f;
		float GeometryRenderDistance = 4000.0f; // PIXL_GR_13Y_DEFAULTS_V1
		float GeometryFadeStart = 3400.0f;
		float GeometryMinimumSlopeZ = 0.42f;
		float GeometryTessellationNear = 12.0f;
		float GeometryTessellationFar = 6.0f;
		float GeometryTessellationNearDistance = 768.0f;
		float GeometryTessellationFarDistance = 2048.0f;
		float SnowCoverageThreshold = 0.06f;
		float SnowCoverageFeather = 0.20f;
		float MudMaximumDepth = 10.0f;
		float GroundNormalStrength = 1.0f;
		float SnowCompactionDarkening = 0.28f;
		float MudDarkening = 0.48f;
		float MudRoughness = 0.24f;
		float MudWetnessThreshold = 0.15f;
		float GroundResponseStrength = 1.0f;
		bool DebugInteractionField = false;
		bool GeometrySelfTest = false;
		// Persistent field recovery in world units per second. New tracks are held
		// unchanged for TrackHoldSeconds before this recovery begins.
		float TrackRecoveryRate = 0.25f;
		float TrackHoldSeconds = 10.0f;

		// GroundResponse 3.1 interaction generalisation. These affect only the
		// CPU stamp/event producer and the dedicated t101/t102/t103 fields; the
		// established terrain b13/deferred Lighting contract remains unchanged.
		bool EnableAnimatedBodyContacts = true;
		bool EnableObjectInteraction = true;
		bool EnableProjectileInteraction = true;
		bool EnableMagicInteraction = true;
		bool EnableShoutInteraction = true;
		bool EnableElementalSnow = true;
		float ObjectInteractionRadius = 1152.0f;
		float ObjectScanInterval = 0.075f;
		float ReceiverContactTolerance = 7.0f;
		float ReceiverBlockerClearance = 4.0f;
		float FireMeltUnits = 9.0f;
		float FrostAddUnits = 7.0f;
		float ElementalHeightLimit = 24.0f;
		float ElementalRecoveryRate = 0.018f;
		std::vector<std::string> AlwaysCompressForms{};
		std::vector<std::string> NeverDeformForms{};
	};

	struct alignas(16) GroundData
	{
		uint EnableDeformableGround;
		uint EnableSnowDeformation;
		uint EnableMudDeformation;
		uint MudRequiresWetness;

		float SnowMaximumDepth;
		float MudMaximumDepth;
		float GroundNormalStrength;
		float SnowCompactionDarkening;

		float MudDarkening;
		float MudRoughness;
		float MudWetnessThreshold;
		float GroundResponseStrength;

		float2 PosOffset;
		DirectX::XMUINT2 ArrayOrigin;
	};
	STATIC_ASSERT_ALIGNAS_16(GroundData);

	struct alignas(16) BoundingBoxPacked
	{
		float2 MinExtent = { 0, 0 };  // initialized from first accepted collision shape
		float2 MaxExtent = { 0, 0 };
		uint IndexStart = 0;
		uint IndexEnd = 0;
		float2 pad0;
	};
	STATIC_ASSERT_ALIGNAS_16(BoundingBoxPacked);

	// Independent normalized world-space surface deformation field. Unlike the
	// grass collision texture this contains no camera-relative Z values:
	// x=current compaction, y=freshness/hold, z=previous compaction,
	// w=previous freshness. Snow and mud consume the same footprint field with
	// different raised-shell thickness/material responses.
	struct SurfaceStampBoxPacked
	{
		float2 MinExtent = { 0, 0 };
		float2 MaxExtent = { 0, 0 };
		uint IndexStart = 0;
		uint IndexEnd = 0;
		float2 pad0;
	};
	STATIC_ASSERT_ALIGNAS_16(SurfaceStampBoxPacked);

	struct SurfaceStampPacked
	{
		float2 CurrentPosition = { 0, 0 };   // absolute world XY
		float2 PreviousPosition = { 0, 0 };  // absolute world XY
		float Radius = 0.0f;                 // radius at CurrentPosition
		float PreviousRadius = 0.0f;         // radius at PreviousPosition (tapered/cone stamps)
		float Strength = 0.0f;               // normalized compaction strength
		float DisplacementScale = 1.0f;      // lateral displaced-snow contribution
		float ElementalDelta = 0.0f;         // signed snow height delta in world units
		float Smoothing = 0.0f;              // transient fire/heat compaction smoothing mask
		uint Flags = 0u;                     // GroundSurfaceStampFlags
		float ContactDepth = 0.0f;           // physical depth; flagged shout stamps store end/start falloff ratio
	};
	STATIC_ASSERT_ALIGNAS_16(SurfaceStampPacked);

	struct SurfaceFieldData
	{
		float2 OriginAbsolute = { 0, 0 };
		DirectX::XMUINT2 ArrayOrigin{};
		DirectX::XMINT2 ValidMargin{};
		float TimeDelta = 0.0f;
		uint StampBoxCount = 0;
		float TrackHoldSeconds = 0.0f;
		float RecoveryRate = 0.0f;   // normalized compaction units / second
		float StampStrength = 1.0f;
		float ElementalRecoveryRate = 0.0f;
	};
	STATIC_ASSERT_ALIGNAS_16(SurfaceFieldData);
	static_assert(sizeof(SurfaceStampBoxPacked) == 32, "GroundResponse::SurfaceStampBoxPacked ABI mismatch.");
	static_assert(sizeof(SurfaceStampPacked) == 48, "GroundResponse::SurfaceStampPacked ABI mismatch.");
	static_assert(sizeof(SurfaceFieldData) == 48, "GroundResponse::SurfaceFieldData ABI mismatch.");

	struct PerFrame
	{
		float2 PosOffset;              // cell origin in camera model space
		DirectX::XMUINT2 ArrayOrigin;  // xy: array origin (clipmap wrapping)

		DirectX::XMINT2 ValidMargin;
		float TimeDelta;
		uint BoundingBoxCount;

		float CameraHeightDelta;
		uint DebugInteractionField; // legacy/unused in PS; layout retained
		float TrackHoldSeconds;     // repurposes the old unused DebugDepthRange slot
		float TrackRecoveryRate;    // first 48 bytes remain compute-shader compatible

		// Per-terrain-draw runtime classification and unified geometric snow/mud controls.
		// These fields are consumed by PS/HS/DS b13 only. CollisionUpdateCS still
		// reads the unchanged first 48 bytes from b0.
		float4 TerrainSnow1to4;
		float4 TerrainSnow5to6;
		uint TerrainSnowValid;
		uint TerrainGeometryPass;
		uint TerrainMaterialForge;
		uint TerrainDebug;

		float SnowSurfaceThickness;
		float GeometryRenderDistance;
		float GeometryFadeStart;
		float GeometryMinimumSlopeZ;

		float GeometryTessellationNear;
		float GeometryTessellationFar;
		float GeometryTessellationNearDistance;
		float GeometryTessellationFarDistance;

		float SnowCoverageThreshold;
		float SnowCoverageFeather;
		uint RuntimeMagic;
		uint RuntimeVersion;

		// Absolute XY transform for the dedicated normalized surface field. This
		// is intentionally separate from PosOffset/ArrayOrigin, which remain the
		// legacy grass-collision transform and therefore may be camera-relative.
		float2 SurfaceOriginAbsolute;
		DirectX::XMUINT2 SurfaceArrayOrigin;

		// Slowly varying weather layer. Current/previous values keep terrain
		// motion vectors coherent while a storm accumulates or clear weather
		// settles the blanket back to the configured base thickness.
		float WeatherSnowRaise;
		float PreviousWeatherSnowRaise;
		float WeatherSnowIntensity;
		uint WeatherSnowEnabled;
	};
	STATIC_ASSERT_ALIGNAS_16(PerFrame);
	static_assert(offsetof(PerFrame, TerrainSnow1to4) == 48, "GroundResponse compute ABI must keep the first 48 bytes unchanged.");
	static_assert(offsetof(PerFrame, TerrainSnowValid) == 80, "GroundResponse b13 snow classification ABI mismatch.");
	static_assert(offsetof(PerFrame, SnowSurfaceThickness) == 96, "GroundResponse b13 geometry tuning ABI mismatch.");
	static_assert(offsetof(PerFrame, SnowCoverageThreshold) == 128, "GroundResponse b13 coverage tuning ABI mismatch.");
	static_assert(offsetof(PerFrame, RuntimeMagic) == 136, "GroundResponse b13 runtime magic ABI mismatch.");
	static_assert(offsetof(PerFrame, RuntimeVersion) == 140, "GroundResponse b13 runtime version ABI mismatch.");
	static_assert(offsetof(PerFrame, SurfaceOriginAbsolute) == 144, "GroundResponse b13 surface origin ABI mismatch.");
	static_assert(offsetof(PerFrame, SurfaceArrayOrigin) == 152, "GroundResponse b13 surface array origin ABI mismatch.");
	static_assert(offsetof(PerFrame, WeatherSnowRaise) == 160, "GroundResponse b13 weather-snow ABI mismatch.");
	static_assert(sizeof(PerFrame) == 176, "GroundResponse::PerFrame must match GroundResponse/Runtime.hlsli.");

	Settings settings;
	float weatherSnowRaiseState = 0.0f;
	float previousWeatherSnowRaiseState = 0.0f;
	float weatherSnowIntensityState = 0.0f;
	float2 currentPosOffset{};
	DirectX::XMUINT2 currentArrayOrigin{};
	GroundData GetGroundData() const;

	// Current b13 payload. The base copy is updated once per frame; terrain replay
	// temporarily overlays exact six-layer snow classification plus unified snow/mud geometry state per landscape draw.
	PerFrame currentPerFrame{};
	PerFrame activeTerrainRuntime{};

	ConstantBuffer* perFrame = nullptr;
	ConstantBuffer* surfacePerFrame = nullptr;

	eastl::unique_ptr<Buffer> collisionBoundingBoxes = nullptr;
	eastl::unique_ptr<Buffer> collisionInstances = nullptr;
	eastl::unique_ptr<Buffer> surfaceStampBoxes = nullptr;
	eastl::unique_ptr<Buffer> surfaceStamps = nullptr;

	eastl::vector<BoundingBoxPacked> queuedBoundingBoxes;
	eastl::vector<float4> queuedCollisions;
	eastl::vector<SurfaceStampBoxPacked> queuedSurfaceStampBoxes;
	eastl::vector<SurfaceStampPacked> queuedSurfaceStamps;

	/** @brief Releases cached GroundResponse compute/tessellation shaders. */
	virtual void ClearShaderCache() override;

	/** @brief Returns the grass/collider-height update compute shader. */
	ID3D11ComputeShader* GetCollisionUpdateCS();
	ID3D11ComputeShader* collisionUpdateCS = nullptr;

	/** @brief Returns the normalized persistent surface-deformation update compute shader. */
	ID3D11ComputeShader* GetSurfaceDeformationUpdateCS();
	ID3D11ComputeShader* surfaceDeformationUpdateCS = nullptr;

	/** Compiles the pass-through terrain tessellation stages used for the unified geometric snow/mud surface. */
	ID3D11HullShader* GetTerrainSurfaceHS(bool a_counterClockwise);
	ID3D11DomainShader* GetTerrainSurfaceDS();
	ID3D11HullShader* terrainSurfaceHSCW = nullptr;
	ID3D11HullShader* terrainSurfaceHSCCW = nullptr;
	ID3D11DomainShader* terrainSurfaceDS = nullptr;

	Texture2D* collisionTexture = nullptr;
	Texture2D* surfaceDeformationTexture = nullptr;
	Texture2D* surfaceDisplacementTexture = nullptr;  // t102 displaced snow
	Texture2D* surfaceElementalTexture = nullptr;     // t103 signed frost/fire height + heat smoothing
	// User-supplied, redistribution-cleared snow microsurface atlas. This is a
	// read-only optional detail resource; a missing file leaves the procedural
	// GroundResponse snow path intact.
	winrt::com_ptr<ID3D11ShaderResourceView> snowMicroTextureSRV;  // PS t110

	// PIXL_GR_13BF_CRISP_HULL_SHADOWS_V1
	// Owned copies of Skyrim's live directional shadow-cascade atlas captured while
	// RenderShadowmask has the correct PS t4 binding. The raised hull samples these
	// with its displaced world position instead of the stale base-terrain shadow mask.
	winrt::com_ptr<ID3D11Texture2D> directionalShadowAtlasCopyTexture;
	winrt::com_ptr<ID3D11ShaderResourceView> directionalShadowAtlasCopySRV;
	// Legacy 13BF ESRAM copy is retained for binary/source continuity but 13BG's
	// exact Utility directional path does not depend on it.
	winrt::com_ptr<ID3D11Texture2D> directionalShadowEsramCopyTexture;
	winrt::com_ptr<ID3D11ShaderResourceView> directionalShadowEsramCopySRV;

	// 13BG mirrors Utility PS b0/b2 into raw SRVs so the Lighting replay can use
	// the exact three-cascade split/projection/filter parameters at the displaced
	// hull receiver without consuming another D3D11 constant-buffer slot.
	winrt::com_ptr<ID3D11Buffer> directionalShadowTechniqueCopyBuffer;
	winrt::com_ptr<ID3D11ShaderResourceView> directionalShadowTechniqueCopySRV;
	winrt::com_ptr<ID3D11Buffer> directionalShadowGeometryCopyBuffer;
	winrt::com_ptr<ID3D11ShaderResourceView> directionalShadowGeometryCopySRV;
	winrt::com_ptr<ID3D11SamplerState> directionalShadowComparisonSampler;
	bool directionalShadowAtlasCaptured = false;

	// Optional Utility focus-shadow resources. The stencil is used only to select
	// a focus map; the actual comparison still uses the displaced world receiver.
	winrt::com_ptr<ID3D11Texture2D> directionalFocusStencilCopyTexture;
	winrt::com_ptr<ID3D11ShaderResourceView> directionalFocusStencilCopySRV;
	winrt::com_ptr<ID3D11Texture2D> directionalFocusShadowCopyTexture;
	winrt::com_ptr<ID3D11ShaderResourceView> directionalFocusShadowCopySRV;
	winrt::com_ptr<ID3D11SamplerState> directionalFocusShadowComparisonSampler;
	bool directionalFocusShadowCaptured = false;

	// Avoid clearing the full 1024² t103 UAV every frame while elemental snow is
	// disabled. The first disabled update clears stale mass/heat once; re-enabling
	// arms the one-shot clear for the next disable transition.
	bool surfaceElementalClearedWhileDisabled = false;

	struct GeometryTelemetry
	{
		uint TerrainPasses = 0;
		uint ClassificationValid = 0;
		uint MaterialForgePasses = 0;
		uint SnowBearingPasses = 0;
		uint GeometryWanted = 0;
		uint TopologyRejected = 0;
		uint ExistingTessellation = 0;
		uint ShaderFailure = 0;
		uint HsDsApplied = 0;
		uint PlayerProxyStamps = 0;
		uint ClipmapResets = 0;
		uint CameraRebaseSuppressed = 0;
	};
	GeometryTelemetry geometryTelemetry{};
	float geometryTelemetrySeconds = 0.0f;
	void FlushGeometryTelemetry();

	// TerrainSeam replay state. The original Lighting VS/PS and material bindings
	// remain authoritative; GroundResponse inserts identical HS/DS on every eligible nearby terrain pass so layered terrain passes cannot split onto different planes.
	bool activeTerrainPass = false;
	bool terrainGeometryWanted = false;
	bool terrainOverrideApplied = false;
	bool terrainGeometryRenderedThisFrame = false;
	D3D11_PRIMITIVE_TOPOLOGY savedTerrainTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
	ID3D11HullShader* savedTerrainHS = nullptr;
	ID3D11DomainShader* savedTerrainDS = nullptr;
	ID3D11Buffer* savedTerrainHSCB5to6[2] = { nullptr, nullptr };
	ID3D11Buffer* savedTerrainHSCB12to13[2] = { nullptr, nullptr };
	ID3D11Buffer* savedTerrainDSCB5to6[2] = { nullptr, nullptr };
	ID3D11Buffer* savedTerrainDSCB12to13[2] = { nullptr, nullptr };
	ID3D11ShaderResourceView* savedTerrainDSSRV101 = nullptr;
	ID3D11ShaderResourceView* savedTerrainDSSRV102 = nullptr;
	ID3D11ShaderResourceView* savedTerrainDSSRV103 = nullptr;
	ID3D11ShaderResourceView* savedTerrainPSSRV104 = nullptr;
	ID3D11ShaderResourceView* savedTerrainPSSRV105 = nullptr;
	ID3D11ShaderResourceView* savedTerrainPSSRV106 = nullptr;
	ID3D11ShaderResourceView* savedTerrainPSSRV107 = nullptr;
	ID3D11ShaderResourceView* savedTerrainPSSRV108 = nullptr;
	ID3D11ShaderResourceView* savedTerrainPSSRV109 = nullptr;
	ID3D11SamplerState* savedTerrainPSSampler7 = nullptr;
	ID3D11SamplerState* savedTerrainPSSampler8 = nullptr;
	ID3D11RasterizerState* savedTerrainRasterizer = nullptr;

	struct TerrainRasterizerVariant
	{
		D3D11_RASTERIZER_DESC sourceDesc{};
		ID3D11RasterizerState* noCullState = nullptr;
	};
	eastl::vector<TerrainRasterizerVariant> terrainNoCullRasterizers;
	ID3D11RasterizerState* GetTerrainNoCullRasterizer(const D3D11_RASTERIZER_DESC& a_sourceDesc);

	/** Clears transient terrain replay state at the start of a frame. */
	virtual void Reset() override;
	/** Resolves exact snow-layer metadata and prepares b13 before TerrainSeam replays one terrain draw. */
	void PrepareTerrainPass(RE::BSRenderPass* a_pass);
	/** Captures the live sun cascade atlas while Skyrim is building RenderShadowmask. */
	void CaptureDirectionalShadowAtlas(bool a_captureFocusShadow = false);
	/** Rebinds GroundResponse resources after Skyrim's per-draw setup and optionally inserts HS/DS. */
	void TerrainPassShaderHacks();
	/** Restores topology/stages after the raw TerrainSeam draw. */
	void FinishTerrainPass();
	/** Restores a non-pass-specific b13 payload so classification cannot leak to later draws. */
	void BindDefaultRuntimeData();
	/** Returns exact six-layer snow flags for vanilla or Material Forge landscape materials. */
	bool GetTerrainSnowFlags(RE::BSRenderPass* a_pass, std::array<float, 6>& a_flags, bool& a_materialForge) const;

	/** @brief Creates the collision texture, structured buffers for bounding boxes and collision instances. */
	virtual void SetupResources() override;
	/** @brief Updates the shared actor interaction field before terrain rendering. */
	virtual void EarlyPrepass() override;

	/** @brief Draws the ImGui settings UI for grass collision options. */
	virtual void DrawSettings() override;
	/**
	 * @brief Gathers collision shapes from nearby actors and queues them for GPU upload.
	 *
	 * Sorted by distance, limited to MAX_BOUNDING_BOXES actors with up to
	 * MAX_COLLISIONS_PER_BOUNDING_BOX shapes each.
	 */
	void QueueCollisions();
	/**
	 * @brief Uploads queued collision data to GPU buffers and dispatches the collision texture update.
	 *
	 * Called once per frame from the grass shader setup geometry hook.
	 */
	void Update();

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;

	/** Queues one projectile/magic impact into the absolute-world surface field. */
	void QueueProjectileImpact(
		RE::Projectile* a_projectile,
		const RE::NiPoint3& a_position,
		const RE::NiPoint3& a_velocity,
		RE::TESObjectREFR* a_target = nullptr);
	/** Converts an elemental concentration/cone/breath spell into receiver-aware ground interaction. */
	void QueueMagicCast(RE::TESObjectREFR* a_caster, RE::FormID a_spellFormID, bool a_continuousTick = false);
	/** Converts a TESShout variation spell cast into an occlusion-aware tapered cone. */
	void QueueShoutCast(RE::TESObjectREFR* a_caster, RE::FormID a_spellFormID);

	/** @brief Registers spell/shout events and resolves configured compatibility overrides. */
	virtual void DataLoaded() override;

	/** @brief Installs the BSGrassShader and main update hooks after all plugins have loaded. */
	virtual void PostPostLoad() override;


	struct SpellCastEventSink : RE::BSTEventSink<RE::TESSpellCastEvent>
	{
		static SpellCastEventSink* GetSingleton()
		{
			static SpellCastEventSink singleton;
			return &singleton;
		}

		RE::BSEventNotifyControl ProcessEvent(
			const RE::TESSpellCastEvent* a_event,
			RE::BSTEventSource<RE::TESSpellCastEvent>* a_source) override;
	};

	struct Hooks
	{
		struct BSGrassShader_SetupGeometry
		{
			static void thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct MainUpdate_QueueCollisions
		{
			static void thunk();
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// 13BH uses ProcessImpacts as the authoritative projectile interception
		// point. The ImpactData list already contains the exact world hit and survives
		// projectile classes that bypass the concrete AddImpact vtable entries.
		struct Projectile_ProcessImpacts
		{
			static bool thunk(RE::Projectile* This);
			static inline REL::Relocation<decltype(thunk)> func;
		};
		struct MissileProjectile_ProcessImpacts
		{
			static bool thunk(RE::MissileProjectile* This);
			static inline REL::Relocation<decltype(thunk)> func;
		};
		struct ArrowProjectile_ProcessImpacts
		{
			static bool thunk(RE::ArrowProjectile* This);
			static inline REL::Relocation<decltype(thunk)> func;
		};
		struct GrenadeProjectile_ProcessImpacts
		{
			static bool thunk(RE::GrenadeProjectile* This);
			static inline REL::Relocation<decltype(thunk)> func;
		};
		struct BeamProjectile_ProcessImpacts
		{
			static bool thunk(RE::BeamProjectile* This);
			static inline REL::Relocation<decltype(thunk)> func;
		};
		struct FlameProjectile_ProcessImpacts
		{
			static bool thunk(RE::FlameProjectile* This);
			static inline REL::Relocation<decltype(thunk)> func;
		};
		struct ConeProjectile_ProcessImpacts
		{
			static bool thunk(RE::ConeProjectile* This);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct MissileProjectile_AddImpact
		{
			static void thunk(RE::MissileProjectile* This, RE::TESObjectREFR* a_ref, const RE::NiPoint3& a_targetLoc, const RE::NiPoint3& a_velocity, RE::hkpCollidable* a_collidable, std::int32_t a_arg6, std::uint32_t a_arg7);
			static inline REL::Relocation<decltype(thunk)> func;
		};
		struct ArrowProjectile_AddImpact
		{
			static void thunk(RE::ArrowProjectile* This, RE::TESObjectREFR* a_ref, const RE::NiPoint3& a_targetLoc, const RE::NiPoint3& a_velocity, RE::hkpCollidable* a_collidable, std::int32_t a_arg6, std::uint32_t a_arg7);
			static inline REL::Relocation<decltype(thunk)> func;
		};
		struct GrenadeProjectile_AddImpact
		{
			static void thunk(RE::GrenadeProjectile* This, RE::TESObjectREFR* a_ref, const RE::NiPoint3& a_targetLoc, const RE::NiPoint3& a_velocity, RE::hkpCollidable* a_collidable, std::int32_t a_arg6, std::uint32_t a_arg7);
			static inline REL::Relocation<decltype(thunk)> func;
		};
		struct BeamProjectile_AddImpact
		{
			static void thunk(RE::BeamProjectile* This, RE::TESObjectREFR* a_ref, const RE::NiPoint3& a_targetLoc, const RE::NiPoint3& a_velocity, RE::hkpCollidable* a_collidable, std::int32_t a_arg6, std::uint32_t a_arg7);
			static inline REL::Relocation<decltype(thunk)> func;
		};
		struct FlameProjectile_AddImpact
		{
			static void thunk(RE::FlameProjectile* This, RE::TESObjectREFR* a_ref, const RE::NiPoint3& a_targetLoc, const RE::NiPoint3& a_velocity, RE::hkpCollidable* a_collidable, std::int32_t a_arg6, std::uint32_t a_arg7);
			static inline REL::Relocation<decltype(thunk)> func;
		};
		struct ConeProjectile_AddImpact
		{
			static void thunk(RE::ConeProjectile* This, RE::TESObjectREFR* a_ref, const RE::NiPoint3& a_targetLoc, const RE::NiPoint3& a_velocity, RE::hkpCollidable* a_collidable, std::int32_t a_arg6, std::uint32_t a_arg7);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		static void Install()
		{
			stl::write_vfunc<0x6, BSGrassShader_SetupGeometry>(RE::VTABLE_BSGrassShader[0]);
			stl::write_thunk_call<MainUpdate_QueueCollisions>(REL::RelocationID(35565, 36564).address() + REL::Relocate(0x748, 0xC26));
#ifndef SKYRIM_CROSS_VR
			// 13BH: ProcessImpacts (AC) is later and more authoritative than AddImpact
			// for this PIXL/Skyrim stack. Hook the base plus every concrete projectile
			// family we care about, and also scan Projectile::Manager as a fail-safe.
			stl::write_vfunc<0xAC, Projectile_ProcessImpacts>(RE::VTABLE_Projectile[0]);
			stl::write_vfunc<0xAC, MissileProjectile_ProcessImpacts>(RE::VTABLE_MissileProjectile[0]);
			stl::write_vfunc<0xAC, ArrowProjectile_ProcessImpacts>(RE::VTABLE_ArrowProjectile[0]);
			stl::write_vfunc<0xAC, GrenadeProjectile_ProcessImpacts>(RE::VTABLE_GrenadeProjectile[0]);
			stl::write_vfunc<0xAC, BeamProjectile_ProcessImpacts>(RE::VTABLE_BeamProjectile[0]);
			stl::write_vfunc<0xAC, FlameProjectile_ProcessImpacts>(RE::VTABLE_FlameProjectile[0]);
			stl::write_vfunc<0xAC, ConeProjectile_ProcessImpacts>(RE::VTABLE_ConeProjectile[0]);
#endif
			logger::info("[GroundResponse] Installed grass, update, ProcessImpacts projectile hooks and projectile-manager fallback");
		}
	};
};
