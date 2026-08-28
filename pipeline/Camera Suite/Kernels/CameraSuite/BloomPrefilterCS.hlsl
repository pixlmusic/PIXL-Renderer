#include "Common/Color.hlsli"
#include "Common/DisplayMapping.hlsli"
#include "Common/SharedData.hlsli"
#include "CameraSuite/PhysicalCameraCommon.hlsli"

Texture2D<float4> SceneTex : register(t0);
Texture2D<float> ExposureTex : register(t1);
RWTexture2D<float3> BloomOut : register(u0);

float3 DecodeBloomScene(uint2 pixel)
{
    float3 scene = max(SceneTex.Load(int3(pixel, 0)).rgb, 0.0f);
    float3 linearScene = isSceneLinear > 0.5f ? scene : Color::GammaToLinearSafe(scene);
    if (applyAutoHDR > 0.5f && isSceneLinear <= 0.5f)
        linearScene = DisplayMapping::PumboAutoHDR(linearScene, SharedData::HDRData.z, SharedData::HDRData.y, 2.25f, 1.0f);
    return max(linearScene, 0.0f);
}

[numthreads(8, 8, 1)]
void main(uint2 dispatchID : SV_DispatchThreadID)
{
    uint outputWidth, outputHeight;
    BloomOut.GetDimensions(outputWidth, outputHeight);
    if (dispatchID.x >= outputWidth || dispatchID.y >= outputHeight)
        return;

    uint sceneWidth, sceneHeight;
    SceneTex.GetDimensions(sceneWidth, sceneHeight);
    uint2 sceneDimensions = uint2(sceneWidth, sceneHeight);
    uint2 base = dispatchID * 2u;
    float exposure = max(ExposureTex.Load(int3(0, 0, 0)), 1e-5f);

    float3 sum = 0.0f;
    float weightSum = 0.0f;
	float rawLuminanceSum = 0.0f;
	float maximumLuminance = 0.0f;
    [unroll]
    for (uint y = 0u; y < 2u; ++y) {
        [unroll]
        for (uint x = 0u; x < 2u; ++x) {
            uint2 p = min(base + uint2(x, y), sceneDimensions - 1u);
            float3 color = DecodeBloomScene(p) * exposure;
			float luminance = PixlLuminance(color);
			// Karis average suppresses isolated sub-pixel fireflies while retaining
			// coherent emissive surfaces and bright practical light sources.
            float karis = rcp(1.0f + 0.65f * luminance);
            sum += color * karis;
            weightSum += karis;
			rawLuminanceSum += luminance;
			maximumLuminance = max(maximumLuminance, luminance);
        }
    }

    float3 average = sum / max(weightSum, 1e-4f);
    float threshold = max(bloomThreshold, 0.0f);
    float knee = max(threshold * 0.35f, 0.05f);
    float luminance = PixlLuminance(average);
	// Quadratic soft knee: continuous in value and slope, unlike the broad halo
	// ramp of Skyrim's original image-space bloom.
	float soft = clamp(luminance - threshold + knee, 0.0f, 2.0f * knee);
	soft = soft * soft / max(4.0f * knee, 1e-4f);
	float contribution = max(luminance - threshold, soft);

	float rawAverageLuminance = rawLuminanceSum * 0.25f;
	float coherence = saturate(rawAverageLuminance / max(maximumLuminance, 1e-4f) * 3.0f);
	float fireflyGuard = lerp(0.72f, 1.0f, coherence);
    BloomOut[dispatchID] = average * (contribution / max(luminance, 1e-4f)) * fireflyGuard;
}
