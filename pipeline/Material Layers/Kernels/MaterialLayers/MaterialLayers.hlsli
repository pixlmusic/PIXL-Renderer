// https://github.com/tgjones/slimshader-cpp/blob/master/src/Shaders/Sdk/Direct3D11/DetailTessellation11/POM.hlsl
// https://github.com/alandtse/SSEShaderTools/blob/main/shaders_vr/ParallaxEffect.h

// https://github.com/marselas/Zombie-Direct3D-Samples/blob/5f53dc2d6f7deb32eb2e5e438d6b6644430fe9ee/Direct3D/ParallaxOcclusionMapping/ParallaxOcclusionMapping.fx
// http://www.diva-portal.org/smash/get/diva2:831762/FULLTEXT01.pdf
// https://bartwronski.files.wordpress.com/2014/03/ac4_gdc.pdf

#ifndef MATERIAL_LAYERS_HLSLI
#define MATERIAL_LAYERS_HLSLI

#include "MaterialLayers/MaterialLayersTuning.hlsli"
#include "MaterialLayers/MaterialDetail.hlsli"

#if defined(LANDSCAPE)
#	if defined(TERRAIN_DETAIL)
#		include "TerrainDetail/TerrainDetail.hlsli"
#	else
struct StochasticOffsets
{
	float2 offset1;
	float2 offset2;
	float tap1Weight;
};
#	endif
#endif

struct DisplacementParams
{
	float DisplacementScale;
	float DisplacementOffset;
	float HeightScale;
	float FlattenAmount;
};

namespace MaterialLayers
{
	static const float ShadowIntensity = 2.0;
	static const float ParallaxCheapDistance = 1024.0;
	static const float ParallaxNearShadowQuality = 1.0;
	static const float ParallaxFarShadowQuality = 0.5;
	static const float TerrainParallaxShadowMaxMipLevel = 2.0;

	// Coarse height-map mip for POM march. Distance bias is applied and floored.
	// Grazing bias is omitted — it creates discontinuous hits.
	inline float ComputeParallaxMarchMip(float baseMip, float viewDist)
	{
		float m = baseMip + 1.0;
		m += min(2.0, baseMip * 0.5);
		float farFloor = viewDist > 0.0
			? lerp(0.0, 3.0, saturate((viewDist - 512.0) * rcp(1536.0)))
			: saturate(baseMip - 1.0);
		m = max(m, farFloor);
		return floor(m);
	}

	inline uint ParallaxShadowTapCount(float quality)
	{
		uint taps = 1;
		if (quality > 0.25)
			taps++;
		if (quality > 0.5)
			taps++;
		if (quality > 0.75)
			taps++;
		return taps;
	}

	float ScaleDisplacement(float displacement, DisplacementParams params)
	{
		return (displacement - 0.5) * params.HeightScale;
	}

	float AdjustDisplacementNormalized(float displacement, DisplacementParams params)
	{
		return (displacement - 0.5) * params.DisplacementScale + 0.5 + params.DisplacementOffset;
	}

	float4 AdjustDisplacementNormalized(float4 displacement, DisplacementParams params)
	{
		// Vector form avoids four scalar helper calls in the hottest POM path.
		return (displacement - 0.5f) * params.DisplacementScale + 0.5f + params.DisplacementOffset;
	}

	float GetMipLevelFromDims(float2 coords, float2 textureDims)
	{
#	if !defined(PARALLAX) && !defined(MATERIAL_FORGE)
		textureDims /= 2.0;
#	endif

		float2 texCoordsPerSize = coords * textureDims;

		// Compute the current gradients:
		float2 dxSize = ddx(texCoordsPerSize);
		float2 dySize = ddy(texCoordsPerSize);

		// Standard texture footprint uses the largest derivative. The old min()
		// underestimated anisotropic/grazing footprints, forcing unnecessarily fine
		// height mips that shimmered and reduced cache locality.
		float maxTexCoordDelta = max(dot(dxSize, dxSize), dot(dySize, dySize));

		// 0.5 * log2(length^2) == log2(length). Keep a finite floor for
		// degenerate derivatives.
		float mipLevel = max(0.5f * log2(max(maxTexCoordDelta, 1e-8f)), 0.0f);

#	if !defined(PARALLAX) && !defined(MATERIAL_FORGE)
		mipLevel++;
#	endif

		return floor(max(mipLevel + SharedData::MipBias, 0));
	}

	float GetMipLevel(float2 coords, Texture2D<float4> tex)
	{
		float2 textureDims;
		tex.GetDimensions(textureDims.x, textureDims.y);
		return GetMipLevelFromDims(coords, textureDims);
	}

#	if defined(LANDSCAPE)
#		include "MaterialLayers/MaterialLayersTerrain.hlsli"
#	endif
#	include "MaterialLayers/MaterialLayersParallaxCore.hlsli"
}

#endif  // MATERIAL_LAYERS_HLSLI
