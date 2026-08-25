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

float2 BodycamUV(float2 uv)
{
	float2 result = uv;
	if (bodycamEnabled >= 0.5f) {
		float strength = saturate(bodycamStrength);
		float2 p = uv * 2.0f - 1.0f;
		float r2 = dot(p, p);
		// Positive value emulates a compact wide-angle barrel lens while the
		// clamp prevents undefined sampling at the extreme frame boundary.
		p *= 1.0f + max(bodycamDistortion, 0.0f) * strength * r2;
		result = saturate(p * 0.5f + 0.5f);
	}
	return result;
}

float3 LoadBodycamScene(float2 uv, uint2 dim)
{
    float2 warped = BodycamUV(uv);
    float3 center = LoadSceneLinear(warped, dim);
	float3 result = center;
	if (bodycamEnabled >= 0.5f) {
		float strength = saturate(bodycamStrength);
		float2 radial = warped - 0.5f;
		float r = length(radial);
		float2 dir = r > 1e-5f ? radial / r : 0.0f;
		float2 ca = dir * max(bodycamChromaticAberration, 0.0f) * 0.020f * strength;
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

float3 ApplyPhysicalCamera(float2 uv, uint2 dim, float exposure, bool hdrOutput)
{
    float2 warped = BodycamUV(uv);
    float3 scene = LoadBodycamScene(uv, dim);

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

        // Small-sensor digital edge processing.
        float3 near0 = LoadSceneLinear(saturate(warped + float2(1.0f, 0.0f) * px), dim);
        float3 near1 = LoadSceneLinear(saturate(warped - float2(1.0f, 0.0f) * px), dim);
        float3 near2 = LoadSceneLinear(saturate(warped + float2(0.0f, 1.0f) * px), dim);
        float3 near3 = LoadSceneLinear(saturate(warped - float2(0.0f, 1.0f) * px), dim);
        float3 nearAvg = (near0 + near1 + near2 + near3) * 0.25f;
        scene = max(0.0f, scene + (scene - nearAvg) * max(bodycamSharpen, 0.0f) * strength);

        // Restrained automatic white-balance compensation from a broad local
        // average; clamp prevents the camera from erasing authored color.
        float avgLum = max(PixlLuminance(localAverage), 1e-4f);
        float3 wb = clamp(avgLum / max(localAverage, float3(0.02f, 0.02f, 0.02f)), float3(0.82f, 0.82f, 0.82f), float3(1.18f, 1.18f, 1.18f));
        scene *= lerp(1.0f.xxx, wb, saturate(bodycamWhiteBalance) * strength * 0.45f);

    }

    scene *= exposure * localGain;
	bool hasBloom = fmod(auxiliaryPassMask, 2.0f) >= 0.5f;
	if (hasBloom && bloomEnabled > 0.5f) {
		float3 bloom = BloomTex.SampleLevel(LinearClampSampler, warped, 0.0f);
		scene += max(bloom, 0.0f) * max(bloomStrength, 0.0f) * 0.22f;
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
	bool environmentFinishing = stormglassFinishing || submergedFinishing;
    bool finishingEnabled = !(isMainOrLoadingMenu > 0.5f) && (bloomFinishing || lookFinishing || environmentFinishing);

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

	float2 submergedUV = PixlApplySubmergedWarp(uv, dim);
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
	float3 cameraScene = cameraEnabled ? ApplyPhysicalCamera(sceneUV, dim, exposure, emitHDR) : LoadSubmergedSceneLinear(sceneUV, dim);
	if (!cameraEnabled && bloomFinishing) {
		float3 bloom = BloomTex.SampleLevel(LinearClampSampler, sceneUV, 0.0f);
		cameraScene += max(bloom, 0.0f) * max(bloomStrength, 0.0f) * 0.22f;
	}
	// Skyrim's SDR image-space chain already contains its authored weather and
	// night response. A second full-strength camera curve changed that art
	// direction and crushed dark scenes. Blend the photographic refinement over
	// the clean authored response in SDR; HDR still requires the complete camera
	// mapping to fit scene-linear energy into the display range.
	if (cameraEnabled && !emitHDR) {
		float3 authoredScene = LoadSubmergedSceneLinear(sceneUV, dim);
		cameraScene = lerp(authoredScene, cameraScene, saturate(cameraInfluence));
	}
	cameraScene = PixlApplySubmergedGrade(cameraScene);
	cameraScene = PixlApplyStormglassResponse(cameraScene, stormglass);
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
