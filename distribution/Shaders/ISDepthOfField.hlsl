#include "Common/DummyVSTexCoord.hlsl"
#include "Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 Color: SV_Target0;
};

#if defined(PSHADER)
SamplerState ImageSampler : register(s0);
SamplerState BlurredSampler : register(s1);
SamplerState DepthSampler : register(s2);
SamplerState AvgDepthSampler : register(s3);
SamplerState MaskSampler : register(s4);

Texture2D<float4> ImageTex : register(t0);
Texture2D<float4> BlurredTex : register(t1);
Texture2D<float> DepthTex : register(t2);
Texture2D<float4> AvgDepthTex : register(t3);
Texture2D<float4> MaskTex : register(t4);

cbuffer PerGeometry : register(b2)
{
	float4 invScreenRes : packoffset(c0);  // inverse render target width and height in xy
	float4 params : packoffset(c1);        // DOF near range in x, far range in y
	float4 params2 : packoffset(c2);       // DOF near blur in x, far blur in w
	float4 params3 : packoffset(c3);       // 1 / (far - near) in z, near / (far - near) in w
	float4 params4 : packoffset(c4);
	float4 params5 : packoffset(c5);
	float4 params6 : packoffset(c6);
	float4 params7 : packoffset(c7);
};

void CheckOffsetDepth(float2 center, float2 offset, inout float crossSection,
	inout float totalDepth)
{
	float depth = DepthTex.Sample(DepthSampler, FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(invScreenRes.xy * offset + center));

	float crossSectionDelta = 0;
	if (depth > 0.999998987) {
		crossSectionDelta = (1. / 9.);
	}
	crossSection += crossSectionDelta;
	totalDepth += depth;
}

float GetFinalDepth(float depth, float near, float far)
{
	return (2 * near * far) / ((far + near) - (depth * 2 - 1) * (far - near));
}

#ifndef USE_PIXL_CINEMATIC_DOF
#	define USE_PIXL_CINEMATIC_DOF 1
#endif

#define PIXL_DOF_STABLE_GATHER_V2 1

#if USE_PIXL_CINEMATIC_DOF
static const float2 PixlDofDisk[12] = {
	float2(0.0000f, 0.0000f), float2(0.5278f, 0.0859f), float2(-0.0401f, 0.5365f),
	float2(-0.5542f, -0.0420f), float2(0.1004f, -0.5617f), float2(0.7799f, 0.4098f),
	float2(-0.3630f, 0.8147f), float2(-0.8280f, -0.3376f), float2(0.3292f, -0.8409f),
	float2(0.9583f, -0.1607f), float2(-0.0998f, 0.9748f), float2(-0.9506f, 0.1887f)
};

float PixlDofLuminance(float3 color)
{
	return dot(max(color, 0.0f), float3(0.2126f, 0.7152f, 0.0722f));
}

float PixlDofLinearDepth(float rawDepth, float4 dofParams, float4 depthParams)
{
	if (rawDepth <= 1e-5f || rawDepth > 0.999998987f)
		return 0.0f;
	if (rawDepth <= 0.01f)
		return GetFinalDepth(100.0f * rawDepth, depthParams.x, depthParams.y);
	return GetFinalDepth(1.01f * rawDepth - 0.01f, dofParams.z, dofParams.w);
}

float PixlDofSignedCoC(float linearDepth, float focusDistance, float2 dofBlurRange)
{
	if (linearDepth <= 0.0f)
		return 0.0f;
	float range = linearDepth < focusDistance ? max(dofBlurRange.y, 1e-4f) : max(dofBlurRange.y, 1e-4f);
	return clamp((linearDepth - focusDistance) / range, -1.0f, 1.0f);
}

float2 PixlShapeAperture(float2 diskOffset, float2 screenUV)
{
	float anamorphic = max(SharedData::postProcessSettings.DofAnamorphicRatio, 0.25f);
	float apertureScale = sqrt(anamorphic);
	diskOffset *= float2(apertureScale, rcp(apertureScale));

	float2 screenVector = screenUV * 2.0f - 1.0f;
	float edge = saturate(length(screenVector));
	float screenLength = max(length(screenVector), 1e-4f);
	float2 radial = screenVector / screenLength;
	float radialProjection = dot(diskOffset, radial);
	diskOffset -= radial * radialProjection *
		(saturate(SharedData::postProcessSettings.DofCatEye) * edge * 0.65f);
	return diskOffset;
}

float3 PixlGatherBokeh(float2 centerUV, float centerRawDepth, float centerLinearDepth,
	float focusDistance, float2 dofBlurRange, float blurFactor, float4 dofParams)
{
	float centerCoC = PixlDofSignedCoC(centerLinearDepth, focusDistance, dofBlurRange);
	// PIXL_DOF_STABLE_GATHER_V2: enhancement stays conservative because Skyrim's
	// native pass already supplies the base blur. Gather the sharp source to form
	// bokeh detail without recursively blurring an already-blurred texture.
	float radiusPixels = lerp(0.50f, 3.25f, saturate(blurFactor)) *
		max(SharedData::postProcessSettings.DofBokehRadius, 0.5f);
	float edgeProtection = max(SharedData::postProcessSettings.DofFocusEdgeProtection, 0.0f);
	float foregroundCoverage = max(SharedData::postProcessSettings.DofForegroundCoverage, 0.0f);
	float highlightResponse = saturate(SharedData::postProcessSettings.DofHighlightResponse);

	float3 sum = 0.0f;
	float weightSum = 0.0f;
	uint sampleCount = 6u + min(SharedData::postProcessSettings.DofQuality, 3u) * 2u;
	[loop] for (uint sampleIndex = 0u; sampleIndex < sampleCount; ++sampleIndex) {
		float2 aperture = PixlShapeAperture(PixlDofDisk[sampleIndex], centerUV);
		float2 sampleUV = saturate(centerUV + aperture * invScreenRes.xy * radiusPixels);
		float2 adjustedSampleUV = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(sampleUV);
		float sampleRawDepth = DepthTex.SampleLevel(DepthSampler, adjustedSampleUV, 0.0f);
		float sampleLinearDepth = PixlDofLinearDepth(sampleRawDepth, dofParams, params3);
		float sampleCoC = PixlDofSignedCoC(sampleLinearDepth, focusDistance, dofBlurRange);

		float relativeDepth = abs(sampleLinearDepth - centerLinearDepth) /
			max(max(centerLinearDepth, sampleLinearDepth), 1.0f);
		float sameLayer = centerCoC * sampleCoC >= 0.0f ? 1.0f : 0.0f;
		float depthWeight = exp2(-relativeDepth * (8.0f + 40.0f * edgeProtection));
		// A near defocused sample may cover a farther receiver; the reverse would
		// leak background colour through the foreground silhouette.
		float foregroundSpread = sampleCoC < centerCoC ? saturate(-sampleCoC) * foregroundCoverage : 0.0f;
		float layerWeight = sameLayer > 0.5f ? depthWeight : max(depthWeight * 0.08f, foregroundSpread);
		if (sampleLinearDepth <= 0.0f)
			layerWeight *= centerLinearDepth <= 0.0f ? 1.0f : 0.05f;

		float3 sampleColor = ImageTex.SampleLevel(ImageSampler, adjustedSampleUV, 0.0f).xyz;
		float luminance = PixlDofLuminance(sampleColor);
		float highlight = 1.0f + highlightResponse * saturate((luminance - 0.8f) / (luminance + 1.0f)) * 1.5f;
		float cocWeight = 0.20f + 0.80f * max(abs(sampleCoC), blurFactor);
		float weight = max(layerWeight * cocWeight * highlight, 1e-4f);
		sum += sampleColor * weight;
		weightSum += weight;
	}

	float3 bokeh = sum / max(weightSum, 1e-4f);
	// Bound the luma-weighted aperture so a single HDR texel cannot become a firefly.
	float bokehLum = PixlDofLuminance(bokeh);
	float3 centerBlur = BlurredTex.Sample(BlurredSampler,
		FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(centerUV)).xyz;
	float centerLum = PixlDofLuminance(centerBlur);
	float maxLum = max(centerLum * (2.0f + 2.0f * highlightResponse) + 0.25f, 1.0f);
	if (bokehLum > maxLum)
		bokeh *= maxLum / max(bokehLum, 1e-5f);
	return max(bokeh, 0.0f);
}
#endif

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	float2 adjustedTexCoord = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(input.TexCoord);

	float3 imageColor = ImageTex.Sample(ImageSampler, adjustedTexCoord).xyz;
	float3 blurColor = BlurredTex.Sample(BlurredSampler, adjustedTexCoord).xyz;

	float mask = 1;
	float4 dofParams = params;
	float4 dofParams2 = params2;
#	if defined(MASKED)
	mask = MaskTex.Sample(ImageSampler, adjustedTexCoord).x;
	dofParams = lerp(params, params6, mask);
	dofParams2 = lerp(params2, params7, mask);
#	endif

	float2 dofBlurRange = float2(dofParams2.x, dofParams.x);
	float focusDistance = dofParams.y;

#	if !defined(MASKED)
	if (params3.z > 0) {
		focusDistance = AvgDepthTex.Sample(AvgDepthSampler, 0).x;
		float depthFactor = saturate(focusDistance * params3.z - params3.w);
		dofBlurRange = lerp(float2(params2.x, params.x), float2(params2.w, params.y), depthFactor);
	}
#	endif

	float depthCC = DepthTex.Sample(DepthSampler, adjustedTexCoord);

	float crossSection = 0;
	float avgDepth = depthCC;
	bool isTooDeep = false;
	if (dofParams2.w != 0 && depthCC > 0.999998987) {
		crossSection = 1. / 9.;
		float totalDepth = depthCC;
		CheckOffsetDepth(input.TexCoord, float2(-3, -3), crossSection, totalDepth);
		CheckOffsetDepth(input.TexCoord, float2(-3, 0), crossSection, totalDepth);
		CheckOffsetDepth(input.TexCoord, float2(-3, 3), crossSection, totalDepth);
		CheckOffsetDepth(input.TexCoord, float2(3, -3), crossSection, totalDepth);
		CheckOffsetDepth(input.TexCoord, float2(3, 0), crossSection, totalDepth);
		CheckOffsetDepth(input.TexCoord, float2(3, 3), crossSection, totalDepth);
		CheckOffsetDepth(input.TexCoord, float2(0, -3), crossSection, totalDepth);
		CheckOffsetDepth(input.TexCoord, float2(0, 3), crossSection, totalDepth);

		avgDepth = totalDepth / 9;
		isTooDeep = avgDepth > 0.999998987;
	}

	float blurFactor = 0;
	float finalDepth = avgDepth;
	if (!isTooDeep && avgDepth > 1e-5) {
		float depth, near, far;
		if (avgDepth <= 0.01) {
			depth = 100 * avgDepth;
			near = params3.x;
			far = params3.y;
		} else {
			depth = 1.01 * avgDepth - 0.01;
			near = dofParams.z;
			far = dofParams.w;
		}
		finalDepth = GetFinalDepth(depth, near, far);

		float dofStrength = 0;
#	if defined(DISTANT)
		dofStrength = (finalDepth - focusDistance) / dofBlurRange.y;
#	else
		if ((focusDistance > finalDepth || mask == 0) && dofParams2.y != 0) {
			dofStrength = (focusDistance - finalDepth) / dofBlurRange.y;
		} else if (finalDepth > focusDistance && dofParams2.z != 0) {
			dofStrength = (finalDepth - focusDistance) / dofBlurRange.y;
		}
#	endif

		blurFactor = saturate(dofStrength) * (dofBlurRange.x * (1 - 0.5 * crossSection));
	}

	float3 finalColor = lerp(imageColor, blurColor, blurFactor);
#	if USE_PIXL_CINEMATIC_DOF
	[branch] if (SharedData::postProcessSettings.EnableEnhancedDepthOfField != 0 && blurFactor > 1e-4f) {
		float3 bokehColor = PixlGatherBokeh(
			input.TexCoord, depthCC, finalDepth, focusDistance, dofBlurRange, blurFactor, dofParams);
		// Preserve Skyrim's authored base DOF and use PIXL as a bounded optical
		// enhancement rather than replacing the whole pass. This removes the old
		// double-blur / miniature-look failure while retaining bokeh highlights.
		finalColor = lerp(finalColor, bokehColor, saturate(blurFactor * 0.62f));
	}
#	endif
#	if defined(FOGGED)
	float fogFactor = (params4.w * saturate((finalDepth - params5.y) / (params5.x - params5.y))) * mask;
	finalColor = lerp(finalColor, params4.xyz, fogFactor);
#	endif

	psout.Color = float4(finalColor, 1);

	return psout;
}
#endif
