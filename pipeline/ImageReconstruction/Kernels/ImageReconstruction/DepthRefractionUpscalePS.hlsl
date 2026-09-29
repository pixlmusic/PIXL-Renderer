#include "ImageReconstruction/UpscaleVS.hlsl"

#if defined(PSHADER)
#	include "Common/FrameBuffer.hlsli"
#	include "Common/SharedData.hlsli"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 RefractionNormals: SV_TARGET0;
	float SAOCameraZ: SV_TARGET1;
	float Depth: SV_Depth;
};

SamplerState LinearSampler : register(s0);

Texture2D<float4> RefractionNormals : register(t0);
Texture2D<float> DepthTex : register(t1);

cbuffer JitterCB : register(b0)
{
	float2 jitter;
	float useWideKernel;
	float pad0;
};

float SampleMinDepth2x2(float2 uv)
{
	float4 depthQuad = DepthTex.GatherRed(LinearSampler, uv);
	return min(min(depthQuad.x, depthQuad.y), min(depthQuad.z, depthQuad.w));
}

float Min4(float4 v)
{
	return min(min(v.x, v.y), min(v.z, v.w));
}

float SampleMinDepthWideGather(float2 uv)
{
	// Gather-only wide footprint (4 offset 2x2 GatherRed calls) to save performance.
	float d0 = Min4(DepthTex.GatherRed(LinearSampler, uv, int2(-1, -1)));
	float d1 = Min4(DepthTex.GatherRed(LinearSampler, uv, int2(1, -1)));
	float d2 = Min4(DepthTex.GatherRed(LinearSampler, uv, int2(-1, 1)));
	float d3 = Min4(DepthTex.GatherRed(LinearSampler, uv, int2(1, 1)));
	return min(min(d0, d1), min(d2, d3));
}

float SampleEdgeAwareDepth(float2 uv)
{
	float filteredDepth = DepthTex.SampleLevel(LinearSampler, uv, 0);
	float4 depthQuad = DepthTex.GatherRed(LinearSampler, uv);
	float minimumDepth = Min4(depthQuad);
	float maximumDepth = max(max(depthQuad.x, depthQuad.y), max(depthQuad.z, depthQuad.w));

	const bool containsSky = maximumDepth >= 0.999998f;
	const bool containsGeometry = minimumDepth > 1.0e-6f && minimumDepth < 0.999998f;
	bool discontinuity = containsSky && containsGeometry;
	if (!discontinuity && containsGeometry) {
		float nearLinear = SharedData::GetScreenDepth(minimumDepth);
		float farLinear = SharedData::GetScreenDepth(maximumDepth);
		float relativeSpan = abs(farLinear - nearLinear) / max(min(nearLinear, farLinear), 64.0f);
		discontinuity = relativeSpan > 0.08f;
	}

	if (!discontinuity)
		return filteredDepth;

	// Device depth is not a colour value: bilinear interpolation across a
	// silhouette invents a false depth ramp which post effects interpret as real
	// geometry. Select the nearest source-depth texel only at discontinuities;
	// continuous terrain still receives smooth interpolation.
	uint width, height;
	DepthTex.GetDimensions(width, height);
	uint2 nearestPixel = min(uint2(saturate(uv) * float2(width, height)), uint2(width, height) - 1u);
	return DepthTex.Load(int3(nearestPixel, 0));
}

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	float2 originalUV = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(input.TexCoord);

	// Remove jitter offset to get the correct sampling coordinates
	float2 uv = originalUV - (jitter * SharedData::BufferDim.zw);

	// Clamp within dynamic-resolution bounds.
	uv = FrameBuffer::ClampDynamicResolutionAdjustedScreenPosition(uv, input.TexCoord);

	// Refraction normals are continuous and may use linear filtering. Depth uses
	// edge-aware reconstruction so DLSS/FSR silhouettes remain categorical.
	psout.RefractionNormals = RefractionNormals.SampleLevel(LinearSampler, uv, 0);
	psout.Depth = SampleEdgeAwareDepth(uv);

	psout.SAOCameraZ = psout.Depth;

	return psout;
}

#endif
