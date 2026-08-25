#ifndef PIXL_MATERIAL_DETAIL_HLSLI
#define PIXL_MATERIAL_DETAIL_HLSLI

namespace MaterialDetail
{
	float Luminance(float3 color)
	{
		return dot(max(color, 0.0f), float3(0.2126f, 0.7152f, 0.0722f));
	}

	float GetMipLevel(float2 coords, Texture2D<float4> tex)
	{
		float2 dims;
		tex.GetDimensions(dims.x, dims.y);
		float2 uvTexels = coords * dims;
		float2 dxSize = ddx(uvTexels);
		float2 dySize = ddy(uvTexels);
		return max(0.5f * log2(max(max(dot(dxSize, dxSize), dot(dySize, dySize)), 1e-8f)), 0.0f);
	}

	float Visibility(float distance, float mipLevel, float materialStrength)
	{
		if (!MaterialLayersTuning::DetailReconstructionEnabled())
			return 0.0f;

		float distanceFade =
			1.0f - smoothstep(
				MaterialLayersTuning::DetailFadeStart(),
				MaterialLayersTuning::DetailFadeEnd(),
				abs(distance));
		float maxMip = MaterialLayersTuning::DetailMaxMip();
		float mipFade = 1.0f - smoothstep(max(maxMip - 1.5f, 0.0f), maxMip, mipLevel);

		// Anti-shimmer progressively suppresses recovered detail as the footprint
		// grows. At zero the feature follows only the explicit mip/distance fades.
		float shimmerGuard = lerp(1.0f, saturate(1.0f - mipLevel / max(maxMip, 1.0f)),
			MaterialLayersTuning::DetailAntiShimmer());

		return saturate(distanceFade * mipFade * shimmerGuard * max(materialStrength, 0.0f));
	}

	float DarkProtection(float3 coarseColor)
	{
		float luma = Luminance(coarseColor);
		float safeDark = smoothstep(0.025f, 0.20f, luma);
		return lerp(1.0f, safeDark, MaterialLayersTuning::DetailDarkProtection());
	}

	float3 ReconstructAlbedo(float3 fineColor, float3 coarseColor, float visibility)
	{
		// Recover luminance structure preferentially. Full RGB amplification turns
		// low-resolution colour noise and compression blocks into false painted
		// detail, especially on old vanilla textures.
		float3 residual = fineColor - coarseColor;
		float lumaResidual = Luminance(fineColor) - Luminance(coarseColor);
		float3 chromaResidual = residual - lumaResidual.xxx;
		float residualLimit = lerp(
			0.035f,
			0.16f,
			smoothstep(0.025f, 0.40f, Luminance(coarseColor)));
		float boundedLuma = lumaResidual * rcp(
			1.0f + abs(lumaResidual) * rcp(max(residualLimit, 1e-4f)));
		float3 structuralResidual = boundedLuma.xxx + chromaResidual * 0.28f;
		float protection = DarkProtection(coarseColor);
		float strength =
			visibility *
			MaterialLayersTuning::DetailAlbedoStrength() *
			MaterialLayersTuning::DetailContrast() *
			protection;
		return saturate(fineColor + structuralResidual * strength);
	}

	float3 ReconstructNormalEncoded(float3 fineEncoded, float3 coarseEncoded, float visibility)
	{
		float3 fine = fineEncoded * 2.0f - 1.0f;
		float3 coarse = coarseEncoded * 2.0f - 1.0f;
		float2 delta = fine.xy - coarse.xy;
		float2 xy = fine.xy + delta *
			(visibility * MaterialLayersTuning::DetailNormalStrength());

		float xyLenSq = dot(xy, xy);
		if (xyLenSq > 0.9025f)
			xy *= sqrt(0.9025f / max(xyLenSq, 1e-6f));

		float z = sqrt(saturate(1.0f - dot(xy, xy)));
		return float3(xy * 0.5f + 0.5f, z * 0.5f + 0.5f);
	}

	float RoughnessDelta(float3 fineColor, float3 coarseColor, float visibility)
	{
		float3 colorResidual = fineColor - coarseColor;
		float lumaResidual = Luminance(fineColor) - Luminance(coarseColor);
		float3 chromaResidual = colorResidual - lumaResidual.xxx;
		float chromaMagnitude = length(chromaResidual);
		float lumaConfidence = saturate(
			abs(lumaResidual) * rcp(abs(lumaResidual) + chromaMagnitude + 0.008f));

		// Dark micro-cavities become a little rougher while bright exposed ridges
		// become only slightly smoother. This adds a stable material cue instead of
		// converting every colour edge into uniformly matte noise.
		float cavity = saturate(-lumaResidual * 4.0f);
		float ridge = saturate(lumaResidual * 4.0f);
		float signedDelta = cavity * 0.18f - ridge * 0.055f;
		return signedDelta *
			lerp(0.20f, 1.0f, lumaConfidence) *
			visibility *
			MaterialLayersTuning::DetailRoughnessStrength();
	}

	float ApplyRoughnessDetail(float roughness, float delta)
	{
		float smoothProtection = lerp(
			1.0f,
			smoothstep(0.08f, 0.40f, roughness),
			MaterialLayersTuning::DetailSmoothProtection());
		return clamp(
			roughness + delta * smoothProtection,
			0.04f,
			1.0f);
	}
}

#endif
