// PIXL Renderer - bilateral half-resolution DOF foundation.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional permissions are described in the repository EXCEPTIONS.md.

#include "Common/Color.hlsli"
#include "Common/DisplayMapping.hlsli"
#include "Common/SharedData.hlsli"
#include "CameraSuite/PhysicalCameraCommon.hlsli"
#include "CameraSuite/DofControl.hlsli"

Texture2D<float4> SceneTex : register(t0);
SamplerState LinearClampSampler : register(s0);
RWTexture2D<float4> HalfSceneOut : register(u0);

float3 LoadScene(float2 uv)
{
    float3 c = SceneTex.SampleLevel(LinearClampSampler, saturate(uv), 0.0f).rgb;
    float3 linearScene = isSceneLinear > 0.5f ? max(c, 0.0f) : Color::GammaToLinearSafe(max(c, 0.0f));
    // Match HDROutputCS::DecodeScene exactly. Without this recovery, the
    // half-resolution DOF layer is darker than the sharp AutoHDR scene and
    // the blend reads as an unwanted atmospheric veil.
    if (applyAutoHDR > 0.5f && isSceneLinear <= 0.5f)
        linearScene = DisplayMapping::PumboAutoHDR(linearScene, SharedData::HDRData.z, SharedData::HDRData.y, 2.25, 1.0);
    return max(linearScene, 0.0f);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint width, height;
    HalfSceneOut.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) return;
    float2 uv = (float2(id.xy) + 0.5f) / float2(width, height);
    float2 stepUV = float2(dofControlInvRenderWidth, dofControlInvRenderHeight);
    float3 a = LoadScene(uv + stepUV * float2(-0.5f, -0.5f));
    float3 b = LoadScene(uv + stepUV * float2( 0.5f, -0.5f));
    float3 c = LoadScene(uv + stepUV * float2(-0.5f,  0.5f));
    float3 d = LoadScene(uv + stepUV * float2( 0.5f,  0.5f));
    // Exposure-preserving average; keeping alpha at one makes this usable as
    // a normal scene colour source for the existing gather.
    HalfSceneOut[id.xy] = float4((a + b + c + d) * 0.25f, 1.0f);
}
