#ifndef PIXL_PHYSICAL_CAMERA_COMMON
#define PIXL_PHYSICAL_CAMERA_COMMON

cbuffer PerFrame : register(b0)
{
    float enableHDR : packoffset(c0.x);
    float paperWhite : packoffset(c0.y);
    float peakNits : packoffset(c0.z);
    float skipUIComposite : packoffset(c0.w);

    float uiBrightness : packoffset(c1.x);
    float isSceneLinear : packoffset(c1.y);
    float isMainOrLoadingMenu : packoffset(c1.z);
    float fgTweenMenuMidAlphaBoost : packoffset(c1.w);

    float previewSDR : packoffset(c2.x);
    float applyAutoHDR : packoffset(c2.y);
	float menuSceneBrightness : packoffset(c2.z);
    float stormglassLateralInertia : packoffset(c2.w);

    float physicalCameraEnabled : packoffset(c3.x);
    float cameraAutoExposure : packoffset(c3.y);
    float cameraExposureCompensationEV : packoffset(c3.z);
    float cameraMinExposureEV : packoffset(c3.w);

    float cameraMaxExposureEV : packoffset(c4.x);
    float cameraLowPercentile : packoffset(c4.y);
    float cameraHighPercentile : packoffset(c4.z);
    float cameraHighlightProtection : packoffset(c4.w);

    float cameraShadowDetail : packoffset(c5.x);
    float cameraContrast : packoffset(c5.y);
    float cameraLocalExposure : packoffset(c5.z);
    float cameraAdaptBrightToDark : packoffset(c5.w);

    float cameraAdaptDarkToBright : packoffset(c6.x);
    float bodycamEnabled : packoffset(c6.y);
    float bodycamStrength : packoffset(c6.z);
    float bodycamDistortion : packoffset(c6.w);

    float bodycamNoise : packoffset(c7.x);
    float bodycamVignette : packoffset(c7.y);
    float bodycamChromaticAberration : packoffset(c7.z);
    float bodycamSharpen : packoffset(c7.w);

    float bodycamExposureAggressiveness : packoffset(c8.x);
    float bodycamHighlightBloom : packoffset(c8.y);
    float bodycamWhiteBalance : packoffset(c8.z);
    float cameraSaturation : packoffset(c8.w);

    float deltaTime : packoffset(c9.x);
    uint frameIndex : packoffset(c9.y);
    float cameraToe : packoffset(c9.z);
    float cameraShoulder : packoffset(c9.w);

	float lookEnabled : packoffset(c10.x);
	float lookOpacity : packoffset(c10.y);
	float cameraInfluence : packoffset(c10.z);
	float auxiliaryPassMask : packoffset(c10.w);

	float bloomEnabled : packoffset(c11.x);
	float bloomStrength : packoffset(c11.y);
	float bloomThreshold : packoffset(c11.z);
	float bloomRadius : packoffset(c11.w);

	float stormglassEnabled : packoffset(c12.x);
	float stormglassRainIntensity : packoffset(c12.y);
	float stormglassWetness : packoffset(c12.z);
	float stormglassStrength : packoffset(c12.w);

	float stormglassDropScale : packoffset(c13.x);
	float stormglassRefraction : packoffset(c13.y);
	float stormglassTrails : packoffset(c13.z);
	float stormglassTime : packoffset(c13.w);

	float surfaceBreakFilm : packoffset(c14.x);
	float submergedOpticsEnabled : packoffset(c14.y);
	float submergedBlend : packoffset(c14.z);
	float submergedStrength : packoffset(c14.w);

	float submergedBlur : packoffset(c15.x);
	float submergedRefraction : packoffset(c15.y);
	float submergedFogAmount : packoffset(c15.z);
	float cameraQuality : packoffset(c15.w);

	float4 submergedWaterTint : packoffset(c16);

	float dofEnabled : packoffset(c17.x);
	float dofStrength : packoffset(c17.y);
	float dofFocusDistance : packoffset(c17.z);
	float dofFocusRange : packoffset(c17.w);

	float dofBokehRadius : packoffset(c18.x);
	float dofHighlightResponse : packoffset(c18.y);
	float dofFocusEdgeProtection : packoffset(c18.z);
	float dofForegroundCoverage : packoffset(c18.w);

	float dofCatEye : packoffset(c19.x);
	float dofAnamorphicRatio : packoffset(c19.y);
	float dofQuality : packoffset(c19.z);
	float dofAutoFocus : packoffset(c19.w);

	float coldLensAmount : packoffset(c20.x);
	float fireLensAmount : packoffset(c20.y);
	float coldLensStrength : packoffset(c20.z);
	float elementalLensStrength : packoffset(c20.w);

	float motionBlurEnabled : packoffset(c21.x);
	float motionBlurStrength : packoffset(c21.y);
	float motionBlurShutter : packoffset(c21.z);
	float motionBlurMaxPixels : packoffset(c21.w);
};

static const float PIXL_HISTOGRAM_LOG_MIN = -12.0f;
static const float PIXL_HISTOGRAM_LOG_MAX = 12.0f;
static const float PIXL_HISTOGRAM_LOG_RANGE = 24.0f;

uint PixlCameraQualityTier()
{
    return min((uint)(clamp(cameraQuality, 0.0f, 3.0f) + 0.5f), 3u);
}

uint PixlCameraHistogramStride()
{
    const uint tier = PixlCameraQualityTier();
    return tier == 3u ? 4u : (tier == 2u ? 5u : (tier == 1u ? 6u : 8u));
}

uint PixlCameraLocalExposureSamples()
{
    return 2u + PixlCameraQualityTier() * 2u;
}

float PixlLuminance(float3 c)
{
    return dot(max(c, 0.0f), float3(0.2126f, 0.7152f, 0.0722f));
}

uint PixlHash(uint2 p, uint frame)
{
    uint h = p.x * 0x8da6b343u ^ p.y * 0xd8163841u ^ (frame + 1u) * 0xcb1ab31fu;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

float PixlRandom01(uint2 p, uint frame)
{
    return (PixlHash(p, frame) & 0x00ffffffu) * (1.0f / 16777216.0f);
}

float3 PixlSaturatePreserveHue(float3 c, float saturation)
{
    float y = PixlLuminance(c);
    return max(0.0f, lerp(y.xxx, c, max(saturation, 0.0f)));
}

#endif
