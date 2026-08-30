#ifndef __ATMOSPHERE_VOLUMETRIC_COMMON_HLSLI__
#define __ATMOSPHERE_VOLUMETRIC_COMMON_HLSLI__

#include "Common/Math.hlsli"
#include "Common/SharedData.hlsli"

namespace Atmosphere
{
	#ifndef USE_PIXL_VOLUMETRIC_FOG
	#	define USE_PIXL_VOLUMETRIC_FOG 1
	#endif

	float3 SafeNormalize(float3 value, float3 fallback)
	{
		float lengthSq = dot(value, value);
		return lengthSq > 1.0e-8f ? value * rsqrt(lengthSq) : fallback;
	}

	float HenyeyGreenstein(float cosTheta, float g)
	{
		g = clamp(g, -0.99f, 0.99f);
		cosTheta = clamp(cosTheta, -1.0f, 1.0f);
		float g2 = g * g;
		float denom = 1.0f + g2 - 2.0f * g * cosTheta;
		return (1.0f - g2) / (4.0f * Math::PI * pow(max(denom, 1e-5f), 1.5f));
	}

	float PIXLPhaseFunction(float cosTheta, float g)
	{
#if USE_PIXL_VOLUMETRIC_FOG
		// A narrow forward lobe carries shafts while a weak reverse lobe keeps
		// fog readable when looking away from the primary light. The normalized
		// blend remains energy conserving and is stable for the full UI range.
		g = clamp(g, -0.9f, 0.9f);
		float forward = HenyeyGreenstein(cosTheta, g);
		float backward = HenyeyGreenstein(cosTheta, -0.25f * g);
		return lerp(backward, forward, 0.82f);
#else
		return HenyeyGreenstein(cosTheta, g);
#endif
	}

	float3 PIXLMultipleScatteringCompensation(float3 scatteringAlbedo)
	{
#if USE_PIXL_VOLUMETRIC_FOG
		// Bounded analytic compensation for light that would otherwise be lost
		// after the first scattering event. This is deliberately conservative so
		// dense weather never becomes self-emissive.
		return rcp(max(1.0f.xxx - saturate(scatteringAlbedo) * 0.18f, 0.72f.xxx));
#else
		return 1.0f.xxx;
#endif
	}

	bool IsMapAtmosphereActive()
	{
		return SharedData::InMapMenu && SharedData::atmosphereSettings.mapAtmosphereEnabled != 0;
	}

	float GetHeightFogFalloff()
	{
		float falloff = SharedData::atmosphereSettings.fogHeightFalloff * 0.001f;
		if (IsMapAtmosphereActive())
			falloff *= max(SharedData::atmosphereSettings.mapFogHeightFalloffMultiplier, 0.0f);
		return max(falloff, 1e-7f);
	}

	float GetHeightFogDensity()
	{
		float density = SharedData::atmosphereSettings.fogDensity * 0.001f;
		if (IsMapAtmosphereActive()) {
			// Interior MapMenu views are local maps rather than long-range aerial
			// perspective. Keep them clean instead of applying an outdoor height field.
			if (SharedData::InInterior)
				return 0.0f;
			density *= max(SharedData::atmosphereSettings.mapFogDensityMultiplier, 0.0f);
		}
		return max(density, 0.0f);
	}

	float GetAnalyticalFogStartDistance()
	{
		if (IsMapAtmosphereActive())
			return max(SharedData::atmosphereSettings.startDistance, SharedData::atmosphereSettings.mapStartDistance);
		return SharedData::atmosphereSettings.startDistance;
	}

	float GetMinimumTransmittance()
	{
		return IsMapAtmosphereActive() ? saturate(SharedData::atmosphereSettings.mapMinimumTransmittance) : 0.0f;
	}

	float GetVolumetricStartDistance()
	{
		return max(0.0f, SharedData::atmosphereSettings.volumetricFogStartDistance);
	}

	float GetVolumetricEndDistance()
	{
		return max(GetVolumetricStartDistance() + 1.0f, SharedData::atmosphereSettings.volumetricFogDistance);
	}

	float GetVolumetricGridSizeZ()
	{
#if defined(ATMOSPHERE_PIPELINE_GRID_SIZE_Z)
		return clamp(float(ATMOSPHERE_PIPELINE_GRID_SIZE_Z), 16.0f, 160.0f);
#else
		return clamp(float(SharedData::atmosphereSettings.volumetricGridSizeZ), 16.0f, 160.0f);
#endif
	}

	float GetVolumetricDepthDistributionScale()
	{
		return max(SharedData::atmosphereSettings.volumetricDepthDistributionScale, GetVolumetricGridSizeZ() / 120.0f);
	}

	float3 GetVolumetricGridZParams(float gridSizeZ)
	{
#if defined(ATMOSPHERE_PIPELINE_GRID_Z_PARAMS)
		return ATMOSPHERE_PIPELINE_GRID_Z_PARAMS;
#else
		gridSizeZ = clamp(gridSizeZ, 16.0f, 160.0f);
		float nearPlane = max(SharedData::CameraData.y, GetVolumetricStartDistance());
		float farPlane = max(nearPlane + 1.0f, GetVolumetricEndDistance());
		float nearWithOffset = nearPlane + 0.095f * 100.0f;
		float farExp = exp2(min(gridSizeZ / GetVolumetricDepthDistributionScale(), 120.0f));
		float depthDenominator = farPlane - nearWithOffset;
		float safeDepthDenominator =
			abs(depthDenominator) >= 1e-6f
				? depthDenominator
				: (depthDenominator < 0.0f ? -1e-6f : 1e-6f);
		float gridZOffset = (farPlane - nearWithOffset * farExp) / safeDepthDenominator;
		float gridZScale = (1.0f - gridZOffset) / max(nearWithOffset, 1e-4f);
		return float3(gridZScale, gridZOffset, GetVolumetricDepthDistributionScale());
#endif
	}

	float3 GetVolumetricGridZParams()
	{
		return GetVolumetricGridZParams(GetVolumetricGridSizeZ());
	}

	float ComputeVolumetricSliceDepth(float slice)
	{
		float3 gridZParams = GetVolumetricGridZParams();
		float sliceExp = exp2(min(slice / max(gridZParams.z, 1e-4f), 120.0f));
		return (sliceExp - gridZParams.y) / max(gridZParams.x, 1e-20f);
	}

	float ComputeVolumetricNormalizedSlice(float viewDepth, float gridSizeZ)
	{
		gridSizeZ = clamp(gridSizeZ, 16.0f, 160.0f);
		float3 gridZParams = GetVolumetricGridZParams(gridSizeZ);
		return log2(max(viewDepth * gridZParams.x + gridZParams.y, 1e-6f)) * gridZParams.z / gridSizeZ;
	}

	float ComputeVolumetricNormalizedSlice(float viewDepth)
	{
		return ComputeVolumetricNormalizedSlice(viewDepth, GetVolumetricGridSizeZ());
	}

	float EvaluateHeightFogExtinction(float3 positionWS, float3 cameraWS)
	{
		float fogDensity = GetHeightFogDensity();
		float fogHeightFalloff = GetHeightFogFalloff();
		float worldHeight = positionWS.z + cameraWS.z;
		float exponent = fogHeightFalloff * max(worldHeight - SharedData::atmosphereSettings.fogHeight, 0.0f);
		float localDensity = fogDensity * exp2(-exponent);
		return max(localDensity * SharedData::atmosphereSettings.volumetricFogExtinctionScale * 0.5f, 0.0f);
	}
}

#endif
