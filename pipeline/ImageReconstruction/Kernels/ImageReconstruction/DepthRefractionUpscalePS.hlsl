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
	if (!all(isfinite(uv)))
		return 1.0f;
	float filteredDepth = DepthTex.SampleLevel(LinearSampler, uv, 0);
	float4 depthQuad = DepthTex.GatherRed(LinearSampler, uv);
	if (!isfinite(filteredDepth) || !all(isfinite(depthQuad)))
		return 1.0f;
	depthQuad = saturate(depthQuad);
	float minimumDepth = Min4(depthQuad);
	float maximumDepth = max(max(depthQuad.x, depthQuad.y), max(depthQuad.z, depthQuad.w));

	const bool containsSky = maximumDepth >= 0.999998f;
	const bool containsGeometry = minimumDepth > 1.0e-6f && minimumDepth < 0.999998f;
	bool discontinuity = containsSky && containsGeometry;
	// Wide mode widens *detection*, but the selected depth remains local to
	// the destination pixel. Borrowing far-away minimum depth would grow edges.
	if (!discontinuity && useWideKernel > 0.5f) {
		const float wideMin = SampleMinDepthWideGather(uv);
		if (isfinite(wideMin) && wideMin >= 0.0f && wideMin < 1.0f) {
			discontinuity = maximumDepth - wideMin > 0.08f;
		}
	}
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
	if (width == 0u || height == 0u)
		return saturate(filteredDepth);
	// Gather footprint is centered around uv*dimensions - 0.5. Sample that
	// exact 2x2 footprint and select nearest *valid* geometry (standard Z).
	const uint2 maxPixel = uint2(width, height) - 1u;
	const float2 footprint = clamp(floor(uv * float2(width, height) - 0.5f), 0.0f.xx, float2(maxPixel));
	const uint2 origin = uint2(footprint);
	float nearest = 1.0f;
	[unroll] for (uint y = 0u; y < 2u; ++y) {
		[unroll] for (uint x = 0u; x < 2u; ++x) {
			const uint2 p = min(origin + uint2(x, y), maxPixel);
			const float depth = DepthTex.Load(int3(p, 0));
			if (isfinite(depth) && depth >= 0.0f)
				nearest = min(nearest, depth);
		}
	}
	return saturate(nearest);
}

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	float2 originalUV = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(input.TexCoord);

	// Remove jitter offset to get the correct sampling coordinates
	float2 safeJitter = all(isfinite(jitter)) ? clamp(jitter, -2.0f.xx, 2.0f.xx) : 0.0f.xx;
	float2 uv = originalUV - (safeJitter * SharedData::BufferDim.zw);

	// Clamp within dynamic-resolution bounds.
	uv = FrameBuffer::ClampDynamicResolutionAdjustedScreenPosition(uv, input.TexCoord);

	// Refraction normals are continuous and may use linear filtering. Depth uses
	// edge-aware reconstruction so DLSS/FSR silhouettes remain categorical.
	psout.RefractionNormals = RefractionNormals.SampleLevel(LinearSampler, uv, 0);
	psout.RefractionNormals = all(isfinite(psout.RefractionNormals)) ? psout.RefractionNormals : float4(0.5f, 0.5f, 0.0f, 0.0f);
	const float sampledDepth = SampleEdgeAwareDepth(uv);
	psout.Depth = isfinite(sampledDepth) ? saturate(sampledDepth) : 1.0f;

	psout.SAOCameraZ = psout.Depth;

	return psout;
}

#endif
