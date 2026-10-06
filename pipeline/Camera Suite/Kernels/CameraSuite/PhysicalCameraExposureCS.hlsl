#include "CameraSuite/PhysicalCameraCommon.hlsli"

Texture2D<uint> Histogram : register(t0);
RWTexture2D<float> Exposure : register(u0);

// Private dispatch data, matching CameraSuite::ExposureControlCB (16 bytes).
cbuffer ExposureControl : register(b1)
{
    float freezeMetering;
    float compensationDeltaEV;
    float2 exposureControlPadding;
};

float PIXLPhysicalCameraFiniteOr(float value, float fallback)
{
    return PixlCameraFinite(value) ? value : fallback;
}

[numthreads(1, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uint exposureWidth, exposureHeight;
    Exposure.GetDimensions(exposureWidth, exposureHeight);
    if (exposureWidth == 0u || exposureHeight == 0u)
        return;

    float safeMinExposureEV = clamp(PIXLPhysicalCameraFiniteOr(cameraMinExposureEV, -16.0f), -80.0f, 80.0f);
    float safeMaxExposureEV = clamp(PIXLPhysicalCameraFiniteOr(cameraMaxExposureEV, 16.0f), -80.0f, 80.0f);
    if (safeMinExposureEV > safeMaxExposureEV) {
        float swapEV = safeMinExposureEV;
        safeMinExposureEV = safeMaxExposureEV;
        safeMaxExposureEV = swapEV;
    }
    float safeCompensationDeltaEV = PIXLPhysicalCameraFiniteOr(compensationDeltaEV, 0.0f);
    float safeExposureCompensationEV = clamp(PIXLPhysicalCameraFiniteOr(cameraExposureCompensationEV, 0.0f), -80.0f, 80.0f);
    float safeHighlightProtection = PixlCameraFinite(cameraHighlightProtection) ? saturate(cameraHighlightProtection) : 0.0f;

    float previous = Exposure[uint2(0, 0)];
    previous = PixlCameraFinite(previous) && previous > 0.0f ? previous : 1.0f;
    // Compensation is an intentional user adjustment, not scene adaptation.
    if (cameraAutoExposure > 0.5f)
        previous = exp2(clamp(log2(previous) + safeCompensationDeltaEV, safeMinExposureEV, safeMaxExposureEV));
    if (freezeMetering > 0.5f && cameraAutoExposure > 0.5f) {
        Exposure[uint2(0, 0)] = previous;
        return;
    }
    float target = exp2(safeExposureCompensationEV);

    if (physicalCameraEnabled > 0.5f && cameraAutoExposure > 0.5f) {
        uint histogramWidth, histogramHeight;
        Histogram.GetDimensions(histogramWidth, histogramHeight);
        if (histogramWidth < 256u || histogramHeight == 0u) {
            Exposure[uint2(0, 0)] = previous;
            return;
        }

        uint total = 0u;
        [unroll]
        for (uint i = 0u; i < 256u; ++i)
            total += Histogram.Load(int3(i, 0, 0));

        if (total == 0u) {
            Exposure[uint2(0, 0)] = previous;
            return;
        }

		if (total > 0u) {
            float safeLowPercentile = PixlCameraFinite(cameraLowPercentile) ? saturate(cameraLowPercentile) : 0.0f;
            float safeHighPercentile = PixlCameraFinite(cameraHighPercentile) ? saturate(cameraHighPercentile) : 1.0f;
            uint lowCut = min((uint)((float)total * safeLowPercentile), total - 1u);
            uint highCut = min((uint)((float)total * safeHighPercentile), total);
            highCut = min(total, max(highCut, lowCut + 1u));

            uint cumulative = 0u;
            float weightedLog = 0.0f;
			uint accepted = 0u;
			// Ignore isolated emissives/specular peaks; broad highlights still meter.
			// Protect broad bright regions, not a tiny candle/specular point. With
			// uniform spatial weights this requires roughly 5% frame coverage.
			uint highlightCut = max(1u, (uint)((float)total * 0.95f));
			float highlightLum = 0.0f;

            [loop]
            for (uint i = 0u; i < 256u; ++i) {
                uint count = Histogram.Load(int3(i, 0, 0));
                if (count == 0u)
                    continue;

                uint begin = cumulative;
                uint end = cumulative + count;
                uint useBegin = max(begin, lowCut);
                uint useEnd = min(end, highCut);
                uint useCount = useEnd > useBegin ? useEnd - useBegin : 0u;
				if (useCount > 0u) {
                    float t = (float)i / 255.0f;
                    float logLum = lerp(PIXL_HISTOGRAM_LOG_MIN, PIXL_HISTOGRAM_LOG_MAX, t);
                    weightedLog += logLum * (float)useCount;
					accepted += useCount;
				}
				if (highlightLum <= 0.0f && end >= highlightCut) {
					float t = (float)i / 255.0f;
					highlightLum = exp2(lerp(PIXL_HISTOGRAM_LOG_MIN, PIXL_HISTOGRAM_LOG_MAX, t));
				}
				cumulative = end;
            }

            if (accepted > 0u) {
                float avgLogLum = weightedLog / (float)accepted;
                float avgLum = exp2(avgLogLum);
                // 18% scene key. Exposure compensation remains a photographic
                // stop adjustment layered on top of scene metering.
				target = (0.18f / max(avgLum, 1e-5f)) * exp2(safeExposureCompensationEV);
			}

			// Reserve highlight headroom before tone mapping.  A 1.5-stop bound keeps
			// bright interiors and snow from lifting the whole frame, while avoiding
			// the unbounded exposure collapse used by the original implementation.
			if (highlightLum > 0.0f) {
				float protectedLevel = lerp(5.0f, 1.65f, safeHighlightProtection);
				float safeExposure = protectedLevel / max(highlightLum, 1e-5f) * exp2(safeExposureCompensationEV);
				float reductionEV = clamp(log2(max(target, 1e-6f) / max(safeExposure, 1e-6f)), 0.0f, 1.5f);
				target *= exp2(-reductionEV * safeHighlightProtection);
			}
		}
    }

    target = PixlCameraFinite(target) && target > 0.0f ? target : exp2(clamp(safeExposureCompensationEV, safeMinExposureEV, safeMaxExposureEV));
    float targetEV = clamp(log2(max(target, 1e-6f)), safeMinExposureEV, safeMaxExposureEV);

    if (bodycamEnabled > 0.5f) {
        float safeBodyStrength = PixlCameraFinite(bodycamStrength) ? saturate(bodycamStrength) : 0.0f;
        float safeBodyAggressiveness = PixlCameraFinite(bodycamExposureAggressiveness) ? saturate(bodycamExposureAggressiveness) : 0.0f;
        float body = safeBodyStrength * safeBodyAggressiveness;
        // Subtle sensor-style exposure hunting, intentionally below a tenth of
        // a stop at maximum strength.
        targetEV += sin((float)frameIndex * 0.071f) * 0.045f * body;
        targetEV = clamp(targetEV, safeMinExposureEV, safeMaxExposureEV);
    }

    target = exp2(targetEV);

    // Manual exposure is a direct photographic control; do not make the
    // user wait for adaptation when Auto Exposure is disabled.
    if (cameraAutoExposure < 0.5f) {
        Exposure[uint2(0, 0)] = max(target, 1e-5f);
        return;
    }

    float adaptBrightToDark = max(PIXLPhysicalCameraFiniteOr(cameraAdaptBrightToDark, 0.01f), 0.01f);
    float adaptDarkToBright = max(PIXLPhysicalCameraFiniteOr(cameraAdaptDarkToBright, 0.01f), 0.01f);
    float tau = target > previous ? adaptBrightToDark : adaptDarkToBright;
    if (bodycamEnabled > 0.5f) {
        float safeBodyStrength = PixlCameraFinite(bodycamStrength) ? saturate(bodycamStrength) : 0.0f;
        float safeBodyAggressiveness = PixlCameraFinite(bodycamExposureAggressiveness) ? saturate(bodycamExposureAggressiveness) : 0.0f;
        float speedup = lerp(1.0f, 3.0f, safeBodyStrength * safeBodyAggressiveness);
        tau /= speedup;
    }

    float dt = clamp(PIXLPhysicalCameraFiniteOr(deltaTime, 0.0f), 0.0f, 0.1f);
    float blend = 1.0f - exp(-dt / tau);
    float previousEV = log2(previous);
    float errorEV = targetEV - previousEV;
    // A soft 0.02-stop deadband rejects histogram-bin chatter without a snap.
    errorEV = sign(errorEV) * max(abs(errorEV) - 0.02f, 0.0f);
    // Stops/sec, not a per-frame multiplier. Brightening responds promptly;
    // darkening cannot produce the old frame-rate-dependent plunges.
    float stepEV = clamp(errorEV * blend, -4.0f * dt, 6.0f * dt);
    Exposure[uint2(0, 0)] = exp2(clamp(previousEV + stepEV, safeMinExposureEV, safeMaxExposureEV));
}
