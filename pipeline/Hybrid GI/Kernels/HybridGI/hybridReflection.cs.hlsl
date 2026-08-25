#include "Common/Color.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/GBuffer.hlsli"
#include "HybridGI/common.hlsli"
#include "HybridGI/worldCache.hlsli"

Texture2D<float> srcDepth : register(t0);
Texture2D<float4> srcNormalRoughness : register(t1);
Texture2D<float3> srcRadiance : register(t2);
Texture2D<unorm float2> srcNoise : register(t3);
Texture2D<float4> srcHistory : register(t4); // geometry-remapped history
Texture2D<float3> srcReflectance : register(t5);
Texture2D<uint> srcWorldMetadata : register(t6);
Texture2D<float4> srcWorldSH0 : register(t7);
Texture2D<float4> srcWorldSH1 : register(t8);
Texture2D<float4> srcWorldSH2 : register(t9);
Texture2D<uint> srcWorldNormal : register(t10);

RWTexture2D<float4> outReflection : register(u0);

static const float TWO_PI = 6.28318530718f;

float3 SafeNormalizeReflection(float3 v, float3 fallback)
{
    float l2 = dot(v, v);
    return l2 > 1e-8f ? v * rsqrt(l2) : fallback;
}

void BuildBasis(float3 n, out float3 t, out float3 b)
{
    float signZ = n.z >= 0.0f ? 1.0f : -1.0f;
    float a = -1.0f / (signZ + n.z);
    float xy = n.x * n.y * a;
    t = normalize(float3(1.0f + signZ * n.x * n.x * a, signZ * xy, -signZ * n.x));
    b = normalize(float3(xy, signZ + n.y * n.y * a, -n.y));
}

float SmithGGXG1(float ndotx, float alpha)
{
    ndotx = saturate(ndotx);
    float a2 = alpha * alpha;
    float denom = ndotx + sqrt(max(a2 + (1.0f - a2) * ndotx * ndotx, 1e-8f));
    return denom > 0.0f ? 2.0f * ndotx / denom : 0.0f;
}

float3 SampleGGXVNDF(float3 normal, float3 viewDirection, float roughness, float2 xi,
    out float3 halfVector, out float geometryWeight)
{
    float3 tangent, bitangent;
    BuildBasis(normal, tangent, bitangent);
    float3 V = float3(dot(viewDirection, tangent), dot(viewDirection, bitangent), dot(viewDirection, normal));
    V.z = max(V.z, 1e-4f);

    float alpha = max(roughness * roughness, 1e-3f);
    float3 Vh = SafeNormalizeReflection(float3(alpha * V.x, alpha * V.y, V.z), float3(0,0,1));
    float lensq = dot(Vh.xy, Vh.xy);
    float3 T1 = lensq > 1e-8f ? float3(-Vh.y, Vh.x, 0) * rsqrt(lensq) : float3(1,0,0);
    float3 T2 = cross(Vh, T1);

    float r = sqrt(saturate(xi.x));
    float phi = TWO_PI * xi.y;
    float sn, cs;
    sincos(phi, sn, cs);
    float t1 = r * cs;
    float t2 = r * sn;
    float s = 0.5f * (1.0f + Vh.z);
    t2 = lerp(sqrt(max(1.0f - t1 * t1, 0.0f)), t2, s);

    float3 Nh = t1 * T1 + t2 * T2 + sqrt(max(1.0f - t1 * t1 - t2 * t2, 0.0f)) * Vh;
    float3 Hlocal = SafeNormalizeReflection(float3(alpha * Nh.x, alpha * Nh.y, max(Nh.z, 0.0f)), float3(0,0,1));
    halfVector = SafeNormalizeReflection(tangent * Hlocal.x + bitangent * Hlocal.y + normal * Hlocal.z, normal);
    if (dot(halfVector, viewDirection) < 0.0f)
        halfVector = -halfVector;

    float3 L = SafeNormalizeReflection(reflect(-viewDirection, halfVector), reflect(-viewDirection, normal));
    float ndotl = saturate(dot(normal, L));
    geometryWeight = SmithGGXG1(ndotl, alpha);
    if (ndotl <= 1e-5f) {
        halfVector = normal;
        L = SafeNormalizeReflection(reflect(-viewDirection, normal), normal);
        geometryWeight = 1.0f;
    }
    return L;
}

float3 FresnelSchlickReflection(float3 f0, float vdoth)
{
    float f = 1.0f - saturate(vdoth);
    float f2 = f * f;
    float f5 = f2 * f2 * f;
    return saturate(f0) + (1.0f - saturate(f0)) * f5;
}

inline bool ReadReflectionVoxelCascade(float3 queryWS, float3 receiverWS, uint cascade,
    inout float3 radiance, inout float occupancy)
{
    radiance = 0.0f;
    occupancy = 0.0f;
    float cellSize = WorldCacheCellSize(cascade);
    int3 cell = int3(floor(queryWS / cellSize));
    uint2 coord = WorldCacheAtlasCoord(cell, cascade);
    uint meta = srcWorldMetadata.Load(int3(coord, 0));
    bool valid = (meta & 0x00ffffffu) == WorldCacheHash(cell, cascade);
    if (valid) {
        uint age = ((FrameIndex & 255u) - (meta >> 24)) & 255u;
        float ageFade = WorldCacheAgeFade(age, WorldCacheMaxAge);
        valid = ageFade > 0.0f;
        if (valid) {
            uint surface = srcWorldNormal.Load(int3(coord, 0));
            float confidence = UnpackWorldConfidence(surface) * ageFade;
            occupancy = UnpackWorldOccupancy(surface) * confidence;

            float3 sourceToReceiver = normalize(receiverWS - queryWS);
            float3 sourceNormal = UnpackWorldNormal(surface);
            float sourceFacingSigned = dot(sourceNormal, sourceToReceiver);
            float sourceGate = smoothstep(-0.05f, 0.15f, sourceFacingSigned);
            float leakWeight = lerp(1.0f, sourceGate, saturate(WorldCacheLeakReduction));
            valid = occupancy > (1.0f / 255.0f) && leakWeight > 1e-3f;
            if (valid) {
                radiance = WorldCacheEvaluateRadiance(
                    srcWorldSH0.Load(int3(coord, 0)), srcWorldSH1.Load(int3(coord, 0)), srcWorldSH2.Load(int3(coord, 0)),
                    sourceToReceiver) * leakWeight;
            }
        }
    }
    return valid;
}

inline bool ReadReflectionVoxel(float3 queryWS, float3 receiverWS, float3 cameraWS,
    inout float3 radiance, inout float occupancy)
{
    float3 nearRadiance = 0.0f;
    float3 farRadiance = 0.0f;
    float nearOccupancy = 0.0f;
    float farOccupancy = 0.0f;
    float blend = WorldCacheCascadeBlend(queryWS, cameraWS);
    bool nearValid = false;
    if (blend < 0.999f)
        nearValid = ReadReflectionVoxelCascade(queryWS, receiverWS, 0u, nearRadiance, nearOccupancy);

    bool valid = false;
    if (nearValid && blend <= 0.001f) {
        radiance = nearRadiance;
        occupancy = nearOccupancy;
        valid = true;
    } else {
        bool farValid = ReadReflectionVoxelCascade(queryWS, receiverWS, 1u, farRadiance, farOccupancy);
        if (nearValid && farValid) {
            radiance = lerp(nearRadiance, farRadiance, blend);
            occupancy = lerp(nearOccupancy, farOccupancy, blend);
            valid = occupancy > (1.0f / 255.0f);
        } else if (nearValid) {
            radiance = nearRadiance;
            occupancy = nearOccupancy;
            valid = true;
        } else if (farValid) {
            radiance = farRadiance;
            occupancy = farOccupancy;
            valid = true;
        }
    }
    return valid;
}

inline float3 TraceWorldFallback(float3 positionWS, float3 directionWS, float3 cameraWS, float roughness, inout float confidence)
{
    confidence = 0.0f;
    float3 result = 0.0f;
    if (WorldCacheEnabled != 0u && WorldCacheReflectionEnabled != 0u) {
        uint receiverCascade = WorldCacheCascade(positionWS, cameraWS);
        float baseStep = WorldCacheCellSize(receiverCascade);
        float weightSum = 0.0f;
        float transmittance = 1.0f;
        uint steps = clamp(WorldCacheTraceSteps, 2u, 6u);
        [loop] for (uint i = 0u; i < steps; ++i) {
            float distance = min(WorldCacheRadius, baseStep * (exp2((float)i) + 1.0f) * lerp(1.0f, 1.8f, roughness));
            float3 sampleRadiance = 0.0f;
            float occupancy = 0.0f;
            float3 query = positionWS + directionWS * distance;
            if (ReadReflectionVoxel(query, positionWS, cameraWS, sampleRadiance, occupancy)) {
                float attenuation = rcp(1.0f + distance / max(WorldCacheRadius, 1.0f));
                float w = occupancy * transmittance * attenuation;
                result += sampleRadiance * w;
                weightSum += w;
                transmittance *= 1.0f - occupancy;
                if (transmittance < 0.05f)
                    break;
            }
        }
        confidence = 1.0f - exp2(-weightSum * 1.5f);
        result = weightSum > 1e-4f ? result / weightSum : 0.0f;
    }
    return result;
}

inline bool TraceScreenReflection(float3 originVS, float3 directionVS, float roughness,
    inout float2 hitUV, inout float hitConfidence)
{
    hitUV = 0.0f;
    hitConfidence = 0.0f;
    uint steps = clamp(ReflectionSteps, 8u, 64u);
    float maxDistance = max(ReflectionMaxDistance, 32.0f);
    float thicknessBase = max(ReflectionThickness, 1.0f);
    // Establish a real starting depth relation instead of fabricating a
    // negative previousDelta. The old sentinel let the first positive sample
    // masquerade as a depth crossing and produced false, low-confidence hits.
    float previousT = 0.0f;
    float2 previousUV = ViewToScreenPosition(originVS);
    bool traceActive = !any(previousUV <= 0.001f) && !any(previousUV >= 0.999f);
    bool foundHit = false;
    float previousSceneZ = traceActive ?
        srcDepth.SampleLevel(samplerPointClamp, previousUV * (FrameDim * RcpTexDim), 0.0f) : originVS.z;
    float previousDelta = originVS.z - previousSceneZ;

    [loop] for (uint i = 0u; i < steps && traceActive && !foundHit; ++i) {
        float p = ((float)i + 1.0f) / (float)steps;
        // Quadratic spacing concentrates work around the receiver while still
        // allowing long rays. Rough rays expand slightly faster.
        float t = max(ReflectionRayBias, 1.0f) + p * p * maxDistance * lerp(0.85f, 1.15f, roughness);
        float3 sampleVS = originVS + directionVS * t;
        if (sampleVS.z <= FP_Z)
            break;
        float2 uv = ViewToScreenPosition(sampleVS);
        if (any(uv <= 0.001f) || any(uv >= 0.999f))
            break;

        float2 px = abs((uv - previousUV) * OUT_FRAME_DIM);
        float footprint = max(px.x, px.y);
        float mip = clamp(log2(max(footprint, 1.0f)) - 0.5f + roughness * 1.5f, 0.0f, 4.0f);
        float sceneZ = srcDepth.SampleLevel(samplerPointClamp, uv * (FrameDim * RcpTexDim), mip);
        float delta = sampleVS.z - sceneZ;
        float thickness = thicknessBase * (1.0f + roughness * 2.0f + t / maxDistance * 0.5f);

        bool crossed = (delta >= 0.0f && previousDelta < 0.0f) || abs(delta) <= thickness;
        if (crossed) {
            // Four-step binary refinement against mip 0 prevents coarse mip
            // traversal from producing a detached reflection at depth edges.
            float lo = previousT;
            float hi = t;
            float2 refinedUV = uv;
            [unroll] for (uint r = 0u; r < 4u; ++r) {
                float mid = 0.5f * (lo + hi);
                float3 q = originVS + directionVS * mid;
                refinedUV = ViewToScreenPosition(q);
                float z = srcDepth.SampleLevel(samplerPointClamp, refinedUV * (FrameDim * RcpTexDim), 0.0f);
                if (q.z - z >= 0.0f)
                    hi = mid;
                else
                    lo = mid;
            }
            float3 q = originVS + directionVS * hi;
            float z = srcDepth.SampleLevel(samplerPointClamp, refinedUV * (FrameDim * RcpTexDim), 0.0f);
            float finalDelta = abs(q.z - z);
            if (finalDelta <= thickness * 1.5f) {
                // A depth crossing alone can intersect the back side of a thin
                // screen-space silhouette. Reject strongly back-facing hits so
                // bright radiance behind a wall is not pulled through it as a
                // reflection. The hit normal is already available in the G-buffer.
                float2 hitTexCoord = refinedUV * (FrameDim * RcpTexDim);
                float3 hitNormal = GBuffer::DecodeNormal(srcNormalRoughness.SampleLevel(samplerLinearClamp, hitTexCoord, 0.0f).xy);
                float hitFacing = saturate(dot(hitNormal, -directionVS));
                if (hitFacing <= 0.025f) {
                    traceActive = false;
                } else {
                    float edge = min(min(refinedUV.x, refinedUV.y), min(1.0f - refinedUV.x, 1.0f - refinedUV.y));
                    hitUV = refinedUV;
                    hitConfidence = saturate(edge * 24.0f) *
                        saturate(1.0f - finalDelta / max(thickness * 1.5f, 1.0f)) *
                        smoothstep(0.025f, 0.20f, hitFacing);
                    foundHit = hitConfidence > 1e-4f;
                }
            }
        }
        previousDelta = delta;
        previousT = t;
        previousUV = uv;
    }
    return foundHit;
}

float3 SoftFireflyClamp(float3 value, float threshold)
{
	float3 result = max(value, 0.0f);
	float lum = max(Luminance(result), 0.0f);
	if (threshold > 0.0f && lum > threshold) {
		float excess = lum - threshold;
		float shoulder = max(threshold * 0.5f, 0.25f);
		float compressed = threshold + excess / (1.0f + excess / shoulder);
		result *= compressed / max(lum, 1e-5f);
	}
	return result;
}

[numthreads(8, 8, 1)]
void main(uint2 dtid : SV_DispatchThreadID)
{
    if (any(dtid >= uint2(OUT_FRAME_DIM)))
        return;

    float2 uv = (dtid + 0.5f) * RCP_OUT_FRAME_DIM;
    float viewZ = READ_DEPTH(srcDepth, dtid);
    if (viewZ <= FP_Z || viewZ >= DepthFadeRange.y) {
        outReflection[dtid] = 0.0f;
        return;
    }

    float2 frameScale = FrameDim * RcpTexDim;
    float4 nr = FULLRES_LOAD(srcNormalRoughness, dtid, uv * frameScale, samplerLinearClamp);
    float3 N = GBuffer::DecodeNormal(nr.xy);
    float roughness = saturate(1.0f - nr.z);
    float3 materialReflectance = max(FULLRES_LOAD(srcReflectance, dtid, uv * frameScale, samplerLinearClamp), 0.0f);
    float reflectanceEnergy = max(materialReflectance.x, max(materialReflectance.y, materialReflectance.z));
    float roughFadeWidth = min(0.10f, max(ReflectionMaxRoughness * 0.25f, 0.025f));
    float roughnessSupport = 1.0f - smoothstep(
        max(ReflectionMaxRoughness - roughFadeWidth, 0.0f), ReflectionMaxRoughness, roughness);
    if (reflectanceEnergy <= (1.0f / 4096.0f) || roughnessSupport <= 1e-4f) {
        outReflection[dtid] = 0.0f;
        return;
    }

    float3 P = ScreenToViewPosition(uv, viewZ);
    float3 V = SafeNormalizeReflection(-P, float3(0,0,-1));
    if (dot(N, V) < 0.0f)
        N = -N;

    float2 noise = srcNoise.Load(int3((dtid & 127u) + uint2(0u, (FrameIndex & 63u) * 128u), 0));
    // Reduce stochastic spread on nearly mirror-like surfaces; full VNDF on rough surfaces.
    float stochasticRoughness = roughness * saturate(ReflectionRoughnessJitter);
    float3 H;
    float geometryWeight;
    float3 L = SampleGGXVNDF(N, V, max(stochasticRoughness, 0.015f), noise, H, geometryWeight);
    L = SafeNormalizeReflection(lerp(reflect(-V, N), L, saturate(ReflectionRoughnessJitter)), reflect(-V, N));

	float2 hitUV = 0.0f;
	float screenConfidence = 0.0f;
    bool screenHit = TraceScreenReflection(P + N * max(ReflectionRayBias, 1.0f), L, roughness, hitUV, screenConfidence);
    // Smith visibility is confidence metadata here, not radiance energy. It
    // de-prioritizes grazing stochastic directions without applying the BRDF
    // twice when the deferred composite later multiplies material reflectance.
    screenConfidence *= lerp(0.35f, 1.0f, saturate(geometryWeight));

	float3 reflectedRadiance = 0.0f;
	float3 screenNeighborhoodMin = 0.0f;
	float3 screenNeighborhoodMax = 65504.0f.xxx;
	if (screenHit) {
		float lod = saturate(roughness / max(ReflectionMaxRoughness, 1e-3f)) * 4.0f;
		float2 texel = RCP_OUT_FRAME_DIM * (1.0f + roughness * 2.0f);
		float3 hitCenter = srcRadiance.SampleLevel(samplerLinearClamp, hitUV * frameScale, lod);
		float3 hitX0 = srcRadiance.SampleLevel(samplerLinearClamp, (hitUV - float2(texel.x, 0.0f)) * frameScale, lod);
		float3 hitX1 = srcRadiance.SampleLevel(samplerLinearClamp, (hitUV + float2(texel.x, 0.0f)) * frameScale, lod);
		float3 hitY0 = srcRadiance.SampleLevel(samplerLinearClamp, (hitUV - float2(0.0f, texel.y)) * frameScale, lod);
		float3 hitY1 = srcRadiance.SampleLevel(samplerLinearClamp, (hitUV + float2(0.0f, texel.y)) * frameScale, lod);
		reflectedRadiance = hitCenter;
		screenNeighborhoodMin = min(hitCenter, min(min(hitX0, hitX1), min(hitY0, hitY1)));
		screenNeighborhoodMax = max(hitCenter, max(max(hitX0, hitX1), max(hitY0, hitY1)));
		float3 mean = (hitCenter + hitX0 + hitX1 + hitY0 + hitY1) * 0.2f;
		float3 variance = ((hitCenter - mean) * (hitCenter - mean) +
			(hitX0 - mean) * (hitX0 - mean) + (hitX1 - mean) * (hitX1 - mean) +
			(hitY0 - mean) * (hitY0 - mean) + (hitY1 - mean) * (hitY1 - mean)) * 0.2f;
		float3 sigma = sqrt(max(variance, 1e-6f));
		screenNeighborhoodMin = max(screenNeighborhoodMin, mean - 2.5f * sigma);
		screenNeighborhoodMax = min(screenNeighborhoodMax, mean + 2.5f * sigma);
	}

    float3 positionWS = ViewToWorldPosition(P, FrameBuffer::CameraViewInverse) + FrameBuffer::CameraPosAdjust.xyz;
    float3 cameraWS = ViewToWorldPosition(0.0f, FrameBuffer::CameraViewInverse) + FrameBuffer::CameraPosAdjust.xyz;
    float3 directionWS = normalize(ViewToWorldVector(L, FrameBuffer::CameraViewInverse));
	float worldConfidence = 0.0f;
    float3 worldRadiance = TraceWorldFallback(positionWS, directionWS, cameraWS, roughness, worldConfidence);

    // The voxel cache is intentionally a *rough* reflection fallback. Avoid
    // using its low-frequency SH result as a fake mirror and fade it back out
    // at the user-selected maximum roughness where the cache becomes too broad.
    float cacheCutoff = max(WorldCacheReflectionRoughnessCutoff, 0.20f);
    float roughSupport = smoothstep(0.10f, min(0.35f, cacheCutoff * 0.65f), roughness);
    float roughCutoff = 1.0f - smoothstep(cacheCutoff * 0.90f, cacheCutoff, roughness);
    float worldBlend = (1.0f - screenConfidence) * worldConfidence * roughSupport * roughCutoff;
    worldBlend *= saturate(ReflectionWorldFallbackStrength) * max(WorldCacheReflectionStrength, 0.0f);

    // Confidence is metadata for temporal/spatial reconstruction, not brightness.
    // Blend observations by reliability and normalize the radiance so a valid
    // 30% confidence hit is not rendered at 30% of its physical energy.
    float screenWeight = screenHit ? screenConfidence : 0.0f;
    float observationWeight = screenWeight + worldBlend;
    if (observationWeight > 1e-4f)
        reflectedRadiance = (reflectedRadiance * screenWeight + worldRadiance * worldBlend) / observationWeight;
    else
        reflectedRadiance = 0.0f;
    float confidence = saturate(observationWeight) * roughnessSupport;

    // The Deferred reflectance buffer already stores the integrated material
    // specular lobe. This pass therefore outputs *incident reflected radiance*;
    // applying Fresnel/Smith here and Reflectance again later double-weights the
    // BRDF and was the main cause of the nearly-black Hybrid Reflections dump.
    float3 current = reflectedRadiance * max(ReflectionIntensity, 0.0f);
    current = SoftFireflyClamp(filterInf(filterNaN(current)), ReflectionFireflyClamp);

#ifdef TEMPORAL_DENOISER
    float4 history = srcHistory[dtid];
    if (history.a > 1e-3f) {
        float response = saturate(ReflectionTemporalResponse);
        // Rough reflections need more temporal integration; glossy hits stay
        // more responsive to camera/surface motion.
        response = saturate(lerp(max(response, 0.18f), max(response, 0.07f), roughness));
        float currLum = Luminance(max(current, 0.0f));
        float histLum = Luminance(max(history.rgb, 0.0f));
        float referenceLum = confidence > 1e-3f ? currLum : histLum;
		float limit = max(ReflectionFireflyClamp, max(referenceLum * 4.0f + 0.05f, 1.0f));
		float3 historySafe = SoftFireflyClamp(history.rgb, limit);
		// The remap pass already rejects depth/normal disocclusions. A current
		// hit-space variance box additionally prevents valid-but-stale bright
		// history from trailing across changing reflection content.
		if (screenHit)
			historySafe = clamp(historySafe, screenNeighborhoodMin, screenNeighborhoodMax);

        if (confidence > 1e-3f) {
            // Low-confidence hits are useful observations but should not replace
            // a validated history sample as aggressively as a solid screen hit.
            float observationResponse = lerp(response * 0.30f, response, confidence);
            current = lerp(historySafe, current, observationResponse);
            confidence = max(confidence, history.a * (1.0f - observationResponse));
        } else {
            // The history texture was already geometry/depth/normal validated by
            // radianceDisocc. Keep a short decaying tail through transient
            // screen-space misses instead of popping a reflection to black.
            float retain = lerp(0.82f, 0.94f, roughness);
            current = historySafe * retain;
            confidence = history.a * retain;
        }
    }
#endif

    outReflection[dtid] = float4(max(current, 0.0f), confidence);
}
