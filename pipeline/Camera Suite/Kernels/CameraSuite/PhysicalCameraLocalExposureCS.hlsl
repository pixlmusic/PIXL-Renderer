#include "Common/Color.hlsli"
#include "Common/DisplayMapping.hlsli"
#include "Common/SharedData.hlsli"
#include "CameraSuite/PhysicalCameraCommon.hlsli"

Texture2D<float4> SceneTex : register(t0);
Texture2D<float> ExposureTex : register(t1);
RWTexture2D<float> LocalExposureOut : register(u0);

float3 DecodeLocalExposureScene(float3 scene)
{
    float3 linearScene = isSceneLinear > 0.5f ? max(scene, 0.0f) : Color::GammaToLinearSafe(max(scene, 0.0f));
    if (applyAutoHDR > 0.5f && isSceneLinear <= 0.5f)
        linearScene = DisplayMapping::PumboAutoHDR(linearScene, SharedData::HDRData.z, SharedData::HDRData.y, 2.25f, 1.0f);
    return max(linearScene, 0.0f);
}

float LoadLogLuminance(int2 pixel, uint2 dimensions)
{
    uint2 p = uint2(clamp(pixel, int2(0, 0), int2(dimensions) - 1));
    return log2(max(PixlLuminance(DecodeLocalExposureScene(SceneTex.Load(int3(p, 0)).rgb)), 1e-5f));
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
    uint2 sceneDimensions = uint2(sceneWidth, sceneHeight);
    int2 centerPixel = int2(min(dispatchID * 4u + 2u, sceneDimensions - 1u));
    float centerLogLum = LoadLogLuminance(centerPixel, sceneDimensions);

    // A sparse bilateral luminance kernel produces local adaptation without
    // bleeding a torch or bright sky across a silhouette.
    static const int2 offsets[8] = {
        int2(-12, 0), int2(12, 0), int2(0, -12), int2(0, 12),
        int2(-8, -8), int2(8, -8), int2(-8, 8), int2(8, 8)
    };
	float weightedLogLum = centerLogLum * 2.0f;
	float weightSum = 2.0f;
	uint sampleCount = PixlCameraLocalExposureSamples();
	[loop]
	for (uint i = 0u; i < sampleCount; ++i) {
		// Six-sample High uses an opposed diagonal pair; Ultra preserves all
		// eight samples and their original order.
		uint offsetIndex = sampleCount == 6u && i == 5u ? 7u : i;
		float sampleLogLum = LoadLogLuminance(centerPixel + offsets[offsetIndex], sceneDimensions);
        float edgeWeight = exp2(-abs(sampleLogLum - centerLogLum) * 3.0f);
        weightedLogLum += sampleLogLum * edgeWeight;
        weightSum += edgeWeight;
    }

    float exposure = max(ExposureTex.Load(int3(0, 0, 0)), 1e-5f);
    float localLum = exp2(weightedLogLum / max(weightSum, 1e-4f)) * exposure;
    float localEV = clamp(log2(0.18f / max(localLum, 1e-5f)) * saturate(cameraLocalExposure), -0.5f, 0.5f);
    LocalExposureOut[dispatchID] = exp2(localEV);
}
