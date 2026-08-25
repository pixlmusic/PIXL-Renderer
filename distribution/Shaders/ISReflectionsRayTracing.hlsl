#include "Common/DummyVSTexCoord.hlsl"
#include "Common/FrameBuffer.hlsli"
#include "Common/GBuffer.hlsli"
#include "Common/MotionBlur.hlsli"
#include "Common/SharedData.hlsli"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 Color: SV_Target0;
};

#if defined(PSHADER)
SamplerState NormalSampler : register(s0);
SamplerState ColorSampler : register(s1);
SamplerState DepthSampler : register(s2);
SamplerState AlphaSampler : register(s3);

Texture2D<float4> NormalTex : register(t0);
Texture2D<float4> ColorTex : register(t1);
Texture2D<float4> DepthTex : register(t2);
Texture2D<float4> AlphaTex : register(t3);

cbuffer PerGeometry : register(b2)
{
	float4 SSRParams : packoffset(c0);  // fReflectionRayThickness in x, fReflectionMarchingRadius in y, fAlphaWeight in z, 1 / fReflectionMarchingRadius in w
	float3 DefaultNormal : packoffset(c1);
};

static const int iterations = 64.0;
static const int binaryIterations = ceil(log2(iterations));

static const float rayLength = 1.0;

#ifndef USE_PIXL_ENHANCED_SSR
#	define USE_PIXL_ENHANCED_SSR 1
#endif

float2 ConvertRaySample(float2 raySample)
{
	return FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(raySample);
}

float2 ConvertRaySamplePrevious(float2 raySample)
{
	return FrameBuffer::GetPreviousDynamicResolutionAdjustedScreenPosition(raySample);
}

float4 GetReflectionColorLegacy(
	float3 projReflectionDirection,
	float3 projPosition)
{
	float3 prevRaySample;
	float3 raySample = projPosition;

	for (int i = 0; i < iterations; i++) {
		prevRaySample = raySample;
		raySample = projPosition + (float(i) / float(iterations)) * projReflectionDirection;

		float2 sampleUV = raySample.xy;

		if (FrameBuffer::IsOutsideFrame(sampleUV))
			return 0.0;

		float iterationDepth = DepthTex.SampleLevel(DepthSampler, ConvertRaySample(sampleUV), 0).x;

		if (saturate((raySample.z - iterationDepth) / SSRParams.y) > 0.0) {
			float3 binaryMinRaySample = prevRaySample;
			float3 binaryMaxRaySample = raySample;
			float3 binaryRaySample = raySample;
			float depthThicknessFactor;

			for (int k = 0; k < binaryIterations; k++) {
				binaryRaySample = lerp(binaryMinRaySample, binaryMaxRaySample, 0.5);

				sampleUV = binaryRaySample.xy;
				iterationDepth = DepthTex.SampleLevel(DepthSampler, ConvertRaySample(sampleUV), 0).x;

				// Compute expected depth vs actual depth
				depthThicknessFactor = 1.0 - saturate(abs(binaryRaySample.z - iterationDepth) / SSRParams.y);

				if (iterationDepth < binaryRaySample.z)
					binaryMaxRaySample = binaryRaySample;
				else
					binaryMinRaySample = binaryRaySample;
			}

			// Fade based on ray length
			float ssrMarchingRadiusFadeFactor = 1.0 - saturate(length(binaryRaySample - projPosition) / rayLength);

			float2 uvResultScreenCenterOffset = binaryRaySample.xy - 0.5;

			float2 centerDistance = abs(uvResultScreenCenterOffset.xy * 2.0);

			// Fade out around screen edges
			float centerDistanceFadeFactorX = smoothstep(0.0, 0.1, saturate(1.0 - centerDistance.x));
			float centerDistanceFadeFactorY = smoothstep(0.0, 0.5, saturate(1.0 - centerDistance.y));

			float fadeFactor = depthThicknessFactor * ssrMarchingRadiusFadeFactor * centerDistanceFadeFactorX * centerDistanceFadeFactorY;

			if (fadeFactor > 0.0) {
				float2 finalSampleUV = binaryRaySample.xy;

				float3 color = ColorTex.SampleLevel(ColorSampler, ConvertRaySample(finalSampleUV), 0).xyz;

				// Final sample to world-space
				float4 positionWS = float4(float2(finalSampleUV.x, 1.0 - finalSampleUV.y) * 2.0 - 1.0, iterationDepth, 1.0);
				positionWS = mul(FrameBuffer::CameraViewProjInverse, positionWS);
				positionWS.xyz = positionWS.xyz / positionWS.w;
				positionWS.w = 1.0;

				// Compute camera motion vector
				float2 cameraMotionVector = MotionBlur::GetSSMotionVector(positionWS, positionWS);

				// Reproject alpha from previous frame
				float2 reprojectedRaySample = finalSampleUV + cameraMotionVector;
				float4 alpha = 0.0;

				// Check that the reprojected data is within the frame
				if (!FrameBuffer::IsOutsideFrame(reprojectedRaySample.xy))
					alpha = float4(AlphaTex.SampleLevel(AlphaSampler, ConvertRaySamplePrevious(reprojectedRaySample.xy), 0).xyz, 1.0);

				float3 reflectionColor = color + SSRParams.z * alpha.xyz * alpha.w;
				return float4(reflectionColor, fadeFactor);
			}

			return 0.0;
		}
	}

	return 0.0;
}

#if USE_PIXL_ENHANCED_SSR
float4 GetReflectionColorEnhanced(float3 projReflectionDirection, float3 projPosition, float3 viewReflectionDirection)
{
	static const int coarseIterations = 48;
	static const int refineIterations = 6;
	float traceScale = clamp(SharedData::waterOpticsSettings.SSRDistanceScale, 0.25f, 1.5f);
	float3 rayDirection = projReflectionDirection * traceScale;
	float3 previousRaySample = projPosition;
	float previousSceneDepth = DepthTex.SampleLevel(DepthSampler, ConvertRaySample(projPosition.xy), 0).x;
	float previousDepthDelta = projPosition.z - previousSceneDepth;
	float thickness = clamp(abs(SSRParams.x) * SharedData::waterOpticsSettings.SSRThicknessScale, 1e-5f, 0.08f);

	[loop] for (int i = 0; i < coarseIterations; ++i) {
		float linearT = float(i + 1) / float(coarseIterations);
		// Dense samples near the source suppress self-intersection and thin-object gaps;
		// wider distant steps provide reach without a 64-sample fixed march.
		float warpedT = lerp(linearT, linearT * linearT, 0.55f);
		float3 raySample = projPosition + warpedT * rayDirection;
		float2 sampleUV = raySample.xy;
		if (FrameBuffer::IsOutsideFrame(sampleUV))
			return 0.0f;

		float sceneDepth = DepthTex.SampleLevel(DepthSampler, ConvertRaySample(sampleUV), 0).x;
		float depthDelta = raySample.z - sceneDepth;
		// Require a genuine front-to-back depth crossing. Treating every positive
		// delta as a hit allowed the water surface to intersect itself on the first
		// step and produced detached bright blocks during camera motion.
		bool crossedSurface = depthDelta >= 0.0f && previousDepthDelta < 0.0f;
		if (crossedSurface) {
			float3 binaryMin = previousRaySample;
			float3 binaryMax = raySample;
			float3 binarySample = raySample;
			float hitT = warpedT;

			[unroll] for (int k = 0; k < refineIterations; ++k) {
				binarySample = 0.5f * (binaryMin + binaryMax);
				sceneDepth = DepthTex.SampleLevel(DepthSampler, ConvertRaySample(binarySample.xy), 0).x;
				if (sceneDepth < binarySample.z)
					binaryMax = binarySample;
				else
					binaryMin = binarySample;
			}

			sceneDepth = DepthTex.SampleLevel(DepthSampler, ConvertRaySample(binarySample.xy), 0).x;
			float finalDepthDelta = abs(binarySample.z - sceneDepth);
			float thicknessConfidence = 1.0f - saturate(finalDepthDelta / thickness);
			float4 hitNormalMask = NormalTex.SampleLevel(NormalSampler, ConvertRaySample(binarySample.xy), 0);
			float3 hitNormalVS = GBuffer::DecodeNormalVanilla(hitNormalMask.xy);
			float hitFacing = saturate(dot(hitNormalVS, -viewReflectionDirection));
			float validity = (sceneDepth < 1.0f - EPSILON_DIVISION) ?
				smoothstep(0.02f, 0.18f, hitFacing) : 0.0f;
			if (thicknessConfidence * validity <= 0.0f)
				return 0.0f;

			float2 edgeDistance = min(binarySample.xy, 1.0f - binarySample.xy);
			float edgeWidth = max(0.025f, 0.10f * SharedData::waterOpticsSettings.SSREdgeFade);
			float edgeConfidence = smoothstep(0.0f, edgeWidth, min(edgeDistance.x, edgeDistance.y));
			float distanceConfidence = saturate(1.0f - hitT);

			float2 traceDirection = binarySample.xy - projPosition.xy;
			float traceLength = max(length(traceDirection), 1e-5f);
			traceDirection /= traceLength;
			float2 filterOffset = traceDirection * SharedData::BufferDim.zw * (1.0f + 2.0f * hitT);
			float3 colorCenter = ColorTex.SampleLevel(ColorSampler, ConvertRaySample(binarySample.xy), 0).xyz;
			float3 colorForward = ColorTex.SampleLevel(ColorSampler, ConvertRaySample(binarySample.xy + filterOffset), 0).xyz;
			float3 colorBackward = ColorTex.SampleLevel(ColorSampler, ConvertRaySample(binarySample.xy - filterOffset), 0).xyz;
			float2 perpendicularOffset = float2(-filterOffset.y, filterOffset.x);
			float3 colorPerpendicularA = ColorTex.SampleLevel(ColorSampler, ConvertRaySample(binarySample.xy + perpendicularOffset), 0).xyz;
			float3 colorPerpendicularB = ColorTex.SampleLevel(ColorSampler, ConvertRaySample(binarySample.xy - perpendicularOffset), 0).xyz;
			float3 color = colorCenter * 0.40f + (colorForward + colorBackward) * 0.20f +
				(colorPerpendicularA + colorPerpendicularB) * 0.10f;
			float3 neighborhoodMin = min(colorCenter, min(min(colorForward, colorBackward), min(colorPerpendicularA, colorPerpendicularB)));
			float3 neighborhoodMax = max(colorCenter, max(max(colorForward, colorBackward), max(colorPerpendicularA, colorPerpendicularB)));
			float3 neighborhoodMean = (colorCenter + colorForward + colorBackward + colorPerpendicularA + colorPerpendicularB) * 0.20f;
			float3 neighborhoodVariance =
				(colorCenter - neighborhoodMean) * (colorCenter - neighborhoodMean) +
				(colorForward - neighborhoodMean) * (colorForward - neighborhoodMean) +
				(colorBackward - neighborhoodMean) * (colorBackward - neighborhoodMean) +
				(colorPerpendicularA - neighborhoodMean) * (colorPerpendicularA - neighborhoodMean) +
				(colorPerpendicularB - neighborhoodMean) * (colorPerpendicularB - neighborhoodMean);
			float3 neighborhoodSigma = sqrt(max(neighborhoodVariance * 0.20f, 1e-6f));
			neighborhoodMin = max(neighborhoodMin, neighborhoodMean - 2.5f * neighborhoodSigma);
			neighborhoodMax = min(neighborhoodMax, neighborhoodMean + 2.5f * neighborhoodSigma);

			float4 positionWS = float4(float2(binarySample.x, 1.0f - binarySample.y) * 2.0f - 1.0f, sceneDepth, 1.0f);
			positionWS = mul(FrameBuffer::CameraViewProjInverse, positionWS);
			positionWS.xyz /= positionWS.w;
			positionWS.w = 1.0f;
			float2 cameraMotionVector = MotionBlur::GetSSMotionVector(positionWS, positionWS);
			float2 reprojectedUV = binarySample.xy + cameraMotionVector;
			float4 history = 0.0f;
			if (!FrameBuffer::IsOutsideFrame(reprojectedUV))
				history = float4(AlphaTex.SampleLevel(AlphaSampler, ConvertRaySamplePrevious(reprojectedUV), 0).xyz, 1.0f);

			float confidence = thicknessConfidence * edgeConfidence * distanceConfidence * validity;
			history.xyz = clamp(history.xyz, neighborhoodMin, neighborhoodMax);
			float motionPixels = length(cameraMotionVector * SharedData::BufferDim.xy);
			float historyStability = exp2(-motionPixels * 0.075f);
			float historyWeight = saturate(SSRParams.z * history.w * historyStability);
			// History is a replacement estimate, not additive light. Normalized
			// temporal blending prevents each accepted frame from increasing energy.
			float3 resolvedColor = lerp(color, history.xyz, historyWeight);
			return float4(max(resolvedColor, 0.0f), confidence);
		}

		previousRaySample = raySample;
		previousDepthDelta = depthDelta;
	}

	return 0.0f;
}
#endif

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;
	psout.Color = 0;

#	ifndef ENABLESSR
	// Disable SSR raymarch
	return psout;
#	endif

	float2 uv = input.TexCoord;
	float2 screenPosition = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(uv);

	[branch] if (NormalTex.Sample(NormalSampler, screenPosition).z <= 0)
	{
		return psout;
	}

	float3 viewNormal = DefaultNormal;

	float depth = DepthTex.SampleLevel(DepthSampler, screenPosition, 0).x;

	float4 positionVS = float4(float2(uv.x, 1.0 - uv.y) * 2.0 - 1.0, depth, 1.0);
	positionVS = mul(FrameBuffer::CameraProjInverse, positionVS);
	positionVS.xyz = positionVS.xyz / positionVS.w;

	float3 viewPosition = positionVS.xyz;
	float3 viewDirection = normalize(viewPosition);

	float3 reflectionDirection = reflect(viewDirection, viewNormal);
	float viewAttenuation = saturate(dot(viewDirection, reflectionDirection));
	[branch] if (viewAttenuation <= 1e-4f)
	{
		return psout;
	}

	float4 reflectionPosition = float4(viewPosition + reflectionDirection, 1.0);
	float4 projReflectionPosition = mul(FrameBuffer::CameraProj, reflectionPosition);
	projReflectionPosition /= projReflectionPosition.w;
	projReflectionPosition.xy = projReflectionPosition.xy * float2(0.5, -0.5) + float2(0.5, 0.5);

	float3 projPosition = float3(uv, depth);
	float3 projReflectionDirection = normalize(projReflectionPosition.xyz - projPosition) * rayLength;

	#if USE_PIXL_ENHANCED_SSR
		if (SharedData::waterOpticsSettings.EnableEnhancedSSR != 0)
			psout.Color = GetReflectionColorEnhanced(projReflectionDirection, projPosition, reflectionDirection);
	else
		psout.Color = GetReflectionColorLegacy(projReflectionDirection, projPosition);
#else
		psout.Color = GetReflectionColorLegacy(projReflectionDirection, projPosition);
	#endif
	psout.Color.w *= smoothstep(0.0f, 0.20f, viewAttenuation);

	return psout;
}
#endif
