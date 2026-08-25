#ifndef __ATMOSPHERE_HLSLI__
#define __ATMOSPHERE_HLSLI__

#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"
#include "Atmosphere/VolumetricFogCommon.hlsli"

#if defined(WORLD_PROBES)
#	include "WorldProbes/WorldProbes.hlsli"
#endif

Texture3D<float4> ExponentialHeightFogIntegratedLightScattering : register(t19);
// x = maximum scene depth covered by the froxel XY footprint, y = minimum.
// The range allows the final lookup to reject cross-edge interpolation without
// requiring a full-resolution volumetric buffer.
Texture2D<float2> ExponentialHeightFogDepthRange : register(t99);

namespace Atmosphere
{
	float GetVanillaFogFade(float vanillaFogFade)
	{
		return SharedData::atmosphereSettings.respectVanillaFogFade != 0 ? vanillaFogFade : 1.0f;
	}

	bool ShouldDisableVanillaFog()
	{
		if (IsMapAtmosphereActive() && SharedData::atmosphereSettings.mapDisableVanillaFog != 0)
			return true;
		return SharedData::atmosphereSettings.enabled && SharedData::atmosphereSettings.disableVanillaFog != 0;
	}

	bool ShouldApplyVolumetricFog()
	{
		if (IsMapAtmosphereActive() && SharedData::atmosphereSettings.mapDisableVolumetricFog != 0)
			return false;
		return SharedData::atmosphereSettings.enabled != 0 &&
		       SharedData::atmosphereSettings.volumetricFogEnabled != 0 &&
		       SharedData::atmosphereSettings.fogDensity > 0.0f &&
		       SharedData::atmosphereSettings.volumetricFogExtinctionScale > 0.0f &&
		       SharedData::atmosphereSettings.volumetricFogDistance > SharedData::atmosphereSettings.volumetricFogStartDistance + 1.0f;
	}

	float GetSceneDepthFromClip(float4 clipPosition)
	{
		return max(clipPosition.w, SharedData::CameraData.y);
	}

	float GetSceneDepthForFog(float3 positionWS, out float2 volumeUV, out float projectedDepth)
	{
		float4 clipPosition = mul(FrameBuffer::CameraViewProj, float4(positionWS, 1.0f));
		[branch] if (clipPosition.w <= 0.0f)
		{
			volumeUV = 0.0f.xx;
			projectedDepth = 0.0f;
			return 0.0f;
		}

		projectedDepth = GetSceneDepthFromClip(clipPosition);
		volumeUV = clipPosition.xy / clipPosition.w * float2(0.5f, -0.5f) + 0.5f;

		volumeUV = saturate(volumeUV);
		return projectedDepth;
	}

	float GetDepthRangeWeight(float sceneDepth, float2 depthRange)
	{
		float maxDepth = max(depthRange.x, depthRange.y);
		float minDepth = min(depthRange.x, depthRange.y);
		if (maxDepth <= 0.0f)
			return 1.0f;

		float depthGap = max(max(minDepth - sceneDepth, sceneDepth - maxDepth), 0.0f);
		float depthScale = max(sceneDepth, max(SharedData::CameraData.y * 4.0f, 256.0f));
		float relativeGap = depthGap / depthScale;
		return exp2(-max(SharedData::atmosphereSettings.volumetricDepthAwareUpsamplingStrength, 0.0f) * relativeGap);
	}

	void AccumulateDepthAwareVolumeSample(
		int2 coord,
		float spatialWeight,
		float volumeZ,
		float sceneDepth,
		uint volumeWidth,
		uint volumeHeight,
		uint volumeDepth,
		inout float4 accumulatedFog,
		inout float accumulatedWeight)
	{
		coord = clamp(coord, int2(0, 0), int2(int(volumeWidth) - 1, int(volumeHeight) - 1));
		float2 sampleUV = (float2(coord) + 0.5f) / float2(volumeWidth, volumeHeight);
		float2 depthRange = ExponentialHeightFogDepthRange.Load(int3(coord, 0));
		float weight = spatialWeight * GetDepthRangeWeight(sceneDepth, depthRange);
		if (weight <= 1e-5f)
			return;

		float zMin = 0.5f / float(volumeDepth);
		float zMax = 1.0f - zMin;
		float4 fog = ExponentialHeightFogIntegratedLightScattering.SampleLevel(
			SampColorSampler,
			float3(sampleUV, clamp(volumeZ, zMin, zMax)),
			0);
		accumulatedFog += fog * weight;
		accumulatedWeight += weight;
	}

	float4 SampleIntegratedVolumetricFog(float2 volumeUV, float volumeZ, float sceneDepth, uint volumeWidth, uint volumeHeight, uint volumeDepth)
	{
		float3 volumeTexelCenter = 0.5f / float3(volumeWidth, volumeHeight, volumeDepth);
		float2 volumeUVMin = volumeTexelCenter.xy;
		float2 volumeUVMax = 1.0f.xx - volumeTexelCenter.xy;
		volumeUV = clamp(volumeUV, volumeUVMin, volumeUVMax);
		volumeZ = clamp(volumeZ, volumeTexelCenter.z, 1.0f - volumeTexelCenter.z);

		float4 linearSample = ExponentialHeightFogIntegratedLightScattering.SampleLevel(
			SampColorSampler, float3(volumeUV, volumeZ), 0);

		if (SharedData::atmosphereSettings.volumetricDepthAwareUpsampling == 0)
			return linearSample;

		uint depthWidth;
		uint depthHeight;
		ExponentialHeightFogDepthRange.GetDimensions(depthWidth, depthHeight);
		if (depthWidth != volumeWidth || depthHeight != volumeHeight || depthWidth == 0 || depthHeight == 0)
			return linearSample;

		// In smooth depth regions regular trilinear filtering is both cheaper and
		// slightly smoother. Only invoke the bilateral 2x2 resolve near geometry
		// discontinuities or when the current pixel lies outside the nearest
		// froxel's scene-depth envelope.
		int2 nearestCoord = clamp(
			int2(volumeUV * float2(volumeWidth, volumeHeight)),
			int2(0, 0),
			int2(int(volumeWidth) - 1, int(volumeHeight) - 1));
		float2 nearestRange = ExponentialHeightFogDepthRange.Load(int3(nearestCoord, 0));
		float nearestMax = max(nearestRange.x, nearestRange.y);
		float nearestMin = min(nearestRange.x, nearestRange.y);
		float depthScale = max(sceneDepth, max(SharedData::CameraData.y * 4.0f, 256.0f));
		float rangeSpan = max(nearestMax - nearestMin, 0.0f) / depthScale;
		float outsideRange = max(max(nearestMin - sceneDepth, sceneDepth - nearestMax), 0.0f) / depthScale;
		if (rangeSpan < 0.02f && outsideRange < 0.01f)
			return linearSample;

		float2 texelPosition = volumeUV * float2(volumeWidth, volumeHeight) - 0.5f;
		int2 baseCoord = int2(floor(texelPosition));
		float2 f = frac(texelPosition);

		float4 accumulatedFog = 0.0f.xxxx;
		float accumulatedWeight = 0.0f;
		AccumulateDepthAwareVolumeSample(baseCoord + int2(0, 0), (1.0f - f.x) * (1.0f - f.y), volumeZ, sceneDepth, volumeWidth, volumeHeight, volumeDepth, accumulatedFog, accumulatedWeight);
		AccumulateDepthAwareVolumeSample(baseCoord + int2(1, 0), f.x * (1.0f - f.y), volumeZ, sceneDepth, volumeWidth, volumeHeight, volumeDepth, accumulatedFog, accumulatedWeight);
		AccumulateDepthAwareVolumeSample(baseCoord + int2(0, 1), (1.0f - f.x) * f.y, volumeZ, sceneDepth, volumeWidth, volumeHeight, volumeDepth, accumulatedFog, accumulatedWeight);
		AccumulateDepthAwareVolumeSample(baseCoord + int2(1, 1), f.x * f.y, volumeZ, sceneDepth, volumeWidth, volumeHeight, volumeDepth, accumulatedFog, accumulatedWeight);

		return accumulatedWeight > 1e-5f ? accumulatedFog / accumulatedWeight : linearSample;
	}

	float2 GetLegacyUpsampleJitter(float2 screenPixel, float2 volumeSize)
	{
		// Depth-aware reconstruction and independent final screen-space jitter are
		// mutually exclusive. This also protects older presets that explicitly stored
		// the legacy value of 1.0 before Atmosphere 2.0 existed.
		if (SharedData::atmosphereSettings.volumetricDepthAwareUpsampling != 0)
			return 0.0f.xx;

		float multiplier = SharedData::atmosphereSettings.volumetricUpsampleJitterMultiplier;
		if (multiplier <= 0.0f)
			return 0.0f.xx;

		// Retained only as an optional compatibility control. Atmosphere 2.0's
		// default is zero; depth-aware reconstruction replaces this independent
		// screen-space temporal pattern for DLSS/FSR/Frame Generation stability.
		float2 noise = float2(
			Random::InterleavedGradientNoise(screenPixel, SharedData::FrameCount),
			Random::InterleavedGradientNoise(screenPixel.yx + 19.19f, SharedData::FrameCount));
		return (noise * 2.0f - 1.0f) * multiplier / volumeSize;
	}

	float4 SampleVolumetricFog(float3 positionWS)
	{
		if (!ShouldApplyVolumetricFog())
			return float4(0.0f, 0.0f, 0.0f, 1.0f);

		uint volumeWidth;
		uint volumeHeight;
		uint volumeDepth;
		ExponentialHeightFogIntegratedLightScattering.GetDimensions(volumeWidth, volumeHeight, volumeDepth);
		if (volumeWidth == 0 || volumeHeight == 0 || volumeDepth == 0)
			return float4(0.0f, 0.0f, 0.0f, 1.0f);

		float2 volumeUV = 0.0f.xx;
		float projectedDepth = 0.0f;
		float sceneDepth = GetSceneDepthForFog(positionWS, volumeUV, projectedDepth);
		if (projectedDepth <= 0.0f)
			return float4(0.0f, 0.0f, 0.0f, 1.0f);

		float volumeZ = saturate(ComputeVolumetricNormalizedSlice(sceneDepth, float(volumeDepth)));
		float4 volumetricFog = SampleIntegratedVolumetricFog(volumeUV, volumeZ, sceneDepth, volumeWidth, volumeHeight, volumeDepth);
		return lerp(float4(0.0f, 0.0f, 0.0f, 1.0f), volumetricFog, saturate((sceneDepth - GetVolumetricStartDistance()) * 100000000.0f));
	}

	float4 SampleVolumetricFog(float4 screenPosition)
	{
		if (!ShouldApplyVolumetricFog())
			return float4(0.0f, 0.0f, 0.0f, 1.0f);

		uint volumeWidth;
		uint volumeHeight;
		uint volumeDepth;
		ExponentialHeightFogIntegratedLightScattering.GetDimensions(volumeWidth, volumeHeight, volumeDepth);
		if (volumeWidth == 0 || volumeHeight == 0 || volumeDepth == 0)
			return float4(0.0f, 0.0f, 0.0f, 1.0f);

		float sceneDepth = SharedData::GetScreenDepth(screenPosition.z);
		float volumeZ = saturate(ComputeVolumetricNormalizedSlice(sceneDepth, float(volumeDepth)));

		float2 volumeSize = float2(volumeWidth, volumeHeight);
		// Lighting passes full-display pixel coordinates here by undoing dynamic
		// resolution. This UV is therefore stable across DLSS/FSR quality modes.
		float2 volumeUV = screenPosition.xy * SharedData::BufferDim.zw;
		volumeUV += GetLegacyUpsampleJitter(screenPosition.xy, volumeSize);

		float4 volumetricFog = SampleIntegratedVolumetricFog(volumeUV, volumeZ, sceneDepth, volumeWidth, volumeHeight, volumeDepth);
		return lerp(float4(0.0f, 0.0f, 0.0f, 1.0f), volumetricFog, saturate((sceneDepth - GetVolumetricStartDistance()) * 100000000.0f));
	}

	float4 CombineVolumetricFog(float4 analyticalFog, float3 positionWS)
	{
		float4 volumetricFog = SampleVolumetricFog(positionWS);
		float analyticalTransmittance = 1.0f - analyticalFog.w;
		float combinedTransmittance = volumetricFog.a * analyticalTransmittance;
		float combinedOpacity = saturate(1.0f - combinedTransmittance);
		float3 analyticalPremultiplied = analyticalFog.rgb * analyticalFog.w;
		float3 combinedPremultiplied = volumetricFog.rgb + volumetricFog.a * analyticalPremultiplied;
		return float4(combinedOpacity > 1e-4f ? combinedPremultiplied / combinedOpacity : float3(0.0f, 0.0f, 0.0f), combinedOpacity);
	}

	float4 CombineVolumetricFog(float4 analyticalFog, float4 screenPosition)
	{
		float4 volumetricFog = SampleVolumetricFog(screenPosition);
		float analyticalTransmittance = 1.0f - analyticalFog.w;
		float combinedTransmittance = volumetricFog.a * analyticalTransmittance;
		float combinedOpacity = saturate(1.0f - combinedTransmittance);
		float3 analyticalPremultiplied = analyticalFog.rgb * analyticalFog.w;
		float3 combinedPremultiplied = volumetricFog.rgb + volumetricFog.a * analyticalPremultiplied;
		return float4(combinedOpacity > 1e-4f ? combinedPremultiplied / combinedOpacity : float3(0.0f, 0.0f, 0.0f), combinedOpacity);
	}

	float4 GetAtmosphereInternal(float3 positionWS, float3 cameraWS, float3 fogColor, bool useScreenPosition, float4 screenPosition, bool applyVolumetricFog)
	{
		float fogHeightFalloff = GetHeightFogFalloff();
		float fogDensity = GetHeightFogDensity();
		if (fogDensity <= 0.0f) {
			return 0.0f;
		}
		float3 viewToPos = positionWS;
		float2 volumeUV = 0.0f.xx;
		float projectedDepth = 0.0f;
		float sceneDepth = GetSceneDepthForFog(positionWS, volumeUV, projectedDepth);
		[branch] if (projectedDepth > 1e-4f && sceneDepth > projectedDepth)
		{
			viewToPos *= sceneDepth / projectedDepth;
		}

		float viewToPosLength = length(viewToPos);
		float viewToPosLengthInv = rcp(max(viewToPosLength, 1e-4f));

		float rayOriginTerms = fogDensity * exp2(-fogHeightFalloff * max(cameraWS.z - SharedData::atmosphereSettings.fogHeight, 0));
		float rayLength = viewToPosLength;
		float rayDirectionZ = viewToPos.z;

		float excludeDistance = GetAnalyticalFogStartDistance();
		if (applyVolumetricFog && ShouldApplyVolumetricFog()) {
			float cosAngle = sceneDepth * viewToPosLengthInv;
			float invCosAngle = cosAngle > 0.001f ? rcp(cosAngle) : 0.0f;
			excludeDistance = max(excludeDistance, GetVolumetricEndDistance() * invCosAngle);
		}

		if (excludeDistance > 0) {
			excludeDistance = min(excludeDistance, viewToPosLength);
			float excludeIntersectionTime = excludeDistance * viewToPosLengthInv;
			float cameraToExclusionIntersectionZ = excludeIntersectionTime * viewToPos.z;
			float exclusionIntersectionZ = cameraWS.z + cameraToExclusionIntersectionZ;
			rayLength = (1.0f - excludeIntersectionTime) * viewToPosLength;
			rayDirectionZ = viewToPos.z - cameraToExclusionIntersectionZ;
			float exponent = fogHeightFalloff * max(exclusionIntersectionZ - SharedData::atmosphereSettings.fogHeight, 0);
			rayOriginTerms = fogDensity * exp2(-exponent);
		}

		float falloff = fogHeightFalloff * rayDirectionZ;
		// Stable analytic integral across Skyrim's extreme vertical/map rays. For a
		// steep upward ray, use the exact asymptotic 1/falloff rather than clamping
		// the geometry. A steep downward ray is already optically opaque, so saturate
		// optical depth before exp2 can overflow.
		float exponentialHeightLineIntegral = 0.0f;
		if (falloff < -80.0f) {
			exponentialHeightLineIntegral = 80.0f;
		} else {
			float lineIntegral = 0.0f;
			if (falloff > 80.0f) {
				lineIntegral = rcp(max(falloff, 1e-6f));
			} else if (abs(falloff) > 0.01f) {
				lineIntegral = (1.0f - exp2(-falloff)) / falloff;
			} else {
				lineIntegral = 0.69314718056f - 0.24022650695f * falloff;  // log(2) - 0.5*log(2)^2*x
			}
			exponentialHeightLineIntegral = max(rayOriginTerms * lineIntegral * rayLength, 0.0f);
		}

		float expFogFactor = saturate(exp2(-min(exponentialHeightLineIntegral, 80.0f)));
		expFogFactor = max(expFogFactor, GetMinimumTransmittance());

		float ambientInscatteringMultiplier = IsMapAtmosphereActive() ?
			max(SharedData::atmosphereSettings.mapAmbientInscatteringMultiplier, 0.0f) : 1.0f;
		float3 fogInscatteringColor = fogColor * SharedData::atmosphereSettings.originalFogColorAmount;
		fogInscatteringColor += SharedData::atmosphereSettings.fogInscatteringColor.rgb * SharedData::atmosphereSettings.fogInscatteringColor.a;
		fogInscatteringColor *= ambientInscatteringMultiplier;

#if defined(WORLD_PROBES)
		// Interior cells and HideSky worldspaces should not pull outdoor cubemap
		// radiance into fog. Their incoming fogColor/ambient probe remains intact.
		if (SharedData::atmosphereSettings.useWorldProbes > 0 && !SharedData::InInterior && !SharedData::HideSky) {
			float3 cubemapColor = WorldProbes::EnvReflectionsTexture.SampleLevel(SampColorSampler, normalize(lerp(positionWS, float3(0, 0, 1), saturate((SharedData::atmosphereSettings.cubemapMipLevel + 1) / 8))), SharedData::atmosphereSettings.cubemapMipLevel).xyz;
			float worldProbeMultiplier = IsMapAtmosphereActive() ? max(SharedData::atmosphereSettings.mapWorldProbeMultiplier, 0.0f) : 1.0f;
			fogInscatteringColor += cubemapColor * SharedData::atmosphereSettings.inscatteringTint.rgb * SharedData::atmosphereSettings.inscatteringTint.a * worldProbeMultiplier;
		}
#endif

		fogColor = fogInscatteringColor * (1.0f - expFogFactor);

		float3 directionalInscattering = 0;

		float3 viewDirection = viewToPos * viewToPosLengthInv;

		// Directional atmosphere is allowed only when the renderer reports a
		// valid directional-shadow path. This preserves real daylight interiors while
		// preventing fake unshadowed sun shafts in ordinary interiors/HideSky spaces.
		if (SharedData::atmosphereSettings.directionalInscatteringMultiplier > 0 && SharedData::HasDirectionalShadows && !SharedData::HideSky) {
			float3 lightDirection = normalize(SharedData::DirLightDirection.xyz);
			float cosTheta = dot(lightDirection, viewDirection);
			float phase = PIXLPhaseFunction(cosTheta, SharedData::atmosphereSettings.directionalInscatteringAnisotropy);
			float3 directionalLightInscattering = SharedData::DirLightColor.xyz * phase;
			float mapDirectionalMultiplier = IsMapAtmosphereActive() ? max(SharedData::atmosphereSettings.mapDirectionalInscatteringMultiplier, 0.0f) : 1.0f;
			directionalInscattering = directionalLightInscattering * (1.0f - expFogFactor) * SharedData::atmosphereSettings.directionalInscatteringMultiplier * mapDirectionalMultiplier;
		}

		fogColor += directionalInscattering;
		float4 analyticalFog = float4(fogColor, 1.0f - expFogFactor);
		if (!applyVolumetricFog) {
			return analyticalFog;
		}
		return useScreenPosition ? CombineVolumetricFog(analyticalFog, screenPosition) : CombineVolumetricFog(analyticalFog, positionWS);
	}

	float4 GetAtmosphere(float3 positionWS, float3 cameraWS, float3 fogColor)
	{
		return GetAtmosphereInternal(positionWS, cameraWS, fogColor, false, 0.0f.xxxx, true);
	}

	float4 GetAtmosphere(float3 positionWS, float3 cameraWS, float3 fogColor, float4 screenPosition)
	{
		return GetAtmosphereInternal(positionWS, cameraWS, fogColor, true, screenPosition, true);
	}

	float4 GetAtmosphereWithoutVolumes(float3 positionWS, float3 cameraWS, float3 fogColor)
	{
		return GetAtmosphereInternal(positionWS, cameraWS, fogColor, false, 0.0f.xxxx, false);
	}

	float4 GetAtmosphereWithoutVolumes(float3 positionWS, float3 cameraWS, float3 fogColor, float4 screenPosition)
	{
		return GetAtmosphereInternal(positionWS, cameraWS, fogColor, true, screenPosition, false);
	}

	float GetSunlightFogAttenuation(float3 positionWS, float3 cameraWS)
	{
		if (!SharedData::HasDirectionalShadows || SharedData::HideSky)
			return 1.0f;

		float fogHeightFalloff = GetHeightFogFalloff();
		float fogDensity = GetHeightFogDensity();
		if (fogDensity <= 0.0f) {
			return 1.0f;
		}

		float exponent = fogHeightFalloff * max(positionWS.z + cameraWS.z - SharedData::atmosphereSettings.fogHeight, 0.0f);
		float localDensity = fogDensity * exp2(-exponent);

		float3 lightDir = SharedData::DirLightDirection.xyz;
		float lightDirZ = lightDir.z;

		float sunlightFogAttenuation = 0.0f;

		// Integral = Density * (1 - exp2(-slope * inf)) / slope
		if (lightDirZ > 0.001f) {
			float slope = max(fogHeightFalloff * lightDirZ, 1e-8f);
			float exponentialHeightLineIntegral = localDensity / slope;
			sunlightFogAttenuation = saturate(exp2(-exponentialHeightLineIntegral));
		}

		float attenuationAmount = SharedData::atmosphereSettings.sunlightAttenuationAmount;
		if (IsMapAtmosphereActive())
			attenuationAmount *= saturate(SharedData::atmosphereSettings.mapSunlightAttenuationMultiplier);
		return lerp(1.0f, sunlightFogAttenuation, attenuationAmount);
	}
}
#endif
