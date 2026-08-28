/**
 * @file HDROutputCS.hlsl
 * @brief PIXL HDR output + Physical Camera / bodycam mapper.
 *
 * The legacy path is preserved when Physical Camera is disabled. The PIXL
 * path meters/exposes the scene before UI composition, performs a
 * luminance-preserving camera response, then maps to SDR sRGB or HDR10 PQ.
 */

#include "Common/Color.hlsli"
#include "Common/DisplayMapping.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"
#include "CameraSuite/PhysicalCameraCommon.hlsli"
#include "CameraSuite/Stormglass.hlsli"

Texture2D<float4> SceneTex : register(t0);
Texture2D<float4> UITex : register(t1);
Texture2D<float> ExposureTex : register(t2);
Texture2D<float4> LookTex : register(t3);
Texture2D<float3> BloomTex : register(t4);
Texture2D<float> LocalExposureTex : register(t5);
Texture2D<float4> StormglassFieldTex : register(t6);
Texture2D<float4> FrostLensTex : register(t7);
Texture2D<float4> FireLensTex : register(t8);
SamplerState LinearClampSampler : register(s0);
RWTexture2D<float4> HDROutput : register(u0);

#ifndef USE_PIXL_DISPLAY_REFERRED_MENUS
#define USE_PIXL_DISPLAY_REFERRED_MENUS 1
#endif

uint2 ClampSceneCoord(int2 p, uint2 dim)
{
    return uint2(clamp(p, int2(0, 0), int2(dim) - 1));
}

float3 DecodeScene(float3 scene)
{
    float3 linearScene = isSceneLinear > 0.5f ? max(scene, 0.0f) : Color::GammaToLinearSafe(max(scene, 0.0f));
    // When a legacy replacement tonemap replaced ISHDR, recover useful scene headroom before
    // the PIXL camera maps it. This is beneficial in SDR too: the camera can
    // then compress that reconstructed range once, rather than applying its
    // response directly to an already-clipped SDR image.
    if (applyAutoHDR > 0.5f && isSceneLinear <= 0.5f)
        linearScene = DisplayMapping::PumboAutoHDR(linearScene, SharedData::HDRData.z, SharedData::HDRData.y, 2.25, 1.0);
    return max(linearScene, 0.0f);
}

float3 LoadSceneLinear(float2 uv, uint2 dim)
{
    float2 p = saturate(uv) * float2(dim);
    uint2 coord = ClampSceneCoord(int2(p), dim);
    return DecodeScene(SceneTex.Load(int3(coord, 0)).rgb);
}

float PixlLensHash(float2 p)
{
	p = frac(p * float2(0.1031f, 0.1030f));
	p += dot(p, p.yx + 33.33f);
	return frac((p.x + p.y) * p.x);
}

float PixlLensNoise(float2 p)
{
	float2 cell = floor(p);
	float2 f = frac(p);
	float2 u = f * f * (3.0f - 2.0f * f);
	return lerp(
		lerp(PixlLensHash(cell), PixlLensHash(cell + float2(1.0f, 0.0f)), u.x),
		lerp(PixlLensHash(cell + float2(0.0f, 1.0f)), PixlLensHash(cell + 1.0f.xx), u.x),
		u.y);
}

float PixlLensEdge(float2 uv)
{
	float2 p = abs(uv * 2.0f - 1.0f);
	return smoothstep(0.46f, 1.0f, max(p.x, p.y));
}

float4 PixlSampleLensArtwork(Texture2D<float4> artwork, float2 uv, out bool available)
{
	uint width = 0u;
	uint height = 0u;
	artwork.GetDimensions(width, height);
	available = width > 0u && height > 0u;
	return available
		? artwork.SampleLevel(LinearClampSampler, saturate(uv), 0.0f)
		: 0.0f.xxxx;
}

float2 PixlApplyElementalWarp(float2 uv, uint2 dim)
{
	float fire = saturate(fireLensAmount * elementalLensStrength);
	float frost = saturate(coldLensAmount * coldLensStrength);
	if (fire <= 1.0e-4f && frost <= 1.0e-4f)
		return uv;

	float edge = PixlLensEdge(uv);
	bool fireArtworkReady;
	float4 fireArtwork = PixlSampleLensArtwork(FireLensTex, uv, fireArtworkReady);
	float fireCoverage = fireArtworkReady ? fireArtwork.a : edge;
	float time = stormglassTime;
	float risingNoise = PixlLensNoise(float2(uv.x * 15.0f, uv.y * 9.0f - time * 2.4f));
	float shimmer = sin(uv.y * 83.0f - time * 15.0f + risingNoise * 9.0f);
	float heat = fire * fireCoverage * smoothstep(0.22f, 0.88f, risingNoise);
	float2 pixelSize = rcp(max(float2(dim), 1.0f.xx));
	float2 fireOffset = float2(
		(shimmer + risingNoise - 0.5f) * 3.2f,
		(shimmer * 0.45f + risingNoise - 0.62f) * 2.0f) * pixelSize * heat;

	// A tiny normal-like bend from the authored opacity edge gives the frost real
	// optical thickness without making the whole screen wobble.
	bool frostArtworkReady;
	float4 frostArtwork = PixlSampleLensArtwork(FrostLensTex, uv, frostArtworkReady);
	float frostCoverage = frostArtworkReady ? frostArtwork.a : edge;
	float2 frostGradient = 0.0f.xx;
	if (frostArtworkReady && frost > 1.0e-4f) {
		float2 gradientStep = pixelSize * 2.0f;
		frostGradient.x =
			FrostLensTex.SampleLevel(LinearClampSampler, saturate(uv + float2(gradientStep.x, 0.0f)), 0.0f).a -
			FrostLensTex.SampleLevel(LinearClampSampler, saturate(uv - float2(gradientStep.x, 0.0f)), 0.0f).a;
		frostGradient.y =
			FrostLensTex.SampleLevel(LinearClampSampler, saturate(uv + float2(0.0f, gradientStep.y)), 0.0f).a -
			FrostLensTex.SampleLevel(LinearClampSampler, saturate(uv - float2(0.0f, gradientStep.y)), 0.0f).a;
	}
	float2 frostOffset = frostGradient * pixelSize * (2.1f * frost * frostCoverage);
	return saturate(uv + fireOffset + frostOffset);
}

float3 PixlApplyElementalGrade(float3 scene, float2 uv, uint2 dim)
{
	float edge = PixlLensEdge(uv);
	float frost = saturate(coldLensAmount * coldLensStrength);
	if (frost > 1.0e-4f) {
		bool artworkReady;
		float4 artwork = PixlSampleLensArtwork(FrostLensTex, uv, artworkReady);
		// Resolution-aware crystalline modulation preserves fine branches at 4K,
		// while the authored alpha supplies the large, organic frost boundary.
		float aspect = max((float)dim.x / max((float)dim.y, 1.0f), 1.0f);
		float2 crystalUV = float2((uv.x - 0.5f) * aspect, uv.y - 0.5f);
		float coarse = PixlLensNoise(crystalUV * float2(34.0f, 22.0f));
		float fine = PixlLensNoise(crystalUV.yx * float2(91.0f, 67.0f) + 7.3f);
		float veins = 1.0f - smoothstep(0.025f, 0.105f, abs(frac((crystalUV.x + crystalUV.y * 0.73f) * 49.0f + coarse * 2.4f) - 0.5f));
		float crystal = saturate(smoothstep(0.34f, 0.86f, abs(coarse - fine) * 1.72f) + veins * 0.32f);
		float coverage = artworkReady ? artwork.a : edge * (0.24f + crystal * 0.76f);
		float frostMask = saturate(frost * coverage);
		float luminance = dot(scene, float3(0.2126f, 0.7152f, 0.0722f));
		float3 coldScene = lerp(scene, luminance.xxx, 0.14f) * float3(0.88f, 0.96f, 1.08f);
		float3 authoredIce = artworkReady ? artwork.rgb : float3(0.58f, 0.75f, 0.92f);
		coldScene += authoredIce * (0.10f + crystal * 0.19f);
		scene = lerp(scene, coldScene, frostMask * 0.72f);
	}

	float fire = saturate(fireLensAmount * elementalLensStrength);
	if (fire > 1.0e-4f) {
		bool artworkReady;
		float4 artwork = PixlSampleLensArtwork(FireLensTex, uv, artworkReady);
		float time = stormglassTime;
		float flame = PixlLensNoise(float2(uv.x * 18.0f, uv.y * 10.0f - time * 2.1f));
		flame *= 0.55f + 0.45f * PixlLensNoise(float2(uv.x * 41.0f + 9.0f, uv.y * 21.0f - time * 3.7f));
		float coverage = artworkReady ? artwork.a : edge;
		float flameMask = saturate(fire * coverage * (0.70f + 0.30f * smoothstep(0.20f, 0.76f, flame)));
		float3 authoredFire = artworkReady ? artwork.rgb : float3(0.72f, 0.12f, 0.01f);
		float3 flameTint = scene * float3(1.07f, 0.86f, 0.68f) + authoredFire * (0.16f + flame * 0.15f);
		scene = lerp(scene, flameTint, flameMask * 0.68f);
	}

	return max(scene, 0.0f);
}

float2 BodycamUV(float2 uv, uint2 dim)
{
	float2 result = uv;
	if (bodycamEnabled >= 0.5f) {
		float strength = saturate(bodycamStrength);
		float2 p = uv * 2.0f - 1.0f;
		float aspect = max((float)dim.x / max((float)dim.y, 1.0f), 1.0f);
		p.x *= aspect;
		float r2 = dot(p, p);
		// Bounded inverse radial mapping widens the field of view without ever
		// driving samples outside the source image. The previous outward multiply
		// relied on saturate(), smearing the final texels into broad edge bands.
		float k1 = max(bodycamDistortion, 0.0f) * strength * 0.72f;
		float k2 = k1 * 0.18f;
		p *= rcp(1.0f + k1 * r2 + k2 * r2 * r2);
		p.x /= aspect;
		result = p * 0.5f + 0.5f;
	}
	return clamp(result, 0.0005f.xx, 0.9995f.xx);
}

float3 LoadBodycamScene(float2 uv, uint2 dim)
{
	float2 warped = BodycamUV(uv, dim);
    float3 center = LoadSceneLinear(warped, dim);
	float3 result = center;
	if (bodycamEnabled >= 0.5f) {
		float strength = saturate(bodycamStrength);
		float aspect = max((float)dim.x / max((float)dim.y, 1.0f), 1.0f);
		float2 radial = warped * 2.0f - 1.0f;
		radial.x *= aspect;
		float r = length(radial);
		float2 dir = r > 1e-5f ? radial / r : 0.0f;
		dir.x /= aspect;
		float edge = smoothstep(0.28f, 1.28f, r);
		float2 ca = dir * edge * edge * max(bodycamChromaticAberration, 0.0f) * 0.008f * strength;
		float3 plus = LoadSceneLinear(saturate(warped + ca), dim);
		float3 minus = LoadSceneLinear(saturate(warped - ca), dim);
		result = float3(plus.r, center.g, minus.b);
	}
	return result;
}

float3 LoadSubmergedSceneLinear(float2 uv, uint2 dim)
{
	float3 centre = LoadSceneLinear(uv, dim);
	float blurAmount = saturate(submergedOpticsEnabled) * saturate(submergedBlend) * saturate(submergedBlur);
	float3 result = centre;
	if (blurAmount > 0.001f) {
		float2 pixelSize = rcp(max(float2(dim), 1.0f.xx));
		float2 radius = pixelSize * lerp(0.75f, 3.25f, saturate(submergedBlur));
		float3 neighbours =
			LoadSceneLinear(saturate(uv + float2(radius.x, 0.0f)), dim) +
			LoadSceneLinear(saturate(uv - float2(radius.x, 0.0f)), dim) +
			LoadSceneLinear(saturate(uv + float2(0.0f, radius.y)), dim) +
			LoadSceneLinear(saturate(uv - float2(0.0f, radius.y)), dim);
		float3 soft = (centre * 2.0f + neighbours) * (1.0f / 6.0f);
		result = lerp(centre, soft, blurAmount * 0.72f);
	}
	return result;
}

float PixlDofLinearDepth(float2 uv, out bool isSky)
{
	float rawDepth = SharedData::GetDepth(saturate(uv));
	// A cleared/unavailable depth SRV reads zero. Keep that pixel sharp instead
	// of mistaking it for a near-plane receiver and blurring the entire frame.
	if (rawDepth <= 1.0e-5f) {
		isSky = false;
		return 0.0f;
	}
	isSky = rawDepth >= 0.999998f;
	return isSky
		? dofFocusDistance + dofFocusRange * 8.0f
		: max(SharedData::GetScreenDepth(rawDepth), 0.0f);
}

float PixlDofResolvedFocusDistance(uint2 dim)
{
	if (dofAutoFocus < 0.5f)
		return dofFocusDistance;

	// Centre focus uses the exact scene depth that drives bokeh. Four nearby
	// guards rescue thin crosshair targets without averaging a foreground NPC
	// together with the distant background.
	float2 pixelSize = rcp(max(float2(dim), 1.0f.xx));
	const float2 centre = float2(0.5f, 0.5f);
	bool centreSky;
	float centreDepth = PixlDofLinearDepth(centre, centreSky);
	if (centreDepth > 0.0f && !centreSky)
		return floor(centreDepth * 0.125f + 0.5f) * 8.0f;

	const float2 offsets[4] = {
		float2(-4.0f, 0.0f), float2(4.0f, 0.0f),
		float2(0.0f, -4.0f), float2(0.0f, 4.0f)
	};
	float nearestValid = 0.0f;
	[unroll]
	for (uint i = 0u; i < 4u; ++i) {
		bool sampleSky;
		float sampleDepth = PixlDofLinearDepth(centre + offsets[i] * pixelSize, sampleSky);
		if (sampleDepth > 0.0f && !sampleSky)
			nearestValid = nearestValid > 0.0f ? min(nearestValid, sampleDepth) : sampleDepth;
	}
	return nearestValid > 0.0f
		? floor(nearestValid * 0.125f + 0.5f) * 8.0f
		: dofFocusDistance;
}

float PixlDofEffectiveFocusRange(float focusDistance)
{
	if (dofAutoFocus < 0.5f)
		return max(dofFocusRange, 1.0f);

	// Dialogue remains selective while landscape focus gains enough tolerance to
	// avoid nervous focus pumping from small depth changes at the crosshair. Very
	// large values saved by the old manual-only UI are migrated in-shader to the
	// new 480-unit autofocus baseline; manual mode still honours them exactly.
	float configuredAutoBase = dofFocusRange > 4000.0f
		? 480.0f
		: clamp(dofFocusRange, 240.0f, 900.0f);
	float adaptiveMinimum = clamp(160.0f + focusDistance * 0.12f, 300.0f, 1800.0f);
	return max(configuredAutoBase, adaptiveMinimum);
}

float PixlDofEffectiveEdgeProtection()
{
	// Old/manual profiles are allowed to expose the full artistic range. Gameplay
	// autofocus must never inherit a zero edge guard: that value lets ground and
	// displaced snow gather colour through their own depth silhouette.
	return dofAutoFocus > 0.5f
		? max(dofFocusEdgeProtection, 0.85f)
		: max(dofFocusEdgeProtection, 0.0f);
}

float PixlDofEffectiveForegroundCoverage()
{
	// A coverage of 1.0 previously replaced depth rejection entirely and caused
	// foreground terrain to smear over distant scenery. Keep autofocus cinematic
	// but bounded; manual photo-mode work still honours the full control range.
	return dofAutoFocus > 0.5f
		? min(saturate(dofForegroundCoverage), 0.72f)
		: saturate(dofForegroundCoverage);
}

float PixlDofDepthContinuity(
	float2 uv,
	uint2 dim,
	float centerDepth,
	bool centerIsSky)
{
	if (centerDepth <= 0.0f)
		return 0.0f;

	float2 pixelSize = rcp(max(float2(dim), 1.0f.xx));
	const float2 offsets[4] = {
		float2(-1.0f, 0.0f), float2(1.0f, 0.0f),
		float2(0.0f, -1.0f), float2(0.0f, 1.0f)
	};
	float minimumDepth = centerDepth;
	float maximumDepth = centerDepth;
	float validNeighbors = 0.0f;
	float skyMismatch = 0.0f;
	[unroll]
	for (uint i = 0u; i < 4u; ++i) {
		bool neighborIsSky;
		float neighborDepth = PixlDofLinearDepth(uv + offsets[i] * pixelSize, neighborIsSky);
		if (neighborDepth > 0.0f) {
			minimumDepth = min(minimumDepth, neighborDepth);
			maximumDepth = max(maximumDepth, neighborDepth);
			validNeighbors += 1.0f;
			skyMismatch = max(skyMismatch, neighborIsSky != centerIsSky ? 1.0f : 0.0f);
		}
	}

	float relativeSpan = (maximumDepth - minimumDepth) / max(centerDepth, 64.0f);
	float continuousSurface = 1.0f - smoothstep(0.018f, 0.11f, relativeSpan);
	float neighborhoodReady = smoothstep(2.5f, 4.0f, validNeighbors);
	return continuousSurface * neighborhoodReady * (1.0f - skyMismatch);
}

float PixlDofSignedCoC(float linearDepth, bool isSky, float focusDistance, float focusRange)
{
	if (linearDepth <= 0.0f)
		return 0.0f;
	float signedDistance = isSky
		? 8.0f
		: (linearDepth - focusDistance) / max(focusRange, 1.0f);
	// Nearby objects should expand over a focused background rather than looking
	// cut out, while the user control still bounds that foreground coverage.
	if (signedDistance < 0.0f)
		signedDistance *= lerp(0.72f, 1.35f, PixlDofEffectiveForegroundCoverage());
	return clamp(signedDistance * saturate(dofStrength), -1.0f, 1.0f);
}

float2 PixlDofAperturePoint(uint sampleIndex, uint sampleCount, float rotation)
{
	const float goldenAngle = 2.39996323f;
	float radius = sqrt((sampleIndex + 0.5f) / max((float)sampleCount, 1.0f));
	float angle = sampleIndex * goldenAngle + rotation;
	return float2(cos(angle), sin(angle)) * radius;
}

float2 PixlCameraMotionVector(float2 uv, uint2 dim, out float centerDepth)
{
	centerDepth = 0.0f;
	float rawDepth = SharedData::GetDepth(saturate(uv));
	if (rawDepth <= 1.0e-5f || rawDepth >= 0.999998f)
		return 0.0f.xx;

	float4 currentCS = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), rawDepth, 1.0f);
	float4 currentRelative = mul(FrameBuffer::CameraViewProjInverse, currentCS);
	if (abs(currentRelative.w) <= 1.0e-5f)
		return 0.0f.xx;
	currentRelative /= currentRelative.w;
	centerDepth = max(SharedData::GetScreenDepth(rawDepth), 0.0f);

	float3 absolutePosition = currentRelative.xyz + FrameBuffer::CameraPosAdjust.xyz;
	float3 previousRelative = absolutePosition - FrameBuffer::CameraPreviousPosAdjust.xyz;
	float4 previousCS = mul(
		FrameBuffer::CameraPreviousViewProjUnjittered,
		float4(previousRelative, 1.0f));
	if (previousCS.w <= 1.0e-5f)
		return 0.0f.xx;

	float2 previousUV = previousCS.xy / previousCS.w * float2(0.5f, -0.5f) + 0.5f;
	if (FrameBuffer::IsOutsideFrame(previousUV, false))
		return 0.0f.xx;

	float2 velocity = uv - previousUV;
	float pixelMotion = length(velocity * float2(dim));
	// Sub-pixel reprojection jitter must never become visible camera blur.
	if (pixelMotion < 0.65f)
		return 0.0f.xx;
	float maxPixels = max(motionBlurMaxPixels, 1.0f);
	return velocity * min(1.0f, maxPixels / max(pixelMotion, 1.0e-4f));
}

float3 PixlApplyCameraMotionBlur(float2 uv, uint2 dim, float3 sharpScene)
{
	if (motionBlurEnabled < 0.5f || motionBlurStrength <= 1.0e-4f)
		return sharpScene;

	float centerDepth;
	float2 velocity = PixlCameraMotionVector(uv, dim, centerDepth);
	float pixelMotion = length(velocity * float2(dim));
	if (centerDepth <= 0.0f || pixelMotion < 0.65f)
		return sharpScene;

	uint quality = PixlCameraQualityTier();
	uint sampleCount = quality == 3u ? 11u : (quality == 2u ? 9u : (quality == 1u ? 7u : 5u));
	float2 span = velocity * saturate(motionBlurStrength) * clamp(motionBlurShutter, 0.10f, 1.0f);
	float3 accumulated = sharpScene * 2.0f;
	float accumulatedWeight = 2.0f;
	[loop]
	for (uint i = 0u; i < 11u; ++i) {
		if (i >= sampleCount)
			break;
		float t = ((float)i + 0.5f) / (float)sampleCount - 0.5f;
		float2 sampleUV = saturate(uv - span * t);
		float rawSampleDepth = SharedData::GetDepth(sampleUV);
		if (rawSampleDepth <= 1.0e-5f || rawSampleDepth >= 0.999998f)
			continue;
		float sampleDepth = max(SharedData::GetScreenDepth(rawSampleDepth), 0.0f);
		float relativeSeparation = abs(sampleDepth - centerDepth) / max(min(sampleDepth, centerDepth), 128.0f);
		float depthWeight = 1.0f - smoothstep(0.025f, 0.18f, relativeSeparation);
		// Do not pull a distant background across a nearer silhouette.
		if (sampleDepth > centerDepth)
			depthWeight *= depthWeight;
		float shutterWeight = 1.0f - abs(t) * 0.55f;
		float weight = depthWeight * shutterWeight;
		accumulated += LoadBodycamScene(sampleUV, dim) * weight;
		accumulatedWeight += weight;
	}

	float3 blurred = accumulated / max(accumulatedWeight, 1.0e-4f);
	return lerp(sharpScene, blurred, smoothstep(0.65f, 3.0f, pixelMotion) * saturate(motionBlurStrength));
}

float3 PixlApplyDepthOfField(float2 uv, uint2 dim, float3 sharpScene)
{
	if (dofEnabled < 0.5f || dofStrength <= 1.0e-4f)
		return sharpScene;

	bool centerIsSky;
	float centerDepth = PixlDofLinearDepth(uv, centerIsSky);
	float focusDistance = PixlDofResolvedFocusDistance(dim);
	float focusRange = PixlDofEffectiveFocusRange(focusDistance);
	float centerCoC = PixlDofSignedCoC(centerDepth, centerIsSky, focusDistance, focusRange);
	float depthContinuity = PixlDofDepthContinuity(uv, dim, centerDepth, centerIsSky);
	float blurAmount = saturate(abs(centerCoC)) * depthContinuity;
	if (blurAmount <= 0.012f)
		return sharpScene;

	uint quality = min((uint)(clamp(dofQuality, 0.0f, 3.0f) + 0.5f), 3u);
	uint sampleCount = quality == 3u ? 16u : (quality == 2u ? 12u : (quality == 1u ? 8u : 6u));
	float maxRadiusPixels = lerp(7.0f, 18.0f, quality / 3.0f) * max(dofBokehRadius, 0.5f);
	float2 pixelSize = rcp(max(float2(dim), 1.0f.xx));
	float2 framePosition = uv * 2.0f - 1.0f;
	float frameRadius = saturate(length(framePosition) * 0.82f);
	float2 radialDirection = length(framePosition) > 1.0e-4f
		? normalize(framePosition)
		: float2(1.0f, 0.0f);
	float2 tangentDirection = float2(-radialDirection.y, radialDirection.x);
	float catEyeCompression = 1.0f - saturate(dofCatEye) * frameRadius * 0.48f;
	float anamorphic = clamp(dofAnamorphicRatio, 0.5f, 2.0f);
	// A per-pixel random aperture rotation made every defocused region look like
	// persistent sensor noise. The golden-angle disk is already well distributed;
	// a fixed orientation keeps the kernel deterministic and temporally stable.
	const float rotation = 0.0f;

	float effectiveEdgeProtection = PixlDofEffectiveEdgeProtection();
	float effectiveForegroundCoverage = PixlDofEffectiveForegroundCoverage();
	float centerWeight = lerp(3.25f, 1.50f, blurAmount);
	float3 accumulated = sharpScene * centerWeight;
	float accumulatedWeight = centerWeight;
	[loop]
	for (uint i = 0u; i < 16u; ++i) {
		if (i >= sampleCount)
			break;

		float2 aperture = PixlDofAperturePoint(i, sampleCount, rotation);
		float radialComponent = dot(aperture, radialDirection) * catEyeCompression;
		float tangentComponent = dot(aperture, tangentDirection);
		aperture = radialDirection * radialComponent + tangentDirection * tangentComponent;
		aperture *= float2(anamorphic, rcp(sqrt(anamorphic)));

		float2 sampleUV = saturate(uv + aperture * pixelSize * (maxRadiusPixels * blurAmount));
		bool sampleIsSky;
		float sampleDepth = PixlDofLinearDepth(sampleUV, sampleIsSky);
		if (sampleDepth <= 0.0f)
			continue;
		float sampleCoC = PixlDofSignedCoC(sampleDepth, sampleIsSky, focusDistance, focusRange);
		float depthScale = max(min(centerDepth, sampleDepth) * 0.035f + focusRange * 0.12f, 8.0f);
		float depthSeparation = abs(sampleDepth - centerDepth) / depthScale;
		float edgeWeight = exp2(-depthSeparation * effectiveEdgeProtection * 1.7f);

		// Background bokeh cannot bleed through a nearer silhouette. Foreground
		// bokeh is allowed to cover the background according to its dedicated control.
		float occlusionWeight = edgeWeight;
		if (centerCoC > 0.0f && sampleDepth < centerDepth)
			occlusionWeight *= edgeWeight;
		else if (centerCoC < 0.0f && sampleDepth > centerDepth)
			occlusionWeight = lerp(
				edgeWeight,
				sqrt(saturate(edgeWeight)),
				effectiveForegroundCoverage * 0.55f);

		// A gather filter cannot correctly scatter a foreground disc across the
		// opposite side of a depth edge. Suppress those cross-sign taps instead of
		// producing the familiar halo around terrain, actors and snow displacement.
		if (centerCoC * sampleCoC < 0.0f)
			occlusionWeight *= 0.08f;

		float sampleSupport = smoothstep(0.015f, 0.16f, abs(sampleCoC));
		float3 sampleColor = LoadSceneLinear(sampleUV, dim);
		float highlight = saturate((PixlLuminance(sampleColor) - 0.65f) * 0.75f);
		float highlightWeight = 1.0f + highlight * saturate(dofHighlightResponse) * 1.5f;
		float weight = max(occlusionWeight * lerp(0.45f, 1.0f, sampleSupport) * highlightWeight, 1.0e-4f);
		accumulated += sampleColor * weight;
		accumulatedWeight += weight;
	}

	float3 bokeh = accumulated / max(accumulatedWeight, 1.0e-4f);
	return lerp(sharpScene, bokeh, smoothstep(0.012f, 0.88f, blurAmount));
}

float CameraMapLuminance(float x, float displayRange)
{
    x = max(x, 0.0f);
    displayRange = max(displayRange, 1.0f);

    // Rational shoulder normalized at 18% gray so highlight protection can
    // change without moving the exposure key.
    float shoulderK = lerp(0.20f, 1.65f, saturate(cameraHighlightProtection)) * max(cameraShoulder, 0.05f);
    float grayNorm = 0.18f / displayRange;
    float normalizer = 1.0f + shoulderK * grayNorm;
    float mapped = x * normalizer / (1.0f + shoulderK * x / displayRange);

    // Film-like toe. Shadow Detail progressively restores useful shape rather
    // than adding a flat lift, so true black can remain black.
    float toe = saturate(cameraToe);
    float toeMapped = mapped * mapped / max(mapped + 0.035f, 1e-5f);
    mapped = lerp(mapped, toeMapped, toe);
    mapped = lerp(mapped, max(mapped, x * 0.72f), saturate(cameraShadowDetail));

    // Contrast pivots around photographic middle gray.
    float pivot = 0.18f;
    mapped = pivot * pow(max(mapped / pivot, 1e-5f), max(cameraContrast, 0.05f));
    return min(mapped, displayRange * 1.25f);
}

float3 ApplyPhysicalCamera(float2 uv, uint2 dim, float exposure, bool hdrOutput, float3 opticalScene)
{
    float2 warped = BodycamUV(uv, dim);
    float3 scene = opticalScene;
	float3 bodycamBroad = scene;

    float2 px = 1.0f / float2(dim);
	float submergedBlurAmount = saturate(submergedOpticsEnabled) * saturate(submergedBlend) * saturate(submergedBlur);
	if (submergedBlurAmount > 0.001f) {
		float2 radius = px * lerp(0.75f, 3.25f, saturate(submergedBlur));
		float3 neighbours =
			LoadBodycamScene(saturate(uv + float2(radius.x, 0.0f)), dim) +
			LoadBodycamScene(saturate(uv - float2(radius.x, 0.0f)), dim) +
			LoadBodycamScene(saturate(uv + float2(0.0f, radius.y)), dim) +
			LoadBodycamScene(saturate(uv - float2(0.0f, radius.y)), dim);
		float3 soft = (scene * 2.0f + neighbours) * (1.0f / 6.0f);
		scene = lerp(scene, soft, submergedBlurAmount * 0.72f);
	}
	bool hasLocalExposure = fmod(floor(auxiliaryPassMask * 0.5f), 2.0f) >= 0.5f;
    float localGain = hasLocalExposure
        ? max(LocalExposureTex.SampleLevel(LinearClampSampler, warped, 0.0f), 0.25f)
        : 1.0f;

    if (bodycamEnabled > 0.5f) {
        float strength = saturate(bodycamStrength);
		float3 broad0 = LoadSceneLinear(saturate(warped + float2(12.0f, 0.0f) * px), dim);
		float3 broad1 = LoadSceneLinear(saturate(warped - float2(12.0f, 0.0f) * px), dim);
		float3 broad2 = LoadSceneLinear(saturate(warped + float2(0.0f, 12.0f) * px), dim);
		float3 broad3 = LoadSceneLinear(saturate(warped - float2(0.0f, 12.0f) * px), dim);
		float3 localAverage = (scene + broad0 + broad1 + broad2 + broad3) * 0.2f;
		bodycamBroad = localAverage;

        // Small-sensor digital edge processing.
        float3 near0 = LoadSceneLinear(saturate(warped + float2(1.0f, 0.0f) * px), dim);
        float3 near1 = LoadSceneLinear(saturate(warped - float2(1.0f, 0.0f) * px), dim);
        float3 near2 = LoadSceneLinear(saturate(warped + float2(0.0f, 1.0f) * px), dim);
        float3 near3 = LoadSceneLinear(saturate(warped - float2(0.0f, 1.0f) * px), dim);
        float3 nearAvg = (near0 + near1 + near2 + near3) * 0.25f;
		float2 lensP = uv * 2.0f - 1.0f;
		float lensEdge = smoothstep(0.42f, 1.35f, dot(lensP, lensP));
		float sharpening = max(bodycamSharpen, 0.0f) * strength * lerp(1.0f, 0.38f, lensEdge);
        scene = max(0.0f, scene + (scene - nearAvg) * sharpening);

        // Restrained automatic white-balance compensation from a broad local
        // average; clamp prevents the camera from erasing authored color.
        float avgLum = max(PixlLuminance(localAverage), 1e-4f);
        float3 wb = clamp(avgLum / max(localAverage, float3(0.02f, 0.02f, 0.02f)), float3(0.82f, 0.82f, 0.82f), float3(1.18f, 1.18f, 1.18f));
        scene *= lerp(1.0f.xxx, wb, saturate(bodycamWhiteBalance) * strength * 0.45f);

    }

    scene *= exposure * localGain;
	if (bodycamEnabled > 0.5f && bodycamHighlightBloom > 1.0e-4f) {
		// Compact sensor/lens halation from the already sampled broad neighbourhood.
		// It is colour preserving and soft-kneed, rather than a white full-frame veil.
		float3 broadExposed = max(bodycamBroad * exposure * localGain, 0.0f);
		float broadLum = PixlLuminance(broadExposed);
		float halation = smoothstep(0.72f, 2.40f, broadLum);
		float3 broadChroma = broadExposed / max(broadLum, 1.0e-4f);
		scene += broadChroma * halation * max(bodycamHighlightBloom, 0.0f) *
			saturate(bodycamStrength) * 0.075f;
	}
	bool hasBloom = fmod(auxiliaryPassMask, 2.0f) >= 0.5f;
	if (hasBloom && bloomEnabled > 0.5f) {
		float3 bloom = BloomTex.SampleLevel(LinearClampSampler, warped, 0.0f);
		scene += max(bloom, 0.0f) * max(bloomStrength, 0.0f) * 0.28f;
	}

    if (bodycamEnabled > 0.5f) {
        float strength = saturate(bodycamStrength);
        float lum = PixlLuminance(scene);
        float lowLight = saturate(1.0f - lum / 0.28f);
        float sensorGain = saturate(log2(max(exposure, 1.0f) + 1.0f) / 6.0f);
        float noiseAmp = max(bodycamNoise, 0.0f) * strength * lowLight * (0.008f + 0.018f * sensorGain);
        uint2 pixel = uint2(saturate(uv) * float2(dim));
        float n = PixlRandom01(pixel, frameIndex) - 0.5f;
        float nc = PixlRandom01(pixel.yx + uint2(37u, 19u), frameIndex + 17u) - 0.5f;
        scene += noiseAmp * float3(n + nc * 0.35f, n, n - nc * 0.35f);
        scene = max(scene, 0.0f);
    }

    float displayRange = hdrOutput ? max(peakNits / max(paperWhite, 1.0f), 1.0f) : 1.0f;
    float lum = max(PixlLuminance(scene), 1e-6f);
    float mappedLum = CameraMapLuminance(lum, displayRange);
    float3 mapped = scene * (mappedLum / lum);
    mapped = PixlSaturatePreserveHue(mapped, cameraSaturation);

    if (bodycamEnabled > 0.5f) {
        float2 p = uv * 2.0f - 1.0f;
        float vignette = smoothstep(0.35f, 1.10f, dot(p, p));
        mapped *= 1.0f - vignette * max(bodycamVignette, 0.0f) * saturate(bodycamStrength);
    }

    return max(mapped, 0.0f);
}

float3 SamplePixlLook(float3 linearColor, float displayRange)
{
	float3 result = linearColor;
	if (lookEnabled >= 0.5f && lookOpacity > 0.0f) {
		// The curated assets are conventional 32^3 strip LUTs (1024x32), with
		// red within each tile, inverted green vertically and blue across tiles.
		// Grade normalized display-referred colour so HDR highlight range is kept.
		float3 normalized = saturate(linearColor / max(displayRange, 1.0f));
		float3 encoded = Color::LinearToSrgb(normalized);
		float blue = encoded.b * 31.0f;
		float slice0 = floor(blue);
		float slice1 = min(slice0 + 1.0f, 31.0f);
		float x0 = slice0 * 32.0f + 0.5f + encoded.r * 31.0f;
		float x1 = slice1 * 32.0f + 0.5f + encoded.r * 31.0f;
		float y = 0.5f + (1.0f - encoded.g) * 31.0f;
		float2 uv0 = float2(x0 / 1024.0f, y / 32.0f);
		float2 uv1 = float2(x1 / 1024.0f, y / 32.0f);
		float3 gradedEncoded = lerp(
			LookTex.SampleLevel(LinearClampSampler, uv0, 0.0f).rgb,
			LookTex.SampleLevel(LinearClampSampler, uv1, 0.0f).rgb,
			frac(blue));
		float3 gradedLinear = Color::SrgbToLinear(saturate(gradedEncoded)) * max(displayRange, 1.0f);
		result = lerp(linearColor, gradedLinear, saturate(lookOpacity));
	}
	return result;
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID)
{
    uint width, height;
    HDROutput.GetDimensions(width, height);
    if (dispatchID.x >= width || dispatchID.y >= height)
        return;

    uint2 pixel = dispatchID.xy;
    uint2 dim = uint2(width, height);
    float2 uv = (float2(pixel) + 0.5f) / float2(dim);
    float4 scene = SceneTex[pixel];
    float4 ui = UITex[pixel];

    bool hdrEnabled = enableHDR > 0.5f;
    bool skipUI = skipUIComposite > 0.5f;
    // Main-menu and loading-screen images are authored display-referred LDR,
    // not scene-linear camera negatives. Re-exposing them crushed their narrow
    // shadow range (especially with a gameplay-oriented maximum exposure).
    // Route those scenes through the original CS output mapper; gameplay,
    // pause and map scenes retain the PIXL Physical Camera unchanged.
#if USE_PIXL_DISPLAY_REFERRED_MENUS
    bool cameraEnabled = physicalCameraEnabled > 0.5f && !(isMainOrLoadingMenu > 0.5f);
#else
    bool cameraEnabled = physicalCameraEnabled > 0.5f;
#endif
    bool emitHDR = hdrEnabled && previewSDR <= 0.5f;

    bool hasBloom = fmod(auxiliaryPassMask, 2.0f) >= 0.5f;
    bool bloomFinishing = hasBloom && bloomEnabled > 0.5f;
    bool lookFinishing = lookEnabled > 0.5f && lookOpacity > 0.0f;
	// Activation and field readiness are intentionally separate. Stormglass is a
	// weather effect, so rain/wetness determines whether it exists. The quarter-res
	// compute field is only an optimization. If that pass is unavailable for any
	// reason, HDROutput falls back to the same proven procedural optics directly.
	float stormglassLiveRain = PixlStormglassLiveRain();
	bool stormglassSignalActive = stormglassEnabled > 0.5f &&
		max(max(stormglassLiveRain, stormglassWetness), surfaceBreakFilm) > 0.002f;
	bool stormglassFieldReady = fmod(floor(auxiliaryPassMask * 0.25f), 2.0f) >= 0.5f;
	bool stormglassFinishing = stormglassSignalActive;
	bool submergedFinishing = submergedOpticsEnabled > 0.5f && submergedBlend > 0.002f;
	bool elementalFinishing =
		(coldLensAmount * coldLensStrength > 0.001f) ||
		(fireLensAmount * elementalLensStrength > 0.001f);
	bool environmentFinishing = stormglassFinishing || submergedFinishing || elementalFinishing;
    bool dofFinishing = dofEnabled > 0.5f && dofStrength > 0.0001f;
    bool motionBlurFinishing = motionBlurEnabled > 0.5f && motionBlurStrength > 0.0001f;
    bool finishingEnabled = !(isMainOrLoadingMenu > 0.5f) &&
		(bloomFinishing || lookFinishing || environmentFinishing || dofFinishing || motionBlurFinishing);

    // Preserve the compatibility display path when no PIXL presentation
    // effect is active. Bloom and LUT grading intentionally remain usable
    // without forcing Physical Camera or changing the authored exposure.
    if (!cameraEnabled && !finishingEnabled) {
        float3 finalColor;
        if (hdrEnabled) {
            bool sceneIsLinear = isSceneLinear > 0.5f;
            if (applyAutoHDR > 0.5f) {
                float3 outputColor = sceneIsLinear ? scene.xyz : Color::GammaToLinearSafe(scene.xyz);
                outputColor = DisplayMapping::PumboAutoHDR(outputColor, SharedData::HDRData.z, SharedData::HDRData.y, 2.75, 1.0);
                scene.xyz = sceneIsLinear ? outputColor : Color::LinearToGammaSafe(outputColor);
            }

            float3 compositedColorLinear;
            if (sceneIsLinear) {
                float3 sceneLinear = max(0.0f, scene.rgb);
                if (isMainOrLoadingMenu > 0.5f)
                    sceneLinear *= max(menuSceneBrightness, 0.0f);
                if (skipUI) {
                    compositedColorLinear = sceneLinear;
                } else {
                    float3 uiLinear = Color::SrgbToLinear(max(0.0f, ui.rgb));
                    if (!(isMainOrLoadingMenu > 0.5f))
                        uiLinear *= uiBrightness;
                    compositedColorLinear = uiLinear + sceneLinear * (1.0f - ui.a);
                }
            } else {
                float3 sceneGamma = scene.rgb;
                if (isMainOrLoadingMenu > 0.5f) {
                    float3 menuLinear = Color::GammaToLinearSafe(max(0.0f, sceneGamma));
                    sceneGamma = Color::LinearToGammaSafe(menuLinear * max(menuSceneBrightness, 0.0f));
                }
                float3 compositedColorGamma;
                if (skipUI) {
                    compositedColorGamma = sceneGamma;
                } else {
                    float3 uiGamma = ui.rgb;
                    if (!(isMainOrLoadingMenu > 0.5f)) {
                        float3 uiLinear = Color::SrgbToLinear(max(0.0f, uiGamma));
                        uiLinear *= uiBrightness;
                        uiGamma = Color::LinearToSrgb(uiLinear);
                    }
                    compositedColorGamma = uiGamma + sceneGamma * (1.0f - ui.a);
                }
                compositedColorLinear = Color::GammaToLinearSafe(compositedColorGamma);
            }

            if (previewSDR > 0.5f) {
                finalColor = saturate(Color::LinearToSrgb(max(0.0f, compositedColorLinear)));
            } else {
                compositedColorLinear = Color::BT709ToBT2020(compositedColorLinear);
                finalColor = saturate(Color::pq::Encode(max(0.0f, compositedColorLinear), paperWhite));
            }
		} else {
			float3 sceneGamma = scene.rgb;
			if (isMainOrLoadingMenu > 0.5f)
				sceneGamma *= max(menuSceneBrightness, 0.0f);
			finalColor = skipUI ? sceneGamma : ui.rgb + sceneGamma * (1.0f - ui.a);
            finalColor = saturate(finalColor);
        }
        HDROutput[pixel] = float4(finalColor, 1.0f);
        return;
    }

	float2 elementalUV = PixlApplyElementalWarp(uv, dim);
	float2 submergedUV = PixlApplySubmergedWarp(elementalUV, dim);
	PixlStormglassWarpData stormglass;
	stormglass.uv = submergedUV;
	stormglass.coverage = 0.0f;
	stormglass.rim = 0.0f;
	if (stormglassFinishing) {
		if (stormglassFieldReady) {
			// Fast production path: quarter-resolution precomputed optical field.
			float4 field = StormglassFieldTex.SampleLevel(LinearClampSampler, submergedUV, 0.0f);
			stormglass.uv = saturate(submergedUV + field.xy);
			stormglass.coverage = saturate(field.z);
			stormglass.rim = saturate(field.w);
		} else {
			// Reliability fallback: this is the same direct procedural path that
			// proved the visual effect works in the live test. Unlike that diagnostic,
			// it is NOT forced; it only runs when the real Stormglass rain/wetness
			// signal is active. This makes field dispatch/compile readiness an
			// optimization rather than a hard requirement for visible droplets.
			stormglass = PixlApplyStormglass(submergedUV, dim);
		}
	}
	float2 sceneUV = stormglass.uv;
	float exposure = cameraEnabled ? max(ExposureTex.Load(int3(0, 0, 0)), 1e-5f) : 1.0f;
	float2 opticalUV = cameraEnabled ? BodycamUV(sceneUV, dim) : sceneUV;
	float3 sharpOpticalScene = cameraEnabled ? LoadBodycamScene(sceneUV, dim) : LoadSceneLinear(sceneUV, dim);
	float3 motionScene = PixlApplyCameraMotionBlur(sceneUV, dim, sharpOpticalScene);
	float3 dofScene = PixlApplyDepthOfField(opticalUV, dim, motionScene);
	float3 cameraScene = cameraEnabled ? ApplyPhysicalCamera(sceneUV, dim, exposure, emitHDR, dofScene) : dofScene;
	if (!cameraEnabled && bloomFinishing) {
		float3 bloom = BloomTex.SampleLevel(LinearClampSampler, sceneUV, 0.0f);
		cameraScene += max(bloom, 0.0f) * max(bloomStrength, 0.0f) * 0.28f;
	}
	// Skyrim's SDR image-space chain already contains its authored weather and
	// night response. A second full-strength camera curve changed that art
	// direction and crushed dark scenes. Blend the photographic refinement over
	// the clean authored response in SDR; HDR still requires the complete camera
	// mapping to fit scene-linear energy into the display range.
	if (cameraEnabled && !emitHDR) {
		float3 authoredScene = dofScene;
		cameraScene = lerp(authoredScene, cameraScene, saturate(cameraInfluence));
	}
	cameraScene = PixlApplySubmergedGrade(cameraScene);
	cameraScene = PixlApplyStormglassResponse(cameraScene, stormglass);
	cameraScene = PixlApplyElementalGrade(cameraScene, uv, dim);
	float lookRange = emitHDR ? max(peakNits / max(paperWhite, 1.0f), 1.0f) : 1.0f;
	cameraScene = SamplePixlLook(cameraScene, lookRange);


    float3 finalLinear;
    if (skipUI) {
        finalLinear = cameraScene;
    } else {
        float3 uiLinear = Color::SrgbToLinear(max(0.0f, ui.rgb));
        if (hdrEnabled && !(isMainOrLoadingMenu > 0.5f))
            uiLinear *= uiBrightness;
        finalLinear = uiLinear + cameraScene * (1.0f - ui.a);
    }

    float3 finalColor;
    if (emitHDR) {
        finalLinear = Color::BT709ToBT2020(max(finalLinear, 0.0f));
        finalColor = saturate(Color::pq::Encode(finalLinear, paperWhite));
    } else {
        finalColor = saturate(Color::LinearToSrgb(max(finalLinear, 0.0f)));
    }

    HDROutput[pixel] = float4(finalColor, 1.0f);
}
