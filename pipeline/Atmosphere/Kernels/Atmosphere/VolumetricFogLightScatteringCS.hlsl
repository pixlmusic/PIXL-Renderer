SamplerState LinearSampler : register(s0);
SamplerComparisonState ShadowSampler : register(s1);
Texture3D<float4> VBufferA : register(t0);
Texture2DArray<float4> DirectionalShadowMap : register(t1);
Texture3D<float4> LightScatteringHistory : register(t2);
Texture2D<float2> ConservativeDepthTexture : register(t3);
Texture2D<float2> PrevConservativeDepthTexture : register(t4);
RWTexture3D<float4> LightScattering : register(u0);

#include "Common/Random.hlsli"
#include "Atmosphere/VolumetricFogCSCommon.hlsli"
#include "AmbientProbe/AmbientProbe.hlsli"
#if defined(TERRAIN_OCCLUSION)
#	include "TerrainOcclusion/TerrainOcclusion.hlsli"
#endif
#if defined(SKY_VEIL)
#	include "SkyVeil/SkyVeil.hlsli"
#endif
#if defined(RADIANT_GRID)
#	include "RadiantGrid/RadiantGrid.hlsli"
#	include "NaturalLighting/NaturalLighting.hlsli"
#endif
#define SKY_BOUNCE_PROBE_REGISTER t50
#include "SkyBounce/SkyBounce.hlsli"

struct DirectionalShadowLightData
{
	column_major float4x4 ShadowProj[2];
	column_major float4x4 InvShadowProj[2];
	float2 EndSplitDistances;
	float2 StartSplitDistances;
};

StructuredBuffer<DirectionalShadowLightData> DirectionalShadowLights : register(t98);

// 4D PCG hash matching UE's Rand4DPCG32 (jcgt.org/published/0009/03/02/)
uint4 Rand4DPCG32(int4 p)
{
	uint4 v = uint4(p);
	v = v * 1664525u + 1013904223u;
	v.x += v.y * v.w;
	v.y += v.z * v.x;
	v.z += v.x * v.y;
	v.w += v.y * v.z;
	v ^= (v >> 16u);
	v.x += v.y * v.w;
	v.y += v.z * v.x;
	v.z += v.x * v.y;
	v.w += v.y * v.z;
	return v;
}

// Matches UE's MakePositiveFinite - ensures no NaN/Inf propagates into history chain
float4 MakePositiveFinite(float4 v)
{
	v = max(v, 0.0f.xxxx);
	v.x = isfinite(v.x) ? v.x : 0.0f;
	v.y = isfinite(v.y) ? v.y : 0.0f;
	v.z = isfinite(v.z) ? v.z : 0.0f;
	v.w = isfinite(v.w) ? v.w : 0.0f;
	return v;
}

bool IsFroxelBehindSceneDepth(uint3 coord)
{
	float frontDepth = Atmosphere::ComputeVolumetricSliceDepth(max(float(coord.z) - 0.5f, 0.0f));
	float sceneDepth = ConservativeDepthTexture[coord.xy].x;
	return sceneDepth < frontDepth;
}

float3 ComputeHistoryVolumeUVAndDepth(float3 positionWS, out bool validHistory, out float previousViewDepth)
{
	float3 previousPositionWS = positionWS + FrameBuffer::CameraPosAdjust.xyz - FrameBuffer::CameraPreviousPosAdjust.xyz;
	float4 previousClip = mul(FrameBuffer::CameraPreviousViewProjUnjittered, float4(previousPositionWS, 1.0f));

	previousViewDepth = abs(previousClip.w);
	validHistory = previousClip.w > 0.0f;
	if (!validHistory)
		return 0.0f.xxx;

	float2 historyUV = previousClip.xy / previousClip.w * float2(0.5f, -0.5f) + 0.5f;

	float historyZ = Atmosphere::ComputeVolumetricNormalizedSlice(previousViewDepth);
	float3 volumeUV = float3(historyUV, historyZ);
	validHistory = !any(volumeUV < 0.0f) && !any(volumeUV >= 1.0f);
	return saturate(volumeUV);
}

float3 ComputeHistoryVolumeUV(float3 positionWS, out bool validHistory)
{
	float previousViewDepth;
	return ComputeHistoryVolumeUVAndDepth(positionWS, validHistory, previousViewDepth);
}

float2 FixupHistoryUV(float2 uv, float previousCellDepth, out bool validHistory)
{
	float2 size = float2(VolumetricFogGridSize.xy);
	float2 fullResUV = uv * size;
	float2 screenCoord = floor(fullResUV - 0.5f);
	float2 fullResOffset = fullResUV - screenCoord;
	float2 gatherUV = (screenCoord + 1.0f) / size;

	float4 previousSceneDepths = PrevConservativeDepthTexture.GatherRed(LinearSampler, gatherUV);
	bool4 validSamples = previousSceneDepths >= previousCellDepth;

	validHistory = true;
	if (all(validSamples))
		return uv;

	if (all(validSamples.wz))
		return (screenCoord + float2(fullResOffset.x, 0.5f)) / size;
	if (all(validSamples.xy))
		return (screenCoord + float2(fullResOffset.x, 1.5f)) / size;
	if (all(validSamples.wx))
		return (screenCoord + float2(0.5f, fullResOffset.y)) / size;
	if (all(validSamples.zy))
		return (screenCoord + float2(1.5f, fullResOffset.y)) / size;

	if (validSamples.x)
		return (screenCoord + float2(0.5f, 1.5f)) / size;
	if (validSamples.y)
		return (screenCoord + float2(1.5f, 1.5f)) / size;
	if (validSamples.w)
		return (screenCoord + float2(0.5f, 0.5f)) / size;
	if (validSamples.z)
		return (screenCoord + float2(1.5f, 0.5f)) / size;

	validHistory = false;
	return uv;
}

float SampleDirectionalShadowPCF(float3 positionLS, uint cascadeIndex)
{
	uint shadowWidth;
	uint shadowHeight;
	uint shadowSlices;
	DirectionalShadowMap.GetDimensions(shadowWidth, shadowHeight, shadowSlices);
	if (cascadeIndex >= shadowSlices)
		return 1.0f;

	float2 texelSize = rcp(float2(max(shadowWidth, 1), max(shadowHeight, 1)));
	float compareDepth = positionLS.z - SharedData::atmosphereSettings.volumetricShadowBias;

	float2 uvMin = texelSize * 1.5f;
	float2 uvMax = 1.0f.xx - uvMin;
	if (any(positionLS.xy < uvMin) || any(positionLS.xy > uvMax))
		return DirectionalShadowMap.SampleCmpLevelZero(ShadowSampler, float3(saturate(positionLS.xy), cascadeIndex), compareDepth).x;

	float center = DirectionalShadowMap.SampleCmpLevelZero(ShadowSampler, float3(positionLS.xy, cascadeIndex), compareDepth).x;
	float cross =
		DirectionalShadowMap.SampleCmpLevelZero(ShadowSampler, float3(positionLS.xy + float2(texelSize.x, 0.0f), cascadeIndex), compareDepth).x +
		DirectionalShadowMap.SampleCmpLevelZero(ShadowSampler, float3(positionLS.xy - float2(texelSize.x, 0.0f), cascadeIndex), compareDepth).x +
		DirectionalShadowMap.SampleCmpLevelZero(ShadowSampler, float3(positionLS.xy + float2(0.0f, texelSize.y), cascadeIndex), compareDepth).x +
		DirectionalShadowMap.SampleCmpLevelZero(ShadowSampler, float3(positionLS.xy - float2(0.0f, texelSize.y), cascadeIndex), compareDepth).x;

	return (center * 4.0f + cross) * rcp(8.0f);
}

float SampleDirectionalShadow(float3 positionWS)
{
	if (!SharedData::HasDirectionalShadows || SharedData::HideSky || SharedData::InMapMenu)
		return 1.0f;
	if (!VolumetricFogHasDirectionalShadowMap)
		return 1.0f;

	// Pull only the split data we need instead of copying the full structure
	// (including two unused inverse matrices) into registers for every froxel.
	float2 endSplitDistances = DirectionalShadowLights[0].EndSplitDistances;
	float2 startSplitDistances = DirectionalShadowLights[0].StartSplitDistances;
	float shadowMapDepth = SharedData::GetScreenDepth(FrameBuffer::GetShadowDepth(positionWS));
	if (shadowMapDepth >= endSplitDistances.y)
		return 1.0f;

	float splitDenom = max(endSplitDistances.x - startSplitDistances.y, 1e-4f);
	float cascadeSelect = smoothstep(0.0f, 1.0f, saturate((shadowMapDepth - startSplitDistances.y) / splitDenom));
	uint primaryCascade = (uint)cascadeSelect;

	float3 absolutePositionWS = positionWS + FrameBuffer::CameraPosAdjust.xyz;
	float3 positionLS = mul(DirectionalShadowLights[0].ShadowProj[primaryCascade], float4(absolutePositionWS, 1.0f)).xyz;
	if (any(positionLS.xy < 0.0f) || any(positionLS.xy > 1.0f))
		return 1.0f;

	float shadow = SampleDirectionalShadowPCF(positionLS, primaryCascade);

	[branch] if (cascadeSelect > 0.0f && cascadeSelect < 1.0f)
	{
		uint secondaryCascade = 1u - primaryCascade;
		float3 secondaryLS = mul(DirectionalShadowLights[0].ShadowProj[secondaryCascade], float4(absolutePositionWS, 1.0f)).xyz;
		if (!any(secondaryLS.xy < 0.0f) && !any(secondaryLS.xy > 1.0f)) {
			float secondaryShadow = SampleDirectionalShadowPCF(secondaryLS, secondaryCascade);
			shadow = lerp(shadow, secondaryShadow, cascadeSelect);
		}
	}

	float fade = saturate(shadowMapDepth / max(endSplitDistances.y, 1.0f));
	float fade2 = fade * fade;
	float fade4 = fade2 * fade2;
	float fade8 = fade4 * fade4;
	float fadeFactor = 1.0f - fade8 * fade8;
	return lerp(1.0f, shadow, fadeFactor);
}

float SampleDirectionalWorldShadow(float3 positionWS)
{
	if (SharedData::InInterior || !SharedData::HasDirectionalShadows || SharedData::HideSky || SharedData::InMapMenu)
		return 1.0f;

	float worldShadow = 1.0f;
#if defined(TERRAIN_OCCLUSION)
	worldShadow *= TerrainOcclusion::GetTerrainShadow(positionWS + FrameBuffer::CameraPosAdjust.xyz, LinearSampler);
#endif
#if defined(SKY_VEIL)
	worldShadow *= SkyVeil::GetCloudShadowMult(positionWS, LinearSampler);
#endif
	return worldShadow;
}

float3 ComputeSkyLightScattering(float3 positionWS, float3 viewDirection)
{
	float phaseG = SharedData::atmosphereSettings.volumetricFogScatteringDistribution;
	float3 skyDirection = abs(phaseG) > 0.001f ? normalize(-viewDirection * phaseG) : 0.0f.xxx;
	float3 skyVisibilityDirection = abs(phaseG) > 0.001f ? skyDirection : float3(0.0f, 0.0f, 1.0f);
	float skyVisibility = 1.0f;
	if (VolumetricFogHasSkyBounce && !SharedData::InInterior && !SharedData::HideSky) {
		float3 skylightingPosition = positionWS;
		sh2 skyBounceSH = SkyBounce::SampleNoBias(skylightingPosition);
		skyVisibility = SkyBounce::EvaluateDiffuse(skyBounceSH, skyVisibilityDirection, SkyBounce::GetFadeOutFactor(skylightingPosition));
	}

	float3 skyLighting =
		SharedData::atmosphereSettings.fogInscatteringColor.rgb *
		SharedData::atmosphereSettings.fogInscatteringColor.a *
		skyVisibility;
	[branch] if (VolumetricFogHasIBL)
		skyLighting = AmbientProbe::GetAmbientColorOccluded(skyDirection, skyVisibility);

	float skyIntensity = SharedData::atmosphereSettings.volumetricSkyLightingIntensity;
	if (Atmosphere::IsMapAtmosphereActive())
		skyIntensity *= max(SharedData::atmosphereSettings.mapAmbientInscatteringMultiplier, 0.0f);
	return skyLighting * skyIntensity;
}

#if defined(RADIANT_GRID)
float ComputeLocalLightAttenuation(float distanceSqr, float cellRadius, RadiantGrid::Light light)
{
	float distance = sqrt(max(distanceSqr, 1e-6f));

	// UE biases local light integration by froxel size to avoid singular bright voxels close to the light.
	if (light.lightFlags & RadiantGrid::LightFlags::InverseSquare) {
		distance = sqrt(max(distanceSqr, cellRadius * cellRadius));
	}

	return NaturalLighting::GetAttenuation(distance, light);
}

float3 AccumulateLocalLightScattering(
	uint3 coord,
	float3 cellOffset,
	float3 positionWS,
	float viewDepth,
	float3 viewDirection,
	float3 materialScattering)
{
	if (!VolumetricFogHasLocalLights)
		return 0.0f.xxx;

	float2 volumeUV = (float2(coord.xy) + cellOffset.xy) * VolumetricFogInvGridSize.xy;
	float2 screenUV = volumeUV;

	uint clusterIndex = 0;
	if (!RadiantGrid::GetClusterIndex(screenUV, viewDepth, clusterIndex))
		return 0.0f.xxx;

	RadiantGrid::LightGrid grid = RadiantGrid::lightGrid[clusterIndex];
	uint lightCount = min(grid.lightCount, (uint)MAX_CLUSTER_LIGHTS);

	float cornerViewDepth;
	float3 cellCornerWS = Atmosphere::ComputeCellWorldPosition(coord + uint3(1, 1, 1), cellOffset, cornerViewDepth);
	float cellRadius = max(length(cellCornerWS - positionWS), 1.0f);

	float phaseG = SharedData::atmosphereSettings.volumetricFogScatteringDistribution;
	float3 localScattering = 0.0f.xxx;
	[loop] for (uint lightIndex = 0; lightIndex < lightCount; lightIndex++)
	{
		uint clusteredLightIndex = RadiantGrid::lightList[grid.offset + lightIndex];
		RadiantGrid::Light light = RadiantGrid::lights[clusteredLightIndex];

		if (light.lightFlags & RadiantGrid::LightFlags::Disabled)
			continue;

		float3 toLight = light.positionWS.xyz - positionWS;
		float distanceSqr = dot(toLight, toLight);
		if (distanceSqr < 1e-6f)
			continue;

		float attenuation = ComputeLocalLightAttenuation(distanceSqr, cellRadius, light);
		if (attenuation < 1e-5f)
			continue;

		float3 L = toLight * rsqrt(distanceSqr);
		float phase = Atmosphere::PIXLPhaseFunction(dot(L, -viewDirection), phaseG);

		const bool isPointLightLinear = light.lightFlags & RadiantGrid::LightFlags::Linear;
		float3 lightColor = Color::PointLight(light.color.xyz, isPointLightLinear) * attenuation * light.fade;
		localScattering += lightColor * phase;
	}

	return localScattering *
	       SharedData::atmosphereSettings.volumetricLocalLightScatteringIntensity *
	       materialScattering;
}
#else
float3 AccumulateLocalLightScattering(
	uint3 coord,
	float3 cellOffset,
	float3 positionWS,
	float viewDepth,
	float3 viewDirection,
	float3 materialScattering)
{
	return 0.0f.xxx;
}
#endif

float4 ComputeLightScattering(uint3 coord, float3 cellOffset)
{
	float viewDepth;
	float3 positionWS = Atmosphere::ComputeCellWorldPosition(coord, cellOffset, viewDepth);

	float4 materialScatteringAndExtinction = VBufferA[coord];
	float extinction = materialScatteringAndExtinction.w;

	float3 viewDirection = normalize(positionWS);
	float phase = Atmosphere::PIXLPhaseFunction(
		dot(normalize(SharedData::DirLightDirection.xyz), viewDirection),
		SharedData::atmosphereSettings.volumetricFogScatteringDistribution);

	float3 directionalScattering = 0.0f.xxx;
	[branch] if (SharedData::HasDirectionalShadows && !SharedData::HideSky && !SharedData::InMapMenu)
	{
		float directionalShadow = SampleDirectionalShadow(positionWS) *
		                          SampleDirectionalWorldShadow(positionWS);
		float directionalMultiplier = SharedData::atmosphereSettings.volumetricDirectionalScatteringIntensity;
		if (Atmosphere::IsMapAtmosphereActive())
			directionalMultiplier *= max(SharedData::atmosphereSettings.mapDirectionalInscatteringMultiplier, 0.0f);
		directionalScattering =
			SharedData::DirLightColor.xyz *
			directionalMultiplier *
			directionalShadow *
			phase *
			materialScatteringAndExtinction.rgb;
	}

	float3 skyScattering = ComputeSkyLightScattering(positionWS, viewDirection) *
	                       materialScatteringAndExtinction.rgb;

	float3 localScattering = AccumulateLocalLightScattering(
		coord,
		cellOffset,
		positionWS,
		viewDepth,
		viewDirection,
		materialScatteringAndExtinction.rgb);

	float3 emissive = SharedData::atmosphereSettings.volumetricFogEmissive.rgb *
	                  SharedData::atmosphereSettings.volumetricFogEmissive.a *
	                  extinction;

	float3 multipleScatter = Atmosphere::PIXLMultipleScatteringCompensation(
		SharedData::atmosphereSettings.volumetricFogAlbedo.rgb);
	return float4(max((directionalScattering + skyScattering + localScattering) * multipleScatter + emissive, 0.0f.xxx), extinction);
}

[numthreads(8, 8, 4)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	if (!Atmosphere::IsInsideVolumetricGrid(dispatchID))
		return;

	float viewDepth;
	float3 centerPositionWS = Atmosphere::ComputeCellWorldPosition(dispatchID, 0.5f.xxx, viewDepth);
	if (VolumetricFogHasConservativeDepth && IsFroxelBehindSceneDepth(dispatchID)) {
		LightScattering[dispatchID] = 0.0f.xxxx;
		return;
	}

	bool validHistory;
	float3 historyUV = ComputeHistoryVolumeUV(centerPositionWS, validHistory);
	if (VolumetricFogHasPrevConservativeDepth && validHistory) {
		float frontDepth;
		float3 frontPositionWS = Atmosphere::ComputeCellWorldPosition(dispatchID, float3(0.5f, 0.5f, -0.5f), frontDepth);
		bool validFrontHistory;
		float previousFrontDepth;
		ComputeHistoryVolumeUVAndDepth(frontPositionWS, validFrontHistory, previousFrontDepth);
		if (validFrontHistory) {
			historyUV.xy = saturate(FixupHistoryUV(historyUV.xy, previousFrontDepth, validHistory));
		} else {
			validHistory = false;
		}
	}

	float historyAlpha = VolumetricFogHistoryWeight;
	[flatten] if (!validHistory || any(historyUV < 0.0f) || any(historyUV >= 1.0f))
	{
		historyAlpha = 0.0f;
	}

	uint sampleCount = historyAlpha < 0.001f ? VolumetricFogHistoryMissSampleCount : 1u;
	float4 scatteringAndExtinction = 0.0f.xxxx;
	[loop] for (uint sampleIndex = 0; sampleIndex < sampleCount; sampleIndex++)
	{
		// Per-voxel random noise matching UE's LightScatteringCS:
		// Rand4DPCG32(int4(GridCoordinate.xyz, StateFrameIndexMod8 + 8 * SampleIndex))
		// This decorrelates the jitter pattern across voxels, preventing coherent temporal artifacts
		uint3 Rand32Bits = Rand4DPCG32(int4(dispatchID.xyz, VolumetricFogStateFrameIndexMod8 + 8 * sampleIndex)).xyz;
		float3 Rand3D = (float3(Rand32Bits) / float(uint(0xffffffff))) * 2.0f - 1.0f;
		float3 cellOffset = VolumetricFogFrameJitterOffsets[sampleIndex].xyz + VolumetricFogSampleJitterMultiplier * Rand3D;

		scatteringAndExtinction += ComputeLightScattering(dispatchID, cellOffset);
	}
	scatteringAndExtinction *= rcp(float(sampleCount));

	[branch] if (historyAlpha > 0.0f)
	{
		float4 history = LightScatteringHistory.SampleLevel(LinearSampler, historyUV, 0);
		// Sanitize history to prevent NaN/Inf propagation in the temporal chain
		history = MakePositiveFinite(history);
	#if USE_PIXL_VOLUMETRIC_FOG
		// Extinction changes are a reliable disocclusion signal in a froxel
		// volume. Reject mismatched history and clamp radiance to a responsive
		// envelope to prevent bright/dark shafts smearing across camera motion.
		float extinctionScale = max(max(history.w, scatteringAndExtinction.w), 1e-5f);
		float extinctionDelta = abs(history.w - scatteringAndExtinction.w) / extinctionScale;
		historyAlpha *= exp2(-8.0f * extinctionDelta);

		if (VolumetricFogHasPrevConservativeDepth) {
			float2 currentDepthRange = ConservativeDepthTexture[dispatchID.xy];
			int2 previousDepthCoord = clamp(
				int2(historyUV.xy * float2(VolumetricFogGridSize.xy)),
				int2(0, 0),
				int2(VolumetricFogGridSize.xy) - 1);
			float2 previousDepthRange = PrevConservativeDepthTexture.Load(int3(previousDepthCoord, 0));
			float currentMax = max(currentDepthRange.x, currentDepthRange.y);
			float currentMin = min(currentDepthRange.x, currentDepthRange.y);
			float previousMax = max(previousDepthRange.x, previousDepthRange.y);
			float previousMin = min(previousDepthRange.x, previousDepthRange.y);
			float intervalGap = max(max(currentMin - previousMax, previousMin - currentMax), 0.0f);
			float endpointDelta = max(abs(currentMin - previousMin), abs(currentMax - previousMax));
			float depthScale = max(viewDepth, 256.0f);
			float depthMismatch = (intervalGap + 0.25f * endpointDelta) / depthScale;
			historyAlpha *= exp2(-max(SharedData::atmosphereSettings.volumetricHistoryDepthRejection, 0.0f) * depthMismatch);
		}

		float radianceClamp = max(SharedData::atmosphereSettings.volumetricHistoryRadianceClamp, 1.0f);
		float3 radianceFloor = max(scatteringAndExtinction.rgb / radianceClamp - 0.002f.xxx, 0.0f.xxx);
		float3 radianceCeiling = scatteringAndExtinction.rgb * radianceClamp + 0.002f.xxx;
		history.rgb = clamp(history.rgb, radianceFloor, radianceCeiling);
	#endif
		scatteringAndExtinction = lerp(scatteringAndExtinction, history, historyAlpha);
	}

	LightScattering[dispatchID] = MakePositiveFinite(scatteringAndExtinction);
}
