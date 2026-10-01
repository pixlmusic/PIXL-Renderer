// PIXL Renderer - Auto-DOF private shader ABI.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional permissions are described in the repository EXCEPTIONS.md.
// Private CameraSuite DOF ABI. Kept separate from the legacy HDRDataCB.
cbuffer DofControl : register(b1)
{
    float dofControlFocusDistance;
    float dofControlFocusDiopter;
    float dofControlFocusSpeed;
    float dofControlFocusDeadband;
    float dofControlFocalLengthMm;
    float dofControlFStop;
    float dofControlSensorHeightMm;
    float dofControlMaxCoCPixels;
    float dofControlBokehRadius;
    float dofControlStrength;
    float dofControlHighlightResponse;
    float dofControlFocusEdgeProtection;
    float dofControlForegroundCoverage;
    float dofControlNearBlurIntensity;
    float dofControlFarBlurIntensity;
    float dofControlCatEye;
    float dofControlAnamorphicRatio;
    float dofControlApertureRotation;
    float dofControlBladeCurvature;
    float dofControlApertureBlades;
    float dofControlQuality;
    float dofControlFocusMode;
    float dofControlRenderWidth;
    float dofControlRenderHeight;
    float dofControlInvRenderWidth;
    float dofControlInvRenderHeight;
    uint dofControlFrameIndex;
    uint dofControlFlags;
    uint dofControlHistoryValid;
    float dofControlDeltaTime;
    float dofControlFarBlurDistance;
    uint dofControlPad2;
};

bool DofPhysicalLensEnabled()
{
    return (dofControlFlags & 1u) != 0u;
}

bool DofFirstPersonView()
{
    return (dofControlFlags & 2u) != 0u;
}

bool DofDirectorPresentation()
{
    return (dofControlFlags & 4u) != 0u;
}

bool DofTemporalReconstructionEnabled()
{
    return (dofControlFlags & 8u) != 0u;
}

bool DofThirdPersonView()
{
    return (dofControlFlags & 16u) != 0u;
}
