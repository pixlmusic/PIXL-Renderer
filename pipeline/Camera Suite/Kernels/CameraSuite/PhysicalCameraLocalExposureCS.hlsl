#include "Common/Color.hlsli"
#include "Common/DisplayMapping.hlsli"
#include "Common/SharedData.hlsli"
#include "CameraSuite/PhysicalCameraCommon.hlsli"

Texture2D<float4> SceneTex : register(t0);
Texture2D<float> ExposureTex : register(t1);
RWTexture2D<float> LocalExposureOut : register(u0);

float3 PIXLPhysicalCameraFiniteNonNegative3(float3 value)
{
    value.x = PixlCameraFinite(value.x) ? max(value.x, 0.0f) : 0.0f;
    value.y = PixlCameraFinite(value.y) ? max(value.y, 0.0f) : 0.0f;
    value.z = PixlCameraFinite(value.z) ? max(value.z, 0.0f) : 0.0f;
    return value;
}

float3 DecodeLocalExposureScene(float3 scene)
{
    scene = PIXLPhysicalCameraFiniteNonNegative3(scene);
    float3 linearScene = isSceneLinear > 0.5f ? scene : Color::GammaToLinearSafe(scene);
    if (applyAutoHDR > 0.5f && isSceneLinear <= 0.5f)
        linearScene = DisplayMapping::PumboAutoHDR(linearScene, SharedData::HDRData.z, SharedData::HDRData.y, 2.25f, 1.0f);
    return PIXLPhysicalCameraFiniteNonNegative3(linearScene);
}

float LoadLogLuminance(int2 pixel, uint2 dimensions)
{
    uint2 p = uint2(clamp(pixel, int2(0, 0), int2(dimensions) - 1));
    float luminance = PixlLuminance(DecodeLocalExposureScene(SceneTex.Load(int3(p, 0)).rgb));
    luminance = PixlCameraFinite(luminance) ? max(luminance, 1e-5f) : 1e-5f;
    return log2(luminance);
}

[numthreads(8, 8, 1)]
void main(uint2 dispatchID : SV_DispatchThreadID)
{
    uint outputWidth, outputHeight;
    LocalExposureOut.GetDimensions(outputWidth, outputHeight);
    if (dispatchID.x >= outputWidth || dispatchID.y >= outputHeight)
        return;

    uint sceneWidth, sceneHeight;
    SceneTex.GetDimensions(sceneWidth, sceneHeight);
    if (sceneWidth == 0u || sceneHeight == 0u) {
        LocalExposureOut[dispatchID] = 1.0f;
        return;
    }

    uint2 sceneDimensions = uint2(sceneWidth, sceneHeight);
    int2 centerPixel = int2(min(dispatchID * 4u + 2u, sceneDimensions - 1u));
    float centerLogLum = LoadLogLuminance(centerPixel, sceneDimensions);

    // A sparse bilateral luminance kernel produces local adaptation without
    // bleeding a torch or bright sky across a silhouette.
    static const int2 offsets[24] = {
        int2(-12, 0), int2(12, 0), int2(0, -12), int2(0, 12),
        int2(-8, -8), int2(8, -8), int2(-8, 8), int2(8, 8),
        int2(-20, -5), int2(20, 5), int2(5, -20), int2(-5, 20),
        int2(-17, 11), int2(17, -11), int2(-11, -17), int2(11, 17),
        int2(-28, 0), int2(28, 0), int2(0, -28), int2(0, 28),
        int2(-20, -20), int2(20, -20), int2(-20, 20), int2(20, 20)
    };
	float weightedLogLum = centerLogLum * 2.0f;
	float weightSum = 2.0f;
	uint sampleCount = min(PixlCameraLocalExposureSamples(), 24u);
	[loop]
	for (uint i = 0u; i < sampleCount; ++i) {
		float sampleLogLum = LoadLogLuminance(centerPixel + offsets[i], sceneDimensions);
        float edgeWeight = exp2(-abs(sampleLogLum - centerLogLum) * 3.0f);
        weightedLogLum += sampleLogLum * edgeWeight;
        weightSum += edgeWeight;
    }

    float exposure = ExposureTex.Load(int3(0, 0, 0));
    exposure = PixlCameraFinite(exposure) && exposure > 0.0f ? exposure : 1.0f;

    float averageLogLum = weightedLogLum / max(weightSum, 1e-4f);
    averageLogLum = PixlCameraFinite(averageLogLum) ? averageLogLum : centerLogLum;
    float localLum = exp2(clamp(averageLogLum, -80.0f, 80.0f)) * exposure;
    localLum = PixlCameraFinite(localLum) ? max(localLum, 1e-5f) : 3.402823466e+38f;

    float localExposureStrength = PixlCameraFinite(cameraLocalExposure) ? saturate(cameraLocalExposure) : 0.0f;
    float keyedRatio = max(0.18f / localLum, 1e-20f);
    float localEV = clamp(log2(keyedRatio) * localExposureStrength, -0.5f, 0.5f);
    LocalExposureOut[dispatchID] = exp2(localEV);
}
