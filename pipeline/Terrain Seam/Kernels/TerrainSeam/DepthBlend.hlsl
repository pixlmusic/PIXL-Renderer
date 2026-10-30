RWTexture2D<float> BlendedDepthTexture : register(u0);
RWTexture2D<unorm float> BlendedDepthTexture16 : register(u1);

Texture2D<unorm float> MainDepthTexture : register(t0);
Texture2D<unorm float> TerrainDepthTexture : register(t1);

[numthreads(8, 8, 1)] void main(uint3 DTid : SV_DispatchThreadID) {
	uint width, height;
	MainDepthTexture.GetDimensions(width, height);
	uint terrainWidth, terrainHeight;
	TerrainDepthTexture.GetDimensions(terrainWidth, terrainHeight);
	uint2 validSize = min(uint2(width, height), uint2(terrainWidth, terrainHeight));
	uint blendedWidth, blendedHeight;
	BlendedDepthTexture.GetDimensions(blendedWidth, blendedHeight);
	uint blended16Width, blended16Height;
	BlendedDepthTexture16.GetDimensions(blended16Width, blended16Height);
	validSize = min(validSize, min(uint2(blendedWidth, blendedHeight), uint2(blended16Width, blended16Height)));
	if (any(validSize == 0u) || any(DTid.xy >= validSize))
		return;
	float mainDepth = MainDepthTexture[DTid.xy];
	float terrainDepth = TerrainDepthTexture[DTid.xy];
	// Inspect IEEE exponent bits directly because these depth textures use UNORM
	// resources and FXC warns that floating-point isfinite can force IEEE mode.
	mainDepth = ((asuint(mainDepth) & 0x7f800000u) != 0x7f800000u) ? saturate(mainDepth) : 1.0f;
	terrainDepth = ((asuint(terrainDepth) & 0x7f800000u) != 0x7f800000u) ? saturate(terrainDepth) : 1.0f;
	float mixedDepth = min(mainDepth, terrainDepth);
	BlendedDepthTexture[DTid.xy] = mixedDepth;
	BlendedDepthTexture16[DTid.xy] = mixedDepth;
}
