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

#define PIXL_DOF_OWNED_APERTURE_V3 1

#if USE_PIXL_CINEMATIC_DOF
// Vogel aperture: concentric rings alias into visible spokes while this
// low-discrepancy disk remains stable at every Camera quality sample budget.
static const float2 PixlDofDisk[20] = {
	float2( 0.158114f,  0.000000f), float2(-0.201937f,  0.184991f),
	float2( 0.030910f, -0.352200f), float2( 0.254528f,  0.331987f),
	float2(-0.467091f, -0.082622f), float2( 0.442469f, -0.281463f),
	float2(-0.147997f,  0.550542f), float2(-0.282247f, -0.543449f),
	float2( 0.612363f,  0.223634f), float2(-0.637061f,  0.262970f),
	float2( 0.307106f, -0.656267f), float2( 0.226943f,  0.723531f),
	float2(-0.684010f, -0.396397f), float2( 0.802421f, -0.176410f),
	float2(-0.489705f,  0.696555f), float2(-0.113133f, -0.873041f),
	float2( 0.694527f,  0.585348f), float2(-0.934616f,  0.038649f),
	float2( 0.681730f, -0.678413f), float2(-0.045610f,  0.986367f)
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

float PixlDofSignedCoC(float linearDepth, bool isSky, float focusDistance, float focusRange)
{
	if (isSky)
		return 1.0f;
	if (linearDepth <= 0.0f)
		return 0.0f;
	float range = max(focusRange, 1e-4f);
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

float3 PixlGatherBokeh(float2 centerUV, float centerLinearDepth, bool centerIsSky,
	float focusDistance, float focusRange, float blurFactor, float4 dofParams)
{
	float centerCoC = PixlDofSignedCoC(centerLinearDepth, centerIsSky, focusDistance, focusRange);
	// This is the complete visible aperture, not an enhancement over Skyrim's
	// preblur. The wider photographic footprint is therefore sampled exclusively
	// from the sharp source and remains stable under dynamic resolution.
	float shapedBlur = blurFactor * blurFactor * (3.0f - 2.0f * blurFactor);
	float radiusPixels = lerp(0.75f, 18.0f, shapedBlur) *
		max(SharedData::postProcessSettings.DofBokehRadius, 0.5f);
	float edgeProtection = max(SharedData::postProcessSettings.DofFocusEdgeProtection, 0.0f);
	float foregroundCoverage = max(SharedData::postProcessSettings.DofForegroundCoverage, 0.0f);
	float highlightResponse = saturate(SharedData::postProcessSettings.DofHighlightResponse);

	float3 sum = 0.0f;
	float weightSum = 0.0f;
	uint sampleCount = 8u + min(SharedData::postProcessSettings.DofQuality, 3u) * 4u;
	[loop] for (uint sampleIndex = 0u; sampleIndex < sampleCount; ++sampleIndex) {
		float2 aperture = PixlShapeAperture(PixlDofDisk[sampleIndex], centerUV);
		float2 sampleUV = saturate(centerUV + aperture * invScreenRes.xy * radiusPixels);
		float2 adjustedSampleUV = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(sampleUV);
		float sampleRawDepth = DepthTex.SampleLevel(DepthSampler, adjustedSampleUV, 0.0f);
		float sampleLinearDepth = PixlDofLinearDepth(sampleRawDepth, dofParams, params3);
		bool sampleIsSky = sampleRawDepth > 0.999998987f;
		float sampleCoC = PixlDofSignedCoC(sampleLinearDepth, sampleIsSky, focusDistance, focusRange);

		float relativeDepth = (centerIsSky || sampleIsSky)
			? (centerIsSky == sampleIsSky ? 0.0f : 1.0f)
			: abs(sampleLinearDepth - centerLinearDepth) /
				max(max(centerLinearDepth, sampleLinearDepth), 1.0f);
		float sameLayer = centerCoC * sampleCoC >= 0.0f ? 1.0f : 0.0f;
		float depthWeight = exp2(-relativeDepth * (6.0f + 42.0f * edgeProtection));
		// A near defocused sample may cover a farther receiver; the reverse would
		// leak background colour through the foreground silhouette.
		float foregroundSpread = sampleCoC < 0.0f && centerCoC >= 0.0f
			? saturate(-sampleCoC) * foregroundCoverage
			: 0.0f;
		float layerWeight = sameLayer > 0.5f ? depthWeight : max(depthWeight * 0.025f, foregroundSpread);
		if (sampleLinearDepth <= 0.0f)
			layerWeight *= centerLinearDepth <= 0.0f ? 1.0f : 0.05f;

		float3 sampleColor = ImageTex.SampleLevel(ImageSampler, adjustedSampleUV, 0.0f).xyz;
		float luminance = PixlDofLuminance(sampleColor);
		float highlight = 1.0f + highlightResponse * saturate((luminance - 0.65f) / (luminance + 1.0f)) * 1.35f;
		float cocWeight = 0.15f + 0.85f * max(abs(sampleCoC), blurFactor);
		float weight = max(layerWeight * cocWeight * highlight, 1e-4f);
		sum += sampleColor * weight;
		weightSum += weight;
	}

	float3 bokeh = sum / max(weightSum, 1e-4f);
	// Bound the luma-weighted aperture without borrowing Skyrim's blurred image.
	// The floor deliberately leaves headroom for convincing point-light bokeh.
	float bokehLum = PixlDofLuminance(bokeh);
	float3 centerColor = ImageTex.SampleLevel(ImageSampler,
		FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(centerUV), 0.0f).xyz;
	float centerLum = PixlDofLuminance(centerColor);
	float maxLum = max(centerLum * (2.0f + 2.0f * highlightResponse) + 0.5f,
		2.0f + 3.0f * highlightResponse);
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
	[branch] if (SharedData::postProcessSettings.EnableEnhancedDepthOfField != 0) {
		// HDROutputCS is the sole PIXL lens owner. Keep this scheduled Bethesda
		// integration pass sharp so native parameters cannot apply a differently
		// focused blur before the final compositor.
		finalColor = imageColor;
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
