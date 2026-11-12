#include "ImageReconstruction/UpscaleVS.hlsl"

#if defined(PSHADER)
typedef VS_OUTPUT PS_INPUT;

Texture2D<float4> SourceColor : register(t0);
SamplerState LinearSampler : register(s0);
cbuffer FallbackDimensions : register(b0)
{
	float2 trueSamplingDim;
	float cloudMaskAvailable;
	float pad0;
};

float4 main(PS_INPUT input) : SV_TARGET
{
	// A neutral bilinear presentation fallback for a failed temporal upscaler.
	// The source contains the current reduced-resolution scene in its valid
	// upper-left rectangle; UVs cover that rectangle using the render scale.
	uint sourceWidth, sourceHeight;
	SourceColor.GetDimensions(sourceWidth, sourceHeight);
	float2 sourceDim = max(float2(sourceWidth, sourceHeight), 1.0f.xx);
	float2 inputDim = all(isfinite(trueSamplingDim))
		? clamp(trueSamplingDim, 1.0f.xx, sourceDim) : sourceDim;
	float2 texCoord = all(isfinite(input.TexCoord)) ? saturate(input.TexCoord) : 0.5f.xx;
	float2 sourcePixel = texCoord * max(inputDim - 1.0f, 0.0f.xx) + 0.5f;
	float4 color = SourceColor.SampleLevel(LinearSampler, sourcePixel / sourceDim, 0.0f);
	return all(isfinite(color)) ? color : float4(0.0f, 0.0f, 0.0f, 1.0f);
}
#endif
