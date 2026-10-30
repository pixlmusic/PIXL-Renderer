#include "Common/DummyVSTexCoord.hlsl"
#include "Common/FrameBuffer.hlsli"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float VL: SV_Target0;
};

#if defined(PSHADER)
SamplerState DepthSampler : register(s0);
SamplerState PreviousFrameSampler : register(s1);
SamplerState VLSampler : register(s2);
SamplerState RepartitionSampler : register(s3);
SamplerState NoiseGradSamplerSampler : register(s4);
SamplerState MotionVectorsSampler : register(s5);
SamplerState PreviousDepthSampler : register(s6);

Texture2D<float4> DepthTex : register(t0);
Texture2D<float4> PreviousFrameTex : register(t1);
Texture3D<float4> VLTex : register(t2);
Texture1D<float4> RepartitionTex : register(t3);
Texture2D<float4> NoiseGradSamplerTex : register(t4);
Texture2D<float4> MotionVectorsTex : register(t5);
Texture2D<float4> PreviousDepthTex : register(t6);

cbuffer PerGeometry : register(b2)
{
	float4 g_IntensityX_TemporalY : packoffset(c0);
};

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;
	static const float kVLThresholdBias = 1.0 / 128.0;

	float2 screenPosition = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(input.TexCoord);
	float depth = DepthTex.Sample(DepthSampler, screenPosition).x;
	bool validCurrentDepth = isfinite(depth) && depth >= 0.0f && depth <= 1.0f;
	bool currentIsSky = validCurrentDepth && depth >= 0.999998f;

	float repartition = RepartitionTex.SampleLevel(RepartitionSampler, validCurrentDepth ? depth : 0.0f, 0).x;
	repartition = isfinite(repartition) ? clamp(repartition, 0.0f, 0.9999f) : 0.0f;
	float vl = g_IntensityX_TemporalY.x * VLTex.SampleLevel(VLSampler, float3(input.TexCoord, repartition), 0).x;
	vl = isfinite(vl) ? vl : 0.0f;

	// Vanilla sampled normalized noise at 0.125 * pixelPosition, which repeats
	// every eight screen pixels with a wrap sampler and can become a visible
	// checker/grid after DLSS or Frame Generation. Scramble integer pixels into
	// the same noise texture instead: identical amplitude/distribution, no 8px tile.
	uint noiseWidth;
	uint noiseHeight;
	NoiseGradSamplerTex.GetDimensions(noiseWidth, noiseHeight);
	uint2 pixel = uint2(input.Position.xy);
	uint2 noiseCoord = uint2(
		(pixel.x * 37u + pixel.y * 17u) % max(noiseWidth, 1u),
		(pixel.x * 13u + pixel.y * 53u) % max(noiseHeight, 1u));
	float noiseGrad = 0.03125f * NoiseGradSamplerTex.Load(int3(noiseCoord, 0)).x;

	float adjustedVl = max(0.0f, (isfinite(noiseGrad) ? noiseGrad : 0.0f) + vl - kVLThresholdBias);

	float temporalResponse = isfinite(g_IntensityX_TemporalY.y)
		? saturate(g_IntensityX_TemporalY.y)
		: 0.0f;
	if (temporalResponse > 0.001f) {
		float2 motionVector = MotionVectorsTex.Sample(MotionVectorsSampler, screenPosition).xy;
		float2 previousTexCoord = input.TexCoord + motionVector;
		// Vanilla motion vectors describe opaque surfaces, not PIXL's independently
		// advected cloud layer. Do not drag the prior screen-space volume over sky
		// pixels; cloud silhouettes and sky lighting can change without depth motion.
		bool isValid = validCurrentDepth && !currentIsSky && all(isfinite(motionVector)) && all(isfinite(previousTexCoord)) &&
			previousTexCoord.x >= 0.0f && previousTexCoord.x < 1.0f &&
			previousTexCoord.y >= 0.0f && previousTexCoord.y < 1.0f;
		float previousVl = adjustedVl;
		float temporalContribution = 0.0f;
		if (isValid) {
			float2 previousScreenPosition = FrameBuffer::GetPreviousDynamicResolutionAdjustedScreenPosition(previousTexCoord);
			previousVl = PreviousFrameTex.Sample(PreviousFrameSampler, previousScreenPosition).x;
			float previousDepth = PreviousDepthTex.Sample(PreviousDepthSampler, previousScreenPosition).x;
			if (isfinite(previousVl) && previousVl >= 0.0f && isfinite(previousDepth) &&
				previousDepth >= 0.0f && previousDepth < 0.999998f) {
				float depthDelta = abs(depth - previousDepth);
				float depthTolerance = max(0.0005f, depth * 0.003f);
				float depthConfidence = 1.0f - smoothstep(depthTolerance, depthTolerance * 4.0f, depthDelta);
				// Large per-pixel movement and abrupt light-volume changes need less
				// old radiance. This keeps slow motion stable but rejects camera pans,
				// disocclusions, and rapidly changing fog/cloud illumination.
				float motionConfidence = 1.0f - smoothstep(0.001f, 0.012f, length(motionVector));
				float radianceDelta = abs(adjustedVl - previousVl);
				float radianceTolerance = max(0.06f, max(adjustedVl, previousVl) * 0.50f);
				float radianceConfidence = 1.0f - smoothstep(radianceTolerance, radianceTolerance * 3.0f, radianceDelta);
				temporalContribution = temporalResponse * depthConfidence * motionConfidence * radianceConfidence;
			} else {
				previousVl = adjustedVl;
			}
		}
		psout.VL = lerp(adjustedVl, previousVl, saturate(temporalContribution));
	} else {
		psout.VL = adjustedVl;
	}

	return psout;
}
#endif
