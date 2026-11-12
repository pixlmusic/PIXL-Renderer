namespace TerrainOcclusion
{
	float ShadowSmootherStep(float value)
	{
		float t = saturate(value);
		return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
	}
	#ifndef USE_PIXL_TERRAIN_HORIZON_SHADOWS
	#	define USE_PIXL_TERRAIN_HORIZON_SHADOWS 1
	#endif

	Texture2D<float2> ShadowHeightTexture : register(t60);

	float2 GetTerrainShadowUV(float2 xy)
	{
		float2 uv = xy * SharedData::terrainOcclusionSettings.Scale.xy + SharedData::terrainOcclusionSettings.Offset.xy;
		return all(isfinite(uv)) ? uv : -1.0f.xx;
	}

	float GetTerrainZ(float norm_z)
	{
		return lerp(SharedData::terrainOcclusionSettings.ZRange.x, SharedData::terrainOcclusionSettings.ZRange.y, saturate(norm_z)) - 256;
	}

	float2 GetTerrainZ(float2 norm_z)
	{
		return float2(GetTerrainZ(norm_z.x), GetTerrainZ(norm_z.y));
	}

	float GetTerrainShadow(const float3 worldPos, SamplerState samp)
	{
		if (!SharedData::terrainOcclusionSettings.EnableTerrainShadow)
			return 1.0;
		float2 shadowUV = GetTerrainShadowUV(worldPos.xy);
		if (any(shadowUV < 0.0f) || any(shadowUV > 1.0f) || !isfinite(worldPos.z))
			return 1.0f;
	#if USE_PIXL_TERRAIN_HORIZON_SHADOWS
		uint width, height;
		ShadowHeightTexture.GetDimensions(width, height);
		if (width == 0u || height == 0u)
			return 1.0f;
		float2 texel = rcp(float2(max(width, 1u), max(height, 1u)));
		float2 offsets[5] = {
			0.0f.xx,
			float2(texel.x, 0.0f), float2(-texel.x, 0.0f),
			float2(0.0f, texel.y), float2(0.0f, -texel.y)
		};
		float weights[5] = { 0.40f, 0.15f, 0.15f, 0.15f, 0.15f };
		float shadow = 0.0f;
		[unroll] for (uint i = 0; i < 5; ++i)
		{
			float2 sampleUV = shadowUV + offsets[i];
			if (any(sampleUV < 0.0f) || any(sampleUV > 1.0f)) {
				shadow += weights[i];
				continue;
			}
			float2 shadowHeight = GetTerrainZ(ShadowHeightTexture.SampleLevel(samp, sampleUV, 0));
			float penumbraWidth = max(shadowHeight.x - shadowHeight.y, 1.0f);
			float visibility = saturate((worldPos.z - shadowHeight.y) / penumbraWidth);
			shadow += ShadowSmootherStep(visibility) * weights[i];
		}
		return saturate(shadow);
	#else
		float2 shadowHeight = GetTerrainZ(ShadowHeightTexture.SampleLevel(samp, shadowUV, 0));
		float visibility = saturate((worldPos.z - shadowHeight.y) / max(shadowHeight.x - shadowHeight.y, 1.0f));
		return ShadowSmootherStep(visibility);
	#endif
	}
}
