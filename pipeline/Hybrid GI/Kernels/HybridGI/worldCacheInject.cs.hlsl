#include "Common/Color.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/GBuffer.hlsli"
#include "RadiantGrid/Common.hlsli"
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
StructuredBuffer<Light> srcRadiantLights : register(t9);
StructuredBuffer<uint> srcRadiantLightList : register(t10);
StructuredBuffer<LightGrid> srcRadiantLightGrid : register(t11);

RWTexture2D<uint> outWorldMetadata : register(u0);
RWTexture2D<float4> outWorldSH0 : register(u1);
RWTexture2D<float4> outWorldSH1 : register(u2);
RWTexture2D<float4> outWorldSH2 : register(u3);
RWTexture2D<uint> outWorldNormal : register(u4);
// Cleared to UINT_MAX each frame; select and publish are separate dispatches.
RWTexture2D<uint> outWorldWinners : register(u5);

float3 EstimateEmitterRadiance(float2 screenPos, float viewDepth, float3 renderPosition,
    float3 worldNormal, float3 albedo)
{
    if (WorldCacheEmitterInjectionEnabled == 0u || RadiantParticleLightCount == 0u ||
        WorldCacheEmitterInjectionStrength <= 0.0f)
        return 0.0f;

    const uint3 clusterSize = SharedData::radiantGridSettings.ClusterSize.xyz;
    if (any(clusterSize == 0u))
        return 0.0f;

    float nearPlane = max(SharedData::CameraData.y, 1e-3f);
    float farPlane = max(SharedData::CameraData.x, nearPlane + 1.0f);
    float clusterDepth = log(max(viewDepth, nearPlane) / nearPlane) * clusterSize.z /
        max(log(farPlane / nearPlane), 1e-4f);
    uint3 cluster = uint3(min(uint2(screenPos * clusterSize.xy), clusterSize.xy - 1u),
        min((uint)clusterDepth, clusterSize.z - 1u));
    uint clusterIndex = cluster.x + clusterSize.x * cluster.y + clusterSize.x * clusterSize.y * cluster.z;
    LightGrid grid = srcRadiantLightGrid[clusterIndex];

    float3 linearAlbedo = saturate(Color::IrradianceToLinear(albedo / Color::PBRLightingScale));
    float3 emitterRadiance = 0.0f;
    // Emitter lights are normally sparse. Bound pathological particle scenes so
    // cache injection cannot become more expensive than the primary lighting pass.
    uint lightCount = min(grid.lightCount, 48u);
    [loop] for (uint i = 0u; i < lightCount; ++i) {
        Light light = srcRadiantLights[srcRadiantLightList[grid.offset + i]];
        if ((light.lightFlags & LightFlags::Simple) == 0u)
            continue;

        float3 toLight = light.positionWS.xyz - renderPosition;
        float distanceSq = dot(toLight, toLight);
        float radius = max(light.radius, 1.0f);
        if (distanceSq >= radius * radius)
            continue;

        float distance = sqrt(max(distanceSq, 1e-5f));
        float3 lightDirection = toLight / distance;
        float nDotL = saturate(dot(worldNormal, lightDirection));
        if (nDotL <= 0.0f)
            continue;

        float normalizedDistance = saturate(distance / radius);
        float rangeFalloff = 1.0f - normalizedDistance * normalizedDistance;
        rangeFalloff *= rangeFalloff;
        bool linearLight = (light.lightFlags & LightFlags::Linear) != 0u;
        float3 lightEnergy = Color::PointLight(light.color, linearLight) *
            max(light.fade, 0.0f) * rangeFalloff * nDotL;
        emitterRadiance += lightEnergy * linearAlbedo;
    }

    return max(filterInf(filterNaN(emitterRadiance *
        (GIStrength * WorldCacheEmitterInjectionStrength))), 0.0f);
}

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
    uint surface = srcPrevWorldNormal.Load(int3(coord, 0));
    uint age = ((WorldCacheClock & 255u) - (meta >> 24)) & 255u;
    float confidence = UnpackWorldConfidence(surface) * WorldCacheAgeFade(age, WorldCacheMaxAge);
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

void InjectWorldVoxel(float3 worldPosition, float3 worldNormal, uint cascade, uint2 pixCoord, float viewDepth)
{
    float cellSize = WorldCacheCellSize(cascade);
    int3 cell = int3(floor(worldPosition / cellSize));
    uint2 coord = WorldCacheAtlasCoord(cell, cascade);
    uint hash = WorldCacheHash(cell, cascade);

    uint prevMeta = srcPrevWorldMetadata.Load(int3(coord, 0));
    uint prevSurface = srcPrevWorldNormal.Load(int3(coord, 0));
    // Age in fixed-rate cache ticks rather than rendered frames. This preserves
    // useful off-screen lighting without making cache lifetime depend on FPS,
    // upscaling, frame generation or foreground shader compilation stalls.
    uint prevAge = ((WorldCacheClock & 255u) - (prevMeta >> 24)) & 255u;
    bool historyValid = ((prevMeta & 0x00ffffffu) == hash) && prevAge <= WorldCacheMaxAge;

    // Prefer an observation near the voxel centre. Existing surface orientation
    // wins ties over unrelated walls/floors without blocking refreshed history.
    // The unique pixel index makes election independent of GPU scheduling.
    float3 offset = frac(worldPosition / cellSize) - 0.5f;
    uint priority = (uint)(saturate(dot(offset, offset) / 0.75f) * 30.0f);
    if (historyValid && dot(UnpackWorldNormal(prevSurface), worldNormal) < 0.35f)
        priority += 32u;
    uint pixelIndex = pixCoord.y * (uint)OUT_FRAME_DIM.x + pixCoord.x;
    uint candidate = (priority << 26) | pixelIndex;
#ifdef WORLD_CACHE_SELECT
    InterlockedMin(outWorldWinners[coord], candidate);
    return;
#else
    if (outWorldWinners[coord] != candidate)
        return;
#endif

    // Only the elected representative evaluates light clusters and colour.
    // Selection itself needs depth/normals, not expensive emitter lighting.
    float2 screenPos = (pixCoord + 0.5f) * RCP_OUT_FRAME_DIM;
    float3 radiance = max(srcRadiance.Load(int3(pixCoord, 0)), 0.0f);
    float3 albedo = saturate(FULLRES_LOAD(srcAlbedo, pixCoord, screenPos * (FrameDim * RcpTexDim), samplerLinearClamp));
    // Transparent flames arrive after the opaque input. Seed from their existing
    // clustered proxies using max, never double-add already observed lighting.
    radiance = max(radiance, EstimateEmitterRadiance(screenPos, viewDepth,
        worldPosition - FrameBuffer::CameraPosAdjust.xyz, worldNormal, albedo));

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

        // A new plane replaces rather than averages unrelated geometry. Election
        // already prefers matching history when that surface remains visible.
        if (normalAgreement < 0.35f) {
            historyValid = false;
        }

        if (historyValid && newLum > oldLum) {
            // The prefilter already rejects isolated radiance spikes. Permit a
            // coherent practical light to establish within a handful of frames
            // instead of taking seconds to emerge from a dark cached voxel.
            float accepted = max(oldLum * 2.25f, oldLum + 0.75f);
            if (newLum > accepted && newLum > 1e-5f) {
                float scale = accepted / newLum;
                newSH0 *= scale;
                newSH1 *= scale;
                newSH2.xw *= scale;
                newLum = accepted;
            }
            response = max(response * lerp(1.0f, 0.55f, relativeChange), 0.09f);
        } else if (historyValid) {
            // Many same-plane pixels compete for one coarse cell. Do not let a
            // randomly darker atomic winner bypass the configured smoothing.
            response = min(max(response, 0.06f + relativeChange * 0.04f), 0.10f);
        }

        if (historyValid) {
            // Preserve the response slider's 60 Hz meaning at other frame rates.
            response = 1.0f - pow(max(1.0f - response, 0.0f), WorldCacheDeltaTime * 60.0f);
            newSH0 = lerp(oldSH0, newSH0, response);
            newSH1 = lerp(oldSH1, newSH1, response);
            // Chroma ratios should not be scaled as radiance energy, but are still
            // temporally averaged to prevent hue flicker in coarse cells.
            newSH2 = lerp(oldSH2, newSH2, response);

            worldNormal = normalize(lerp(oldNormal, worldNormal, response));

            float oldConfidence = UnpackWorldConfidence(prevSurface);
            bool firstUpdateThisTick = (prevMeta >> 24) != (WorldCacheClock & 255u);
            confidence = firstUpdateThisTick ? min(oldConfidence + 0.20f, 1.0f) : oldConfidence;
        }
    }

    // Only the elected thread publishes. A timestamp CAS is not a lock: within
    // one clock tick the published metadata equals the old expected value.
    outWorldSH0[coord] = newSH0;
    outWorldSH1[coord] = newSH1;
    outWorldSH2[coord] = newSH2;
    outWorldNormal[coord] = PackWorldSurface(worldNormal, 1.0f, confidence);
    outWorldMetadata[coord] = ((WorldCacheClock & 255u) << 24) | hash;
}

[numthreads(8, 8, 1)]
void main(const uint2 dispatchThreadID : SV_DispatchThreadID)
{
    uint stride = max(WorldCacheInjectionStride, 1u);
    uint2 pixCoord = dispatchThreadID * stride;
    if (any(pixCoord >= uint2(OUT_FRAME_DIM)))
        return;
    uint pixelIndex = pixCoord.y * (uint)OUT_FRAME_DIM.x + pixCoord.x;
    if (pixelIndex >= 0x03ffffffu)
        return;

    float viewDepth = READ_DEPTH(srcWorkingDepth, pixCoord);
    if (viewDepth <= FP_Z || viewDepth >= DepthFadeRange.y)
        return;

    float2 screenPos = (pixCoord + 0.5f) * RCP_OUT_FRAME_DIM;
    float3 viewPosition = ScreenToViewPosition(screenPos, viewDepth);
    float3 renderPosition = ViewToWorldPosition(viewPosition, FrameBuffer::CameraViewInverse);
    float3 worldPosition = renderPosition + FrameBuffer::CameraPosAdjust.xyz;
    float3 cameraWS = ViewToWorldPosition(0.0f, FrameBuffer::CameraViewInverse) + FrameBuffer::CameraPosAdjust.xyz;
    // A toroidal atlas only represents one contiguous 32-cell window. Remote
    // geometry outside it must not overwrite nearer cells sharing the same slot.
    int3 farOffset = int3(floor(worldPosition / WorldCacheCellSizeFar)) -
        int3(floor(cameraWS / WorldCacheCellSizeFar));
    if (any(farOffset < -16) || any(farOffset >= 16))
        return;

    float3 viewNormal = GBuffer::DecodeNormal(srcNormal.Load(int3(pixCoord, RES_MIP)));
    float3 worldNormal = normalize(ViewToWorldVector(viewNormal, FrameBuffer::CameraViewInverse));
    InjectWorldVoxel(worldPosition, worldNormal, 1u, pixCoord, viewDepth);
    if (WorldCacheCascadeBlend(worldPosition, cameraWS) < 1.0f)
        InjectWorldVoxel(worldPosition, worldNormal, 0u, pixCoord, viewDepth);
}
