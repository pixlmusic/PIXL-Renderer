#include "Common/Color.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/GBuffer.hlsli"
#include "HybridGI/common.hlsli"
#include "HybridGI/worldCache.hlsli"

Texture2D<float> srcWorkingDepth : register(t0);
Texture2D<float2> srcNormal : register(t1);
Texture2D<float3> srcRadiance : register(t2);
Texture2D<float3> srcAlbedo : register(t3);
Texture2D<uint> srcPrevWorldMetadata : register(t4);
Texture2D<float4> srcPrevWorldSH0 : register(t5);
Texture2D<float4> srcPrevWorldSH1 : register(t6);
Texture2D<float4> srcPrevWorldSH2 : register(t7);
Texture2D<uint> srcPrevWorldNormal : register(t8);

RWTexture2D<uint> outWorldMetadata : register(u0);
RWTexture2D<float4> outWorldSH0 : register(u1);
RWTexture2D<float4> outWorldSH1 : register(u2);
RWTexture2D<float4> outWorldSH2 : register(u3);
RWTexture2D<uint> outWorldNormal : register(u4);

bool ReadPreviousVoxel(float3 queryWS, uint cascade, float3 receiverWS,
    out float3 incomingRadiance, out float occupancy)
{
    incomingRadiance = 0.0f;
    occupancy = 0.0f;
    float cellSize = WorldCacheCellSize(cascade);
    int3 cell = int3(floor(queryWS / cellSize));
    uint2 coord = WorldCacheAtlasCoord(cell, cascade);
    uint meta = srcPrevWorldMetadata.Load(int3(coord, 0));
    if ((meta & 0x00ffffffu) != WorldCacheHash(cell, cascade))
        return false;
    uint age = ((FrameIndex & 255u) - (meta >> 24)) & 255u;
    float ageFade = WorldCacheAgeFade(age, WorldCacheMaxAge);
    if (ageFade <= 0.0f)
        return false;

    uint surface = srcPrevWorldNormal.Load(int3(coord, 0));
    float confidence = UnpackWorldConfidence(surface) * ageFade;
    // Confidence belongs to the sampling weight, not to emitted radiance.
    occupancy = UnpackWorldOccupancy(surface) * confidence;
    float3 fromSourceToReceiver = normalize(receiverWS - queryWS);
    float3 sourceNormal = UnpackWorldNormal(surface);
    // Gate the back side rather than multiplying by another cosine.  The SH
    // payload already encodes directional emission and double-cosine weighting
    // was making the second bounce effectively disappear.
    float sourceFacingSigned = dot(sourceNormal, fromSourceToReceiver);
    float sourceGate = smoothstep(-0.05f, 0.15f, sourceFacingSigned);
    float leakWeight = lerp(1.0f, sourceGate, saturate(WorldCacheLeakReduction));
    incomingRadiance = WorldCacheEvaluateRadiance(
        srcPrevWorldSH0.Load(int3(coord, 0)),
        srcPrevWorldSH1.Load(int3(coord, 0)),
        srcPrevWorldSH2.Load(int3(coord, 0)),
        fromSourceToReceiver) * leakWeight;
    return occupancy > (1.0f / 255.0f) && leakWeight > 1e-3f;
}

float3 SampleSecondaryBounce(float3 worldPosition, float3 worldNormal, float3 albedo, uint cascade)
{
    if (WorldCacheSecondBounceEnabled == 0u || WorldCacheSecondBounceStrength <= 0.0f)
        return 0.0f;

    float cellSize = WorldCacheCellSize(cascade);
    float3 up = abs(worldNormal.z) < 0.95f ? float3(0, 0, 1) : float3(0, 1, 0);
    float3 t = normalize(cross(up, worldNormal));
    float3 b = cross(worldNormal, t);
    // Explicit assignment is friendlier to older FXC/SM5 front-ends than a
    // local aggregate initializer containing function calls.
    float3 dirs[5];
    dirs[0] = worldNormal;
    dirs[1] = normalize(worldNormal + t);
    dirs[2] = normalize(worldNormal - t);
    dirs[3] = normalize(worldNormal + b);
    dirs[4] = normalize(worldNormal - b);

    float3 incoming = 0.0f;
    float weightSum = 0.0f;
    [unroll] for (uint i = 0u; i < 5u; ++i) {
        float3 query = worldPosition + dirs[i] * (cellSize * 1.5f);
        float3 sampleRadiance;
        float occupancy;
        if (ReadPreviousVoxel(query, cascade, worldPosition, sampleRadiance, occupancy)) {
            float facing = saturate(dot(worldNormal, dirs[i]));
            float w = occupancy * lerp(0.35f, 1.0f, facing);
            incoming += sampleRadiance * w;
            weightSum += w;
        }
    }
    if (weightSum > 1e-4f)
        incoming *= rcp(weightSum);

    // Lambertian second bounce; keep it deliberately bounded to prevent cache
    // feedback from creating energy over multiple frames.
    return incoming * saturate(albedo) * (WorldCacheSecondBounceStrength / 3.14159265359f);
}

void InjectWorldVoxel(float3 worldPosition, float3 worldNormal, float3 radiance, float3 albedo, uint cascade)
{
    float cellSize = WorldCacheCellSize(cascade);
    int3 cell = int3(floor(worldPosition / cellSize));
    uint2 coord = WorldCacheAtlasCoord(cell, cascade);
    uint hash = WorldCacheHash(cell, cascade);

    uint prevMeta = srcPrevWorldMetadata.Load(int3(coord, 0));
    uint prevSurface = srcPrevWorldNormal.Load(int3(coord, 0));
    uint prevAge = ((FrameIndex & 255u) - (prevMeta >> 24)) & 255u;
    bool historyValid = ((prevMeta & 0x00ffffffu) == hash) && prevAge <= WorldCacheMaxAge;

    radiance += SampleSecondaryBounce(worldPosition, worldNormal, albedo, cascade);
    radiance = max(filterInf(filterNaN(radiance)), 0.0f);

    float clampValue = RadianceFireflyClamp > 0.0f ? RadianceFireflyClamp : 4.0f;
    radiance = ClampFireflies(radiance, clampValue);

    float4 newSH0, newSH1, newSH2;
    WorldCacheProjectRadiance(radiance, worldNormal, newSH0, newSH1, newSH2);
    float confidence = 0.20f;

    if (historyValid) {
        float4 oldSH0 = srcPrevWorldSH0.Load(int3(coord, 0));
        float4 oldSH1 = srcPrevWorldSH1.Load(int3(coord, 0));
        float4 oldSH2 = srcPrevWorldSH2.Load(int3(coord, 0));
        float3 oldNormal = UnpackWorldNormal(prevSurface);
        float oldLum = WorldCachePayloadLuminance(oldSH2);
        float newLum = WorldCachePayloadLuminance(newSH2);
        float relativeChange = saturate(abs(newLum - oldLum) / max(max(oldLum, newLum), 0.05f));
        float normalAgreement = dot(oldNormal, worldNormal);
        float response = saturate(WorldCacheTemporalResponse);

        if (newLum > oldLum) {
            float accepted = max(oldLum * 1.75f, oldLum + 0.35f);
            if (newLum > accepted && newLum > 1e-5f) {
                float scale = accepted / newLum;
                newSH0 *= scale;
                newSH1 *= scale;
                newSH2.xw *= scale;
                newLum = accepted;
            }
            response *= lerp(1.0f, 0.35f, relativeChange);
        } else {
            response = lerp(response, max(response, 0.30f), relativeChange);
        }

        if (normalAgreement < 0.35f)
            response = min(response, 0.04f);

        newSH0 = lerp(oldSH0, newSH0, response);
        newSH1 = lerp(oldSH1, newSH1, response);
        // Chroma ratios should not be scaled as radiance energy, but are still
        // temporally averaged to prevent hue flicker in coarse cells.
        newSH2 = lerp(oldSH2, newSH2, response);

        if (normalAgreement > 0.35f)
            worldNormal = normalize(lerp(oldNormal, worldNormal, response));
        else
            worldNormal = oldNormal;

        float oldConfidence = UnpackWorldConfidence(prevSurface);
        bool firstUpdateThisFrame = (prevMeta >> 24) != (FrameIndex & 255u);
        confidence = firstUpdateThisFrame ? min(oldConfidence + 0.20f, 1.0f) : oldConfidence;
        if (normalAgreement < 0.35f)
            confidence = min(confidence, max(oldConfidence, 0.20f));
    }

    // Claim this toroidal texel before publishing a multi-texture payload.
    // Several visible pixels often map to one voxel; without a claim, SH0/1/2
    // can be written by different threads and form an incoherent radiance
    // record. The previous-frame snapshot gives every contender the same
    // expected value, so exactly one thread wins this frame. Hash 0 is reserved
    // as invalid and therefore doubles as a transient write lock.
    uint writeLock = (FrameIndex & 255u) << 24;
    uint observed;
    InterlockedCompareExchange(outWorldMetadata[coord], prevMeta, writeLock, observed);
    if (observed != prevMeta)
        return;

    // Publish the complete payload and make the real hash/timestamp visible
    // last. No GI/reflection pass reads this UAV until the dispatch completes.
    outWorldSH0[coord] = newSH0;
    outWorldSH1[coord] = newSH1;
    outWorldSH2[coord] = newSH2;
    outWorldNormal[coord] = PackWorldSurface(worldNormal, 1.0f, confidence);
    DeviceMemoryBarrier();
    uint discarded;
    InterlockedExchange(outWorldMetadata[coord], ((FrameIndex & 255u) << 24) | hash, discarded);
}

[numthreads(8, 8, 1)]
void main(const uint2 dispatchThreadID : SV_DispatchThreadID)
{
    uint stride = max(WorldCacheInjectionStride, 1u);
    uint2 jitter = uint2(FrameIndex % stride, (FrameIndex / stride) % stride);
    uint2 pixCoord = dispatchThreadID * stride + jitter;
    if (any(pixCoord >= uint2(OUT_FRAME_DIM)))
        return;

    float viewDepth = READ_DEPTH(srcWorkingDepth, pixCoord);
    if (viewDepth <= FP_Z || viewDepth >= DepthFadeRange.y)
        return;

    float2 screenPos = (pixCoord + 0.5f) * RCP_OUT_FRAME_DIM;
    float3 viewPosition = ScreenToViewPosition(screenPos, viewDepth);
    float3 worldPosition = ViewToWorldPosition(viewPosition, FrameBuffer::CameraViewInverse) + FrameBuffer::CameraPosAdjust.xyz;
    float3 cameraWS = ViewToWorldPosition(0.0f, FrameBuffer::CameraViewInverse) + FrameBuffer::CameraPosAdjust.xyz;

    float3 viewNormal = GBuffer::DecodeNormal(srcNormal.Load(int3(pixCoord, RES_MIP)));
    float3 worldNormal = normalize(ViewToWorldVector(viewNormal, FrameBuffer::CameraViewInverse));
    float3 radiance = max(srcRadiance.Load(int3(pixCoord, 0)), 0.0f);
    float3 albedo = saturate(FULLRES_LOAD(srcAlbedo, pixCoord, screenPos * (FrameDim * RcpTexDim), samplerLinearClamp));

    InjectWorldVoxel(worldPosition, worldNormal, radiance, albedo, 1u);
    if (WorldCacheCascadeBlend(worldPosition, cameraWS) < 1.0f)
        InjectWorldVoxel(worldPosition, worldNormal, radiance, albedo, 0u);
}


