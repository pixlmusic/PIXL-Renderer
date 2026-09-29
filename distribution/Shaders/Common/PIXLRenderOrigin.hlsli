// PIXL Renderer - shared shader render-origin conversion helpers.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional permissions are described in the repository EXCEPTIONS.md.

#ifndef PIXL_RENDER_ORIGIN_HLSLI
#define PIXL_RENDER_ORIGIN_HLSLI
#include "Common/SharedData.hlsli"

namespace PIXLRenderOrigin
{
    bool Enabled() { return SharedData::RenderOriginFlags.x != 0u; }
    bool HistoryValid() { return SharedData::RenderOriginFlags.w != 0u; }
    uint Epoch() { return SharedData::RenderOriginFlags.y; }
    // Native positions are relative to FrameBuffer::CameraPosAdjust, not PIXL's
    // snapped origin. Never feed a PIXL position directly to an engine matrix.
    float3 EngineToRender(float3 p) { return p + SharedData::EngineToRenderOffset.xyz; }
    float3 RenderToEngine(float3 p) { return p - SharedData::EngineToRenderOffset.xyz; }
    float3 WorldToRender(float3 p) { return (p - SharedData::RenderOriginHigh.xyz) - SharedData::RenderOriginLow.xyz; }
    float3 RenderToWorld(float3 p) { return (p + SharedData::RenderOriginLow.xyz) + SharedData::RenderOriginHigh.xyz; }
    float3 CurrentToPreviousRender(float3 p) { return p + SharedData::RenderOriginDelta.xyz; }
    float3 PreviousRenderToEngine(float3 p) { return p - SharedData::PreviousEngineToRenderOffset.xyz; }

    float3 CurrentEngineToPreviousEngine(float3 p)
    {
        // Algebraic reduction of EngineToRender -> CurrentToPreviousRender ->
        // PreviousRenderToEngine avoids extra float cancellation near boundaries.
        return Enabled() ? p + SharedData::EngineOriginDelta.xyz :
            p + FrameBuffer::CameraPosAdjust.xyz - FrameBuffer::CameraPreviousPosAdjust.xyz;
    }
    float4 ProjectRender(float3 p)
    { return mul(FrameBuffer::CameraViewProj, float4(RenderToEngine(p), 1.0f)); }
    float4 ProjectPreviousRender(float3 p)
    { return mul(FrameBuffer::CameraPreviousViewProjUnjittered, float4(PreviousRenderToEngine(p), 1.0f)); }
    float3 ReconstructRenderPosition(float2 uv, float depth)
    {
        float4 p = mul(FrameBuffer::CameraViewProjInverse, float4(uv * float2(2,-2) + float2(-1,1), depth, 1));
        return EngineToRender(p.xyz / p.w);
    }
    // Difference between a native render point and an absolute persistent anchor.
    // Subtract the large anchors first; never add a small point to huge world xyz.
    float3 RelativeToWorldAnchor(float3 enginePosition, float3 anchor)
    {
        return Enabled() ? enginePosition + (FrameBuffer::CameraPosAdjust.xyz - anchor) :
            (enginePosition + FrameBuffer::CameraPosAdjust.xyz) - anchor;
    }
    // Stable periodic phase for power-of-two periods (the caller's contract).
    // Binary periods keep the high-origin remainder exact. Arbitrary periods
    // need a CPU double remainder. For non-periodic hashes use persistent
    // integer cell IDs, never epoch or render-local positions.
    float3 PeriodicWorldPosition(float3 renderPosition, float period)
    {
        period = max(period, 1.0f);
        float3 highPhase = SharedData::RenderOriginHigh.xyz - floor(SharedData::RenderOriginHigh.xyz / period) * period;
        return frac((renderPosition + SharedData::RenderOriginLow.xyz + highPhase) / period) * period;
    }
}
#endif
