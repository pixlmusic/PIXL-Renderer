#ifndef PIXL_ADVANCED_SNOW_MATERIAL_HLSLI
#define PIXL_ADVANCED_SNOW_MATERIAL_HLSLI

// Shared optical response for world snow and thin actor accumulation. Geometry,
// texture scale and compaction remain consumer-specific; tint, dielectric response
// and porous backscatter stay recognizably the same physical PIXL material.
namespace PIXLAdvancedSnowMaterial
{
	float3 Tint()
	{
		return float3(0.72f, 0.82f, 0.93f);
	}

	float PackedRoughness(float freshness)
	{
		return lerp(0.64f, 0.48f, saturate(freshness));
	}

	float ThinAccumulationRoughness(float freshAmount, float meltingAmount)
	{
		float total = max(freshAmount + meltingAmount, 1.0e-4f);
		float meltRatio = saturate(meltingAmount / total);
		return lerp(0.76f, 0.38f, meltRatio);
	}

	float3 DielectricF0(float freshness)
	{
		return lerp(0.030f, 0.040f, saturate(freshness)).xxx;
	}

	float3 BackscatterTint()
	{
		return float3(0.74f, 0.86f, 1.00f);
	}
}

#endif  // PIXL_ADVANCED_SNOW_MATERIAL_HLSLI
