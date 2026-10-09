Texture3D<float4> LightScattering : register(t0);
RWTexture3D<float4> IntegratedLightScattering : register(u0);

#include "Atmosphere/VolumetricFogCSCommon.hlsli"

// Integral of exp(-extinction*x) over one homogeneous segment. Express the
// small-optical-depth limit as a series so neither a broad extinction floor
// nor subtraction of nearly equal floats suppresses thin fog scattering.
float HomogeneousScatteringWeight(float extinction, float distance)
{
	extinction = max(extinction, 0.0f);
	distance = max(distance, 0.0f);
	float opticalDepth = extinction * distance;
	if (opticalDepth < 0.01f)
		return distance * (1.0f - opticalDepth * (0.5f - opticalDepth * (1.0f / 6.0f - opticalDepth / 24.0f)));
	return (1.0f - exp(-min(opticalDepth, 80.0f))) / max(extinction, 1.0e-30f);
}

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	if (any(dispatchID.xy >= VolumetricFogGridSize.xy))
		return;

	float3 accumulatedLighting = 0.0f.xxx;
	float accumulatedTransmittance = 1.0f;
	float accumulatedDepth = 0.0f;

	float previousDepth;
	float3 previousPositionWS = Atmosphere::ComputeCellWorldPosition(uint3(dispatchID.xy, 0), float3(0.5f, 0.5f, 0.0f), previousDepth);

	[loop] for (uint layerIndex = 0; layerIndex < VolumetricFogGridSize.z; layerIndex++)
	{
		uint3 layerCoordinate = uint3(dispatchID.xy, layerIndex);
		float4 scatteringAndExtinction = LightScattering[layerCoordinate];

		float layerDepth;
		float3 layerPositionWS = Atmosphere::ComputeCellWorldPosition(layerCoordinate, 0.5f.xxx, layerDepth);
		float stepLength = length(layerPositionWS - previousPositionWS);
		previousPositionWS = layerPositionWS;

		float extinction = max(scatteringAndExtinction.w, 0.0f);
		float transmittance = exp(-extinction * stepLength);

		accumulatedDepth += stepLength;
		float fadeIn = saturate(accumulatedDepth * VolumetricFogNearFadeInDistanceInv);

		float3 scatteringIntegratedOverSlice =
			fadeIn * scatteringAndExtinction.rgb * HomogeneousScatteringWeight(extinction, stepLength);
		accumulatedLighting += scatteringIntegratedOverSlice * accumulatedTransmittance;
		accumulatedTransmittance *= lerp(1.0f, transmittance, fadeIn);

		IntegratedLightScattering[layerCoordinate] = float4(max(accumulatedLighting, 0.0f.xxx), saturate(accumulatedTransmittance));
	}
}
