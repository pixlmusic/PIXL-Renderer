#ifndef PIXL_HybridGI_WORLD_CACHE
#define PIXL_HybridGI_WORLD_CACHE

#include "Common/Color.hlsli"

// PIXL persistent world irradiance cache.
// Two toroidal 32^3 cascades are flattened into a 1024x64 atlas. Metadata and
// the packed surface normal remain R32_UINT so publication can be atomic. The
// radiance payload is L2 (nine coefficient) luminance SH stored in three
// RGBA16F textures. Chroma is stored as YCoCg/Y ratios in SH2.yz.
#define WORLD_CACHE_DIM 32u
#define WORLD_CACHE_MASK 31u
#define WORLD_CACHE_CASCADES 2u
#define WORLD_CACHE_INVALID_HASH 0u
#define WORLD_CACHE_EPSILON 1e-5f

// Companion word: logarithmic relative variance, unresolved light change,
// material class, explicit observation state. Hash/age remain authoritative.
#define WORLD_STATE_UNOBSERVED 0u
#define WORLD_STATE_EMPTY 1u
#define WORLD_STATE_DARK 2u
#define WORLD_STATE_UNCERTAIN 3u
#define WORLD_STATE_RELIABLE 4u
#define WORLD_STATE_STALE_LIGHT 5u
#define WORLD_STATE_OCCLUDER 6u
#define WORLD_STATE_EMPTY_PENDING 7u
#define WORLD_STATE_OCCLUDER_PENDING 8u

// These classes describe proven opaque MRT signals, not engine material IDs.
// Emissive/particle class IDs are not encoded by the current deferred mask.
uint WorldObservedMaterial(float2 tissueAndDeformation)
{
    if (tissueAndDeformation.x > 0.0f) return 2u;
    return tissueAndDeformation.y > (1.0f / 255.0f) ? 1u : 0u;
}

float WorldMaterialResponse(uint material)
{
    return material == 1u ? 0.28f : material == 2u ? 0.18f : 0.0f;
}

uint WorldCacheRayBudget(uint maximum, float demand, float minimumFraction, uint tileKey, uint clock)
{
    maximum = clamp(maximum, 1u, 8u);
    // Keep periodic full-budget exploration even in a converged tile. Selection
    // rotates at the cache clock, independent of presentation/frame generation.
    if (((tileKey + clock) & 3u) == 0u) return maximum;
    float work = lerp(clamp(minimumFraction, 0.5f, 1.0f), 1.0f, smoothstep(0.10f, 0.75f, demand));
    return clamp((uint)ceil(float(maximum) * work), 1u, maximum);
}

uint WorldCacheRayIndex(uint sample, uint selected, uint maximum)
{
    // Stratify across the fixed reference ray set rather than retaining only
    // its near-normal prefix. Retained rays keep their original directions.
    return min(((2u * sample + 1u) * maximum) / (2u * max(selected, 1u)), maximum - 1u);
}

bool WorldCacheScheduleCell(uint hash, uint age, float confidence, uint intelligence, uint clock, float fraction)
{
    // Unknown geometry and changed lighting always take priority. The ageing
    // guard prevents any visible stable cell waiting more than two clock ticks.
    uint state = intelligence >> 28;
    if ((asuint(fraction) & 0x7f800000u) == 0x7f800000u || fraction <= 0.0f || fraction >= 1.0f || age >= 2u || confidence < 0.8f ||
        state < WORLD_STATE_DARK || state > WORLD_STATE_RELIABLE ||
        state == WORLD_STATE_UNCERTAIN ||
        ((intelligence >> 16) & 255u) != 0u || (intelligence & 65535u) > 1319u ||
        ((intelligence >> 24) & 15u) != 0u)
        return true;
    // Stable world-space identity, independent of the camera and wrapped atlas
    // slot. Rotate four strata at each fixed tick; minimum budget is four.
    uint quota = clamp((uint)ceil(saturate(fraction) * 16.0f), 4u, 16u);
    return ((hash + clock * 4u) & 15u) < quota;
}

bool WorldCacheExpired(uint metadata, uint intelligence, uint clock, uint maxAge)
{
    if ((metadata & 0x00ffffffu) == WORLD_CACHE_INVALID_HASH) return false;
    uint lifetime = maxAge;
#ifdef CACHE_INTELLIGENCE
    uint state = intelligence >> 28;
    if (state == WORLD_STATE_EMPTY || state == WORLD_STATE_EMPTY_PENDING ||
        state == WORLD_STATE_OCCLUDER || state == WORLD_STATE_OCCLUDER_PENDING)
        lifetime = min(lifetime, 4u);
#endif
    return (((clock & 255u) - (metadata >> 24)) & 255u) > lifetime;
}

// UINT_MAX means preserve the existing record. Two distinct ray witnesses
// and a separate fixed clock tick are required to promote either evidence type.
uint WorldResolveEvidenceState(uint bits, uint oldState, bool matching, uint age)
{
    if (matching && oldState >= WORLD_STATE_DARK && oldState <= WORLD_STATE_STALE_LIGHT)
        return 0xffffffffu;
    bool hit = (bits & 12u) == 12u;
    bool empty = (bits & 3u) == 3u && (bits & 12u) == 0u;
    if (!hit && !empty) return 0xffffffffu;
    if (matching && age == 0u) return 0xffffffffu;
    uint pending = hit ? WORLD_STATE_OCCLUDER_PENDING : WORLD_STATE_EMPTY_PENDING;
    uint confirmed = hit ? WORLD_STATE_OCCLUDER : WORLD_STATE_EMPTY;
    return matching && age <= 4u && (oldState == pending || oldState == confirmed) ? confirmed : pending;
}

uint PackWorldIntelligence(float variance, float lightChange, uint material, uint state)
{
    float boundedVariance = ((asuint(variance) & 0x7f800000u) != 0x7f800000u) ? clamp(variance, 0.0f, 65535.0f) : 65535.0f;
    uint v = (uint)round(log2(1.0f + boundedVariance) * (65535.0f / 16.0f));
    uint change = (uint)round(saturate(lightChange) * 255.0f);
    return v | (change << 16) | ((material & 15u) << 24) | ((state & 15u) << 28);
}

float WorldIntelligenceVariance(uint packed)
{
    return exp2(float(packed & 65535u) * (16.0f / 65535.0f)) - 1.0f;
}

float WorldIntelligenceReliability(uint packed)
{
#ifndef CACHE_INTELLIGENCE
    return 1.0f;
#else
    uint state = packed >> 28;
    if (state < WORLD_STATE_DARK || state > WORLD_STATE_STALE_LIGHT)
        return 0.0f;
    float change = float((packed >> 16) & 255u) / 255.0f;
    return rcp(1.0f + WorldIntelligenceVariance(packed)) * (1.0f - change);
#endif
}

// A cell stores a normal but not the representative point. The projected
// half-box support bounds every possible point on that cell's surface. Only
// receivers beyond that uncertainty band provide evidence of a wrong side.
float WorldReceiverPlaneConfidence(float3 receiverWS, int3 sourceCell,
    float3 sourceNormal, float cellSize, float reliability)
{
#ifndef CACHE_INTELLIGENCE
    return 1.0f;
#else
    float3 center = (float3(sourceCell) + 0.5f) * cellSize;
    float support = max(0.5f * cellSize * dot(abs(sourceNormal), 1.0f.xxx), 1e-3f);
    float separation = dot(receiverWS - center, sourceNormal);
    float compatible = smoothstep(-2.0f * support, -support, separation);
    return lerp(1.0f, compatible, saturate(reliability));
#endif
}

// Occupancy includes age, variance and plane compatibility. Normalize only
// the radiance mixture; never apply confidence to stored energy itself.
float WorldCascadeRadianceBlend(float blend, float nearConfidence, float farConfidence)
{
#ifdef CACHE_INTELLIGENCE
    float nearWeight = (1.0f - blend) * nearConfidence;
    float farWeight = blend * farConfidence;
    return farWeight / max(nearWeight + farWeight, 1e-6f);
#else
    return blend;
#endif
}

float WorldCacheBlendDistance(float3 diff, float nearExtent)
{
#ifdef CACHE_INTELLIGENCE
    float radialDistance = length(diff * float3(1.0f, 1.0f, 0.75f));
    float windowDistance = max(abs(diff.x), max(abs(diff.y), abs(diff.z)));
    float windowBlend = smoothstep(0.90f, 1.06f, windowDistance / max(nearExtent, 1.0f));
    return max(windowBlend, smoothstep(0.62f, 1.08f, radialDistance / max(nearExtent, 1.0f)));
#else
    float radialDistance = length(diff);
    return smoothstep(0.62f, 1.08f, radialDistance / max(nearExtent, 1.0f));
#endif
}

uint WorldCacheHash(int3 cell, uint cascade)
{
    uint3 v = (uint3)cell;
    uint h = v.x * 0x8da6b343u ^ v.y * 0xd8163841u ^ v.z * 0xcb1ab31fu ^ cascade * 0x165667b1u;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    return (h & 0x00fffffeu) + 1u;
}

uint2 WorldCacheAtlasCoord(int3 cell, uint cascade)
{
    uint3 wrapped = (uint3)(cell & (int)WORLD_CACHE_MASK);
    return uint2(wrapped.x + wrapped.z * WORLD_CACHE_DIM, wrapped.y + cascade * WORLD_CACHE_DIM);
}

float2 EncodeWorldOctahedron(float3 normal)
{
    normal *= rcp(abs(normal.x) + abs(normal.y) + abs(normal.z) + 1e-6f);
    float2 encoded = normal.xy;
    if (normal.z < 0.0f) {
        float2 s = sign(encoded);
        s.x = (s.x == 0.0f) ? 1.0f : s.x;
        s.y = (s.y == 0.0f) ? 1.0f : s.y;
        encoded = (1.0f - abs(encoded.yx)) * s;
    }
    return encoded * 0.5f + 0.5f;
}

float3 DecodeWorldOctahedron(float2 encoded)
{
    float2 f = encoded * 2.0f - 1.0f;
    float3 n = float3(f, 1.0f - abs(f.x) - abs(f.y));
    float t = saturate(-n.z);
    float2 s = sign(n.xy);
    s.x = (s.x == 0.0f) ? 1.0f : s.x;
    s.y = (s.y == 0.0f) ? 1.0f : s.y;
    n.xy -= s * t;
    return normalize(n);
}

uint PackWorldSurface(float3 normal, float occupancy, float confidence)
{
    uint2 encoded = (uint2)round(saturate(EncodeWorldOctahedron(normal)) * 255.0f);
    uint packedOccupancy = (uint)round(saturate(occupancy) * 255.0f);
    uint packedConfidence = (uint)round(saturate(confidence) * 255.0f);
    return encoded.x | (encoded.y << 8) | (packedOccupancy << 16) | (packedConfidence << 24);
}

float3 UnpackWorldNormal(uint value)
{
    float2 encoded = float2(value & 255u, (value >> 8) & 255u) * (1.0f / 255.0f);
    return DecodeWorldOctahedron(encoded);
}
float UnpackWorldOccupancy(uint value) { return ((value >> 16) & 255u) * (1.0f / 255.0f); }
float UnpackWorldConfidence(uint value) { return (value >> 24) * (1.0f / 255.0f); }

// Real SH basis, bands 0..2.  Keep the basis expansion inline rather than
// materialising temporary float4 basis variables.  The legacy FXC path used by
// Skyrim's FXC integration can misparse those temporary declarations in an
// included helper even though the same source is accepted by newer HLSL parsers.
// The scalar form is mathematically identical and also gives FXC more constant
// folding opportunities.

// Project a Lambert/cosine lobe centred on surfaceNormal. The band factors are
// the analytic clamped-cosine convolution factors (pi, 2pi/3, pi/4).
void WorldCacheProjectRadiance(float3 radiance, float3 surfaceNormal,
    out float4 sh0, out float4 sh1, out float4 sh2)
{
    radiance = max(filterInf(filterNaN(radiance)), 0.0f);

    float3 n = normalize(surfaceNormal);
    float3 ycocg = Color::RGBToYCoCg(radiance);
    float luminanceY = max(ycocg.x, 0.0f);
    float invLuminanceY = rcp(max(luminanceY, 1e-4f));

    const float SH_L0 = 0.2820947918f;
    const float SH_L1 = 0.4886025119f;
    const float SH_L2A = 1.0925484306f;
    const float SH_L2B = 0.3153915653f;
    const float SH_L2C = 0.5462742153f;
    const float COSINE_L0 = 3.14159265359f;
    const float COSINE_L1 = 2.09439510239f;
    const float COSINE_L2 = 0.78539816339f;

    // Assign components individually. Some Skyrim FXC
    // compilation contexts expose a value symbol named `float4`; type declarations
    // remain valid, but constructor syntax `float4(...)` is then parsed as a call to
    // that value and fails with X3005.  Component writes are fully equivalent and
    // avoid the ambiguity without changing the SH payload ABI.
    sh0.x = luminanceY * (SH_L0 * COSINE_L0);
    sh0.y = luminanceY * (SH_L1 * n.y * COSINE_L1);
    sh0.z = luminanceY * (SH_L1 * n.z * COSINE_L1);
    sh0.w = luminanceY * (SH_L1 * n.x * COSINE_L1);

    sh1.x = luminanceY * (SH_L2A * n.x * n.y * COSINE_L2);
    sh1.y = luminanceY * (SH_L2A * n.y * n.z * COSINE_L2);
    sh1.z = luminanceY * (SH_L2B * (3.0f * n.z * n.z - 1.0f) * COSINE_L2);
    sh1.w = luminanceY * (SH_L2A * n.x * n.z * COSINE_L2);

    sh2.x = luminanceY * (SH_L2C * (n.x * n.x - n.y * n.y) * COSINE_L2);
    sh2.y = ycocg.y * invLuminanceY;
    sh2.z = ycocg.z * invLuminanceY;
    sh2.w = luminanceY;
}

float WorldCacheEvaluateLuminance(float4 sh0, float4 sh1, float4 sh2, float3 direction)
{
    float3 d = normalize(direction);

    const float SH_L0 = 0.2820947918f;
    const float SH_L1 = 0.4886025119f;
    const float SH_L2A = 1.0925484306f;
    const float SH_L2B = 0.3153915653f;
    const float SH_L2C = 0.5462742153f;

    float result = sh0.x * SH_L0;
    result += sh0.y * (SH_L1 * d.y);
    result += sh0.z * (SH_L1 * d.z);
    result += sh0.w * (SH_L1 * d.x);
    result += sh1.x * (SH_L2A * d.x * d.y);
    result += sh1.y * (SH_L2A * d.y * d.z);
    result += sh1.z * (SH_L2B * (3.0f * d.z * d.z - 1.0f));
    result += sh1.w * (SH_L2A * d.x * d.z);
    result += sh2.x * (SH_L2C * (d.x * d.x - d.y * d.y));
    return max(result, 0.0f);
}

float3 WorldCacheEvaluateRadiance(float4 sh0, float4 sh1, float4 sh2, float3 direction)
{
    float y = WorldCacheEvaluateLuminance(sh0, sh1, sh2, direction);
    float2 chroma = clamp(sh2.yz, -4.0f, 4.0f) * y;
    return max(Color::YCoCgToRGB(float3(y, chroma)), 0.0f);
}

// A stable scalar representing the payload energy, used only for temporal
// response/firefly decisions. SH2.w stores the injected luminance explicitly.
float WorldCachePayloadLuminance(float4 sh2) { return max(sh2.w, 0.0f); }

float WorldCacheCellSize(uint cascade)
{
    return cascade == 0u ? WorldCacheCellSizeNear : WorldCacheCellSizeFar;
}

bool WorldCacheCellInWindow(int3 cell, float3 cameraWS, uint cascade)
{
    float cellSize = WorldCacheCellSize(cascade);
    int3 cameraCell = int3(floor(cameraWS / cellSize));
    int3 offset = cell - cameraCell;
    // A 32-cell toroidal axis has one unique signed window: [-16, 16).
    // Never publish/read cells outside it, even during the near/far blend band,
    // otherwise unrelated world cells can compete for the same wrapped slot.
    return all(offset >= -16) && all(offset < 16);
}

bool WorldCachePositionInWindow(float3 positionWS, float3 cameraWS, uint cascade)
{
    float cellSize = WorldCacheCellSize(cascade);
    return WorldCacheCellInWindow(int3(floor(positionWS / cellSize)), cameraWS, cascade);
}

float WorldCacheCascadeBlend(float3 positionWS, float3 cameraWS)
{
    float nearExtent = WorldCacheCellSizeNear * (WORLD_CACHE_DIM * 0.47f);
    float3 diff = positionWS - cameraWS;
    // Skyrim's vertical axis is Z. Retain fine cells longer above/below the
    // camera, while the cube-window guard hands off before addressing aliases.
    return WorldCacheBlendDistance(diff, nearExtent);
}

uint WorldCacheCascade(float3 positionWS, float3 cameraWS)
{
    return WorldCacheCascadeBlend(positionWS, cameraWS) >= 0.5f ? 1u : 0u;
}

float WorldCacheTraceCellSize(float3 positionWS, float3 cameraWS)
{
    // Match ray spacing continuously to the near/far lookup transition.
    return lerp(WorldCacheCellSizeNear, WorldCacheCellSizeFar,
        WorldCacheCascadeBlend(positionWS, cameraWS));
}

float WorldCacheSmoothRotation(float3 positionWS)
{
    // World-locked smooth noise avoids rotating all rays abruptly at voxel edges.
    float3 p = positionWS / max(WorldCacheCellSizeNear * 2.0f, 128.0f);
    int3 cell = int3(floor(p));
    float3 f = frac(p);
    f = f * f * (3.0f - 2.0f * f);
    float h000 = (WorldCacheHash(cell, 0u) & 65535u) * (1.0f / 65536.0f);
    float h100 = (WorldCacheHash(cell + int3(1, 0, 0), 0u) & 65535u) * (1.0f / 65536.0f);
    float h010 = (WorldCacheHash(cell + int3(0, 1, 0), 0u) & 65535u) * (1.0f / 65536.0f);
    float h110 = (WorldCacheHash(cell + int3(1, 1, 0), 0u) & 65535u) * (1.0f / 65536.0f);
    float h001 = (WorldCacheHash(cell + int3(0, 0, 1), 0u) & 65535u) * (1.0f / 65536.0f);
    float h101 = (WorldCacheHash(cell + int3(1, 0, 1), 0u) & 65535u) * (1.0f / 65536.0f);
    float h011 = (WorldCacheHash(cell + int3(0, 1, 1), 0u) & 65535u) * (1.0f / 65536.0f);
    float h111 = (WorldCacheHash(cell + int3(1, 1, 1), 0u) & 65535u) * (1.0f / 65536.0f);
    float lo = lerp(lerp(h000, h100, f.x), lerp(h010, h110, f.x), f.y);
    float hi = lerp(lerp(h001, h101, f.x), lerp(h011, h111, f.x), f.y);
    return lerp(lo, hi, f.z);
}

float WorldCacheStableRotationForCascade(float3 positionWS, uint cascade)
{
    float cellSize = WorldCacheCellSize(cascade);
    int3 cell = (int3)floor(positionWS / cellSize);
    return (float)(WorldCacheHash(cell, cascade) & 0x0000ffffu) * 1.5258789e-5f;
}

float WorldCacheStableRotation(float3 positionWS, float3 cameraWS)
{
    return WorldCacheStableRotationForCascade(positionWS, WorldCacheCascade(positionWS, cameraWS));
}

float WorldCacheAgeFade(uint age, uint maxAge)
{
    // Visible voxels refresh at 30 Hz, but off-screen cells can only be
    // rediscovered after camera movement. Fade them after one quarter of their
    // lifetime so stale lighting does not remain at full strength for most of
    // the cache window. GI, secondary bounce, and voxel reflections share this.
    float softStart = (float)maxAge * 0.25f;
    return 1.0f - smoothstep(softStart, (float)max(maxAge, 1u), (float)age);
}

void WorldCacheBasis(float3 normalWS, out float3 tangent, out float3 bitangent)
{
    float3 up = abs(normalWS.z) < 0.95f ? float3(0, 0, 1) : float3(0, 1, 0);
    tangent = normalize(cross(up, normalWS));
    bitangent = cross(normalWS, tangent);
}

float3 WorldCacheDirectionFromBasis(uint index, uint count, float3 normalWS,
    float3 tangent, float3 bitangent, float rotation)
{
    float u = ((float)index + 0.5f) * rcp(max((float)count, 1.0f));
    float phi = 6.28318530718f * frac((float)index * 0.61803398875f + rotation);
    // Uniform solid-angle hemisphere sampling. The previous 0.8 cap never
    // reached grazing directions (cos(theta) stayed >= ~0.45), starving
    // wall-to-wall transport and directional occlusion. A uniform hemisphere
    // also makes the SH projection estimator exact with the 2*PI/N weight used
    // by gi.cs.hlsl.
    float cosTheta = saturate(1.0f - u);
    float sinTheta = sqrt(saturate(1.0f - cosTheta * cosTheta));
    float s, c;
    sincos(phi, s, c);
    return normalize(tangent * (c * sinTheta) + bitangent * (s * sinTheta) + normalWS * cosTheta);
}

float3 WorldCacheDirection(uint index, uint count, float3 normalWS, float rotation)
{
    float3 tangent;
    float3 bitangent;
    WorldCacheBasis(normalWS, tangent, bitangent);
    return WorldCacheDirectionFromBasis(index, count, normalWS, tangent, bitangent, rotation);
}

#endif
