#include "Common/Color.hlsli"
#include "Common/DisplayMapping.hlsli"
#include "Common/SharedData.hlsli"
#include "CameraSuite/PhysicalCameraCommon.hlsli"

Texture2D<float4> SceneTex : register(t0);
RWTexture2D<uint> Histogram : register(u0);

// One 256-bin histogram per thread group in LDS drastically reduces global
// UAV contention compared with atomically incrementing the same 256 counters
// for every metering sample. An 8x8 group has 64 lanes, so each lane clears
// and flushes four bins.
groupshared uint LocalHistogram[256];

[numthreads(8, 8, 1)]
void main(uint2 dtid : SV_DispatchThreadID, uint2 gtid : SV_GroupThreadID)
{
    uint lane = gtid.y * 8u + gtid.x;
    [unroll]
    for (uint clearIndex = 0u; clearIndex < 4u; ++clearIndex)
        LocalHistogram[lane + clearIndex * 64u] = 0u;
    GroupMemoryBarrierWithGroupSync();

    if (physicalCameraEnabled > 0.5f && cameraAutoExposure > 0.5f) {
        uint width, height;
        SceneTex.GetDimensions(width, height);

		// Quality controls real metering workload (8/6/5/4 source-pixel stride).
		// Ultra retains the shipped 4x4 path. Rotate the sub-pixel choice each
		// frame to avoid a fixed sampling pattern without a noise texture.
		uint stride = PixlCameraHistogramStride();
		uint2 jitter = uint2(frameIndex % stride, (frameIndex / stride) % stride);
		uint2 pixel = dtid * stride + jitter;
        if (pixel.x < width && pixel.y < height) {
            float3 scene = max(SceneTex.Load(int3(pixel, 0)).rgb, 0.0f);
            float3 linearScene = isSceneLinear > 0.5f ? scene : Color::GammaToLinearSafe(scene);
            // Match HDROutputCS metering when a legacy replacement tonemap replaced the original
            // ISHDR mapper: expose the same reconstructed HDR signal that the
            // camera sees.
            if (applyAutoHDR > 0.5f && isSceneLinear <= 0.5f)
                linearScene = DisplayMapping::PumboAutoHDR(linearScene, SharedData::HDRData.z, SharedData::HDRData.y, 2.25f, 1.0f);

            float lum = max(PixlLuminance(linearScene), exp2(PIXL_HISTOGRAM_LOG_MIN));
            float logLum = clamp(log2(lum), PIXL_HISTOGRAM_LOG_MIN, PIXL_HISTOGRAM_LOG_MAX);
            uint bin = min(255u, (uint)((logLum - PIXL_HISTOGRAM_LOG_MIN) * (255.0f / PIXL_HISTOGRAM_LOG_RANGE) + 0.5f));
            float2 screenUV = (float2(pixel) + 0.5f) / float2(width, height);
            float2 edgeDistance = abs(screenUV * 2.0f - 1.0f);
            float centerWeight = saturate(1.0f - max(edgeDistance.x, edgeDistance.y));
            uint meterWeight = 1u + (uint)(3.0f * centerWeight * centerWeight + 0.5f);
            InterlockedAdd(LocalHistogram[bin], meterWeight);
        }
    }

    GroupMemoryBarrierWithGroupSync();

    [unroll]
    for (uint flushIndex = 0u; flushIndex < 4u; ++flushIndex) {
        uint bin = lane + flushIndex * 64u;
        uint count = LocalHistogram[bin];
        if (count != 0u)
            InterlockedAdd(Histogram[uint2(bin, 0)], count);
    }
}
