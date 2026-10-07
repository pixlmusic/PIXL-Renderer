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
// CS-only t11; bound/unbound by HybridGI alongside t0..t16. No b1/b5/b6 ABI change.
Texture2D<float> srcReflectionHiZ : register(t11);

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

inline bool ReadReflectionVoxelCascade(float3 queryWS, float3 receiverWS, float3 cameraWS, uint cascade,
    inout float3 radiance, inout float occupancy)
{
    radiance = 0.0f;
    occupancy = 0.0f;
    float cellSize = WorldCacheCellSize(cascade);
    int3 cell = int3(floor(queryWS / cellSize));
    if (!WorldCacheCellInWindow(cell, cameraWS, cascade))
        return false;
    uint2 coord = WorldCacheAtlasCoord(cell, cascade);
    uint meta = srcWorldMetadata.Load(int3(coord, 0));
    bool valid = (meta & 0x00ffffffu) == WorldCacheHash(cell, cascade);
    if (valid) {
		uint age = ((WorldCacheClock & 255u) - (meta >> 24)) & 255u;
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
            valid = occupancy > (1.0f / 255.0f);
            if (valid && leakWeight > 1e-3f) {
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
        nearValid = ReadReflectionVoxelCascade(queryWS, receiverWS, cameraWS, 0u, nearRadiance, nearOccupancy);

    bool valid = false;
    if (nearValid && blend <= 0.001f) {
        radiance = nearRadiance;
        occupancy = nearOccupancy;
        valid = true;
    } else {
        bool farValid = ReadReflectionVoxelCascade(queryWS, receiverWS, cameraWS, 1u, farRadiance, farOccupancy);
        if (nearValid && farValid) {
            radiance = lerp(nearRadiance, farRadiance, blend);
            occupancy = lerp(nearOccupancy, farOccupancy, blend);
            valid = occupancy > (1.0f / 255.0f);
        } else if (nearValid) {
            radiance = nearRadiance;
            occupancy = nearOccupancy * (1.0f - smoothstep(0.35f, 0.999f, blend));
            valid = occupancy > (1.0f / 255.0f);
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
        float baseStep = WorldCacheTraceCellSize(positionWS, cameraWS);
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

// Perspective-correct depth along a projected line. Screen interpolation is
// linear, view-space Z is not: interpolate reciprocal depth, not depth.
float ReflectionRayDepth(float2 inverseDepth, float s)
{
    return rcp(max(lerp(inverseDepth.x, inverseDepth.y, s), 1e-8f));
}

inline bool TraceScreenReflection(float3 originVS, float3 directionVS, float roughness,
    inout float2 hitUV, inout float hitConfidence)
{
    hitUV = 0.0f;
    hitConfidence = 0.0f;
    if (!all(isfinite(originVS)) || !all(isfinite(directionVS)) || originVS.z <= FP_Z)
        return false;

    float maxDistance = max(ReflectionMaxDistance, 32.0f);
    float endT = maxDistance * lerp(0.85f, 1.15f, roughness);
    // Clip before projection; rays toward the camera must never cross Z=0.
    if (directionVS.z < -1e-6f)
        endT = min(endT, (originVS.z - FP_Z - 0.01f) / -directionVS.z);
    float startT = max(ReflectionRayBias, 1.0f);
    if (endT <= startT)
        return false;
    float3 startVS = originVS + directionVS * startT;
    float3 endVS = originVS + directionVS * endT;
    float2 startUV = ViewToScreenPosition(startVS);
    float2 endUV = ViewToScreenPosition(endVS);
    float2 deltaUV = endUV - startUV;
    if (!all(isfinite(startUV)) || !all(isfinite(endUV)))
        return false;

    // Clip the projected segment to the active viewport, NOT backing size.
    float enter = 0.0f, leave = 1.0f;
    [unroll] for (uint axis = 0u; axis < 2u; ++axis) {
        if (abs(deltaUV[axis]) < 1e-8f) {
            if (startUV[axis] < 0.001f || startUV[axis] > 0.999f)
                return false;
        } else {
            float a = (0.001f - startUV[axis]) / deltaUV[axis];
            float b = (0.999f - startUV[axis]) / deltaUV[axis];
            enter = max(enter, min(a, b));
            leave = min(leave, max(a, b));
        }
    }
    if (enter >= leave)
        return false;

    // Integer mip cells cover 2^mip backing pixels (also at odd dimensions).
    // There is no HALF/QUARTER_RES mip bias: every hit descends to full depth.
    float2 pixelStart = startUV * FrameDim;
    float2 pixelDelta = deltaUV * FrameDim;
    float2 inverseDepth = rcp(float2(startVS.z, endVS.z));
    float epsilonS = 1e-4f / max(max(abs(pixelDelta.x), abs(pixelDelta.y)), 1.0f);
    float s = enter;
    uint mip = 4u;
    uint tests = clamp(ReflectionSteps, 8u, 64u);

    [loop] for (uint i = 0u; i < tests && s < leave; ++i) {
        uint width, height, levels;
        srcReflectionHiZ.GetDimensions(mip, width, height, levels);
        float cellSize = (float)(1u << mip);
        // Look infinitesimally into the current interval for negative directions.
        float2 pixel = pixelStart + pixelDelta * min(s + epsilonS, leave);
        int2 cell = int2(floor(pixel / cellSize));
        // Floor-sized NPOT mip chains omit trailing pixels. Descend rather
        // than sampling/clamping a different cell and inventing a depth bound.
        if (any(cell < 0) || any(cell >= int2(width, height))) {
            if (mip == 0u)
                break;
            --mip;
            continue;
        }
        float2 boundary = (float2(cell) + float2(pixelDelta.x >= 0.0f ? 1.0f : 0.0f,
            pixelDelta.y >= 0.0f ? 1.0f : 0.0f)) * cellSize;
        float exitS = leave;
        [unroll] for (uint axis = 0u; axis < 2u; ++axis)
            if (abs(pixelDelta[axis]) > 1e-8f)
                exitS = min(exitS, (boundary[axis] - pixelStart[axis]) / pixelDelta[axis]);
        exitS = max(exitS, s);
        float sceneZ = srcReflectionHiZ.Load(int3(cell, mip));
        float entryZ = ReflectionRayDepth(inverseDepth, s);
        float exitZ = ReflectionRayDepth(inverseDepth, exitS);
        float rayT = lerp(startT * inverseDepth.x, endT * inverseDepth.y, exitS) * exitZ;
        float thickness = max(ReflectionThickness, 1.0f) * (1.0f + roughness * 2.0f +
            (rayT / maxDistance) * 0.5f);

        // A min hierarchy proves empty space only when the entire segment is
        // in front of its nearest surface. Otherwise descend without advancing.
        if (max(entryZ, exitZ) + thickness >= sceneZ && mip > 0u) {
            --mip;
            continue;
        }
        if (mip == 0u && sceneZ > FP_Z &&
            min(entryZ, exitZ) <= sceneZ + thickness &&
            max(entryZ, exitZ) >= sceneZ - thickness) {
            float depthSlope = inverseDepth.y - inverseDepth.x;
            float hitS = abs(depthSlope) > 1e-10f ?
                clamp((rcp(sceneZ) - inverseDepth.x) / depthSlope, s, exitS) : s;
            float2 uv = startUV + deltaUV * hitS;
            float finalDelta = abs(ReflectionRayDepth(inverseDepth, hitS) - sceneZ);
            float3 hitNormal = GBuffer::DecodeNormal(srcNormalRoughness.SampleLevel(
                samplerLinearClamp, FullFrameTextureUV(uv), 0.0f).xy);
            float facing = saturate(dot(hitNormal, -directionVS));
            if (facing > 0.025f && finalDelta <= thickness) {
                float edge = min(min(uv.x, uv.y), min(1.0f - uv.x, 1.0f - uv.y));
                hitUV = uv;
                hitConfidence = saturate(edge * 24.0f) *
                    saturate(1.0f - finalDelta / max(thickness * 1.5f, 1.0f)) *
                    smoothstep(0.025f, 0.20f, facing);
                return hitConfidence > 1e-4f;
            }
        }
        // Skip this cell, then ascend for the next empty-space test. Bias only
        // by a tiny fraction of a pixel to guarantee progress at shared edges.
        s = exitS + epsilonS;
        mip = min(mip + 1u, 4u);
    }
    return false;
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

    float4 nr = FULLRES_LOAD(srcNormalRoughness, dtid, FullFrameTextureUV(uv), samplerLinearClamp);
    float3 N = GBuffer::DecodeNormal(nr.xy);
    float roughness = saturate(1.0f - nr.z);
    float3 materialReflectance = max(FULLRES_LOAD(srcReflectance, dtid, FullFrameTextureUV(uv), samplerLinearClamp), 0.0f);
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
#ifdef TEMPORAL_DENOISER
	float3 screenNeighborhoodMin = 0.0f;
	float3 screenNeighborhoodMax = 65504.0f.xxx;
#endif
	if (screenHit) {
		float lod = saturate(roughness / max(ReflectionMaxRoughness, 1e-3f)) * 4.0f;
		float2 texel = RCP_OUT_FRAME_DIM * (1.0f + roughness * 2.0f);
		float3 hitCenter = srcRadiance.SampleLevel(samplerLinearClamp, InternalFrameTextureUV(hitUV), lod);
		reflectedRadiance = hitCenter;
#ifdef TEMPORAL_DENOISER
		// Radiance is stored in the compact internal footprint. Neighbouring
		// screen-space samples must use the internal scale as well.
		float3 hitX0 = srcRadiance.SampleLevel(samplerLinearClamp, InternalFrameTextureUV(hitUV - float2(texel.x, 0.0f)), lod);
		float3 hitX1 = srcRadiance.SampleLevel(samplerLinearClamp, InternalFrameTextureUV(hitUV + float2(texel.x, 0.0f)), lod);
		float3 hitY0 = srcRadiance.SampleLevel(samplerLinearClamp, InternalFrameTextureUV(hitUV - float2(0.0f, texel.y)), lod);
		float3 hitY1 = srcRadiance.SampleLevel(samplerLinearClamp, InternalFrameTextureUV(hitUV + float2(0.0f, texel.y)), lod);
		screenNeighborhoodMin = min(hitCenter, min(min(hitX0, hitX1), min(hitY0, hitY1)));
		screenNeighborhoodMax = max(hitCenter, max(max(hitX0, hitX1), max(hitY0, hitY1)));
		float3 mean = (hitCenter + hitX0 + hitX1 + hitY0 + hitY1) * 0.2f;
		float3 variance = ((hitCenter - mean) * (hitCenter - mean) +
			(hitX0 - mean) * (hitX0 - mean) + (hitX1 - mean) * (hitX1 - mean) +
			(hitY0 - mean) * (hitY0 - mean) + (hitY1 - mean) * (hitY1 - mean)) * 0.2f;
		float3 sigma = sqrt(max(variance, 1e-6f));
		screenNeighborhoodMin = max(screenNeighborhoodMin, mean - 2.5f * sigma);
		screenNeighborhoodMax = min(screenNeighborhoodMax, mean + 2.5f * sigma);
#endif
	}

    // The voxel cache is intentionally a *rough* reflection fallback. Avoid
    // using its low-frequency SH result as a fake mirror and fade it back out
    // at the user-selected maximum roughness where the cache becomes too broad.
    float cacheCutoff = max(WorldCacheReflectionRoughnessCutoff, 0.20f);
    float roughSupport = smoothstep(0.10f, min(0.35f, cacheCutoff * 0.65f), roughness);
    float roughCutoff = 1.0f - smoothstep(cacheCutoff * 0.90f, cacheCutoff, roughness);
    float fallbackPotential = (1.0f - screenConfidence) * roughSupport * roughCutoff *
        saturate(ReflectionWorldFallbackStrength) * max(WorldCacheReflectionStrength, 0.0f);
	float worldConfidence = 0.0f;
	float3 worldRadiance = 0.0f;
	[branch] if (fallbackPotential > 1e-3f && WorldCacheEnabled != 0u && WorldCacheReflectionEnabled != 0u) {
        float3 positionWS = ViewToWorldPosition(P, FrameBuffer::CameraViewInverse) + FrameBuffer::CameraPosAdjust.xyz;
        float3 cameraWS = ViewToWorldPosition(0.0f, FrameBuffer::CameraViewInverse) + FrameBuffer::CameraPosAdjust.xyz;
        float3 directionWS = normalize(ViewToWorldVector(L, FrameBuffer::CameraViewInverse));
        worldRadiance = TraceWorldFallback(positionWS, directionWS, cameraWS, roughness, worldConfidence);
    }
    float worldBlend = fallbackPotential * worldConfidence;

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
