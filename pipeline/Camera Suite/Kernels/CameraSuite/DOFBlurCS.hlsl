// PIXL Renderer - adaptive near/far bokeh and bilateral reconstruction.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional permissions are described in the repository EXCEPTIONS.md.

#include "Common/Color.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"
#include "CameraSuite/PhysicalCameraCommon.hlsli"
#include "CameraSuite/DofControl.hlsli"

Texture2D<float4> SceneTex : register(t0);
Texture2D<float> CoCTex : register(t1);
Texture2D<float4> HalfSceneTex : register(t2);
Texture2D<float4> TileTex : register(t3);
SamplerState LinearClampSampler : register(s0);
RWTexture2D<float4> BlurOut : register(u0);

#if DOF_NEAR
#define DOF_EMPTY_COVERAGE 0.0f
#else
#define DOF_EMPTY_COVERAGE 1.0f
#endif

float3 LoadLinear(float2 uv)
{
	float3 color = HalfSceneTex.SampleLevel(LinearClampSampler, saturate(uv), 0.0f).rgb;
	// DOFHalfDownsampleCS always writes scene-linear colour, regardless of the
	// encoding of SceneTex. Decoding this texture a second time on the SDR path
	// was the primary source of the dark/spotlight appearance.
	return max(color, 0.0f);
}

float ReadGatherDepth(float2 uv)
{
	float raw = SharedData::DepthTexture.SampleLevel(LinearClampSampler, saturate(uv), 0.0f).x;
	if (!isfinite(raw) || raw >= 0.999998f)
		return 200000.0f;
	float depth = SharedData::GetScreenDepth(raw);
	return isfinite(depth) && depth > 0.0f ? depth : 200000.0f;
}

float DepthEdgeWeight(float centerDepth, float sampleDepth)
{
	if (centerDepth >= 199999.0f)
		return sampleDepth >= 199999.0f ? 1.0f : 0.0f;
	if (sampleDepth >= 199999.0f)
		return centerDepth >= 199999.0f ? 1.0f : 0.0f;
	float relativeDelta = abs(sampleDepth - centerDepth) / max(centerDepth, 1.0f);
	// Keep the gather from crossing hard silhouettes. A small tolerance covers
	// depth quantisation and foliage/grass without producing a visible halo.
	return 1.0f - smoothstep(0.10f, 0.42f, relativeDelta);
}

float BokehApertureWeight(float2 offset, float2 uv)
{
	// Analytic regular aperture. Curvature continuously rounds the polygon into
	// a circular iris, while rotation and blade count now affect the actual
	// sampling support rather than being UI-only controls.
	const float PI = 3.14159265359f;
	float bladeCount = clamp(round(dofControlApertureBlades), 3.0f, 12.0f);
	float angle = atan2(offset.y, offset.x) - dofControlApertureRotation;
	float sector = 2.0f * PI / bladeCount;
	float localAngle = abs(frac(angle / sector + 0.5f) * sector - 0.5f * sector);
	float polygonRadius = cos(PI / bladeCount) / max(cos(localAngle), 1.0e-3f);
	float apertureRadius = lerp(polygonRadius, 1.0f, saturate(dofControlBladeCurvature));
	float radial = length(offset);
	float aperture = 1.0f - smoothstep(apertureRadius * 0.86f, apertureRadius * 1.04f, radial);
	float edge = saturate(length(offset));
	float2 optical = (uv - 0.5f) * float2(dofControlRenderWidth * dofControlInvRenderHeight, 1.0f);
	float pupilClip = saturate(length(optical) * 0.72f);
	float catEye = 1.0f - dofControlCatEye * pupilClip * smoothstep(0.25f, 1.0f, edge) * abs(offset.x);
	return aperture * catEye;
}

float3 HighlightLift(float3 color)
{
	// Preserve bright HDR points as a restrained bokeh contribution. This is
	// intentionally thresholded instead of adding a general bloom pass, so the
	// focused image and exposure remain unchanged.
	float luminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
	float threshold = max(bloomThreshold, 1.0f);
	float highlight = smoothstep(threshold, threshold * 2.5f, luminance) * saturate(dofControlHighlightResponse);
	return color * highlight;
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID)
{
	uint width, height;
	BlurOut.GetDimensions(width, height);
	if (dispatchID.x >= width || dispatchID.y >= height)
		return;
	float2 uv = (float2(dispatchID.xy) + 0.5f) / float2(width, height);
	float center = CoCTex.SampleLevel(LinearClampSampler, uv, 0.0f);
	float centerDepth = ReadGatherDepth(uv);
	uint tileWidth, tileHeight;
	TileTex.GetDimensions(tileWidth, tileHeight);
	float4 tile = TileTex.Load(int3(min(uint2(uv * float2(tileWidth, tileHeight)), uint2(tileWidth, tileHeight) - 1u), 0));
	uint tileFlags = (uint)tile.z;
	float requested = saturate(abs(center) / max(dofControlMaxCoCPixels, 1.0f));
#if DOF_NEAR
	if ((tileFlags & 1u) == 0u) {
		BlurOut[dispatchID.xy] = float4(LoadLinear(uv), DOF_EMPTY_COVERAGE);
		return;
	}
	if (center >= 0.0f) {
		BlurOut[dispatchID.xy] = float4(LoadLinear(uv), DOF_EMPTY_COVERAGE);
		return;
	}
#else
	if ((tileFlags & 2u) == 0u) {
		BlurOut[dispatchID.xy] = float4(LoadLinear(uv), DOF_EMPTY_COVERAGE);
		return;
	}
	if (center <= 0.0f) {
		BlurOut[dispatchID.xy] = float4(LoadLinear(uv), DOF_EMPTY_COVERAGE);
		return;
	}
#endif
	float2 pixel = 1.0f / float2(width, height);
	// Physical CoC is already a radius in full-resolution pixels. The 0.5 factor
	// converts it to this half-resolution target; only the artistic gameplay
	// path uses the legacy bokeh-radius multiplier.
	float artisticRadius = DofPhysicalLensEnabled() ? 1.0f : max(dofControlBokehRadius, 0.5f);
	float radius = max(1.0f, abs(center) * 0.50f) * artisticRadius;
	const float2 taps[16] = {
		float2(1, 0), float2(0.7071f, 0.7071f), float2(0, 1), float2(-0.7071f, 0.7071f),
		float2(-1, 0), float2(-0.7071f, -0.7071f), float2(0, -1), float2(0.7071f, -0.7071f),
		float2(0.9239f, 0.3827f), float2(0.3827f, 0.9239f), float2(-0.3827f, 0.9239f), float2(-0.9239f, 0.3827f),
		float2(-0.9239f, -0.3827f), float2(-0.3827f, -0.9239f), float2(0.3827f, -0.9239f), float2(0.9239f, -0.3827f)
	};
	uint qualityTier = min((uint)(clamp(dofControlQuality, 0.0f, 3.0f) + 0.5f), 3u);
	uint sampleCount = qualityTier == 3u ? 48u : (qualityTier == 2u ? 16u : (qualityTier == 1u ? 10u : 6u));
	// Keep a centre contribution for stability without allowing the original
	// reconstructed pixel to dominate the far bokeh and retain card silhouettes.
#if DOF_NEAR
	const float centerWeight = 1.25f;
#else
	const float centerWeight = 0.65f;
#endif
	float3 result = LoadLinear(uv) * centerWeight;
	float3 highlightResult = 0.0f.xxx;
#if DOF_NEAR
	highlightResult = HighlightLift(LoadLinear(uv)) * centerWeight;
#endif
	float weight = centerWeight;
	float coverage = 0.0f;
	[loop]
	for (uint i = 0; i < 48; ++i) {
		if (i >= sampleCount)
			break;
		// Two interleaved rings reduce the visible spoke/card pattern of the old
		// eight-tap cross while keeping the blur gather deterministic. Cinematic
		// adds two rotated copies of the complete High kernel (48 vs 16 samples).
		uint tapIndex = sampleCount < 16u ? (i * 16u) / sampleCount : i % 16u;
		uint ringGroup = i / 16u;
		float ringScale = tapIndex < 8u ? 0.48f : 1.0f;
		float2 apertureOffset = taps[tapIndex] * ringScale;
		float rotation = (float)ringGroup * 2.39996323f;
		float rotationSin;
		float rotationCos;
		sincos(rotation, rotationSin, rotationCos);
		apertureOffset = float2(
			apertureOffset.x * rotationCos - apertureOffset.y * rotationSin,
			apertureOffset.x * rotationSin + apertureOffset.y * rotationCos);
		float apertureWeight = BokehApertureWeight(apertureOffset, uv);
		float2 sampleUV = uv + float2(apertureOffset.x * max(dofControlAnamorphicRatio, 0.5f), apertureOffset.y) * pixel * radius;
		float sampleCoC = CoCTex.SampleLevel(LinearClampSampler, saturate(sampleUV), 0.0f);
		float sampleDepth = ReadGatherDepth(sampleUV);
#if DOF_NEAR
		float support = saturate(-sampleCoC / max(dofControlMaxCoCPixels, 1.0f));
#else
		float support = saturate(sampleCoC / max(dofControlMaxCoCPixels, 1.0f));
#endif
		float tapWeight = smoothstep(0.02f, 0.35f, support) * apertureWeight * (tapIndex < 8u ? 0.82f : 0.68f);
#if DOF_NEAR
		float depthWeight = DepthEdgeWeight(centerDepth, sampleDepth);
		// A near layer must not import distant sky/mountains into a foreground
		// silhouette.
		if (sampleDepth > centerDepth * 1.12f && centerDepth < 199999.0f)
			depthWeight = 0.0f;
#else
		// Signed CoC already rejects focused and foreground occluders. Raw depth
		// rejection here incorrectly separated two blurred background surfaces and
		// preserved mountain/tree cut-outs against the sky.
		float depthWeight = sampleCoC > 0.0f ? 1.0f : 0.0f;
#endif
		tapWeight *= depthWeight;
		float3 sampleColor = LoadLinear(sampleUV);
		result += sampleColor * tapWeight;
	#if DOF_NEAR
		highlightResult += HighlightLift(sampleColor) * tapWeight;
	#endif
		coverage += tapWeight * support;
		weight += tapWeight;
	}
	float3 resolved = result / max(weight, 1.0e-4f);
	float3 highlights = highlightResult / max(weight, 1.0e-4f);
	float nearCoverage = saturate(coverage / max(weight, 1.0e-4f) * dofControlForegroundCoverage);
	BlurOut[dispatchID.xy] = float4(resolved + highlights * 0.35f, nearCoverage);
}
