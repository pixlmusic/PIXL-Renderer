#include "Common/Color.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/GBuffer.hlsli"
#include "Common/MotionBlur.hlsli"
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
Texture2D<uint> srcWorldIntelligence : register(t13);
// CS-only t11; bound/unbound by HybridGI alongside t0..t16. No b1/b5/b6 ABI change.
Texture2D<float2> srcReflectionHiZ : register(t11);
// Geometry-validated GI history length, reduced locally when direct lighting
// changes. Reflection history follows the same responsiveness at those pixels.
Texture2D<unorm float> srcGIHistoryLength : register(t12);
Texture3D<uint> srcSceneVoxelOccupancy : register(t14);

cbuffer SceneVoxelCB : register(b3)
{
	float3 SceneVoxelWorldMin;
	float SceneVoxelSize;
	uint SceneVoxelDimension;
	uint SceneVoxelValid;
	float2 SceneVoxelPadding;
};

RWTexture2D<float4> outReflection : register(u0);

static const float TWO_PI = 6.28318530718f;

float3 SafeNormalizeReflection(float3 v, float3 fallback)
{
    float l2 = dot(v, v);
    bool finiteVector = all((asuint(v) & 0x7f800000u) != 0x7f800000u);
    bool finiteLength = (asuint(l2) & 0x7f800000u) != 0x7f800000u;
    return finiteVector && finiteLength && l2 > 1e-8f && l2 < 1e30f
        ? v * rsqrt(l2)
        : fallback;
}

bool SceneVoxelOccupied(float3 worldPosition)
{
	if (SceneVoxelValid == 0u || SceneVoxelDimension == 0u || !(SceneVoxelSize > 0.0f))
		return false;
	int3 cell = int3(floor((worldPosition - SceneVoxelWorldMin) / SceneVoxelSize));
	if (any(cell < 0) || any(cell >= int(SceneVoxelDimension)))
		return false;
	return srcSceneVoxelOccupancy.Load(int4(cell, 0)) != 0u;
}

bool SceneVoxelSegmentOccluded(float3 startWS, float3 endWS)
{
	if (SceneVoxelValid == 0u || SceneVoxelDimension == 0u || !(SceneVoxelSize > 0.0f))
		return false;
	float3 startGrid = (startWS - SceneVoxelWorldMin) / SceneVoxelSize;
	float3 endGrid = (endWS - SceneVoxelWorldMin) / SceneVoxelSize;
	if (!all(isfinite(startGrid)) || !all(isfinite(endGrid)))
		return false;
	float3 delta = endGrid - startGrid;
	int3 cell = int3(floor(startGrid));
	int3 endCell = int3(floor(endGrid));
	int3 stepDirection = int3(delta.x >= 0.0f ? 1 : -1, delta.y >= 0.0f ? 1 : -1, delta.z >= 0.0f ? 1 : -1);
	float3 tDelta = float3(abs(delta.x) > 1.0e-6f ? abs(rcp(delta.x)) : 1.0e20f,
		abs(delta.y) > 1.0e-6f ? abs(rcp(delta.y)) : 1.0e20f,
		abs(delta.z) > 1.0e-6f ? abs(rcp(delta.z)) : 1.0e20f);
	float3 nextBoundary = float3(cell) + float3(stepDirection.x > 0 ? 1.0f : 0.0f,
		stepDirection.y > 0 ? 1.0f : 0.0f, stepDirection.z > 0 ? 1.0f : 0.0f);
	float3 tMax = float3(abs(delta.x) > 1.0e-6f ? (nextBoundary.x - startGrid.x) / delta.x : 1.0e20f,
		abs(delta.y) > 1.0e-6f ? (nextBoundary.y - startGrid.y) / delta.y : 1.0e20f,
		abs(delta.z) > 1.0e-6f ? (nextBoundary.z - startGrid.z) / delta.z : 1.0e20f);
	[loop] for (uint step = 0u; step < 64u; ++step) {
		if (any(cell < 0) || any(cell >= int(SceneVoxelDimension)))
			return false;
		if (srcSceneVoxelOccupancy.Load(int4(cell, 0)) != 0u)
			return true;
		if (all(cell == endCell))
			return false;
		if (tMax.x <= tMax.y && tMax.x <= tMax.z) { cell.x += stepDirection.x; tMax.x += tDelta.x; }
		else if (tMax.y <= tMax.z) { cell.y += stepDirection.y; tMax.y += tDelta.y; }
		else { cell.z += stepDirection.z; tMax.z += tDelta.z; }
	}
	return false;
}

void BuildBasis(float3 n, out float3 t, out float3 b)
{
    n = SafeNormalizeReflection(n, float3(0, 0, 1));
    float signZ = n.z >= 0.0f ? 1.0f : -1.0f;
    float a = -1.0f / (signZ + n.z);
    float xy = n.x * n.y * a;
    t = SafeNormalizeReflection(float3(1.0f + signZ * n.x * n.x * a, signZ * xy, -signZ * n.x), float3(1, 0, 0));
    b = SafeNormalizeReflection(float3(xy, signZ + n.y * n.y * a, -n.y), float3(0, 1, 0));
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
			float confidence = UnpackWorldConfidence(surface) * ageFade * WorldIntelligenceReliability(srcWorldIntelligence.Load(int3(coord, 0)));
			occupancy = saturate(UnpackWorldOccupancy(surface) * confidence);

            float3 sourceNormal = UnpackWorldNormal(surface);
    occupancy *= WorldReceiverPlaneConfidence(receiverWS, cell, sourceNormal, cellSize, confidence);
            float3 sourceToReceiver = SafeNormalizeReflection(receiverWS - queryWS, sourceNormal);
            float sourceFacingSigned = dot(sourceNormal, sourceToReceiver);
            float sourceGate = smoothstep(-0.05f, 0.15f, sourceFacingSigned);
            float leakWeight = lerp(1.0f, sourceGate, saturate(WorldCacheLeakReduction));
            valid = occupancy > (1.0f / 255.0f);
            if (valid && leakWeight > 1e-3f) {
                radiance = WorldCacheEvaluateRadiance(
                    srcWorldSH0.Load(int3(coord, 0)), srcWorldSH1.Load(int3(coord, 0)), srcWorldSH2.Load(int3(coord, 0)),
                    sourceToReceiver) * leakWeight;

				// Smooth only the closest voxel face, and only when the adjacent
				// cache sample represents the same oriented surface. This bounds the
				// extra lookup cost while removing low-frequency SH steps from rough
				// reflections without blending across corners or separate room walls.
				float3 fraction = frac(queryWS / cellSize);
				float3 edge = min(fraction, 1.0f - fraction);
				float edgeDistance = min(edge.x, min(edge.y, edge.z));
				if (edgeDistance < 0.125f) {
					int3 side = edge.x <= edge.y && edge.x <= edge.z ?
						int3(fraction.x < 0.5f ? -1 : 1, 0, 0) :
						edge.y <= edge.z ? int3(0, fraction.y < 0.5f ? -1 : 1, 0) :
						int3(0, 0, fraction.z < 0.5f ? -1 : 1);
					int3 neighborCell = cell + side;
					if (WorldCacheCellInWindow(neighborCell, cameraWS, cascade)) {
						uint2 neighborCoord = WorldCacheAtlasCoord(neighborCell, cascade);
						uint neighborMeta = srcWorldMetadata.Load(int3(neighborCoord, 0));
						uint neighborAge = ((WorldCacheClock & 255u) - (neighborMeta >> 24)) & 255u;
						if ((neighborMeta & 0x00ffffffu) == WorldCacheHash(neighborCell, cascade) &&
							WorldCacheAgeFade(neighborAge, WorldCacheMaxAge) > 0.0f) {
							uint neighborSurface = srcWorldNormal.Load(int3(neighborCoord, 0));
							float3 neighborNormal = UnpackWorldNormal(neighborSurface);
							float neighborOccupancy = saturate(UnpackWorldOccupancy(neighborSurface) *
								UnpackWorldConfidence(neighborSurface) * WorldCacheAgeFade(neighborAge, WorldCacheMaxAge) * WorldIntelligenceReliability(srcWorldIntelligence.Load(int3(neighborCoord, 0))));
							neighborOccupancy *= WorldReceiverPlaneConfidence(receiverWS, neighborCell, neighborNormal, cellSize, UnpackWorldConfidence(neighborSurface));
                            if (neighborOccupancy > (1.0f / 255.0f) && dot(sourceNormal, neighborNormal) > 0.85f) {
								float3 neighborToReceiver = SafeNormalizeReflection(receiverWS - queryWS, neighborNormal);
								float neighborGate = lerp(1.0f,
									smoothstep(-0.05f, 0.15f, dot(neighborNormal, neighborToReceiver)),
									saturate(WorldCacheLeakReduction));
								float3 neighborRadiance = WorldCacheEvaluateRadiance(
									srcWorldSH0.Load(int3(neighborCoord, 0)),
									srcWorldSH1.Load(int3(neighborCoord, 0)),
									srcWorldSH2.Load(int3(neighborCoord, 0)), neighborToReceiver) * neighborGate;
								float neighborWeight = 0.5f * (1.0f - smoothstep(0.0f, 0.125f, edgeDistance));
								radiance = lerp(radiance, neighborRadiance, neighborWeight);
								occupancy = lerp(occupancy, neighborOccupancy, neighborWeight);
							}
						}
					}
				}
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
            radiance = lerp(nearRadiance, farRadiance,
                WorldCascadeRadianceBlend(blend, nearOccupancy, farOccupancy));
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
    roughness = saturate(roughness);
    directionWS = SafeNormalizeReflection(directionWS, float3(0, 0, 1));
    if (WorldCacheEnabled != 0u && WorldCacheReflectionEnabled != 0u) {
        uint receiverCascade = WorldCacheCascade(positionWS, cameraWS);
        float baseStep = WorldCacheCellSize(receiverCascade);
        float weightSum = 0.0f;
        float transmittance = 1.0f;
		float previousDistance = SceneVoxelSize * 1.25f;
        uint steps = clamp(WorldCacheTraceSteps, 2u, 6u);
        [loop] for (uint i = 0u; i < steps; ++i) {
            float distance = min(WorldCacheRadius, baseStep * (exp2((float)i) + 1.0f) * lerp(1.0f, 1.8f, roughness));
			if (SceneVoxelValid != 0u && distance > previousDistance) {
				if (SceneVoxelSegmentOccluded(positionWS + directionWS * previousDistance,
					positionWS + directionWS * distance))
					break; // captured mesh geometry blocks off-screen cache radiance
			}
			previousDistance = distance;
            float3 sampleRadiance = 0.0f;
            float occupancy = 0.0f;
            float3 query = positionWS + directionWS * distance;
            if (ReadReflectionVoxel(query, positionWS, cameraWS, sampleRadiance, occupancy)) {
                float attenuation = rcp(1.0f + distance / max(WorldCacheRadius, 1.0f));
                float w = occupancy * transmittance * attenuation;
                sampleRadiance = all(isfinite(sampleRadiance)) ? max(sampleRadiance, 0.0f) : 0.0f;
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
    uint configuredTests = clamp(ReflectionSteps, 8u, 64u);
    // Rough reflections already sample a wider radiance footprint and blend
    // toward the world cache when screen confidence is weak. Spend the full
    // traversal budget on glossy detail, then reduce the hard iteration cap
    // smoothly for broad lobes where an exact pixel hit contributes less.
    float roughTraceScale = lerp(1.0f, 0.70f,
        smoothstep(0.30f, max(ReflectionMaxRoughness, 0.31f), roughness));
    uint tests = max(8u, (uint)ceil((float)configuredTests * roughTraceScale));

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
		float2 sceneDepthRange = srcReflectionHiZ.Load(int3(cell, mip));
        float entryZ = ReflectionRayDepth(inverseDepth, s);
        float exitZ = ReflectionRayDepth(inverseDepth, exitS);
		float rayNearZ = min(entryZ, exitZ);
		float rayFarZ = max(entryZ, exitZ);
        float rayT = lerp(startT * inverseDepth.x, endT * inverseDepth.y, exitS) * exitZ;
        float thickness = max(ReflectionThickness, 1.0f) * (1.0f + roughness * 2.0f +
            (rayT / maxDistance) * 0.5f);

		// The interval pyramid proves empty space on either side of all geometry.
		// Tiles containing sky carry a far=max sentinel, which conservatively
		// disables only the behind-surface rejection for that mixed tile.
		const bool whollyBefore = rayFarZ + thickness < sceneDepthRange.x;
		const bool whollyBehind = rayNearZ - thickness > sceneDepthRange.y;
		if (!whollyBefore && !whollyBehind && mip > 0u) {
            --mip;
            continue;
        }
		float sceneZ = sceneDepthRange.x;
		if (mip == 0u && isfinite(sceneZ) && sceneZ > FP_Z &&
			rayNearZ <= sceneZ + thickness && rayFarZ >= sceneZ - thickness) {
            float depthSlope = inverseDepth.y - inverseDepth.x;
            float hitS = abs(depthSlope) > 1e-10f ?
                clamp((rcp(sceneZ) - inverseDepth.x) / depthSlope, s, exitS) : s;
            float2 uv = startUV + deltaUV * hitS;
            float finalDelta = abs(ReflectionRayDepth(inverseDepth, hitS) - sceneZ);
			// Point sampling keeps silhouettes from interpolating foreground and
			// background normals into a false grazing hit.
			float3 hitNormal = ToTraceView(GBuffer::DecodeNormal(srcNormalRoughness.SampleLevel(
				samplerPointClamp, FullFrameTextureUV(uv), 0.0f).xy));
            float facing = saturate(dot(hitNormal, -directionVS));
            // Grazing rays run almost parallel to the hit surface, where a
            // broad depth tolerance can accept a distant terrain layer as a
            // valid reflection and form a camera-following band. Keep the
            // coarse HiZ test permissive, but tighten the final full-res hit
            // tolerance until the ray meets the surface at a stable angle.
            float incidence = smoothstep(0.05f, 0.30f, facing);
            float hitTolerance = thickness * lerp(0.20f, 1.0f, incidence);
            if (facing > 0.025f && finalDelta <= hitTolerance) {
                float edge = min(min(uv.x, uv.y), min(1.0f - uv.x, 1.0f - uv.y));
                hitUV = uv;
                hitConfidence = saturate(edge * 24.0f) *
                    saturate(1.0f - finalDelta / max(hitTolerance * 1.5f, 1.0f)) *
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

// The source atlas contains a 128x128 spatiotemporal blue-noise tile. Reusing
// the same tile at every screen-space block makes stochastic reflection hits
// correlate into a visible grid, especially on broad low-roughness terrain.
// A stable per-block dihedral transform and toroidal offset preserve the atlas
// distribution while decorrelating repeated screen tiles. The temporal slice
// remains identical for every pixel in a frame.
uint2 ReflectionNoiseCoordinate(uint2 pixel, uint frameIndex)
{
	uint2 tile = pixel >> 7u;
	uint hash = tile.x * 0x9e3779b9u ^ tile.y * 0x85ebca6bu;
	hash ^= hash >> 16u;
	hash *= 0x7feb352du;
	hash ^= hash >> 15u;
	hash *= 0x846ca68bu;
	hash ^= hash >> 16u;

	uint2 local = pixel & 127u;
	switch (hash & 3u) {
	case 1u: local = uint2(127u - local.y, local.x); break;
	case 2u: local = uint2(127u - local.x, 127u - local.y); break;
	case 3u: local = uint2(local.y, 127u - local.x); break;
	}
	if ((hash & 4u) != 0u)
		local.x = 127u - local.x;
	uint2 offset = uint2(hash & 127u, (hash >> 8u) & 127u);
	local = (local + offset) & 127u;
	local.y += (frameIndex & 63u) * 128u;
	return local;
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
    float3 N = ToTraceView(GBuffer::DecodeNormal(nr.xy));
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

    float2 noise = srcNoise.Load(int3(ReflectionNoiseCoordinate(dtid, FrameIndex), 0));
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
        float3 positionWS = TraceViewToWorldPosition(P, FrameBuffer::CameraViewInverse) + FrameBuffer::CameraPosAdjust.xyz;
        float3 cameraWS = TraceViewToWorldPosition(0.0f, FrameBuffer::CameraViewInverse) + FrameBuffer::CameraPosAdjust.xyz;
        float3 directionWS = SafeNormalizeReflection(TraceViewToWorldVector(L, FrameBuffer::CameraViewInverse), float3(0, 0, 1));
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
    // The confidence factors are already bounded; saturate also maps infinities
    // to a finite endpoint. Preserve a NaN-only guard without triggering FXC's
    // redundant isfinite warning for this bounded expression.
    confidence = confidence == confidence ? saturate(confidence) : 0.0f;

#ifdef TEMPORAL_DENOISER
    float4 history = srcHistory[dtid];
    // A corrupt/infinite history entry must never feed recurrent reflections.
    // Keep its validity tied to alpha, rather than manufacturing a new hit.
    history = all(isfinite(history)) ?
        float4(max(history.rgb, 0.0f), saturate(history.a)) : 0.0f;
    if (history.a > 1e-3f) {
        float response = saturate(ReflectionTemporalResponse);
		// Receiver reprojection validates the surface; screen-hit confidence and
		// radiance change validate the reflected source. Do not substitute camera
		// motion for source motion: that makes otherwise stable world reflections
		// decay unevenly as the player pans.
		// The GI reprojection stage cuts its local accumulation budget when the
		// receiver's direct lighting changes. Mirror that signal here so indirect
		// reflections shed stale local-light/shadow contributions as well.
		float giFrames = srcGIHistoryLength.Load(int3(dtid, 0)) * 255.0f;
		float historyBudget = max((float)MaxAccumFrames, 1.0f);
		float directLightChange = saturate(1.0f - (giFrames - 1.0f) / max(historyBudget - 1.0f, 1.0f));
		response = max(response, 0.52f * directLightChange);
        response = saturate(lerp(max(response, 0.18f), max(response, 0.07f), roughness));
        float currLum = Luminance(max(current, 0.0f));
        float histLum = Luminance(max(history.rgb, 0.0f));
		float relativeRadianceChange = abs(currLum - histLum) /
			max(max(currLum, histLum), 0.05f);
		// High-confidence screen hits carry new source radiance. Let coherent
		// source changes converge promptly while weak stochastic samples keep
		// relying on temporal stability.
		response = max(response, 0.48f * smoothstep(0.20f, 0.75f, relativeRadianceChange) *
			smoothstep(0.30f, 0.75f, confidence));
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
			// Bridge isolated stochastic misses with a bounded roughness response;
			// invalid receiver history is already rejected by depth/normal reprojection.
			float retain = lerp(0.28f, 0.48f, roughness);
            current = historySafe * retain;
            confidence = history.a * retain;
        }
    }
#endif

    outReflection[dtid] = float4(max(current, 0.0f), confidence);
}
