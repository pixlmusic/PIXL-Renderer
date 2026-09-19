///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation
//
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// XeGTAO is based on GTAO/GTSO "Jimenez et al. / Practical Real-Time Strategies for Accurate Indirect Occlusion",
// https://www.activision.com/cdn/research/Practical_Real_Time_Strategies_for_Accurate_Indirect_Occlusion_NEW%20VERSION_COLOR.pdf
//
// Implementation:  Filip Strugar (filip.strugar@intel.com), Steve Mccalla <stephen.mccalla@intel.com>         (\_/)
// Version:         (see XeGTAO.h)                                                                            (='.'=)
// Details:         https://github.com/GameTechDev/XeGTAO                                                     (")_(")
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// with additional edits by FiveLimbedCat/ProfJack
//
// More references:
//
// Screen Space Indirect Lighting with Visibility Bitmask
//  https://arxiv.org/abs/2301.11376
//
// Exploring Raytraced Future in Metro Exodus
//  https://developer.download.nvidia.com/video/gputechconf/gtc/2019/presentation/s9985-exploring-ray-traced-future-in-metro-exodus.pdf
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Common/Color.hlsli"
#include "Common/FastMath.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/GBuffer.hlsli"
#include "Common/Math.hlsli"
#include "Common/Spherical Harmonics/SphericalHarmonics.hlsli"
#include "HybridGI/common.hlsli"
#include "HybridGI/worldCache.hlsli"

Texture2D<float> srcWorkingDepth : register(t0);
Texture2D<float4> srcNormalRoughness : register(t1);
Texture2D<float3> srcRadiance : register(t2);  // maybe half-res
Texture2D<unorm float2> srcNoise : register(t3);
Texture2D<unorm float> srcAccumFrames : register(t4);  // maybe half-res
Texture2D<float4> srcPrevY : register(t5);             // maybe half-res
Texture2D<float2> srcPrevCoCg : register(t6);          // maybe half-res
Texture2D<float4> srcPrevGISpecular : register(t7);    // maybe half-res
Texture2D<float2> srcNormal : register(t8);
Texture2D<uint> srcWorldMetadata : register(t9);
Texture2D<float4> srcWorldSH0 : register(t10);
Texture2D<float4> srcWorldSH1 : register(t11);
Texture2D<float4> srcWorldSH2 : register(t12);
Texture2D<uint> srcWorldNormal : register(t13);
Texture2D<float3> srcReflectance : register(t14);
Texture2D<float4> srcPrevBentVisibility : register(t15);
Texture2D<unorm float> srcPrevAo : register(t16);

RWTexture2D<unorm float> outAo : register(u0);
RWTexture2D<float4> outY : register(u1);
RWTexture2D<float2> outCoCg : register(u2);
RWTexture2D<float4> outGISpecular : register(u3);
RWTexture2D<half3> outPrevGeo : register(u4);
RWTexture2D<float4> outBentVisibility : register(u5);

float GetDepthFade(float depth)
{
	return saturate((depth - DepthFadeRange.x) * DepthFadeScaleConst);
}

// [Optimization 5]: Bitwise AND instead of integer modulo for power-of-two noise textures
float2 SpatioTemporalNoise(uint2 pixCoord, uint temporalIndex)
{
	uint2 noiseCoord = (pixCoord & 127u) + uint2(0, (temporalIndex & 63u) * 128u);
	return srcNoise.Load(uint3(noiseCoord, 0));
}

// [Walter et al. 2007, "Microfacet models for refraction through rough surfaces"]
float GetNormalDistributionFunctionGGX(float roughness, float NdotH)
{
	float a = roughness * roughness;
	float a2 = a * a;
	float d = max((NdotH * a2 - NdotH) * NdotH + 1, 1e-5);
	return a2 / (Math::PI * d * d);
}

// [Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"]
// [Optimization 8]: Branchless Smith-Joint vis evaluation with epsilon guard
float GetVisibilityFunctionSmithJointApprox(float roughness, float NdotV, float NdotL)
{
	float a = roughness * roughness;
	float visSmithV = NdotL * (NdotV * (1.0 - a) + a);
	float visSmithL = NdotV * (NdotL * (1.0 - a) + a);
	float vis = max(visSmithV + visSmithL, 1e-5);
	return 0.5 / vis;
}

bool ReadWorldVoxelCascade(
	float3 queryPositionWS, float3 receiverNormalWS, float3 sourceDirection, uint cascade,
	inout float3 irradiance, inout float occupancy, bool readRadiance)
{
	irradiance = 0.0;
	occupancy = 0.0;
	float cellSize = WorldCacheCellSize(cascade);
	int3 cell = int3(floor(queryPositionWS / cellSize));
	uint2 atlasCoord = WorldCacheAtlasCoord(cell, cascade);
	uint metadata = srcWorldMetadata.Load(int3(atlasCoord, 0));
	bool valid = (metadata & 0x00ffffffu) == WorldCacheHash(cell, cascade);
	if (valid) {
		uint age = ((FrameIndex & 255u) - (metadata >> 24)) & 255u;
		float ageFade = WorldCacheAgeFade(age, WorldCacheMaxAge);
		valid = ageFade > 0.0;
		if (valid) {
			uint surface = srcWorldNormal.Load(int3(atlasCoord, 0));
			float confidence = UnpackWorldConfidence(surface) * ageFade;
			// Confidence describes sample reliability, not emitted energy. Keep it in
			// occupancy/weighting so uncertain cells matter less, but do not attenuate
			// the stored radiance by confidence a second time.
			occupancy = UnpackWorldOccupancy(surface) * confidence;
			valid = occupancy > (1.0 / 255.0);
			if (valid) {
				float3 sourceNormal = UnpackWorldNormal(surface);
				float receiverFacingSigned = dot(receiverNormalWS, sourceDirection);
				float sourceFacingSigned = dot(sourceNormal, -sourceDirection);
				// One-sided gates reject transport through the back of a cached surfel
				// without applying another cosine. The SH payload already carries source
				// directionality; multiplying sourceFacing again produced a cos^2 energy loss.
				float leakReduction = saturate(WorldCacheLeakReduction);
				float leakWeight = 1.0f;
				[branch] if (leakReduction > 1e-4f) {
					float receiverGate = smoothstep(-0.05f, 0.10f, receiverFacingSigned);
					float sourceGate = smoothstep(-0.05f, 0.15f, sourceFacingSigned);
					leakWeight = lerp(1.0f, receiverGate * sourceGate, leakReduction);
				}
				valid = leakWeight > 1e-3;
				if (valid && readRadiance) {
					irradiance = WorldCacheEvaluateRadiance(
						srcWorldSH0.Load(int3(atlasCoord, 0)),
						srcWorldSH1.Load(int3(atlasCoord, 0)),
						srcWorldSH2.Load(int3(atlasCoord, 0)),
						-sourceDirection) * leakWeight;
				}
			}
		}
	}
	return valid;
}

// [Optimization 2]: Cleaned cascade branching to prevent unnecessary queries
bool ReadWorldVoxel(
	float3 queryPositionWS, float3 receiverNormalWS, float3 rayDirection, float3 cameraWS,
	inout float3 irradiance, inout float cascadeMix, inout float occupancy, bool readRadiance = true)
{
	irradiance = 0.0;
	cascadeMix = 0.0;
	occupancy = 0.0;
	float blend = WorldCacheCascadeBlend(queryPositionWS, cameraWS);

	float3 nearIrradiance = 0.0;
	float nearOccupancy = 0.0;
	bool nearValid = false;

	if (blend < 0.999) {
		nearValid = ReadWorldVoxelCascade(queryPositionWS, receiverNormalWS, rayDirection, 0u,
			nearIrradiance, nearOccupancy, readRadiance);
	}

	bool valid = false;
	if (nearValid && blend <= 0.001) {
		irradiance = nearIrradiance;
		occupancy = nearOccupancy;
		valid = true;
	} else {
		float3 farIrradiance = 0.0;
		float farOccupancy = 0.0;
		bool farValid = ReadWorldVoxelCascade(queryPositionWS, receiverNormalWS, rayDirection, 1u,
			farIrradiance, farOccupancy, readRadiance);

		if (nearValid && farValid) {
			irradiance = lerp(nearIrradiance, farIrradiance, blend);
			occupancy = lerp(nearOccupancy, farOccupancy, blend);
			cascadeMix = blend;
			valid = occupancy > (1.0 / 255.0);
		} else if (nearValid) {
			irradiance = nearIrradiance;
			occupancy = nearOccupancy;
			valid = true;
		} else if (farValid) {
			irradiance = farIrradiance;
			occupancy = farOccupancy;
			cascadeMix = 1.0;
			valid = true;
		}
	}
	return valid;
}

// [Optimization 9]: Accept cameraWS and receiver WS transformations directly
void SampleWorldCache(
	float3 receiverPositionWS, float3 receiverNormalWS, float3 cameraWS,
	out float4 cacheY, out float2 cacheCoCg, out float hitRatio, out float cascadeMix,
	out float directionalOcclusion)
{
	cacheY = 0.0;
	cacheCoCg = 0.0;
	hitRatio = 0.0;
	cascadeMix = 0.0;
	directionalOcclusion = 0.0;
	const bool computeDirectionalOcclusion = WorldCacheDirectionalOcclusionEnabled != 0u &&
		(WorldCacheDirectionalOcclusionStrength > 1e-4f || DebugView == 8u);
	uint sampleCount = clamp(WorldCacheSampleCount, 1u, 8u);
	uint traceSteps = clamp(WorldCacheTraceSteps, 2u, 6u);
	uint receiverCascade = WorldCacheCascadeBlend(receiverPositionWS, cameraWS) >= 0.5 ? 1u : 0u;
	float rotation = WorldCacheStableRotationForCascade(receiverPositionWS, receiverCascade);
	float baseStep = WorldCacheCellSize(receiverCascade);
	float inverseRadius = rcp(max(WorldCacheRadius, 1.0));
	float3 cacheTangent;
	float3 cacheBitangent;
	WorldCacheBasis(receiverNormalWS, cacheTangent, cacheBitangent);

	[loop] for (uint sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
		float3 direction = WorldCacheDirectionFromBasis(
			sampleIndex, sampleCount, receiverNormalWS, cacheTangent, cacheBitangent, rotation);
		float rayTransmittance = 1.0;
		bool foundIrradiance = false;
		float stepPower = 1.0;
		[loop] for (uint stepIndex = 0; stepIndex < traceSteps; ++stepIndex) {
			float stepScale = stepPower + 0.5;
			float distance = min(WorldCacheRadius, baseStep * stepScale);
			float3 sampleIrradiance = 0.0;
			float cascade = 0.0;
			float occupancy = 0.0;
			if (ReadWorldVoxel(receiverPositionWS + direction * distance, receiverNormalWS, direction, cameraWS,
					sampleIrradiance, cascade, occupancy, !foundIrradiance)) {
				float attenuation = rcp(1.0 + distance * inverseRadius);
				if (computeDirectionalOcclusion)
					directionalOcclusion += occupancy * rayTransmittance * attenuation;
				rayTransmittance *= 1.0 - occupancy;

				if (!foundIrradiance && any(sampleIrradiance > 1e-5)) {
					float3 ycocg = Color::RGBToYCoCg(sampleIrradiance * attenuation * occupancy);
					cacheY += ycocg.r * SphericalHarmonics::Evaluate(direction);
					cacheCoCg += ycocg.gb;
					hitRatio += occupancy;
					cascadeMix += cascade * occupancy;
					foundIrradiance = true;
					// Diffuse transport uses only the first radiating hit. Continue
					// occupancy traversal only when directional occlusion consumes it.
					if (!computeDirectionalOcclusion)
						break;
				}
				if (rayTransmittance < 0.05)
					break;
			}
			stepPower += stepPower;
		}
	}

	float normalization = rcp(max((float)sampleCount, 1.0));
	// WorldCacheDirection is uniform over the receiver hemisphere, so projecting
	// incident radiance into SH requires the hemisphere solid-angle factor 2*PI/N.
	// The previous unit average omitted this integral weight and starved the cache.
	float cacheTransportScale = 2.0 * Math::PI;
	cacheY *= normalization * cacheTransportScale;
	cacheCoCg *= normalization * cacheTransportScale;
	hitRatio *= normalization;

	// [Optimization 7]: Use fast hardware exp2 native instruction, but only when
	// the directional-occlusion result is consumed by the composite.
	if (computeDirectionalOcclusion)
		directionalOcclusion = 1.0 - exp2(-directionalOcclusion * normalization * (0.45 * 1.44269504));
	cascadeMix = hitRatio > 0.0 ? cascadeMix / max(hitRatio * sampleCount, 1.0) : 0.0;
}

void SampleWorldCacheReflection(
	float3 receiverPositionWS, float3 receiverNormalWS, float3 receiverToCameraWS, float roughness, float3 cameraWS,
	out float3 reflectionRadiance, out float reflectionWeight)
{
	reflectionRadiance = 0.0;
	reflectionWeight = 0.0;
	float3 reflectionDirection = normalize(reflect(-receiverToCameraWS, receiverNormalWS));
	if (dot(reflectionDirection, receiverNormalWS) <= 0.01)
		return;

	uint receiverCascade = WorldCacheCascadeBlend(receiverPositionWS, cameraWS) >= 0.5 ? 1u : 0u;
	float baseStep = WorldCacheCellSize(receiverCascade);
	uint traceSteps = clamp(WorldCacheTraceSteps, 2u, 6u);
	float rayTransmittance = 1.0;
	float inverseRadius = rcp(max(WorldCacheRadius, 1.0));
	float roughnessScale = lerp(1.0, 1.8, roughness);
	float stepPower = 1.0;

	[loop] for (uint stepIndex = 0; stepIndex < traceSteps; ++stepIndex) {
		float stepScale = (stepPower + 1.0) * roughnessScale;
		float distance = min(WorldCacheRadius, baseStep * stepScale);
		float3 sampleIrradiance = 0.0;
		float cascade = 0.0;
		float occupancy = 0.0;
		if (ReadWorldVoxel(receiverPositionWS + reflectionDirection * distance,
				receiverNormalWS, reflectionDirection, cameraWS, sampleIrradiance, cascade, occupancy)) {
			float attenuation = rcp(1.0 + distance * inverseRadius);
			float weight = occupancy * rayTransmittance * attenuation;
			// Attenuation belongs in the sample weight only. Applying it again to
			// radiance produced attenuation^2 and starved the reflection fallback.
			reflectionRadiance += sampleIrradiance * weight;
			reflectionWeight += weight;
			rayTransmittance *= 1.0 - occupancy;
			if (rayTransmittance < 0.05)
				break;
		}
		stepPower += stepPower;
	}

	if (reflectionWeight > 1e-4)
		reflectionRadiance /= reflectionWeight;
	reflectionWeight = saturate(reflectionWeight);
}

// Quantize an angular interval robustly. Saturating and sorting the endpoints
// avoids unsigned underflow from a reversed interval, while limiting the count
// to the bits remaining after shift prevents undefined SM5 shifts by 32.
uint SafeBitMask(uint count, uint shift) {
	uint result = 0u;
	if (shift < 32u && count != 0u) {
		count = min(count, 32u - shift);
		uint mask = count == 32u ? 0xFFFFFFFFu : ((1u << count) - 1u);
		result = mask << shift;
	}
	return result;
}

uint QuantizeAngularMask(float2 normalizedRange)
{
	float lo = saturate(min(normalizedRange.x, normalizedRange.y));
	float hi = saturate(max(normalizedRange.x, normalizedRange.y));
	uint first = (uint)round(lo * 32.0f);
	uint last = (uint)round(hi * 32.0f);
	return SafeBitMask(last - first, first);
}

#if defined(ADAPTIVE_RAY_ALLOCATION)
// First-stage Task 09 classifier. It deliberately shares one conservative
// maximum across the existing 8x8 GI group: a single disocclusion or geometric
// edge keeps the whole tile at high quality, while stable, low-frequency tiles
// may reduce both horizon-loop dimensions. No queue, indirect dispatch or new
// texture is introduced until this classification has live profiler proof.
groupshared uint gAdaptiveTileDemand;

float AdaptiveWorldCacheConfidence(float3 viewspacePosition)
{
	if (WorldCacheEnabled == 0u)
		return 1.0f;

	float3 cameraWS = ViewToWorldPosition(0.0f, FrameBuffer::CameraViewInverse) + FrameBuffer::CameraPosAdjust.xyz;
	float3 positionWS = ViewToWorldPosition(viewspacePosition, FrameBuffer::CameraViewInverse) + FrameBuffer::CameraPosAdjust.xyz;
	uint cascade = WorldCacheCascadeBlend(positionWS, cameraWS) >= 0.5f ? 1u : 0u;
	int3 cell = int3(floor(positionWS / WorldCacheCellSize(cascade)));
	uint2 atlasCoord = WorldCacheAtlasCoord(cell, cascade);
	uint metadata = srcWorldMetadata.Load(int3(atlasCoord, 0));
	if ((metadata & 0x00ffffffu) != WorldCacheHash(cell, cascade))
		return 0.0f;

	uint age = ((FrameIndex & 255u) - (metadata >> 24)) & 255u;
	float ageFade = WorldCacheAgeFade(age, WorldCacheMaxAge);
	uint surface = srcWorldNormal.Load(int3(atlasCoord, 0));
	return saturate(UnpackWorldConfidence(surface) * UnpackWorldOccupancy(surface) * ageFade);
}

float ClassifyAdaptiveRayDemand(
	uint2 dtid, float2 uv, float2 frameScale, float viewspaceZ, float3 viewspaceNormal)
{
	const bool validSurface = viewspaceZ > FP_Z && viewspaceZ < DepthFadeRange.y;
	if (!validSurface)
		return 0.0f;

	const float coarseMip = min((float)RES_MIP + 1.0f, 4.0f);
	float coarseZ = srcWorkingDepth.SampleLevel(samplerPointClamp, uv * frameScale, coarseMip);
	float depthDemand = saturate(abs(coarseZ - viewspaceZ) / max(abs(viewspaceZ), 64.0f) * 24.0f);

	float3 coarseNormal = GBuffer::DecodeNormal(srcNormal.SampleLevel(samplerPointClamp, uv * frameScale, coarseMip));
	float normalDemand = saturate((1.0f - abs(dot(viewspaceNormal, coarseNormal))) * 6.0f);

	float accumulatedFrames = srcAccumFrames[dtid] * 255.0f;
	// radianceDisocc already converts motion, depth/normal rejection and
	// disocclusion into a reduced history count, so this consumes all four
	// signals without adding another motion-vector binding to the GI pass.
	float temporalDemand = 1.0f - saturate(accumulatedFrames * (1.0f / 8.0f));

	const float fineRadianceMip = min((float)RES_MIP, 4.0f);
	const float coarseRadianceMip = min(fineRadianceMip + 2.0f, 4.0f);
	float fineLuminance = Luminance(srcRadiance.SampleLevel(samplerPointClamp, uv * frameScale, fineRadianceMip));
	float coarseLuminance = Luminance(srcRadiance.SampleLevel(samplerPointClamp, uv * frameScale, coarseRadianceMip));
	float luminanceDemand = saturate(abs(fineLuminance - coarseLuminance) /
		max(max(fineLuminance, coarseLuminance), 0.05f) * 1.5f);

	float4 normalRoughness = FULLRES_LOAD(srcNormalRoughness, dtid, uv * frameScale, samplerLinearClamp);
	float roughness = saturate(1.0f - normalRoughness.z);
	float3 reflectance = FULLRES_LOAD(srcReflectance, dtid, uv * frameScale, samplerLinearClamp);
	float specularDemand = saturate((1.0f - roughness) * 1.5f) * saturate(Luminance(reflectance) * 4.0f);

	float3 viewspacePosition = ScreenToViewPosition(uv, viewspaceZ);
	float cacheDemand = 1.0f - AdaptiveWorldCacheConfidence(viewspacePosition);

	float demand = max(depthDemand, normalDemand);
	demand = max(demand, temporalDemand * 0.90f);
	demand = max(demand, luminanceDemand * 0.80f);
	demand = max(demand, specularDemand);
	demand = max(demand, cacheDemand * 0.60f);
	return saturate(demand);
}
#endif

void CalculateGI(
	uint2 dtid, float2 uv, float viewspaceZ, float3 viewspaceNormal,
	uint effectiveNumSlices, uint effectiveNumSteps,
	out float o_ao, out sh2 o_currY, out float2 o_currCoCg, out float4 o_currGIAOSpecular, out float4 o_bentVisibility)
{
	const float2 frameScale = FrameDim * RcpTexDim;
	float2 normalizedScreenPos = uv;

	const float rcpNumSlices = rcp((float)effectiveNumSlices);
	const float rcpNumSteps = rcp((float)effectiveNumSteps);

	const float pixelTooCloseThreshold = 1.3;
	const float2 pixelDirRBViewspaceSizeAtCenterZ = viewspaceZ.xx * NDCToViewMul.xy * RCP_OUT_FRAME_DIM;

	float screenspaceRadius = EffectRadius / pixelDirRBViewspaceSizeAtCenterZ.x;
	screenspaceRadius = max(MinScreenRadius, screenspaceRadius);
	const float minS = pixelTooCloseThreshold / screenspaceRadius;

	// uv is generated from (dtid + 0.5) / OUT_FRAME_DIM, so truncating it back
	// to integer coordinates is exactly dtid. Avoid the redundant float multiply.
	const float2 localNoise = SpatioTemporalNoise(dtid, FrameIndex);
	const float noiseSlice = localNoise.x;
	const float noiseStep = localNoise.y;

	const float3 pixCenterPos = ScreenToViewPosition(normalizedScreenPos, viewspaceZ);
	const float3 viewVec = normalize(-pixCenterPos);
#if defined(GI_SPECULAR) && !defined(HYBRID_REFLECTIONS)
	const float NoV = clamp(dot(viewVec, viewspaceNormal), 1e-5, 1);
#endif

	if (dot(viewVec, pixCenterPos) > 0)
		viewspaceNormal = -viewspaceNormal;

	float visibility = 0;
	float visibilitySpecular = 0;
	float4 radianceY = 0;
	float2 radianceCoCg = 0;
	float3 radianceSpecular = 0;
	float giCoverage = 0;
	float3 bentDirectionAccum = 0.0f;
	float bentWeightAccum = 0.0f;
	float contactDepthAccum = 0.0f;
	float contactDepthWeight = 0.0f;
	float contactRadiusSq = 0.0f;
	float invContactRadius = 0.0f;
	float invContactBiasRange = 0.0f;
	if (ContactDepthEnabled != 0u) {
		contactRadiusSq = ContactDepthRadius * ContactDepthRadius;
		invContactRadius = rcp(max(ContactDepthRadius, 1.0f));
		invContactBiasRange = rcp(max(1.0f - ContactDepthBias, 0.01f));
	}

#ifdef GI
	const float surfaceRoughness = max(0.05, saturate(1 - FULLRES_LOAD(srcNormalRoughness, dtid, uv * frameScale, samplerLinearClamp).z));
#endif
#if defined(GI_SPECULAR) && !defined(HYBRID_REFLECTIONS)
	const float roughness = max(0.2, surfaceRoughness);
	const float specularConeHalfAngle = max(5e-2, specularLobeHalfAngle(roughness));
	const float specularConeScale = 0.5f * rcp(specularConeHalfAngle);
#endif

	// [Optimization 1 & 9]: Precalculate constants for inside the slice/step loops
	const float2 scaledOutFrameRcp = RCP_OUT_FRAME_DIM * OUT_FRAME_SCALE;
	float3 receiverPositionWS = 0.0;
	float3 receiverNormalWS = 0.0;
	float3 cameraWS = 0.0;

	if (WorldCacheEnabled != 0u) {
		cameraWS = ViewToWorldPosition(0.0, FrameBuffer::CameraViewInverse) + FrameBuffer::CameraPosAdjust.xyz;
		receiverPositionWS = ViewToWorldPosition(pixCenterPos, FrameBuffer::CameraViewInverse) + FrameBuffer::CameraPosAdjust.xyz;
		receiverNormalWS = normalize(ViewToWorldVector(viewspaceNormal, FrameBuffer::CameraViewInverse));
	}

	for (uint slice = 0; slice < effectiveNumSlices; slice++) {
		float phi = (Math::PI * rcpNumSlices) * (slice + noiseSlice);
		float3 directionVec = 0;
		sincos(phi, directionVec.y, directionVec.x);

		float2 omega = float2(directionVec.x, -directionVec.y) * screenspaceRadius;
		const float logLenOmega = 0.5 * log2(max(dot(omega, omega), EPSILON_LENGTH_SQ));

		const float3 orthoDirectionVec = directionVec - (dot(directionVec, viewVec) * viewVec);
		const float3 axisVec = normalize(cross(orthoDirectionVec, viewVec));

		float3 projectedNormalVec = viewspaceNormal - axisVec * dot(viewspaceNormal, axisVec);
		float rcpProjectedNormalVecLength = rsqrt(max(dot(projectedNormalVec, projectedNormalVec), EPSILON_LENGTH_SQ));
		float signNorm = sign(dot(orthoDirectionVec, projectedNormalVec));
		float cosNorm = saturate(dot(projectedNormalVec, viewVec) * rcpProjectedNormalVecLength);

		float n = signNorm * FastMath::ACos(cosNorm);

		uint bitmask = 0;
#ifdef GI
		uint bitmaskGI = 0;
#	if defined(GI_SPECULAR) && !defined(HYBRID_REFLECTIONS)
		uint bitmaskGISpecular = 0;
		float3 domVec = getSpecularDominantDirection(viewspaceNormal, viewVec, roughness);
		float3 projectedDomVec = normalize(domVec - axisVec * dot(domVec, axisVec));
		float nDom = sign(dot(orthoDirectionVec, projectedDomVec)) * FastMath::ACos(saturate(dot(projectedDomVec, viewVec)));
#	endif
#endif

		float stepNoise = frac(noiseStep + slice * 0.6180339887498948482);

		[unroll] for (int sideSign = -1; sideSign <= 1; sideSign += 2)
		{
			[loop] for (uint step = 0; step < effectiveNumSteps; step++)
			{
				float s = (step + stepNoise) * rcpNumSteps;
				s *= s;
				s += minS;

				float2 sampleOffset = s * omega;
				float2 samplePxCoord = dtid + .5 + sampleOffset * sideSign;
				float2 sampleUV = samplePxCoord * RCP_OUT_FRAME_DIM;

				// Each side advances monotonically away from the receiver. Once
				// outside the viewport, all later steps on that side are outside too.
				[branch] if (any(sampleUV > 1.0) || any(sampleUV < 0.0)) break;

				// SetupResources allocates exactly five levels (indices 0..4). Keep
				// traversal explicit instead of relying on implicit sampler clamping.
				float mipLevel = clamp(log2(s) + logLenOmega - 3.3, 0, 4);
				float mipLevelRadiance = mipLevel;
#if defined(HALF_RES)
				mipLevel = max(mipLevel, 1);
				mipLevelRadiance = max(mipLevelRadiance, 2);
#elif defined(QUARTER_RES)
				mipLevel = max(mipLevel, 2);
				mipLevelRadiance = max(mipLevelRadiance, 3);
#else
				mipLevelRadiance = max(mipLevelRadiance, 1);
#endif

				float SZ = srcWorkingDepth.SampleLevel(samplerPointClamp, sampleUV * frameScale, mipLevel);
				float3 samplePos = ScreenToViewPosition(sampleUV, SZ);
				float3 sampleDelta = samplePos - pixCenterPos;

				// Do not pay for back-horizon normalization/acos work once a sample lies
				// outside the corresponding effect radius. The old code computed these
				// values and only then discarded the generated mask. Outputs are identical.
				bool needAOAngular = s < AORadius && bitmask != 0xffffffffu;
#ifdef GI
				bool needGIAngular = s < GIRadius && bitmaskGI != 0xffffffffu;
#	if defined(GI_SPECULAR) && !defined(HYBRID_REFLECTIONS)
				needGIAngular = needGIAngular || (s < GIRadius && bitmaskGISpecular != 0xffffffffu);
#	endif
#else
				bool needGIAngular = false;
#endif
				bool needContactDepth = ContactDepthEnabled != 0u;
				if (!needAOAngular && !needGIAngular && !needContactDepth)
					continue;

				float sampleDistanceSq = max(dot(sampleDelta, sampleDelta), EPSILON_LENGTH_SQ);
				float invSampleDistance = rsqrt(sampleDistanceSq);
				float3 sampleHorizonVec = sampleDelta * invSampleDistance;

				// PIXL Contact Depth: reuse the horizon vector's reciprocal length. This
				// removes a second sqrt/normalize pair whenever ContactDepth is enabled.
				[branch] if (needContactDepth) {
					if (sampleDistanceSq < contactRadiusSq) {
						float sampleDistance = sampleDistanceSq * invSampleDistance;
						float radialWeight = saturate(1.0f - sampleDistance * invContactRadius);
						radialWeight *= radialWeight;
						float horizon = saturate((dot(viewspaceNormal, sampleHorizonVec) - ContactDepthBias) *
							invContactBiasRange);
						contactDepthAccum += horizon * radialWeight;
						contactDepthWeight += radialWeight;
					}
				}

				float angleFront = 0.0f;
				if (needAOAngular || needGIAngular)
					angleFront = FastMath::ACos(clamp(dot(sampleHorizonVec, viewVec), -1.0f, 1.0f));

				uint maskedBits = 0u;
				if (needAOAngular) {
					float3 sampleBackHorizonVec = normalize(sampleDelta - viewVec * Thickness);
					float angleBack = FastMath::ACos(clamp(dot(sampleBackHorizonVec, viewVec), -1.0f, 1.0f));
					float2 angleRange = -sideSign * (sideSign == -1 ? float2(angleFront, angleBack) : float2(angleBack, angleFront));
					angleRange = smoothstep(0, 1, (angleRange + n) * Math::INV_PI + .5);
					maskedBits = QuantizeAngularMask(angleRange);
				}

#ifdef GI
				uint maskedBitsGI = 0u;
#	if defined(GI_SPECULAR) && !defined(HYBRID_REFLECTIONS)
				uint maskedBitsGISpecular = 0u;
#	endif

				if (needGIAngular) {
					float3 sampleBackHorizonVecGI = normalize(sampleDelta - viewVec * 300);
					float angleBackGI = FastMath::ACos(clamp(dot(sampleBackHorizonVecGI, viewVec), -1.0f, 1.0f));
					float2 angleRangeGI = -sideSign * (sideSign == -1 ? float2(angleFront, angleBackGI) : float2(angleBackGI, angleFront));

#	if defined(GI_SPECULAR) && !defined(HYBRID_REFLECTIONS)
					if (bitmaskGISpecular != 0xffffffffu) {
						float2 angleRangeSpecular = clamp((angleRangeGI + nDom) * specularConeScale, -1, 1) * 0.5 + 0.5;
						maskedBitsGISpecular = QuantizeAngularMask(angleRangeSpecular);
					}
#	endif

					if (bitmaskGI != 0xffffffffu) {
						angleRangeGI = smoothstep(0, 1, (angleRangeGI + n) * Math::INV_PI + .5);
						maskedBitsGI = QuantizeAngularMask(angleRangeGI);
					}

					uint validBits = maskedBitsGI & ~bitmaskGI;
					uint validBitCount = countbits(validBits);
					bool checkGI = validBitCount != 0u;

#	if defined(GI_SPECULAR) && !defined(HYBRID_REFLECTIONS)
					uint overlappedBitsSpecular = maskedBitsGISpecular & ~bitmaskGISpecular;
					// The existing specular estimator is multiplied by the diffuse valid-bit
					// weight below, so a spec-only interval contributes exactly zero. Avoid
					// the normal/radiance fetch in that zero-contribution case.
#	endif

					if (checkGI) {
						float giBoost = 4.0 * Math::PI * (1 + GIDistanceCompensation * smoothstep(0, GICompensationMaxDist, s * EffectRadius));

						// [Optimization 1 & 3]: Optimized precalculated frame scale UVs and reduced redundant decoding
						float2 radianceSampleUV = samplePxCoord * scaledOutFrameRcp;
						float3 normalSample = GBuffer::DecodeNormal(srcNormal.SampleLevel(samplerPointClamp, radianceSampleUV, mipLevelRadiance));
						if (dot(samplePos, normalSample) > 0)
							normalSample = -normalSample;
						float frontBackMult = max(0.0, -dot(normalSample, sampleHorizonVec));

						if (frontBackMult > 0.f) {
							// CameraViewInverse is rigid; rotating a unit view vector preserves length.
							float3 sampleHorizonVecWS = mul(FrameBuffer::CameraViewInverse, half4(sampleHorizonVec, 0)).xyz;

							float3 sampleRadiance = srcRadiance.SampleLevel(samplerPointClamp, radianceSampleUV, mipLevelRadiance).rgb * frontBackMult * giBoost * validBitCount * 0.03125f;
							// Clamp after geometric/distance weighting, where a sparse
							// horizon sample can become a true outlier. Prefiltering the
							// source alone cannot catch this amplification.
							sampleRadiance = ClampFireflies(max(sampleRadiance, 0), RadianceFireflyClamp);
							float3 sampleRadianceYCoCg = Color::RGBToYCoCg(sampleRadiance);

							radianceY += sampleRadianceYCoCg.r * SphericalHarmonics::Evaluate(sampleHorizonVecWS);
							radianceCoCg += sampleRadianceYCoCg.gb;

#	if defined(GI_SPECULAR) && !defined(HYBRID_REFLECTIONS)
							if (overlappedBitsSpecular != 0u) {
								float NoH = clamp(dot(viewspaceNormal, normalize(viewVec + sampleHorizonVec)), 1e-2, 1);
								float NoL = clamp(dot(viewspaceNormal, sampleHorizonVec), 1e-2, 1);

								float3 specularRadiance = sampleRadiance * countbits(overlappedBitsSpecular) * 0.03125;
								specularRadiance *= GetNormalDistributionFunctionGGX(roughness, NoH) * GetVisibilityFunctionSmithJointApprox(roughness, NoV, NoL);
								specularRadiance = max(0, specularRadiance);

								radianceSpecular += specularRadiance;
							}
#	endif
						}
					}
				}
#endif
				bitmask |= maskedBits;
#ifdef GI
				bitmaskGI |= maskedBitsGI;
#	if defined(GI_SPECULAR) && !defined(HYBRID_REFLECTIONS)
				// [Fix] This accumulation was missing: bitmaskGISpecular was
				// declared and read (via ~bitmaskGISpecular above) but never
				// updated, so it stayed 0 for the whole slice. That made
				// overlappedBitsSpecular == maskedBitsGISpecular on every
				// single step instead of only the *newly* covered angular
				// bits, so overlapping steps along a slice double- (or
				// N-times-) counted the same specular angular range,
				// over-brightening specular GI. It also meant
				// countbits(bitmaskGISpecular) below - used to build
				// visibilitySpecular, which later gates both the temporal
				// blend and the world-cache reflection screen-space-miss
				// fallback - always evaluated as if the mask were empty,
				// so visibilitySpecular came out as 0 every frame.
				bitmaskGISpecular |= maskedBitsGISpecular;
#	endif
#endif
				bool angularCoverageComplete = bitmask == 0xffffffffu;
#ifdef GI
				angularCoverageComplete = angularCoverageComplete && bitmaskGI == 0xffffffffu;
#	if defined(GI_SPECULAR) && !defined(HYBRID_REFLECTIONS)
				angularCoverageComplete = angularCoverageComplete && bitmaskGISpecular == 0xffffffffu;
#	endif
#endif
				[branch] if (angularCoverageComplete)
					break;
			}
		}

		// Derive a directional visibility estimate from the same horizon mask.
		// The asymmetry between the two angular halves bends the normal away
		// from blocked directions; no additional depth rays are required.
		float lowerOcclusion = (float)countbits(bitmask & 0x0000ffffu) * (1.0f / 16.0f);
		float upperOcclusion = (float)countbits((bitmask >> 16) & 0x0000ffffu) * (1.0f / 16.0f);
		float sliceVisibility = 1.0f - (float)countbits(bitmask) * 0.03125f;
		float sideBias = clamp(lowerOcclusion - upperOcclusion, -1.0f, 1.0f);
		float3 sliceBent = normalize(viewspaceNormal + orthoDirectionVec * (sideBias * 1.35f));
		float bentWeight = max(sliceVisibility, 0.05f);
		bentDirectionAccum += sliceBent * bentWeight;
		bentWeightAccum += bentWeight;

		visibility += countbits(bitmask) * 0.03125;

#if defined(GI) && defined(GI_SPECULAR) && !defined(HYBRID_REFLECTIONS)
		visibilitySpecular += countbits(bitmaskGISpecular) * 0.03125;
#endif
#ifdef GI
		giCoverage += countbits(bitmaskGI) * 0.03125;
#endif
	}

	float depthFade = GetDepthFade(viewspaceZ);

	float rawOcclusion = saturate(visibility * rcpNumSlices);
	// Horizon coverage is AO-like area coverage, not a specular-cone aperture.
	// Remap it conservatively so ordinary interiors do not report a uniformly
	// half-closed specular hemisphere.
	float rawDirectionalVisibility = saturate(1.0f - rawOcclusion);
	float directionalVisibility = sqrt(rawDirectionalVisibility);
	visibility = lerp(rawOcclusion, 0, depthFade);
	visibility = 1 - pow(abs(1 - visibility), AOPower);
	if (ContactDepthEnabled != 0u && contactDepthWeight > 1e-4f) {
		float contactDepth = saturate(contactDepthAccum / contactDepthWeight) *
			saturate(ContactDepthStrength) * (1.0f - depthFade);
		// Union rather than addition keeps total occlusion energy bounded.
		visibility = 1.0f - (1.0f - saturate(visibility)) * (1.0f - contactDepth);
	}

	float3 rawBentVS = bentWeightAccum > 1e-4f ? normalize(bentDirectionAccum / bentWeightAccum) : viewspaceNormal;

	// Directional visibility is a screen-space observation, so its confidence
	// must describe more than the distance fade. Near a viewport boundary the
	// horizon footprint is clipped and an "open" direction may simply mean that
	// the ray left the screen. Fade only the observation there; AO/GI retain their
	// existing behavior and the consumer falls back to the geometric normal and
	// unoccluded probe lighting. Cap the transition to 64 pixels so this remains a
	// narrow stability guard rather than a visible vignette.
	float2 viewportBorderPixels = min(normalizedScreenPos, 1.0f - normalizedScreenPos) * OUT_FRAME_DIM;
	float closestViewportBorder = min(viewportBorderPixels.x, viewportBorderPixels.y);
	float confidenceBorderWidth = clamp(screenspaceRadius * 0.20f, 8.0f, 64.0f);
	float viewportConfidence = smoothstep(0.0f, 1.0f, saturate(closestViewportBorder / confidenceBorderWidth));

	// Adaptive ray allocation deliberately lowers the angular sample budget on
	// quiet tiles. It is still a valid observation, but should not carry exactly
	// the same authority as a fully sampled tile when its direction is reused by
	// several downstream lighting systems.
	float sliceCoverage = saturate((float)effectiveNumSlices / max((float)NumSlices, 1.0f));
	float stepCoverage = saturate((float)effectiveNumSteps / max((float)NumSteps, 1.0f));
	float samplingConfidence = sqrt(max(sliceCoverage * stepCoverage, 0.0f));
	float observationConfidence = (1.0f - depthFade) * viewportConfidence * lerp(0.65f, 1.0f, samplingConfidence);

	// Store the strength-adjusted direction independently from observation
	// confidence. DeferredComposite applies confidence exactly once when choosing
	// between the geometric and bent normal. Baking it here as well made the bend
	// confidence-squared through distance/edge transitions.
	float bentAmount = BentNormalEnabled != 0u ? saturate(BentNormalStrength) : 0.0f;
	float3 bentVS = normalize(lerp(viewspaceNormal, rawBentVS, bentAmount));
	float3 bentWS = normalize(ViewToWorldVector(bentVS, FrameBuffer::CameraViewInverse));
	float2 encodedBentWS = GBuffer::EncodeNormal(bentWS);

	// Directional visibility is useful independently of bent-normal lighting.
	// Fade it toward fully visible as HybridGI itself fades out, rather than
	// multiplying toward zero (which would incorrectly make far-field surfaces
	// *more* occluded). The deferred composite decides whether to apply it.
	float storedDirectionalVisibility = lerp(1.0f, directionalVisibility, observationConfidence);
	o_bentVisibility = float4(encodedBentWS, storedDirectionalVisibility, observationConfidence);

#ifdef GI
	radianceY *= rcpNumSlices;
	radianceY = lerp(radianceY, 0, depthFade);

	radianceCoCg *= rcpNumSlices * GISaturation;

	float4 cacheY = 0.0;
	float2 cacheCoCg = 0.0;
	float cacheHitRatio = 0.0;
	float cacheCascadeMix = 0.0;
	float cacheDirectionalOcclusion = 0.0;
	float3 cacheReflectionRadiance = 0.0;
	float cacheReflectionHit = 0.0;
	if (WorldCacheEnabled != 0u) {
		const bool needCacheDiffuse = WorldCacheStrength > 1e-4f;
		const bool needCacheDirectional = WorldCacheDirectionalOcclusionEnabled != 0u &&
			WorldCacheDirectionalOcclusionStrength > 1e-4f;
		const bool needCacheDebug = DebugView >= 5u && DebugView <= 8u;

		[branch] if (needCacheDiffuse || needCacheDirectional || needCacheDebug) {
			SampleWorldCache(receiverPositionWS, receiverNormalWS, cameraWS, cacheY, cacheCoCg,
				cacheHitRatio, cacheCascadeMix, cacheDirectionalOcclusion);
		}

		if (needCacheDiffuse) {
			float screenMiss = 1.0f - saturate(giCoverage * rcpNumSlices);
			float cacheBlend = WorldCacheStrength * lerp(0.15f, 1.0f, screenMiss);
			radianceY += cacheY * cacheBlend;
			radianceCoCg += cacheCoCg * cacheBlend * GISaturation;
		}
		if (needCacheDirectional) {
			float voxelAO = cacheDirectionalOcclusion * WorldCacheDirectionalOcclusionStrength * 0.40f * (1.0f - depthFade);
			visibility = 1.0f - (1.0f - visibility) * (1.0f - saturate(voxelAO));
		}
		bool needCacheReflection = (DebugView == 9u);
#	if defined(GI_SPECULAR) && !defined(HYBRID_REFLECTIONS)
		needCacheReflection = needCacheReflection || (WorldCacheReflectionEnabled != 0u);
#	endif
		if (WorldCacheReflectionEnabled != 0u && needCacheReflection) {
			float3 receiverToCameraWS = normalize(ViewToWorldVector(viewVec, FrameBuffer::CameraViewInverse));
			SampleWorldCacheReflection(receiverPositionWS, receiverNormalWS, receiverToCameraWS, surfaceRoughness, cameraWS,
				cacheReflectionRadiance, cacheReflectionHit);
		}
	}

	if (DebugView >= 5u && DebugView <= 9u) {
		float3 debugColor = 0.0;
		if (DebugView == 5u)
			debugColor = lerp(float3(0.35, 0.0, 0.0), float3(0.0, 1.0, 0.15), cacheHitRatio);
		else if (DebugView == 6u)
			debugColor = lerp(float3(0.0, 0.7, 1.0), float3(1.0, 0.1, 0.7), cacheCascadeMix) * max(cacheHitRatio, 0.15);
		else if (DebugView == 7u) {
			// Display the cache independently of its artistic contribution slider.
			float3 cacheRGB = max(Color::YCoCgToRGB(float3(cacheY.x, cacheCoCg)), 0.0);
			debugColor = 1.0 - exp(-cacheRGB);
		}
		else if (DebugView == 8u)
			debugColor = saturate(cacheDirectionalOcclusion).xxx;
		else {
			float3 cacheReflectionDisplay = max(cacheReflectionRadiance * cacheReflectionHit, 0.0);
			debugColor = 1.0 - exp(-cacheReflectionDisplay);
		}
		float3 debugYCoCg = Color::RGBToYCoCg(debugColor);
		radianceY = float4(debugYCoCg.r / 0.88622692545, 0.0, 0.0, 0.0);
		radianceCoCg = debugYCoCg.gb;
		visibility = 0.0;
	}

#	if defined(GI_SPECULAR) && !defined(HYBRID_REFLECTIONS)
	radianceSpecular *= rcpNumSlices;
	radianceSpecular = lerp(radianceSpecular, 0, depthFade);

	visibilitySpecular *= rcpNumSlices;
	visibilitySpecular = lerp(saturate(visibilitySpecular), 0, depthFade);

	if (WorldCacheReflectionEnabled != 0u && cacheReflectionHit > 1e-4) {
		float screenSpaceMiss = 1.0 - visibilitySpecular;
		float roughnessFade = 1.0 - smoothstep(
			WorldCacheReflectionRoughnessCutoff * 0.80,
			WorldCacheReflectionRoughnessCutoff,
			roughness);
		radianceSpecular += cacheReflectionRadiance * cacheReflectionHit * WorldCacheReflectionStrength *
			screenSpaceMiss * roughnessFade * (1.0 - depthFade);
	}
#	endif
#endif

	o_ao = visibility;
	o_currY = radianceY;
	o_currCoCg = radianceCoCg;
	o_currGIAOSpecular = float4(radianceSpecular, visibilitySpecular);
}

[numthreads(8, 8, 1)] void main(
	const uint2 dtid : SV_DispatchThreadID
#if defined(ADAPTIVE_RAY_ALLOCATION)
	, const uint3 groupThreadID : SV_GroupThreadID
#endif
) {
	const bool insideFrame = !any(dtid >= uint2(OUT_FRAME_DIM));
#if !defined(ADAPTIVE_RAY_ALLOCATION)
	if (!insideFrame)
		return;
#endif

	const float2 frameScale = FrameDim * RcpTexDim;
	uint2 pxCoord = dtid;
	float2 uv = (pxCoord + .5) * RCP_OUT_FRAME_DIM;

	float viewspaceZ = 0.0f;
	float3 viewspaceNormal = float3(0.0f, 0.0f, 1.0f);
	if (insideFrame) {
		viewspaceZ = READ_DEPTH(srcWorkingDepth, pxCoord);
		float2 normalSample = FULLRES_LOAD(srcNormal, pxCoord, uv * frameScale, samplerLinearClamp);
		viewspaceNormal = GBuffer::DecodeNormal(normalSample);
	}

	uint effectiveNumSlices = NumSlices;
	uint effectiveNumSteps = NumSteps;
#if defined(ADAPTIVE_RAY_ALLOCATION)
	if (groupThreadID.x == 0u && groupThreadID.y == 0u && groupThreadID.z == 0u)
		gAdaptiveTileDemand = 0u;
	GroupMemoryBarrierWithGroupSync();

	float localDemand = insideFrame ?
		ClassifyAdaptiveRayDemand(pxCoord, uv, frameScale, viewspaceZ, viewspaceNormal) : 0.0f;
	uint quantizedDemand = (uint)round(saturate(localDemand) * 65535.0f);
	InterlockedMax(gAdaptiveTileDemand, quantizedDemand);
	GroupMemoryBarrierWithGroupSync();

	if (!insideFrame)
		return;

	float tileDemand = (float)gAdaptiveTileDemand * (1.0f / 65535.0f);
	float minimumWork = clamp(AdaptiveRayMinimum, 0.35f, 1.0f);
	float workFraction = lerp(minimumWork, 1.0f, smoothstep(0.10f, 0.75f, tileDemand));
	effectiveNumSlices = max(1u, min(NumSlices, (uint)ceil((float)NumSlices * workFraction)));
	effectiveNumSteps = max(1u, min(NumSteps, (uint)ceil((float)NumSteps * workFraction)));
#endif

	half2 encodedWorldNormal = GBuffer::EncodeNormal(ViewToWorldVector(viewspaceNormal, FrameBuffer::CameraViewInverse));
	outPrevGeo[pxCoord] = half3(viewspaceZ, encodedWorldNormal);

	viewspaceZ *= 0.99920h;

	float currAo = 0;

	// [Optimization 10]: Utilize 16-bit half registers for temporal accumulator intermediate precision
	half4 currY = 0;
	half2 currCoCg = 0;
	half4 currGIAOSpecular = half4(0, 0, 0, 0);
	half4 currBentVisibility = half4(encodedWorldNormal, 1.0h, 0.0h);

	bool needGI = viewspaceZ > FP_Z && viewspaceZ < DepthFadeRange.y;
	if (needGI) {
		float4 tempY;
		float2 tempCoCg;
		float4 tempSpec;
		float4 tempBentVisibility;

		CalculateGI(
			pxCoord, uv, viewspaceZ, viewspaceNormal,
			effectiveNumSlices, effectiveNumSteps,
			currAo, tempY, tempCoCg, tempSpec, tempBentVisibility);

		currY = (half4)tempY;
		currCoCg = (half2)tempCoCg;
		currGIAOSpecular = (half4)tempSpec;
		currBentVisibility = (half4)tempBentVisibility;

#ifdef TEMPORAL_DENOISER
		float accumulatedFrames = srcAccumFrames[pxCoord] * 255.0f;
		half lerpFactor = (half)rcp(max(accumulatedFrames, 1.0f));

		// [Optimization 6 & 10]: Optimized neighborhood gathering and 16-bit half precision operations
		half4 prevY = (half4)srcPrevY[pxCoord];
		half2 prevCoCg = (half2)srcPrevCoCg[pxCoord];
		half4 prevBentVisibility = (half4)srcPrevBentVisibility[pxCoord];
		half prevAo = (half)srcPrevAo[pxCoord];
#	if defined(GI_SPECULAR) && !defined(HYBRID_REFLECTIONS)
		half4 prevGISpecular = (half4)srcPrevGISpecular[pxCoord];
#	endif

		// Keep history clipping active after convergence. Previously it stopped
		// once a few frames accumulated, allowing one accepted firefly to persist.
		// lerpFactor is rcp(max(accumulatedFrames, 1)), therefore always > 0.
		// Keep a scope for the neighbourhood temporaries without emitting a branch.
		{
			// Gather 2x2 texel quad for reduced instruction overhead
			int2 maxCoord = int2(OUT_FRAME_DIM) - 1;
			int2 pL = clamp(int2(pxCoord) + int2(-1, 0), 0, maxCoord);
			int2 pR = clamp(int2(pxCoord) + int2(1, 0), 0, maxCoord);
			int2 pU = clamp(int2(pxCoord) + int2(0, -1), 0, maxCoord);
			int2 pD = clamp(int2(pxCoord) + int2(0, 1), 0, maxCoord);

			half4 yL = (half4)srcPrevY[pL];
			half4 yR = (half4)srcPrevY[pR];
			half4 yU = (half4)srcPrevY[pU];
			half4 yD = (half4)srcPrevY[pD];
			half2 cL = (half2)srcPrevCoCg[pL];
			half2 cR = (half2)srcPrevCoCg[pR];
			half2 cU = (half2)srcPrevCoCg[pU];
			half2 cD = (half2)srcPrevCoCg[pD];
			// A single GatherRed supplies a compact 2x2 history envelope for AO.
			// This replaces four scalar texture reads while remaining conservative
			// enough to reject history leaking across a moving contact edge.
			half4 aoHistoryQuad = (half4)srcPrevAo.GatherRed(samplerPointClamp, uv * OUT_FRAME_SCALE);

			// Reject a current-frame spike against an outlier-resistant history
			// reference. Using the neighbourhood maximum allowed one already-poisoned
			// history texel to raise the limit for all four neighbours. Dropping the
			// brightest of the five samples prevents that feedback loop while the
			// 2.5x envelope still admits newly appearing area light.
			half historyMaxY = max(max(max(prevY.x, yL.x), max(yR.x, yU.x)), yD.x);
			half historyReferenceY = max((prevY.x + yL.x + yR.x + yU.x + yD.x - historyMaxY) * 0.25h, 0.0h);
			half currentLimitY = max((half)(RadianceFireflyClamp * 0.35f), historyReferenceY * 2.5h + 0.05h);
			if (accumulatedFrames > 3.0f && currY.x > currentLimitY) {
				half currentScale = currentLimitY / max(currY.x, 1e-3h);
				currY *= currentScale;
				currCoCg *= currentScale;
			}

			half4 nMinY = min(min(min(yL, yR), min(yU, yD)), currY);
			half4 nMaxY = max(max(max(yL, yR), max(yU, yD)), currY);
			half2 nMinCoCg = min(min(min(cL, cR), min(cU, cD)), currCoCg);
			half2 nMaxCoCg = max(max(max(cL, cR), max(cU, cD)), currCoCg);

			prevY = clamp(prevY, nMinY, nMaxY);
			prevCoCg = clamp(prevCoCg, nMinCoCg, nMaxCoCg);
			half aoMin = min(min(min(aoHistoryQuad.x, aoHistoryQuad.y), min(aoHistoryQuad.z, aoHistoryQuad.w)), (half)currAo);
			half aoMax = max(max(max(aoHistoryQuad.x, aoHistoryQuad.y), max(aoHistoryQuad.z, aoHistoryQuad.w)), (half)currAo);
			prevAo = clamp(prevAo, aoMin, aoMax);

#	if defined(GI_SPECULAR) && !defined(HYBRID_REFLECTIONS)
			// [Improvement] The diffuse Y/CoCg channels get neighborhood
			// (variance-window) clamping against their own screen-space
			// neighbors before the temporal blend, but the specular channel
			// never did - it went straight from history into the lerp below.
			// Specular GI is naturally spikier (it's a narrow BRDF lobe
			// sampled stochastically), so it's actually the channel most
			// likely to carry a temporally-persistent firefly. Apply the same
			// clamp here, now that visibilitySpecular (currGIAOSpecular.a) is
			// also correctly non-zero after the bitmask fix above, so this
			// clamp has meaningful data to work with.
			half4 specL = (half4)srcPrevGISpecular[pL];
			half4 specR = (half4)srcPrevGISpecular[pR];
			half4 specU = (half4)srcPrevGISpecular[pU];
			half4 specD = (half4)srcPrevGISpecular[pD];
			half4 nMinSpec = min(min(min(specL, specR), min(specU, specD)), currGIAOSpecular);
			half4 nMaxSpec = max(max(max(specL, specR), max(specU, specD)), currGIAOSpecular);
			prevGISpecular = clamp(prevGISpecular, nMinSpec, nMaxSpec);
#	endif
		}

		half luminanceDelta = abs(currY.x - prevY.x);
		half luminanceScale = max(max(abs(currY.x), abs(prevY.x)), 0.02h);
		half historyReactivity = saturate(luminanceDelta / luminanceScale);
		lerpFactor = max(lerpFactor, historyReactivity * 0.65h);

		// The horizon AO was already motion/depth/normal reprojected by
		// radianceDisocc, but historically that remapped value was never consumed.
		// Reusing it here removes low-sample shimmer at no extra pass or allocation.
		half aoDelta = abs((half)currAo - prevAo);
		half aoLerpFactor = max(lerpFactor, saturate(aoDelta * 0.35h));
		currAo = (float)lerp(prevAo, (half)currAo, aoLerpFactor);
		currY = lerp(prevY, currY, lerpFactor);
		currCoCg = lerp(prevCoCg, currCoCg, lerpFactor);
		if (prevBentVisibility.w > 0.0h) {
			float3 prevBentWS = GBuffer::DecodeNormal(prevBentVisibility.xy);
			float3 currBentWS = GBuffer::DecodeNormal(currBentVisibility.xy);

			// Radiance reactivity is not a reliable proxy for changing geometry.
			// A moving silhouette can alter the visibility cone without changing
			// luminance, leaving a stale bent direction in otherwise valid history.
			// Raise the blend rate only when the cone direction, aperture, or
			// observation confidence actually disagrees with the reprojected value.
			float directionDelta = 1.0f - saturate(dot(prevBentWS, currBentWS));
			float visibilityDelta = abs((float)currBentVisibility.z - (float)prevBentVisibility.z);
			float confidenceDelta = abs((float)currBentVisibility.w - (float)prevBentVisibility.w);
			float geometryReactivity = saturate(max(directionDelta * 1.5f,
				max(visibilityDelta * 2.0f, confidenceDelta * 2.0f)));
			float bentLerpFactor = max((float)lerpFactor, geometryReactivity * 0.55f);

			float3 blendedBentWS = lerp(prevBentWS, currBentWS, bentLerpFactor);
			float blendedBentLengthSq = dot(blendedBentWS, blendedBentWS);
			float3 stableBentWS = blendedBentLengthSq > 1e-6f ?
				blendedBentWS * rsqrt(blendedBentLengthSq) : currBentWS;
			currBentVisibility.xy = (half2)GBuffer::EncodeNormal(stableBentWS);
			currBentVisibility.zw = lerp(prevBentVisibility.zw, currBentVisibility.zw, (half)bentLerpFactor);
		}

#	if defined(GI_SPECULAR) && !defined(HYBRID_REFLECTIONS)
		currGIAOSpecular = lerp(prevGISpecular, currGIAOSpecular, lerpFactor);
#	endif
#endif
	}

	currY = filterNaN(currY);
	currCoCg = filterNaN(currCoCg);
	currGIAOSpecular = filterNaN(currGIAOSpecular);
	currBentVisibility = filterNaN(currBentVisibility);

	outAo[pxCoord] = currAo;
	outY[pxCoord] = (float4)currY;
	outCoCg[pxCoord] = (float2)currCoCg;
#if defined(GI_SPECULAR) && !defined(HYBRID_REFLECTIONS)
	outGISpecular[pxCoord] = (float4)currGIAOSpecular;
#endif
	outBentVisibility[pxCoord] = (float4)currBentVisibility;
}
