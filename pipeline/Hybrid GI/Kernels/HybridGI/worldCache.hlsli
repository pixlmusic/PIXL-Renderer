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

float WorldCacheCascadeBlend(float3 positionWS, float3 cameraWS)
{
    float nearExtent = WorldCacheCellSizeNear * (WORLD_CACHE_DIM * 0.47f);
    float3 diff = abs(positionWS - cameraWS);
    float maxDiff = max(diff.x, max(diff.y, diff.z));
    return smoothstep(0.72f, 1.0f, maxDiff / max(nearExtent, 1.0f));
}

uint WorldCacheCascade(float3 positionWS, float3 cameraWS)
{
    return WorldCacheCascadeBlend(positionWS, cameraWS) >= 0.5f ? 1u : 0u;
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
    float softStart = (float)maxAge * 0.65f;
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
