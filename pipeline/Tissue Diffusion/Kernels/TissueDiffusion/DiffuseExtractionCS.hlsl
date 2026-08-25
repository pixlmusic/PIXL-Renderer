RWTexture2D<float4> OutputRW : register(u0);

Texture2D<float4> ColorTexture : register(t0);
Texture2D<float4> AlbedoTexture : register(t3);

#include "Common/Color.hlsli"
#include "Common/SharedData.hlsli"
#include "TissueDiffusion/SSSCommon.hlsli"

// Strips albedo from the lit color so the SSS blur passes diffuse only
// irradiance (no per-texel diffuse color), then converts back to linear
// for the blur/importance-sampling passes that follow. Race/profile
// handling lives entirely downstream in Burley.hlsli / SeparableSSS.hlsli -
// this pass is universal across all races and doesn't need mask data.
[numthreads(8, 8, 1)] void main(uint3 DTid : SV_DispatchThreadID) {
	if (any(DTid.xy >= uint2(SharedData::BufferDim.xy)))
		return;

	float4 color = ColorTexture[DTid.xy];
	color.rgb = max(0.0, color.rgb);
	color.rgb = SSSRemoveAlbedo(color.rgb, AlbedoTexture[DTid.xy].rgb, ScatterMode);
	color.rgb = Color::IrradianceToLinear(color.rgb);
	OutputRW[DTid.xy] = max(0.0, color);
}
