#include "CameraSuite/PhysicalCameraCommon.hlsli"

Texture2D<uint> Histogram : register(t0);
RWTexture2D<float> Exposure : register(u0);

[numthreads(1, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    float previous = max(Exposure[uint2(0, 0)], 1e-5f);
    float target = exp2(cameraExposureCompensationEV);

    if (physicalCameraEnabled > 0.5f && cameraAutoExposure > 0.5f) {
        uint total = 0u;
        [unroll]
        for (uint i = 0u; i < 256u; ++i)
            total += Histogram.Load(int3(i, 0, 0));

		if (total > 0u) {
            uint lowCut = (uint)((float)total * saturate(cameraLowPercentile));
            uint highCut = (uint)((float)total * saturate(cameraHighPercentile));
            highCut = max(highCut, lowCut + 1u);

            uint cumulative = 0u;
            float weightedLog = 0.0f;
			uint accepted = 0u;
			uint highlightCut = (uint)((float)total * 0.995f);
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
                    float t = ((float)i + 0.5f) / 256.0f;
                    float logLum = lerp(PIXL_HISTOGRAM_LOG_MIN, PIXL_HISTOGRAM_LOG_MAX, t);
                    weightedLog += logLum * (float)useCount;
					accepted += useCount;
				}
				if (highlightLum <= 0.0f && end >= highlightCut) {
					float t = ((float)i + 0.5f) / 256.0f;
					highlightLum = exp2(lerp(PIXL_HISTOGRAM_LOG_MIN, PIXL_HISTOGRAM_LOG_MAX, t));
				}
				cumulative = end;
            }

            if (accepted > 0u) {
                float avgLogLum = weightedLog / (float)accepted;
                float avgLum = exp2(avgLogLum);
                // 18% scene key. Exposure compensation remains a photographic
                // stop adjustment layered on top of scene metering.
				target = (0.18f / max(avgLum, 1e-5f)) * exp2(cameraExposureCompensationEV);
			}

			// Protect the upper scene percentile immediately. Trimmed average
			// metering alone can legitimately ignore a bright doorway, flame bank or
			// snow field and briefly drive it into a massive display blowout.
			// This cap preserves photographic adaptation while reserving highlight
			// headroom proportional to the user's protection control.
			if (highlightLum > 0.0f) {
				float protectedLevel = lerp(5.0f, 1.65f, saturate(cameraHighlightProtection));
				float safeExposure = protectedLevel / max(highlightLum, 1e-5f);
				target = min(target, lerp(target, safeExposure, saturate(cameraHighlightProtection)));
			}
		}
    }

    float targetEV = clamp(log2(max(target, 1e-6f)), cameraMinExposureEV, cameraMaxExposureEV);

    if (bodycamEnabled > 0.5f) {
        float body = saturate(bodycamStrength) * saturate(bodycamExposureAggressiveness);
        // Subtle sensor-style exposure hunting, intentionally below a tenth of
        // a stop at maximum strength.
        targetEV += sin((float)frameIndex * 0.071f) * 0.045f * body;
        targetEV = clamp(targetEV, cameraMinExposureEV, cameraMaxExposureEV);
    }

    target = exp2(targetEV);

    // Manual exposure is a direct photographic control; do not make the
    // user wait for adaptation when Auto Exposure is disabled.
    if (cameraAutoExposure < 0.5f) {
        Exposure[uint2(0, 0)] = max(target, 1e-5f);
        return;
    }

    float tau = target > previous ? max(cameraAdaptBrightToDark, 0.01f) : max(cameraAdaptDarkToBright, 0.01f);
    if (bodycamEnabled > 0.5f) {
        float speedup = lerp(1.0f, 3.0f, saturate(bodycamStrength) * saturate(bodycamExposureAggressiveness));
        tau /= speedup;
    }

    float dt = clamp(deltaTime, 1.0f / 240.0f, 0.1f);
    float blend = 1.0f - exp(-dt / tau);
	float adapted = max(lerp(previous, target, saturate(blend)), 1e-5f);
	// Exposure may brighten gradually, but a newly visible highlight is allowed
	// to pull exposure down immediately enough to avoid a white flash.
	if (target < adapted) {
		float immediateProtection = saturate(cameraHighlightProtection) * 0.82f;
		adapted = lerp(adapted, max(target, adapted * 0.25f), immediateProtection);
	}
	Exposure[uint2(0, 0)] = adapted;
}

