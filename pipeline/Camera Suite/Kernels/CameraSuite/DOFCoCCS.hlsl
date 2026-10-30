// PIXL Renderer - physical signed circle-of-confusion generation.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional permissions are described in the repository EXCEPTIONS.md.

#include "Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"
#include "CameraSuite/PhysicalCameraCommon.hlsli"
#include "CameraSuite/DofControl.hlsli"

Texture2D<float4> SceneTex : register(t0);
Texture2D<float4> FocusTex : register(t1);
Texture2D<float> PreviousCoCTex : register(t2);
Texture2D<float2> MotionVectorTex : register(t3);
Texture2D<float4> VolumetricCloudTex : register(t18);
Texture2D<float> VolumetricCloudDepthTex : register(t19);
SamplerState LinearClampSampler : register(s0);
RWTexture2D<float> CoCOut : register(u0);

float ReadLinearDepth(float2 uv, out bool valid, out bool sky)
{
	float raw = SharedData::DepthTexture.SampleLevel(LinearClampSampler, saturate(uv), 0.0f).x;
	if (!isfinite(raw) || raw <= 1.0e-5f) {
		valid = false;
		sky = false;
		return 0.0f;
	}
	// The depth clear value is exactly 1.0. A near-far tolerance also classifies
	// distant LOD mountains and canopy cards as sky, suppressing their CoC and
	// creating a sudden sharp band along the skyline.
	sky = raw >= 1.0f;
	// Give sky pixels a finite optical distance. Including the configured far
	// fade keeps mountains/sky in the same CoC domain instead of creating a hard
	// silhouette where the depth buffer changes to its clear value.
	float skyDepth = max(
		dofControlFocusDistance + max(dofFocusRange, 1.0f) * 4.0f,
		dofControlFarBlurDistance + max(dofFocusRange, 1.0f) * 2.0f);
	float depth = sky ? min(skyDepth, 2000000.0f) : SharedData::GetScreenDepth(raw);
	// Skyrim's LOD terrain can legitimately linearize beyond 200k units. The old
	// release guard classified it as invalid and wrote zero CoC, leaving a sharp
	// mountain strip behind the blurred middle distance. Retain a generous finite
	// guard while allowing the complete exterior depth range to participate.
	valid = isfinite(depth) && depth > 0.0f && depth <= 2000000.0f;
	return valid ? depth : 0.0f;
}

float SignedCircleOfConfusionPixels(float depth)
{
	float focus = max(dofControlFocusDistance, 1.0f);
	if (dofControlFocusMode >= 3.0f)
		focus = max(FocusTex.Load(int3(0, 0, 0)).x, 1.0f);
	float signedCoC;
	if (!DofPhysicalLensEnabled()) {
		float signedDistance = (depth - focus) / max(dofFocusRange, 1.0f);
		signedCoC = sign(signedDistance) * smoothstep(0.30f, 1.85f, abs(signedDistance)) *
			dofControlMaxCoCPixels * saturate(dofControlStrength);
	} else {
		// Thin-lens CoC in millimetres. Skyrim's conventional world scale is
		// 1 unit = 1.428 cm (70 units/metre), matching the standalone reference.
		// The projected sensor-space CoC is converted to vertical render pixels,
		// making f-stop/focal length consistent at 1080p, 1440p and 4K.
		const float GAME_UNIT_TO_MM = 14.28f;
		float focal = max(dofControlFocalLengthMm, 1.0f);
		float zMm = max(depth * GAME_UNIT_TO_MM, focal + 1.0f);
		float focusMm = max(focus * GAME_UNIT_TO_MM, focal + 1.0f);
		float apertureDiameterMm = focal / max(dofControlFStop, 0.7f);
		float cocMm = apertureDiameterMm * focal * (zMm - focusMm) /
			max(zMm * (focusMm - focal), 1.0f);
		float cocPixels = cocMm / max(dofControlSensorHeightMm, 1.0f) * dofControlRenderHeight;
		signedCoC = clamp(cocPixels * saturate(dofControlStrength),
			-dofControlMaxCoCPixels, dofControlMaxCoCPixels);
	}

	// Crysis-style gameplay presentation: retain physical/autofocus CoC, then
	// add a gradual far-distance floor that hides LOD transitions. Gameplay is
	// restrained; Director/Photo/Video reaches the full cinematic radius. This
	// happens before tile classification so it receives the same edge rejection
	// and reconstruction as optical far blur.
	float sceneAltitude = max(FrameBuffer::CameraPosAdjust.z, 0.0f);
	float altitudeGate = DofDirectorPresentation() ? 0.0f : smoothstep(8000.0f, 26000.0f, sceneAltitude);
	float gameplayFarStart = dofControlFarBlurDistance + altitudeGate * 36000.0f;
	float fadeWidth = max(dofFocusRange, 240.0f) + altitudeGate * 16000.0f;
	float distanceFade = smoothstep(
		gameplayFarStart,
		gameplayFarStart + fadeWidth,
		depth);
	float presentationScale = DofDirectorPresentation() ? 1.0f : 0.55f;
	float distanceCoC = distanceFade * dofControlMaxCoCPixels *
		saturate(dofControlStrength) * presentationScale;
	// A zero Far / Distance Blur setting must leave the optical near layer
	// untouched rather than replacing its negative CoC with a dormant far CoC.
	float resolvedCoC = dofControlFarBlurIntensity > 1.0e-3f
		? max(signedCoC, distanceCoC)
		: signedCoC;
	return clamp(resolvedCoC,
		-dofControlMaxCoCPixels, dofControlMaxCoCPixels);
}

float FirstPersonHandProtection(float2 uv, float depth)
{
	// Skyrim has no reliable weapon stencil at this stage. Protect only the
	// characteristic near-camera, lower-side hand zones and feather both the
	// screen and depth boundaries. Protection also requires the surface to sit
	// clearly in front of the resolved focus plane. When autofocus intentionally
	// resolves a close prop, face, or inspection target, its depth approaches the
	// focus plane and the protection releases instead of suppressing macro DOF.
	float nearCamera = 1.0f - smoothstep(105.0f, 285.0f, depth);
	float resolvedFocus = max(dofControlFocusDistance, 1.0f);
	if (dofControlFocusMode >= 3.0f)
		resolvedFocus = max(FocusTex.Load(int3(0, 0, 0)).x, 1.0f);
	float separatedFromFocus = 1.0f - smoothstep(0.48f, 0.78f, depth / resolvedFocus);
	float lowerFrame = smoothstep(0.48f, 0.80f, uv.y);
	float sideRegion = smoothstep(0.09f, 0.27f, abs(uv.x - 0.5f));
	return saturate(nearCamera * separatedFromFocus * lowerFrame * sideRegion);
}

float ThirdPersonSubjectProtection(float2 uv, float depth)
{
	if (!DofThirdPersonView())
		return 0.0f;
	// Skyrim's standard third-person camera keeps the player in the lower-centre
	// composition. Protect the near camera-boom layer independently of autofocus,
	// so the character remains readable while focus resolves into the world ahead.
	// The broad feather avoids a visible oval as animation changes the silhouette.
	float2 subjectSpace = (uv - float2(0.5f, 0.64f)) / float2(0.24f, 0.48f);
	float spatial = 1.0f - smoothstep(0.42f, 1.0f, length(subjectSpace));
	float playerLayer = 1.0f - smoothstep(260.0f, 760.0f, depth);
	return saturate(spatial * playerLayer);
}

float SkyDofProtection(float2 uv)
{
	// Feather sky protection by camera-relative ray elevation. Absolute world
	// height made the same horizon ray change classification at High Hrothgar.
	float2 clipXY = uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
	// Do not unproject z=1 through the combined inverse: Skyrim may use an
	// infinite far projection where that valid point has homogeneous w=0.
	// A finite view-space point gives the same ray and remains well-conditioned.
	float4 viewH = mul(FrameBuffer::CameraProjUnjitteredInverse, float4(clipXY, 0.5f, 1.0f));
	if (!all(isfinite(viewH)) || abs(viewH.w) <= 1.0e-6f)
		return 0.0f;
	float3 viewRay = viewH.xyz / viewH.w;
	float3 ray = mul(FrameBuffer::CameraViewInverse, float4(viewRay, 0.0f)).xyz;
	float rayLengthSq = dot(ray, ray);
	if (!isfinite(rayLengthSq) || rayLengthSq <= 1.0e-10f)
		return 0.0f;
	float3 rayDirection = ray * rsqrt(rayLengthSq);
	const float SKY_DOME_RADIUS = 50000.0f;
	return smoothstep(2500.0f, 28000.0f, rayDirection.z * SKY_DOME_RADIUS);
}

float FeatherSkyCoCAgainstFarGeometry(float2 uv, float skyCoC)
{
	// Sky protection should not change the bokeh radius abruptly at a mountain
	// ridge or tree canopy. Borrow only positive (far-layer) CoC from nearby
	// geometry; foreground leaves remain the responsibility of the near gather.
	const float2 directions[8] = {
		float2(1.0f, 0.0f), float2(-1.0f, 0.0f),
		float2(0.0f, 1.0f), float2(0.0f, -1.0f),
		float2(0.7071f, 0.7071f), float2(-0.7071f, 0.7071f),
		float2(0.7071f, -0.7071f), float2(-0.7071f, -0.7071f)
	};
	float2 pixelStep = float2(dofControlInvRenderWidth, dofControlInvRenderHeight);
	float nearCoCSum = 0.0f;
	float nearCoCWeight = 0.0f;
	[unroll] for (uint i = 0u; i < 8u; ++i) {
		bool depthValid;
		bool sampleIsSky;
		float sampleDepth = ReadLinearDepth(uv + directions[i] * pixelStep, depthValid, sampleIsSky);
		if (depthValid && !sampleIsSky) {
			float sampleCoC = SignedCircleOfConfusionPixels(sampleDepth);
			if (sampleCoC > 0.0f) {
				nearCoCSum += sampleCoC;
				nearCoCWeight += 1.0f;
			}
		}
	}
	if (nearCoCWeight > 0.0f) {
		float edgeCoC = nearCoCSum / nearCoCWeight;
		return lerp(skyCoC, edgeCoC, 0.72f);
	}

	// A second, sparse ring feathers the protected value a few pixels beyond the
	// silhouette so the half-resolution aperture gather does not expose a narrow
	// sharp fringe. The lower weight keeps open sky protected away from the edge.
	float outerCoCSum = 0.0f;
	float outerCoCWeight = 0.0f;
	[unroll] for (uint axis = 0u; axis < 4u; ++axis) {
		float2 direction = axis == 0u ? float2(1.0f, 0.0f) :
			axis == 1u ? float2(-1.0f, 0.0f) :
			axis == 2u ? float2(0.0f, 1.0f) : float2(0.0f, -1.0f);
		bool depthValid;
		bool sampleIsSky;
		float sampleDepth = ReadLinearDepth(uv + direction * pixelStep * 3.0f, depthValid, sampleIsSky);
		if (depthValid && !sampleIsSky) {
			float sampleCoC = SignedCircleOfConfusionPixels(sampleDepth);
			if (sampleCoC > 0.0f) {
				outerCoCSum += sampleCoC;
				outerCoCWeight += 1.0f;
			}
		}
	}
	if (outerCoCWeight > 0.0f) {
		float edgeCoC = outerCoCSum / outerCoCWeight;
		return lerp(skyCoC, edgeCoC, 0.32f);
	}
	return skyCoC;
}

float StabilizeCoCWithReconstruction(float2 uv, float currentCoC)
{
	if (!DofTemporalReconstructionEnabled() || dofControlHistoryValid == 0u)
		return currentCoC;

	// Skyrim motion vectors are normalized current-to-previous screen deltas.
	// They live in the dynamic render region even after DLSS/FSR has reconstructed
	// colour, so sample that region and reproject the display-resolution CoC
	// history. This keeps bokeh coverage attached to foliage silhouettes instead
	// of reclassifying a different edge pixel on every temporal jitter phase.
	float2 motionUV = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(uv);
	float2 motion = MotionVectorTex.SampleLevel(LinearClampSampler, motionUV, 0.0f);
	if (any(!isfinite(motion)) || any(abs(motion) > 0.25f))
		return currentCoC;
	float2 historyUV = uv + motion;
	if (any(historyUV <= 0.0f) || any(historyUV >= 1.0f))
		return currentCoC;

	float historyCoC = PreviousCoCTex.SampleLevel(LinearClampSampler, historyUV, 0.0f);
	if (!isfinite(historyCoC))
		return currentCoC;

	// CoC history is especially prone to dancing where a jittered sample moves
	// between the foreground and background side of a wall, tree or character
	// silhouette.  The previous CoC alone cannot distinguish that from a valid
	// moving surface.  Use the existing depth resource as a conservative gate:
	// reject a reprojection whose linear depth disagrees materially, and reduce
	// history weight across a local depth edge.  This adds no resource or ABI
	// changes and is intentionally one-sided: uncertain history is discarded,
	// never used to occlude a valid current pixel.
	bool currentDepthValid;
	bool currentSky;
	float currentDepth = ReadLinearDepth(uv, currentDepthValid, currentSky);
	bool historyDepthValid;
	bool historySky;
	float historyDepth = ReadLinearDepth(historyUV, historyDepthValid, historySky);
	// A reprojected history sample from clear sky is not valid for a foliage or
	// mountain pixel, even when both CoCs happen to have the same sign.
	if (currentDepthValid && historyDepthValid && currentSky != historySky)
		return currentCoC;
	if (currentDepthValid && historyDepthValid && !currentSky && !historySky) {
		float depthScale = max(max(currentDepth, historyDepth), 128.0f);
		float depthMismatch = abs(currentDepth - historyDepth) / depthScale;
		if (depthMismatch > 0.10f)
			return currentCoC;
	}

	float depthEdge = 0.0f;
	float2 depthTexel = float2(dofControlInvRenderWidth, dofControlInvRenderHeight);
	[unroll] for (uint edgeSample = 0u; edgeSample < 4u; ++edgeSample) {
		float2 direction = edgeSample == 0u ? float2(1.0f, 0.0f) :
			edgeSample == 1u ? float2(-1.0f, 0.0f) :
			edgeSample == 2u ? float2(0.0f, 1.0f) : float2(0.0f, -1.0f);
		bool neighbourValid;
		bool neighbourSky;
		float neighbourDepth = ReadLinearDepth(uv + direction * depthTexel, neighbourValid, neighbourSky);
		if (currentDepthValid && neighbourValid) {
			if (currentSky != neighbourSky) {
				depthEdge = 1.0f;
			} else if (!currentSky && !neighbourSky) {
				float edgeScale = max(max(currentDepth, neighbourDepth), 128.0f);
				depthEdge = max(depthEdge, abs(currentDepth - neighbourDepth) / edgeScale);
			}
		}
	}
	// Do not carry near coverage into a far surface (or vice versa). Around the
	// focus plane, allow history to settle gently to zero without visible steps.
	if (currentCoC * historyCoC < 0.0f &&
		min(abs(currentCoC), abs(historyCoC)) > 0.75f)
		return currentCoC;

	float motionPixels = length(motion * float2(dofControlRenderWidth, dofControlRenderHeight));
	float historyWeight = lerp(0.82f, 0.58f, saturate(motionPixels / 8.0f));
	historyWeight *= 1.0f - 0.78f * smoothstep(0.015f, 0.085f, depthEdge);
	// A tight history clamp limits trails during disocclusion while still
	// suppressing the sub-pixel CoC toggling visible on tree and grass edges.
	float clampRadius = max(0.75f, abs(currentCoC) * 0.18f);
	historyCoC = clamp(historyCoC, currentCoC - clampRadius, currentCoC + clampRadius);
	return lerp(currentCoC, historyCoC, historyWeight);
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID)
{
	uint width, height;
	CoCOut.GetDimensions(width, height);
	if (dispatchID.x >= width || dispatchID.y >= height)
		return;
	float2 uv = (float2(dispatchID.xy) + 0.5f) / float2(width, height);
	bool valid;
	bool sky;
	float depth = ReadLinearDepth(uv, valid, sky);
	if (!valid) {
		CoCOut[dispatchID.xy] = 0.0f;
		return;
	}
	float coc = SignedCircleOfConfusionPixels(depth);
	if (DofFirstPersonView())
		coc *= 1.0f - FirstPersonHandProtection(uv, depth);
	else
		coc *= 1.0f - ThirdPersonSubjectProtection(uv, depth) * 0.92f;
	coc = StabilizeCoCWithReconstruction(uv, coc);
	if (sky) {
		// Fade sky defocus gradually across a broad elevation range. Keep 45% of
		// the optical CoC at the top of the dome so foliage silhouettes do not hit
		// a sharp blurred-geometry / protected-sky boundary in the bokeh passes.
		float skyFade = SkyDofProtection(uv);
		coc *= 1.0f - skyFade * 0.55f;
		coc = FeatherSkyCoCAgainstFarGeometry(uv, coc);
	}
	// Cloud exclusion applies only to distant cloud volume on clear-depth sky
	// pixels. Nearby cloud layers retain normal gameplay DOF, and a cloud mask
	// can never suppress defocus on foreground geometry beneath the same screen UV.
	if (sky) {
		uint cloudWidth, cloudHeight;
		VolumetricCloudTex.GetDimensions(cloudWidth, cloudHeight);
		if (cloudWidth > 0u && cloudHeight > 0u) {
			float cloudDistance = VolumetricCloudDepthTex.SampleLevel(LinearClampSampler, uv, 0.0f);
			float cloudTransmittance = VolumetricCloudTex.SampleLevel(LinearClampSampler, uv, 0.0f).a;
			float cloudCoverage = saturate((1.0f - cloudTransmittance) * 8.0f);
			float gameplayFarDistance = max(dofControlFarBlurDistance,
				dofControlFocusDistance + max(dofFocusRange, 1.0f) * 2.0f);
			float cloudDistanceFade = smoothstep(gameplayFarDistance,
				gameplayFarDistance + max(dofFocusRange, 4000.0f), cloudDistance);
			coc *= 1.0f - cloudCoverage * cloudDistanceFade;
		}
	}
	CoCOut[dispatchID.xy] = coc;
}
