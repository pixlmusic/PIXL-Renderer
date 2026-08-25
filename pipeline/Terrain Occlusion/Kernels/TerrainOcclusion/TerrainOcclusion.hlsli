namespace TerrainOcclusion
{
	#ifndef USE_PIXL_TERRAIN_HORIZON_SHADOWS
	#	define USE_PIXL_TERRAIN_HORIZON_SHADOWS 1
	#endif

	Texture2D<float2> ShadowHeightTexture : register(t60);

	float2 GetTerrainShadowUV(float2 xy)
	{
		return xy * SharedData::terrainOcclusionSettings.Scale.xy + SharedData::terrainOcclusionSettings.Offset.xy;
	}

	float GetTerrainZ(float norm_z)
	{
		return lerp(SharedData::terrainOcclusionSettings.ZRange.x, SharedData::terrainOcclusionSettings.ZRange.y, norm_z) - 256;
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
	#if USE_PIXL_TERRAIN_HORIZON_SHADOWS
		uint width, height;
		ShadowHeightTexture.GetDimensions(width, height);
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
			shadow += saturate((worldPos.z - shadowHeight.y) / penumbraWidth) * weights[i];
		}
		return saturate(shadow);
	#else
		float2 shadowHeight = GetTerrainZ(ShadowHeightTexture.SampleLevel(samp, shadowUV, 0));
		return saturate((worldPos.z - shadowHeight.y) / max(shadowHeight.x - shadowHeight.y, 1.0f));
	#endif
	}
}
