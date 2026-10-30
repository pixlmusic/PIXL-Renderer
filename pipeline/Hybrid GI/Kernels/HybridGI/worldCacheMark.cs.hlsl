#include "Common/FrameBuffer.hlsli"
#include "HybridGI/common.hlsli"
#include "HybridGI/worldCache.hlsli"

// Dedicated mark pass: b2 does not alter HybridGICB (b1) or SharedData (b5).
cbuffer EmitterChanges : register(b2)
{
    uint ChangeCount;
    uint ChangeOverflow;
    uint2 ChangePadding;
    float4 ChangeSpheres[128];
};
Texture2D<uint> srcMetadata : register(t0);
RWTexture2D<uint> outIntelligence : register(u0);

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
    if (any(pixel >= uint2(1024, 64))) return;
    uint meta = srcMetadata.Load(int3(pixel, 0));
    if ((meta & 0xffffffu) == 0u) return;
    uint cascade = pixel.y / WORLD_CACHE_DIM;
    int3 wrapped = int3(pixel.x & WORLD_CACHE_MASK, pixel.y & WORLD_CACHE_MASK, pixel.x / WORLD_CACHE_DIM);
    float size = WorldCacheCellSize(cascade);
    float3 cameraWS = ViewToWorldPosition(0.0f, FrameBuffer::CameraViewInverse) + FrameBuffer::CameraPosAdjust.xyz;
    int3 minimumCell = int3(floor(cameraWS / size)) - 16;
    int3 cell = minimumCell + ((wrapped - minimumCell) & 31);
    if ((meta & 0xffffffu) != WorldCacheHash(cell, cascade)) return;
    bool affected = ChangeOverflow != 0u;
    float3 minimumWS = float3(cell) * size;
    [loop] for (uint i = 0; i < min(ChangeCount, 128u) && !affected; ++i) {
        float4 sphere = ChangeSpheres[i];
        float3 delta = sphere.xyz - clamp(sphere.xyz, minimumWS, minimumWS + size);
        affected = dot(delta, delta) <= sphere.w * sphere.w;
    }
    if (affected) {
        uint word = outIntelligence[pixel];
        uint state = word >> 28;
        if (state == WORLD_STATE_DARK || state == WORLD_STATE_UNCERTAIN ||
            state == WORLD_STATE_RELIABLE || state == WORLD_STATE_STALE_LIGHT) {
            // Preserve moments/material and retain a bounded fallback until a
            // successful observation replaces this lighting generation.
            uint evidence = max((word >> 16) & 255u, 192u);
            outIntelligence[pixel] = (word & 0x0f00ffffu) | (evidence << 16) | (WORLD_STATE_STALE_LIGHT << 28);
        }
    }
}
