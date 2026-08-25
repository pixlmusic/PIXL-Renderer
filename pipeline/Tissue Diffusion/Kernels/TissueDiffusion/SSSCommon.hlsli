#ifndef SSS_COMMON_HLSLI
#define SSS_COMMON_HLSLI

#include "Common/Math.hlsli"

#define SSSS_N_SAMPLES 21

#define SSS_SCATTER_MODE_PRE 0
#define SSS_SCATTER_MODE_POST 1
#define SSS_SCATTER_MODE_PRE_POST 2

// -----------------------------------------------------------------------------
// Mask channel contract (Texture2D<float4> MaskTexture, t2)
//
//   x : sssAmount      - overall SSS strength/weight for this pixel (unchanged).
//   y : humanBlend      - 0..1 blend weight between BaseProfile and HumanProfile.
//                         Was previously read as a hard bool (>0.0). It is now treated
//                         as a continuous lerp factor, so painting exactly 0 or 1 is
//                         bit-identical to the old behaviour; painting an in-between
//                         value along a race boundary simply removes the seam.
//   z : directionalAmbientLuma - existing deferred-lighting payload. It is not a
//                                material classification channel and must never alter
//                                the diffusion profile.
//   w : coverage/stochastic data - existing deferred payload. It is not fur coverage.
//
// Important: Lighting.hlsl owns this render-target contract. Earlier PIXL shader
// revisions misread z/w as keratin/fur, which caused opaque ordinary skin (w ~= 1)
// to be treated as hardened tissue and shortened its diffusion path dramatically.
// Beast/human selection remains explicitly represented by y; no ABI is changed here.
// -----------------------------------------------------------------------------

cbuffer PerFrameSSS : register(b1)
{
	float4 Kernels[SSSS_N_SAMPLES + SSSS_N_SAMPLES];
	float4 BaseProfile;
	float4 HumanProfile;
	float SSSS_FOVY;
	uint BurleySamples;
	uint ScatterMode;
	uint pad;
	float4 MeanFreePathBase;
	float4 MeanFreePathHuman;
};

float3 SSSRemoveAlbedo(float3 color, float3 albedo, uint mode)
{
	if (mode == SSS_SCATTER_MODE_PRE)
		return color;
	albedo /= Color::PBRLightingScale;
	float3 divisor = (mode == SSS_SCATTER_MODE_PRE_POST) ? sqrt(albedo) : albedo;
	return lerp(color, color / max(divisor, EPSILON_SSS_ALBEDO), albedo > EPSILON_SSS_ALBEDO);
}

float3 SSSApplyAlbedo(float3 irradiance, float3 albedo, uint mode)
{
	if (mode == SSS_SCATTER_MODE_PRE)
		return irradiance;
	albedo /= Color::PBRLightingScale;
	float3 multiplier = (mode == SSS_SCATTER_MODE_PRE_POST) ? sqrt(albedo) : albedo;
	return lerp(irradiance, irradiance * multiplier, albedo > EPSILON_SSS_ALBEDO);
}

// Continuous Base<->Human profile blend weight. See mask channel contract above.
float SSSGetHumanBlend(float4 mask)
{
	return saturate(mask.y);
}

// Cross-profile samples are rejected smoothly so adjacent actors/materials cannot
// smear their different diffusion profiles into one another at silhouettes.
float SSSGetProfileAgreement(float4 centerMask, float4 sampleMask)
{
	return 1.0f - smoothstep(0.05f, 0.5f, abs(SSSGetHumanBlend(centerMask) - SSSGetHumanBlend(sampleMask)));
}

// Per-component safe divide: colorSum / weightSum, but any channel whose weight
// never accumulated (or is numerically tiny) falls back to 0 instead of poisoning
// the whole pixel the way a single `any(weightSum == 0)` test would.
float3 SSSSafeNormalize(float3 colorSum, float3 weightSum)
{
	float3 weightSumSafe = max(weightSum, 1e-6f);
	float3 normalized = colorSum * (1.0f / weightSumSafe);
	return lerp(0.0f, normalized, weightSum > 1e-6f);
}

#endif
