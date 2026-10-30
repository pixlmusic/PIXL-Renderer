#include "Common/Math.hlsli"
#include "Common/Random.hlsli"

#if defined(CSHADER)
SamplerState ShadowmapSampler : register(s0);
SamplerState ShadowmapVLSampler : register(s1);
SamplerState InverseRepartitionSampler : register(s2);
SamplerState NoiseSampler : register(s3);

Texture2DArray<float> ShadowmapTex : register(t0);
Texture2DArray<float> ShadowmapVLTex : register(t1);
Texture1D<float> InverseRepartitionTex : register(t2);
Texture3D<float> NoiseTex : register(t3);

RWTexture3D<float> DensityRW : register(u0);

#	define LinearSampler ShadowmapSampler

#	include "Common/Framebuffer.hlsli"
#	include "Common/SharedData.hlsli"

#	include "Common/ShadowSampling.hlsli"

cbuffer PerTechnique : register(b0)
{
	row_major float4x4 CameraViewProj : packoffset(c0);
	row_major float4x4 CameraViewProjInverse : packoffset(c4);
	float4x3 ShadowMapProj[3] : packoffset(c8);
	float3 EndSplitDistances : packoffset(c17.x);
	float ShadowMapCount : packoffset(c17.w);
	float EnableShadowCasting : packoffset(c18);
	float3 DirLightDirection : packoffset(c19);
	float3 TextureDimensions : packoffset(c20);
	float3 WindInput : packoffset(c21);
	float InverseDensityScale : packoffset(c21.w);
	float3 PosAdjust : packoffset(c22);
	float IterationIndex : packoffset(c22.w);
	float PhaseContribution : packoffset(c23.x);
	float PhaseScattering : packoffset(c23.y);
	float DensityContribution : packoffset(c23.z);
}

[numthreads(32, 32, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	uint3 volumeDimensions = (uint3)max(TextureDimensions, 1.0f.xxx);
	if (any(dispatchID >= volumeDimensions))
		return;

	const float3 StepCoefficients[] = {
		{ 0, 0, 0 },
		{ 0, 0, 0.001 },
		{ 0, 0.001, 0 },
		{ 0, 0.001, 0.001 },
		{ 0.001, 0, 0 },
		{ 0.001, 0, 0.001 },
		{ 0.001, 0.001, 0 },
		{ 0.001, 0.001, 0.001 }
	};

	float3 normalizedCoordinates = float3(dispatchID.xy + 0.5, dispatchID.z - 1.0) * rcp(TextureDimensions.xyz);
	uint iterationIndex = (uint)clamp(IterationIndex, 0.0f, 7.0f);
	float3 depthUv = normalizedCoordinates + StepCoefficients[iterationIndex];
	float depth = InverseRepartitionTex.SampleLevel(InverseRepartitionSampler, depthUv.z, 0);
	float4 positionCS = float4(2 * depthUv.x - 1, 1 - 2 * depthUv.y, depth, 1);

	float4 positionWS = mul(CameraViewProjInverse, positionCS);
	if (!isfinite(positionWS.w) || abs(positionWS.w) < 1.0e-6f) {
		DensityRW[dispatchID.xyz] = 0.0f;
		return;
	}
	positionWS *= rcp(positionWS.w);

	float4 positionCSShifted = mul(CameraViewProj, positionWS);
	positionCSShifted *= rcp(positionCSShifted.w);

	float shadowMapDepth = positionCSShifted.z;

	bool noShadow = !SharedData::InInterior;
	if (EndSplitDistances.z >= shadowMapDepth) {
		uint cascadeIndex = ShadowMapCount >= 3.0f && shadowMapDepth > EndSplitDistances.y ? 2 : shadowMapDepth > EndSplitDistances.x ? 1 :
		                                                                                                                                0;
		float shadowMapThreshold = cascadeIndex == 0 ? 0.01f : 0.0f;
		float4x3 lightProjectionMatrix = ShadowMapProj[cascadeIndex];

		float3 positionLS = mul(transpose(lightProjectionMatrix), float4(positionWS.xyz, 1)).xyz;
		float shadowMapValue = ShadowmapTex.SampleLevel(ShadowmapSampler, float3(positionLS.xy, cascadeIndex), 0);
		noShadow = shadowMapValue >= positionLS.z - shadowMapThreshold;

		if (EnableShadowCasting < 0.5) {
			float shadowMapVLValue = ShadowmapVLTex.SampleLevel(ShadowmapVLSampler, float3(positionLS.xy, cascadeIndex), 0);
			noShadow = noShadow & shadowMapVLValue >= positionLS.z - shadowMapThreshold;
		}
	}

	float3 noiseUv = 0.0125 * (InverseDensityScale * (positionWS.xyz + WindInput));
	float noise = NoiseTex.SampleLevel(NoiseSampler, noiseUv, 0);
	float heightT = saturate(2.0f * positionWS.z / 300.0f);
	heightT = heightT * heightT * heightT * (heightT * (heightT * 6.0f - 15.0f) + 10.0f);
	float densityFactor = noise * (1.0f - 0.75f * heightT);
	float densityContribution = lerp(1, densityFactor, DensityContribution);

	float3 viewToOrigin = -positionWS.xyz;
	float LdotN = dot(viewToOrigin * rsqrt(max(dot(viewToOrigin, viewToOrigin), 1.0e-8f)), DirLightDirection);
	// Henyey-Greenstein phase function. The prior squared (1-g*cos(theta))
	// approximation skewed energy and under-shaped forward scattering. Keep g
	// inside the stable physical range and guard the grazing/forward denominator.
	float phaseG = clamp(PhaseScattering, -0.95f, 0.95f);
	float phaseDenominator = max(1.0f + phaseG * phaseG - 2.0f * phaseG * LdotN, 1.0e-4f);
	float phaseFactor = (1.0f - phaseG * phaseG) *
		rcp(4.0f * Math::PI * phaseDenominator * sqrt(phaseDenominator));
	float phaseContribution = lerp(1, phaseFactor, PhaseContribution);

	float shadowContribution = noShadow;

#	if defined(TERRAIN_OCCLUSION) || defined(SKY_VEIL)
	shadowContribution *= sqrt(ShadowSampling::GetWorldShadow(positionWS.xyz, PosAdjust));
#	endif

	float vl = shadowContribution * densityContribution * phaseContribution;

	DensityRW[dispatchID.xyz] = vl;
}
#endif
