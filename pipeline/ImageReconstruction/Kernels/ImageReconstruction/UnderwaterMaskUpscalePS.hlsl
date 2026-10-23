#include "ImageReconstruction/UpscaleVS.hlsl"

#if defined(PSHADER)
#	include "Common/FrameBuffer.hlsli"
#	include "Common/Math.hlsli"
#	include "Common/SharedData.hlsli"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float UnderwaterMask: SV_TARGET;
};

SamplerState LinearSampler : register(s0);

Texture2D<float> UnderwaterMask : register(t0);
Texture2D<float> SceneDepth : register(t1);

bool IsFiniteScalar(float value)
{
	return (asuint(value) & 0x7f800000u) != 0x7f800000u;
}

cbuffer JitterCB : register(b0)
{
	float2 jitter;
	float useWideKernel;
	float pad0;
};

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	float2 originalUV = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(input.TexCoord);

	// Match the depth upscale path's finite jitter handling and dynamic bounds.
	float2 safeJitter = all(isfinite(jitter)) ? clamp(jitter, -2.0f.xx, 2.0f.xx) : 0.0f.xx;
	float2 uv = originalUV - safeJitter * SharedData::BufferDim.zw;
	uv = FrameBuffer::ClampDynamicResolutionAdjustedScreenPosition(uv, input.TexCoord);

	uint maskWidth, maskHeight;
	UnderwaterMask.GetDimensions(maskWidth, maskHeight);
	if (maskWidth == 0u || maskHeight == 0u) {
		psout.UnderwaterMask = 0.0f;
		return psout;
	}
	float mask = UnderwaterMask.SampleLevel(LinearSampler, uv, 0);
	if (!IsFiniteScalar(mask))
		mask = 0.0f;

	// Linear mask filtering invents fractional underwater coverage at hard depth
	// silhouettes. Preserve categorical coverage at discontinuities by following
	// the nearest device-depth sample; the wide mode also treats large local depth
	// changes as edges, matching the companion depth-upscale setting.
	float4 depthQuad = SceneDepth.GatherRed(LinearSampler, uv);
	if (all(isfinite(depthQuad))) {
		float minimumDepth = min(min(depthQuad.x, depthQuad.y), min(depthQuad.z, depthQuad.w));
		float maximumDepth = max(max(depthQuad.x, depthQuad.y), max(depthQuad.z, depthQuad.w));
		bool skyGeometryEdge = minimumDepth < 0.999998f && maximumDepth >= 0.999998f;
		bool wideDepthEdge = useWideKernel > 0.5f && maximumDepth - minimumDepth > 0.08f;
		if (skyGeometryEdge || wideDepthEdge) {
			uint depthWidth, depthHeight;
			SceneDepth.GetDimensions(depthWidth, depthHeight);
			if (depthWidth > 0u && depthHeight > 0u) {
				uint2 depthPixel = min(uint2(saturate(uv) * float2(depthWidth, depthHeight)), uint2(depthWidth - 1u, depthHeight - 1u));
				uint2 maskPixel = min(uint2(saturate(uv) * float2(maskWidth, maskHeight)), uint2(maskWidth - 1u, maskHeight - 1u));
				float nearestDepth = 1.0f;
				uint2 nearestMaskPixel = maskPixel;
				[unroll]
				for (uint y = 0u; y < 2u; ++y) {
					[unroll]
					for (uint x = 0u; x < 2u; ++x) {
						uint2 p = min(depthPixel + uint2(x, y), uint2(depthWidth - 1u, depthHeight - 1u));
						float sampleDepth = SceneDepth.Load(int3(p, 0));
						if (IsFiniteScalar(sampleDepth) && sampleDepth < nearestDepth) {
							nearestDepth = sampleDepth;
							float2 sampleUV = (float2(p) + 0.5f) / float2(depthWidth, depthHeight);
							nearestMaskPixel = min(uint2(sampleUV * float2(maskWidth, maskHeight)), uint2(maskWidth - 1u, maskHeight - 1u));
						}
					}
				}
				float nearestMask = UnderwaterMask.Load(int3(nearestMaskPixel, 0));
				mask = IsFiniteScalar(nearestMask) ? nearestMask : 0.0f;
			}
		}
	}
	psout.UnderwaterMask = saturate(mask);

	return psout;
}

#endif
