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

		// Quality controls real metering workload (10/7/4/2 source-pixel stride).
		// Stable cell centres avoid metering a different set of emissive/window
		// pixels every frame while the camera and scene are stationary.
		uint stride = max(PixlCameraHistogramStride(), 1u);
		uint2 pixel = dtid * stride + stride / 2u;
        if (pixel.x < width && pixel.y < height) {
            float3 scene = SceneTex.Load(int3(pixel, 0)).rgb;
            if (all(PixlCameraFinite(scene))) {
                scene = max(scene, 0.0f);
                float3 linearScene = isSceneLinear > 0.5f ? scene : Color::GammaToLinearSafe(scene);
                // Match HDROutputCS metering when a legacy replacement tonemap replaced the original
                // ISHDR mapper: expose the same reconstructed HDR signal that the
                // camera sees.
                if (applyAutoHDR > 0.5f && isSceneLinear <= 0.5f)
                    linearScene = DisplayMapping::PumboAutoHDR(linearScene, SharedData::HDRData.z, SharedData::HDRData.y, 2.25f, 1.0f);

                // Reject non-finite HDR reconstruction before luminance/log/bin conversion.
                // This prevents NaN/Inf from ever becoming an undefined histogram index.
                if (all(PixlCameraFinite(linearScene))) {
                    float measuredLum = PixlLuminance(max(linearScene, 0.0f));
                    // Black borders and fully occluded pixels contain no useful
                    // illumination measurement. Binning them at the histogram
                    // floor would make a mostly black frame brighten itself.
                    if (PixlCameraFinite(measuredLum) && measuredLum > 1e-5f) {
                        float lum = max(measuredLum, exp2(PIXL_HISTOGRAM_LOG_MIN));
                        float logLum = clamp(log2(lum), PIXL_HISTOGRAM_LOG_MIN, PIXL_HISTOGRAM_LOG_MAX);
                        float histogramRange = max(PIXL_HISTOGRAM_LOG_RANGE, 1e-5f);
                        float binPosition = clamp((logLum - PIXL_HISTOGRAM_LOG_MIN) * (255.0f / histogramRange), 0.0f, 255.0f);
                        uint bin = (uint)binPosition;
                        // Split one equal-area vote across adjacent log-luminance
                        // bins. 5 fractional bits bound the total to 2^31 at the
                        // DX11 16384^2 texture limit and our minimum stride of 2.
                        uint upperWeight = (uint)(frac(binPosition) * 32.0f + 0.5f);
                        uint lowerWeight = 32u - upperWeight;
                        // Broad evaluative metering: every sampled cell has an
                        // equal vote across the frame (and its equal-area zones).
                        // The old 4:1 centre bias let a distant candle count like
                        // a much larger bright region when aimed at directly.
                        // Percentile trimming rejects isolated bright/dark cells;
                        // no additional histogram, attachment or pass is needed.
                        if (lowerWeight != 0u)
                            InterlockedAdd(LocalHistogram[bin], lowerWeight);
                        if (upperWeight != 0u)
                            InterlockedAdd(LocalHistogram[min(bin + 1u, 255u)], upperWeight);
                    }
                }
            }
        }
    }

    GroupMemoryBarrierWithGroupSync();

    uint histogramWidth, histogramHeight;
    Histogram.GetDimensions(histogramWidth, histogramHeight);

    [unroll]
    for (uint flushIndex = 0u; flushIndex < 4u; ++flushIndex) {
        uint bin = lane + flushIndex * 64u;
        uint count = LocalHistogram[bin];
        if (count != 0u && bin < histogramWidth && histogramHeight > 0u)
            InterlockedAdd(Histogram[uint2(bin, 0)], count);
    }
}
