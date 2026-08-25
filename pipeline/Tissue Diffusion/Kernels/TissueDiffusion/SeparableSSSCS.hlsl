RWTexture2D<float4> SSSRW : register(u0);

Texture2D<float4> ColorTexture : register(t0);
Texture2D<float4> DepthTexture : register(t1);
Texture2D<float4> MaskTexture : register(t2);
Texture2D<float4> AlbedoTexture : register(t3);
Texture2D<float4> NormalTexture : register(t4);

SamplerState PointSampler : register(s0);

#include "Common/Color.hlsli"
#include "Common/GBuffer.hlsli"
#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"
#include "TissueDiffusion/SSSCommon.hlsli"

#if defined(BURLEY)
#	include "TissueDiffusion/Burley.hlsli"
#else
#	include "TissueDiffusion/SeparableSSS.hlsli"
#endif

[numthreads(8, 8, 1)] void main(uint3 DTid : SV_DispatchThreadID) {
	// Early exit if dispatch thread is outside screen bounds
	if (any(DTid.xy >= uint2(SharedData::BufferDim.xy)))
		return;

	float2 texCoord = (DTid.xy + 0.5) * SharedData::BufferDim.zw;

#if defined(BURLEY)

	float4 mask = MaskTexture[DTid.xy];
	float sssAmount = mask.x;

	if (sssAmount > 0.0) {
		float4 color = BurleyNormalizedSS(DTid.xy, texCoord, sssAmount, mask);
		SSSRW[DTid.xy] = max(0, color);
	}

#elif defined(HORIZONTAL)

	float4 mask = MaskTexture[DTid.xy];
	float sssAmount = mask.x;

	float4 color = SSSSBlurCS(texCoord, float2(1.0, 0.0), sssAmount, mask);
	SSSRW[DTid.xy] = max(0, color);

#else

	float4 mask = MaskTexture[DTid.xy];
	float sssAmount = mask.x;

	if (sssAmount > 0.0) {
		float4 color = SSSSBlurCS(texCoord, float2(0.0, 1.0), sssAmount, mask);
		color.rgb = Color::IrradianceToGamma(color.rgb);
		color.rgb = SSSApplyAlbedo(color.rgb, AlbedoTexture[DTid.xy].rgb, ScatterMode);
		SSSRW[DTid.xy] = float4(max(0.0, color.rgb), 1.0);
	}

#endif
}
