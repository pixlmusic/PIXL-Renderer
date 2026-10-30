#include "Common/FrameBuffer.hlsli"
#include "HybridGI/common.hlsli"
#include "HybridGI/worldCache.hlsli"

// Reuse the election texture after publication: two empty witness bits and two
// depth-hit bits. No extra persistent validity atlas or cross-thread payload writes.
Texture2D<float> srcDepth : register(t0);
RWTexture2D<uint> evidence : register(u0);
RWTexture2D<uint> metadata : register(u1);
RWTexture2D<uint> intelligence : register(u2);

float3 EvidenceCameraWS()
{
    return ViewToWorldPosition(0.0f, FrameBuffer::CameraViewInverse) + FrameBuffer::CameraPosAdjust.xyz;
}

#ifdef RESOLVE_EVIDENCE
[numthreads(8,8,1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
    if (any(pixel >= uint2(1024,64))) return;
    uint bits = evidence[pixel];
    if (bits == 0u) return;
    uint cascade = pixel.y / 32u;
    float size = WorldCacheCellSize(cascade);
    int3 minimumCell = int3(floor(EvidenceCameraWS() / size)) - 16;
    int3 wrapped = int3(pixel.x & 31u, pixel.y & 31u, pixel.x / 32u);
    int3 cell = minimumCell + ((wrapped - minimumCell) & 31);
    uint hash = WorldCacheHash(cell, cascade);
    uint oldMeta = metadata[pixel];
    uint oldState = intelligence[pixel] >> 28;
    bool matching = (oldMeta & 0xffffffu) == hash;
    uint age = ((WorldCacheClock & 255u) - (oldMeta >> 24)) & 255u;
    uint state = WorldResolveEvidenceState(bits, oldState, matching, age);
    if (state == 0xffffffffu) return;
    intelligence[pixel] = PackWorldIntelligence(0.0f, 0.0f, 0u, state);
    metadata[pixel] = ((WorldCacheClock & 255u) << 24) | hash;
}
#else
[numthreads(8,8,1)]
void main(uint2 thread : SV_DispatchThreadID)
{
    uint stride = max(WorldCacheInjectionStride, 1u);
    uint2 pixel = thread * stride;
    if (any(pixel >= uint2(OUT_FRAME_DIM))) return;
    float2 uv = (float2(pixel) + 0.5f) * RCP_OUT_FRAME_DIM;
    float rawDepth = srcDepth.SampleLevel(samplerPointClamp, FullFrameTextureUV(uv), 0);
    if (rawDepth >= 1.0f || rawDepth <= 0.0f) return;
    float depth = ScreenToViewDepth(rawDepth);
    if (!isfinite(depth) || depth <= FP_Z || depth >= DepthFadeRange.y) return;
    float3 camera = EvidenceCameraWS();
    float3 hit = ViewToWorldPosition(ScreenToViewPosition(uv, depth), FrameBuffer::CameraViewInverse) + FrameBuffer::CameraPosAdjust.xyz;
    float3 segment = hit - camera;
    float lengthWS = length(segment);
    if (!isfinite(lengthWS) || lengthWS <= 1e-3f) return;
    float3 direction = segment / lengthWS;
    uint witness = 1u << ((thread.x + thread.y) & 1u);
    [unroll] for (uint cascade = 0u; cascade < 2u; ++cascade) {
        float size = WorldCacheCellSize(cascade);
        int3 hitCell = int3(floor(hit / size));
        if (WorldCacheCellInWindow(hitCell, camera, cascade))
            InterlockedOr(evidence[WorldCacheAtlasCoord(hitCell, cascade)], witness << 2u);
        [unroll] for (uint step = 1u; step <= 8u; ++step) {
            float distance = lengthWS - size * float(step);
            if (distance <= 0.0f) break;
            int3 cell = int3(floor((camera + direction * distance) / size));
            if (all(cell == hitCell) || !WorldCacheCellInWindow(cell, camera, cascade)) continue;
            InterlockedOr(evidence[WorldCacheAtlasCoord(cell, cascade)], witness);
        }
    }
}
#endif
