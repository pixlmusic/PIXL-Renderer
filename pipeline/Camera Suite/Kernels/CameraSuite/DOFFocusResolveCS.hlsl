// PIXL Renderer - robust per-frame depth autofocus resolver.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional permissions are described in the repository EXCEPTIONS.md.

#include "Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"
#include "CameraSuite/DofControl.hlsli"

Texture2D<float4> PreviousFocus : register(t0);
SamplerState LinearClampSampler : register(s0);
RWTexture2D<float4> FocusOut : register(u0);

float ReadFocusDepth(float2 uv, out bool valid)
{
    float raw = SharedData::DepthTexture.SampleLevel(LinearClampSampler, saturate(uv), 0.0f).x;
    valid = raw > 1.0e-5f && raw < 0.999998f && isfinite(raw);
    return valid ? SharedData::GetScreenDepth(raw) : 0.0f;
}

[numthreads(1, 1, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID)
{
    if (dispatchID.x != 0 || dispatchID.y != 0)
        return;

    // A compact centre ROI rejects a single bad depth sample while keeping the
    // focus query constant-cost regardless of the output resolution.
    const float2 offsets[13] = {
        float2(0, 0),
        float2(-0.006, 0), float2(0.006, 0), float2(0, -0.006), float2(0, 0.006),
        float2(-0.012, -0.012), float2(0.012, -0.012),
        float2(-0.012, 0.012), float2(0.012, 0.012),
        float2(-0.024, 0), float2(0.024, 0), float2(0, -0.024), float2(0, 0.024)
    };
    float weightedDepth = 0.0f;
    float weight = 0.0f;
    float nearestDepth = 1.0e20f;
    float centreDepth = 0.0f;
    float validDepths[13];
    [unroll]
    for (uint i = 0; i < 13; ++i) {
        bool valid;
        float depth = ReadFocusDepth(0.5f.xx + offsets[i], valid);
        validDepths[i] = 0.0f;
        if (!valid || depth <= 1.0f || depth >= 2000000.0f)
            continue;
		// First-person arms, weapons and spell geometry occupy the near camera
		// volume and must neither steal autofocus nor pull a rack focus away from
		// the world-space subject under the crosshair.
		if (DofFirstPersonView() && depth < 220.0f)
			continue;
		// Reject the player back inside the third-person camera boom without
		// suppressing genuine close world targets beyond the character.
		if (DofThirdPersonView() && depth < 520.0f)
			continue;
		validDepths[i] = depth;
		if (i == 0)
			centreDepth = depth;
		float sampleWeight = i == 0 ? 3.0f : (i < 5 ? 1.5f : 1.0f);
        weightedDepth += depth * sampleWeight;
        weight += sampleWeight;
        nearestDepth = min(nearestDepth, depth);
    }
    float target = weight > 0.0f ? weightedDepth / weight : dofControlFocusDistance;
    // Resolve a compact cluster around the nearest hit. This lets a thin tree,
    // weapon or fence under the crosshair win against the distant background,
    // but requires either the centre sample or multiple nearby supporting hits.
    float clusterSum = 0.0f;
    float clusterWeight = 0.0f;
    float clusterLimit = nearestDepth * 1.20f + 8.0f;
    [unroll]
    for (uint j = 0; j < 13; ++j) {
        if (validDepths[j] > 0.0f && validDepths[j] <= clusterLimit) {
            float sampleWeight = j == 0 ? 3.0f : (j < 5 ? 1.5f : 1.0f);
            clusterSum += validDepths[j] * sampleWeight;
            clusterWeight += sampleWeight;
        }
    }
    bool centredSubject = centreDepth > 0.0f && centreDepth < target * 0.92f;
    bool supportedThinSubject = clusterWeight >= 2.5f && nearestDepth < target * 0.80f;
    if (centredSubject || supportedThinSubject)
        target = clusterSum / max(clusterWeight, 1.0f);
    else if (nearestDepth < target * 0.72f)
        target = lerp(target, nearestDepth, 0.30f);

    float previousDistance = PreviousFocus.Load(int3(0, 0, 0)).x;
    bool previousValid = dofControlHistoryValid != 0u && PreviousFocus.Load(int3(0, 0, 0)).y > 0.5f;
    float previousDiopter = 1.0f / max(previousDistance, 1.0f);
    float targetDiopter = 1.0f / max(target, 1.0f);
    float delta = abs(targetDiopter - previousDiopter);
    float deadband = max(dofControlFocusDeadband * targetDiopter, 1.0e-5f);
    if (previousValid && delta < deadband)
        targetDiopter = previousDiopter;
    else if (previousValid) {
		// Time-based diopter smoothing keeps rack focus identical at 30, 60 and
		// high refresh rates. The old fixed 1/60 step changed response speed and
		// visible pumping with frame rate.
		float response = 1.0f - exp(-max(dofControlFocusSpeed, 0.1f) * clamp(dofControlDeltaTime, 1.0f / 240.0f, 0.1f));
        targetDiopter = lerp(previousDiopter, targetDiopter, response);
    }
    FocusOut[uint2(0, 0)] = float4(1.0f / max(targetDiopter, 1.0e-5f), 1.0f, targetDiopter, 0.0f);
}
