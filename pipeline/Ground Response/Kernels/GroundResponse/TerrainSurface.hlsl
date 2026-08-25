// PIXL Ground Response 3.0 - persistent raised snow + mud shell.
//
// TerrainSeam replays Skyrim's real landscape draw. GroundResponse inserts these
// HS/DS stages between the original Lighting VS and PS. Every nearby classified
// landscape pass receives the SAME geometry decision, preventing layered terrain
// passes from splitting between base and displaced planes.
//
// Snow = thick raised surface.
// Wet mud = thinner raised surface using the exact same persistent compaction map.
// Both compress back toward the untouched base terrain under actor contact.

#include "Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"
#include "GroundResponse/Runtime.hlsli"
#include "GroundResponse/DeformableGround.hlsli"

// PIXL_GR_13AJ_DISPLACED_SNOW_FIELD_V1
// Existing 13AF/13AD DLL resource. This remains positive-only geometry.
Texture2D<float4> GroundDisplacedSnowField : register(t102);

// Signed elemental snow response written by SurfaceDeformationUpdateCS.
// x=current frost(+)/melt(-) height, y=current heat smoothing,
// z=previous height, w=previous heat smoothing.
Texture2D<float4> GroundElementalSnowField : register(t103);

struct TERRAIN_POINT
{
    float4 Position : SV_POSITION0;
    float4 TexCoord0 : TEXCOORD0;
    float3 TBN0 : TEXCOORD1;
    float3 TBN1 : TEXCOORD2;
    float3 TBN2 : TEXCOORD3;
    float4 LandBlendWeights1 : TEXCOORD6;
    float4 LandBlendWeights2 : TEXCOORD7;
    float4 WorldPosition : POSITION1;
    float4 PreviousWorldPosition : POSITION2;
    float4 Color : COLOR0;
    float4 FogParam : COLOR1;
    float3 ModelPosition : TEXCOORD12;
    float3 GroundBaseWorldPosition : TEXCOORD13;
};

struct PATCH_CONSTANTS
{
    float Edge[3] : SV_TessFactor;
    float Inside : SV_InsideTessFactor;
};

// PIXL_GR_DEPTH_VARIATION_V7_TALLER_TUNED_DRIFTS
//
// Conservative shader-only world-space depth variation. This deliberately
// consumes NO new CB slots or textures. Absolute XY comes from the same
// camera-relative -> absolute transform used by DeformableGround/t101.
//
// The integer hash is intentionally simple and CPU-portable so movement
// resistance can later evaluate the exact same depth field on the CPU.
static const float PIXL_SNOW_FINE_VARIATION = 0.10f;
static const float PIXL_SNOW_POCKET_STRENGTH = 0.22f;
static const float PIXL_SNOW_FINE_SCALE = 180.0f;
static const float PIXL_SNOW_POCKET_SCALE = 620.0f;
static const float PIXL_SNOW_MOUND_STRENGTH = 0.32f; // PIXL_GR_13AH_DRIFT_VISUAL_PARITY_V1
static const float PIXL_SNOW_MOUND_SCALE = 300.0f;
static const float PIXL_SNOW_MOUND_DETAIL_SCALE = 170.0f;
// Near-field baseline plus procedural-detail-driven adaptive tessellation.
// Flat nearby snow gets a cheaper 12x floor; actual mound/drift regions ramp
// toward D3D11's 16x hardware tessellation limit.
// PIXL_GR_13Y_THREE_STAGE_TESSELLATION_V1
//
// Three-stage terrain LOD:
//   CLOSE  : authored near tessellation + adaptive 12-16x detail
//   MEDIUM : authored far/medium tessellation for stable drift shape
//   FAR    : automatic low-cost 2x silhouette tessellation until geometry fade
//
// The adaptive-detail reach is ~2x the 13X baseline so drift silhouettes do not
// suddenly appear only after the player is already close to them.
static const float PIXL_ADAPTIVE_BASE_NEAR_DISTANCE = 480.0f;
static const float PIXL_ADAPTIVE_BASE_BLEND_END = 840.0f;
static const float PIXL_ADAPTIVE_BASE_TESS = 12.0f;
static const float PIXL_ADAPTIVE_DETAIL_FULL_DISTANCE = 720.0f;
static const float PIXL_ADAPTIVE_DETAIL_BLEND_END = 1520.0f;
static const float PIXL_ADAPTIVE_DETAIL_MIN_TESS = 13.0f;
static const float PIXL_ADAPTIVE_DETAIL_MAX_TESS = 16.0f;
static const float PIXL_FAR_VISUAL_TESS = 2.0f;

// t101 is a 4096-unit square clipmap centered around the simulation anchor.
// Keep expensive 17-tap slope-limited compression inside a safe circular region.
// Beyond this, the shell remains visible but is pristine visual-only geometry.
static const float PIXL_INTERACTION_LOD_FULL_DISTANCE = 1728.0f;
static const float PIXL_INTERACTION_LOD_END_DISTANCE = 1984.0f;

static const float PIXL_MOUND_BLUR_RADIUS = 58.0f;

// Deep drifts are deliberately independent of the user-facing global pristine
// shell thickness. SnowSurfaceThickness remains the ordinary blanket depth,
// while rare accumulation zones can rise toward a character's chest/mid-spine.
static const float PIXL_SNOW_MAX_PRISTINE_HEIGHT = 72.0f; // PIXL_GR_13AH_DRIFT_VISUAL_PARITY_V1
static const float PIXL_SNOW_DEEP_DRIFT_SCALE = 560.0f;
static const float PIXL_SNOW_DEEP_DRIFT_DETAIL_SCALE = 290.0f;
static const float PIXL_SNOW_DEEP_DRIFT_BLUR_RADIUS = 120.0f;

static const float PIXL_MUD_FINE_VARIATION = 0.08f;
static const float PIXL_MUD_POCKET_STRENGTH = 0.16f;
static const float PIXL_MUD_FINE_SCALE = 200.0f;
static const float PIXL_MUD_POCKET_SCALE = 480.0f;

uint GroundDepthHash(int2 p)
{
    uint x = asuint(p.x);
    uint y = asuint(p.y);
    uint h = x * 0x8da6b343u ^ y * 0xd8163841u;
    h ^= h >> 13;
    h *= 0x85ebca6bu;
    h ^= h >> 16;
    return h;
}

float GroundDepthHash01(int2 p)
{
    return float(GroundDepthHash(p) & 0x00FFFFFFu) * (1.0f / 16777215.0f);
}

float GroundDepthValueNoise(float2 absoluteXY, float scale)
{
    float safeScale = max(scale, 1.0f);
    float2 grid = absoluteXY / safeScale;
    int2 cell = int2(floor(grid));
    float2 f = frac(grid);
    // Quintic fade gives continuous first/second derivatives and produces
    // visibly smoother mound silhouettes than cubic smoothstep when tessellated.
    f = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);

    float n00 = GroundDepthHash01(cell);
    float n10 = GroundDepthHash01(cell + int2(1, 0));
    float n01 = GroundDepthHash01(cell + int2(0, 1));
    float n11 = GroundDepthHash01(cell + int2(1, 1));

    return lerp(
        lerp(n00, n10, f.x),
        lerp(n01, n11, f.x),
        f.y);
}

float GroundDepthFineNoise(float2 absoluteXY, float scale)
{
    // Two smooth octaves are enough to break uniformity without producing
    // high-frequency spikes that fight terrain tessellation.
    float n0 = GroundDepthValueNoise(absoluteXY, scale);
    float n1 = GroundDepthValueNoise(
        absoluteXY + float2(137.0f, -251.0f),
        max(scale * 0.47f, 1.0f));
    return n0 * 0.68f + n1 * 0.32f;
}

float GroundDepthBlurredNoise(
    float2 absoluteXY,
    float scale,
    float radius)
{
    // Cross-shaped 5-tap low-pass filter. This acts like a small procedural
    // noise-mask blur without adding a texture/SRV. It is only used for the
    // local snow mound field, where smoother silhouettes matter most.
    float centre = GroundDepthValueNoise(absoluteXY, scale);
    float left = GroundDepthValueNoise(absoluteXY + float2(-radius, 0.0f), scale);
    float right = GroundDepthValueNoise(absoluteXY + float2(radius, 0.0f), scale);
    float down = GroundDepthValueNoise(absoluteXY + float2(0.0f, -radius), scale);
    float up = GroundDepthValueNoise(absoluteXY + float2(0.0f, radius), scale);

    return centre * 0.40f +
        (left + right + down + up) * 0.15f;
}

// PIXL_GR_13BI_WIND_BUILT_DRIFTS_V1
// Stable world-space prevailing wind. The anisotropic transform stretches the
// procedural field along wind direction, producing broad wind-built ridges and
// lee pockets instead of isotropic noise blobs. It is intentionally independent
// of instantaneous weather so old drifts do not rotate when the weather changes.
static const float2 PIXL_SNOW_PREVAILING_WIND =
    float2(0.8192319f, 0.5734623f);

float2 GroundSnowWindSpace(float2 absoluteXY)
{
    float2 wind = PIXL_SNOW_PREVAILING_WIND;
    float2 crossWind = float2(-wind.y, wind.x);
    return float2(
        dot(absoluteXY, wind) * 0.44f,
        dot(absoluteXY, crossWind) * 1.30f);
}
float GroundSnowLocalMoundSignal(float2 absoluteXY)
{
    float2 windXY = GroundSnowWindSpace(absoluteXY);

    // The large blur is a procedural relaxation pass: isolated pristine spikes
    // and tiny pits are rounded before persistent t101 footprint compaction is
    // applied, so real tracks/deformations remain crisp and persistent.
    float moundBase =
        GroundDepthBlurredNoise(
            windXY + float2(91.0f, -173.0f),
            PIXL_SNOW_MOUND_SCALE,
            PIXL_MOUND_BLUR_RADIUS);
    float moundDetail =
        GroundDepthBlurredNoise(
            windXY + float2(-247.0f, 119.0f),
            PIXL_SNOW_MOUND_DETAIL_SCALE,
            PIXL_MOUND_BLUR_RADIUS * 0.65f);

    float moundSeed =
        moundBase * 0.86f +
        moundDetail * 0.14f;

    return
        pow(
            smoothstep(
                0.53f,
                0.84f,
                moundSeed),
            1.60f);
}

// PIXL_GR_13AH_RESTORED_VISIBLE_DRIFT_FIELD_V1
// PIXL_GR_13BI_WIND_RELAXED_DEEP_DRIFT_V1
float GroundSnowDeepDriftSignal(float2 absoluteXY)
{
    float2 windXY = GroundSnowWindSpace(absoluteXY);

    // Broad accumulation overwhelmingly dominates; only a weak secondary field
    // breaks up silhouettes. This is deliberately much lower-frequency than 13BH.
    float broad =
        GroundDepthBlurredNoise(
            windXY + float2(613.0f, -379.0f),
            PIXL_SNOW_DEEP_DRIFT_SCALE,
            PIXL_SNOW_DEEP_DRIFT_BLUR_RADIUS);
    float detail =
        GroundDepthBlurredNoise(
            windXY + float2(-337.0f, 547.0f),
            PIXL_SNOW_DEEP_DRIFT_DETAIL_SCALE,
            PIXL_SNOW_DEEP_DRIFT_BLUR_RADIUS * 0.60f);

    float seed =
        broad * 0.90f +
        detail * 0.10f;

    return
        pow(
            smoothstep(
                0.54f,
                0.79f,
                seed),
            1.50f);
}

float GroundSnowAdaptiveTessDemand(float2 absoluteXY)
{
    // Shared-edge tessellation still uses a cheap estimate, but samples the same
    // wind-space field as DS displacement so tessellation follows the new drifts.
    float2 windXY = GroundSnowWindSpace(absoluteXY);
    float moundEstimate =
        GroundDepthValueNoise(
            windXY + float2(91.0f, -173.0f),
            PIXL_SNOW_MOUND_SCALE);
    float driftEstimate =
        GroundDepthValueNoise(
            windXY + float2(613.0f, -379.0f),
            PIXL_SNOW_DEEP_DRIFT_SCALE);

    float moundDemand =
        smoothstep(0.47f, 0.73f, moundEstimate);
    float driftDemand =
        smoothstep(0.45f, 0.70f, driftEstimate);

    return saturate(max(moundDemand, driftDemand));
}

float GroundDepthVariationScale(float2 absoluteXY,
    float snowActivation,
    float mudActivation)
{
    float activation = saturate(snowActivation + mudActivation);
    if (activation <= 1e-5f)
        return 1.0f;

    float snowFine =
        (GroundDepthFineNoise(
            absoluteXY,
            PIXL_SNOW_FINE_SCALE) * 2.0f - 1.0f) *
        PIXL_SNOW_FINE_VARIATION;
    float snowPocket =
        smoothstep(
            0.52f,
            0.88f,
            GroundDepthValueNoise(
                absoluteXY + float2(-421.0f, 183.0f),
                PIXL_SNOW_POCKET_SCALE)) *
        PIXL_SNOW_POCKET_STRENGTH;

    // Local rounded hummocks layered over the ordinary snow blanket.
    float snowMound =
        GroundSnowLocalMoundSignal(absoluteXY) *
        PIXL_SNOW_MOUND_STRENGTH;

    float mudFine =
        (GroundDepthFineNoise(
            absoluteXY + float2(71.0f, 309.0f),
            PIXL_MUD_FINE_SCALE) * 2.0f - 1.0f) *
        PIXL_MUD_FINE_VARIATION;
    float mudPocket =
        smoothstep(
            0.58f,
            0.92f,
            GroundDepthValueNoise(
                absoluteXY + float2(263.0f, -347.0f),
                PIXL_MUD_POCKET_SCALE)) *
        PIXL_MUD_POCKET_STRENGTH;

    float weightedVariation =
        (snowFine + snowPocket + snowMound) * snowActivation +
        (mudFine + mudPocket) * mudActivation;

    // Ordinary blanket/mound variation remains bounded. Truly deep snow
    // is added later as an absolute world-height accumulation layer.
    return clamp(
        1.0f + weightedVariation,
        0.70f,
        2.15f);
}

float TerrainSnowCoverage(TERRAIN_POINT p)
{
    return GroundResponseRuntime::GetTerrainSnowCoverage(
        p.LandBlendWeights1,
        p.LandBlendWeights2.xy);
}

float3 GeometryNormalAtPoint(TERRAIN_POINT p)
{
    // Lighting reconstructs its vertex geometry normal from the Z column of TBN.
    float3 n = float3(p.TBN0.z, p.TBN1.z, p.TBN2.z);
    float lenSq = dot(n, n);
    return lenSq > 1e-8f
        ? n * rsqrt(lenSq)
        : float3(0.0f, 0.0f, 1.0f);
}

float SlopeMaskFromNormal(float3 normalWS)
{
    if (normalWS.z < 0.0f)
        normalWS = -normalWS;
    float minimumSlopeZ = saturate(GroundRuntimeGeometryMinimumSlopeZ);
    return smoothstep(
        minimumSlopeZ,
        min(minimumSlopeZ + 0.14f, 1.0f),
        saturate(normalWS.z));
}

float UnifiedSurfaceMaskAtPoint(TERRAIN_POINT p)
{
    if (GroundResponseRuntime::GeometrySelfTestEnabled())
        return 1.0f;

    float coverage = TerrainSnowCoverage(p);
    return
        GroundResponseRuntime::GetUnifiedSurfaceActivation(coverage) *
        SlopeMaskFromNormal(GeometryNormalAtPoint(p));
}

// PIXL_GR_13AH_SUBTEXEL_COMPACTION_FILTER_V1
//
// t101 is 1024x1024 over 4096 world units = 4 world units per texel.
// A ~21-unit-wide player trench therefore spans only ~5 texels. At large
// compression depth, bilinear reconstruction can expose those texel rows as
// several parallel capsule/groove bands.
//
// Geometry receives a FIXED half-cell 9-tap low-pass before occupancy shaping.
// The filter radius is always 2 world units and is completely independent of
// SnowMaximumDepth / GroundResponseStrength, so increasing depth cannot make the
// footprint expand laterally.
//
// Lighting/material sampling remains on the original DeformableGround path.
static const float PIXL_T101_GEOMETRY_FILTER_RADIUS = 2.0f;

static const int PIXL_ELEMENT_FIELD_SIZE = 1024;
static const int PIXL_ELEMENT_FIELD_MASK = PIXL_ELEMENT_FIELD_SIZE - 1;
static const float PIXL_ELEMENT_FIELD_WORLD_SIZE = 4096.0f;
static const float PIXL_ELEMENT_FIELD_CELL_SIZE =
    PIXL_ELEMENT_FIELD_WORLD_SIZE / float(PIXL_ELEMENT_FIELD_SIZE);

int2 GroundElementWrapTexel(int2 logicalTexel)
{
    return
        (logicalTexel + int2(GroundRuntimeSurfaceArrayOrigin)) &
        int2(PIXL_ELEMENT_FIELD_MASK, PIXL_ELEMENT_FIELD_MASK);
}

float4 GroundElementLoadLogical(int2 logicalTexel)
{
    logicalTexel =
        clamp(
            logicalTexel,
            int2(0, 0),
            int2(PIXL_ELEMENT_FIELD_SIZE - 1, PIXL_ELEMENT_FIELD_SIZE - 1));

    float4 v =
        GroundElementalSnowField.Load(
            int3(GroundElementWrapTexel(logicalTexel), 0));
    v.yw = saturate(v.yw);
    return v;
}

float4 GroundElementSampleAbsolute(float2 absoluteXY)
{
    float2 localPosition =
        absoluteXY - GroundRuntimeSurfaceOriginAbsolute;

    const float safeHalfExtent =
        PIXL_ELEMENT_FIELD_WORLD_SIZE * 0.5f -
        PIXL_ELEMENT_FIELD_CELL_SIZE;
    if (any(abs(localPosition) >= safeHalfExtent.xx))
        return 0.0f.xxxx;

    float2 texelPosition =
        (localPosition / PIXL_ELEMENT_FIELD_WORLD_SIZE + 0.5f) *
            float(PIXL_ELEMENT_FIELD_SIZE) -
        0.5f;
    texelPosition =
        clamp(
            texelPosition,
            0.0f.xx,
            (float(PIXL_ELEMENT_FIELD_SIZE) - 1.0001f).xx);

    int2 baseTexel =
        min(
            (int2)floor(texelPosition),
            int2(PIXL_ELEMENT_FIELD_SIZE - 2, PIXL_ELEMENT_FIELD_SIZE - 2));
    float2 f = saturate(texelPosition - float2(baseTexel));

    float4 s00 = GroundElementLoadLogical(baseTexel);
    float4 s10 = GroundElementLoadLogical(baseTexel + int2(1, 0));
    float4 s01 = GroundElementLoadLogical(baseTexel + int2(0, 1));
    float4 s11 = GroundElementLoadLogical(baseTexel + int2(1, 1));

    return
        lerp(
            lerp(s00, s10, f.x),
            lerp(s01, s11, f.x),
            f.y);
}

float4 GroundFilteredCompactionField(float2 absoluteXY, float2 heat)
{
    const float r = PIXL_T101_GEOMETRY_FILTER_RADIUS;

    float4 c  = DeformableGround::SampleRawAbsolute(absoluteXY);
    float4 px = DeformableGround::SampleRawAbsolute(absoluteXY + float2( r, 0.0f));
    float4 nx = DeformableGround::SampleRawAbsolute(absoluteXY + float2(-r, 0.0f));
    float4 py = DeformableGround::SampleRawAbsolute(absoluteXY + float2(0.0f,  r));
    float4 ny = DeformableGround::SampleRawAbsolute(absoluteXY + float2(0.0f, -r));

    float4 pp = DeformableGround::SampleRawAbsolute(absoluteXY + float2( r,  r));
    float4 pn = DeformableGround::SampleRawAbsolute(absoluteXY + float2( r, -r));
    float4 np = DeformableGround::SampleRawAbsolute(absoluteXY + float2(-r,  r));
    float4 nn = DeformableGround::SampleRawAbsolute(absoluteXY + float2(-r, -r));

    // Compact Gaussian-like kernel:
    // centre 0.36, axes 0.12 each, diagonals 0.04 each = 1.0 total.
    float4 filtered =
        c * 0.36f +
        (px + nx + py + ny) * 0.12f +
        (pp + pn + np + nn) * 0.04f;

    // Fire/heat can soften an existing sharp compression boundary without
    // destructively rewriting t101. The transient t103.y mask enables a wider
    // geometric reconstruction only where heat was actually applied.
    heat = saturate(heat);
    if (max(heat.x, heat.y) > 1.0e-4f)
    {
        const float wr = 10.0f;
        float4 wxp = DeformableGround::SampleRawAbsolute(absoluteXY + float2( wr, 0.0f));
        float4 wxn = DeformableGround::SampleRawAbsolute(absoluteXY + float2(-wr, 0.0f));
        float4 wyp = DeformableGround::SampleRawAbsolute(absoluteXY + float2(0.0f,  wr));
        float4 wyn = DeformableGround::SampleRawAbsolute(absoluteXY + float2(0.0f, -wr));
        float4 wide = (c + wxp + wxn + wyp + wyn) * 0.20f;
        filtered.x = lerp(filtered.x, wide.x, heat.x * 0.72f);
        filtered.z = lerp(filtered.z, wide.z, heat.y * 0.72f);
    }

    return filtered;
}

float GroundVerticalCompressionProfile(float compression)
{
    // PIXL_GR_13AH_SMOOTH_OCCUPANCY_V1
    //
    // Do not saturate almost the entire bilinear field at 0.175 like 13AC did.
    // That made individual t101 texel rows become discrete full-depth shelves
    // when MaximumDepth was high. The filtered field now receives a much broader
    // quintic transition.
    float c = saturate(compression);

    float t =
        saturate(
            (c - 0.055f) /
            (0.62f - 0.055f));

    return
        t * t * t *
        (t * (t * 6.0f - 15.0f) + 10.0f);
}

DeformableGround::SurfaceCompression GetGroundVerticalSurfaceCompression(
    float3 worldPosition,
    float2 heat)
{
    DeformableGround::SurfaceCompression result;
    result.current = 0.0f;
    result.previous = 0.0f;
    result.freshness = 0.0f;
    result.previousFreshness = 0.0f;

    if (!GroundResponseRuntime::IsRuntimeValid() ||
        SharedData::deformableGroundSettings.EnableDeformableGround == 0u)
        return result;

    float2 absoluteXY =
        DeformableGround::AbsoluteXY(
            worldPosition);

    float4 field =
        saturate(
            GroundFilteredCompactionField(
                absoluteXY,
                heat));

    result.current =
        GroundVerticalCompressionProfile(
            field.x);
    result.previous =
        GroundVerticalCompressionProfile(
            field.z);

    // Freshness is filtered too, but remains linear; only compaction gets the
    // occupancy remap.
    result.freshness = field.y;
    result.previousFreshness = field.w;

    return result;
}


// PIXL_GR_13AK_BULK_WALL_SLUMP_V1
//
// PHYSICS MODEL
// -------------
// The current 13AH trench is a valid compressed channel, but a tall pristine
// drift leaves a near-vertical cut wall. 13AK treats the first recovery phase as
// a granular slope failure:
//
//   1. wall top loses support,
//   2. upper snow moves OUT and DOWN,
//   3. the shoulder gets broader/lower,
//   4. a rounded toe forms,
//   5. only later does the trench bottom begin to rise.
//
// The effect scales with local pristine snow thickness:
// shallow footprint -> subtle,
// deep drift        -> pronounced.
//
// Direction is found from a 2-ring neighbourhood search around the t101 channel,
// not only a 4u local derivative. This is important: 13AJ's local gradient became
// zero only a few units outside the trench, which is why the visible slide was
// too small.

static const int PIXL_PUSH_FIELD_SIZE = 1024;
static const int PIXL_PUSH_FIELD_MASK = PIXL_PUSH_FIELD_SIZE - 1;
static const float PIXL_PUSH_FIELD_WORLD_SIZE = 4096.0f;
static const float PIXL_PUSH_FIELD_CELL_SIZE =
    PIXL_PUSH_FIELD_WORLD_SIZE / float(PIXL_PUSH_FIELD_SIZE);

static const float PIXL_BULK_SNOW_START = 10.0f;
static const float PIXL_BULK_SNOW_FULL = 42.0f;

static const float PIXL_SLIDE_DISTANCE_SHALLOW = 8.0f;
static const float PIXL_SLIDE_DISTANCE_DEEP = 50.0f; // PIXL_GR_13AM_LONGER_SEPARATION_V1

static const float PIXL_WALL_COLLAPSE_MAX = 26.0f;
static const float PIXL_WALL_COLLAPSE_HEIGHT_FRACTION = 0.44f;

static const float PIXL_SETTLED_BERM_MAX_RAISE = 8.0f;
static const float PIXL_SETTLED_BERM_HEIGHT_FRACTION = 0.24f;

int2 GroundPushWrapTexel(int2 logicalTexel)
{
    return
        (logicalTexel + int2(GroundRuntimeSurfaceArrayOrigin)) &
        int2(PIXL_PUSH_FIELD_MASK, PIXL_PUSH_FIELD_MASK);
}

float4 GroundPushLoadLogical(int2 logicalTexel)
{
    logicalTexel =
        clamp(
            logicalTexel,
            int2(0, 0),
            int2(PIXL_PUSH_FIELD_SIZE - 1, PIXL_PUSH_FIELD_SIZE - 1));

    return
        saturate(
            GroundDisplacedSnowField.Load(
                int3(GroundPushWrapTexel(logicalTexel), 0)));
}

float4 GroundPushSampleAbsolute(float2 absoluteXY)
{
    float2 localPosition =
        absoluteXY -
        GroundRuntimeSurfaceOriginAbsolute;

    const float safeHalfExtent =
        PIXL_PUSH_FIELD_WORLD_SIZE * 0.5f -
        PIXL_PUSH_FIELD_CELL_SIZE;

    if (any(abs(localPosition) >= safeHalfExtent.xx))
        return 0.0f.xxxx;

    float2 texelPosition =
        (localPosition / PIXL_PUSH_FIELD_WORLD_SIZE + 0.5f) *
            float(PIXL_PUSH_FIELD_SIZE) -
        0.5f;

    texelPosition =
        clamp(
            texelPosition,
            0.0f.xx,
            (float(PIXL_PUSH_FIELD_SIZE) - 1.0001f).xx);

    int2 baseTexel =
        min(
            (int2)floor(texelPosition),
            int2(PIXL_PUSH_FIELD_SIZE - 2, PIXL_PUSH_FIELD_SIZE - 2));

    float2 f =
        saturate(
            texelPosition -
            float2(baseTexel));

    float4 s00 = GroundPushLoadLogical(baseTexel);
    float4 s10 = GroundPushLoadLogical(baseTexel + int2(1, 0));
    float4 s01 = GroundPushLoadLogical(baseTexel + int2(0, 1));
    float4 s11 = GroundPushLoadLogical(baseTexel + int2(1, 1));

    return
        lerp(
            lerp(s00, s10, f.x),
            lerp(s01, s11, f.x),
            f.y);
}

float GroundRawCompaction(float2 absoluteXY)
{
    return
        saturate(
            DeformableGround::SampleRawAbsolute(
                absoluteXY).x);
}

float2 GroundSearchTrenchOutward(float2 absoluteXY)
{
    // Directions from this destination toward candidate trench texels.
    static const float2 dirs[8] =
    {
        float2( 1.0f,  0.0f),
        float2(-1.0f,  0.0f),
        float2( 0.0f,  1.0f),
        float2( 0.0f, -1.0f),
        float2( 0.70710678f,  0.70710678f),
        float2(-0.70710678f,  0.70710678f),
        float2( 0.70710678f, -0.70710678f),
        float2(-0.70710678f, -0.70710678f)
    };

    float2 inward = 0.0f.xx;
    float weightSum = 0.0f;

    // Near ring keeps the edge stable.
    [unroll]
    for (int i = 0; i < 8; ++i)
    {
        float c =
            GroundRawCompaction(
                absoluteXY +
                    dirs[i] * 12.0f);

        float w =
            c * c;

        inward +=
            dirs[i] * w;
        weightSum += w;
    }

    // Far ring lets the direction remain valid well outside the original wall.
    [unroll]
    for (int farIndex = 0; farIndex < 8; ++farIndex)
    {
        float c =
            GroundRawCompaction(
                absoluteXY +
                    dirs[farIndex] * 30.0f);

        float w =
            c * c * 0.62f;

        inward +=
            dirs[farIndex] * w;
        weightSum += w;
    }

    if (weightSum <= 1.0e-5f)
        return 0.0f.xx;

    float lenSq =
        dot(inward, inward);

    if (lenSq <= 1.0e-6f)
        return 0.0f.xx;

    // 'inward' points toward the nearby compressed channel.
    return
        -inward *
        rsqrt(lenSq);
}

struct GroundBulkSlump
{
    float currentPile;
    float previousPile;
    float currentProgress;
    float previousProgress;
    float currentWallCollapse;
    float previousWallCollapse;
};

GroundBulkSlump GetGroundBulkSlump(
    float2 absoluteXY,
    float bulkWeight)
{
    GroundBulkSlump result;
    result.currentPile = 0.0f;
    result.previousPile = 0.0f;
    result.currentProgress = 0.0f;
    result.previousProgress = 0.0f;
    result.currentWallCollapse = 0.0f;
    result.previousWallCollapse = 0.0f;

    float2 outward =
        GroundSearchTrenchOutward(
            absoluteXY);

    if (dot(outward, outward) <= 1.0e-5f)
        return result;

    float4 centre =
        GroundPushSampleAbsolute(
            absoluteXY);

    float currentLocalProgress =
        1.0f - centre.y;
    float previousLocalProgress =
        1.0f - centre.w;

    // Bulk movement should be essentially complete by ~2/3 of the 7.5 second
    // settling window = ~5 seconds.
    // PIXL_GR_13AM_SMOOTH_PHASE_ENVELOPES_V1
    float currentBulkT =
        saturate((currentLocalProgress - 0.01f) / (0.62f - 0.01f));
    float previousBulkT =
        saturate((previousLocalProgress - 0.01f) / (0.62f - 0.01f));

    float currentBulkPhase =
        currentBulkT * currentBulkT * currentBulkT *
        (currentBulkT * (currentBulkT * 6.0f - 15.0f) + 10.0f);
    float previousBulkPhase =
        previousBulkT * previousBulkT * previousBulkT *
        (previousBulkT * (previousBulkT * 6.0f - 15.0f) + 10.0f);

    float currentEdgeT =
        saturate((currentLocalProgress - 0.50f) / (0.95f - 0.50f));
    float previousEdgeT =
        saturate((previousLocalProgress - 0.50f) / (0.95f - 0.50f));

    float currentEdgePhase =
        currentEdgeT * currentEdgeT * currentEdgeT *
        (currentEdgeT * (currentEdgeT * 6.0f - 15.0f) + 10.0f);
    float previousEdgePhase =
        previousEdgeT * previousEdgeT * previousEdgeT *
        (previousEdgeT * (previousEdgeT * 6.0f - 15.0f) + 10.0f);

    float slideEnd =
        lerp(
            PIXL_SLIDE_DISTANCE_SHALLOW,
            PIXL_SLIDE_DISTANCE_DEEP,
            bulkWeight);

    // Material at this destination originated closer to the trench.
    // Four source distances cover shallow footprints through very tall drifts.
    const float d0 = 6.0f;
    const float d1 = 14.0f;
    const float d2 = 28.0f;
    const float d3 = 44.0f;

    float4 s0 = GroundPushSampleAbsolute(absoluteXY - outward * d0);
    float4 s1 = GroundPushSampleAbsolute(absoluteXY - outward * d1);
    float4 s2 = GroundPushSampleAbsolute(absoluteXY - outward * d2);
    float4 s3 = GroundPushSampleAbsolute(absoluteXY - outward * d3);

    float p0 = 1.0f - s0.y;
    float p1 = 1.0f - s1.y;
    float p2 = 1.0f - s2.y;
    float p3 = 1.0f - s3.y;

    float pp0 = 1.0f - s0.w;
    float pp1 = 1.0f - s1.w;
    float pp2 = 1.0f - s2.w;
    float pp3 = 1.0f - s3.w;

    float t0 = lerp(2.0f, slideEnd, smoothstep(0.02f, 0.68f, p0));
    float t1 = lerp(2.0f, slideEnd, smoothstep(0.02f, 0.68f, p1));
    float t2 = lerp(2.0f, slideEnd, smoothstep(0.02f, 0.68f, p2));
    float t3 = lerp(2.0f, slideEnd, smoothstep(0.02f, 0.68f, p3));

    float pt0 = lerp(2.0f, slideEnd, smoothstep(0.02f, 0.68f, pp0));
    float pt1 = lerp(2.0f, slideEnd, smoothstep(0.02f, 0.68f, pp1));
    float pt2 = lerp(2.0f, slideEnd, smoothstep(0.02f, 0.68f, pp2));
    float pt3 = lerp(2.0f, slideEnd, smoothstep(0.02f, 0.68f, pp3));

    float w0 = 1.0f - smoothstep(4.0f, 10.0f, abs(t0 - d0));
    float w1 = 1.0f - smoothstep(5.0f, 12.0f, abs(t1 - d1));
    float w2 = 1.0f - smoothstep(6.0f, 14.0f, abs(t2 - d2));
    float w3 = 1.0f - smoothstep(7.0f, 16.0f, abs(t3 - d3));

    float pw0 = 1.0f - smoothstep(4.0f, 10.0f, abs(pt0 - d0));
    float pw1 = 1.0f - smoothstep(5.0f, 12.0f, abs(pt1 - d1));
    float pw2 = 1.0f - smoothstep(6.0f, 14.0f, abs(pt2 - d2));
    float pw3 = 1.0f - smoothstep(7.0f, 16.0f, abs(pt3 - d3));

    // PIXL_GR_13AM_CONTINUOUS_MASS_TRANSPORT_V1
    float currentWeightSum = w0 + w1 + w2 + w3;
    float previousWeightSum = pw0 + pw1 + pw2 + pw3;

    float movedCurrent =
        currentWeightSum > 1.0e-4f
            ? (s0.x * w0 + s1.x * w1 + s2.x * w2 + s3.x * w3) /
              currentWeightSum
            : 0.0f;

    float movedPrevious =
        previousWeightSum > 1.0e-4f
            ? (s0.z * pw0 + s1.z * pw1 + s2.z * pw2 + s3.z * pw3) /
              previousWeightSum
            : 0.0f;

    // The deposited centre pile fades away as its translated copy becomes the
    // dominant visible mass.
    result.currentPile =
        max(
            centre.x *
                (1.0f - currentBulkPhase),
            movedCurrent);

    result.previousPile =
        max(
            centre.z *
                (1.0f - previousBulkPhase),
            movedPrevious);

    result.currentProgress =
        max(
            currentLocalProgress,
            max(
                p0 * w0,
                max(
                    p1 * w1,
                    max(
                        p2 * w2,
                        p3 * w3))));

    result.previousProgress =
        max(
            previousLocalProgress,
            max(
                pp0 * pw0,
                max(
                    pp1 * pw1,
                    max(
                        pp2 * pw2,
                        pp3 * pw3))));

    // WALL COLLAPSE
    // -------------
    // Pull compaction influence outward from the trench while keeping the bottom
    // itself down. This is what turns a vertical cut wall into the red-line
    // sloped profile instead of merely adding a berm beside a cliff.
    float localC =
        GroundRawCompaction(
            absoluteXY);

    float c6 =
        GroundRawCompaction(
            absoluteXY - outward * 6.0f);
    float c14 =
        GroundRawCompaction(
            absoluteXY - outward * 14.0f);
    float c28 =
        GroundRawCompaction(
            absoluteXY - outward * 28.0f);

    // PIXL_GR_13AM_SMOOTH_WALL_PROFILE_V1
    float wallSignal =
        saturate(
            c6 * 0.56f +
            c14 * 0.30f +
            c28 * 0.14f -
            localC * 0.58f);

    float previousLocalC =
        saturate(
            DeformableGround::SampleRawAbsolute(
                absoluteXY).z);

    float pc6 =
        saturate(
            DeformableGround::SampleRawAbsolute(
                absoluteXY - outward * 6.0f).z);
    float pc14 =
        saturate(
            DeformableGround::SampleRawAbsolute(
                absoluteXY - outward * 14.0f).z);
    float pc28 =
        saturate(
            DeformableGround::SampleRawAbsolute(
                absoluteXY - outward * 28.0f).z);

    float previousWallSignal =
        saturate(
            pc6 * 0.56f +
            pc14 * 0.30f +
            pc28 * 0.14f -
            previousLocalC * 0.58f);

    // Bulk phase establishes most of the wall motion by ~5 sec.
    // Edge phase then gently increases only the toe/rounding contribution.
    result.currentWallCollapse =
        wallSignal *
        bulkWeight *
        saturate(
            currentBulkPhase * 0.90f +
            currentEdgePhase * 0.10f);

    result.previousWallCollapse =
        previousWallSignal *
        bulkWeight *
        saturate(
            previousBulkPhase * 0.90f +
            previousEdgePhase * 0.10f);

    return result;
}


float PatchTessellation(
    float distanceToCamera,
    float detailDemand)
{
    if (GroundResponseRuntime::GeometrySelfTestEnabled())
        return 16.0f;

    float distanceSafe = max(distanceToCamera, 0.0f);

    // Existing runtime fields are reinterpreted without changing the b13 ABI:
    //   GeometryTessellationNear         = CLOSE quality
    //   GeometryTessellationFar          = MEDIUM quality
    //   GeometryTessellationNearDistance = CLOSE->MEDIUM boundary
    //   GeometryTessellationFarDistance  = MEDIUM->FAR boundary
    // FAR quality is derived automatically so no new constant-buffer member is
    // required and the proven 160-byte GroundResponse ABI remains untouched.
    float closeTess =
        clamp(GroundRuntimeGeometryTessellationNear, 1.0f, 16.0f);
    float mediumTess =
        clamp(GroundRuntimeGeometryTessellationFar, 1.0f, closeTess);
    float farTess =
        min(mediumTess, PIXL_FAR_VISUAL_TESS);

    float closeDistance =
        max(GroundRuntimeGeometryTessellationNearDistance, 1.0f);
    float mediumDistance =
        max(
            GroundRuntimeGeometryTessellationFarDistance,
            closeDistance + 1.0f);

    // Smooth ring transitions avoid visible LOD steps as terrain quads cross
    // the close/medium/far boundaries.
    float closeToMedium =
        smoothstep(
            closeDistance * 0.78f,
            closeDistance * 1.18f,
            distanceSafe);
    float mediumToFar =
        smoothstep(
            mediumDistance * 0.82f,
            mediumDistance * 1.14f,
            distanceSafe);

    float regularTess =
        lerp(
            closeTess,
            mediumTess,
            closeToMedium);
    regularTess =
        lerp(
            regularTess,
            farTess,
            mediumToFar);
    regularTess = clamp(regularTess, 1.0f, 16.0f);

    // A modest near-player floor keeps footprints from becoming faceted even
    // on flat snow, but no longer forces 14x over every nearby patch.
    float baseNearWeight =
        1.0f -
        smoothstep(
            PIXL_ADAPTIVE_BASE_NEAR_DISTANCE,
            PIXL_ADAPTIVE_BASE_BLEND_END,
            distanceSafe);
    float tess =
        lerp(
            regularTess,
            max(regularTess, PIXL_ADAPTIVE_BASE_TESS),
            baseNearWeight);

    // Procedural mound/drift regions earn additional tessellation. Close high-
    // detail edges can reach 16x; the boost fades with distance and disappears
    // completely before the normal far terrain regime.
    float detailDistanceWeight =
        1.0f -
        smoothstep(
            PIXL_ADAPTIVE_DETAIL_FULL_DISTANCE,
            PIXL_ADAPTIVE_DETAIL_BLEND_END,
            distanceSafe);

    float detailTess =
        lerp(
            PIXL_ADAPTIVE_DETAIL_MIN_TESS,
            PIXL_ADAPTIVE_DETAIL_MAX_TESS,
            saturate(detailDemand));

    float adaptiveWeight =
        saturate(detailDemand) *
        detailDistanceWeight;

    tess =
        lerp(
            tess,
            max(tess, detailTess),
            adaptiveWeight);

    return clamp(tess, 1.0f, 16.0f);
}

PATCH_CONSTANTS PatchConstants(
    InputPatch<TERRAIN_POINT, 3> patch,
    uint patchID : SV_PrimitiveID)
{
    PATCH_CONSTANTS output;
    output.Edge[0] = 1.0f;
    output.Edge[1] = 1.0f;
    output.Edge[2] = 1.0f;
    output.Inside = 1.0f;

    if (!GroundResponseRuntime::IsRuntimeValid() ||
        GroundRuntimeTerrainGeometryPass == 0u ||
        GroundRuntimeTerrainSnowValid == 0u ||
        SharedData::deformableGroundSettings.EnableDeformableGround == 0u)
        return output;

    float m0 = UnifiedSurfaceMaskAtPoint(patch[0]);
    float m1 = UnifiedSurfaceMaskAtPoint(patch[1]);
    float m2 = UnifiedSurfaceMaskAtPoint(patch[2]);
    float patchMask = max(m0, max(m1, m2));
    if (patchMask <= 1e-4f && !GroundResponseRuntime::GeometrySelfTestEnabled())
        return output;

    float3 p0 = patch[0].GroundBaseWorldPosition;
    float3 p1 = patch[1].GroundBaseWorldPosition;
    float3 p2 = patch[2].GroundBaseWorldPosition;
    float3 e0 = 0.5f * (p1 + p2);
    float3 e1 = 0.5f * (p2 + p0);
    float3 e2 = 0.5f * (p0 + p1);
    float d0 = length(e0);
    float d1 = length(e1);
    float d2 = length(e2);
    float renderDistance = max(GroundRuntimeGeometryRenderDistance, 1.0f);

    // Shared-edge factors depend only on the two vertices of the edge and its
    // midpoint. The procedural tess demand follows the same rule, so adjacent
    // terrain triangles cannot disagree on a shared tessellation edge.
    float edgeMask0 = max(m1, m2);
    float edgeMask1 = max(m2, m0);
    float edgeMask2 = max(m0, m1);

    float snow0 = TerrainSnowCoverage(patch[0]);
    float snow1 = TerrainSnowCoverage(patch[1]);
    float snow2 = TerrainSnowCoverage(patch[2]);

    float snowEdge0 =
        GroundResponseRuntime::GetSnowSurfaceMask(max(snow1, snow2));
    float snowEdge1 =
        GroundResponseRuntime::GetSnowSurfaceMask(max(snow2, snow0));
    float snowEdge2 =
        GroundResponseRuntime::GetSnowSurfaceMask(max(snow0, snow1));

    float2 absE0 = e0.xy + FrameBuffer::CameraPosAdjust.xy;
    float2 absE1 = e1.xy + FrameBuffer::CameraPosAdjust.xy;
    float2 absE2 = e2.xy + FrameBuffer::CameraPosAdjust.xy;

    float detail0 =
        GroundSnowAdaptiveTessDemand(absE0) *
        snowEdge0;
    float detail1 =
        GroundSnowAdaptiveTessDemand(absE1) *
        snowEdge1;
    float detail2 =
        GroundSnowAdaptiveTessDemand(absE2) *
        snowEdge2;

    if (GroundResponseRuntime::GeometrySelfTestEnabled()) {
        edgeMask0 = edgeMask1 = edgeMask2 = 1.0f;
        detail0 = detail1 = detail2 = 1.0f;
    }

    output.Edge[0] =
        edgeMask0 > 1e-4f && d0 < renderDistance
            ? PatchTessellation(d0, detail0)
            : 1.0f;
    output.Edge[1] =
        edgeMask1 > 1e-4f && d1 < renderDistance
            ? PatchTessellation(d1, detail1)
            : 1.0f;
    output.Edge[2] =
        edgeMask2 > 1e-4f && d2 < renderDistance
            ? PatchTessellation(d2, detail2)
            : 1.0f;
    output.Inside = max(output.Edge[0], max(output.Edge[1], output.Edge[2]));
    return output;
}

[domain("tri")]
[partitioning("fractional_odd")]
// D3D11 tessellator winding is opposite the rasterizer-front-face flag used
// by Skyrim's landscape passes in this pipeline. v2.1 mapped these directly,
// which culled the displaced surface from above and exposed only its underside.
// Reverse the mapping so the tessellated replay preserves the visible face.
// PIXL_GR_TOP_FACE_WINDING_V2
// PIXL_GR_13AN_PHYSICAL_TOP_IS_FRONT_V1
//
// GroundResponse.cpp selects this HS variant from the active rasterizer's
// FrontCounterClockwise flag. Keep the tessellator output in the SAME winding
// convention so the physical TOP of the raised snow/mud hull is SV_IsFrontFace.
#if defined(PIXL_TERRAIN_OUTPUT_CCW)
[outputtopology("triangle_ccw")]
#else
[outputtopology("triangle_cw")]
#endif
[outputcontrolpoints(3)]
[patchconstantfunc("PatchConstants")]
TERRAIN_POINT HSMain(
    InputPatch<TERRAIN_POINT, 3> patch,
    uint controlPointID : SV_OutputControlPointID,
    uint patchID : SV_PrimitiveID)
{
    return patch[controlPointID];
}

float4 Bary4(float4 a, float4 b, float4 c, float3 bary)
{
    return a * bary.x + b * bary.y + c * bary.z;
}

float3 Bary3(float3 a, float3 b, float3 c, float3 bary)
{
    return a * bary.x + b * bary.y + c * bary.z;
}

[domain("tri")]
TERRAIN_POINT DSMain(
    PATCH_CONSTANTS patchConstants,
    float3 bary : SV_DomainLocation,
    const OutputPatch<TERRAIN_POINT, 3> patch)
{
    TERRAIN_POINT output;

    output.Position = Bary4(patch[0].Position, patch[1].Position, patch[2].Position, bary);
    output.TexCoord0 = Bary4(patch[0].TexCoord0, patch[1].TexCoord0, patch[2].TexCoord0, bary);
    output.TBN0 = Bary3(patch[0].TBN0, patch[1].TBN0, patch[2].TBN0, bary);
    output.TBN1 = Bary3(patch[0].TBN1, patch[1].TBN1, patch[2].TBN1, bary);
    output.TBN2 = Bary3(patch[0].TBN2, patch[1].TBN2, patch[2].TBN2, bary);
    output.LandBlendWeights1 = Bary4(patch[0].LandBlendWeights1, patch[1].LandBlendWeights1, patch[2].LandBlendWeights1, bary);
    output.LandBlendWeights2 = Bary4(patch[0].LandBlendWeights2, patch[1].LandBlendWeights2, patch[2].LandBlendWeights2, bary);
    output.WorldPosition = Bary4(patch[0].WorldPosition, patch[1].WorldPosition, patch[2].WorldPosition, bary);
    output.PreviousWorldPosition = Bary4(patch[0].PreviousWorldPosition, patch[1].PreviousWorldPosition, patch[2].PreviousWorldPosition, bary);
    output.Color = Bary4(patch[0].Color, patch[1].Color, patch[2].Color, bary);
    output.FogParam = Bary4(patch[0].FogParam, patch[1].FogParam, patch[2].FogParam, bary);
    output.ModelPosition = Bary3(patch[0].ModelPosition, patch[1].ModelPosition, patch[2].ModelPosition, bary);
    output.GroundBaseWorldPosition = Bary3(
        patch[0].GroundBaseWorldPosition,
        patch[1].GroundBaseWorldPosition,
        patch[2].GroundBaseWorldPosition,
        bary);

    if (GroundResponseRuntime::IsRuntimeValid() &&
        GroundRuntimeTerrainGeometryPass != 0u &&
        GroundRuntimeTerrainSnowValid != 0u &&
        SharedData::deformableGroundSettings.EnableDeformableGround != 0u)
    {
        // PIXL_GR_13X_TBN_ORIENTATION_COMPENSATION_V1
        //
        // 13W deliberately flips the tessellator face winding so the physical
        // TOP of the raised shell is the rasterizer front face and therefore
        // writes/occludes correctly. Skyrim's landscape TBN, however, was built
        // for the original replay orientation. Preserve the old top-side
        // material/lighting basis while keeping the corrected raster winding.
        //
        // This is the TerrainSurface-only equivalent of the old PS back-face
        // TBN correction, so Lighting.hlsl does not need to be rebuilt.
        // PIXL_GR_13X_TBN_ORIENTATION_COMPENSATION_V1
        //
        // Physical top is front-facing again. Restore the known-good landscape
        // basis compensation paired with the 13W front-face winding contract.
        output.TBN0 = -output.TBN0;
        output.TBN1 = -output.TBN1;
        output.TBN2 = -output.TBN2;

        float distanceToCamera = length(output.GroundBaseWorldPosition);
        float previousDistanceToCamera = length(output.PreviousWorldPosition.xyz);
        float renderDistance = max(GroundRuntimeGeometryRenderDistance, 1.0f);
        float fadeStart = min(GroundRuntimeGeometryFadeStart, renderDistance - 1.0f);
        float distanceMask = 1.0f - smoothstep(
            max(fadeStart, 0.0f),
            renderDistance,
            distanceToCamera);
        float previousDistanceMask = 1.0f - smoothstep(
            max(fadeStart, 0.0f),
            renderDistance,
            previousDistanceToCamera);

        float currentRaise = 0.0f;
        float previousRaise = 0.0f;

        if (GroundResponseRuntime::GeometrySelfTestEnabled()) {
            // Deliberately absurd diagnostic displacement. It bypasses snow, mud,
            // collision and slope masks so one launch can prove whether HS/DS owns the draw.
            currentRaise = 64.0f * distanceMask;
            previousRaise = 64.0f * previousDistanceMask;
        } else {
            float snowCoverage = TerrainSnowCoverage(output);
            float3 baseGeometryNormal = GeometryNormalAtPoint(output);
            if (baseGeometryNormal.z < 0.0f)
                baseGeometryNormal = -baseGeometryNormal;
            float slopeMask = SlopeMaskFromNormal(baseGeometryNormal);

            float snowActivation = 0.0f;
            float mudActivation = 0.0f;
            GroundResponseRuntime::GetSurfaceActivations(
                snowCoverage,
                snowActivation,
                mudActivation);

            float baseSurfaceThickness =
                GroundResponseRuntime::GetUnifiedSurfaceThickness(snowCoverage) *
                slopeMask;
            float baseCompressedFloor =
                GroundResponseRuntime::GetUnifiedCompressedFloor(snowCoverage) *
                slopeMask;

            // GroundBaseWorldPosition is camera-relative in this terrain path.
            // Convert back to absolute XY so the procedural depth field is fixed
            // to Skyrim's world and never swims with the camera.
            float2 absoluteXY =
                output.GroundBaseWorldPosition.xy +
                FrameBuffer::CameraPosAdjust.xy;

            float depthVariationScale =
                GroundDepthVariationScale(
                    absoluteXY,
                    snowActivation,
                    mudActivation);

            float variedBaseSurfaceThickness =
                baseSurfaceThickness * depthVariationScale;

            // Ordinary SnowSurfaceThickness is intentionally still the global
            // blanket depth. Deep accumulation is a separate absolute-height
            // layer, allowing rare drifts to rise far beyond the 24-unit global
            // shell clamp without making every snow surface enormous.
            float deepDriftSignal =
                GroundSnowDeepDriftSignal(absoluteXY);

            // PIXL_GR_13BI_PROGRESSIVE_ROCK_REJECTION_V1
            // Detect sharp local geometric disagreement without touching authored
            // classification, the base shell, winding, raster depth or shadow code.
            // Each edge term depends only on its two shared vertices and its barycentric
            // edge weight, so neighbouring terrain triangles agree along shared edges.
            float3 rockN0 = GeometryNormalAtPoint(patch[0]);
            float3 rockN1 = GeometryNormalAtPoint(patch[1]);
            float3 rockN2 = GeometryNormalAtPoint(patch[2]);
            if (rockN0.z < 0.0f) rockN0 = -rockN0;
            if (rockN1.z < 0.0f) rockN1 = -rockN1;
            if (rockN2.z < 0.0f) rockN2 = -rockN2;

            float rockEdge01 =
                smoothstep(
                    0.055f,
                    0.30f,
                    1.0f - saturate(dot(rockN0, rockN1)));
            float rockEdge12 =
                smoothstep(
                    0.055f,
                    0.30f,
                    1.0f - saturate(dot(rockN1, rockN2)));
            float rockEdge20 =
                smoothstep(
                    0.055f,
                    0.30f,
                    1.0f - saturate(dot(rockN2, rockN0)));

            float rockConfidence =
                saturate(
                    max(
                        rockEdge01 * (4.0f * bary.x * bary.y),
                        max(
                            rockEdge12 * (4.0f * bary.y * bary.z),
                            rockEdge20 * (4.0f * bary.z * bary.x))));

            // Authored hard-surface filtering remains authoritative. This extra
            // confidence primarily prevents inherited snow material on sharp rock
            // protrusions from ballooning into deep/chest-high accumulation.
            float driftCompatibility =
                1.0f - rockConfidence * snowActivation * 0.92f;

            float deepSnowTargetHeight =
                PIXL_SNOW_MAX_PRISTINE_HEIGHT *
                snowActivation *
                slopeMask;
            float deepSnowAvailableRaise =
                max(
                    deepSnowTargetHeight -
                    variedBaseSurfaceThickness,
                    0.0f);

            variedBaseSurfaceThickness +=
                deepSnowAvailableRaise *
                deepDriftSignal *
                driftCompatibility;

            // PIXL_GR_13AC_CAPACITY_NORMALIZED_COMPACTION_V1
            //
            // The packed/base floor must NOT inherit procedural pristine-height
            // variation. Scaling the floor by depthVariationScale made neighbouring
            // points stop at different Z values, which turned deep compression into
            // visible contour shelves. Keep the floor tied to the underlying
            // surface/material classification instead.
            float variedBaseCompressedFloor =
                min(
                    variedBaseSurfaceThickness,
                    baseCompressedFloor);

            float requestedCompressionDepth =
                (max(
                    SharedData::deformableGroundSettings.SnowMaximumDepth,
                    0.0f) *
                    snowActivation +
                 max(
                    SharedData::deformableGroundSettings.MudMaximumDepth,
                    0.0f) *
                    mudActivation) *
                slopeMask;

            float referencePhysicalCapacity =
                max(
                    baseSurfaceThickness -
                    baseCompressedFloor,
                    1.0e-4f);

            // Interpret MaximumDepth as a fraction of the material's ordinary
            // compressible layer, then apply that same fraction to the LOCAL
            // pristine capacity. This makes every point descend proportionally
            // toward its floor instead of shallow points hitting the floor early
            // while thicker neighbours remain suspended as terraces.
            float compressionFraction =
                saturate(
                    requestedCompressionDepth /
                    referencePhysicalCapacity);

            float variedPhysicalCapacity =
                max(
                    variedBaseSurfaceThickness -
                    variedBaseCompressedFloor,
                    0.0f);

            float variedBaseMaximumCompression =
                variedPhysicalCapacity *
                compressionFraction;

            float surfaceThickness =
                variedBaseSurfaceThickness * distanceMask;
            float previousSurfaceThickness =
                variedBaseSurfaceThickness * previousDistanceMask;
            float compressedFloor =
                variedBaseCompressedFloor * distanceMask;
            float previousCompressedFloor =
                variedBaseCompressedFloor * previousDistanceMask;
            float maximumCompression =
                variedBaseMaximumCompression * distanceMask;
            float previousMaximumCompression =
                variedBaseMaximumCompression * previousDistanceMask;

            // PIXL_GR_13AA_VERTICAL_COMPACTION_ACTIVE_V1
            //
            // Sample the persistent t101 footprint directly. No depth-derived
            // neighbourhood expansion is performed: footprint width comes only
            // from the contact/stamp field, while MaximumDepth moves the raised
            // snow vertically toward its packed/base floor.
            // PIXL_GR_13Y_FAR_VISUAL_ONLY_V1
            //
            // At long range we only need the broad pristine drift silhouette.
            // The interaction clipmap is finite (+/- ~2044 units in XY), so
            // repeatedly performing the 17-tap slope-limited t101 lookup outside
            // that footprint wastes GPU work and cannot return track history.
            float interactionWeight =
                1.0f -
                smoothstep(
                    PIXL_INTERACTION_LOD_FULL_DISTANCE,
                    PIXL_INTERACTION_LOD_END_DISTANCE,
                    distanceToCamera);
            float previousInteractionWeight =
                1.0f -
                smoothstep(
                    PIXL_INTERACTION_LOD_FULL_DISTANCE,
                    PIXL_INTERACTION_LOD_END_DISTANCE,
                    previousDistanceToCamera);

            // One t103 bilinear sample per generated vertex supplies both
            // transient fire smoothing and signed elemental height.
            float4 elementalSnow = GroundElementSampleAbsolute(absoluteXY);

            DeformableGround::SurfaceCompression surface;
            surface.current = 0.0f;
            surface.previous = 0.0f;
            surface.freshness = 0.0f;
            surface.previousFreshness = 0.0f;

            if (max(interactionWeight, previousInteractionWeight) > 1.0e-4f) {
                surface =
                    GetGroundVerticalSurfaceCompression(
                        output.GroundBaseWorldPosition,
                        elementalSnow.yw);

                surface.current *= interactionWeight;
                surface.freshness *= interactionWeight;
                surface.previous *= previousInteractionWeight;
                surface.previousFreshness *= previousInteractionWeight;
            }

            // The pristine landscape replay is the raised shell. Compaction
            // now removes a proportional fraction of each point's local physical
            // capacity, so the whole contacted region approaches the packed/base
            // floor continuously instead of hitting it in contour-like terraces.
            currentRaise =
                max(
                    surfaceThickness -
                        maximumCompression * surface.current,
                    compressedFloor);
            previousRaise =
                max(
                    previousSurfaceThickness -
                        previousMaximumCompression * surface.previous,
                    previousCompressedFloor);

            // PIXL_GR_13AK_BULK_SLUMP_GEOMETRY_V1
            //
            // Scale the granular failure by local pristine snow height.
            // Normal footprints receive only a mild shoulder relaxation; deep
            // 40-72u drifts visibly collapse toward the red-line profile.
            float bulkWeight =
                smoothstep(
                    PIXL_BULK_SNOW_START,
                    PIXL_BULK_SNOW_FULL,
                    variedBaseSurfaceThickness) *
                snowActivation;

            if (bulkWeight > 1.0e-4f &&
                max(interactionWeight, previousInteractionWeight) > 1.0e-4f)
            {
                GroundBulkSlump slump =
                    GetGroundBulkSlump(
                        absoluteXY,
                        bulkWeight);

                // A. BULK WALL DROP / OUTWARD TOE
                // This temporarily lowers unsupported snow immediately outside
                // the trench, converting the cliff into a broad granular slope.
                float wallCollapseCapacity =
                    min(
                        variedBaseSurfaceThickness *
                            PIXL_WALL_COLLAPSE_HEIGHT_FRACTION,
                        PIXL_WALL_COLLAPSE_MAX) *
                    bulkWeight;

                float previousWallCollapseCapacity =
                    min(
                        previousSurfaceThickness *
                            PIXL_WALL_COLLAPSE_HEIGHT_FRACTION,
                        PIXL_WALL_COLLAPSE_MAX) *
                    bulkWeight;

                currentRaise =
                    max(
                        currentRaise -
                            slump.currentWallCollapse *
                            wallCollapseCapacity *
                            interactionWeight *
                            distanceMask,
                        compressedFloor);

                previousRaise =
                    max(
                        previousRaise -
                            slump.previousWallCollapse *
                            previousWallCollapseCapacity *
                            previousInteractionWeight *
                            previousDistanceMask,
                        previousCompressedFloor);

                // B. DISPLACED / SETTLED SNOW
                // The material translated out of the wall becomes a low, broad
                // shoulder. It loses height strongly as slide progress increases.
                float bermCapacity =
                    min(
                        variedBaseSurfaceThickness *
                            PIXL_SETTLED_BERM_HEIGHT_FRACTION,
                        PIXL_SETTLED_BERM_MAX_RAISE) *
                    bulkWeight;

                float currentBermHeight =
                    lerp(
                        1.0f,
                        0.22f,
                        smoothstep(
                            0.04f,
                            0.86f,
                            slump.currentProgress));

                float previousBermHeight =
                    lerp(
                        1.0f,
                        0.22f,
                        smoothstep(
                            0.04f,
                            0.86f,
                            slump.previousProgress));

                currentRaise +=
                    max(
                        slump.currentPile *
                            bermCapacity *
                            currentBermHeight *
                            interactionWeight *
                            distanceMask,
                        0.0f);

                previousRaise +=
                    max(
                        slump.previousPile *
                            bermCapacity *
                            previousBermHeight *
                            previousInteractionWeight *
                            previousDistanceMask,
                        0.0f);
            }

            // Elemental snow mass is independent of compaction history. Frost
            // adds a compressible layer; fire removes height and may reach the
            // authored base terrain, but never displaces geometry below it.
            float currentElemental =
                elementalSnow.x * snowActivation * slopeMask * distanceMask;
            float previousElemental =
                elementalSnow.z * snowActivation * slopeMask * previousDistanceMask;

            if (currentElemental > 0.0f)
                currentElemental *=
                    1.0f - saturate(surface.current * compressionFraction);
            if (previousElemental > 0.0f)
                previousElemental *=
                    1.0f - saturate(surface.previous * compressionFraction);

            currentRaise = max(currentRaise + currentElemental, 0.0f);
            previousRaise = max(previousRaise + previousElemental, 0.0f);
        }

        output.WorldPosition.z += currentRaise;
        output.PreviousWorldPosition.z += previousRaise;

        // b12 c8 is the same ViewProj used by Lighting VS. Re-project the camera-relative
        // displaced world position so raster depth, G-buffer and motion vectors agree.
        output.Position = mul(
            FrameBuffer::CameraViewProj,
            float4(output.WorldPosition.xyz, 1.0f));
    }

    return output;
}
