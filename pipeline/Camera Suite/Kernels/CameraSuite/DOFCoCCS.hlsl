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
	sky = raw >= 0.999998f;
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
	float fadeWidth = max(dofFocusRange, 240.0f);
	float distanceFade = smoothstep(
		dofControlFarBlurDistance,
		dofControlFarBlurDistance + fadeWidth,
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
	// Treat Skyrim's far-depth sky as a camera-centred dome, then classify it by
	// absolute world elevation rather than screen position or viewing angle.
	// The low dome remains in DOF to blend the landscape horizon; sky above the
	// approximate High Hrothgar elevation feathers into full protection.
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
	const float HIGH_HROTHGAR_REFERENCE_Z = 18000.0f;
	const float UPPER_SKY_PROTECTED_Z = 24000.0f;
	float skyDomeAbsoluteZ = FrameBuffer::CameraPosAdjust.z +
		rayDirection.z * SKY_DOME_RADIUS;
	return smoothstep(
		HIGH_HROTHGAR_REFERENCE_Z,
		UPPER_SKY_PROTECTED_Z,
		skyDomeAbsoluteZ);
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
	// Do not carry near coverage into a far surface (or vice versa). Around the
	// focus plane, allow history to settle gently to zero without visible steps.
	if (currentCoC * historyCoC < 0.0f &&
		min(abs(currentCoC), abs(historyCoC)) > 0.75f)
		return currentCoC;

	float motionPixels = length(motion * float2(dofControlRenderWidth, dofControlRenderHeight));
	float historyWeight = lerp(0.82f, 0.58f, saturate(motionPixels / 8.0f));
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
	if (sky)
		coc *= 1.0f - SkyDofProtection(uv);
	CoCOut[dispatchID.xy] = coc;
}
