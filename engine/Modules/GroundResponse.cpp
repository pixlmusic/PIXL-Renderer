#include "GroundResponse.h"

#include "Globals.h"
#include "I18n/I18n.h"
#include "MaterialForge/BSLightingShaderMaterialPBRLandscape.h"
#include "ShaderCache.h"
#include "RainResponse.h"


#include "State.h"
#include "Utils/ActorUtils.h"
#include "Utils/D3D.h"

#include <RE/B/BGSMaterialType.h>
#include <RE/B/BGSProjectile.h>
#include <RE/M/MagicCaster.h>
#include <RE/S/SpellItem.h>
#include <RE/T/TES.h>
#include <RE/T/TESLandTexture.h>
#include <RE/T/TESObjectCELL.h>
#include <RE/T/TESObjectLAND.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#define I18N_KEY_PREFIX "feature.ground_response."

static constexpr uint MAX_BOUNDING_BOXES = 64;
static constexpr uint MAX_COLLISIONS_PER_BOUNDING_BOX = 64;
static constexpr uint MAX_COLLISIONS = MAX_BOUNDING_BOXES * MAX_COLLISIONS_PER_BOUNDING_BOX;
static constexpr float MAX_ACTOR_DISTANCE = 2048.0f;
static constexpr float MAX_ACTOR_SQ_DISTANCE = MAX_ACTOR_DISTANCE * MAX_ACTOR_DISTANCE;
static constexpr float MIN_COLLISION_RADIUS_DISTANCE_SCALE = 0.001f;

// Ground Response consumes physics bounds from arbitrary actor skeletons. Keep a
// generous gameplay-safe ceiling while rejecting malformed/placeholder bounds
// that would otherwise stamp a huge portion of the 4096-unit interaction field.
static constexpr float MIN_COLLISION_RADIUS = 0.05f;
static constexpr float MAX_COLLISION_RADIUS = 512.0f;

static constexpr float INTERACTION_WORLD_SIZE = 4096.0f;
static constexpr uint INTERACTION_TEXTURE_SIZE = 512u;

// Dedicated snow/mud surface field. It stores normalized compaction only; no
// camera-relative height ever enters this texture.
static constexpr float SURFACE_WORLD_SIZE = 4096.0f;
static constexpr uint SURFACE_TEXTURE_SIZE = 1024u;
static constexpr uint MAX_SURFACE_STAMP_BOXES = 64u;
static constexpr uint MAX_SURFACE_STAMPS_PER_BOX = 24u;
static constexpr uint MAX_SURFACE_STAMPS = MAX_SURFACE_STAMP_BOXES * MAX_SURFACE_STAMPS_PER_BOX;
static constexpr float SURFACE_MIN_SOURCE_RADIUS = 1.5f;
static constexpr float SURFACE_MAX_SOURCE_RADIUS = 42.0f;
static constexpr float SURFACE_GROUND_BAND = 24.0f;
static constexpr float SURFACE_ACTOR_Z_TOLERANCE = 96.0f;
static constexpr float SURFACE_STAMP_MIN_RADIUS = 3.0f;
static constexpr float SURFACE_STAMP_MAX_RADIUS = 22.0f;
static constexpr float SURFACE_TELEPORT_DISTANCE = 256.0f;
static constexpr uint SURFACE_STAMP_FLAG_DISTANCE_FALLOFF = 1u << 0;
static constexpr uint MAX_ACTOR_SURFACE_STAMP_BOXES = 48u;
static constexpr uint MAX_PENDING_GROUND_INTERACTIONS = 128u;
static constexpr float OBJECT_MAX_STAMP_RADIUS = 38.0f;
static constexpr float OBJECT_MIN_STAMP_RADIUS = 3.0f;
static constexpr float ELEMENTAL_HARD_LIMIT = 32.0f;
static constexpr float MAGIC_STREAM_UPDATE_INTERVAL = 0.075f;

static constexpr float MAX_GROUND_FRAME_DELTA = 0.25f;
static constexpr float CLIPMAP_VERTICAL_RESET_DISTANCE = INTERACTION_WORLD_SIZE * 0.5f;
// Reject only pathological camera-Z discontinuities (for example a transient eye
// origin during camera/equipment graph changes). Normal first/third-person camera
// offsets remain fully rebased and preserve the field.
static constexpr float MAX_CAMERA_REBASE_DELTA = 512.0f;
// Legacy grass-only player proxy. Snow/mud no longer consume this camera-relative
// height path; their fallback stamp is written separately in absolute world XY.
static constexpr float PLAYER_GROUND_PROXY_RADIUS = 14.0f;
static constexpr float PLAYER_GROUND_PROXY_CENTER_Z = 7.0f;
static constexpr uint GROUND_RUNTIME_MAGIC = 0x47523330u;      // "GR30"
static constexpr uint GROUND_RUNTIME_VERSION = 0x00030000u;
static constexpr uint TERRAIN_DEBUG_OVERLAY = 1u << 0;
static constexpr uint TERRAIN_DEBUG_GEOMETRY_SELF_TEST = 1u << 1;
// ABI-safe runtime bit: the 160-byte b13 layout is unchanged.
static constexpr uint TERRAIN_DEBUG_RAW_DIRECTIONAL_SHADOW = 1u << 2;
static constexpr uint TERRAIN_DEBUG_FOCUS_DIRECTIONAL_SHADOW = 1u << 3;

// TerrainSeam intentionally enables alpha blending for its deferred terrain replay.
// GroundResponse's raised snow/mud shell is a real opaque surface and must not
// inherit that blend mode. These POD snapshots are captured only for an active
// geometric terrain draw and restored immediately afterward.
static ID3D11BlendState* g_savedTerrainBlendState = nullptr;
static float g_savedTerrainBlendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
static UINT g_savedTerrainSampleMask = 0xFFFFFFFFu;
// PIXL_GR_13AV_AUTHORITATIVE_MAIN_DEPTH_V1
//
// Raised GroundResponse terrain must write into Skyrim's authoritative kMAIN
// scene depth while keeping the exact terrain replay MRT set. Translation-unit
// local only: GroundResponse.h, bridge ABI and shader ABI remain untouched.
static ID3D11RenderTargetView*
	g_savedGroundResponseRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
static ID3D11DepthStencilView* g_savedGroundResponseDSV = nullptr;
static bool g_groundResponseOMCaptured = false;
static bool g_groundResponseMainDepthRebound = false;

// 13P: after 13O fixed TerrainSeam's colour/GBuffer coverage, the remaining
// "certain objects cut through" case is depth authority. Some forward/late
// objects render after the delayed terrain replay and can only be rejected if
// the RAISED HS/DS shell has actually written the main hardware depth buffer.
static ID3D11DepthStencilState* g_savedTerrainDepthStencilState = nullptr;
static UINT g_savedTerrainStencilRef = 0u;

struct GroundShellDepthStateVariant
{
	D3D11_DEPTH_STENCIL_DESC sourceDesc{};
	ID3D11DepthStencilState* shellState = nullptr;
};

static std::vector<GroundShellDepthStateVariant> g_groundShellDepthStates;


// PIXL_GR_13BF_CRISP_HULL_SHADOWS_V1
// Copy a live depth-shadow SRV into an owned SRV texture. Skyrim reuses/aliases
// its shadow targets later in the frame, so holding the live SRV is not enough.
static bool GroundCopyShadowTarget(
	ID3D11ShaderResourceView* a_sourceSRV,
	const char* a_debugName,
	winrt::com_ptr<ID3D11Texture2D>& a_copyTexture,
	winrt::com_ptr<ID3D11ShaderResourceView>& a_copySRV)
{
	if (!a_sourceSRV)
		return false;

	winrt::com_ptr<ID3D11Resource> sourceResource;
	a_sourceSRV->GetResource(sourceResource.put());
	auto sourceTexture = sourceResource.try_as<ID3D11Texture2D>();
	if (!sourceTexture)
		return false;

	D3D11_TEXTURE2D_DESC sourceDesc{};
	sourceTexture->GetDesc(&sourceDesc);

	bool recreate = !a_copyTexture || !a_copySRV;
	if (a_copyTexture) {
		D3D11_TEXTURE2D_DESC copyDesc{};
		a_copyTexture->GetDesc(&copyDesc);
		recreate =
			recreate ||
			copyDesc.Width != sourceDesc.Width ||
			copyDesc.Height != sourceDesc.Height ||
			copyDesc.MipLevels != sourceDesc.MipLevels ||
			copyDesc.ArraySize != sourceDesc.ArraySize ||
			copyDesc.Format != sourceDesc.Format ||
			copyDesc.SampleDesc.Count != sourceDesc.SampleDesc.Count ||
			copyDesc.SampleDesc.Quality != sourceDesc.SampleDesc.Quality;
	}

	if (recreate) {
		a_copySRV = nullptr;
		a_copyTexture = nullptr;

		D3D11_TEXTURE2D_DESC copyDesc = sourceDesc;
		copyDesc.Usage = D3D11_USAGE_DEFAULT;
		copyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		copyDesc.CPUAccessFlags = 0;
		copyDesc.MiscFlags = 0;

		if (FAILED(globals::d3d::device->CreateTexture2D(
				&copyDesc, nullptr, a_copyTexture.put()))) {
			return false;
		}
		Util::SetResourceName(a_copyTexture.get(), a_debugName);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		a_sourceSRV->GetDesc(&srvDesc);
		if (FAILED(globals::d3d::device->CreateShaderResourceView(
				a_copyTexture.get(), &srvDesc, a_copySRV.put()))) {
			a_copyTexture = nullptr;
			return false;
		}
	}

	globals::d3d::context->CopyResource(
		a_copyTexture.get(),
		sourceTexture.get());
	return a_copySRV != nullptr;
}

// PIXL_GR_13BG_UTILITY_SHADOW_CONSTANT_CAPTURE_V1
// Utility's exact shadow-mask matrices/splits live in PS b0/b2. PIXL already
// owns b13 for GroundResponse, so mirror those constant-buffer bytes into raw
// SRVs and decode their float4 registers in ShadowSampling.hlsli. This remains
// GPU-only and avoids a CPU readback/stall on every shadow-mask setup.
static bool GroundCopyConstantBufferToRawSRV(
	ID3D11Buffer* a_sourceBuffer,
	UINT a_minimumBytes,
	const char* a_debugName,
	winrt::com_ptr<ID3D11Buffer>& a_copyBuffer,
	winrt::com_ptr<ID3D11ShaderResourceView>& a_copySRV)
{
	if (!a_sourceBuffer)
		return false;

	D3D11_BUFFER_DESC sourceDesc{};
	a_sourceBuffer->GetDesc(&sourceDesc);
	if (sourceDesc.ByteWidth < a_minimumBytes || (sourceDesc.ByteWidth & 15u) != 0u)
		return false;

	bool recreate = !a_copyBuffer || !a_copySRV;
	if (a_copyBuffer) {
		D3D11_BUFFER_DESC copyDesc{};
		a_copyBuffer->GetDesc(&copyDesc);
		recreate = recreate || copyDesc.ByteWidth != sourceDesc.ByteWidth;
	}

	if (recreate) {
		a_copySRV = nullptr;
		a_copyBuffer = nullptr;

		D3D11_BUFFER_DESC copyDesc{};
		copyDesc.ByteWidth = sourceDesc.ByteWidth;
		copyDesc.Usage = D3D11_USAGE_DEFAULT;
		copyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		copyDesc.CPUAccessFlags = 0u;
		copyDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
		copyDesc.StructureByteStride = 0u;
		if (FAILED(globals::d3d::device->CreateBuffer(
				&copyDesc, nullptr, a_copyBuffer.put()))) {
			return false;
		}
		Util::SetResourceName(a_copyBuffer.get(), a_debugName);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
		srvDesc.BufferEx.FirstElement = 0u;
		srvDesc.BufferEx.NumElements = sourceDesc.ByteWidth / sizeof(std::uint32_t);
		srvDesc.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
		if (FAILED(globals::d3d::device->CreateShaderResourceView(
				a_copyBuffer.get(), &srvDesc, a_copySRV.put()))) {
			a_copyBuffer = nullptr;
			return false;
		}
	}

	globals::d3d::context->CopyResource(a_copyBuffer.get(), a_sourceBuffer);
	return a_copySRV != nullptr;
}

struct GroundResponseActorCandidate
{
	RE::ActorHandle handle;
	float sqDistance;
};


namespace
{
	struct GroundMovementResistanceSettings
	{
		bool EnableMovementResistance = true;
		bool EnablePlayerResistance = true;
		bool EnableNPCResistance = true;
		float SnowResistanceStrength = 1.0f;
		float FluffySnowResistanceStrength = 0.36f;
		float MudResistanceStrength = 0.82f;
		float MudMinimumSpeedScale = 0.62f;
		float MinimumSurfaceSpeedScale = 0.32f;
		float SnowResistanceStartDepth = 1.0f;
		float SnowFullResistanceDepth = 68.0f;
		float ResistanceResponseRate = 6.5f;
		float ResistanceUpdateInterval = 0.075f;
		bool DebugMovementResistance = false;
	};

	enum class GroundResistanceSurface : std::uint8_t
	{
		kNone,
		kSnow,
		kMud
	};

	enum class GroundSnowClassifier : std::uint8_t
	{
		kNone,
		kLandscapeBlend,
		kPhysicalMaterial,
		kRendererTextureFlag,
		kTextureNameFallback
	};

	struct GroundResistanceSample
	{
		GroundResistanceSurface surface = GroundResistanceSurface::kNone;
		GroundSnowClassifier snowClassifier = GroundSnowClassifier::kNone;
		float pristineDepth = 0.0f;
		float targetSpeedScale = 1.0f;
		float slopeMask = 0.0f;
		float wetnessActivation = 0.0f;
		float snowCoverage = 0.0f;
		float snowActivation = 0.0f;
		std::uint32_t materialID = 0u;
	};

	struct GroundMovementResistanceTrack
	{
		RE::ActorHandle handle;
		std::uint32_t actorID = 0u;
		std::uint32_t lastSeenGeneration = 0u;
		float filteredSpeedScale = 1.0f;
		float appliedSpeedDelta = 0.0f;

		// Skyrim caches player locomotion speed. SpeedMult can change numerically
		// while the controller continues using its old cached movement speed.
		// A tiny temporary CarryWeight pulse is the established refresh trigger.
		float carryWeightRefreshTimer = 0.0f;
		float carryWeightRefreshAmount = 0.0f;
		float lastRefreshedSpeedDelta = 0.0f;
	};

	static GroundMovementResistanceSettings g_groundResistanceSettings{};
	static std::vector<GroundMovementResistanceTrack> g_groundResistanceTracks{};
	static float g_groundResistanceAccumulator = 0.0f;
	static std::uint32_t g_groundResistanceGeneration = 0u;
	static std::uint32_t g_groundResistanceDiagCount = 0u;

	static GroundResistanceSurface g_lastPlayerResistanceSurface =
		GroundResistanceSurface::kNone;
	static float g_lastPlayerResistanceDepth = 0.0f;
	static float g_lastPlayerResistanceTargetScale = 1.0f;
	static float g_lastPlayerResistanceFilteredScale = 1.0f;
	static std::uint32_t g_lastPlayerResistanceMaterial = 0u;
	static GroundSnowClassifier g_lastPlayerSnowClassifier =
		GroundSnowClassifier::kNone;
	static float g_lastPlayerObservedSpeedMult = 0.0f;
	static float g_lastPlayerAppliedSpeedDelta = 0.0f;
	static float g_lastPlayerSnowCoverage = 0.0f;
	static float g_lastPlayerSnowActivation = 0.0f;

	// Renderer-derived snow metadata cache.
	//
	// The visual snow shell uses BSLightingShaderMaterialLandscape::textureIsSnow,
	// not TES physical material IDs. The first resistance build only checked
	// MATERIAL_ID::kSnow/kSnowStairs, so modded/vanilla landscape textures whose
	// physical material is generic dirt/stone could visibly render deep snow but
	// never enter the gameplay resistance branch.
	//
	// Cache the renderer's authoritative per-texture snow flag by diffuse texture
	// path. Game-thread actor classification can then match TESLandTexture's diffuse
	// texture path against the exact flag observed in a real terrain draw.
	static std::mutex g_resistanceSnowTextureCacheMutex;
	static std::unordered_map<std::string, float>
		g_resistanceSnowTextureFlags;

	// PIXL_GR_13BI_CPU_WIND_DRIFT_PARITY_V1
	// These constants MUST mirror the 13BI TerrainSurface.hlsl pristine snow field.
	static constexpr float RESIST_SNOW_FINE_VARIATION = 0.10f;
	static constexpr float RESIST_SNOW_POCKET_STRENGTH = 0.22f;
	static constexpr float RESIST_SNOW_FINE_SCALE = 180.0f;
	static constexpr float RESIST_SNOW_POCKET_SCALE = 620.0f;
	static constexpr float RESIST_SNOW_MOUND_STRENGTH = 0.32f;
	static constexpr float RESIST_SNOW_MOUND_SCALE = 300.0f;
	static constexpr float RESIST_SNOW_MOUND_DETAIL_SCALE = 170.0f;
	static constexpr float RESIST_SNOW_MAX_PRISTINE_HEIGHT = 72.0f;
	static constexpr float RESIST_SNOW_DEEP_DRIFT_SCALE = 560.0f;
	static constexpr float RESIST_SNOW_DEEP_DRIFT_DETAIL_SCALE = 290.0f;
	static constexpr float RESIST_SNOW_DEEP_DRIFT_BLUR_RADIUS = 120.0f;
	static constexpr float RESIST_SNOW_MOUND_BLUR_RADIUS = 58.0f;
	static constexpr float RESIST_SNOW_WIND_X = 0.8192319f;
	static constexpr float RESIST_SNOW_WIND_Y = 0.5734623f;

	static constexpr float RESIST_MUD_FINE_VARIATION = 0.08f;
	static constexpr float RESIST_MUD_POCKET_STRENGTH = 0.16f;
	static constexpr float RESIST_MUD_FINE_SCALE = 200.0f;
	static constexpr float RESIST_MUD_POCKET_SCALE = 480.0f;

	static constexpr float RESIST_SLOPE_SAMPLE_STEP = 32.0f;
	static constexpr float RESIST_LAND_Z_TOLERANCE = 128.0f;

	float ResistanceSaturate(float a_value)
	{
		return std::clamp(a_value, 0.0f, 1.0f);
	}

	float ResistanceSmoothStep(float a_edge0, float a_edge1, float a_value)
	{
		const float denom = a_edge1 - a_edge0;
		if (std::abs(denom) <= 1.0e-6f)
			return a_value >= a_edge1 ? 1.0f : 0.0f;

		const float t =
			ResistanceSaturate(
				(a_value - a_edge0) / denom);
		return t * t * (3.0f - 2.0f * t);
	}

	float ResistanceLerp(float a_a, float a_b, float a_t)
	{
		return a_a + (a_b - a_a) * a_t;
	}

	std::uint32_t ResistanceDepthHash(
		std::int32_t a_x,
		std::int32_t a_y)
	{
		// Mirrors HLSL asuint(int) + uint overflow exactly.
		std::uint32_t x = static_cast<std::uint32_t>(a_x);
		std::uint32_t y = static_cast<std::uint32_t>(a_y);
		std::uint32_t h =
			x * 0x8da6b343u ^
			y * 0xd8163841u;
		h ^= h >> 13;
		h *= 0x85ebca6bu;
		h ^= h >> 16;
		return h;
	}

	float ResistanceDepthHash01(
		std::int32_t a_x,
		std::int32_t a_y)
	{
		return
			static_cast<float>(
				ResistanceDepthHash(a_x, a_y) &
				0x00FFFFFFu) *
			(1.0f / 16777215.0f);
	}

	float ResistanceQuintic(float a_value)
	{
		return
			a_value * a_value * a_value *
			(a_value *
				(a_value * 6.0f - 15.0f) +
				10.0f);
	}

	float ResistanceDepthValueNoise(
		float a_x,
		float a_y,
		float a_scale)
	{
		const float safeScale =
			std::max(a_scale, 1.0f);
		const float gridX = a_x / safeScale;
		const float gridY = a_y / safeScale;

		const float floorX = std::floor(gridX);
		const float floorY = std::floor(gridY);
		const std::int32_t cellX =
			static_cast<std::int32_t>(floorX);
		const std::int32_t cellY =
			static_cast<std::int32_t>(floorY);

		const float fx =
			ResistanceQuintic(gridX - floorX);
		const float fy =
			ResistanceQuintic(gridY - floorY);

		const float n00 =
			ResistanceDepthHash01(cellX, cellY);
		const float n10 =
			ResistanceDepthHash01(cellX + 1, cellY);
		const float n01 =
			ResistanceDepthHash01(cellX, cellY + 1);
		const float n11 =
			ResistanceDepthHash01(cellX + 1, cellY + 1);

		return
			ResistanceLerp(
				ResistanceLerp(n00, n10, fx),
				ResistanceLerp(n01, n11, fx),
				fy);
	}

	float ResistanceDepthFineNoise(
		float a_x,
		float a_y,
		float a_scale)
	{
		const float n0 =
			ResistanceDepthValueNoise(
				a_x,
				a_y,
				a_scale);
		const float n1 =
			ResistanceDepthValueNoise(
				a_x + 137.0f,
				a_y - 251.0f,
				std::max(a_scale * 0.47f, 1.0f));
		return n0 * 0.68f + n1 * 0.32f;
	}

	float ResistanceDepthBlurredNoise(
		float a_x,
		float a_y,
		float a_scale,
		float a_radius)
	{
		const float centre =
			ResistanceDepthValueNoise(
				a_x,
				a_y,
				a_scale);
		const float left =
			ResistanceDepthValueNoise(
				a_x - a_radius,
				a_y,
				a_scale);
		const float right =
			ResistanceDepthValueNoise(
				a_x + a_radius,
				a_y,
				a_scale);
		const float down =
			ResistanceDepthValueNoise(
				a_x,
				a_y - a_radius,
				a_scale);
		const float up =
			ResistanceDepthValueNoise(
				a_x,
				a_y + a_radius,
				a_scale);

		return
			centre * 0.40f +
			(left + right + down + up) * 0.15f;
	}

	void ResistanceSnowWindSpace(
		float a_x,
		float a_y,
		float& a_outX,
		float& a_outY)
	{
		// Exact CPU mirror of GroundSnowWindSpace().
		a_outX =
			(a_x * RESIST_SNOW_WIND_X +
			 a_y * RESIST_SNOW_WIND_Y) *
			0.44f;
		a_outY =
			(a_x * -RESIST_SNOW_WIND_Y +
			 a_y * RESIST_SNOW_WIND_X) *
			1.30f;
	}
	float ResistanceSnowMoundSignal(float a_x, float a_y)
	{
		float windX = 0.0f;
		float windY = 0.0f;
		ResistanceSnowWindSpace(a_x, a_y, windX, windY);

		const float moundBase =
			ResistanceDepthBlurredNoise(
				windX + 91.0f,
				windY - 173.0f,
				RESIST_SNOW_MOUND_SCALE,
				RESIST_SNOW_MOUND_BLUR_RADIUS);
		const float moundDetail =
			ResistanceDepthBlurredNoise(
				windX - 247.0f,
				windY + 119.0f,
				RESIST_SNOW_MOUND_DETAIL_SCALE,
				RESIST_SNOW_MOUND_BLUR_RADIUS * 0.65f);

		const float moundSeed =
			moundBase * 0.86f +
			moundDetail * 0.14f;

		return
			std::pow(
				ResistanceSmoothStep(
					0.53f,
					0.84f,
					moundSeed),
				1.60f);
	}

	float ResistanceSnowDeepDriftSignal(float a_x, float a_y)
	{
		float windX = 0.0f;
		float windY = 0.0f;
		ResistanceSnowWindSpace(a_x, a_y, windX, windY);

		const float broad =
			ResistanceDepthBlurredNoise(
				windX + 613.0f,
				windY - 379.0f,
				RESIST_SNOW_DEEP_DRIFT_SCALE,
				RESIST_SNOW_DEEP_DRIFT_BLUR_RADIUS);
		const float detail =
			ResistanceDepthBlurredNoise(
				windX - 337.0f,
				windY + 547.0f,
				RESIST_SNOW_DEEP_DRIFT_DETAIL_SCALE,
				RESIST_SNOW_DEEP_DRIFT_BLUR_RADIUS *
					0.60f);

		const float seed =
			broad * 0.90f +
			detail * 0.10f;

		return
			std::pow(
				ResistanceSmoothStep(
					0.54f,
					0.79f,
					seed),
				1.50f);
	}

	float ResistanceSnowVariationScale(float a_x, float a_y){
		const float snowFine =
			(ResistanceDepthFineNoise(
				a_x,
				a_y,
				RESIST_SNOW_FINE_SCALE) *
					2.0f -
				1.0f) *
			RESIST_SNOW_FINE_VARIATION;

		const float snowPocket =
			ResistanceSmoothStep(
				0.52f,
				0.88f,
				ResistanceDepthValueNoise(
					a_x - 421.0f,
					a_y + 183.0f,
					RESIST_SNOW_POCKET_SCALE)) *
			RESIST_SNOW_POCKET_STRENGTH;

		const float snowMound =
			ResistanceSnowMoundSignal(a_x, a_y) *
			RESIST_SNOW_MOUND_STRENGTH;

		return
			std::clamp(
				1.0f +
					snowFine +
					snowPocket +
					snowMound,
				0.70f,
				2.15f);
	}

	float ResistanceMudVariationScale(
		float a_x,
		float a_y,
		float a_mudActivation)
	{
		const float mudFine =
			(ResistanceDepthFineNoise(
				a_x + 71.0f,
				a_y + 309.0f,
				RESIST_MUD_FINE_SCALE) *
					2.0f -
				1.0f) *
			RESIST_MUD_FINE_VARIATION;

		const float mudPocket =
			ResistanceSmoothStep(
				0.58f,
				0.92f,
				ResistanceDepthValueNoise(
					a_x + 263.0f,
					a_y - 347.0f,
					RESIST_MUD_POCKET_SCALE)) *
			RESIST_MUD_POCKET_STRENGTH;

		return
			std::clamp(
				1.0f +
					(mudFine + mudPocket) *
						a_mudActivation,
				0.70f,
				2.15f);
	}

	bool ResistanceIsSnowMaterial(RE::MATERIAL_ID a_material)
	{
		return
			a_material == RE::MATERIAL_ID::kSnow ||
			a_material == RE::MATERIAL_ID::kSnowStairs;
	}

	std::string ResistanceNormalizeTextureKey(const char* a_path)
	{
		if (!a_path || a_path[0] == '\0')
			return {};

		std::string key;
		key.reserve(std::char_traits<char>::length(a_path));

		for (const char c : std::string_view(a_path)) {
			char out = c == '/' ? '\\' : c;
			if (out >= 'A' && out <= 'Z')
				out = static_cast<char>(out - 'A' + 'a');
			key.push_back(out);
		}

		auto stripPrefix = [&](std::string_view prefix) {
			if (key.size() >= prefix.size() &&
				key.compare(0, prefix.size(), prefix) == 0) {
				key.erase(0, prefix.size());
			}
		};

		stripPrefix("data\\");
		stripPrefix("textures\\");

		if (key.size() > 4 &&
			key.compare(key.size() - 4, 4, ".dds") == 0) {
			key.resize(key.size() - 4);
		}

		return key;
	}

	void ResistanceCacheRenderedSnowTexture(
		const char* a_path,
		float a_snowFlag)
	{
		const std::string key =
			ResistanceNormalizeTextureKey(a_path);
		if (key.empty())
			return;

		const float flag =
			std::isfinite(a_snowFlag)
				? std::clamp(a_snowFlag, 0.0f, 1.0f)
				: 0.0f;

		std::scoped_lock lock(
			g_resistanceSnowTextureCacheMutex);

		auto [it, inserted] =
			g_resistanceSnowTextureFlags.emplace(
				key,
				flag);
		if (!inserted) {
			// The same texture should normally carry one stable authored snow flag.
			// Prefer the strongest observed value so layered/replayed passes cannot
			// accidentally erase a previously proven snow classification.
			it->second =
				std::max(
					it->second,
					flag);
		}

		if (g_resistanceSnowTextureFlags.size() > 1024u) {
			g_resistanceSnowTextureFlags.clear();
		}
	}

	void ResistanceCacheRenderedSnowTextures(
		RE::BSLightingShaderMaterialLandscape* a_material,
		const std::array<float, 6>& a_flags)
	{
		if (!a_material)
			return;

		// Landscape has six logical layers: the inherited base diffuse texture
		// followed by five landscape diffuse textures.
		if (a_material->diffuseTexture) {
			ResistanceCacheRenderedSnowTexture(
				a_material->diffuseTexture->name.c_str(),
				a_flags[0]);
		}

		for (std::size_t i = 0; i < 5; ++i) {
			if (!a_material->landscapeDiffuseTexture[i])
				continue;

			ResistanceCacheRenderedSnowTexture(
				a_material->landscapeDiffuseTexture[i]->name.c_str(),
				a_flags[i + 1]);
		}
	}

	bool ResistanceRendererTextureIsSnow(
		const RE::TESLandTexture* a_landTexture)
	{
		if (!a_landTexture ||
			!a_landTexture->textureSet)
			return false;

		const char* diffusePath =
			a_landTexture->textureSet->textures[0].textureName.c_str();
		const std::string key =
			ResistanceNormalizeTextureKey(diffusePath);
		if (key.empty())
			return false;

		std::scoped_lock lock(
			g_resistanceSnowTextureCacheMutex);

		const auto it =
			g_resistanceSnowTextureFlags.find(key);
		return
			it != g_resistanceSnowTextureFlags.end() &&
			it->second > 0.50f;
	}

	bool ResistanceSnowTextureFallback(
		const RE::TESLandTexture* a_landTexture)
	{
		if (!a_landTexture)
			return false;

		std::string text;

		if (const char* editorID =
			a_landTexture->GetFormEditorID();
			editorID && editorID[0] != '\0') {
			text += editorID;
			text.push_back(' ');
		}

		if (a_landTexture->textureSet) {
			const char* diffusePath =
				a_landTexture->textureSet->textures[0].textureName.c_str();
			if (diffusePath)
				text += diffusePath;
		}

		for (char& c : text) {
			if (c >= 'A' && c <= 'Z')
				c = static_cast<char>(c - 'A' + 'a');
		}

		return
			text.find("snow") != std::string::npos ||
			text.find("snw") != std::string::npos ||
			text.find("glacier") != std::string::npos;
	}

	GroundSnowClassifier ResistanceClassifySnow(
		RE::MATERIAL_ID a_materialID,
		const RE::TESLandTexture* a_landTexture)
	{
		if (ResistanceIsSnowMaterial(a_materialID))
			return GroundSnowClassifier::kPhysicalMaterial;

		if (ResistanceRendererTextureIsSnow(a_landTexture))
			return GroundSnowClassifier::kRendererTextureFlag;

		// Last-resort compatibility for a land texture that has not yet appeared
		// in the renderer cache (for example immediately after loading a cell).
		// This never affects the render classifier; it is gameplay-only.
		if (ResistanceSnowTextureFallback(a_landTexture))
			return GroundSnowClassifier::kTextureNameFallback;

		return GroundSnowClassifier::kNone;
	}

	bool ResistanceLandTextureIsSnow(
		const RE::TESLandTexture* a_landTexture)
	{
		if (!a_landTexture)
			return false;

		if (a_landTexture->materialType &&
			ResistanceIsSnowMaterial(
				a_landTexture->materialType->materialID)) {
			return true;
		}

		return
			ResistanceRendererTextureIsSnow(a_landTexture) ||
			ResistanceSnowTextureFallback(a_landTexture);
	}

	float ResistanceLoadedLandVertexSnowCoverage(
		const RE::TESObjectLAND::LoadedLandData* a_loaded,
		std::uint32_t a_globalX,
		std::uint32_t a_globalY)
	{
		if (!a_loaded)
			return 0.0f;

		a_globalX = std::min(a_globalX, 32u);
		a_globalY = std::min(a_globalY, 32u);

		// Four 17x17 overlapping quadrants:
		//   0=SW, 1=SE, 2=NW, 3=NE.
		const std::uint32_t quadX =
			a_globalX > 16u ? 1u : 0u;
		const std::uint32_t quadY =
			a_globalY > 16u ? 1u : 0u;
		const std::uint32_t quadI =
			quadX + quadY * 2u;

		const std::uint32_t localX =
			a_globalX - quadX * 16u;
		const std::uint32_t localY =
			a_globalY - quadY * 16u;
		const std::uint32_t vertexI =
			localY * 17u + localX;

		float layerSum = 0.0f;
		float snowWeight = 0.0f;

		for (std::uint32_t layerI = 0; layerI < 6u; ++layerI) {
			// CommonLib declares this storage int8_t, but Skyrim uses the full
			// 0..255 byte range. Reinterpret through uint8_t exactly like the
			// established terrain-shell implementation.
			const float weight =
				static_cast<float>(
					static_cast<std::uint8_t>(
						a_loaded->percents[quadI][vertexI][layerI])) *
				(1.0f / 255.0f);

			layerSum += weight;

			if (ResistanceLandTextureIsSnow(
					a_loaded->quadTextures[quadI][layerI])) {
				snowWeight += weight;
			}
		}

		const float baseWeight =
			std::max(1.0f - layerSum, 0.0f);

		if (ResistanceLandTextureIsSnow(
				a_loaded->defQuadTextures[quadI])) {
			snowWeight += baseWeight;
		}

		return ResistanceSaturate(snowWeight);
	}

	bool ResistanceSampleLoadedLandSnowCoverage(
		RE::Actor* a_actor,
		const RE::NiPoint3& a_position,
		float& a_outCoverage)
	{
		a_outCoverage = 0.0f;

		if (!a_actor)
			return false;

		auto* cell = a_actor->GetParentCell();
		if (!cell || !cell->IsExteriorCell())
			return false;

		auto* coords = cell->GetCoordinates();
		if (!coords)
			return false;

		auto* land =
			cell->GetRuntimeData().cellLand;
		if (!land || !land->loadedData)
			return false;

		constexpr float cellSize = 4096.0f;
		constexpr float vertexSpacing = 128.0f;

		const float cellOriginX =
			static_cast<float>(coords->cellX) *
			cellSize;
		const float cellOriginY =
			static_cast<float>(coords->cellY) *
			cellSize;

		const float gridX =
			std::clamp(
				(a_position.x - cellOriginX) /
					vertexSpacing,
				0.0f,
				32.0f);
		const float gridY =
			std::clamp(
				(a_position.y - cellOriginY) /
					vertexSpacing,
				0.0f,
				32.0f);

		const std::uint32_t x0 =
			static_cast<std::uint32_t>(
				std::floor(gridX));
		const std::uint32_t y0 =
			static_cast<std::uint32_t>(
				std::floor(gridY));
		const std::uint32_t x1 =
			std::min(x0 + 1u, 32u);
		const std::uint32_t y1 =
			std::min(y0 + 1u, 32u);

		const float tx = gridX - std::floor(gridX);
		const float ty = gridY - std::floor(gridY);

		const float c00 =
			ResistanceLoadedLandVertexSnowCoverage(
				land->loadedData,
				x0,
				y0);
		const float c10 =
			ResistanceLoadedLandVertexSnowCoverage(
				land->loadedData,
				x1,
				y0);
		const float c01 =
			ResistanceLoadedLandVertexSnowCoverage(
				land->loadedData,
				x0,
				y1);
		const float c11 =
			ResistanceLoadedLandVertexSnowCoverage(
				land->loadedData,
				x1,
				y1);

		a_outCoverage =
			ResistanceSaturate(
				ResistanceLerp(
					ResistanceLerp(c00, c10, tx),
					ResistanceLerp(c01, c11, tx),
					ty));

		return true;
	}

	float ResistanceSnowActivationFromCoverage(
		float a_coverage,
		const GroundResponse& a_ground)
	{
		const float threshold =
			ResistanceSaturate(
				a_ground.settings.SnowCoverageThreshold);
		const float feather =
			std::max(
				a_ground.settings.SnowCoverageFeather,
				1.0e-3f);

		return
			ResistanceSmoothStep(
				threshold,
				std::min(
					threshold + feather,
					1.0f),
				ResistanceSaturate(a_coverage));
	}

	float ResistanceMudWetnessActivation(
		const GroundResponse& a_ground)
	{
		if (!a_ground.settings.EnableMudDeformation)
			return 0.0f;

		if (!a_ground.settings.MudRequiresWetness)
			return 1.0f;

		const auto rainData =
			globals::pipeline::rainResponse.GetCommonBufferData();

		const float threshold =
			ResistanceSaturate(
				a_ground.settings.MudWetnessThreshold);
		const float weatherSignal =
			ResistanceSaturate(
				std::max(
					rainData.Raining,
					rainData.Wetness));

		return
			ResistanceSmoothStep(
				std::max(
					threshold - 0.22f,
					0.0f),
				std::min(
					threshold + 0.18f,
					1.0f),
				weatherSignal);
	}

	float ResistanceTerrainSlopeMask(
		RE::TES* a_tes,
		const RE::NiPoint3& a_position,
		const GroundResponse& a_ground)
	{
		if (!a_tes)
			return 1.0f;

		RE::NiPoint3 left{
			a_position.x - RESIST_SLOPE_SAMPLE_STEP,
			a_position.y,
			a_position.z
		};
		RE::NiPoint3 right{
			a_position.x + RESIST_SLOPE_SAMPLE_STEP,
			a_position.y,
			a_position.z
		};
		RE::NiPoint3 down{
			a_position.x,
			a_position.y - RESIST_SLOPE_SAMPLE_STEP,
			a_position.z
		};
		RE::NiPoint3 up{
			a_position.x,
			a_position.y + RESIST_SLOPE_SAMPLE_STEP,
			a_position.z
		};

		float hLeft = 0.0f;
		float hRight = 0.0f;
		float hDown = 0.0f;
		float hUp = 0.0f;

		if (!a_tes->GetLandHeight(left, hLeft) ||
			!a_tes->GetLandHeight(right, hRight) ||
			!a_tes->GetLandHeight(down, hDown) ||
			!a_tes->GetLandHeight(up, hUp)) {
			return 1.0f;
		}

		const float invSpan =
			1.0f /
			std::max(
				2.0f * RESIST_SLOPE_SAMPLE_STEP,
				1.0f);
		const float dzdx =
			(hRight - hLeft) * invSpan;
		const float dzdy =
			(hUp - hDown) * invSpan;

		const float normalZ =
			1.0f /
			std::sqrt(
				1.0f +
				dzdx * dzdx +
				dzdy * dzdy);

		const float minimumSlopeZ =
			std::clamp(
				a_ground.settings.GeometryMinimumSlopeZ,
				0.20f,
				0.90f);

		return
			ResistanceSmoothStep(
				minimumSlopeZ,
				std::min(
					minimumSlopeZ + 0.14f,
					1.0f),
				ResistanceSaturate(normalZ));
	}

	float ResistanceSnowPristineDepth(
		const RE::NiPoint3& a_position,
		float a_slopeMask,
		float a_snowActivation,
		const GroundResponse& a_ground)
	{
		const float snowActivation =
			ResistanceSaturate(a_snowActivation);

		const float baseThickness =
			std::clamp(
				a_ground.settings.SnowSurfaceThickness,
				2.0f,
				24.0f) *
			snowActivation *
			a_slopeMask;

		float variedThickness =
			baseThickness *
			ResistanceSnowVariationScale(
				a_position.x,
				a_position.y);

		const float deepSignal =
			ResistanceSnowDeepDriftSignal(
				a_position.x,
				a_position.y);

		const float deepTargetHeight =
			RESIST_SNOW_MAX_PRISTINE_HEIGHT *
			snowActivation *
			a_slopeMask;

		const float availableDeepRaise =
			std::max(
				deepTargetHeight -
					variedThickness,
				0.0f);

		variedThickness +=
			availableDeepRaise *
			deepSignal;

		return
			std::max(
				variedThickness,
				0.0f);
	}

	float ResistanceMudPristineDepth(
		const RE::NiPoint3& a_position,
		float a_slopeMask,
		float a_mudActivation,
		const GroundResponse& a_ground)
	{
		const float mudThickness =
			std::clamp(
				std::max(
					a_ground.settings.SnowSurfaceThickness *
						0.30f,
					a_ground.settings.MudMaximumDepth *
						0.20f),
				1.5f,
				5.0f);

		const float baseThickness =
			mudThickness *
			a_mudActivation *
			a_slopeMask;

		return
			std::max(
				baseThickness *
					ResistanceMudVariationScale(
						a_position.x,
						a_position.y,
						a_mudActivation),
				0.0f);
	}

	// PIXL_GR_13AL_SURFACE_CLASSIFIER_V1
	// -1.0 in the existing b13 terrain snow floats means HARD / excluded.
	bool GroundTerrainKeyContains(
		const std::string& a_key,
		const char* a_token)
	{
		return
			a_token &&
			a_token[0] != '\0' &&
			a_key.find(a_token) != std::string::npos;
	}

	bool GroundTerrainTextureIsHard(
		const std::string& a_key)
	{
		if (a_key.empty())
			return false;

		const bool structuralHard =
			GroundTerrainKeyContains(a_key, "cliff") ||
			GroundTerrainKeyContains(a_key, "boulder") ||
			GroundTerrainKeyContains(a_key, "bedrock") ||
			GroundTerrainKeyContains(a_key, "mountain") ||
			GroundTerrainKeyContains(a_key, "ledge") ||
			GroundTerrainKeyContains(a_key, "crag") ||
			GroundTerrainKeyContains(a_key, "cavewall") ||
			GroundTerrainKeyContains(a_key, "cave_wall") ||
			GroundTerrainKeyContains(a_key, "rockface") ||
			GroundTerrainKeyContains(a_key, "rock_face");

		if (structuralHard)
			return true;

		const bool genericHard =
			GroundTerrainKeyContains(a_key, "rock") ||
			GroundTerrainKeyContains(a_key, "stone") ||
			GroundTerrainKeyContains(a_key, "granite") ||
			GroundTerrainKeyContains(a_key, "slate") ||
			GroundTerrainKeyContains(a_key, "marble") ||
			GroundTerrainKeyContains(a_key, "ore") ||
			GroundTerrainKeyContains(a_key, "glacier") ||
			GroundTerrainKeyContains(a_key, "ice");

		if (!genericHard)
			return false;

		const bool softOverride =
			GroundTerrainKeyContains(a_key, "snow") ||
			GroundTerrainKeyContains(a_key, "snw") ||
			GroundTerrainKeyContains(a_key, "dirt") ||
			GroundTerrainKeyContains(a_key, "mud") ||
			GroundTerrainKeyContains(a_key, "soil") ||
			GroundTerrainKeyContains(a_key, "earth") ||
			GroundTerrainKeyContains(a_key, "sand") ||
			GroundTerrainKeyContains(a_key, "gravel") ||
			GroundTerrainKeyContains(a_key, "pebble") ||
			GroundTerrainKeyContains(a_key, "tundra") ||
			GroundTerrainKeyContains(a_key, "moss") ||
			GroundTerrainKeyContains(a_key, "grass");

		return !softOverride;
	}

	bool GroundLandTextureIsHard(
		const RE::TESLandTexture* a_landTexture)
	{
		if (!a_landTexture ||
			!a_landTexture->textureSet)
			return false;

		const char* diffusePath =
			a_landTexture->textureSet->textures[0].textureName.c_str();

		return
			GroundTerrainTextureIsHard(
				ResistanceNormalizeTextureKey(diffusePath));
	}

	float GroundEncodeTerrainSurfaceClass(
		const char* a_path,
		float a_authoredSnow,
		std::size_t a_slot,
		const char* a_source)
	{
		const float authoredSnow =
			std::isfinite(a_authoredSnow)
				? std::clamp(a_authoredSnow, 0.0f, 1.0f)
				: 0.0f;

		const std::string key =
			ResistanceNormalizeTextureKey(a_path);

		const bool hard =
			GroundTerrainTextureIsHard(key);

		const float encoded =
			hard ? -1.0f : authoredSnow;

		if (g_groundResistanceSettings.DebugMovementResistance && !key.empty()) {
			static std::mutex s_groundTerrainAuditMutex;
			static std::unordered_map<std::string, std::uint8_t>
				s_groundTerrainAuditSeen;

			std::scoped_lock auditLock(s_groundTerrainAuditMutex);

			if (s_groundTerrainAuditSeen.size() < 512u &&
				s_groundTerrainAuditSeen.emplace(key, 1u).second) {
				const char* finalClass =
					hard
						? "HARD_EXCLUDED"
						: authoredSnow > 0.50f
							? "SNOW"
							: "SOFT";

				logger::info(
					"[GR-MATERIAL] source={} slot={} authoredSnow={:.2f} class={} path={}",
					a_source ? a_source : "Unknown",
					a_slot,
					authoredSnow,
					finalClass,
					key);
			}
		}

		return encoded;
	}

	const char* GroundVanillaLandscapeDiffusePath(
		const RE::BSLightingShaderMaterialLandscape* a_material,
		std::size_t a_slot)
	{
		if (!a_material)
			return nullptr;

		if (a_slot == 0u) {
			return
				a_material->diffuseTexture
					? a_material->diffuseTexture->name.c_str()
					: nullptr;
		}

		const std::size_t landscapeIndex = a_slot - 1u;
		if (landscapeIndex >= 5u ||
			!a_material->landscapeDiffuseTexture[landscapeIndex])
			return nullptr;

		return
			a_material->landscapeDiffuseTexture[landscapeIndex]->name.c_str();
	}

	const char* GroundMaterialForgeLandscapeDiffusePath(
		const BSLightingShaderMaterialPBRLandscape* a_material,
		std::size_t a_slot)
	{
		if (!a_material ||
			a_slot >= BSLightingShaderMaterialPBRLandscape::NumTiles ||
			!a_material->landscapeBaseColorTextures[a_slot])
			return nullptr;

		return
			a_material->landscapeBaseColorTextures[a_slot]->name.c_str();
	}

GroundResistanceSample ResistanceEvaluateActor(
		RE::Actor* a_actor,
		const GroundResponse& a_ground)
	{
		GroundResistanceSample result{};

		if (!a_actor ||
			!a_actor->Is3DLoaded() ||
			a_actor->IsInMidair() ||
			!a_ground.settings.EnableDeformableGround ||
			!a_ground.settings.EnableGeometricSnow) {
			return result;
		}

		auto* tes = RE::TES::GetSingleton();
		if (!tes)
			return result;

		const RE::NiPoint3 actorPosition =
			a_actor->GetPosition();

		float landHeight = actorPosition.z;
		if (!tes->GetLandHeight(
				actorPosition,
				landHeight) ||
			!std::isfinite(landHeight) ||
			std::abs(actorPosition.z - landHeight) >
				RESIST_LAND_Z_TOLERANCE) {
			return result;
		}

		auto* landTexture =
			tes->GetLandTexture(actorPosition);
		if (!landTexture)
			return result;

		// PIXL_GR_13AL_ACTOR_HARD_EXCLUSION_V1
		if (GroundLandTextureIsHard(landTexture))
			return result;

		RE::MATERIAL_ID materialID =
			tes->GetLandMaterialType(actorPosition);

		if (landTexture->materialType &&
			landTexture->materialType->materialID !=
				RE::MATERIAL_ID::kNone) {
			materialID =
				landTexture->materialType->materialID;
		}

		result.materialID =
			static_cast<std::uint32_t>(materialID);

		const float slopeMask =
			ResistanceTerrainSlopeMask(
				tes,
				actorPosition,
				a_ground);
		result.slopeMask = slopeMask;

		const float minimumSpeedScale =
			std::clamp(
				g_groundResistanceSettings.MinimumSurfaceSpeedScale,
				0.20f,
				1.0f);

		float loadedSnowCoverage = 0.0f;
		const bool loadedSnowCoverageValid =
			ResistanceSampleLoadedLandSnowCoverage(
				a_actor,
				actorPosition,
				loadedSnowCoverage);

		const float loadedSnowActivation =
			loadedSnowCoverageValid
				? ResistanceSnowActivationFromCoverage(
					loadedSnowCoverage,
					a_ground)
				: 0.0f;

		GroundSnowClassifier snowClassifier =
			GroundSnowClassifier::kNone;
		float snowActivation = 0.0f;

		if (loadedSnowCoverageValid &&
			loadedSnowActivation > 1.0e-4f) {
			snowClassifier =
				GroundSnowClassifier::kLandscapeBlend;
			snowActivation =
				loadedSnowActivation;
		} else {
			snowClassifier =
				ResistanceClassifySnow(
					materialID,
					landTexture);
			snowActivation =
				snowClassifier != GroundSnowClassifier::kNone
					? 1.0f
					: 0.0f;
		}

		result.snowCoverage =
			loadedSnowCoverage;
		result.snowActivation =
			snowActivation;

		if (a_ground.settings.EnableSnowDeformation &&
			snowClassifier != GroundSnowClassifier::kNone &&
			snowActivation > 1.0e-4f) {
			result.surface =
				GroundResistanceSurface::kSnow;
			result.snowClassifier =
				snowClassifier;
			result.pristineDepth =
				ResistanceSnowPristineDepth(
					actorPosition,
					slopeMask,
					snowActivation,
					a_ground);

			const float startDepth =
				std::clamp(
					g_groundResistanceSettings.SnowResistanceStartDepth,
					0.0f,
					64.0f);
			const float fullDepth =
				std::clamp(
					g_groundResistanceSettings.SnowFullResistanceDepth,
					startDepth + 1.0f,
					72.0f);

			// Fresh snow is fluffy even when shallow. The old single smoothstep
			// barely slowed a 8-15 unit blanket, so actors could sprint through
			// visually soft snow. Build a two-stage response:
			//
			//   shallow/fluffy: quickly reaches a modest resistance plateau
			//   deep/wading:    progressively adds the remaining resistance
			//
			// Combining them multiplicatively keeps the curve smooth while
			// guaranteeing useful slowdown well before knee depth.
			const float fluffyFullDepth =
				std::min(
					std::max(startDepth + 11.0f, 12.0f),
					fullDepth);

			const float fluffySignal =
				ResistanceSmoothStep(
					startDepth,
					fluffyFullDepth,
					result.pristineDepth);

			const float fluffyStrength =
				ResistanceSaturate(
					g_groundResistanceSettings.FluffySnowResistanceStrength);

			const float fluffyResistance =
				fluffySignal *
				fluffyStrength;

			const float deepSignalLinear =
				ResistanceSmoothStep(
					std::max(startDepth + 7.0f, 8.0f),
					fullDepth,
					result.pristineDepth);

			// Slightly front-load the deep curve so thigh/waist snow becomes
			// obviously restrictive before reaching the absolute 72-unit peak.
			const float deepResistance =
				std::pow(
					ResistanceSaturate(deepSignalLinear),
					0.75f);

			const float combinedDepthResistance =
				1.0f -
				(1.0f - fluffyResistance) *
				(1.0f - deepResistance);

			const float resistanceStrength =
				ResistanceSaturate(
					g_groundResistanceSettings.SnowResistanceStrength);

			const float resistanceAmount =
				ResistanceSaturate(
					combinedDepthResistance *
					resistanceStrength);

			result.targetSpeedScale =
				1.0f -
				(1.0f - minimumSpeedScale) *
					resistanceAmount;
			return result;
		}

		const float mudActivation =
			ResistanceMudWetnessActivation(a_ground);
		result.wetnessActivation =
			mudActivation;

		if (a_ground.settings.EnableMudDeformation &&
			mudActivation > 1.0e-4f) {
			result.surface =
				GroundResistanceSurface::kMud;
			result.pristineDepth =
				ResistanceMudPristineDepth(
					actorPosition,
					slopeMask,
					mudActivation,
					a_ground);

			const float mudDepthFraction =
				ResistanceSaturate(
					result.pristineDepth / 5.0f);

			const float mudDepthResistance =
				std::pow(
					mudDepthFraction,
					0.80f);
			const float resistanceAmount =
				ResistanceSaturate(
					mudDepthResistance *
					g_groundResistanceSettings.MudResistanceStrength);

			const float mudMinimumSpeedScale =
				std::clamp(
					g_groundResistanceSettings.MudMinimumSpeedScale,
					0.30f,
					0.95f);

			const float mudTargetScale =
				1.0f -
				(1.0f - mudMinimumSpeedScale) *
					resistanceAmount;

			result.targetSpeedScale =
				std::max(
					mudTargetScale,
					minimumSpeedScale);
		}

		return result;
	}

	template <class T>
	RE::ActorValueOwner* ResistanceActorValueOwner(T* a_actor)
	{
		if (!a_actor)
			return nullptr;

		if constexpr (requires(T* value) {
			value->AsActorValueOwner();
		}) {
			return a_actor->AsActorValueOwner();
		} else {
			return static_cast<RE::ActorValueOwner*>(a_actor);
		}
	}

	GroundMovementResistanceTrack* ResistanceFindTrack(
		std::uint32_t a_actorID)
	{
		for (auto& track : g_groundResistanceTracks) {
			if (track.actorID == a_actorID)
				return &track;
		}
		return nullptr;
	}

	GroundMovementResistanceTrack& ResistanceGetOrCreateTrack(
		RE::Actor* a_actor)
	{
		const std::uint32_t actorID =
			a_actor->GetFormID();

		if (auto* existing =
			ResistanceFindTrack(actorID)) {
			existing->handle =
				a_actor->GetHandle();
			return *existing;
		}

		GroundMovementResistanceTrack track{};
		track.handle = a_actor->GetHandle();
		track.actorID = actorID;
		g_groundResistanceTracks.push_back(track);
		return g_groundResistanceTracks.back();
	}

	bool ResistanceRestoreTrack(
		GroundMovementResistanceTrack& a_track)
	{
		auto actor = a_track.handle.get();
		if (!actor)
			return false;

		auto* actorValues =
			ResistanceActorValueOwner(actor.get());
		if (!actorValues)
			return false;

		if (std::abs(a_track.carryWeightRefreshAmount) >
			1.0e-4f) {
			actorValues->ModActorValue(
				RE::ACTOR_VALUE_MODIFIER::kTemporary,
				RE::ActorValue::kCarryWeight,
				-a_track.carryWeightRefreshAmount);
			a_track.carryWeightRefreshAmount = 0.0f;
			a_track.carryWeightRefreshTimer = 0.0f;
		}

		if (std::abs(a_track.appliedSpeedDelta) >
			1.0e-4f) {
			actorValues->ModActorValue(
				RE::ACTOR_VALUE_MODIFIER::kTemporary,
				RE::ActorValue::kSpeedMult,
				-a_track.appliedSpeedDelta);

			// SpeedMult is cached by Skyrim's movement controller. Nudge a
			// different movement-relevant AV to force the cached speed to refresh
			// immediately when resistance is removed.
			actorValues->ModActorValue(
				RE::ACTOR_VALUE_MODIFIER::kTemporary,
				RE::ActorValue::kCarryWeight,
				0.1f);
			actorValues->ModActorValue(
				RE::ACTOR_VALUE_MODIFIER::kTemporary,
				RE::ActorValue::kCarryWeight,
				-0.1f);
		}

		a_track.appliedSpeedDelta = 0.0f;
		a_track.filteredSpeedScale = 1.0f;
		a_track.lastRefreshedSpeedDelta = 0.0f;
		return true;
	}

	void ResistanceRestoreAll()
	{
		for (auto& track :
			g_groundResistanceTracks) {
			ResistanceRestoreTrack(track);
		}

		// Keep only unresolved handles so a later frame can still remove our
		// temporary modifier if that actor becomes available again.
		for (auto it =
				g_groundResistanceTracks.begin();
			 it != g_groundResistanceTracks.end();) {
			if (std::abs(it->appliedSpeedDelta) <=
				1.0e-4f) {
				it =
					g_groundResistanceTracks.erase(it);
			} else {
				++it;
			}
		}
	}

	void ResistanceApplyTrack(
		RE::Actor* a_actor,
		GroundMovementResistanceTrack& a_track,
		float a_targetScale,
		float a_elapsed)
	{
		auto* actorValues =
			ResistanceActorValueOwner(a_actor);
		if (!actorValues)
			return;

		// Complete the delayed half of the SpeedMult refresh pulse. A short
		// ~0.1 s CarryWeight delta mirrors the established Skyrim workaround
		// used by movement-speed mods to make SpeedMult changes take effect.
		if (std::abs(a_track.carryWeightRefreshAmount) >
			1.0e-4f) {
			a_track.carryWeightRefreshTimer -=
				std::max(a_elapsed, 0.0f);

			if (a_track.carryWeightRefreshTimer <= 0.0f) {
				actorValues->ModActorValue(
					RE::ACTOR_VALUE_MODIFIER::kTemporary,
					RE::ActorValue::kCarryWeight,
					-a_track.carryWeightRefreshAmount);
				a_track.carryWeightRefreshAmount = 0.0f;
				a_track.carryWeightRefreshTimer = 0.0f;
				a_track.lastRefreshedSpeedDelta =
					a_track.appliedSpeedDelta;
			}
		}

		const float responseRate =
			std::clamp(
				g_groundResistanceSettings.ResistanceResponseRate,
				1.0f,
				12.0f);
		const float alpha =
			1.0f -
			std::exp(
				-responseRate *
				std::max(a_elapsed, 0.0f));

		a_track.filteredSpeedScale +=
			(a_targetScale -
				a_track.filteredSpeedScale) *
			ResistanceSaturate(alpha);
		a_track.filteredSpeedScale =
			std::clamp(
				a_track.filteredSpeedScale,
				0.20f,
				1.0f);

		const float observedSpeedMult =
			actorValues->GetActorValue(
				RE::ActorValue::kSpeedMult);

		const float baselineSpeedMult =
			observedSpeedMult -
			a_track.appliedSpeedDelta;

		if (!std::isfinite(baselineSpeedMult) ||
			baselineSpeedMult <= 1.0e-3f) {
			ResistanceRestoreTrack(a_track);
			return;
		}

		const float desiredDelta =
			baselineSpeedMult *
			(a_track.filteredSpeedScale - 1.0f);

		const float deltaChange =
			desiredDelta -
			a_track.appliedSpeedDelta;

		if (std::abs(deltaChange) > 1.0e-4f) {
			// Use the TEMPORARY modifier channel and track only our exact
			// additive contribution. Other mods changing SpeedMult while PIXL
			// resistance is active remain part of baselineSpeedMult.
			actorValues->ModActorValue(
				RE::ACTOR_VALUE_MODIFIER::kTemporary,
				RE::ActorValue::kSpeedMult,
				deltaChange);
			a_track.appliedSpeedDelta =
				desiredDelta;
		}

		// Do not keep CarryWeight permanently modified. Start a tiny delayed
		// pulse only when SpeedMult has moved far enough since the last movement
		// controller refresh to matter perceptibly.
		if (std::abs(a_track.carryWeightRefreshAmount) <=
				1.0e-4f &&
			std::abs(
				a_track.appliedSpeedDelta -
				a_track.lastRefreshedSpeedDelta) >=
				0.75f) {
			constexpr float kRefreshAmount = 0.1f;
			actorValues->ModActorValue(
				RE::ACTOR_VALUE_MODIFIER::kTemporary,
				RE::ActorValue::kCarryWeight,
				kRefreshAmount);
			a_track.carryWeightRefreshAmount =
				kRefreshAmount;
			a_track.carryWeightRefreshTimer =
				0.10f;
		}
	}

	const char* ResistanceSurfaceName(
		GroundResistanceSurface a_surface)
	{
		switch (a_surface) {
		case GroundResistanceSurface::kSnow:
			return "Snow";
		case GroundResistanceSurface::kMud:
			return "Mud";
		default:
			return "None";
		}
	}

	const char* ResistanceSnowClassifierName(
		GroundSnowClassifier a_classifier)
	{
		switch (a_classifier) {
		case GroundSnowClassifier::kLandscapeBlend:
			return "LandscapeBlend";
		case GroundSnowClassifier::kPhysicalMaterial:
			return "PhysicalMaterial";
		case GroundSnowClassifier::kRendererTextureFlag:
			return "RendererTextureFlag";
		case GroundSnowClassifier::kTextureNameFallback:
			return "TextureFallback";
		default:
			return "None";
		}
	}

	void UpdateGroundMovementResistance()
	{
		auto& ground =
			globals::pipeline::groundResponse;

		if (!g_groundResistanceSettings.EnableMovementResistance ||
			!ground.settings.EnableDeformableGround ||
			!ground.settings.EnableGeometricSnow ||
			(!g_groundResistanceSettings.EnablePlayerResistance &&
			 !g_groundResistanceSettings.EnableNPCResistance)) {
			g_groundResistanceAccumulator = 0.0f;
			ResistanceRestoreAll();
			g_lastPlayerResistanceSurface =
				GroundResistanceSurface::kNone;
			g_lastPlayerResistanceDepth = 0.0f;
			g_lastPlayerResistanceTargetScale = 1.0f;
			g_lastPlayerResistanceFilteredScale = 1.0f;
			g_lastPlayerSnowClassifier =
				GroundSnowClassifier::kNone;
			g_lastPlayerObservedSpeedMult = 0.0f;
			g_lastPlayerAppliedSpeedDelta = 0.0f;
			g_lastPlayerSnowCoverage = 0.0f;
			g_lastPlayerSnowActivation = 0.0f;
			return;
		}

		float frameDelta =
			globals::game::deltaTime
				? *globals::game::deltaTime
				: 0.0f;
		if (!std::isfinite(frameDelta) ||
			frameDelta < 0.0f) {
			frameDelta = 0.0f;
		}
		frameDelta =
			std::min(
				frameDelta,
				MAX_GROUND_FRAME_DELTA);

		if (globals::game::ui &&
			globals::game::ui->GameIsPaused()) {
			frameDelta = 0.0f;
		}

		g_groundResistanceAccumulator +=
			frameDelta;

		const float updateInterval =
			std::clamp(
				g_groundResistanceSettings.ResistanceUpdateInterval,
				0.03f,
				0.25f);

		if (g_groundResistanceAccumulator <
			updateInterval) {
			return;
		}

		const float elapsed =
			std::min(
				g_groundResistanceAccumulator,
				0.25f);
		g_groundResistanceAccumulator = 0.0f;

		auto* player =
			RE::PlayerCharacter::GetSingleton();
		if (!player) {
			ResistanceRestoreAll();
			return;
		}

		const RE::NiPoint3 playerPosition =
			player->GetPosition();
		++g_groundResistanceGeneration;
		if (g_groundResistanceGeneration == 0u)
			++g_groundResistanceGeneration;

		auto processActor =
			[&](RE::Actor* a_actor, bool a_isPlayer) {
				if (!a_actor ||
					!a_actor->Is3DLoaded()) {
					return;
				}

				if ((a_isPlayer &&
					 !g_groundResistanceSettings.EnablePlayerResistance) ||
					(!a_isPlayer &&
					 !g_groundResistanceSettings.EnableNPCResistance)) {
					if (auto* track =
						ResistanceFindTrack(
							a_actor->GetFormID())) {
						track->lastSeenGeneration =
							g_groundResistanceGeneration;
						ResistanceRestoreTrack(*track);
					}
					return;
				}

				auto& track =
					ResistanceGetOrCreateTrack(a_actor);

				if (track.lastSeenGeneration ==
					g_groundResistanceGeneration) {
					return;
				}

				track.lastSeenGeneration =
					g_groundResistanceGeneration;

				const GroundResistanceSample sample =
					ResistanceEvaluateActor(
						a_actor,
						ground);

				ResistanceApplyTrack(
					a_actor,
					track,
					std::clamp(
						sample.targetSpeedScale,
						0.20f,
						1.0f),
					elapsed);

				if (a_isPlayer) {
					g_lastPlayerResistanceSurface =
						sample.surface;
					g_lastPlayerResistanceDepth =
						sample.pristineDepth;
					g_lastPlayerResistanceTargetScale =
						sample.targetSpeedScale;
					g_lastPlayerResistanceFilteredScale =
						track.filteredSpeedScale;
					g_lastPlayerResistanceMaterial =
						sample.materialID;
					g_lastPlayerSnowClassifier =
						sample.snowClassifier;
					g_lastPlayerAppliedSpeedDelta =
						track.appliedSpeedDelta;
					g_lastPlayerSnowCoverage =
						sample.snowCoverage;
					g_lastPlayerSnowActivation =
						sample.snowActivation;

					if (auto* actorValues =
						ResistanceActorValueOwner(a_actor)) {
						g_lastPlayerObservedSpeedMult =
							actorValues->GetActorValue(
								RE::ActorValue::kSpeedMult);
					}

					if (g_groundResistanceSettings.DebugMovementResistance &&
						g_groundResistanceDiagCount < 180u) {
						logger::info(
							"[GR-RESIST] player surface={} classifier={} material={} coverage={:.3f} activation={:.3f} depth={:.2f} slope={:.3f} wet={:.3f} targetScale={:.3f} filteredScale={:.3f} appliedSpeedDelta={:.3f}",
							ResistanceSurfaceName(sample.surface),
							ResistanceSnowClassifierName(sample.snowClassifier),
							sample.materialID,
							sample.snowCoverage,
							sample.snowActivation,
							sample.pristineDepth,
							sample.slopeMask,
							sample.wetnessActivation,
							sample.targetSpeedScale,
							track.filteredSpeedScale,
							track.appliedSpeedDelta);
						++g_groundResistanceDiagCount;
					}
				}
			};

		// Player is explicit because highActorHandles is not a useful contract
		// for local-player presence in every camera/game state.
		processActor(player, true);

		if (g_groundResistanceSettings.EnableNPCResistance) {
			if (const auto* processLists =
				RE::ProcessLists::GetSingleton();
				processLists) {
				for (const auto& actorHandle :
					processLists->highActorHandles) {
					auto actor = actorHandle.get();
					if (!actor)
						continue;

					if (actor->GetFormID() ==
						player->GetFormID()) {
						continue;
					}

					if (playerPosition.GetSquaredDistance(
							actor->GetPosition()) >
						MAX_ACTOR_SQ_DISTANCE) {
						continue;
					}

					processActor(
						actor.get(),
						false);
				}
			}
		}

		// Anything no longer in the active scan immediately gets its exact PIXL
		// delta removed. This prevents resistance from sticking to an NPC after
		// it leaves the loaded/high-process area.
		for (auto& track :
			g_groundResistanceTracks) {
			if (track.lastSeenGeneration !=
				g_groundResistanceGeneration) {
				ResistanceRestoreTrack(track);
			}
		}

		for (auto it =
				g_groundResistanceTracks.begin();
			 it != g_groundResistanceTracks.end();) {
			const bool inactive =
				std::abs(it->appliedSpeedDelta) <=
					1.0e-3f &&
				std::abs(
					it->filteredSpeedScale -
					1.0f) <= 1.0e-3f;

			if (inactive &&
				it->lastSeenGeneration !=
					g_groundResistanceGeneration) {
				it =
					g_groundResistanceTracks.erase(it);
			} else {
				++it;
			}
		}
	}

	// -------------------------------------------------------------------------
	// GroundResponse 3.1 generic interaction producer
	// -------------------------------------------------------------------------

	enum class GroundElementKind : std::uint8_t
	{
		kNone,
		kFire,
		kFrost,
		kShock
	};

	const char* GroundElementLabel(GroundElementKind a_element)
	{
		switch (a_element) {
		case GroundElementKind::kFire:
			return "fire";
		case GroundElementKind::kFrost:
			return "frost";
		case GroundElementKind::kShock:
			return "shock";
		default:
			return "none";
		}
	}

	struct GroundSurfaceProbe
	{
		bool valid = false;
		bool snow = false;
		bool mud = false;
		float landHeight = 0.0f;
		float surfaceTop = 0.0f;
		float pristineDepth = 0.0f;
		float snowActivation = 0.0f;
		float mudActivation = 0.0f;
		float slopeMask = 0.0f;
	};

	enum class GroundInteractionSource : std::uint8_t
	{
		kUnknown,
		kObject,
		kProjectile,
		kMagic,
		kShout
	};

	struct GroundPendingInteraction
	{
		float2 start{};
		float2 end{};
		float startRadius = 0.0f;
		float endRadius = 0.0f;
		float strength = 0.0f;
		float displacementScale = 1.0f;
		float elementalDelta = 0.0f;
		float smoothing = 0.0f;
		std::uint32_t flags = 0u;
		float contactDepth = 0.0f;
		float priority = 0.0f;
		float receiverZ = 0.0f;
		bool validateReceiver = false;
		bool elementalSnowOnly = false;
		GroundInteractionSource source = GroundInteractionSource::kUnknown;
		RE::FormID sourceFormID = 0u;
	};

	struct GroundPreviousBodyState
	{
		RE::NiPoint3 center{};
		std::uint32_t generation = 0u;
	};

	struct GroundShoutSpellInfo
	{
		std::uint8_t tier = 1u;
		GroundElementKind element = GroundElementKind::kNone;
		float range = 0.0f;
	};

	static std::mutex g_groundInteractionMutex;
	static std::vector<GroundPendingInteraction> g_pendingGroundInteractions;
	static std::unordered_map<std::uint64_t, GroundPreviousBodyState> g_previousBodyStates;
	static std::unordered_map<std::uint64_t, GroundPreviousBodyState> g_previousObjectStates;
	static std::uint32_t g_bodyStateGeneration = 0u;
	static std::uint32_t g_objectStateGeneration = 0u;
	static std::unordered_map<RE::FormID, GroundShoutSpellInfo> g_shoutSpellCache;
	static std::unordered_set<RE::FormID> g_alwaysCompressForms;
	static std::unordered_set<RE::FormID> g_neverDeformForms;

	// PIXL_GR_13BH_PROJECTILE_PROCESS_FALLBACK_V1
	// Some Skyrim projectile classes do not reliably reach a concrete-class
	// AddImpact vtable hook in this renderer/plugin stack. ProcessImpacts owns the
	// authoritative ImpactData list immediately before vanilla consumes it, so 13BH
	// also harvests those records and de-duplicates them by projectile/impact identity.
	static std::mutex g_projectileImpactSeenMutex;
	static std::unordered_set<std::uint64_t> g_projectileImpactSeen;

	std::uint64_t GroundBodyKey(std::uint32_t a_ownerID, std::uintptr_t a_identity)
	{
		const std::uint64_t mixedIdentity =
			(static_cast<std::uint64_t>(a_identity) >> 4u) *
			0x9E3779B185EBCA87ull;
		return
			(static_cast<std::uint64_t>(a_ownerID) << 32u) ^
			mixedIdentity;
	}

	bool GroundFinitePoint(const RE::NiPoint3& a_point)
	{
		return
			std::isfinite(a_point.x) &&
			std::isfinite(a_point.y) &&
			std::isfinite(a_point.z);
	}


	std::uint64_t GroundProjectileImpactKey(
		const RE::Projectile* a_projectile,
		const RE::Projectile::ImpactData* a_impact)
	{
		const auto projectileBits =
			static_cast<std::uint64_t>(
				reinterpret_cast<std::uintptr_t>(a_projectile) >> 4u);
		const auto impactBits =
			static_cast<std::uint64_t>(
				reinterpret_cast<std::uintptr_t>(a_impact) >> 4u);
		return
			projectileBits * 0x9E3779B185EBCA87ull ^
			(impactBits + 0xD1B54A32D192ED03ull);
	}

	bool GroundClaimProjectileImpact(
		const RE::Projectile* a_projectile,
		const RE::Projectile::ImpactData* a_impact)
	{
		if (!a_projectile || !a_impact)
			return false;

		const std::uint64_t key =
			GroundProjectileImpactKey(a_projectile, a_impact);
		std::scoped_lock lock(g_projectileImpactSeenMutex);
		if (g_projectileImpactSeen.size() > 16384u)
			g_projectileImpactSeen.clear();
		return g_projectileImpactSeen.insert(key).second;
	}

	std::uint32_t GroundQueueRuntimeProjectileImpacts(
		RE::Projectile* a_projectile,
		const char* a_source)
	{
		if (!a_projectile)
			return 0u;

		auto& runtime = a_projectile->GetProjectileRuntimeData();
		static std::atomic<std::uint32_t> s_processEntryDiagCount{ 0u };
		const auto entryDiag =
			s_processEntryDiagCount.fetch_add(1u, std::memory_order_relaxed);
		if (entryDiag < 48u) {
				logger::debug(
					"[GR-PROJECTILE-PROCESS] source={} type={} form={:08X} impactsEmpty={}",
				a_source ? a_source : "unknown",
				static_cast<std::uint32_t>(a_projectile->GetFormType()),
				a_projectile->GetFormID(),
				runtime.impacts.empty() ? 1 : 0);
		}

		std::uint32_t queued = 0u;
		for (auto* impact : runtime.impacts) {
			if (!impact ||
				!GroundFinitePoint(impact->desiredTargetLoc) ||
				!GroundClaimProjectileImpact(a_projectile, impact)) {
				continue;
			}

			RE::NiPoint3 velocity{
				-impact->negativeVelocity.x,
				-impact->negativeVelocity.y,
				-impact->negativeVelocity.z
			};
			if (!GroundFinitePoint(velocity) ||
				(std::abs(velocity.x) +
				 std::abs(velocity.y) +
				 std::abs(velocity.z)) < 1.0e-3f) {
				velocity = runtime.velocity;
			}

			globals::pipeline::groundResponse.QueueProjectileImpact(
				a_projectile,
				impact->desiredTargetLoc,
				velocity);
			++queued;
		}

		if (queued > 0u) {
			static std::atomic<std::uint32_t> s_processImpactDiagCount{ 0u };
			const auto diag =
				s_processImpactDiagCount.fetch_add(1u, std::memory_order_relaxed);
			if (diag < 64u) {
				logger::debug(
					"[GR-PROJECTILE-HOOK] source={} type={} form={:08X} newImpacts={}",
					a_source ? a_source : "unknown",
					static_cast<std::uint32_t>(a_projectile->GetFormType()),
					a_projectile->GetFormID(),
					queued);
			}
		}
		return queued;
	}

	void GroundScanProjectileManagerImpacts()
	{
		auto* manager = RE::Projectile::Manager::GetSingleton();
		if (!manager)
			return;

		auto scan =
			[](auto& a_handles) {
				for (auto& handle : a_handles) {
					auto projectile = handle.get();
					if (projectile)
						GroundQueueRuntimeProjectileImpacts(
							projectile.get(),
							"manager");
				}
			};

		// The arrays can overlap while projectiles transition between manager
		// buckets; impact identity de-duplication makes scanning all three safe.
		scan(manager->unlimited);
		scan(manager->limited);
		scan(manager->pending);
	}

	float GroundLength2D(float2 a_value)
	{
		return std::sqrt(
			std::max(a_value.x * a_value.x + a_value.y * a_value.y, 0.0f));
	}

	std::string GroundLowerText(std::string_view a_text)
	{
		std::string result;
		result.reserve(a_text.size());
		for (const char c : a_text) {
			result.push_back(
				static_cast<char>(
					std::tolower(static_cast<unsigned char>(c))));
		}
		return result;
	}

	GroundElementKind GroundClassifyEffect(const RE::EffectSetting* a_effect)
	{
		if (!a_effect)
			return GroundElementKind::kNone;

		// Vanilla elemental effects and well-authored mod effects normally retain
		// these standard keywords. EditorID fallback keeps compatibility with
		// simpler mods without hard-coding any plugin or FormID.
		if (a_effect->HasKeywordString("MagicDamageFire") ||
			a_effect->HasKeywordString("MagicFireDamage") ||
			a_effect->HasKeywordString("MagicFire")) {
			return GroundElementKind::kFire;
		}
		if (a_effect->HasKeywordString("MagicDamageFrost") ||
			a_effect->HasKeywordString("MagicFrostDamage") ||
			a_effect->HasKeywordString("MagicFrost") ||
			a_effect->HasKeywordString("MagicIce")) {
			return GroundElementKind::kFrost;
		}
		if (a_effect->HasKeywordString("MagicDamageShock") ||
			a_effect->HasKeywordString("MagicShockDamage") ||
			a_effect->HasKeywordString("MagicShock") ||
			a_effect->HasKeywordString("MagicLightning")) {
			return GroundElementKind::kShock;
		}

		const char* editorID = a_effect->GetFormEditorID();
		const std::string text =
			editorID ? GroundLowerText(editorID) : std::string{};
		if (text.find("fire") != std::string::npos ||
			text.find("flame") != std::string::npos ||
			text.find("burn") != std::string::npos) {
			return GroundElementKind::kFire;
		}
		if (text.find("frost") != std::string::npos ||
			text.find("ice") != std::string::npos ||
			text.find("freeze") != std::string::npos ||
			text.find("cold") != std::string::npos) {
			return GroundElementKind::kFrost;
		}
		if (text.find("shock") != std::string::npos ||
			text.find("lightning") != std::string::npos ||
			text.find("electric") != std::string::npos ||
			text.find("spark") != std::string::npos) {
			return GroundElementKind::kShock;
		}
		return GroundElementKind::kNone;
	}

	GroundElementKind GroundClassifyMagicItem(const RE::MagicItem* a_item)
	{
		if (!a_item)
			return GroundElementKind::kNone;

		float fireScore = 0.0f;
		float frostScore = 0.0f;
		float shockScore = 0.0f;
		for (const auto* effect : a_item->effects) {
			if (!effect || !effect->baseEffect)
				continue;
			const float weight =
				std::max(std::abs(effect->effectItem.magnitude), 1.0f);
			switch (GroundClassifyEffect(effect->baseEffect)) {
			case GroundElementKind::kFire:
				fireScore += weight;
				break;
			case GroundElementKind::kFrost:
				frostScore += weight;
				break;
			case GroundElementKind::kShock:
				shockScore += weight;
				break;
			default:
				break;
			}
		}

		if (fireScore >= frostScore &&
			fireScore >= shockScore &&
			fireScore > 0.0f) {
			return GroundElementKind::kFire;
		}
		if (frostScore >= shockScore && frostScore > 0.0f)
			return GroundElementKind::kFrost;
		if (shockScore > 0.0f)
			return GroundElementKind::kShock;

		if (const char* editorID = a_item->GetFormEditorID(); editorID && *editorID) {
			const std::string text = GroundLowerText(editorID);
			if (text.find("fire") != std::string::npos ||
				text.find("flame") != std::string::npos ||
				text.find("burn") != std::string::npos) {
				return GroundElementKind::kFire;
			}
			if (text.find("frost") != std::string::npos ||
				text.find("ice") != std::string::npos ||
				text.find("freeze") != std::string::npos ||
				text.find("cold") != std::string::npos) {
				return GroundElementKind::kFrost;
			}
			if (text.find("shock") != std::string::npos ||
				text.find("lightning") != std::string::npos ||
				text.find("electric") != std::string::npos ||
				text.find("spark") != std::string::npos) {
				return GroundElementKind::kShock;
			}
		}
		return GroundElementKind::kNone;
	}

	void GroundPushPendingInteraction(const GroundPendingInteraction& a_interaction)
	{
		if (!std::isfinite(a_interaction.start.x) ||
			!std::isfinite(a_interaction.start.y) ||
			!std::isfinite(a_interaction.end.x) ||
			!std::isfinite(a_interaction.end.y) ||
			!std::isfinite(a_interaction.startRadius) ||
			!std::isfinite(a_interaction.endRadius) ||
			!std::isfinite(a_interaction.strength) ||
			!std::isfinite(a_interaction.displacementScale) ||
			!std::isfinite(a_interaction.elementalDelta) ||
			!std::isfinite(a_interaction.smoothing) ||
			!std::isfinite(a_interaction.contactDepth) ||
			!std::isfinite(a_interaction.priority) ||
			(a_interaction.validateReceiver &&
				!std::isfinite(a_interaction.receiverZ))) {
			return;
		}

		std::scoped_lock lock(g_groundInteractionMutex);
		if (g_pendingGroundInteractions.size() <
			MAX_PENDING_GROUND_INTERACTIONS) {
			g_pendingGroundInteractions.push_back(a_interaction);
			return;
		}

		// Keep gameplay-significant events (shouts/projectiles) from being evicted
		// by a burst of low-priority resting-object refreshes.
		auto weakest = std::min_element(
			g_pendingGroundInteractions.begin(),
			g_pendingGroundInteractions.end(),
			[](const auto& a, const auto& b) {
				return a.priority < b.priority;
			});
		if (weakest != g_pendingGroundInteractions.end() &&
			a_interaction.priority > weakest->priority) {
			*weakest = a_interaction;
		}
	}

	std::vector<GroundPendingInteraction> GroundDrainPendingInteractions()
	{
		std::vector<GroundPendingInteraction> result;
		{
			std::scoped_lock lock(g_groundInteractionMutex);
			result.swap(g_pendingGroundInteractions);
		}
		std::stable_sort(
			result.begin(),
			result.end(),
			[](const auto& a, const auto& b) {
				return a.priority > b.priority;
			});
		return result;
	}

	bool GroundSampleLoadedLandSnowCoverageAt(
		const RE::NiPoint3& a_position,
		float& a_outCoverage)
	{
		a_outCoverage = 0.0f;
		auto* tes = RE::TES::GetSingleton();
		if (!tes)
			return false;

		auto* cell = tes->GetCell(a_position);
		if (!cell || !cell->IsExteriorCell())
			return false;
		auto* coords = cell->GetCoordinates();
		if (!coords)
			return false;
		auto* land = cell->GetRuntimeData().cellLand;
		if (!land || !land->loadedData)
			return false;

		constexpr float cellSize = 4096.0f;
		constexpr float vertexSpacing = 128.0f;
		const float cellOriginX =
			static_cast<float>(coords->cellX) * cellSize;
		const float cellOriginY =
			static_cast<float>(coords->cellY) * cellSize;
		const float gridX = std::clamp(
			(a_position.x - cellOriginX) / vertexSpacing,
			0.0f, 32.0f);
		const float gridY = std::clamp(
			(a_position.y - cellOriginY) / vertexSpacing,
			0.0f, 32.0f);
		const auto x0 = static_cast<std::uint32_t>(std::floor(gridX));
		const auto y0 = static_cast<std::uint32_t>(std::floor(gridY));
		const auto x1 = std::min(x0 + 1u, 32u);
		const auto y1 = std::min(y0 + 1u, 32u);
		const float tx = gridX - std::floor(gridX);
		const float ty = gridY - std::floor(gridY);

		const float c00 = ResistanceLoadedLandVertexSnowCoverage(land->loadedData, x0, y0);
		const float c10 = ResistanceLoadedLandVertexSnowCoverage(land->loadedData, x1, y0);
		const float c01 = ResistanceLoadedLandVertexSnowCoverage(land->loadedData, x0, y1);
		const float c11 = ResistanceLoadedLandVertexSnowCoverage(land->loadedData, x1, y1);
		a_outCoverage = ResistanceSaturate(
			ResistanceLerp(
				ResistanceLerp(c00, c10, tx),
				ResistanceLerp(c01, c11, tx),
				ty));
		return true;
	}

	bool GroundProbeSurface(
		const RE::NiPoint3& a_position,
		const GroundResponse& a_ground,
		GroundSurfaceProbe& a_out)
	{
		a_out = {};
		// Receiver classification belongs to the deformation/event system, not to
		// the renderer toggle. Keeping it alive while geometric rendering is
		// temporarily disabled preserves valid world interactions/history and avoids
		// making roofs/platform filtering depend on a visual setting.
		if (!a_ground.settings.EnableDeformableGround)
			return false;

		auto* tes = RE::TES::GetSingleton();
		if (!tes)
			return false;
		float landHeight = a_position.z;
		if (!tes->GetLandHeight(a_position, landHeight) ||
			!std::isfinite(landHeight)) {
			return false;
		}

		auto* landTexture = tes->GetLandTexture(a_position);
		if (!landTexture || GroundLandTextureIsHard(landTexture))
			return false;

		RE::MATERIAL_ID materialID = tes->GetLandMaterialType(a_position);
		if (landTexture->materialType &&
			landTexture->materialType->materialID != RE::MATERIAL_ID::kNone) {
			materialID = landTexture->materialType->materialID;
		}

		const float slopeMask =
			ResistanceTerrainSlopeMask(tes, a_position, a_ground);
		if (slopeMask <= 1.0e-4f)
			return false;

		float snowCoverage = 0.0f;
		const bool coverageValid =
			GroundSampleLoadedLandSnowCoverageAt(a_position, snowCoverage);
		float snowActivation = coverageValid
			? ResistanceSnowActivationFromCoverage(snowCoverage, a_ground)
			: 0.0f;
		if (snowActivation <= 1.0e-4f &&
			ResistanceClassifySnow(materialID, landTexture) != GroundSnowClassifier::kNone) {
			snowActivation = 1.0f;
		}

		float pristineDepth = 0.0f;
		float mudActivation = 0.0f;
		bool snow = false;
		bool mud = false;
		if (a_ground.settings.EnableSnowDeformation &&
			snowActivation > 1.0e-4f) {
			pristineDepth = ResistanceSnowPristineDepth(
				a_position, slopeMask, snowActivation, a_ground);
			snow = pristineDepth > 1.0e-3f;
		} else if (a_ground.settings.EnableMudDeformation) {
			mudActivation = ResistanceMudWetnessActivation(a_ground);
			if (mudActivation > 1.0e-4f) {
				pristineDepth = ResistanceMudPristineDepth(
					a_position, slopeMask, mudActivation, a_ground);
				mud = pristineDepth > 1.0e-3f;
			}
		}

		if (!snow && !mud)
			return false;

		a_out.valid = true;
		a_out.snow = snow;
		a_out.mud = mud;
		a_out.landHeight = landHeight;
		a_out.pristineDepth = pristineDepth;
		a_out.surfaceTop = landHeight + pristineDepth;
		a_out.snowActivation = snowActivation;
		a_out.mudActivation = mudActivation;
		a_out.slopeMask = slopeMask;
		return true;
	}

	bool GroundRaycastFraction(
		const RE::NiPoint3& a_start,
		const RE::NiPoint3& a_end,
		float& a_outFraction)
	{
		a_outFraction = 1.0f;
		if (!GroundFinitePoint(a_start) || !GroundFinitePoint(a_end))
			return false;
		auto* tes = RE::TES::GetSingleton();
		if (!tes)
			return false;

		const float scale = RE::bhkWorld::GetWorldScale();
		RE::bhkPickData pick{};
		pick.rayInput.from = RE::hkVector4{
			a_start.x * scale,
			a_start.y * scale,
			a_start.z * scale,
			0.0f
		};
		pick.rayInput.to = RE::hkVector4{
			a_end.x * scale,
			a_end.y * scale,
			a_end.z * scale,
			0.0f
		};
		// Use Skyrim's LOS collision layer for support/obstruction queries. A zero
		// filterInfo is kUnidentified and can silently miss the static geometry we
		// specifically need for roofs, bridges, platforms and shout blockers.
		// CommonLibSSE-NG v4.26.x exposes filterInfo as RE::CFilter, so use its
		// collision-layer setter rather than assigning a raw uint32_t.
		pick.rayInput.enableShapeCollectionFilter = false;
		pick.rayInput.filterInfo.SetCollisionLayer(RE::COL_LAYER::kLOS);
		pick.ray = RE::hkVector4{
			(a_end.x - a_start.x) * scale,
			(a_end.y - a_start.y) * scale,
			(a_end.z - a_start.z) * scale,
			0.0f
		};
		pick.rayOutput.Reset();
		tes->Pick(pick);
		if (!pick.rayOutput.HasHit())
			return false;
		a_outFraction = std::clamp(pick.rayOutput.hitFraction, 0.0f, 1.0f);
		return true;
	}

	bool GroundHasRaisedBlocker(
		float a_landHeight,
		float a_contactZ,
		float2 a_xy,
		float a_clearance)
	{
		const float clearance = std::max(a_clearance, 1.0f);
		const float startZ = a_landHeight + clearance;
		const float endZ = a_contactZ - clearance;
		if (endZ <= startZ + 1.0f)
			return false;

		float fraction = 1.0f;
		const bool hit = GroundRaycastFraction(
			RE::NiPoint3{ a_xy.x, a_xy.y, startZ },
			RE::NiPoint3{ a_xy.x, a_xy.y, endZ },
			fraction);
		// Fractions extremely close to zero can be the LAND collision itself due
		// to numerical mismatch between GetLandHeight and the Havok heightfield.
		return hit && fraction > 0.035f;
	}

	bool GroundReceiverAcceptsContact(
		float2 a_xy,
		float a_contactBottom,
		const GroundResponse& a_ground,
		GroundSurfaceProbe& a_probe,
		bool a_testBlocker)
	{
		const RE::NiPoint3 query{ a_xy.x, a_xy.y, a_contactBottom };
		if (!GroundProbeSurface(query, a_ground, a_probe))
			return false;

		const float tolerance = std::clamp(
			a_ground.settings.ReceiverContactTolerance,
			2.0f, 24.0f);
		if (a_contactBottom > a_probe.surfaceTop + tolerance ||
			a_contactBottom < a_probe.landHeight - 32.0f) {
			return false;
		}

		if (a_testBlocker &&
			GroundHasRaisedBlocker(
				a_probe.landHeight,
				std::max(a_contactBottom, a_probe.landHeight),
				a_xy,
				a_ground.settings.ReceiverBlockerClearance)) {
			return false;
		}
		return true;
	}

	bool GroundActorSupportBlocked(
		const RE::NiPoint3& a_actorPosition,
		const GroundResponse& a_ground)
	{
		GroundSurfaceProbe probe{};
		if (!GroundProbeSurface(a_actorPosition, a_ground, probe))
			return true;

		// If the actor is already on/inside the authored LAND/snow layer there is
		// no vertical gap in which a roof/bridge/platform can exist.
		const float clearance = std::clamp(
			a_ground.settings.ReceiverBlockerClearance,
			2.0f, 12.0f);
		if (a_actorPosition.z <= probe.landHeight + clearance * 2.0f)
			return false;

		return GroundHasRaisedBlocker(
			probe.landHeight,
			a_actorPosition.z,
			float2{ a_actorPosition.x, a_actorPosition.y },
			clearance);
	}

	bool GroundReferenceMatchesSet(
		const RE::TESObjectREFR* a_ref,
		const std::unordered_set<RE::FormID>& a_set)
	{
		if (!a_ref || a_set.empty())
			return false;
		if (a_set.contains(a_ref->GetFormID()))
			return true;
		if (const auto* base = a_ref->GetBaseObject();
			base && a_set.contains(base->GetFormID())) {
			return true;
		}
		return false;
	}

	bool GroundEditorIDContains(
		const RE::TESObjectREFR* a_ref,
		std::string_view a_token)
	{
		if (!a_ref || a_token.empty())
			return false;
		auto contains = [&](const RE::TESForm* form) {
			const char* id = form ? form->GetFormEditorID() : nullptr;
			return id && GroundLowerText(id).find(a_token) != std::string::npos;
		};
		return contains(a_ref) || contains(a_ref->GetBaseObject());
	}

	std::optional<RE::FormID> GroundResolveConfiguredForm(std::string_view a_token)
	{
		if (a_token.empty())
			return std::nullopt;
		const std::size_t split = a_token.find('|');

		if (split == std::string_view::npos) {
			const std::string raw(a_token);
			try {
				std::size_t consumed = 0u;
				const auto value = static_cast<RE::FormID>(
					std::stoul(raw, &consumed, 16));
				if (consumed == raw.size() && value != 0u)
					return value;
			} catch (...) {
				// Not a raw hex FormID; try it as an EditorID below.
			}

			if (auto* form = RE::TESForm::LookupByEditorID(raw))
				return form->GetFormID();
			return std::nullopt;
		}

		try {
			const std::string plugin(a_token.substr(0, split));
			std::string localText(a_token.substr(split + 1));
			if (localText.size() >= 2u &&
				localText[0] == '0' &&
				(localText[1] == 'x' || localText[1] == 'X'))
				localText.erase(0, 2);
			std::size_t consumed = 0u;
			const auto localID = static_cast<RE::FormID>(
				std::stoul(localText, &consumed, 16));
			if (consumed != localText.size())
				return std::nullopt;
			auto* data = RE::TESDataHandler::GetSingleton();
			if (!data)
				return std::nullopt;
			const RE::FormID resolved = data->LookupFormID(localID, plugin);
			return resolved != 0u
				? std::optional<RE::FormID>{ resolved }
				: std::nullopt;
		} catch (...) {
			return std::nullopt;
		}
	}

	void GroundResolveCompatibilityOverrides(const GroundResponse& a_ground)
	{
		g_alwaysCompressForms.clear();
		g_neverDeformForms.clear();
		for (const auto& token : a_ground.settings.AlwaysCompressForms) {
			if (auto id = GroundResolveConfiguredForm(token))
				g_alwaysCompressForms.insert(*id);
			else
				logger::warn("[GroundResponse] Could not resolve AlwaysCompressForms entry '{}'", token);
		}
		for (const auto& token : a_ground.settings.NeverDeformForms) {
			if (auto id = GroundResolveConfiguredForm(token))
				g_neverDeformForms.insert(*id);
			else
				logger::warn("[GroundResponse] Could not resolve NeverDeformForms entry '{}'", token);
		}
		logger::info(
			"[GroundResponse] compatibility overrides: alwaysCompress={} neverDeform={}",
			g_alwaysCompressForms.size(),
			g_neverDeformForms.size());
	}

	void GroundRebuildShoutSpellCache()
	{
		g_shoutSpellCache.clear();
		auto* data = RE::TESDataHandler::GetSingleton();
		if (!data)
			return;

		for (auto* shout : data->GetFormArray<RE::TESShout>()) {
			if (!shout)
				continue;
			for (std::uint32_t i = 0u; i < 3u; ++i) {
				auto* spell = shout->variations[i].spell;
				if (!spell)
					continue;
				GroundShoutSpellInfo info{};
				info.tier = static_cast<std::uint8_t>(i + 1u);
				info.element = GroundClassifyMagicItem(spell);
				const float authoredRange = spell->GetRange();
				info.range = std::isfinite(authoredRange) && authoredRange > 1.0f
					? authoredRange
					: (360.0f + 180.0f * static_cast<float>(i));
				g_shoutSpellCache[spell->GetFormID()] = info;
			}
		}
		logger::info(
			"[GroundResponse] cached {} shout variation spells",
			g_shoutSpellCache.size());
	}

	float GroundProjectileEffectArea(const RE::MagicItem* a_item)
	{
		if (!a_item)
			return 0.0f;
		float largest = 0.0f;
		for (const auto* effect : a_item->effects) {
			if (!effect)
				continue;
			largest = std::max(
				largest,
				static_cast<float>(effect->effectItem.area));
		}
		return largest;
	}

	void GroundPruneBodyStateMap(
		std::unordered_map<std::uint64_t, GroundPreviousBodyState>& a_map,
		std::uint32_t a_generation,
		std::uint32_t a_maxAge)
	{
		if (a_map.size() > 8192u) {
			a_map.clear();
			return;
		}
		for (auto it = a_map.begin(); it != a_map.end();) {
			const std::uint32_t age = a_generation - it->second.generation;
			if (age > a_maxAge)
				it = a_map.erase(it);
			else
				++it;
		}
	}

	float GroundAreaEquivalentRadius(const Util::DetailedShapeBound& a_bound)
	{
		const float hx = std::max(a_bound.halfExtents.x, 0.25f);
		const float hy = std::max(a_bound.halfExtents.y, 0.25f);
		return std::sqrt(hx * hy) * 1.28f;
	}

	void GroundScanObjectInteractions(
		RE::PlayerCharacter* a_player,
		GroundResponse& a_ground,
		std::vector<GroundPendingInteraction>& a_out)
	{
		static bool wasEnabled = false;
		if (!a_player)
			return;

		if (!a_ground.settings.EnableObjectInteraction) {
			if (wasEnabled)
				g_previousObjectStates.clear();
			wasEnabled = false;
			return;
		}

		// Do not reconnect a newly re-enabled object to a stale body centre from
		// before the feature was disabled; that would draw a long phantom sweep.
		if (!wasEnabled) {
			g_previousObjectStates.clear();
			wasEnabled = true;
		}

		static float accumulator = 0.0f;
		float dt = globals::game::deltaTime ? *globals::game::deltaTime : 0.0f;
		if (!std::isfinite(dt) || dt < 0.0f)
			dt = 0.0f;
		accumulator += std::min(dt, MAX_GROUND_FRAME_DELTA);
		const float interval = std::clamp(
			a_ground.settings.ObjectScanInterval,
			0.04f, 0.25f);
		if (accumulator < interval)
			return;
		accumulator = 0.0f;

		++g_objectStateGeneration;
		if (g_objectStateGeneration == 0u)
			++g_objectStateGeneration;

		auto* tes = RE::TES::GetSingleton();
		if (!tes)
			return;

		const float radius = std::clamp(
			a_ground.settings.ObjectInteractionRadius,
			256.0f, 1800.0f);
		const RE::NiPoint3 anchor = a_player->GetPosition();
		std::vector<GroundPendingInteraction> candidates;
		candidates.reserve(48);

		tes->ForEachReferenceInRange(
			a_player,
			radius,
			[&](RE::TESObjectREFR* ref) {
				if (!ref || ref == a_player || !ref->Is3DLoaded() || ref->IsDisabled())
					return RE::BSContainer::ForEachResult::kContinue;
				if (ref->As<RE::Actor>() || ref->AsProjectile())
					return RE::BSContainer::ForEachResult::kContinue;

				const bool alwaysCompress =
					GroundReferenceMatchesSet(ref, g_alwaysCompressForms) ||
					GroundEditorIDContains(ref, "pixl_groundcompressionmarker");
				const bool neverDeform =
					GroundReferenceMatchesSet(ref, g_neverDeformForms) ||
					GroundEditorIDContains(ref, "pixl_groundnodeform");
				if (neverDeform || (!alwaysCompress && !ref->CanBeMoved()))
					return RE::BSContainer::ForEachResult::kContinue;

				const RE::NiPoint3 refPos = ref->GetPosition();
				const float sqDistance = anchor.GetSquaredDistance(refPos);
				if (!std::isfinite(sqDistance) || sqDistance > radius * radius)
					return RE::BSContainer::ForEachResult::kContinue;

				RE::NiPoint3 linearVelocity{};
				ref->GetLinearVelocity(linearVelocity);
				float weight = ref->GetWeight();
				if (!std::isfinite(weight) || weight < 0.0f)
					weight = 0.0f;
				const float weightSignal = std::clamp(
					std::log2(1.0f + weight) / 7.0f,
					0.0f, 1.0f);

				auto* root = ref->Get3D();
				std::uint32_t accepted = 0u;
				if (root) {
					RE::BSVisit::TraverseScenegraphCollision(
						root,
						[&](RE::bhkNiCollisionObject* object) {
							if (accepted >= 6u)
								return RE::BSVisit::BSVisitControl::kStop;
							Util::DetailedShapeBound bound{};
							if (!Util::GetDetailedShapeBound(object, bound))
								return RE::BSVisit::BSVisitControl::kContinue;
							if (bound.boundingRadius < 0.5f || bound.boundingRadius > 180.0f)
								return RE::BSVisit::BSVisitControl::kContinue;

							const float bottom = bound.center.z - bound.halfExtents.z;
							GroundSurfaceProbe probe{};
							const float2 xy{ bound.center.x, bound.center.y };
							if (!GroundReceiverAcceptsContact(
								xy, bottom, a_ground, probe, true)) {
								return RE::BSVisit::BSVisitControl::kContinue;
							}

							const float contactDepth = std::clamp(
								probe.surfaceTop - bottom,
								0.0f, 96.0f);
							const float depthSignal = std::clamp(
								contactDepth /
									std::max(bound.halfExtents.z * 1.5f + 4.0f, 6.0f),
								0.0f, 1.0f);

							const std::uint64_t key =
								GroundBodyKey(ref->GetFormID(), bound.identity);
							RE::NiPoint3 previous = bound.center;
							if (const auto it = g_previousObjectStates.find(key);
								it != g_previousObjectStates.end() &&
								g_objectStateGeneration - it->second.generation == 1u) {
								previous = it->second.center;
							}
							g_previousObjectStates[key] = {
								bound.center,
								g_objectStateGeneration
							};

							float2 motion{
								bound.center.x - previous.x,
								bound.center.y - previous.y
							};
							if (GroundLength2D(motion) > SURFACE_TELEPORT_DISTANCE) {
								previous = bound.center;
								motion = {};
							}
							const float speedHint =
								std::sqrt(
									linearVelocity.x * linearVelocity.x +
									linearVelocity.y * linearVelocity.y);
							const float movingSignal = std::clamp(speedHint / 180.0f, 0.0f, 1.0f);
							const float stampRadius = std::clamp(
								GroundAreaEquivalentRadius(bound),
								OBJECT_MIN_STAMP_RADIUS,
								OBJECT_MAX_STAMP_RADIUS);

							GroundPendingInteraction interaction{};
							interaction.start = float2{ previous.x, previous.y };
							interaction.end = xy;
							interaction.startRadius = stampRadius;
							interaction.endRadius = stampRadius;
							interaction.contactDepth = contactDepth;
							interaction.strength = alwaysCompress
								? 1.0f
								: std::clamp(
									0.46f + depthSignal * 0.32f + weightSignal * 0.20f,
									0.46f, 0.98f);
							interaction.displacementScale = alwaysCompress
								? 0.08f
								: 0.10f + 0.78f * movingSignal;
							interaction.priority =
								20.0f + weightSignal * 8.0f -
								std::sqrt(std::max(sqDistance, 0.0f)) * 0.002f;
							candidates.push_back(interaction);
							++accepted;
							return RE::BSVisit::BSVisitControl::kContinue;
						});
				}

				// Invisible/configured marker fallback. Radius follows reference scale,
				// allowing a mod author to place/scale a marker without PIXL owning an ESP.
				if (alwaysCompress && accepted == 0u) {
					GroundSurfaceProbe probe{};
					if (GroundReceiverAcceptsContact(
						float2{ refPos.x, refPos.y },
						refPos.z,
						a_ground,
						probe,
						true)) {
						GroundPendingInteraction interaction{};
						interaction.start = interaction.end = float2{ refPos.x, refPos.y };
						const float markerRadius = std::clamp(
							24.0f * std::max(ref->GetScale(), 0.1f),
							6.0f, 96.0f);
						interaction.startRadius = interaction.endRadius = markerRadius;
						interaction.strength = 1.0f;
						interaction.displacementScale = 0.0f;
						interaction.contactDepth = probe.pristineDepth;
						interaction.priority = 30.0f;
						candidates.push_back(interaction);
					}
				}

				return RE::BSContainer::ForEachResult::kContinue;
			});

		std::stable_sort(
			candidates.begin(), candidates.end(),
			[](const auto& a, const auto& b) { return a.priority > b.priority; });
		if (candidates.size() > 24u)
			candidates.resize(24u);
		a_out.insert(a_out.end(), candidates.begin(), candidates.end());
		GroundPruneBodyStateMap(
			g_previousObjectStates,
			g_objectStateGeneration,
			180u);
	}
}


NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	GroundResponse::Settings,
	EnableGroundResponse,
	TrackRagdolls,
	EnableDeformableGround,
	EnableSnowDeformation,
	EnableGeometricSnow,
	EnableMudDeformation,
	MudRequiresWetness,
	SnowMaximumDepth,
	SnowSurfaceThickness,
	GeometryRenderDistance,
	GeometryFadeStart,
	GeometryMinimumSlopeZ,
	GeometryTessellationNear,
	GeometryTessellationFar,
	GeometryTessellationNearDistance,
	GeometryTessellationFarDistance,
	SnowCoverageThreshold,
	SnowCoverageFeather,
	MudMaximumDepth,
	GroundNormalStrength,
	SnowCompactionDarkening,
	MudDarkening,
	MudRoughness,
	MudWetnessThreshold,
	GroundResponseStrength,
	DebugInteractionField,
	GeometrySelfTest,
	TrackRecoveryRate,
	TrackHoldSeconds,
	EnableAnimatedBodyContacts,
	EnableObjectInteraction,
	EnableProjectileInteraction,
	EnableMagicInteraction,
	EnableShoutInteraction,
	EnableElementalSnow,
	ObjectInteractionRadius,
	ObjectScanInterval,
	ReceiverContactTolerance,
	ReceiverBlockerClearance,
	FireMeltUnits,
	FrostAddUnits,
	ElementalHeightLimit,
	ElementalRecoveryRate,
	AlwaysCompressForms,
	NeverDeformForms)

void GroundResponse::DrawSettings()
{
	bool changed = false;
	if (ImGui::TreeNodeEx(T(TKEY("grass_collision"), "Ground Response"), ImGuiTreeNodeFlags_DefaultOpen)) {
		changed |= ImGui::Checkbox(T(TKEY("enable"), "Enable Ground Response"), &settings.EnableGroundResponse);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Bends grass around nearby actors using the shared world-space interaction field. Applies immediately.");
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Deformable Snow & Mud", ImGuiTreeNodeFlags_DefaultOpen)) {
		changed |= ImGui::Checkbox("Enable Ground Deformation", &settings.EnableDeformableGround);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Adds a persistent raised landscape shell for snow and wet non-snow terrain. Actor Havok bounds write continuous previous-to-current capsule stamps into a dedicated absolute-world XY compaction map. Snow is thick, mud is thinner, and both compress toward Skyrim's untouched base terrain without storing camera height in the trail history.");
		if (!settings.EnableDeformableGround) {
			// Diagnostics must never survive a master disable and keep painting terrain.
			settings.DebugInteractionField = false;
			settings.GeometrySelfTest = false;
		}
		ImGui::BeginDisabled(!settings.EnableDeformableGround);
		changed |= ImGui::Checkbox("Snow Tracks", &settings.EnableSnowDeformation);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Compacts only the landscape texture layers Skyrim marks as snow. Mixed snow/rock quads use the six interpolated landscape blend weights continuously, so geometry fades through the authored snow transition instead of covering the entire terrain pass.");
		changed |= ImGui::SliderFloat("Snow Compression Depth", &settings.SnowMaximumDepth, 2.0f, 40.0f, "%.1f units", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Material response depth for compacted snow. Geometric depth is physically capped by Undisturbed Snow Thickness minus the thin packed-snow floor, so increase shell thickness for very deep snow.");
		changed |= ImGui::SliderFloat("Compacted Snow Darkening", &settings.SnowCompactionDarkening, 0.0f, 0.65f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Darkens compressed snow slightly to reveal granular tracks without painting black decals.");

		if (ImGui::TreeNodeEx("Geometric Snow & Mud Surface", ImGuiTreeNodeFlags_DefaultOpen)) {
			changed |= ImGui::Checkbox("Enable Geometric Snow + Mud", &settings.EnableGeometricSnow);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::TextWrapped("Replays the real landscape as a raised adaptive shell. The shell samples the persistent absolute-world compaction map in the domain shader: untouched texels stay raised, stamped texels collapse toward the original landscape. Snow and mud use the same footprint data but different physical thickness/floor values.");
			ImGui::BeginDisabled(!settings.EnableGeometricSnow);
			changed |= ImGui::SliderFloat("Undisturbed Snow Thickness", &settings.SnowSurfaceThickness, 2.0f, 24.0f, "%.1f units", ImGuiSliderFlags_AlwaysClamp);
			const float derivedMudSurfaceThickness =
				std::clamp(
					std::max(
						settings.SnowSurfaceThickness * 0.30f,
						settings.MudMaximumDepth * 0.20f),
					1.5f,
					5.0f);
			ImGui::TextDisabled("Wet mud surface thickness: %.2f units (derived)", derivedMudSurfaceThickness);
			changed |= ImGui::SliderFloat("Geometry Distance", &settings.GeometryRenderDistance, 384.0f, 4096.0f, "%.0f units", ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::SliderFloat("Geometry Fade Start", &settings.GeometryFadeStart, 256.0f, 4000.0f, "%.0f units", ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::SliderFloat("Minimum Upward Slope", &settings.GeometryMinimumSlopeZ, 0.20f, 0.90f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::SliderFloat("Close Tessellation", &settings.GeometryTessellationNear, 1.0f, 16.0f, "%.1fx", ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::SliderFloat("Medium Tessellation", &settings.GeometryTessellationFar, 1.0f, 10.0f, "%.1fx", ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::SliderFloat("Close LOD Distance", &settings.GeometryTessellationNearDistance, 64.0f, 1600.0f, "%.0f units", ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::SliderFloat("Medium LOD Distance", &settings.GeometryTessellationFarDistance, 384.0f, 3200.0f, "%.0f units", ImGuiSliderFlags_AlwaysClamp);
			ImGui::TextDisabled("Far LOD: automatic <= 2x silhouette tessellation until Geometry Distance.");
			changed |= ImGui::SliderFloat("Snow Coverage Threshold", &settings.SnowCoverageThreshold, 0.0f, 0.40f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::SliderFloat("Snow Coverage Feather", &settings.SnowCoverageFeather, 0.02f, 0.50f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::TextWrapped("Coverage uses Skyrim's per-layer snow flags multiplied by the landscape blend weights available to the terrain draw. Threshold/feather only shape the transition; they never infer snow from texture names or weather.");
			ImGui::EndDisabled();
			ImGui::TreePop();
		}

		ImGui::Separator();
		changed |= ImGui::Checkbox("Mud Ruts", &settings.EnableMudDeformation);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Uses the same persistent world-space capsule trail and raised-shell geometry as snow on wet non-snow landscape. Mud is thinner (roughly 30% of snow thickness, 1.5-5 units) and compresses almost to the original terrain so the original rock/soil detail can show through the rut.");
		changed |= ImGui::Checkbox("Require Wet Ground", &settings.MudRequiresWetness);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Prevents dry soil and stone landscape layers from looking muddy. Disable to preview mud deformation in any weather.");
		changed |= ImGui::SliderFloat("Mud Activation", &settings.MudWetnessThreshold, 0.0f, 0.75f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Wetness level required before mud tracks become visible. Lower values react sooner after rain begins.");
		changed |= ImGui::SliderFloat("Mud Rut Depth", &settings.MudMaximumDepth, 2.0f, 30.0f, "%.1f units", ImGuiSliderFlags_AlwaysClamp);
		changed |= ImGui::SliderFloat("Mud Darkening", &settings.MudDarkening, 0.0f, 0.80f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		// Keep the existing MudRoughness ABI/JSON field, but expose it as the
		// artist-facing quantity being tuned: wet gloss/specular response.
		float mudGlossSpecular =
			1.0f -
			std::clamp(
				(settings.MudRoughness - 0.08f) / (0.80f - 0.08f),
				0.0f,
				1.0f);
		if (ImGui::SliderFloat(
				"Compressed Mud Gloss / Specular",
				&mudGlossSpecular,
				0.0f,
				1.0f,
				"%.2f",
				ImGuiSliderFlags_AlwaysClamp)) {
			settings.MudRoughness =
				0.80f + (0.08f - 0.80f) * mudGlossSpecular;
			changed = true;
		}
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Higher values make fresh compressed mud smoother with a stronger wet dielectric highlight. This reuses the existing mud roughness field, so the shared-data ABI stays unchanged.");

		ImGui::Separator();
		if (ImGui::TreeNodeEx("Movement Resistance", ImGuiTreeNodeFlags_DefaultOpen)) {
			changed |= ImGui::Checkbox(
				"Enable Snow / Mud Resistance",
				&g_groundResistanceSettings.EnableMovementResistance);
			changed |= ImGui::Checkbox(
				"Player Resistance",
				&g_groundResistanceSettings.EnablePlayerResistance);
			changed |= ImGui::Checkbox(
				"NPC Resistance",
				&g_groundResistanceSettings.EnableNPCResistance);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::TextWrapped("NPC resistance only scans loaded high-process actors within GroundResponse's existing 2048-unit gameplay radius.");

			changed |= ImGui::SliderFloat(
				"Snow Resistance Strength",
				&g_groundResistanceSettings.SnowResistanceStrength,
				0.0f,
				1.0f,
				"%.2f",
				ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::SliderFloat(
				"Fluffy Snow Resistance",
				&g_groundResistanceSettings.FluffySnowResistanceStrength,
				0.0f,
				0.60f,
				"%.2f",
				ImGuiSliderFlags_AlwaysClamp);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::TextWrapped("Baseline resistance reached quickly in shallow fresh snow before the deeper wading curve takes over. 0.30 gives roughly 10-15% slowdown in an ordinary ~8-10 unit blanket and more by shin depth.");
			changed |= ImGui::SliderFloat(
				"Mud Resistance Strength",
				&g_groundResistanceSettings.MudResistanceStrength,
				0.0f,
				1.0f,
				"%.2f",
				ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::SliderFloat(
				"Mud Minimum Speed",
				&g_groundResistanceSettings.MudMinimumSpeedScale,
				0.30f,
				0.95f,
				"%.2fx",
				ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::SliderFloat(
				"Deep Snow Minimum Speed",
				&g_groundResistanceSettings.MinimumSurfaceSpeedScale,
				0.20f,
				0.85f,
				"%.2fx",
				ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::SliderFloat(
				"Snow Resistance Starts",
				&g_groundResistanceSettings.SnowResistanceStartDepth,
				2.0f,
				32.0f,
				"%.1f units",
				ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::SliderFloat(
				"Full Snow Resistance Depth",
				&g_groundResistanceSettings.SnowFullResistanceDepth,
				24.0f,
				72.0f,
				"%.1f units",
				ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::SliderFloat(
				"Resistance Response",
				&g_groundResistanceSettings.ResistanceResponseRate,
				1.0f,
				12.0f,
				"%.1f /s",
				ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::Checkbox(
				"Debug Movement Resistance",
				&g_groundResistanceSettings.DebugMovementResistance);

			ImGui::TextDisabled(
				"Player: %s | depth %.1f | target %.0f%% | filtered %.0f%%",
				ResistanceSurfaceName(g_lastPlayerResistanceSurface),
				g_lastPlayerResistanceDepth,
				g_lastPlayerResistanceTargetScale * 100.0f,
				g_lastPlayerResistanceFilteredScale * 100.0f);
			ImGui::TextDisabled(
				"Snow: %s | coverage %.2f | activation %.2f | material %u",
				ResistanceSnowClassifierName(g_lastPlayerSnowClassifier),
				g_lastPlayerSnowCoverage,
				g_lastPlayerSnowActivation,
				g_lastPlayerResistanceMaterial);
			ImGui::TextDisabled(
				"SpeedMult %.2f | PIXL delta %.2f",
				g_lastPlayerObservedSpeedMult,
				g_lastPlayerAppliedSpeedDelta);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::TextWrapped("Snow depth is the CPU mirror of the accepted 13G absolute-world mound/deep-drift function. Resistance is smoothly applied through the temporary SpeedMult modifier channel and the exact PIXL contribution is removed when an actor leaves the surface.");
			ImGui::TreePop();
		}

		ImGui::Separator();
		if (ImGui::TreeNodeEx("World Interactions", ImGuiTreeNodeFlags_DefaultOpen)) {
			changed |= ImGui::Checkbox("Animated Havok Body Contacts", &settings.EnableAnimatedBodyContacts);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::TextWrapped("Uses live third-person Havok body bounds for independent feet, paws, limbs, hands and ragdolls. The old single player capsule is retained only as a fail-soft fallback.");
			changed |= ImGui::Checkbox("Movable / Resting Objects", &settings.EnableObjectInteraction);
			changed |= ImGui::Checkbox("Projectile Impacts", &settings.EnableProjectileInteraction);
			changed |= ImGui::Checkbox("Magic Ground Interaction", &settings.EnableMagicInteraction);
			changed |= ImGui::Checkbox("Shout Cone Interaction", &settings.EnableShoutInteraction);
			changed |= ImGui::Checkbox("Fire / Frost Snow Mass", &settings.EnableElementalSnow);
			changed |= ImGui::SliderFloat("Object Interaction Radius", &settings.ObjectInteractionRadius, 384.0f, 1800.0f, "%.0f units", ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::SliderFloat("Object Scan Interval", &settings.ObjectScanInterval, 0.04f, 0.25f, "%.3f s", ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::SliderFloat("Surface Contact Tolerance", &settings.ReceiverContactTolerance, 2.0f, 24.0f, "%.1f units", ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::SliderFloat("Raised Support Clearance", &settings.ReceiverBlockerClearance, 2.0f, 12.0f, "%.1f units", ImGuiSliderFlags_AlwaysClamp);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::TextWrapped("Receiver validation compares contacts to LAND and rejects raised Havok support between the landscape and actor/object, preventing roofs, bridges, platforms and rocks from stamping terrain underneath.");
			ImGui::BeginDisabled(!settings.EnableElementalSnow);
			changed |= ImGui::SliderFloat("Fire Melt Depth", &settings.FireMeltUnits, 0.0f, 24.0f, "%.1f units", ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::SliderFloat("Frost Added Snow", &settings.FrostAddUnits, 0.0f, 24.0f, "%.1f units", ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::SliderFloat("Elemental Height Limit", &settings.ElementalHeightLimit, 0.0f, 32.0f, "%.1f units", ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::SliderFloat("Elemental Recovery", &settings.ElementalRecoveryRate, 0.0f, 0.25f, "%.3f units/s", ImGuiSliderFlags_AlwaysClamp);
			ImGui::EndDisabled();
			ImGui::TextDisabled("Compatibility IDs: Plugin|LocalHex, full FormID, or EditorID in AlwaysCompressForms / NeverDeformForms");
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::TextWrapped("Entries accept Plugin.esp|LocalFormID or a resolved hexadecimal FormID. Mod authors may also use editor IDs containing PIXL_GroundCompressionMarker or PIXL_GroundNoDeform.");
			ImGui::TreePop();
		}

		ImGui::Separator();
		changed |= ImGui::SliderFloat("Track Normal Strength", &settings.GroundNormalStrength, 0.0f, 2.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		changed |= ImGui::SliderFloat("Interaction Strength", &settings.GroundResponseStrength, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Overall stamp strength for both materials. Snow/mud history is stored as normalized compaction in an absolute-world XY toroidal field; first/third person, camera height and equipment changes never rebase existing trail values.");
		changed |= ImGui::SliderFloat("Track Hold Time", &settings.TrackHoldSeconds, 0.0f, 30.0f, "%.1f s", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("How long a fresh footprint/rut stays at full depth before recovery begins. Re-contact restarts the hold timer locally; it does not reset the whole field.");
		changed |= ImGui::SliderFloat("Track Recovery Rate", &settings.TrackRecoveryRate, 0.05f, 2.0f, "%.2f units/s", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Recovery speed after Track Hold Time expires. Internally this is converted to normalized compaction recovery using the snow-shell thickness, so snow and mud share one persistent footprint lifetime.");
		changed |= ImGui::Checkbox("Debug Interaction Field", &settings.DebugInteractionField);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Non-destructive diagnostic tint over the real terrain texture: red = no actor collision boxes, magenta = missing Material Forge snow metadata, cyan = snow, brown = mud/non-snow, green/yellow = contact/depression. The overlay is accepted only when the GroundResponse runtime magic/version match exactly.");
		changed |= ImGui::Checkbox("Geometry Self-Test (+64 units)", &settings.GeometrySelfTest);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Diagnostic only. Forces every eligible nearby terrain patch through 16x tessellation and raises the generated surface by 64 units. If the surface does not visibly jump upward, the HS/DS stages are not controlling the draw. Turn this off for normal play.");
		ImGui::EndDisabled();
		ImGui::TreePop();
	}

	if (changed)
		globals::state->UpdateFeatureData(globals::state->inWorld);
}

void GroundResponse::QueueCollisions()
{
	static uint32_t s_queueDiagCount = 0u;
	const bool queueDiag = s_queueDiagCount < 16u;
	if (queueDiag)
		logger::debug("[GR-DIAG] QueueCollisions enter #{}", s_queueDiagCount);

	if (!settings.EnableGroundResponse && !settings.EnableDeformableGround) {
		if (queueDiag)
			logger::debug("[GR-DIAG] QueueCollisions early-out: disabled");
		++s_queueDiagCount;
		return;
	}

	eastl::vector<GroundResponseActorCandidate> actorCandidates{};
	auto* player = RE::PlayerCharacter::GetSingleton();
	const RE::NiPoint3 interactionAnchor =
		player ? player->GetPosition() : Util::GetEyePosition();

	auto addActorCandidate = [&](RE::ActorHandle a_handle) {
		auto actor = a_handle.get();
		if (actor && actor->Is3DLoaded()) {
			const float sqDistance =
				interactionAnchor.GetSquaredDistance(actor->GetPosition());
			if (std::isfinite(sqDistance) && sqDistance <= MAX_ACTOR_SQ_DISTANCE)
				actorCandidates.push_back({ a_handle, sqDistance });
		}
	};

	if (const auto processLists = RE::ProcessLists::GetSingleton(); processLists) {
		for (auto& actorHandle : processLists->highActorHandles)
			addActorCandidate(actorHandle);
	}

	// The player is explicit because highActorHandles is not a stable contract for
	// the local player across camera/game states.
	if (player)
		addActorCandidate(player->GetHandle());

	std::sort(
		actorCandidates.begin(),
		actorCandidates.end(),
		[](const GroundResponseActorCandidate& a, const GroundResponseActorCandidate& b) {
			return a.sqDistance < b.sqDistance;
		});

	if (queueDiag)
		logger::debug("[GR-DIAG] QueueCollisions candidates={}", actorCandidates.size());

	eastl::vector<BoundingBoxPacked> boundingBoxData{};
	boundingBoxData.reserve(MAX_BOUNDING_BOXES);
	eastl::vector<float4> collisionsData{};
	collisionsData.reserve(MAX_COLLISIONS);

	eastl::vector<SurfaceStampBoxPacked> surfaceBoxData{};
	surfaceBoxData.reserve(MAX_SURFACE_STAMP_BOXES);
	eastl::vector<SurfaceStampPacked> surfaceStampData{};
	surfaceStampData.reserve(MAX_SURFACE_STAMPS);

	// Used only by the fail-soft local-player fallback when no usable animated
	// Havok bodies exist. Normal snow/mud interaction tracks each body separately.
	static std::unordered_map<std::uint32_t, RE::NiPoint3> previousSurfaceActorPositions;
	if (previousSurfaceActorPositions.size() > 4096u)
		previousSurfaceActorPositions.clear();

	++g_bodyStateGeneration;
	if (g_bodyStateGeneration == 0u)
		++g_bodyStateGeneration;

	uint collisionIndexExtent = 0u;
	uint surfaceStampIndexExtent = 0u;
	bool playerProxyQueued = false;
	bool playerProcessed = false;

	const float frameDtRaw =
		globals::game::deltaTime ? *globals::game::deltaTime : 0.0f;
	const float frameDt =
		std::clamp(
			std::isfinite(frameDtRaw) ? frameDtRaw : 0.0f,
			1.0f / 240.0f,
			MAX_GROUND_FRAME_DELTA);

	for (const auto& actorCandidate : actorCandidates) {
		auto actor = actorCandidate.handle.get();
		if (!actor || !actor->Is3DLoaded())
			continue;

		const bool isPlayer =
			player && actor->GetFormID() == player->GetFormID();
		if (isPlayer && playerProcessed)
			continue;

		auto* primaryRoot = actor->Get3D(false);
		auto* firstPersonRoot = isPlayer ? actor->Get3D(true) : nullptr;
		if (!primaryRoot && !firstPersonRoot && !isPlayer)
			continue;

		const RE::NiPoint3 actorPosition = actor->GetPosition();
		// Resolve support once per actor. The same result gates detailed body
		// contacts and the fail-soft player fallback, avoiding a duplicate LAND +
		// Havok receiver query when a skeleton exposes no usable body shapes.
		const bool supportedOnDeformableSurface =
			settings.EnableDeformableGround &&
			!actor->IsInMidair() &&
			!GroundActorSupportBlocked(actorPosition, *this);
		const bool surfaceContactAllowed =
			supportedOnDeformableSurface &&
			settings.EnableAnimatedBodyContacts;

		const float distance =
			std::sqrt(std::max(actorCandidate.sqDistance, 0.0f));

		eastl::vector<float4> collisionShapes{};
		std::vector<Util::DetailedShapeBound> detailedSurfaceShapes{};
		detailedSurfaceShapes.reserve(32);

		auto collectRoot = [&](RE::NiAVObject* a_root, bool a_surfaceStableRoot) {
			if (!a_root)
				return;

			RE::BSVisit::TraverseScenegraphCollision(
				a_root,
				[&](RE::bhkNiCollisionObject* a_object) -> RE::BSVisit::BSVisitControl {
					// Legacy centre/radius path remains byte-for-byte compatible for
					// grass. Surface interaction uses the richer live AABB separately.
					RE::NiPoint3 centerPos{};
					float radius = 0.0f;
					if (Util::GetShapeBound(a_object, centerPos, radius)) {
						const bool finiteBound =
							GroundFinitePoint(centerPos) &&
							std::isfinite(radius);
						if (finiteBound &&
							radius > MIN_COLLISION_RADIUS &&
							radius <= MAX_COLLISION_RADIUS &&
							radius >= distance * MIN_COLLISION_RADIUS_DISTANCE_SCALE) {
							collisionShapes.push_back(
								float4{ centerPos.x, centerPos.y, centerPos.z, radius });
						}
					}

					if (a_surfaceStableRoot) {
						Util::DetailedShapeBound detailed{};
						if (Util::GetDetailedShapeBound(a_object, detailed) &&
							GroundFinitePoint(detailed.center) &&
							GroundFinitePoint(detailed.halfExtents) &&
							std::isfinite(detailed.boundingRadius) &&
							detailed.boundingRadius > 0.35f &&
							detailed.boundingRadius <= 180.0f &&
							detailed.halfExtents.x >= 0.0f &&
							detailed.halfExtents.y >= 0.0f &&
							detailed.halfExtents.z >= 0.0f &&
							detailed.halfExtents.x <= 128.0f &&
							detailed.halfExtents.y <= 128.0f &&
							detailed.halfExtents.z <= 180.0f) {
							detailedSurfaceShapes.push_back(detailed);
						}
					}
					return RE::BSVisit::BSVisitControl::kContinue;
				});
		};

		// Third-person/world graph is authoritative for body/surface interaction.
		// Detailed AABBs are only needed while the actor can actually contact a
		// deformable receiver; grass still consumes the unchanged legacy bounds.
		// First-person equipment remains grass-only so equip/unequip cannot reset
		// or redirect snow history.
		collectRoot(primaryRoot, surfaceContactAllowed);
		if (firstPersonRoot && firstPersonRoot != primaryRoot)
			collectRoot(firstPersonRoot, false);

		const std::uint32_t actorID = actor->GetFormID();
		RE::NiPoint3 previousActorPosition = actorPosition;
		if (const auto it = previousSurfaceActorPositions.find(actorID);
			it != previousSurfaceActorPositions.end()) {
			previousActorPosition = it->second;
		}
		float2 actorDelta{
			actorPosition.x - previousActorPosition.x,
			actorPosition.y - previousActorPosition.y
		};
		if (!std::isfinite(actorDelta.x) || !std::isfinite(actorDelta.y) ||
			GroundLength2D(actorDelta) > SURFACE_TELEPORT_DISTANCE) {
			actorDelta = {};
		}

		SurfaceStampBoxPacked actorSurfaceBox{};
		actorSurfaceBox.IndexStart = surfaceStampIndexExtent;
		actorSurfaceBox.IndexEnd = surfaceStampIndexExtent;
		uint actorSurfaceStampCount = 0u;

		auto appendActorSurfaceStamp =
			[&](const GroundPendingInteraction& a_interaction) {
				if (surfaceBoxData.size() >= MAX_ACTOR_SURFACE_STAMP_BOXES ||
					surfaceStampIndexExtent + actorSurfaceStampCount >= MAX_SURFACE_STAMPS ||
					actorSurfaceStampCount >= MAX_SURFACE_STAMPS_PER_BOX) {
					return false;
				}

				SurfaceStampPacked stamp{};
				stamp.CurrentPosition = a_interaction.end;
				stamp.PreviousPosition = a_interaction.start;
				stamp.Radius =
					std::clamp(
						a_interaction.endRadius,
						SURFACE_STAMP_MIN_RADIUS,
						SURFACE_STAMP_MAX_RADIUS);
				stamp.PreviousRadius =
					std::clamp(
						a_interaction.startRadius,
						SURFACE_STAMP_MIN_RADIUS,
						SURFACE_STAMP_MAX_RADIUS);
				stamp.Strength =
					std::clamp(a_interaction.strength, 0.0f, 1.0f);
				stamp.DisplacementScale =
					std::clamp(a_interaction.displacementScale, 0.0f, 1.5f);
				stamp.ElementalDelta = 0.0f;
				stamp.Smoothing = 0.0f;
				stamp.Flags = 0u;
				stamp.ContactDepth =
					std::max(a_interaction.contactDepth, 0.0f);
				surfaceStampData.push_back(stamp);

				const float influenceRadius =
					std::max(stamp.Radius, stamp.PreviousRadius) * 2.85f + 4.0f;
				const float2 pointMin{
					std::min(stamp.CurrentPosition.x, stamp.PreviousPosition.x) -
						influenceRadius,
					std::min(stamp.CurrentPosition.y, stamp.PreviousPosition.y) -
						influenceRadius
				};
				const float2 pointMax{
					std::max(stamp.CurrentPosition.x, stamp.PreviousPosition.x) +
						influenceRadius,
					std::max(stamp.CurrentPosition.y, stamp.PreviousPosition.y) +
						influenceRadius
				};

				if (actorSurfaceStampCount == 0u) {
					actorSurfaceBox.MinExtent = pointMin;
					actorSurfaceBox.MaxExtent = pointMax;
				} else {
					actorSurfaceBox.MinExtent.x =
						std::min(actorSurfaceBox.MinExtent.x, pointMin.x);
					actorSurfaceBox.MinExtent.y =
						std::min(actorSurfaceBox.MinExtent.y, pointMin.y);
					actorSurfaceBox.MaxExtent.x =
						std::max(actorSurfaceBox.MaxExtent.x, pointMax.x);
					actorSurfaceBox.MaxExtent.y =
						std::max(actorSurfaceBox.MaxExtent.y, pointMax.y);
				}
				++actorSurfaceStampCount;
				++actorSurfaceBox.IndexEnd;
				return true;
			};

		// Chain a body sweep only across consecutive frames in which that same
		// Havok body actually intersects the deformable surface. A hand entering
		// snow from above therefore starts with a local contact instead of carving
		// the XY path it travelled while it was still in the air.
		std::vector<GroundPendingInteraction> bodyContacts;
		bodyContacts.reserve(detailedSurfaceShapes.size());

		for (const auto& bound : detailedSurfaceShapes) {
			const std::uint64_t key =
				GroundBodyKey(actorID, bound.identity);

			const float bottom =
				bound.center.z - bound.halfExtents.z;
			const float top =
				bound.center.z + bound.halfExtents.z;
			GroundSurfaceProbe probe{};
			if (!GroundReceiverAcceptsContact(
					float2{ bound.center.x, bound.center.y },
					bottom,
					*this,
					probe,
					false)) {
				continue;
			}
			if (top < probe.landHeight - 8.0f)
				continue;

			RE::NiPoint3 previous = bound.center;
			if (const auto it = g_previousBodyStates.find(key);
				it != g_previousBodyStates.end() &&
				g_bodyStateGeneration - it->second.generation == 1u) {
				previous = it->second.center;
			}
			g_previousBodyStates[key] = {
				bound.center,
				g_bodyStateGeneration
			};

			float2 bodyMotion{
				bound.center.x - previous.x,
				bound.center.y - previous.y
			};
			if (!std::isfinite(bodyMotion.x) ||
				!std::isfinite(bodyMotion.y) ||
				GroundLength2D(bodyMotion) > SURFACE_TELEPORT_DISTANCE) {
				previous = bound.center;
				bodyMotion = {};
			}

			const float contactDepth =
				std::clamp(
					probe.surfaceTop - bottom,
					0.0f,
					std::max(probe.pristineDepth + 20.0f, 8.0f));
			const float depthSignal =
				std::clamp(
					contactDepth /
						std::max(
							std::min(bound.halfExtents.z * 1.35f + 3.0f, 36.0f),
							5.0f),
					0.0f,
					1.0f);
			const float motionLength = GroundLength2D(bodyMotion);
			const float bodySpeed = motionLength / frameDt;
			const float movingSignal =
				std::clamp(bodySpeed / 150.0f, 0.0f, 1.0f);
			const float stampRadius =
				std::clamp(
					GroundAreaEquivalentRadius(bound),
					SURFACE_STAMP_MIN_RADIUS,
					SURFACE_STAMP_MAX_RADIUS);

			GroundPendingInteraction interaction{};
			interaction.start = float2{ previous.x, previous.y };
			interaction.end = float2{ bound.center.x, bound.center.y };
			interaction.startRadius = stampRadius;
			interaction.endRadius = stampRadius;
			interaction.contactDepth = contactDepth;
			interaction.strength =
				std::clamp(
					0.55f +
						depthSignal * 0.34f +
						std::clamp(stampRadius / 22.0f, 0.0f, 1.0f) * 0.08f,
					0.55f,
					0.98f);

			// A hand/forearm grazing deep snow should cut/pack it without creating
			// the same lateral berm as a planted paw/boot or a moving torso.
			const float shallowContact =
				1.0f - std::clamp(contactDepth / 10.0f, 0.0f, 1.0f);
			interaction.displacementScale =
				std::clamp(
					0.08f +
						movingSignal * 0.70f +
						depthSignal * 0.12f -
						shallowContact * 0.10f,
					0.05f,
					0.92f);
			interaction.priority =
				contactDepth * 1.4f +
				motionLength * 2.2f +
				stampRadius * 0.55f;
			bodyContacts.push_back(interaction);
		}

		// A creature can expose dozens of ragdoll bodies. Keep the contacts that
		// matter most visually/physically while retaining independent paws/hands.
		std::stable_sort(
			bodyContacts.begin(),
			bodyContacts.end(),
			[](const auto& a, const auto& b) {
				return a.priority > b.priority;
			});
		if (bodyContacts.size() > 16u)
			bodyContacts.resize(16u);

		if (surfaceContactAllowed) {
			for (const auto& contact : bodyContacts) {
				if (!appendActorSurfaceStamp(contact))
					break;
			}
		}

		// Fail-soft only: skeletons with no usable world Havok bodies retain one
		// narrow actor-root capsule. It is never preferred over body contacts.
		if (supportedOnDeformableSurface &&
			actorSurfaceStampCount == 0u &&
			isPlayer) {
			GroundPendingInteraction fallback{};
			fallback.start = float2{
				actorPosition.x - actorDelta.x,
				actorPosition.y - actorDelta.y
			};
			fallback.end = float2{ actorPosition.x, actorPosition.y };
			fallback.startRadius = fallback.endRadius = 9.0f;
			fallback.strength = 0.92f;
			fallback.displacementScale =
				std::clamp(
					0.10f + GroundLength2D(actorDelta) / 18.0f,
					0.10f,
					0.82f);
			fallback.contactDepth = 6.0f;
			appendActorSurfaceStamp(fallback);
		}

		if (actorSurfaceBox.IndexStart != actorSurfaceBox.IndexEnd &&
			surfaceBoxData.size() < MAX_ACTOR_SURFACE_STAMP_BOXES) {
			surfaceBoxData.push_back(actorSurfaceBox);
			surfaceStampIndexExtent = actorSurfaceBox.IndexEnd;
		}

		previousSurfaceActorPositions[actorID] = actorPosition;

		// Preserve the existing grass interaction field exactly.
		if (isPlayer && !playerProxyQueued) {
			const auto playerPos = player->GetPosition();
			collisionShapes.push_back(float4{
				playerPos.x,
				playerPos.y,
				playerPos.z + PLAYER_GROUND_PROXY_CENTER_Z,
				PLAYER_GROUND_PROXY_RADIUS });
			playerProxyQueued = true;
			geometryTelemetry.PlayerProxyStamps++;
		}

		if (collisionShapes.empty()) {
			if (isPlayer)
				playerProcessed = true;
			continue;
		}

		std::sort(
			collisionShapes.begin(),
			collisionShapes.end(),
			[](const float4& a, const float4& b) {
				return a.w > b.w;
			});

		BoundingBoxPacked boundingBox{};
		boundingBox.IndexStart = collisionIndexExtent;
		boundingBox.IndexEnd = collisionIndexExtent;
		uint boundingBoxCollisions = 0u;

		for (const auto& data : collisionShapes) {
			if (collisionIndexExtent + boundingBoxCollisions >= MAX_COLLISIONS)
				break;

			collisionsData.push_back(data);
			const float2 pointMin(data.x - data.w, data.y - data.w);
			const float2 pointMax(data.x + data.w, data.y + data.w);
			if (boundingBoxCollisions == 0u) {
				boundingBox.MinExtent = pointMin;
				boundingBox.MaxExtent = pointMax;
			} else {
				boundingBox.MinExtent.x =
					std::min(boundingBox.MinExtent.x, pointMin.x);
				boundingBox.MinExtent.y =
					std::min(boundingBox.MinExtent.y, pointMin.y);
				boundingBox.MaxExtent.x =
					std::max(boundingBox.MaxExtent.x, pointMax.x);
				boundingBox.MaxExtent.y =
					std::max(boundingBox.MaxExtent.y, pointMax.y);
			}
			++boundingBox.IndexEnd;
			++boundingBoxCollisions;
			if (boundingBoxCollisions == MAX_COLLISIONS_PER_BOUNDING_BOX)
				break;
		}

		if (boundingBox.IndexStart != boundingBox.IndexEnd) {
			if (isPlayer)
				playerProcessed = true;
			boundingBoxData.push_back(boundingBox);
			collisionIndexExtent = boundingBox.IndexEnd;
			if (boundingBoxData.size() == MAX_BOUNDING_BOXES ||
				collisionIndexExtent >= MAX_COLLISIONS) {
				break;
			}
		}
	}

	GroundPruneBodyStateMap(
		g_previousBodyStates,
		g_bodyStateGeneration,
		180u);

	// PIXL_GR_13BG_CONTINUOUS_MAGIC_CASTER_V1
	// TESSpellCastEvent is a cast notification, not a per-frame concentration
	// update. Poll live caster state at a modest fixed cadence so Flames,
	// Frostbite, beams/cones and dragon breath continue to affect the receiver for
	// the entire active cast. QueueMagicCast still rejects ordinary fire-and-forget
	// spells, which remain driven by their real projectile impact records.
	static float s_magicStreamAccumulator = 0.0f;
	const bool gamePaused =
		globals::game::ui && globals::game::ui->GameIsPaused();
	if (settings.EnableMagicInteraction &&
		settings.EnableElementalSnow &&
		!gamePaused &&
		std::isfinite(frameDtRaw) && frameDtRaw > 0.0f) {
		s_magicStreamAccumulator +=
			std::clamp(frameDtRaw, 0.0f, MAX_GROUND_FRAME_DELTA);
		if (s_magicStreamAccumulator >= MAGIC_STREAM_UPDATE_INTERVAL) {
			s_magicStreamAccumulator =
				std::fmod(s_magicStreamAccumulator, MAGIC_STREAM_UPDATE_INTERVAL);

			std::unordered_set<std::uint64_t> emittedCasterSpells;
			const std::array<RE::MagicSystem::CastingSource, 4> castingSources{
				RE::MagicSystem::CastingSource::kLeftHand,
				RE::MagicSystem::CastingSource::kRightHand,
				RE::MagicSystem::CastingSource::kOther,
				RE::MagicSystem::CastingSource::kInstant
			};

			for (const auto& actorCandidate : actorCandidates) {
				auto actor = actorCandidate.handle.get();
				if (!actor || !actor->Is3DLoaded())
					continue;

				for (const auto castingSource : castingSources) {
					auto* caster = actor->GetMagicCaster(castingSource);
					if (!caster || !caster->currentSpell)
						continue;

					auto* currentSpell = caster->currentSpell;
					const auto casterState = caster->state.get();
					const bool stateActive =
						casterState == RE::MagicCaster::State::kCharging ||
						casterState == RE::MagicCaster::State::kCasting ||
						casterState == RE::MagicCaster::State::kUnk07;
					const bool actorReportsCasting =
						actor->IsCasting(currentSpell);
					const bool activeCast =
						stateActive || actorReportsCasting;

					// CommonLib labels only the two proven states; Skyrim keeps several
					// concentration/creature casters in the adjacent post-start state while
					// the beam/flame is visibly active. Actor::IsCasting is the authoritative
					// gameplay fallback, so do not require exactly kCasting.
					if (!activeCast) {
						if (currentSpell->GetCastingType() ==
								RE::MagicSystem::CastingType::kConcentration &&
							GroundClassifyMagicItem(currentSpell) !=
								GroundElementKind::kNone) {
							static std::atomic<std::uint32_t>
								s_magicCandidateDiagCount{ 0u };
							const auto diag =
								s_magicCandidateDiagCount.fetch_add(
									1u, std::memory_order_relaxed);
							if (diag < 48u) {
						logger::debug(
							"[GR-MAGIC-CANDIDATE] spell={:08X} caster={:08X} source={} state={} actorCasting=0",
									currentSpell->GetFormID(),
									actor->GetFormID(),
									static_cast<std::uint32_t>(castingSource),
									static_cast<std::uint32_t>(casterState));
							}
						}
						continue;
					}

					const RE::FormID spellFormID = currentSpell->GetFormID();
					if (spellFormID == 0u)
						continue;

					const std::uint64_t key =
						(static_cast<std::uint64_t>(actor->GetFormID()) << 32u) |
						static_cast<std::uint64_t>(spellFormID);
					if (!emittedCasterSpells.insert(key).second)
						continue;

					static std::atomic<std::uint32_t>
						s_magicActiveDiagCount{ 0u };
					const auto diag =
						s_magicActiveDiagCount.fetch_add(
							1u, std::memory_order_relaxed);
					if (diag < 48u) {
					logger::debug(
						"[GR-MAGIC-CANDIDATE] spell={:08X} caster={:08X} source={} state={} actorCasting={} active=1",
							spellFormID,
							actor->GetFormID(),
							static_cast<std::uint32_t>(castingSource),
							static_cast<std::uint32_t>(casterState),
							actorReportsCasting ? 1 : 0);
					}

					QueueMagicCast(actor.get(), spellFormID, true);
				}
			}
		}
	} else {
		s_magicStreamAccumulator = 0.0f;
	}

	// 13BH fail-safe: harvest authoritative ImpactData from the projectile manager.
	// This is intentionally in addition to the ProcessImpacts vtable hooks below;
	// de-duplication by projectile/ImpactData identity guarantees exactly one stamp.
	if (settings.EnableProjectileInteraction)
		GroundScanProjectileManagerImpacts();

	// Non-actor events share the same stamp ABI. Reserve the last 16 boxes for
	// objects/projectiles/shouts so a crowded settlement cannot starve gameplay
	// interactions. Pending projectile/shout events are priority-sorted first.
	std::vector<GroundPendingInteraction> genericInteractions =
		GroundDrainPendingInteractions();
	GroundScanObjectInteractions(player, *this, genericInteractions);
	std::stable_sort(
		genericInteractions.begin(),
		genericInteractions.end(),
		[](const auto& a, const auto& b) {
			return a.priority > b.priority;
		});

	auto appendGenericInteraction =
		[&](const GroundPendingInteraction& interaction) {
			if (surfaceBoxData.size() >= MAX_SURFACE_STAMP_BOXES ||
				surfaceStampData.size() >= MAX_SURFACE_STAMPS)
				return false;

			SurfaceStampPacked stamp{};
			stamp.CurrentPosition = interaction.end;
			stamp.PreviousPosition = interaction.start;
			stamp.Radius =
				std::clamp(interaction.endRadius, 0.5f, 128.0f);
			stamp.PreviousRadius =
				std::clamp(interaction.startRadius, 0.5f, 128.0f);
			stamp.Strength =
				std::clamp(interaction.strength, 0.0f, 1.0f);
			stamp.DisplacementScale =
				std::clamp(interaction.displacementScale, 0.0f, 2.0f);
			const float elementalLimit =
				std::clamp(settings.ElementalHeightLimit, 0.0f, ELEMENTAL_HARD_LIMIT);
			stamp.ElementalDelta =
				settings.EnableElementalSnow
					? std::clamp(
						interaction.elementalDelta,
						-elementalLimit,
						elementalLimit)
					: 0.0f;
			stamp.Smoothing =
				settings.EnableElementalSnow
					? std::clamp(interaction.smoothing, 0.0f, 1.0f)
					: 0.0f;
			stamp.Flags = interaction.flags;
			stamp.ContactDepth = std::max(interaction.contactDepth, 0.0f);

			SurfaceStampBoxPacked box{};
			box.IndexStart = static_cast<uint>(surfaceStampData.size());
			box.IndexEnd = box.IndexStart + 1u;
			const float influenceRadius =
				std::max(stamp.Radius, stamp.PreviousRadius) * 2.85f + 4.0f;
			box.MinExtent = float2{
				std::min(stamp.CurrentPosition.x, stamp.PreviousPosition.x) -
					influenceRadius,
				std::min(stamp.CurrentPosition.y, stamp.PreviousPosition.y) -
					influenceRadius
			};
			box.MaxExtent = float2{
				std::max(stamp.CurrentPosition.x, stamp.PreviousPosition.x) +
					influenceRadius,
				std::max(stamp.CurrentPosition.y, stamp.PreviousPosition.y) +
					influenceRadius
			};

			surfaceStampData.push_back(stamp);
			surfaceBoxData.push_back(box);
			surfaceStampIndexExtent =
				static_cast<uint>(surfaceStampData.size());
			return true;
		};

	for (auto interaction : genericInteractions) {
		if (interaction.validateReceiver) {
			GroundSurfaceProbe probe{};
			if (!GroundReceiverAcceptsContact(
					interaction.end,
					interaction.receiverZ,
					*this,
					probe,
					true)) {
				if (interaction.source == GroundInteractionSource::kProjectile) {
					static std::uint32_t s_projectileRejectDiagCount = 0u;
					if (s_projectileRejectDiagCount++ < 32u) {
						logger::debug(
							"[GR-PROJECTILE-REJECT] spell={:08X} xy=({:.1f},{:.1f}) receiverZ={:.1f}",
							interaction.sourceFormID,
							interaction.end.x, interaction.end.y,
							interaction.receiverZ);
					}
				}
				continue;
			}
			if (interaction.elementalSnowOnly && !probe.snow) {
				interaction.elementalDelta = 0.0f;
				interaction.smoothing = 0.0f;
			}
		}
		if (!appendGenericInteraction(interaction))
			break;
	}

	queuedBoundingBoxes = std::move(boundingBoxData);
	queuedCollisions = std::move(collisionsData);
	queuedSurfaceStampBoxes = std::move(surfaceBoxData);
	queuedSurfaceStamps = std::move(surfaceStampData);

	if (queueDiag) {
		logger::debug(
			"[GR-DIAG] QueueCollisions exit boxes={} collisions={} surfaceBoxes={} surfaceStamps={}",
			queuedBoundingBoxes.size(),
			queuedCollisions.size(),
			queuedSurfaceStampBoxes.size(),
			queuedSurfaceStamps.size());
	}
	++s_queueDiagCount;
}

void GroundResponse::Update()
{
	auto context = globals::d3d::context;
	static Util::FrameChecker frameChecker;
	static uint32_t s_updateDiagCount = 0u;
	if (frameChecker.IsNewFrame()) {
		const bool updateDiag = s_updateDiagCount < 16u;
		if (updateDiag)
			logger::debug("[GR-DIAG] Update new-frame enter #{} context={}", s_updateDiagCount, static_cast<const void*>(context));
		PerFrame perFrameData{};
		perFrameData.BoundingBoxCount = 0;

		static float2 prevCellID = { 0, 0 };
		static RE::NiPoint3 prevEyePosNI{};
		static RE::NiPoint3 prevAnchorPosNI{};
		static bool clipmapInitialized = false;

		// Snow/mud owns a separate XY-only clipmap. It is never rebased by camera
		// height and never reset by equip/unequip or 1P/3P eye-origin changes.
		static float2 prevSurfaceCellID = { 0, 0 };
		static bool surfaceClipmapInitialized = false;

		const auto eyePosNI = Util::GetEyePosition();
		auto* player = RE::PlayerCharacter::GetSingleton();
		const RE::NiPoint3 anchorPosNI = player ? player->GetPosition() : eyePosNI;
		if (updateDiag)
			logger::debug(
				"[GR-DIAG] Update anchor player={} eye=({:.1f},{:.1f},{:.1f}) anchor=({:.1f},{:.1f},{:.1f})",
				static_cast<const void*>(player),
				eyePosNI.x, eyePosNI.y, eyePosNI.z,
				anchorPosNI.x, anchorPosNI.y, anchorPosNI.z);
		const float2 eyePos{ eyePosNI.x, eyePosNI.y };
		const float2 anchorPos{ anchorPosNI.x, anchorPosNI.y };

		const float worldSize = INTERACTION_WORLD_SIZE;
		const uint textureArrayDims = INTERACTION_TEXTURE_SIZE;
		const float cellSize = worldSize / static_cast<float>(textureArrayDims);

		// Legacy grass field. Keep its existing transform/height semantics so this
		// architecture change cannot regress foliage interaction.
		auto cellID = anchorPos / cellSize;
		cellID = { round(cellID.x), round(cellID.y) };
		const auto cellOriginAbsolute = cellID * cellSize;

		float2 cellIDDiff = { 0, 0 };
		if (clipmapInitialized)
			cellIDDiff = prevCellID - cellID;

		const float anchorHeightDelta =
			clipmapInitialized ? (prevAnchorPosNI.z - anchorPosNI.z) : 0.0f;
		const bool clipmapReset =
			!clipmapInitialized ||
			std::abs(cellIDDiff.x) >= static_cast<float>(textureArrayDims) ||
			std::abs(cellIDDiff.y) >= static_cast<float>(textureArrayDims) ||
			std::abs(anchorHeightDelta) >= CLIPMAP_VERTICAL_RESET_DISTANCE;
		if (clipmapReset)
			geometryTelemetry.ClipmapResets++;

		perFrameData.PosOffset = cellOriginAbsolute - eyePos;

		auto wrapOrigin = [](int value, uint dimension) -> uint {
			const int size = static_cast<int>(dimension);
			return static_cast<uint>((value % size + size) % size);
		};
		perFrameData.ArrayOrigin = {
			wrapOrigin(
				static_cast<int>(cellID.x) - static_cast<int>(textureArrayDims / 2),
				textureArrayDims),
			wrapOrigin(
				static_cast<int>(cellID.y) - static_cast<int>(textureArrayDims / 2),
				textureArrayDims)
		};

		perFrameData.ValidMargin =
			clipmapReset
				? DirectX::XMINT2{ (int)textureArrayDims, (int)textureArrayDims }
				: DirectX::XMINT2{ (int)cellIDDiff.x, (int)cellIDDiff.y };

		float frameDelta = *globals::game::deltaTime;
		if (!std::isfinite(frameDelta) || frameDelta < 0.0f)
			frameDelta = 0.0f;
		frameDelta = std::min(frameDelta, MAX_GROUND_FRAME_DELTA);
		perFrameData.TimeDelta = frameDelta * !globals::game::ui->GameIsPaused();

		float cameraHeightDelta =
			clipmapInitialized ? (prevEyePosNI.z - eyePosNI.z) : 0.0f;
		const bool finiteCameraDelta = std::isfinite(cameraHeightDelta);
		const bool suppressCameraRebase =
			!finiteCameraDelta || std::abs(cameraHeightDelta) > MAX_CAMERA_REBASE_DELTA;
		if (suppressCameraRebase) {
			cameraHeightDelta = 0.0f;
			geometryTelemetry.CameraRebaseSuppressed++;
		}
		perFrameData.CameraHeightDelta = clipmapReset ? 0.0f : cameraHeightDelta;
		perFrameData.DebugInteractionField = 0u;
		perFrameData.TrackHoldSeconds = std::clamp(settings.TrackHoldSeconds, 0.0f, 30.0f);
		perFrameData.TrackRecoveryRate = std::clamp(settings.TrackRecoveryRate, 0.05f, 2.0f);

		perFrameData.TerrainSnow1to4 = {};
		perFrameData.TerrainSnow5to6 = {};
		perFrameData.TerrainSnowValid = 0u;
		perFrameData.TerrainGeometryPass = 0u;
		perFrameData.TerrainMaterialForge = 0u;
		perFrameData.TerrainDebug =
			(settings.DebugInteractionField ? TERRAIN_DEBUG_OVERLAY : 0u) |
			(settings.GeometrySelfTest ? TERRAIN_DEBUG_GEOMETRY_SELF_TEST : 0u);
		perFrameData.SnowSurfaceThickness = std::clamp(settings.SnowSurfaceThickness, 2.0f, 24.0f);
		// PIXL_GR_13Y_THREE_STAGE_LOD_CPP_V1
		// Keep the proven b13 layout unchanged; reinterpret existing near/far
		// controls as CLOSE/MEDIUM, with a shader-derived FAR silhouette tier.
		perFrameData.GeometryRenderDistance = std::clamp(settings.GeometryRenderDistance, 384.0f, 4096.0f);
		perFrameData.GeometryFadeStart = std::clamp(settings.GeometryFadeStart, 256.0f, perFrameData.GeometryRenderDistance - 32.0f);
		perFrameData.GeometryMinimumSlopeZ = std::clamp(settings.GeometryMinimumSlopeZ, 0.20f, 0.90f);
		perFrameData.GeometryTessellationNear = std::clamp(settings.GeometryTessellationNear, 1.0f, 16.0f);
		perFrameData.GeometryTessellationFar = std::clamp(
			settings.GeometryTessellationFar,
			1.0f,
			std::min(perFrameData.GeometryTessellationNear, 10.0f));

		const float closeLodMaximum =
			std::max(
				64.0f,
				std::min(
					1600.0f,
					perFrameData.GeometryRenderDistance * 0.45f));
		perFrameData.GeometryTessellationNearDistance = std::clamp(
			settings.GeometryTessellationNearDistance,
			64.0f,
			closeLodMaximum);

		const float mediumLodMinimum =
			perFrameData.GeometryTessellationNearDistance + 128.0f;
		const float mediumLodMaximum =
			std::max(
				mediumLodMinimum,
				std::min(
					3200.0f,
					perFrameData.GeometryRenderDistance - 64.0f));
		perFrameData.GeometryTessellationFarDistance = std::clamp(
			settings.GeometryTessellationFarDistance,
			mediumLodMinimum,
			mediumLodMaximum);
		perFrameData.SnowCoverageThreshold = std::clamp(settings.SnowCoverageThreshold, 0.0f, 0.40f);
		perFrameData.SnowCoverageFeather = std::clamp(settings.SnowCoverageFeather, 0.02f, 0.50f);
		perFrameData.RuntimeMagic = GROUND_RUNTIME_MAGIC;
		perFrameData.RuntimeVersion = GROUND_RUNTIME_VERSION;
		perFrameData.BoundingBoxCount = std::min((uint)queuedBoundingBoxes.size(), MAX_BOUNDING_BOXES);

		// Dedicated normalized surface clipmap: absolute world XY, 4 units/texel.
		const float surfaceCellSize =
			SURFACE_WORLD_SIZE / static_cast<float>(SURFACE_TEXTURE_SIZE);
		auto surfaceCellID = anchorPos / surfaceCellSize;
		surfaceCellID = { round(surfaceCellID.x), round(surfaceCellID.y) };
		const float2 surfaceOriginAbsolute = surfaceCellID * surfaceCellSize;

		float2 surfaceCellDiff = { 0, 0 };
		if (surfaceClipmapInitialized)
			surfaceCellDiff = prevSurfaceCellID - surfaceCellID;

		const bool surfaceClipmapReset =
			!surfaceClipmapInitialized ||
			std::abs(surfaceCellDiff.x) >= static_cast<float>(SURFACE_TEXTURE_SIZE) ||
			std::abs(surfaceCellDiff.y) >= static_cast<float>(SURFACE_TEXTURE_SIZE);

		const DirectX::XMUINT2 surfaceArrayOrigin{
			wrapOrigin(
				static_cast<int>(surfaceCellID.x) -
					static_cast<int>(SURFACE_TEXTURE_SIZE / 2),
				SURFACE_TEXTURE_SIZE),
			wrapOrigin(
				static_cast<int>(surfaceCellID.y) -
					static_cast<int>(SURFACE_TEXTURE_SIZE / 2),
				SURFACE_TEXTURE_SIZE)
		};
		const DirectX::XMINT2 surfaceValidMargin =
			surfaceClipmapReset
				? DirectX::XMINT2{ (int)SURFACE_TEXTURE_SIZE, (int)SURFACE_TEXTURE_SIZE }
				: DirectX::XMINT2{
					(int)surfaceCellDiff.x,
					(int)surfaceCellDiff.y
				};

		perFrameData.SurfaceOriginAbsolute = surfaceOriginAbsolute;
		perFrameData.SurfaceArrayOrigin = surfaceArrayOrigin;

		SurfaceFieldData surfaceFieldData{};
		surfaceFieldData.OriginAbsolute = surfaceOriginAbsolute;
		surfaceFieldData.ArrayOrigin = surfaceArrayOrigin;
		surfaceFieldData.ValidMargin = surfaceValidMargin;
		surfaceFieldData.TimeDelta = perFrameData.TimeDelta;
		surfaceFieldData.StampBoxCount =
			std::min(
				(uint)queuedSurfaceStampBoxes.size(),
				MAX_SURFACE_STAMP_BOXES);
		surfaceFieldData.TrackHoldSeconds =
			std::clamp(settings.TrackHoldSeconds, 0.0f, 30.0f);

		// Preserve the existing user-facing "world units/sec" recovery meaning.
		// The normalized field converts that to a fraction of the pristine snow
		// shell per second. Mud uses the same temporal footprint but a thinner shell.
		const float recoveryReferenceDepth =
			std::max(perFrameData.SnowSurfaceThickness, 1.0f);
		surfaceFieldData.RecoveryRate =
			std::clamp(
				settings.TrackRecoveryRate / recoveryReferenceDepth,
				0.001f,
				1.0f);
		surfaceFieldData.StampStrength =
			std::clamp(settings.GroundResponseStrength, 0.0f, 2.0f);
		surfaceFieldData.ElementalRecoveryRate =
			std::clamp(settings.ElementalRecoveryRate, 0.0f, 2.0f);

		// Legacy grass collision shapes are camera-relative at dispatch time.
		for (auto& collision : queuedCollisions) {
			collision.x -= eyePosNI.x;
			collision.y -= eyePosNI.y;
			collision.z -= eyePosNI.z;
		}
		for (auto& bounds : queuedBoundingBoxes) {
			bounds.MinExtent.x -= eyePosNI.x;
			bounds.MinExtent.y -= eyePosNI.y;
			bounds.MaxExtent.x -= eyePosNI.x;
			bounds.MaxExtent.y -= eyePosNI.y;
		}

		if (!queuedCollisions.empty()) {
			D3D11_MAPPED_SUBRESOURCE mapped;
			DX::ThrowIfFailed(context->Map(collisionInstances->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
			const size_t bytes = sizeof(float4) * queuedCollisions.size();
			memcpy_s(mapped.pData, sizeof(float4) * MAX_COLLISIONS, queuedCollisions.data(), bytes);
			context->Unmap(collisionInstances->resource.get(), 0);
		}

		if (perFrameData.BoundingBoxCount > 0) {
			D3D11_MAPPED_SUBRESOURCE mapped;
			DX::ThrowIfFailed(context->Map(collisionBoundingBoxes->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
			const size_t bytes = sizeof(BoundingBoxPacked) * perFrameData.BoundingBoxCount;
			memcpy_s(mapped.pData, sizeof(BoundingBoxPacked) * MAX_BOUNDING_BOXES, queuedBoundingBoxes.data(), bytes);
			context->Unmap(collisionBoundingBoxes->resource.get(), 0);
		}

		if (!queuedSurfaceStamps.empty()) {
			D3D11_MAPPED_SUBRESOURCE mapped;
			DX::ThrowIfFailed(context->Map(surfaceStamps->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
			const size_t stampCount =
				std::min(queuedSurfaceStamps.size(), static_cast<size_t>(MAX_SURFACE_STAMPS));
			const size_t bytes = sizeof(SurfaceStampPacked) * stampCount;
			memcpy_s(mapped.pData, sizeof(SurfaceStampPacked) * MAX_SURFACE_STAMPS, queuedSurfaceStamps.data(), bytes);
			context->Unmap(surfaceStamps->resource.get(), 0);
		}

		if (surfaceFieldData.StampBoxCount > 0) {
			D3D11_MAPPED_SUBRESOURCE mapped;
			DX::ThrowIfFailed(context->Map(surfaceStampBoxes->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
			const size_t bytes =
				sizeof(SurfaceStampBoxPacked) *
				static_cast<size_t>(surfaceFieldData.StampBoxCount);
			memcpy_s(
				mapped.pData,
				sizeof(SurfaceStampBoxPacked) * MAX_SURFACE_STAMP_BOXES,
				queuedSurfaceStampBoxes.data(),
				bytes);
			context->Unmap(surfaceStampBoxes->resource.get(), 0);
		}

		const size_t uploadedSurfaceStampCount = queuedSurfaceStamps.size();
		queuedBoundingBoxes.clear();
		queuedCollisions.clear();
		queuedSurfaceStampBoxes.clear();
		queuedSurfaceStamps.clear();

		if (updateDiag)
			logger::debug(
				"[GR-DIAG] Update before CB upload boxes={} surfaceBoxes={} surfaceStamps={} reset={} surfaceReset={}",
				perFrameData.BoundingBoxCount,
				surfaceFieldData.StampBoxCount,
				uploadedSurfaceStampCount,
				clipmapReset ? 1 : 0,
				surfaceClipmapReset ? 1 : 0);

		currentPerFrame = perFrameData;
		perFrame->Update(currentPerFrame);
		if (surfacePerFrame)
			surfacePerFrame->Update(surfaceFieldData);

		if (updateDiag)
			logger::debug("[GR-DIAG] Update CB upload complete");

		currentPosOffset = currentPerFrame.PosOffset;
		currentArrayOrigin = currentPerFrame.ArrayOrigin;

		if (updateDiag)
			logger::debug(
				"[GR-DIAG] Update before feature data state={} inWorld={}",
				static_cast<const void*>(globals::state),
				globals::state ? (globals::state->inWorld ? 1 : 0) : -1);

		globals::state->UpdateFeatureData(globals::state->inWorld);

		if (updateDiag)
			logger::debug("[GR-DIAG] Update before legacy collision dispatch");
		// Grass retains its legacy height field. Snow/mud is updated separately
		// from absolute XY capsule stamps and therefore never consumes camera Z.
		UpdateCollisionTexture();
		if (updateDiag)
			logger::debug("[GR-DIAG] Update legacy collision dispatch complete; before surface dispatch");
		UpdateSurfaceDeformationTexture();
		if (updateDiag)
			logger::debug("[GR-DIAG] Update surface dispatch complete");

		prevCellID = cellID;
		prevAnchorPosNI = anchorPosNI;
		if (!suppressCameraRebase || !clipmapInitialized)
			prevEyePosNI = eyePosNI;
		clipmapInitialized = true;

		prevSurfaceCellID = surfaceCellID;
		surfaceClipmapInitialized = true;
		if (updateDiag)
			logger::debug("[GR-DIAG] Update new-frame exit #{}", s_updateDiagCount);
		++s_updateDiagCount;
	}

	ID3D11ShaderResourceView* grassInteractionSRV =
		settings.EnableGroundResponse && collisionTexture
			? collisionTexture->srv.get()
			: nullptr;
	context->VSSetShaderResources(100, 1, &grassInteractionSRV);

	// v3 snow/mud geometry is driven exclusively by the absolute-world t101 field.
	// Do NOT expose the legacy camera-relative collider-height texture to terrain PS
	// t100. Cached pre-v3 Lighting permutations still contain the old material
	// deformation path; feeding them t100 creates a dark camera-following mask even
	// though the actual v3 geometry/tracks remain correctly anchored in world space.
	ID3D11ShaderResourceView* nullLegacyTerrainPS = nullptr;
	ID3D11ShaderResourceView* surfaceSRV =
		settings.EnableDeformableGround && surfaceDeformationTexture
			? surfaceDeformationTexture->srv.get()
			: nullptr;
	context->PSSetShaderResources(100, 1, &nullLegacyTerrainPS);
	context->PSSetShaderResources(101, 1, &surfaceSRV);

	ID3D11Buffer* runtimeCB = perFrame ? perFrame->CB() : nullptr;
	context->PSSetConstantBuffers(13, 1, &runtimeCB);
}

GroundResponse::GroundData GroundResponse::GetGroundData() const
{
	return {
		.EnableDeformableGround = settings.EnableDeformableGround ? 1u : 0u,
		.EnableSnowDeformation = settings.EnableSnowDeformation ? 1u : 0u,
		.EnableMudDeformation = settings.EnableMudDeformation ? 1u : 0u,
		.MudRequiresWetness = settings.MudRequiresWetness ? 1u : 0u,
		.SnowMaximumDepth = std::clamp(settings.SnowMaximumDepth, 2.0f, 40.0f),
		.MudMaximumDepth = std::clamp(settings.MudMaximumDepth, 2.0f, 30.0f),
		.GroundNormalStrength = std::clamp(settings.GroundNormalStrength, 0.0f, 2.5f),
		.SnowCompactionDarkening = std::clamp(settings.SnowCompactionDarkening, 0.0f, 0.65f),
		.MudDarkening = std::clamp(settings.MudDarkening, 0.0f, 0.80f),
		.MudRoughness = std::clamp(settings.MudRoughness, 0.08f, 0.80f),
		.MudWetnessThreshold = std::clamp(settings.MudWetnessThreshold, 0.0f, 0.75f),
		.GroundResponseStrength = std::clamp(settings.GroundResponseStrength, 0.0f, 2.0f),
		.PosOffset = currentPosOffset,
		.ArrayOrigin = currentArrayOrigin
	};
}

void GroundResponse::EarlyPrepass()
{
	Update();
}

void GroundResponse::LoadSettings(json& o_json)
{
	settings = o_json;

	g_groundResistanceSettings.EnableMovementResistance =
		o_json.value("EnableMovementResistance", true);
	g_groundResistanceSettings.EnablePlayerResistance =
		o_json.value("EnablePlayerMovementResistance", true);
	g_groundResistanceSettings.EnableNPCResistance =
		o_json.value("EnableNPCMovementResistance", true);
	g_groundResistanceSettings.SnowResistanceStrength =
		o_json.value("SnowResistanceStrength", 1.0f);
	g_groundResistanceSettings.FluffySnowResistanceStrength =
		o_json.value("FluffySnowResistanceStrength", 0.36f);
	g_groundResistanceSettings.MudResistanceStrength =
		o_json.value("MudResistanceStrength", 0.82f);
	g_groundResistanceSettings.MudMinimumSpeedScale =
		o_json.value("MudMinimumSpeedScale", 0.62f);
	g_groundResistanceSettings.MinimumSurfaceSpeedScale =
		o_json.value("MinimumSurfaceSpeedScale", 0.32f);
	g_groundResistanceSettings.SnowResistanceStartDepth =
		o_json.value("SnowResistanceStartDepth", 1.0f);
	g_groundResistanceSettings.SnowFullResistanceDepth =
		o_json.value("SnowFullResistanceDepth", 68.0f);
	g_groundResistanceSettings.ResistanceResponseRate =
		o_json.value("MovementResistanceResponseRate", 6.5f);

	// 13M movement tuning migration. Preserve manual values and only update
	// exact historical defaults.
	const int resistanceTuningVersion =
		o_json.value("MovementResistanceTuningVersion", 1);
	if (resistanceTuningVersion < 4) {
		if (std::abs(g_groundResistanceSettings.FluffySnowResistanceStrength - 0.30f) < 1.0e-4f ||
			std::abs(g_groundResistanceSettings.FluffySnowResistanceStrength - 0.34f) < 1.0e-4f)
			g_groundResistanceSettings.FluffySnowResistanceStrength = 0.36f;
		if (std::abs(g_groundResistanceSettings.MudResistanceStrength - 0.70f) < 1.0e-4f)
			g_groundResistanceSettings.MudResistanceStrength = 0.82f;
		if (std::abs(g_groundResistanceSettings.MinimumSurfaceSpeedScale - 0.42f) < 1.0e-4f ||
			std::abs(g_groundResistanceSettings.MinimumSurfaceSpeedScale - 0.34f) < 1.0e-4f)
			g_groundResistanceSettings.MinimumSurfaceSpeedScale = 0.32f;
		if (std::abs(g_groundResistanceSettings.SnowResistanceStartDepth - 8.0f) < 1.0e-4f ||
			std::abs(g_groundResistanceSettings.SnowResistanceStartDepth - 2.0f) < 1.0e-4f)
			g_groundResistanceSettings.SnowResistanceStartDepth = 1.0f;
		if (std::abs(g_groundResistanceSettings.SnowFullResistanceDepth - 58.0f) < 1.0e-4f)
			g_groundResistanceSettings.SnowFullResistanceDepth = 68.0f;
		if (std::abs(g_groundResistanceSettings.ResistanceResponseRate - 5.5f) < 1.0e-4f)
			g_groundResistanceSettings.ResistanceResponseRate = 6.5f;
	}
	g_groundResistanceSettings.ResistanceUpdateInterval =
		o_json.value("MovementResistanceUpdateInterval", 0.075f);
	g_groundResistanceSettings.DebugMovementResistance =
		o_json.value("DebugMovementResistance", false);

	// Migrate only the exact v2.1 geometry defaults. User-tuned values are left
	// untouched. The wider range still stays inside the 4096-unit interaction
	// field, while far tessellation remains 1x for cost control.
	if (std::abs(settings.GeometryRenderDistance - 1536.0f) < 0.5f &&
		std::abs(settings.GeometryFadeStart - 1152.0f) < 0.5f) {
		settings.GeometryRenderDistance = 1920.0f;
		settings.GeometryFadeStart = 1600.0f;
	}
	if (std::abs(settings.GeometryTessellationNearDistance - 160.0f) < 0.5f &&
		std::abs(settings.GeometryTessellationFarDistance - 960.0f) < 0.5f) {
		settings.GeometryTessellationNearDistance = 224.0f;
		settings.GeometryTessellationFarDistance = 1280.0f;
	}
	// PIXL_GR_13Y_THREE_STAGE_LOD_MIGRATION_V1
	if (std::abs(settings.GeometryRenderDistance - 1920.0f) < 0.5f &&
		std::abs(settings.GeometryFadeStart - 1600.0f) < 0.5f) {
		settings.GeometryRenderDistance = 4000.0f;
		settings.GeometryFadeStart = 3400.0f;
	}
	if (std::abs(settings.GeometryTessellationNear - 8.0f) < 1.0e-4f &&
		std::abs(settings.GeometryTessellationFar - 1.0f) < 1.0e-4f) {
		settings.GeometryTessellationNear = 12.0f;
		settings.GeometryTessellationFar = 6.0f;
	}
	if (std::abs(settings.GeometryTessellationNearDistance - 224.0f) < 0.5f &&
		std::abs(settings.GeometryTessellationFarDistance - 1280.0f) < 0.5f) {
		settings.GeometryTessellationNearDistance = 768.0f;
		settings.GeometryTessellationFarDistance = 2048.0f;
	}

	// Runtime settings reloads may happen after DataLoaded; refresh compatibility
	// IDs when the data handler is already available.
	if (RE::TESDataHandler::GetSingleton())
		GroundResolveCompatibilityOverrides(*this);
}

void GroundResponse::SaveSettings(json& o_json)
{
	o_json = settings;
	o_json["EnableMovementResistance"] =
		g_groundResistanceSettings.EnableMovementResistance;
	o_json["EnablePlayerMovementResistance"] =
		g_groundResistanceSettings.EnablePlayerResistance;
	o_json["EnableNPCMovementResistance"] =
		g_groundResistanceSettings.EnableNPCResistance;
	o_json["MovementResistanceTuningVersion"] = 4;
	o_json["SnowResistanceStrength"] =
		g_groundResistanceSettings.SnowResistanceStrength;
	o_json["FluffySnowResistanceStrength"] =
		g_groundResistanceSettings.FluffySnowResistanceStrength;
	o_json["MudResistanceStrength"] =
		g_groundResistanceSettings.MudResistanceStrength;
	o_json["MudMinimumSpeedScale"] =
		g_groundResistanceSettings.MudMinimumSpeedScale;
	o_json["MinimumSurfaceSpeedScale"] =
		g_groundResistanceSettings.MinimumSurfaceSpeedScale;
	o_json["SnowResistanceStartDepth"] =
		g_groundResistanceSettings.SnowResistanceStartDepth;
	o_json["SnowFullResistanceDepth"] =
		g_groundResistanceSettings.SnowFullResistanceDepth;
	o_json["MovementResistanceResponseRate"] =
		g_groundResistanceSettings.ResistanceResponseRate;
	o_json["MovementResistanceUpdateInterval"] =
		g_groundResistanceSettings.ResistanceUpdateInterval;
	o_json["DebugMovementResistance"] =
		g_groundResistanceSettings.DebugMovementResistance;
}

void GroundResponse::RestoreDefaultSettings()
{
	settings = {};
	g_groundResistanceSettings = {};
	g_alwaysCompressForms.clear();
	g_neverDeformForms.clear();
	ResistanceRestoreAll();
}

void GroundResponse::QueueProjectileImpact(
	RE::Projectile* a_projectile,
	const RE::NiPoint3& a_position,
	const RE::NiPoint3& a_velocity)
{
	if (!a_projectile ||
		!settings.EnableDeformableGround ||
		!settings.EnableProjectileInteraction) {
		return;
	}

	RE::NiPoint3 impact = a_position;
	if (!GroundFinitePoint(impact))
		impact = a_projectile->GetPosition();
	if (!GroundFinitePoint(impact))
		return;

	// Projectile impact processing may originate from the physics/update path.
	// Defer all LAND/Havok receiver queries to QueueCollisions on the main path.
	const bool arrow =
		a_projectile->formType == RE::FormType::ProjectileArrow;
	const auto& runtime = a_projectile->GetProjectileRuntimeData();

	float effectArea = 0.0f;
	GroundElementKind element = GroundElementKind::kNone;
	if (settings.EnableMagicInteraction) {
		effectArea = GroundProjectileEffectArea(runtime.spell);
		element = GroundClassifyMagicItem(runtime.spell);
		if (element == GroundElementKind::kNone)
			element = GroundClassifyEffect(runtime.avEffect);
		if (element == GroundElementKind::kNone &&
			a_projectile->formType == RE::FormType::ProjectileFlame) {
			element = GroundElementKind::kFire;
		}
	}

	const float velocityMagnitude =
		GroundFinitePoint(a_velocity)
			? std::sqrt(
				std::max(
					a_velocity.x * a_velocity.x +
					a_velocity.y * a_velocity.y +
					a_velocity.z * a_velocity.z,
					0.0f))
			: 0.0f;
	const float impactSignal =
		std::clamp(velocityMagnitude / 1200.0f, 0.0f, 1.0f);

	GroundPendingInteraction interaction{};
	interaction.start = interaction.end =
		float2{ impact.x, impact.y };
	interaction.receiverZ = impact.z;
	interaction.validateReceiver = true;
	interaction.elementalSnowOnly = true;
	interaction.source = GroundInteractionSource::kProjectile;
	interaction.sourceFormID = runtime.spell ? runtime.spell->GetFormID() : 0u;
	if (arrow) {
		const float horizontalVelocity =
			std::sqrt(
				std::max(
					a_velocity.x * a_velocity.x +
					a_velocity.y * a_velocity.y,
					0.0f));
		if (std::isfinite(horizontalVelocity) && horizontalVelocity > 1.0e-3f) {
			const float inverseHorizontal = 1.0f / horizontalVelocity;
			// The normalized deformation field is four world units per texel. The
			// old radius could collapse below one texel after filtering, making an
			// otherwise successful arrow/bolt hook visually disappear.
			const float punctureLength = 8.0f + impactSignal * 6.0f;
			interaction.start = float2{
				impact.x - a_velocity.x * inverseHorizontal * punctureLength,
				impact.y - a_velocity.y * inverseHorizontal * punctureLength
			};
		}
		interaction.startRadius =
			std::clamp(4.5f + impactSignal * 1.0f, 4.5f, 5.5f);
		interaction.endRadius =
			std::clamp(5.5f + impactSignal * 2.0f, 5.5f, 7.5f);
		interaction.strength =
			std::clamp(0.86f + impactSignal * 0.14f, 0.86f, 1.0f);
		interaction.displacementScale = 0.18f + impactSignal * 0.22f;
		interaction.contactDepth = 5.0f + impactSignal * 4.0f;
		interaction.priority = 92.0f;
	} else {
		const float areaRadius =
			6.0f + std::sqrt(std::max(effectArea, 0.0f)) * 2.2f;
		float fallbackRadius = 8.0f;
		if (a_projectile->formType == RE::FormType::ProjectileGrenade)
			fallbackRadius = 18.0f;
		else if (a_projectile->formType == RE::FormType::ProjectileFlame)
			fallbackRadius = 11.0f;
		else if (a_projectile->formType == RE::FormType::ProjectileCone)
			fallbackRadius = 14.0f;
		interaction.startRadius = interaction.endRadius =
			std::clamp(
				effectArea > 0.0f ? areaRadius : fallbackRadius,
				6.0f,
				48.0f);
		interaction.strength =
			std::clamp(0.48f + impactSignal * 0.42f, 0.48f, 0.92f);
		interaction.displacementScale =
			std::clamp(0.22f + impactSignal * 0.68f, 0.22f, 0.90f);
		interaction.contactDepth =
			4.0f + interaction.startRadius * 0.28f;
		interaction.priority = 82.0f;
	}

	if (settings.EnableMagicInteraction &&
		settings.EnableElementalSnow) {
		const float elementalScale =
			std::clamp(
				0.72f +
					interaction.startRadius / 64.0f +
					impactSignal * 0.22f,
				0.65f,
				1.35f);
		const float limit =
			std::clamp(settings.ElementalHeightLimit, 0.0f, ELEMENTAL_HARD_LIMIT);
		if (element == GroundElementKind::kFire) {
			interaction.elementalDelta =
				-std::min(
					std::max(settings.FireMeltUnits, 0.0f) * elementalScale,
					limit);
			interaction.smoothing = 0.82f;
			// Heat mostly melts/softens instead of kicking up a tall powder berm.
			interaction.displacementScale *= 0.45f;
			interaction.priority += 4.0f;
		} else if (element == GroundElementKind::kFrost) {
			interaction.elementalDelta =
				std::min(
					std::max(settings.FrostAddUnits, 0.0f) * elementalScale,
					limit);
			interaction.priority += 3.0f;
		} else if (element == GroundElementKind::kShock) {
			// Electrical destruction briefly sinters/melts the contact patch and
			// punches into loose powder without behaving like sustained flame heat.
			interaction.elementalDelta =
				-std::min(
					std::max(settings.FireMeltUnits, 0.0f) *
						0.24f * elementalScale,
					limit);
			interaction.smoothing = 0.28f;
			interaction.strength =
				std::max(interaction.strength, 0.64f);
			interaction.displacementScale =
				std::max(interaction.displacementScale, 0.32f);
			interaction.priority += 2.0f;
		}
	}

	GroundPushPendingInteraction(interaction);

	static std::atomic<std::uint32_t> s_projectileDiagCount{ 0u };
	const std::uint32_t diagIndex =
		s_projectileDiagCount.fetch_add(1u, std::memory_order_relaxed);
	if (diagIndex < 48u) {
		logger::debug(
			"[GR-PROJECTILE] queued type={} arrow={} spell={:08X} pos=({:.1f},{:.1f},{:.1f}) radius={:.1f}->{:.1f} strength={:.2f}",
			static_cast<std::uint32_t>(a_projectile->GetFormType()),
			arrow ? 1 : 0,
			interaction.sourceFormID,
			impact.x, impact.y, impact.z,
			interaction.startRadius, interaction.endRadius,
			interaction.strength);
	}
}


void GroundResponse::QueueMagicCast(
	RE::TESObjectREFR* a_caster,
	RE::FormID a_spellFormID,
	bool a_continuousTick)
{
	if (!a_caster ||
		!settings.EnableDeformableGround ||
		!settings.EnableMagicInteraction ||
		!settings.EnableElementalSnow) {
		return;
	}

	// TESSpellCastEvent also fires for shout variation spells. The dedicated shout
	// path already handles those with its authored tier/range logic, so never
	// double-stamp them as generic magic.
	if (g_shoutSpellCache.contains(a_spellFormID))
		return;

	auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(a_spellFormID);
	if (!spell)
		return;

	const GroundElementKind element = GroundClassifyMagicItem(spell);
	if (element == GroundElementKind::kNone)
		return;

	const auto delivery = spell->GetDelivery();
	if (delivery != RE::MagicSystem::Delivery::kAimed &&
		delivery != RE::MagicSystem::Delivery::kTargetLocation &&
		delivery != RE::MagicSystem::Delivery::kTargetActor) {
		return;
	}

	bool continuousOrBreath =
		spell->GetCastingType() == RE::MagicSystem::CastingType::kConcentration;
	const bool voicePower =
		spell->GetSpellType() == RE::MagicSystem::SpellType::kVoicePower;
	bool breathLike = voicePower;
	// Event fallback is intentionally narrower than the continuous caster path.
	// TESSpellCastEvent may bootstrap dragon/voice breath in cases where Skyrim's
	// caster state is short-lived, but ordinary concentration spells (Flames,
	// Frostbite) must never recreate the old one-shot shout-shaped stamp.
	bool explicitBreathEvent = voicePower;
	float projectileRange = 0.0f;

	// A number of concentration effects (Flames/Frostbite and many modded breath
	// effects) do not deliver useful LAND AddImpact callbacks. Detect the authored
	// projectile archetype from the magic effect and give those casts a direct
	// receiver-aware surface emitter. Discrete missiles remain impact-driven.
	for (const auto* effect : spell->effects) {
		if (!effect || !effect->baseEffect)
			continue;

		auto* projectile = effect->baseEffect->data.projectileBase;
		if (!projectile)
			continue;

		const bool surfaceStream =
			projectile->IsFlamethrower() ||
			projectile->IsCone() ||
			projectile->IsBeam();
		continuousOrBreath = continuousOrBreath || surfaceStream;
		breathLike =
			breathLike ||
			projectile->IsFlamethrower() ||
			projectile->IsCone();

		if (std::isfinite(projectile->data.range) &&
			projectile->data.range > 1.0f) {
			projectileRange =
				std::max(projectileRange, projectile->data.range);
		}
	}

	// Compatibility fallback for dragon/mod breath spells that use unusual
	// projectile records but retain a descriptive spell EditorID.
	if (const char* editorID = spell->GetFormEditorID(); editorID && *editorID) {
		const std::string spellText = GroundLowerText(editorID);
		if (spellText.find("breath") != std::string::npos ||
			spellText.find("dragonfire") != std::string::npos ||
			spellText.find("dragonfrost") != std::string::npos) {
			continuousOrBreath = true;
			breathLike = true;
			explicitBreathEvent = true;
		}
	}

	if (!continuousOrBreath)
		return;
	if (!a_continuousTick && !explicitBreathEvent)
		return;

	const RE::NiPoint3 casterPos = a_caster->GetPosition();
	if (!GroundFinitePoint(casterPos))
		return;

	// Start beyond most of the caster's own collision volume. This is especially
	// important for dragons, whose body can otherwise stop the LOS probe before
	// their breath ever reaches the ground.
	float casterRadius = 64.0f;
	if (auto* root = a_caster->Get3D()) {
		const float radius = root->worldBound.radius;
		if (std::isfinite(radius) && radius > 1.0f)
			casterRadius = radius;
	}

	const float startDistance =
		std::clamp(casterRadius * 0.72f, 28.0f, breathLike ? 180.0f : 96.0f);
	const float rayHeight =
		casterPos.z +
		std::clamp(casterRadius * 0.34f, 46.0f, breathLike ? 150.0f : 92.0f);

	float authoredRange = spell->GetRange();
	if (!std::isfinite(authoredRange) || authoredRange <= 1.0f)
		authoredRange = 0.0f;

	const float defaultRange = breathLike ? 820.0f : 520.0f;
	const float range =
		std::clamp(
			std::max({ authoredRange, projectileRange, defaultRange }),
			std::max(startDistance + 96.0f, 180.0f),
			1200.0f);

	constexpr float kDegToRad = 0.01745329251994329577f;
	const float halfAngle =
		(breathLike ? 18.0f : 9.0f) * kDegToRad;
	const int rayHalfCount = breathLike ? 2 : 1;
	const float baseAngle = a_caster->GetAngleZ();
	const float clearance =
		std::clamp(settings.ReceiverBlockerClearance, 2.0f, 12.0f);

	// Continuously-polled streams are deliberately weaker per ray than one projectile
	// impact because adjacent cone rays overlap near the caster. Repeated
	// concentration events converge smoothly into t103's existing bounded
	// elemental field instead of hitting the hard limit instantly.
	const float rayElementScale = breathLike ? 0.42f : 0.52f;
	const float limit =
		std::clamp(settings.ElementalHeightLimit, 0.0f, ELEMENTAL_HARD_LIMIT);
	std::uint32_t emittedRuns = 0u;

	for (int rayIndex = -rayHalfCount; rayIndex <= rayHalfCount; ++rayIndex) {
		const float angularT =
			rayHalfCount > 0
				? static_cast<float>(rayIndex) / static_cast<float>(rayHalfCount)
				: 0.0f;
		const float angle = baseAngle + halfAngle * angularT;
		const float dirX = std::sin(angle);
		const float dirY = std::cos(angle);

		const RE::NiPoint3 rayStart{
			casterPos.x + dirX * startDistance,
			casterPos.y + dirY * startDistance,
			rayHeight
		};
		const RE::NiPoint3 rayEnd{
			casterPos.x + dirX * range,
			casterPos.y + dirY * range,
			rayHeight
		};

		float obstructionFraction = 1.0f;
		if (GroundRaycastFraction(rayStart, rayEnd, obstructionFraction)) {
			obstructionFraction =
				std::clamp(obstructionFraction - 0.02f, 0.0f, 1.0f);
		}
		const float reachableDistance =
			startDistance +
			(range - startDistance) * obstructionFraction;
		if (reachableDistance <= startDistance + 16.0f)
			continue;

		const int sampleCount =
			std::clamp(
				static_cast<int>(
					std::ceil((reachableDistance - startDistance) / 56.0f)),
				3,
				18);

		auto elementFalloffAt = [&](float d) {
			const float normalized =
				std::clamp(
					(d - startDistance) /
						std::max(range - startDistance, 1.0f),
					0.0f,
					1.0f);
			return 1.0f -
				0.78f * ResistanceSmoothStep(0.08f, 1.0f, normalized);
		};

		auto emitReceiverRun =
			[&](float firstValidDistance, float lastValidDistance) {
				if (firstValidDistance < 0.0f ||
					lastValidDistance <= firstValidDistance + 4.0f) {
					return;
				}

				const float startFalloff = elementFalloffAt(firstValidDistance);
				const float endFalloff = elementFalloffAt(lastValidDistance);

				GroundPendingInteraction interaction{};
				interaction.start = float2{
					casterPos.x + dirX * firstValidDistance,
					casterPos.y + dirY * firstValidDistance
				};
				interaction.end = float2{
					casterPos.x + dirX * lastValidDistance,
					casterPos.y + dirY * lastValidDistance
				};
				interaction.startRadius =
					breathLike ? 10.0f : 8.0f;
				interaction.endRadius =
					std::clamp(
						interaction.startRadius +
							(lastValidDistance - firstValidDistance) *
								(breathLike ? 0.025f : 0.014f),
						interaction.startRadius,
						breathLike ? 34.0f : 22.0f);

				// Magic streams primarily modify snow mass rather than behaving
				// like a physical shove. Keep compaction subtle, with frost
				// retaining a little more displaced powder than heat.
				interaction.strength =
					(element == GroundElementKind::kFire
						? 0.12f
						: (element == GroundElementKind::kShock ? 0.22f : 0.18f)) *
					startFalloff;
				interaction.displacementScale =
					element == GroundElementKind::kFire
						? 0.035f
						: (element == GroundElementKind::kShock ? 0.18f : 0.14f);
				interaction.flags = SURFACE_STAMP_FLAG_DISTANCE_FALLOFF;
				interaction.contactDepth =
					std::clamp(
						endFalloff / std::max(startFalloff, 1.0e-3f),
						0.04f,
						1.0f);
				interaction.priority = breathLike ? 96.0f : 92.0f;
				interaction.elementalSnowOnly = true;
				interaction.source = GroundInteractionSource::kMagic;
				interaction.sourceFormID = a_spellFormID;

				if (element == GroundElementKind::kFire) {
					interaction.elementalDelta =
						-std::min(
							std::max(settings.FireMeltUnits, 0.0f) *
								rayElementScale * startFalloff,
							limit);
					interaction.smoothing =
						0.88f * startFalloff;
				} else if (element == GroundElementKind::kFrost) {
					interaction.elementalDelta =
						std::min(
							std::max(settings.FrostAddUnits, 0.0f) *
								rayElementScale * startFalloff,
							limit);
				} else {
					interaction.elementalDelta =
						-std::min(
							std::max(settings.FireMeltUnits, 0.0f) *
								0.18f * rayElementScale * startFalloff,
							limit);
					interaction.smoothing =
						0.24f * startFalloff;
				}

				GroundPushPendingInteraction(interaction);
				++emittedRuns;
			};

		float runStart = -1.0f;
		float runLast = -1.0f;
		for (int sampleIndex = 0; sampleIndex <= sampleCount; ++sampleIndex) {
			const float t =
				static_cast<float>(sampleIndex) /
				static_cast<float>(sampleCount);
			const float d =
				startDistance +
				(reachableDistance - startDistance) * t;
			const float2 xy{
				casterPos.x + dirX * d,
				casterPos.y + dirY * d
			};

			GroundSurfaceProbe probe{};
			const RE::NiPoint3 query{ xy.x, xy.y, casterPos.z };
			const bool validSnowReceiver =
				GroundProbeSurface(query, *this, probe) &&
				probe.snow &&
				!GroundHasRaisedBlocker(
					probe.landHeight,
					rayHeight,
					xy,
					clearance);

			if (validSnowReceiver) {
				if (runStart < 0.0f)
					runStart = d;
				runLast = d;
			} else if (runStart >= 0.0f) {
				emitReceiverRun(runStart, runLast);
				runStart = -1.0f;
				runLast = -1.0f;
			}
		}
		if (runStart >= 0.0f)
			emitReceiverRun(runStart, runLast);
	}

	if (emittedRuns > 0u) {
		static std::atomic<std::uint32_t> s_magicDiagCount{ 0u };
		const std::uint32_t diagIndex =
			s_magicDiagCount.fetch_add(1u, std::memory_order_relaxed);
		if (diagIndex < 48u) {
			logger::debug(
				"[GR-MAGIC-STREAM] spell={:08X} element={} breath={} runs={} caster={:08X}",
				a_spellFormID,
				GroundElementLabel(element),
				breathLike ? 1 : 0,
				emittedRuns,
				a_caster->GetFormID());
		}
	}
}


void GroundResponse::QueueShoutCast(
	RE::TESObjectREFR* a_caster,
	RE::FormID a_spellFormID)
{
	if (!a_caster ||
		!settings.EnableDeformableGround ||
		!settings.EnableShoutInteraction) {
		return;
	}

	const auto infoIt = g_shoutSpellCache.find(a_spellFormID);
	if (infoIt == g_shoutSpellCache.end())
		return;
	const GroundShoutSpellInfo info = infoIt->second;

	const RE::NiPoint3 casterPos = a_caster->GetPosition();
	if (!GroundFinitePoint(casterPos))
		return;

	const float tier =
		static_cast<float>(std::clamp<int>(static_cast<int>(info.tier), 1, 3));
	const float range =
		std::clamp(
			info.range > 1.0f ? info.range : (360.0f + tier * 180.0f),
			240.0f,
			1200.0f);
	constexpr float kDegToRad = 0.01745329251994329577f;
	const float halfAngle =
		(14.0f + tier * 3.0f) * kDegToRad;
	const float baseAngle = a_caster->GetAngleZ();
	const float rayHeight = casterPos.z + 52.0f;
	const float startDistance = 30.0f;
	const float clearance =
		std::clamp(settings.ReceiverBlockerClearance, 2.0f, 12.0f);

	for (int rayIndex = -2; rayIndex <= 2; ++rayIndex) {
		const float angularT = static_cast<float>(rayIndex) * 0.5f;
		const float angle = baseAngle + halfAngle * angularT;
		const float dirX = std::sin(angle);
		const float dirY = std::cos(angle);

		const RE::NiPoint3 rayStart{
			casterPos.x + dirX * startDistance,
			casterPos.y + dirY * startDistance,
			rayHeight
		};
		RE::NiPoint3 rayEnd{
			casterPos.x + dirX * range,
			casterPos.y + dirY * range,
			rayHeight
		};

		float obstructionFraction = 1.0f;
		if (GroundRaycastFraction(rayStart, rayEnd, obstructionFraction)) {
			// Leave a small gap before collision geometry so the analytical mask
			// never reaches through the wall that stopped the shout.
			obstructionFraction =
				std::clamp(obstructionFraction - 0.025f, 0.0f, 1.0f);
		}
		const float reachableDistance =
			startDistance + (range - startDistance) * obstructionFraction;
		if (reachableDistance <= startDistance + 12.0f)
			continue;

		// Rare-event CPU sampling prevents the cone from writing LAND underneath
		// roofs/bridges/platforms. Emit separate contiguous receiver runs instead
		// of joining the first and last valid samples; otherwise a low bridge in
		// the middle of the ray could be skipped and the analytical stamp would
		// reconnect straight through the protected terrain beneath it.
		const int sampleCount =
			std::clamp(
				static_cast<int>(
					std::ceil((reachableDistance - startDistance) / 64.0f)),
				3,
				18);

		auto emitReceiverRun =
			[&](float firstValidDistance, float lastValidDistance) {
				if (firstValidDistance < 0.0f ||
					lastValidDistance <= firstValidDistance + 4.0f) {
					return;
				}

				GroundPendingInteraction interaction{};
				interaction.start = float2{
					casterPos.x + dirX * firstValidDistance,
					casterPos.y + dirY * firstValidDistance
				};
				interaction.end = float2{
					casterPos.x + dirX * lastValidDistance,
					casterPos.y + dirY * lastValidDistance
				};
				interaction.startRadius = 8.0f + tier * 2.0f;
				const float halfWidth =
					lastValidDistance * std::tan(halfAngle);
				interaction.endRadius =
					std::clamp(
						halfWidth / 4.0f,
						interaction.startRadius,
						72.0f);

				// Preserve the shout's absolute distance attenuation even when a low
				// bridge splits one ray into multiple valid receiver runs. For flagged
				// stamps ContactDepth carries the end/start falloff ratio rather than a
				// physical penetration depth; SurfaceDeformationUpdateCS consumes it
				// only under STAMP_FLAG_DISTANCE_FALLOFF.
				auto shoutFalloffAt = [&](float d) {
					const float normalized = std::clamp(d / std::max(range, 1.0f), 0.0f, 1.0f);
					return 1.0f -
						0.88f * ResistanceSmoothStep(0.04f, 1.0f, normalized);
				};
				const float startFalloff = shoutFalloffAt(firstValidDistance);
				const float endFalloff = shoutFalloffAt(lastValidDistance);
				const float baseStrength =
					std::clamp(0.58f + tier * 0.13f, 0.70f, 0.98f);
				interaction.strength = baseStrength * startFalloff;
				interaction.displacementScale =
					std::clamp(0.62f + tier * 0.16f, 0.70f, 1.12f);
				interaction.flags = SURFACE_STAMP_FLAG_DISTANCE_FALLOFF;
				interaction.contactDepth =
					std::clamp(endFalloff / std::max(startFalloff, 1.0e-3f), 0.02f, 1.0f);
				interaction.priority = 100.0f + tier * 4.0f;
				interaction.source = GroundInteractionSource::kShout;
				interaction.sourceFormID = a_spellFormID;

				if (settings.EnableMagicInteraction &&
					settings.EnableElementalSnow) {
					const float limit =
						std::clamp(settings.ElementalHeightLimit, 0.0f, ELEMENTAL_HARD_LIMIT);
					if (info.element == GroundElementKind::kFire) {
						interaction.elementalDelta =
							-std::min(
								std::max(settings.FireMeltUnits, 0.0f) *
									(0.75f + tier * 0.18f) * startFalloff,
								limit);
						interaction.smoothing = 0.92f * startFalloff;
					} else if (info.element == GroundElementKind::kFrost) {
						interaction.elementalDelta =
							std::min(
								std::max(settings.FrostAddUnits, 0.0f) *
									(0.75f + tier * 0.18f) * startFalloff,
								limit);
					}
				}

				GroundPushPendingInteraction(interaction);
			};

		float runStart = -1.0f;
		float runLast = -1.0f;
		for (int sampleIndex = 0; sampleIndex <= sampleCount; ++sampleIndex) {
			const float t =
				static_cast<float>(sampleIndex) /
				static_cast<float>(sampleCount);
			const float d =
				startDistance +
				(reachableDistance - startDistance) * t;
			const float2 xy{
				casterPos.x + dirX * d,
				casterPos.y + dirY * d
			};
			GroundSurfaceProbe probe{};
			const RE::NiPoint3 query{ xy.x, xy.y, casterPos.z };
			const bool validReceiver =
				GroundProbeSurface(query, *this, probe) &&
				!GroundHasRaisedBlocker(
					probe.landHeight,
					rayHeight,
					xy,
					clearance);

			if (validReceiver) {
				if (runStart < 0.0f)
					runStart = d;
				runLast = d;
			} else if (runStart >= 0.0f) {
				emitReceiverRun(runStart, runLast);
				runStart = -1.0f;
				runLast = -1.0f;
			}
		}
		if (runStart >= 0.0f)
			emitReceiverRun(runStart, runLast);
	}
}

RE::BSEventNotifyControl GroundResponse::SpellCastEventSink::ProcessEvent(
	const RE::TESSpellCastEvent* a_event,
	RE::BSTEventSource<RE::TESSpellCastEvent>*)
{
	if (a_event && a_event->object) {
		auto& groundResponse = globals::pipeline::groundResponse;
		static std::atomic<std::uint32_t> s_spellEventDiagCount{ 0u };
		const auto eventDiag =
			s_spellEventDiagCount.fetch_add(1u, std::memory_order_relaxed);
		if (eventDiag < 64u) {
			logger::debug(
				"[GR-SPELL-EVENT] caster={:08X} spell={:08X}",
				a_event->object->GetFormID(),
				a_event->spell);
		}
		// Concentration magic is driven from live MagicCaster state in
		// QueueCollisions. The event remains authoritative for shouts, and provides
		// only a narrow dragon/voice-breath bootstrap; QueueMagicCast rejects normal
		// Flames/Frostbite here so their old one-shot shout-shaped mask cannot return.
		groundResponse.QueueShoutCast(
			a_event->object.get(),
			a_event->spell);
		groundResponse.QueueMagicCast(
			a_event->object.get(),
			a_event->spell,
			false);
	}
	return RE::BSEventNotifyControl::kContinue;
}

void GroundResponse::DataLoaded()
{
	static bool registered = false;
	if (!registered) {
		if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
			holder->AddEventSink<RE::TESSpellCastEvent>(
				SpellCastEventSink::GetSingleton());
			registered = true;
		}
	}

	GroundRebuildShoutSpellCache();
	GroundResolveCompatibilityOverrides(*this);
	logger::info(
		"[GroundResponse] interaction data loaded: spellSink={} shouts={} alwaysCompress={} neverDeform={}",
		registered ? 1 : 0,
		g_shoutSpellCache.size(),
		g_alwaysCompressForms.size(),
		g_neverDeformForms.size());
	logger::info(
		"[GroundResponse] interaction toggles: projectile={} magic={} elemental={} shout={}",
		settings.EnableProjectileInteraction ? 1 : 0,
		settings.EnableMagicInteraction ? 1 : 0,
		settings.EnableElementalSnow ? 1 : 0,
		settings.EnableShoutInteraction ? 1 : 0);
}

void GroundResponse::PostPostLoad()
{
	logger::debug("[GroundResponse] v3.0.13P authoritative-raised-depth build ACTIVE");
	logger::debug("PIXL GroundResponse 13BH reliable-projectiles + live-elemental-magic build ACTIVE");
	Hooks::Install();
}

void GroundResponse::CaptureDirectionalShadowAtlas(bool a_captureFocusShadow)
{
	auto* state = globals::state;
	if (!settings.EnableDeformableGround ||
		!settings.EnableGeometricSnow ||
		!state ||
		!state->HasDirectionalShadows()) {
		directionalShadowAtlasCaptured = false;
		return;
	}

	// PIXL_GR_13BG_EXACT_UTILITY_SHADOW_CAPTURE_V1
	// State::Draw calls this while Utility/RenderShadowmask is active. Capture the
	// same resources that Utility.hlsl consumes BEFORE any later module changes PS
	// state: t4 atlas, b0 technique splits/filter radius, b2 projection matrices
	// and s4 comparison sampler. This makes the displaced shell's detailed sun
	// shadow use the actual Skyrim/PIXL shadow-mask data instead of approximating it
	// from VolumeOcclusion's lower-frequency VSM representation.
	auto* context = globals::d3d::context;
	winrt::com_ptr<ID3D11ShaderResourceView> liveAtlasSRV;
	winrt::com_ptr<ID3D11ShaderResourceView> liveFocusStencilSRV;
	winrt::com_ptr<ID3D11ShaderResourceView> liveFocusShadowSRV;
	winrt::com_ptr<ID3D11Buffer> liveTechniqueCB;
	winrt::com_ptr<ID3D11Buffer> liveGeometryCB;
	winrt::com_ptr<ID3D11SamplerState> liveComparisonSampler;
	winrt::com_ptr<ID3D11SamplerState> liveFocusComparisonSampler;
	context->PSGetShaderResources(4, 1, liveAtlasSRV.put());
	context->PSGetShaderResources(5, 1, liveFocusStencilSRV.put());
	context->PSGetShaderResources(6, 1, liveFocusShadowSRV.put());
	context->PSGetConstantBuffers(0, 1, liveTechniqueCB.put());
	context->PSGetConstantBuffers(2, 1, liveGeometryCB.put());
	context->PSGetSamplers(4, 1, liveComparisonSampler.put());
	context->PSGetSamplers(6, 1, liveFocusComparisonSampler.put());

	if (!liveAtlasSRV || !liveTechniqueCB || !liveGeometryCB ||
		!liveComparisonSampler) {
		directionalShadowAtlasCaptured = false;
		directionalFocusShadowCaptured = false;
		return;
	}

	// Utility PerTechnique requires c0-c4 (80 bytes). Its directional PerGeometry
	// path reaches ShadowMapProj[2] at c22-c24, so c0-c24 = 400 bytes minimum.
	const bool atlasCopied =
		GroundCopyShadowTarget(
			liveAtlasSRV.get(),
			"GroundResponse::DirectionalShadowAtlasCopy",
			directionalShadowAtlasCopyTexture,
			directionalShadowAtlasCopySRV);
	const bool techniqueCopied =
		GroundCopyConstantBufferToRawSRV(
			liveTechniqueCB.get(),
			5u * 16u,
			"GroundResponse::UtilityShadowTechniqueCopy",
			directionalShadowTechniqueCopyBuffer,
			directionalShadowTechniqueCopySRV);
	const bool geometryCopied =
		GroundCopyConstantBufferToRawSRV(
			liveGeometryCB.get(),
			25u * 16u,
			"GroundResponse::UtilityShadowGeometryCopy",
			directionalShadowGeometryCopyBuffer,
			directionalShadowGeometryCopySRV);

	if (atlasCopied && techniqueCopied && geometryCopied) {
		// Preserve Utility's exact hardware comparison/filter/address state rather
		// than approximating it with a GroundResponse-created sampler.
		directionalShadowComparisonSampler = liveComparisonSampler;
	}

	// ShaderCache maps UtilityShaderFlags::LodLandscape to FOCUS_SHADOW for
	// RenderShadowmask. Only capture the expensive screen stencil/focus atlas on
	// that exact permutation; ordinary shadowmask draws may leave unrelated
	// resources in t5/t6/s6. Reset() clears this flag at frame start, so a later
	// non-focus shadowmask setup in the same frame cannot erase a valid focus copy.
	if (a_captureFocusShadow) {
		directionalFocusShadowCaptured = false;
		if (liveFocusStencilSRV && liveFocusShadowSRV && liveFocusComparisonSampler) {
			const bool stencilCopied =
				GroundCopyShadowTarget(
					liveFocusStencilSRV.get(),
					"GroundResponse::UtilityFocusStencilCopy",
					directionalFocusStencilCopyTexture,
					directionalFocusStencilCopySRV);
			const bool focusCopied =
				GroundCopyShadowTarget(
					liveFocusShadowSRV.get(),
					"GroundResponse::UtilityFocusShadowCopy",
					directionalFocusShadowCopyTexture,
					directionalFocusShadowCopySRV);
			if (stencilCopied && focusCopied) {
				directionalFocusShadowComparisonSampler = liveFocusComparisonSampler;
				directionalFocusShadowCaptured =
					directionalFocusShadowComparisonSampler != nullptr;
			}
		}
	}

	directionalShadowAtlasCaptured =
		atlasCopied && techniqueCopied && geometryCopied &&
		directionalShadowComparisonSampler;

	static int lastCaptureState = -1;
	static int lastFocusCaptureState = -1;
	const int captureState = directionalShadowAtlasCaptured ? 1 : 0;
	const int focusCaptureState = directionalFocusShadowCaptured ? 1 : 0;
	if (captureState != lastCaptureState ||
		focusCaptureState != lastFocusCaptureState) {
		lastCaptureState = captureState;
		lastFocusCaptureState = focusCaptureState;

		D3D11_TEXTURE2D_DESC atlasDesc{};
		if (directionalShadowAtlasCopyTexture)
			directionalShadowAtlasCopyTexture->GetDesc(&atlasDesc);
		D3D11_BUFFER_DESC techniqueDesc{};
		D3D11_BUFFER_DESC geometryDesc{};
		if (directionalShadowTechniqueCopyBuffer)
			directionalShadowTechniqueCopyBuffer->GetDesc(&techniqueDesc);
		if (directionalShadowGeometryCopyBuffer)
			directionalShadowGeometryCopyBuffer->GetDesc(&geometryDesc);

		logger::debug(
			"PIXL GroundResponse 13BG Utility-shadow capture={} focus={} atlas={}x{} slices={} b0={}B b2={}B",
			captureState,
			focusCaptureState,
			atlasDesc.Width,
			atlasDesc.Height,
			atlasDesc.ArraySize,
			techniqueDesc.ByteWidth,
			geometryDesc.ByteWidth);
	}
}

void GroundResponse::SetupResources()
{
	logger::debug("[GR-DIAG] SetupResources enter");
	logger::debug("PIXL GroundResponse v3.0.13AD displaced-snow two-stage ACTIVE");
	logger::debug("PIXL GroundResponse v3.1 animated-Havok/world-interactions ACTIVE");
	logger::debug("PIXL GroundResponse v3.0.13AL surface-material-filter ACTIVE");
	perFrame = new ConstantBuffer(ConstantBufferDesc<PerFrame>());
	surfacePerFrame = new ConstantBuffer(ConstantBufferDesc<SurfaceFieldData>());
	logger::debug("[GR-DIAG] SetupResources constant buffers ready perFrame={} surfacePerFrame={}",
		static_cast<const void*>(perFrame),
		static_cast<const void*>(surfacePerFrame));

	auto createRWTexture = [](uint width, uint height, const char* debugLabel) -> Texture2D* {
		D3D11_TEXTURE2D_DESC texDesc = {
			.Width = width,
			.Height = height,
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
			.SampleDesc = { .Count = 1 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS
		};

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = 1 }
		};

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		auto* texture = new Texture2D(texDesc);
		texture->CreateSRV(srvDesc);
		texture->CreateUAV(uavDesc);

		const float zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		globals::d3d::context->ClearUnorderedAccessViewFloat(texture->uav.get(), zero);
		logger::debug(
			"[GroundResponse] Created {} {}x{} RGBA16F interaction texture",
			debugLabel,
			width,
			height);
		return texture;
	};

	// Legacy camera-relative collider-height field used by grass only.
	collisionTexture =
		createRWTexture(
			INTERACTION_TEXTURE_SIZE,
			INTERACTION_TEXTURE_SIZE,
			"grass");

	// New absolute-world XY normalized field used by BOTH snow and mud.
	surfaceDeformationTexture =
		createRWTexture(
			SURFACE_TEXTURE_SIZE,
			SURFACE_TEXTURE_SIZE,
			"surface deformation");
	surfaceDisplacementTexture =
		createRWTexture(
			SURFACE_TEXTURE_SIZE,
			SURFACE_TEXTURE_SIZE,
			"surface displacement");
	surfaceElementalTexture =
		createRWTexture(
			SURFACE_TEXTURE_SIZE,
			SURFACE_TEXTURE_SIZE,
			"surface elemental snow");
	// createRWTexture initializes t103 to zero, so an initially disabled
	// elemental system does not need another full-resource clear on frame one.
	surfaceElementalClearedWhileDisabled = !settings.EnableElementalSnow;
	logger::debug(
		"[GR-DIAG] SetupResources textures ready grass={} surface={} displacement={} elemental={}",
		static_cast<const void*>(collisionTexture),
		static_cast<const void*>(surfaceDeformationTexture),
		static_cast<const void*>(surfaceDisplacementTexture),
		static_cast<const void*>(surfaceElementalTexture));

	// Full-resolution hardware comparison PCF for the displaced hull. This sampler
	// is bound only around a GroundResponse geometric terrain replay (PS s7).
	{
		D3D11_SAMPLER_DESC shadowSamplerDesc{};
		shadowSamplerDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
		shadowSamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		shadowSamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		shadowSamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		shadowSamplerDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
		shadowSamplerDesc.MinLOD = 0.0f;
		shadowSamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		DX::ThrowIfFailed(
			globals::d3d::device->CreateSamplerState(
				&shadowSamplerDesc,
				directionalShadowComparisonSampler.put()));
		Util::SetResourceName(
			directionalShadowComparisonSampler.get(),
			"GroundResponse::DirectionalShadowComparisonSampler");
	}

	{
		D3D11_BUFFER_DESC sbDesc{};
		sbDesc.Usage = D3D11_USAGE_DYNAMIC;
		sbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		sbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		sbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		sbDesc.StructureByteStride = sizeof(BoundingBoxPacked);
		sbDesc.ByteWidth = sizeof(BoundingBoxPacked) * MAX_BOUNDING_BOXES;
		collisionBoundingBoxes = eastl::make_unique<Buffer>(sbDesc, nullptr, "GroundResponse::BoundingBoxes");

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = MAX_BOUNDING_BOXES;
		collisionBoundingBoxes->CreateSRV(srvDesc);
	}

	{
		D3D11_BUFFER_DESC sbDesc{};
		sbDesc.Usage = D3D11_USAGE_DYNAMIC;
		sbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		sbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		sbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		sbDesc.StructureByteStride = sizeof(float4);
		sbDesc.ByteWidth = sizeof(float4) * MAX_COLLISIONS;
		collisionInstances = eastl::make_unique<Buffer>(sbDesc, nullptr, "GroundResponse::Instances");

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = MAX_COLLISIONS;
		collisionInstances->CreateSRV(srvDesc);
	}

	{
		D3D11_BUFFER_DESC sbDesc{};
		sbDesc.Usage = D3D11_USAGE_DYNAMIC;
		sbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		sbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		sbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		sbDesc.StructureByteStride = sizeof(SurfaceStampBoxPacked);
		sbDesc.ByteWidth = sizeof(SurfaceStampBoxPacked) * MAX_SURFACE_STAMP_BOXES;
		surfaceStampBoxes = eastl::make_unique<Buffer>(sbDesc, nullptr, "GroundResponse::SurfaceStampBoxes");

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = MAX_SURFACE_STAMP_BOXES;
		surfaceStampBoxes->CreateSRV(srvDesc);
	}

	{
		D3D11_BUFFER_DESC sbDesc{};
		sbDesc.Usage = D3D11_USAGE_DYNAMIC;
		sbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		sbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		sbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		sbDesc.StructureByteStride = sizeof(SurfaceStampPacked);
		sbDesc.ByteWidth = sizeof(SurfaceStampPacked) * MAX_SURFACE_STAMPS;
		surfaceStamps = eastl::make_unique<Buffer>(sbDesc, nullptr, "GroundResponse::SurfaceStamps");

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = MAX_SURFACE_STAMPS;
		surfaceStamps->CreateSRV(srvDesc);
	}
	logger::debug("[GR-DIAG] SetupResources exit");
}

bool GroundResponse::HasShaderDefine(RE::BSShader::Type shaderType)
{
	switch (shaderType) {
	case RE::BSShader::Type::Grass:
	case RE::BSShader::Type::Lighting:
		return true;
	default:
		return false;
	}
}

void GroundResponse::Hooks::MainUpdate_QueueCollisions::thunk()
{
	static uint32_t s_hookDiagCount = 0u;
	const bool hookDiag = s_hookDiagCount < 16u;

	if (hookDiag)
		logger::debug("[GR-DIAG] MainUpdate hook enter #{}", s_hookDiagCount);

	func();

	if (hookDiag)
		logger::debug("[GR-DIAG] MainUpdate original returned; before QueueCollisions");

	globals::pipeline::groundResponse.QueueCollisions();

	// This marker is deliberately after QueueCollisions has completely returned.
	// If QueueCollisions logs its internal "exit" but this line never appears,
	// the fault is during function epilogue/local destruction rather than inside
	// the logged body.
	if (hookDiag)
		logger::debug("[GR-DIAG] MainUpdate QueueCollisions fully returned");

	// Gameplay resistance is evaluated only after the proven deformation queue
	// has fully returned. It cannot gate or mutate surface stamps/geometry.
	UpdateGroundMovementResistance();

	++s_hookDiagCount;

	if (hookDiag)
		logger::debug("[GR-DIAG] MainUpdate hook exit");
}

#ifndef SKYRIM_CROSS_VR
bool GroundResponse::Hooks::Projectile_ProcessImpacts::thunk(RE::Projectile* This)
{
	GroundQueueRuntimeProjectileImpacts(This, "base-process");
	return func(This);
}

bool GroundResponse::Hooks::MissileProjectile_ProcessImpacts::thunk(RE::MissileProjectile* This)
{
	GroundQueueRuntimeProjectileImpacts(This, "missile-process");
	return func(This);
}

bool GroundResponse::Hooks::ArrowProjectile_ProcessImpacts::thunk(RE::ArrowProjectile* This)
{
	GroundQueueRuntimeProjectileImpacts(This, "arrow-process");
	return func(This);
}

bool GroundResponse::Hooks::GrenadeProjectile_ProcessImpacts::thunk(RE::GrenadeProjectile* This)
{
	GroundQueueRuntimeProjectileImpacts(This, "grenade-process");
	return func(This);
}

bool GroundResponse::Hooks::BeamProjectile_ProcessImpacts::thunk(RE::BeamProjectile* This)
{
	GroundQueueRuntimeProjectileImpacts(This, "beam-process");
	return func(This);
}

bool GroundResponse::Hooks::FlameProjectile_ProcessImpacts::thunk(RE::FlameProjectile* This)
{
	GroundQueueRuntimeProjectileImpacts(This, "flame-process");
	return func(This);
}

bool GroundResponse::Hooks::ConeProjectile_ProcessImpacts::thunk(RE::ConeProjectile* This)
{
	GroundQueueRuntimeProjectileImpacts(This, "cone-process");
	return func(This);
}

void GroundResponse::Hooks::MissileProjectile_AddImpact::thunk(
	RE::MissileProjectile* This,
	RE::TESObjectREFR* a_ref,
	const RE::NiPoint3& a_targetLoc,
	const RE::NiPoint3& a_velocity,
	RE::hkpCollidable* a_collidable,
	std::int32_t a_arg6,
	std::uint32_t a_arg7)
{
	// Capture magic/projectile metadata before vanilla AddImpact mutates runtime impact state.
	globals::pipeline::groundResponse.QueueProjectileImpact(
		This, a_targetLoc, a_velocity);
	func(This, a_ref, a_targetLoc, a_velocity, a_collidable, a_arg6, a_arg7);
}

void GroundResponse::Hooks::ArrowProjectile_AddImpact::thunk(
	RE::ArrowProjectile* This,
	RE::TESObjectREFR* a_ref,
	const RE::NiPoint3& a_targetLoc,
	const RE::NiPoint3& a_velocity,
	RE::hkpCollidable* a_collidable,
	std::int32_t a_arg6,
	std::uint32_t a_arg7)
{
	// Capture magic/projectile metadata before vanilla AddImpact mutates runtime impact state.
	globals::pipeline::groundResponse.QueueProjectileImpact(
		This, a_targetLoc, a_velocity);
	func(This, a_ref, a_targetLoc, a_velocity, a_collidable, a_arg6, a_arg7);
}

void GroundResponse::Hooks::GrenadeProjectile_AddImpact::thunk(
	RE::GrenadeProjectile* This,
	RE::TESObjectREFR* a_ref,
	const RE::NiPoint3& a_targetLoc,
	const RE::NiPoint3& a_velocity,
	RE::hkpCollidable* a_collidable,
	std::int32_t a_arg6,
	std::uint32_t a_arg7)
{
	// Capture magic/projectile metadata before vanilla AddImpact mutates runtime impact state.
	globals::pipeline::groundResponse.QueueProjectileImpact(
		This, a_targetLoc, a_velocity);
	func(This, a_ref, a_targetLoc, a_velocity, a_collidable, a_arg6, a_arg7);
}

void GroundResponse::Hooks::BeamProjectile_AddImpact::thunk(
	RE::BeamProjectile* This,
	RE::TESObjectREFR* a_ref,
	const RE::NiPoint3& a_targetLoc,
	const RE::NiPoint3& a_velocity,
	RE::hkpCollidable* a_collidable,
	std::int32_t a_arg6,
	std::uint32_t a_arg7)
{
	// Capture magic/projectile metadata before vanilla AddImpact mutates runtime impact state.
	globals::pipeline::groundResponse.QueueProjectileImpact(
		This, a_targetLoc, a_velocity);
	func(This, a_ref, a_targetLoc, a_velocity, a_collidable, a_arg6, a_arg7);
}

void GroundResponse::Hooks::FlameProjectile_AddImpact::thunk(
	RE::FlameProjectile* This,
	RE::TESObjectREFR* a_ref,
	const RE::NiPoint3& a_targetLoc,
	const RE::NiPoint3& a_velocity,
	RE::hkpCollidable* a_collidable,
	std::int32_t a_arg6,
	std::uint32_t a_arg7)
{
	// Capture magic/projectile metadata before vanilla AddImpact mutates runtime impact state.
	globals::pipeline::groundResponse.QueueProjectileImpact(
		This, a_targetLoc, a_velocity);
	func(This, a_ref, a_targetLoc, a_velocity, a_collidable, a_arg6, a_arg7);
}

void GroundResponse::Hooks::ConeProjectile_AddImpact::thunk(
	RE::ConeProjectile* This,
	RE::TESObjectREFR* a_ref,
	const RE::NiPoint3& a_targetLoc,
	const RE::NiPoint3& a_velocity,
	RE::hkpCollidable* a_collidable,
	std::int32_t a_arg6,
	std::uint32_t a_arg7)
{
	// Capture magic/projectile metadata before vanilla AddImpact mutates runtime impact state.
	globals::pipeline::groundResponse.QueueProjectileImpact(
		This, a_targetLoc, a_velocity);
	func(This, a_ref, a_targetLoc, a_velocity, a_collidable, a_arg6, a_arg7);
}
#endif

void GroundResponse::Hooks::BSGrassShader_SetupGeometry::thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
{
	auto& groundResponse = globals::pipeline::groundResponse;

	// Update the interaction field before Skyrim prepares this grass draw.
	groundResponse.Update();

	// Skyrim applies its per-draw VS constants/resources here and can overwrite
	// PIXL bindings established by Renderer_ResetState.
	func(This, Pass, RenderFlags);

	// Rebind PIXL after vanilla setup, immediately before the actual draw.
	auto* state = globals::state;
	auto* context = globals::d3d::context;
	ID3D11Buffer* pixlBuffers[3] = {
		state->permutationCB->CB(),
		state->sharedDataCB->CB(),
		state->featureDataCB->CB()
	};
	context->VSSetConstantBuffers(4, 3, pixlBuffers);

	ID3D11ShaderResourceView* grassInteractionSRV =
		groundResponse.settings.EnableGroundResponse && groundResponse.collisionTexture
			? groundResponse.collisionTexture->srv.get()
			: nullptr;
	context->VSSetShaderResources(100, 1, &grassInteractionSRV);
}

void GroundResponse::ClearShaderCache()
{
	if (collisionUpdateCS)
		collisionUpdateCS->Release();
	collisionUpdateCS = nullptr;

	if (surfaceDeformationUpdateCS)
		surfaceDeformationUpdateCS->Release();
	surfaceDeformationUpdateCS = nullptr;

	if (terrainSurfaceHSCW)
		terrainSurfaceHSCW->Release();
	terrainSurfaceHSCW = nullptr;
	if (terrainSurfaceHSCCW)
		terrainSurfaceHSCCW->Release();
	terrainSurfaceHSCCW = nullptr;
	if (terrainSurfaceDS)
		terrainSurfaceDS->Release();
	terrainSurfaceDS = nullptr;

	for (auto& variant : terrainNoCullRasterizers) {
		if (variant.noCullState)
			variant.noCullState->Release();
		variant.noCullState = nullptr;
	}
	terrainNoCullRasterizers.clear();

	for (auto& variant : g_groundShellDepthStates) {
		if (variant.shellState)
			variant.shellState->Release();
		variant.shellState = nullptr;
	}
	g_groundShellDepthStates.clear();
}

ID3D11ComputeShader* GroundResponse::GetCollisionUpdateCS()
{
	if (!collisionUpdateCS) {
		logger::debug("Compiling CollisionUpdateCS");
		collisionUpdateCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\GroundResponse\\CollisionUpdateCS.hlsl", {}, "cs_5_0"));
	}
	return collisionUpdateCS;
}

ID3D11ComputeShader* GroundResponse::GetSurfaceDeformationUpdateCS()
{
	if (!surfaceDeformationUpdateCS) {
		logger::debug("[GR-DIAG] Surface CS compile begin");
		surfaceDeformationUpdateCS = static_cast<ID3D11ComputeShader*>(
			Util::CompileShader(
				L"Data\\Shaders\\GroundResponse\\SurfaceDeformationUpdateCS.hlsl",
				{},
				"cs_5_0"));
		logger::debug(
			"[GR-DIAG] Surface CS compile end shader={}",
			static_cast<const void*>(surfaceDeformationUpdateCS));
	}
	return surfaceDeformationUpdateCS;
}

ID3D11HullShader* GroundResponse::GetTerrainSurfaceHS(bool a_counterClockwise)
{
	auto*& shader = a_counterClockwise ? terrainSurfaceHSCCW : terrainSurfaceHSCW;
	if (!shader) {
		logger::debug(
			"Compiling GroundResponse TerrainSurface HS ({})",
			a_counterClockwise ? "CCW" : "CW");
		const std::vector<std::pair<const char*, const char*>> defines =
			a_counterClockwise
				? std::vector<std::pair<const char*, const char*>>{ { "PIXL_TERRAIN_OUTPUT_CCW", "1" } }
				: std::vector<std::pair<const char*, const char*>>{};
		shader = static_cast<ID3D11HullShader*>(
			Util::CompileShader(
				L"Data\\Shaders\\GroundResponse\\TerrainSurface.hlsl",
				defines,
				"hs_5_0",
				"HSMain"));
	}
	return shader;
}

ID3D11DomainShader* GroundResponse::GetTerrainSurfaceDS()
{
	if (!terrainSurfaceDS) {
		logger::debug("Compiling GroundResponse TerrainSurface DS");
		terrainSurfaceDS = static_cast<ID3D11DomainShader*>(
			Util::CompileShader(
				L"Data\\Shaders\\GroundResponse\\TerrainSurface.hlsl",
				{},
				"ds_5_0",
				"DSMain"));
	}
	return terrainSurfaceDS;
}


ID3D11RasterizerState* GroundResponse::GetTerrainNoCullRasterizer(const D3D11_RASTERIZER_DESC& a_sourceDesc)
{
	auto sameDesc = [](const D3D11_RASTERIZER_DESC& a, const D3D11_RASTERIZER_DESC& b) {
		return a.FillMode == b.FillMode &&
			a.CullMode == b.CullMode &&
			a.FrontCounterClockwise == b.FrontCounterClockwise &&
			a.DepthBias == b.DepthBias &&
			a.DepthBiasClamp == b.DepthBiasClamp &&
			a.SlopeScaledDepthBias == b.SlopeScaledDepthBias &&
			a.DepthClipEnable == b.DepthClipEnable &&
			a.ScissorEnable == b.ScissorEnable &&
			a.MultisampleEnable == b.MultisampleEnable &&
			a.AntialiasedLineEnable == b.AntialiasedLineEnable;
	};

	for (auto& variant : terrainNoCullRasterizers) {
		if (sameDesc(variant.sourceDesc, a_sourceDesc))
			return variant.noCullState;
	}

	D3D11_RASTERIZER_DESC noCullDesc = a_sourceDesc;
	noCullDesc.CullMode = D3D11_CULL_NONE;

	// PIXL_GR_13AP_FRONTFACE_CLASSIFICATION_FIX_V1
	//
	// The HS/DS geometry, tessellator output topology and 13X TBN compensation
	// are already verified and must remain untouched. The remaining failure is
	// specifically face classification: the gameplay-visible TOP of the no-cull
	// shell is arriving at Lighting as the opposite SV_IsFrontFace side, while
	// the underside is the opaque side.
	//
	// CullMode is NONE, so FrontCounterClockwise has no effect on which triangles
	// are rasterized. Flipping only this bit changes the D3D11 front/back
	// classification (and therefore SV_IsFrontFace) without changing vertices,
	// tessellation, displacement, depth bias, drift shape, compression, or the
	// TerrainSeam geometry bridge.
	noCullDesc.FrontCounterClockwise =
		a_sourceDesc.FrontCounterClockwise == FALSE ? TRUE : FALSE;

	logger::info(
		"PIXL GroundResponse v3.0.13AP frontface-classification ACTIVE sourceFrontCCW={} shellFrontCCW={}",
		a_sourceDesc.FrontCounterClockwise != FALSE,
		noCullDesc.FrontCounterClockwise != FALSE);

	// The raised snow/mud shell is rendered against Skyrim/TerrainSeam's
	// already-populated vanilla terrain depth. At long range or grazing camera
	// angles, a physically small vertical raise (especially the 1.5-5u mud
	// shell and the final snow fade) can quantize to the same depth value as
	// the base landscape. The result is the angle-dependent dark striping seen
	// in the distance.
	//
	// Use the rasterizer's native depth bias for the shell replay instead of
	// changing geometry/fade distances. This leaves WorldPosition, motion
	// vectors, material classification, tessellation and track coordinates
	// untouched. Negative bias is toward the camera because the replay uses
	// LESS_EQUAL depth testing.
	constexpr INT kShellDepthBias = -8;
	constexpr FLOAT kShellSlopeScaledDepthBias = -0.25f;
	constexpr FLOAT kShellDepthBiasClamp = -1.0e-5f;

	noCullDesc.DepthBias += kShellDepthBias;
	noCullDesc.SlopeScaledDepthBias += kShellSlopeScaledDepthBias;

	// Preserve a source clamp when it is already stricter in the negative
	// direction; otherwise cap the shell assist so steep/grazing terrain cannot
	// be pulled excessively toward the camera.
	if (noCullDesc.DepthBiasClamp == 0.0f ||
		noCullDesc.DepthBiasClamp < kShellDepthBiasClamp) {
		noCullDesc.DepthBiasClamp = kShellDepthBiasClamp;
	}

	ID3D11RasterizerState* noCullState = nullptr;
	const HRESULT hr = globals::d3d::device->CreateRasterizerState(&noCullDesc, &noCullState);
	if (FAILED(hr) || !noCullState) {
		logger::warn("[GroundResponse] Failed to create terrain no-cull rasterizer state (HRESULT 0x{:08X})", static_cast<uint32_t>(hr));
		return nullptr;
	}

	terrainNoCullRasterizers.push_back({ a_sourceDesc, noCullState });
	logger::debug(
		"[GroundResponse] Created shell rasterizer: depthBias={} slopeBias={:.3f} clamp={:.8f}",
		noCullDesc.DepthBias,
		noCullDesc.SlopeScaledDepthBias,
		noCullDesc.DepthBiasClamp);
	return noCullState;
}


static bool GroundShellSameStencilOp(
	const D3D11_DEPTH_STENCILOP_DESC& a,
	const D3D11_DEPTH_STENCILOP_DESC& b)
{
	return
		a.StencilFailOp == b.StencilFailOp &&
		a.StencilDepthFailOp == b.StencilDepthFailOp &&
		a.StencilPassOp == b.StencilPassOp &&
		a.StencilFunc == b.StencilFunc;
}

static bool GroundShellSameDepthDesc(
	const D3D11_DEPTH_STENCIL_DESC& a,
	const D3D11_DEPTH_STENCIL_DESC& b)
{
	return
		a.DepthEnable == b.DepthEnable &&
		a.DepthWriteMask == b.DepthWriteMask &&
		a.DepthFunc == b.DepthFunc &&
		a.StencilEnable == b.StencilEnable &&
		a.StencilReadMask == b.StencilReadMask &&
		a.StencilWriteMask == b.StencilWriteMask &&
		GroundShellSameStencilOp(a.FrontFace, b.FrontFace) &&
		GroundShellSameStencilOp(a.BackFace, b.BackFace);
}

static ID3D11DepthStencilState* GroundShellGetAuthoritativeDepthState(
	const D3D11_DEPTH_STENCIL_DESC& sourceDesc)
{
	for (auto& variant : g_groundShellDepthStates) {
		if (GroundShellSameDepthDesc(variant.sourceDesc, sourceDesc))
			return variant.shellState;
	}

	D3D11_DEPTH_STENCIL_DESC shellDesc = sourceDesc;

	// The tessellated landscape is now the physical surface. Make that surface
	// authoritative for every draw that follows the delayed TerrainSeam replay.
	shellDesc.DepthEnable = TRUE;
	shellDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	shellDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;

	ID3D11DepthStencilState* shellState = nullptr;
	const HRESULT hr =
		globals::d3d::device->CreateDepthStencilState(
			&shellDesc,
			&shellState);

	if (FAILED(hr) || !shellState) {
		logger::warn(
			"[GroundResponse] 13P failed to create authoritative shell depth state (HRESULT 0x{:08X})",
			static_cast<std::uint32_t>(hr));
		return nullptr;
	}

	g_groundShellDepthStates.push_back({ sourceDesc, shellState });

	logger::info(
		"[GroundResponse] 13P shell depth state created: srcEnable={} srcWrite={} srcFunc={} stencil={}",
		sourceDesc.DepthEnable ? 1 : 0,
		static_cast<std::uint32_t>(sourceDesc.DepthWriteMask),
		static_cast<std::uint32_t>(sourceDesc.DepthFunc),
		sourceDesc.StencilEnable ? 1 : 0);

	return shellState;
}


void GroundResponse::FlushGeometryTelemetry()
{
	if ((!settings.DebugInteractionField && !settings.GeometrySelfTest) ||
		geometryTelemetry.TerrainPasses == 0u) {
		geometryTelemetry = {};
		return;
	}

	logger::info(
		"[GroundResponse Geometry] passes={} classified={} mf={} snowBearing={} wanted={} hsds={} topologyReject={} existingTess={} shaderFail={} boxes={} playerProxy={} fieldReset={} cameraGuard={} debug={} selfTest={} thickness={:.1f} mudThickness={:.2f}",
		geometryTelemetry.TerrainPasses,
		geometryTelemetry.ClassificationValid,
		geometryTelemetry.MaterialForgePasses,
		geometryTelemetry.SnowBearingPasses,
		geometryTelemetry.GeometryWanted,
		geometryTelemetry.HsDsApplied,
		geometryTelemetry.TopologyRejected,
		geometryTelemetry.ExistingTessellation,
		geometryTelemetry.ShaderFailure,
		currentPerFrame.BoundingBoxCount,
		geometryTelemetry.PlayerProxyStamps,
		geometryTelemetry.ClipmapResets,
		geometryTelemetry.CameraRebaseSuppressed,
		settings.DebugInteractionField ? 1 : 0,
		settings.GeometrySelfTest ? 1 : 0,
		settings.SnowSurfaceThickness,
		std::clamp(
			std::max(
				settings.SnowSurfaceThickness * 0.30f,
				settings.MudMaximumDepth * 0.20f),
			1.5f,
			5.0f));

	geometryTelemetry = {};
}

void GroundResponse::Reset()
{
	// A completed GroundResponse terrain override owns AddRef'd D3D state and must
	// restore/release it through FinishTerrainPass().
	if (terrainOverrideApplied) {
		FinishTerrainPass();
	} else {
		// Invariant: the saved COM pointers are only legitimately owned while
		// terrainOverrideApplied is true. If a stale/corrupt non-null value survives
		// while no override is active, calling Release() on it is unsafe. Discard the
		// transient snapshot instead of dereferencing an object we do not own.
		//
		// This is intentionally a startup/reset safety path only; normal terrain
		// replay still restores and releases every AddRef through FinishTerrainPass().
		savedTerrainHS = nullptr;
		savedTerrainDS = nullptr;
		for (auto*& buffer : savedTerrainHSCB5to6)
			buffer = nullptr;
		for (auto*& buffer : savedTerrainHSCB12to13)
			buffer = nullptr;
		for (auto*& buffer : savedTerrainDSCB5to6)
			buffer = nullptr;
		for (auto*& buffer : savedTerrainDSCB12to13)
			buffer = nullptr;
		savedTerrainDSSRV101 = nullptr;
		savedTerrainDSSRV102 = nullptr;
		savedTerrainDSSRV103 = nullptr;
		savedTerrainPSSRV104 = nullptr;
		savedTerrainPSSRV105 = nullptr;
		savedTerrainPSSRV106 = nullptr;
		savedTerrainPSSRV107 = nullptr;
		savedTerrainPSSRV108 = nullptr;
		savedTerrainPSSRV109 = nullptr;
		savedTerrainPSSampler7 = nullptr;
		savedTerrainPSSampler8 = nullptr;
		savedTerrainRasterizer = nullptr;
		// Same transient-ownership rule as the other saved D3D state.
		// If no terrain override is active, never dereference a stale snapshot.
		g_savedTerrainBlendState = nullptr;
		g_savedTerrainBlendFactor[0] = 0.0f;
		g_savedTerrainBlendFactor[1] = 0.0f;
		g_savedTerrainBlendFactor[2] = 0.0f;
		g_savedTerrainBlendFactor[3] = 0.0f;
		g_savedTerrainSampleMask = 0xFFFFFFFFu;
		for (auto*& rtv : g_savedGroundResponseRTVs)
			rtv = nullptr;
		g_savedGroundResponseDSV = nullptr;
		g_groundResponseOMCaptured = false;
		g_groundResponseMainDepthRebound = false;
		g_savedTerrainDepthStencilState = nullptr;
		g_savedTerrainStencilRef = 0u;
	}

	float telemetryDelta = 0.0f;
	if (globals::game::deltaTime) {
		telemetryDelta = *globals::game::deltaTime;
		if (!std::isfinite(telemetryDelta) || telemetryDelta < 0.0f)
			telemetryDelta = 0.0f;
		telemetryDelta = std::min(telemetryDelta, MAX_GROUND_FRAME_DELTA);
	}
	geometryTelemetrySeconds += telemetryDelta;
	if (geometryTelemetrySeconds >= 1.0f) {
		FlushGeometryTelemetry();
		geometryTelemetrySeconds = 0.0f;
	}

	activeTerrainPass = false;
	terrainGeometryWanted = false;
	terrainOverrideApplied = false;
	terrainGeometryRenderedThisFrame = false;
	// Raw Utility shadow data is frame-local. Keep the owned GPU resources for
	// reuse, but require a fresh RenderShadowmask capture before any terrain replay
	// advertises the 13BG detailed-shadow path this frame.
	directionalShadowAtlasCaptured = false;
	directionalFocusShadowCaptured = false;

	// Any legitimately-owned snapshot was already handled by FinishTerrainPass()
	// above. Do not unconditionally Release transient pointers here: Windows WER
	// resolved the startup AV to Reset() dereferencing savedTerrainDSCB12to13[0]
	// while terrainOverrideApplied was false.
	savedTerrainTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
}

	bool GroundResponse::GetTerrainSnowFlags(
	RE::BSRenderPass* a_pass,
	std::array<float, 6>& a_flags,
	bool& a_materialForge) const
{
	a_flags.fill(0.0f);
	a_materialForge = false;

	if (!a_pass || !a_pass->shaderProperty || !a_pass->shaderProperty->material)
		return false;

	constexpr uint32_t LightingTechniqueStart = 0x4800002D;
	if (a_pass->passEnum < LightingTechniqueStart)
		return false;

	const uint32_t lightingTechnique =
		a_pass->passEnum - LightingTechniqueStart;
	const uint32_t lightingFlags =
		lightingTechnique & 0x00FFFFFFu;
	const uint32_t materialForgeFlag =
		static_cast<uint32_t>(
			SIE::ShaderCache::LightingShaderFlags::MaterialForge);

	a_materialForge =
		(lightingFlags & materialForgeFlag) != 0u;

	if (a_materialForge) {
		auto* material =
			static_cast<BSLightingShaderMaterialPBRLandscape*>(
				a_pass->shaderProperty->material);

		auto it =
			BSLightingShaderMaterialPBRLandscape::
				SnowMetadataByMaterial.find(material);

		if (it ==
				BSLightingShaderMaterialPBRLandscape::
					SnowMetadataByMaterial.end() ||
			!it->second.valid) {
			return false;
		}

		for (std::size_t i = 0; i < a_flags.size(); ++i) {
			const float value =
				it->second.textureIsSnow[i];

			a_flags[i] =
				GroundEncodeTerrainSurfaceClass(
					GroundMaterialForgeLandscapeDiffusePath(
						material, i),
					value,
					i,
					"MaterialForge");
		}

		return true;
	}

	auto* material =
		static_cast<RE::BSLightingShaderMaterialLandscape*>(
			a_pass->shaderProperty->material);

	for (std::size_t i = 0; i < a_flags.size(); ++i) {
		const float value =
			material->textureIsSnow[i];

		a_flags[i] =
			GroundEncodeTerrainSurfaceClass(
				GroundVanillaLandscapeDiffusePath(material, i),
				value,
				i,
				"Vanilla");
	}

	ResistanceCacheRenderedSnowTextures(material, a_flags);

	return true;
}

void GroundResponse::PrepareTerrainPass(RE::BSRenderPass* a_pass)
{
	// TerrainSeam calls FinishTerrainPass after every raw replay. If an engine-side
	// early exit ever breaks that pairing, restore before preparing another pass.
	if (terrainOverrideApplied)
		FinishTerrainPass();

	activeTerrainPass = false;
	terrainGeometryWanted = false;
	activeTerrainRuntime = currentPerFrame;
	activeTerrainRuntime.TerrainSnow1to4 = {};
	activeTerrainRuntime.TerrainSnow5to6 = {};
	activeTerrainRuntime.TerrainSnowValid = 0u;
	activeTerrainRuntime.TerrainGeometryPass = 0u;
	activeTerrainRuntime.TerrainMaterialForge = 0u;
	activeTerrainRuntime.TerrainDebug &=
		~(TERRAIN_DEBUG_RAW_DIRECTIONAL_SHADOW |
			TERRAIN_DEBUG_FOCUS_DIRECTIONAL_SHADOW);
	if (directionalShadowAtlasCaptured &&
		globals::state &&
		globals::state->HasDirectionalShadows()) {
		activeTerrainRuntime.TerrainDebug |=
			TERRAIN_DEBUG_RAW_DIRECTIONAL_SHADOW;
		if (directionalFocusShadowCaptured)
			activeTerrainRuntime.TerrainDebug |=
				TERRAIN_DEBUG_FOCUS_DIRECTIONAL_SHADOW;
	}

	if (!a_pass || !a_pass->geometry || !a_pass->shaderProperty || !perFrame)
		return;

	geometryTelemetry.TerrainPasses++;

	std::array<float, 6> snowFlags{};
	bool materialForge = false;
	const bool classificationValid =
		GetTerrainSnowFlags(a_pass, snowFlags, materialForge);
	if (classificationValid)
		geometryTelemetry.ClassificationValid++;
	if (materialForge)
		geometryTelemetry.MaterialForgePasses++;

	activeTerrainRuntime.TerrainSnow1to4 =
		float4{ snowFlags[0], snowFlags[1], snowFlags[2], snowFlags[3] };
	activeTerrainRuntime.TerrainSnow5to6 =
		float4{ snowFlags[4], snowFlags[5], 0.0f, 0.0f };
	activeTerrainRuntime.TerrainSnowValid = classificationValid ? 1u : 0u;
	activeTerrainRuntime.TerrainMaterialForge = materialForge ? 1u : 0u;

	const float strongestSnowLayer =
		*std::max_element(snowFlags.begin(), snowFlags.end());
	if (strongestSnowLayer > 1e-4f)
		geometryTelemetry.SnowBearingPasses++;
	const auto eye = Util::GetEyePosition();
	const float distanceToBound =
		std::max(
			a_pass->geometry->worldBound.center.GetDistance(eye) -
				a_pass->geometry->worldBound.radius,
			0.0f);

	// Apply one geometry decision to every nearby classified landscape pass, not
	// only passes containing snow. Skyrim terrain is layered; letting some passes
	// remain on the base plane while others move is a direct route to z-fighting.
	// Snow and mud therefore share the same HS/DS path and differ only in thickness.
	terrainGeometryWanted =
		classificationValid &&
		settings.EnableDeformableGround &&
		(settings.EnableSnowDeformation || settings.EnableMudDeformation) &&
		settings.EnableGeometricSnow &&
		distanceToBound <= activeTerrainRuntime.GeometryRenderDistance + 192.0f;

	if (terrainGeometryWanted)
		geometryTelemetry.GeometryWanted++;

	activeTerrainRuntime.TerrainGeometryPass =
		terrainGeometryWanted ? 1u : 0u;
	activeTerrainPass = true;

	perFrame->Update(activeTerrainRuntime);
}

void GroundResponse::TerrainPassShaderHacks()
{
	if (!activeTerrainPass || !perFrame)
		return;

	auto* context = globals::d3d::context;
	auto* state = globals::state;

	// Skyrim's Lighting setup runs after PrepareTerrainPass and may overwrite these
	// resources. Rebind immediately before the draw, even on non-tessellated terrain,
	// so PS classification/debug never sees stale data from a previous quad.
	ID3D11Buffer* runtimeCB = perFrame->CB();
	context->PSSetConstantBuffers(13, 1, &runtimeCB);

	// Keep the legacy collider-height field on PS t100 so cached pre-v3 Lighting
	// permutations cannot accidentally reinterpret the normalized surface map.
	// v3 snow/mud geometry is driven exclusively by the absolute-world t101 field.
	// Do NOT expose the legacy camera-relative collider-height texture to terrain PS
	// t100. Cached pre-v3 Lighting permutations still contain the old material
	// deformation path; feeding them t100 creates a dark camera-following mask even
	// though the actual v3 geometry/tracks remain correctly anchored in world space.
	ID3D11ShaderResourceView* nullLegacyTerrainPS = nullptr;
	ID3D11ShaderResourceView* surfaceSRV =
		settings.EnableDeformableGround && surfaceDeformationTexture
			? surfaceDeformationTexture->srv.get()
			: nullptr;
	ID3D11ShaderResourceView* displacedSnowSRV =
		settings.EnableDeformableGround && settings.EnableSnowDeformation && surfaceDisplacementTexture
			? surfaceDisplacementTexture->srv.get()
			: nullptr;
	ID3D11ShaderResourceView* elementalSnowSRV =
		settings.EnableDeformableGround && settings.EnableElementalSnow && surfaceElementalTexture
			? surfaceElementalTexture->srv.get()
			: nullptr;
	context->PSSetShaderResources(100, 1, &nullLegacyTerrainPS);
	context->PSSetShaderResources(101, 1, &surfaceSRV);

	if (!terrainGeometryWanted || terrainOverrideApplied)
		return;

	D3D11_PRIMITIVE_TOPOLOGY topology =
		D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
	context->IAGetPrimitiveTopology(&topology);
	if (topology != D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST) {
		geometryTelemetry.TopologyRejected++;
		// Never reinterpret strips or an existing patch topology. Fall back to the
		// regular terrain draw and force the PS out of geometric mode for this pass.
		activeTerrainRuntime.TerrainGeometryPass = 0u;
		terrainGeometryWanted = false;
		perFrame->Update(activeTerrainRuntime);
		context->PSSetConstantBuffers(13, 1, &runtimeCB);
		return;
	}

	context->HSGetShader(&savedTerrainHS, nullptr, nullptr);
	context->DSGetShader(&savedTerrainDS, nullptr, nullptr);
	if (savedTerrainHS || savedTerrainDS) {
		geometryTelemetry.ExistingTessellation++;
		// Do not stack tessellation systems. Release GetShader references and leave
		// the existing terrain stages untouched.
		if (savedTerrainHS) {
			savedTerrainHS->Release();
			savedTerrainHS = nullptr;
		}
		if (savedTerrainDS) {
			savedTerrainDS->Release();
			savedTerrainDS = nullptr;
		}
		activeTerrainRuntime.TerrainGeometryPass = 0u;
		terrainGeometryWanted = false;
		perFrame->Update(activeTerrainRuntime);
		context->PSSetConstantBuffers(13, 1, &runtimeCB);
		return;
	}

	// Match the tessellator's generated winding to the active rasterizer. Then
	// clone that exact state with culling disabled for the tessellated draw. A
	// displaced terrain triangle can flip/degenerate at grazing angles; keeping
	// back-face culling there is what produced the angle-dependent transparent holes.
	D3D11_RASTERIZER_DESC rasterizerDesc{};
	rasterizerDesc.FillMode = D3D11_FILL_SOLID;
	rasterizerDesc.CullMode = D3D11_CULL_BACK;
	rasterizerDesc.FrontCounterClockwise = FALSE;
	rasterizerDesc.DepthClipEnable = TRUE;

	ID3D11RasterizerState* rasterizerState = nullptr;
	context->RSGetState(&rasterizerState); // AddRef when non-null
	if (rasterizerState)
		rasterizerState->GetDesc(&rasterizerDesc);
	const bool counterClockwise = rasterizerDesc.FrontCounterClockwise != FALSE;

	ID3D11HullShader* hs = GetTerrainSurfaceHS(counterClockwise);
	ID3D11DomainShader* ds = GetTerrainSurfaceDS();
	ID3D11RasterizerState* noCullRasterizer = GetTerrainNoCullRasterizer(rasterizerDesc);
	if (!hs || !ds || !noCullRasterizer) {
		if (rasterizerState)
			rasterizerState->Release();
		geometryTelemetry.ShaderFailure++;
		activeTerrainRuntime.TerrainGeometryPass = 0u;
		terrainGeometryWanted = false;
		perFrame->Update(activeTerrainRuntime);
		context->PSSetConstantBuffers(13, 1, &runtimeCB);
		return;
	}

	savedTerrainTopology = topology;
	savedTerrainRasterizer = rasterizerState; // keep RSGetState reference until FinishTerrainPass

	// Preserve every HS/DS binding GroundResponse touches. Even with null stages,
	// another renderer module may have preloaded state for a later draw; restoring it
	// avoids hidden cross-module ordering dependencies. Get* calls AddRef these objects.
	context->HSGetConstantBuffers(5, 2, savedTerrainHSCB5to6);
	context->HSGetConstantBuffers(12, 2, savedTerrainHSCB12to13);
	context->DSGetConstantBuffers(5, 2, savedTerrainDSCB5to6);
	context->DSGetConstantBuffers(12, 2, savedTerrainDSCB12to13);
	context->DSGetShaderResources(101, 1, &savedTerrainDSSRV101);
	context->DSGetShaderResources(102, 1, &savedTerrainDSSRV102);
	context->DSGetShaderResources(103, 1, &savedTerrainDSSRV103);
	context->PSGetShaderResources(104, 1, &savedTerrainPSSRV104);
	context->PSGetShaderResources(105, 1, &savedTerrainPSSRV105);
	context->PSGetShaderResources(106, 1, &savedTerrainPSSRV106);
	context->PSGetShaderResources(107, 1, &savedTerrainPSSRV107);
	context->PSGetShaderResources(108, 1, &savedTerrainPSSRV108);
	context->PSGetShaderResources(109, 1, &savedTerrainPSSRV109);
	context->PSGetSamplers(7, 1, &savedTerrainPSSampler7);
	context->PSGetSamplers(8, 1, &savedTerrainPSSampler8);

	ID3D11Buffer* sharedCB = state->sharedDataCB->CB();
	ID3D11Buffer* featureCB = state->featureDataCB->CB();
	ID3D11Buffer* frameCB = *globals::game::perFrame;

	context->HSSetConstantBuffers(5, 1, &sharedCB);
	context->HSSetConstantBuffers(6, 1, &featureCB);
	context->HSSetConstantBuffers(12, 1, &frameCB);
	context->HSSetConstantBuffers(13, 1, &runtimeCB);
	context->DSSetConstantBuffers(5, 1, &sharedCB);
	context->DSSetConstantBuffers(6, 1, &featureCB);
	context->DSSetConstantBuffers(12, 1, &frameCB);
	context->DSSetConstantBuffers(13, 1, &runtimeCB);
	context->DSSetShaderResources(101, 1, &surfaceSRV);
	context->DSSetShaderResources(102, 1, &displacedSnowSRV);
	context->DSSetShaderResources(103, 1, &elementalSnowSRV);

	ID3D11ShaderResourceView* rawShadowAtlasSRV =
		directionalShadowAtlasCaptured
			? directionalShadowAtlasCopySRV.get()
			: nullptr;
	// t105 remains reserved for the old 13BF ESRAM copy but 13BG no longer
	// requires it. Bind null for the geometric replay and restore the prior state
	// afterward so no unrelated PIXL pass can leak into this slot.
	ID3D11ShaderResourceView* rawShadowEsramSRV = nullptr;
	ID3D11ShaderResourceView* rawShadowTechniqueSRV =
		directionalShadowAtlasCaptured
			? directionalShadowTechniqueCopySRV.get()
			: nullptr;
	ID3D11ShaderResourceView* rawShadowGeometrySRV =
		directionalShadowAtlasCaptured
			? directionalShadowGeometryCopySRV.get()
			: nullptr;
	ID3D11SamplerState* rawShadowSampler =
		directionalShadowAtlasCaptured
			? directionalShadowComparisonSampler.get()
			: nullptr;
	ID3D11ShaderResourceView* focusStencilSRV =
		directionalFocusShadowCaptured
			? directionalFocusStencilCopySRV.get()
			: nullptr;
	ID3D11ShaderResourceView* focusShadowSRV =
		directionalFocusShadowCaptured
			? directionalFocusShadowCopySRV.get()
			: nullptr;
	ID3D11SamplerState* focusShadowSampler =
		directionalFocusShadowCaptured
			? directionalFocusShadowComparisonSampler.get()
			: nullptr;
	context->PSSetShaderResources(104, 1, &rawShadowAtlasSRV);
	context->PSSetShaderResources(105, 1, &rawShadowEsramSRV);
	context->PSSetShaderResources(106, 1, &rawShadowTechniqueSRV);
	context->PSSetShaderResources(107, 1, &rawShadowGeometrySRV);
	context->PSSetShaderResources(108, 1, &focusStencilSRV);
	context->PSSetShaderResources(109, 1, &focusShadowSRV);
	context->PSSetSamplers(7, 1, &rawShadowSampler);
	context->PSSetSamplers(8, 1, &focusShadowSampler);

	// TerrainSeam's replay is alpha blended, but the displaced GroundResponse
	// shell is actual opaque terrain. If that blend state leaks into this draw,
	// actors/trees show through snow and the shell/vanilla overlap becomes a
	// moving dark smear band. Snapshot the replay blend state, use D3D11's
	// default opaque blend state for the shell draw, then restore in FinishTerrainPass.
	context->OMGetBlendState(
		&g_savedTerrainBlendState,
		g_savedTerrainBlendFactor,
		&g_savedTerrainSampleMask);
	context->OMSetBlendState(
		nullptr,
		g_savedTerrainBlendFactor,
		g_savedTerrainSampleMask);

	// 13P authoritative raised-depth write.
	//
	// TerrainSeam sets a writable depth state before replay, but Skyrim's own
	// per-pass setup can rebind OM state before the actual draw. GroundResponse's
	// bridge runs at the final pre-draw point, so snapshot what is REALLY active
	// here and force only the raised shell to write depth.
	context->OMGetDepthStencilState(
		&g_savedTerrainDepthStencilState,
		&g_savedTerrainStencilRef);

	D3D11_DEPTH_STENCIL_DESC sourceDepthDesc{};
	sourceDepthDesc.DepthEnable = TRUE;
	sourceDepthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	sourceDepthDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
	sourceDepthDesc.StencilEnable = FALSE;
	sourceDepthDesc.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
	sourceDepthDesc.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;

	if (g_savedTerrainDepthStencilState)
		g_savedTerrainDepthStencilState->GetDesc(&sourceDepthDesc);

	if (ID3D11DepthStencilState* shellDepthState =
			GroundShellGetAuthoritativeDepthState(sourceDepthDesc)) {
		context->OMSetDepthStencilState(
			shellDepthState,
			g_savedTerrainStencilRef);
	}

		// PIXL_GR_13AV_AUTHORITATIVE_MAIN_DEPTH_V1
	// Capture the actual output-merger targets at the LAST point before the
	// tessellated terrain draw is issued by the bridge.
	for (auto*& rtv : g_savedGroundResponseRTVs) {
		if (rtv) {
			rtv->Release();
			rtv = nullptr;
		}
	}
	if (g_savedGroundResponseDSV) {
		g_savedGroundResponseDSV->Release();
		g_savedGroundResponseDSV = nullptr;
	}

	g_groundResponseOMCaptured = false;
	g_groundResponseMainDepthRebound = false;

	context->OMGetRenderTargets(
		D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
		g_savedGroundResponseRTVs,
		&g_savedGroundResponseDSV);
	g_groundResponseOMCaptured = true;

	ID3D11DepthStencilView* grMainDSV = nullptr;
	if (auto* grRenderer = globals::game::renderer) {
		auto& grMainDepth =
			grRenderer->GetDepthStencilData().depthStencils[
				RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
		grMainDSV = grMainDepth.views[0];
	}

	if (grMainDSV && g_savedGroundResponseDSV != grMainDSV) {
		// Preserve ALL replay MRTs exactly. Substitute only the scene DSV.
		context->OMSetRenderTargets(
			D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
			g_savedGroundResponseRTVs,
			grMainDSV);
		g_groundResponseMainDepthRebound = true;
	}

	static bool gr13AVLogged = false;
	if (!gr13AVLogged) {
		logger::info(
			"PIXL GroundResponse v3.0.13AV authoritative-main-depth ACTIVE currentDSV={} mainDSV={} same={} rebound={}",
			static_cast<const void*>(g_savedGroundResponseDSV),
			static_cast<const void*>(grMainDSV),
			g_savedGroundResponseDSV == grMainDSV,
			g_groundResponseMainDepthRebound);
		gr13AVLogged = true;
	}
	// PIXL_GR_13AW_TERRAIN_PS_DESCRIPTOR_CAPTURE_V1
	//
	// Capture the exact *modified* Lighting PS descriptor used by the shader cache
	// for a real GroundResponse HS/DS shell draw. Hooks.cpp requests cached/custom
	// pixels with state->modifiedPixelDescriptor, not currentPixelDescriptor.
	// Keep only a tiny unique set so normal gameplay logging stays effectively free.
	if (state) {
		const std::uint32_t gr13AWDescriptor = state->modifiedPixelDescriptor;
		static std::array<std::uint32_t, 32> gr13AWSeenDescriptors{};
		static std::uint32_t gr13AWSeenCount = 0u;

		bool gr13AWSeen = false;
		for (std::uint32_t i = 0u; i < gr13AWSeenCount; ++i) {
			if (gr13AWSeenDescriptors[i] == gr13AWDescriptor) {
				gr13AWSeen = true;
				break;
			}
		}

		if (!gr13AWSeen && gr13AWSeenCount < gr13AWSeenDescriptors.size()) {
			gr13AWSeenDescriptors[gr13AWSeenCount++] = gr13AWDescriptor;
			logger::info(
				"PIXL GroundResponse v3.0.13AW terrain-ps-descriptor modified=0x{:08X} original=0x{:08X} shader={}",
				gr13AWDescriptor,
				state->currentPixelDescriptor,
				state->currentShader ? state->currentShader->fxpFilename : "<null>");
		}
	}
context->RSSetState(noCullRasterizer);
	context->HSSetShader(hs, nullptr, 0);
	context->DSSetShader(ds, nullptr, 0);
	context->IASetPrimitiveTopology(
		D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);

	terrainOverrideApplied = true;
	terrainGeometryRenderedThisFrame = true;
	geometryTelemetry.HsDsApplied++;
}

void GroundResponse::FinishTerrainPass()
{
	auto* context = globals::d3d::context;

	if (terrainOverrideApplied) {
		// PIXL_GR_13AV_AUTHORITATIVE_MAIN_DEPTH_V1
		// Restore exactly the OM targets that existed before the raised shell.
		if (g_groundResponseOMCaptured) {
			if (g_groundResponseMainDepthRebound) {
				context->OMSetRenderTargets(
					D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
					g_savedGroundResponseRTVs,
					g_savedGroundResponseDSV);
			}

			for (auto*& rtv : g_savedGroundResponseRTVs) {
				if (rtv) {
					rtv->Release();
					rtv = nullptr;
				}
			}
			if (g_savedGroundResponseDSV) {
				g_savedGroundResponseDSV->Release();
				g_savedGroundResponseDSV = nullptr;
			}

			g_groundResponseOMCaptured = false;
			g_groundResponseMainDepthRebound = false;
		}
		context->IASetPrimitiveTopology(savedTerrainTopology);
		context->RSSetState(savedTerrainRasterizer);
		context->HSSetShader(savedTerrainHS, nullptr, 0);
		context->DSSetShader(savedTerrainDS, nullptr, 0);

		if (savedTerrainHS) {
			savedTerrainHS->Release();
			savedTerrainHS = nullptr;
		}
		if (savedTerrainDS) {
			savedTerrainDS->Release();
			savedTerrainDS = nullptr;
		}

		context->HSSetConstantBuffers(5, 2, savedTerrainHSCB5to6);
		context->HSSetConstantBuffers(12, 2, savedTerrainHSCB12to13);
		context->DSSetConstantBuffers(5, 2, savedTerrainDSCB5to6);
		context->DSSetConstantBuffers(12, 2, savedTerrainDSCB12to13);
		context->DSSetShaderResources(101, 1, &savedTerrainDSSRV101);
		context->DSSetShaderResources(102, 1, &savedTerrainDSSRV102);
		context->DSSetShaderResources(103, 1, &savedTerrainDSSRV103);
		context->PSSetShaderResources(104, 1, &savedTerrainPSSRV104);
		context->PSSetShaderResources(105, 1, &savedTerrainPSSRV105);
		context->PSSetShaderResources(106, 1, &savedTerrainPSSRV106);
		context->PSSetShaderResources(107, 1, &savedTerrainPSSRV107);
		context->PSSetShaderResources(108, 1, &savedTerrainPSSRV108);
		context->PSSetShaderResources(109, 1, &savedTerrainPSSRV109);
		context->PSSetSamplers(7, 1, &savedTerrainPSSampler7);
		context->PSSetSamplers(8, 1, &savedTerrainPSSampler8);

		context->OMSetDepthStencilState(
			g_savedTerrainDepthStencilState,
			g_savedTerrainStencilRef);
		if (g_savedTerrainDepthStencilState) {
			g_savedTerrainDepthStencilState->Release();
			g_savedTerrainDepthStencilState = nullptr;
		}
		g_savedTerrainStencilRef = 0u;

		context->OMSetBlendState(
			g_savedTerrainBlendState,
			g_savedTerrainBlendFactor,
			g_savedTerrainSampleMask);
		if (g_savedTerrainBlendState) {
			g_savedTerrainBlendState->Release();
			g_savedTerrainBlendState = nullptr;
		}
	}

	auto releaseBuffers = [](ID3D11Buffer* (&buffers)[2]) {
		for (auto*& buffer : buffers) {
			if (buffer)
				buffer->Release();
			buffer = nullptr;
		}
	};
	releaseBuffers(savedTerrainHSCB5to6);
	releaseBuffers(savedTerrainHSCB12to13);
	releaseBuffers(savedTerrainDSCB5to6);
	releaseBuffers(savedTerrainDSCB12to13);
	if (savedTerrainDSSRV101) {
		savedTerrainDSSRV101->Release();
		savedTerrainDSSRV101 = nullptr;
	}
	if (savedTerrainDSSRV102) {
		savedTerrainDSSRV102->Release();
		savedTerrainDSSRV102 = nullptr;
	}
	if (savedTerrainDSSRV103) {
		savedTerrainDSSRV103->Release();
		savedTerrainDSSRV103 = nullptr;
	}
	if (savedTerrainPSSRV104) {
		savedTerrainPSSRV104->Release();
		savedTerrainPSSRV104 = nullptr;
	}
	if (savedTerrainPSSRV105) {
		savedTerrainPSSRV105->Release();
		savedTerrainPSSRV105 = nullptr;
	}
	if (savedTerrainPSSRV106) {
		savedTerrainPSSRV106->Release();
		savedTerrainPSSRV106 = nullptr;
	}
	if (savedTerrainPSSRV107) {
		savedTerrainPSSRV107->Release();
		savedTerrainPSSRV107 = nullptr;
	}
	if (savedTerrainPSSRV108) {
		savedTerrainPSSRV108->Release();
		savedTerrainPSSRV108 = nullptr;
	}
	if (savedTerrainPSSRV109) {
		savedTerrainPSSRV109->Release();
		savedTerrainPSSRV109 = nullptr;
	}
	if (savedTerrainPSSampler7) {
		savedTerrainPSSampler7->Release();
		savedTerrainPSSampler7 = nullptr;
	}
	if (savedTerrainPSSampler8) {
		savedTerrainPSSampler8->Release();
		savedTerrainPSSampler8 = nullptr;
	}
	if (savedTerrainRasterizer) {
		savedTerrainRasterizer->Release();
		savedTerrainRasterizer = nullptr;
	}

	savedTerrainTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
	terrainOverrideApplied = false;
	terrainGeometryWanted = false;
	activeTerrainPass = false;
}

void GroundResponse::BindDefaultRuntimeData()
{
	if (!perFrame)
		return;

	currentPerFrame.TerrainSnow1to4 = {};
	currentPerFrame.TerrainSnow5to6 = {};
	currentPerFrame.TerrainSnowValid = 0u;
	currentPerFrame.TerrainGeometryPass = 0u;
	currentPerFrame.TerrainMaterialForge = 0u;
	currentPerFrame.TerrainDebug =
		(settings.DebugInteractionField ? TERRAIN_DEBUG_OVERLAY : 0u) |
		(settings.GeometrySelfTest ? TERRAIN_DEBUG_GEOMETRY_SELF_TEST : 0u);
	currentPerFrame.RuntimeMagic = GROUND_RUNTIME_MAGIC;
	currentPerFrame.RuntimeVersion = GROUND_RUNTIME_VERSION;
	perFrame->Update(currentPerFrame);

	auto* context = globals::d3d::context;
	ID3D11Buffer* runtimeCB = perFrame->CB();
	context->PSSetConstantBuffers(13, 1, &runtimeCB);

	// Keep the legacy collider-height field on PS t100 so cached pre-v3 Lighting
	// permutations cannot accidentally reinterpret the normalized surface map.
	// v3 snow/mud geometry is driven exclusively by the absolute-world t101 field.
	// Do NOT expose the legacy camera-relative collider-height texture to terrain PS
	// t100. Cached pre-v3 Lighting permutations still contain the old material
	// deformation path; feeding them t100 creates a dark camera-following mask even
	// though the actual v3 geometry/tracks remain correctly anchored in world space.
	ID3D11ShaderResourceView* nullLegacyTerrainPS = nullptr;
	ID3D11ShaderResourceView* surfaceSRV =
		settings.EnableDeformableGround && surfaceDeformationTexture
			? surfaceDeformationTexture->srv.get()
			: nullptr;
	context->PSSetShaderResources(100, 1, &nullLegacyTerrainPS);
	context->PSSetShaderResources(101, 1, &surfaceSRV);
}

void GroundResponse::UpdateCollisionTexture()
{
	auto context = globals::d3d::context;
	// The field remains bound to terrain/grass between passes. Explicitly release
	// both read bindings before exposing the same resource as a compute UAV.
	ID3D11ShaderResourceView* nullInteractionSRV = nullptr;
	context->VSSetShaderResources(100, 1, &nullInteractionSRV);
	context->PSSetShaderResources(100, 1, &nullInteractionSRV);
	context->DSSetShaderResources(100, 1, &nullInteractionSRV);

	if (!settings.EnableGroundResponse && !settings.EnableDeformableGround) {
		float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		context->ClearUnorderedAccessViewFloat(collisionTexture->uav.get(), clearColor);
		return;
	}

	{
		ID3D11Buffer* buffers[1] = { *globals::game::perFrame };
		context->CSSetConstantBuffers(12, 1, buffers);
	}

	{
		ID3D11Buffer* buffers[1] = { perFrame->CB() };
		context->CSSetConstantBuffers(0, 1, buffers);

		ID3D11ShaderResourceView* srvs[] = {
			collisionBoundingBoxes->srv.get(),
			collisionInstances->srv.get(),
		};

		context->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

		ID3D11UnorderedAccessView* uavs[] = { collisionTexture->uav.get() };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		context->CSSetShader(GetCollisionUpdateCS(), nullptr, 0);
		globals::profiler->BeginPass("GroundResponse::CollisionUpdate");
		context->Dispatch(512 / 8, 512 / 8, 1);
		globals::profiler->EndPass();
	}

	context->CSSetShader(nullptr, nullptr, 0);

	ID3D11Buffer* null_buffer = nullptr;
	context->CSSetConstantBuffers(0, 1, &null_buffer);
	context->CSSetConstantBuffers(12, 1, &null_buffer);

	ID3D11ShaderResourceView* null_srvs[2] = { nullptr, nullptr };
	context->CSSetShaderResources(0, ARRAYSIZE(null_srvs), null_srvs);

	ID3D11UnorderedAccessView* null_uavs[1] = { nullptr };
	context->CSSetUnorderedAccessViews(0, 1, null_uavs, nullptr);
}

void GroundResponse::UpdateSurfaceDeformationTexture()
{
	static uint32_t s_surfaceDiagCount = 0u;
	const bool surfaceDiag = s_surfaceDiagCount < 16u;
	auto* context = globals::d3d::context;
	if (surfaceDiag)
		logger::debug(
			"[GR-DIAG] SurfaceUpdate enter #{} context={} surface={} displacement={} elemental={} cb={} boxes={} stamps={}",
			s_surfaceDiagCount,
			static_cast<const void*>(context),
			static_cast<const void*>(surfaceDeformationTexture),
			static_cast<const void*>(surfaceDisplacementTexture),
			static_cast<const void*>(surfaceElementalTexture),
			static_cast<const void*>(surfacePerFrame),
			queuedSurfaceStampBoxes.size(),
			queuedSurfaceStamps.size());

	if (!surfaceDeformationTexture ||
		!surfaceDisplacementTexture ||
		!surfaceElementalTexture ||
		!surfacePerFrame) {
		if (surfaceDiag)
			logger::debug("[GR-DIAG] SurfaceUpdate early-out missing resource");
		++s_surfaceDiagCount;
		return;
	}

	// t101/t102/t103 become UAVs for this dispatch. Explicitly unbind the same
	// resources from terrain stages first to avoid SRV/UAV hazards.
	ID3D11ShaderResourceView* nullSurfacePS = nullptr;
	context->PSSetShaderResources(101, 1, &nullSurfacePS);
	ID3D11ShaderResourceView* nullSurfaceDSSRVs[3] = {
		nullptr, nullptr, nullptr
	};
	context->DSSetShaderResources(101, 3, nullSurfaceDSSRVs);

	if (!settings.EnableDeformableGround ||
		(!settings.EnableSnowDeformation && !settings.EnableMudDeformation)) {
		const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		context->ClearUnorderedAccessViewFloat(
			surfaceDeformationTexture->uav.get(),
			clearColor);
		context->ClearUnorderedAccessViewFloat(
			surfaceDisplacementTexture->uav.get(),
			clearColor);
		context->ClearUnorderedAccessViewFloat(
			surfaceElementalTexture->uav.get(),
			clearColor);
		surfaceElementalClearedWhileDisabled = true;
		++s_surfaceDiagCount;
		return;
	}

	if (!settings.EnableElementalSnow) {
		if (!surfaceElementalClearedWhileDisabled) {
			const float clearElemental[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			context->ClearUnorderedAccessViewFloat(
				surfaceElementalTexture->uav.get(),
				clearElemental);
			surfaceElementalClearedWhileDisabled = true;
		}
	} else {
		// A later disable must clear any newly accumulated frost/melt/heat once.
		surfaceElementalClearedWhileDisabled = false;
	}

	ID3D11Buffer* surfaceCB = surfacePerFrame->CB();
	context->CSSetConstantBuffers(0, 1, &surfaceCB);

	ID3D11ShaderResourceView* srvs[] = {
		surfaceStampBoxes ? surfaceStampBoxes->srv.get() : nullptr,
		surfaceStamps ? surfaceStamps->srv.get() : nullptr
	};
	context->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

	ID3D11UnorderedAccessView* uavs[] = {
		surfaceDeformationTexture->uav.get(),
		surfaceDisplacementTexture->uav.get(),
		surfaceElementalTexture->uav.get()
	};
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

	if (surfaceDiag)
		logger::debug("[GR-DIAG] SurfaceUpdate before shader acquisition");
	auto* surfaceCS = GetSurfaceDeformationUpdateCS();
	if (surfaceDiag)
		logger::debug(
			"[GR-DIAG] SurfaceUpdate shader={} profiler={}",
			static_cast<const void*>(surfaceCS),
			static_cast<const void*>(globals::profiler));
	context->CSSetShader(surfaceCS, nullptr, 0);
	if (surfaceDiag)
		logger::debug("[GR-DIAG] SurfaceUpdate before BeginPass");
	globals::profiler->BeginPass("GroundResponse::SurfaceDeformationUpdate");
	context->Dispatch(
		SURFACE_TEXTURE_SIZE / 8,
		SURFACE_TEXTURE_SIZE / 8,
		1);
	globals::profiler->EndPass();

	context->CSSetShader(nullptr, nullptr, 0);

	ID3D11Buffer* nullBuffer = nullptr;
	context->CSSetConstantBuffers(0, 1, &nullBuffer);

	ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
	context->CSSetShaderResources(0, ARRAYSIZE(nullSRVs), nullSRVs);

	ID3D11UnorderedAccessView* nullUAVs[3] = {
		nullptr, nullptr, nullptr
	};
	context->CSSetUnorderedAccessViews(0, 3, nullUAVs, nullptr);

	if (surfaceDiag)
		logger::debug("[GR-DIAG] SurfaceUpdate exit #{}", s_surfaceDiagCount);
	++s_surfaceDiagCount;
}

#undef I18N_KEY_PREFIX

