#ifndef PIXL_GROUND_RESPONSE_RUNTIME_HLSLI
#define PIXL_GROUND_RESPONSE_RUNTIME_HLSLI

#include "GroundResponse/GroundResponseSharedConstants.inl"

// PIXL Ground Response 3.0 runtime ABI (b13).
//
// The first 48 bytes remain byte-for-byte compatible with legacy
// CollisionUpdateCS b0 so grass collision keeps its existing path.
// The terrain-only tail now also carries the ABSOLUTE XY origin of the dedicated
// normalized snow/mud deformation field.

cbuffer GroundResponseRuntimeCB : register(b13)
{
    float2 GroundRuntimePosOffset;
    uint2 GroundRuntimeArrayOrigin;

    int2 GroundRuntimeValidMargin;
    float GroundRuntimeTimeDelta;
    uint GroundRuntimeBoundingBoxCount;

    float GroundRuntimeCameraHeightDelta;
    uint GroundRuntimeDebugInteractionField;
    float GroundRuntimeTrackHoldSeconds;
    float GroundRuntimeTrackRecoveryRate;

    float4 GroundRuntimeTerrainSnow1to4;
    float4 GroundRuntimeTerrainSnow5to6;

    uint GroundRuntimeTerrainSnowValid;
    uint GroundRuntimeTerrainGeometryPass;
    uint GroundRuntimeTerrainMaterialForge;
    uint GroundRuntimeTerrainDebug;

    float GroundRuntimeSnowSurfaceThickness;
    float GroundRuntimeGeometryRenderDistance;
    float GroundRuntimeGeometryFadeStart;
    float GroundRuntimeGeometryMinimumSlopeZ;

    float GroundRuntimeGeometryTessellationNear;
    float GroundRuntimeGeometryTessellationFar;
    float GroundRuntimeGeometryTessellationNearDistance;
    float GroundRuntimeGeometryTessellationFarDistance;

    float GroundRuntimeSnowCoverageThreshold;
    float GroundRuntimeSnowCoverageFeather;
    uint GroundRuntimeMagic;
    uint GroundRuntimeVersion;

    float2 GroundRuntimeSurfaceOriginAbsolute;
    uint2 GroundRuntimeSurfaceArrayOrigin;

    float GroundRuntimeWeatherSnowRaise;
    float GroundRuntimePreviousWeatherSnowRaise;
    float GroundRuntimeWeatherSnowIntensity;
    uint GroundRuntimeWeatherSnowEnabled;

    float GroundRuntimeWeatherSnowTrackCover;
    float GroundRuntimePreviousWeatherSnowTrackCover;
    float GroundRuntimeSurfaceMoisture;
    float GroundRuntimeSurfaceThermalState;

    float GroundRuntimeWeatherWindIntensity;
    float GroundRuntimeEnvironmentExterior;
    float GroundRuntimeEnvironmentPad0;
    float GroundRuntimeEnvironmentPad1;
};

namespace GroundResponseRuntime
{
    static const uint RuntimeMagic = 0x47523330u;   // "GR30"
    static const uint RuntimeVersion = 0x00030200u;
    static const uint DebugOverlayBit = 1u << 0;
    static const uint GeometrySelfTestBit = 1u << 1;
    // Phase 3 developer comparison switch. It deliberately reuses an existing
    // debug word. Phase 5 deliberately appends its versioned environment tail
    // after the existing terrain runtime fields rather than repurposing this.
    static const uint LegacyTerrainSurfaceBit = 1u << 4;
    static const uint GroundMarksDebugBit = 1u << 5;

    bool IsRuntimeValid()
    {
        return GroundRuntimeMagic == RuntimeMagic &&
               GroundRuntimeVersion == RuntimeVersion;
    }

    bool DebugOverlayEnabled()
    {
        return IsRuntimeValid() &&
               SharedData::deformableGroundSettings.EnableDeformableGround != 0u &&
               (GroundRuntimeTerrainDebug & DebugOverlayBit) != 0u;
    }

    bool GeometrySelfTestEnabled()
    {
        return IsRuntimeValid() &&
               SharedData::deformableGroundSettings.EnableDeformableGround != 0u &&
               (GroundRuntimeTerrainDebug & GeometrySelfTestBit) != 0u;
    }

    float2 NormalizeTerrainWeights(
        float4 weights1,
        float2 weights2,
        out float4 normalized1)
    {
        float total =
            dot(max(weights1, 0.0f.xxxx), 1.0f.xxxx) +
            dot(max(weights2, 0.0f.xx), 1.0f.xx);
        float invTotal = rcp(max(total, 1e-6f));
        normalized1 = max(weights1, 0.0f.xxxx) * invTotal;
        return max(weights2, 0.0f.xx) * invTotal;
    }

    // TerrainSnow transports snow *and* hard/excluded layer information:
    // GroundResponse.cpp encodes hard layers as negative values. Preserve the
    // sign before deriving snow coverage; saturating first made hard layers look
    // like ordinary non-snow mud terrain.
    void GetTerrainSurfaceCoverage(
        float4 weights1,
        float2 weights2,
        out float snowCoverage,
        out float hardCoverage,
        out float softGroundCoverage)
    {
        snowCoverage = 0.0f;
        hardCoverage = 0.0f;
        softGroundCoverage = 0.0f;
        if (!IsRuntimeValid() || GroundRuntimeTerrainSnowValid == 0u)
            return;

        float4 w1;
        float2 w2 = NormalizeTerrainWeights(weights1, weights2, w1);
        float4 flags1 = GroundRuntimeTerrainSnow1to4;
        float2 flags2 = GroundRuntimeTerrainSnow5to6.xy;
        snowCoverage = saturate(
            dot(w1, saturate(flags1)) +
            dot(w2, saturate(flags2)));
        hardCoverage = saturate(
            dot(w1, step(flags1, -1.0e-4f.xxxx)) +
            dot(w2, step(flags2, -1.0e-4f.xx)));
        softGroundCoverage = saturate(1.0f - hardCoverage);
    }

    bool LegacyTerrainSurfaceEnabled()
    {
        return IsRuntimeValid() &&
               (GroundRuntimeTerrainDebug & LegacyTerrainSurfaceBit) != 0u;
    }

    bool GroundMarksDebugEnabled()
    {
        return IsRuntimeValid() &&
               (GroundRuntimeTerrainDebug & GroundMarksDebugBit) != 0u;
    }

    float GetTerrainSnowCoverage(float4 weights1, float2 weights2)
    {
        float snowCoverage;
        float hardCoverage;
        float softGroundCoverage;
        GetTerrainSurfaceCoverage(
            weights1,
            weights2,
            snowCoverage,
            hardCoverage,
            softGroundCoverage);
        return snowCoverage;
    }

    float GetSnowSurfaceMask(float snowCoverage)
    {
        float threshold = saturate(GroundRuntimeSnowCoverageThreshold);
        float feather = max(GroundRuntimeSnowCoverageFeather, 1e-3f);
        return smoothstep(
            threshold,
            min(threshold + feather, 1.0f),
            saturate(snowCoverage));
    }

    // Mud uses the exact same raised-shell/compression architecture as snow,
    // just with a thinner physical layer. The previous 15% film was often too
    // small to read geometrically; 30% keeps ruts visible while still exposing
    // the authored rock/soil underneath.
    float GetMudSurfaceThickness()
    {
        float fromSnow =
            max(GroundRuntimeSnowSurfaceThickness, 0.0f) * 0.30f;
        float fromRutDepth =
            max(
                SharedData::deformableGroundSettings.MudMaximumDepth,
                0.0f) * 0.26f;
        return clamp(max(fromSnow, fromRutDepth), 1.5f, 8.0f);
    }

    float GetMudWetnessActivation()
    {
        if (SharedData::deformableGroundSettings.EnableMudDeformation == 0u)
            return 0.0f;

        if (SharedData::deformableGroundSettings.MudRequiresWetness == 0u)
            return 1.0f;

        float threshold =
            saturate(
                SharedData::deformableGroundSettings.MudWetnessThreshold);
        float mudWeatherSignal =
            saturate(max(
                SharedData::rainResponseSettings.Raining,
                max(
                    SharedData::rainResponseSettings.Wetness,
                    GroundRuntimeSurfaceMoisture)));
        return smoothstep(
                max(threshold - PIXL_GR_MUD_WETNESS_LOWER_BAND, 0.0f),
                min(threshold + PIXL_GR_MUD_WETNESS_UPPER_BAND, 1.0f),
            mudWeatherSignal);
    }

    // WaterData is the renderer's existing 5x5 cell water-height lookup. Use
    // only the narrow band of walkable terrain just above a valid water plane;
    // this gives river/lake margins the same wet-weather activation as rain
    // without making an entire dry cell muddy or affecting submerged terrain.
    float GetWaterShoreMudActivation(float3 cameraRelativePosition)
    {
        if (SharedData::InInterior ||
            SharedData::deformableGroundSettings.EnableMudDeformation == 0u)
            return 0.0f;

        const float waterHeight =
            SharedData::GetWaterData(cameraRelativePosition).w;
        if (waterHeight < -1.0e20f)
            return 0.0f;

        const float terrainAboveWater = cameraRelativePosition.z - waterHeight;
        const float aboveWater = smoothstep(
            -PIXL_GR_SHORELINE_BELOW_WATER_FADE,
            PIXL_GR_SHORELINE_ABOVE_WATER_FULL,
            terrainAboveWater);
        const float shorelineBand =
            1.0f - smoothstep(
                PIXL_GR_SHORELINE_ABOVE_WATER_FULL,
                PIXL_GR_SHORELINE_ABOVE_WATER_FADE,
                terrainAboveWater);
        return aboveWater * shorelineBand;
    }

    float GetMudWetnessActivation(float3 cameraRelativePosition)
    {
        if (SharedData::deformableGroundSettings.EnableMudDeformation == 0u)
            return 0.0f;
        if (SharedData::deformableGroundSettings.MudRequiresWetness == 0u)
            return 1.0f;

        float threshold = saturate(
            SharedData::deformableGroundSettings.MudWetnessThreshold);
        float mudSignal = saturate(max(
            SharedData::rainResponseSettings.Raining,
            max(
                SharedData::rainResponseSettings.Wetness,
                max(
                    GroundRuntimeSurfaceMoisture,
                    GetWaterShoreMudActivation(cameraRelativePosition)))));
        return smoothstep(
            max(threshold - PIXL_GR_MUD_WETNESS_LOWER_BAND, 0.0f),
            min(threshold + PIXL_GR_MUD_WETNESS_UPPER_BAND, 1.0f),
            mudSignal);
    }

    // Canonical terrain material model. It keeps snow, hard, and soft coverage
    // distinct; snow may blend over rock, while rock itself stays non-deformable.
    void GetTerrainSurfaceActivations(
        float4 weights1,
        float2 weights2,
        float3 cameraRelativePosition,
        out float snowCoverage,
        out float hardCoverage,
        out float softGroundCoverage,
        out float snowActivation,
        out float mudActivation)
    {
        GetTerrainSurfaceCoverage(
            weights1,
            weights2,
            snowCoverage,
            hardCoverage,
            softGroundCoverage);
        float snowMask = GetSnowSurfaceMask(snowCoverage);
        snowActivation =
            SharedData::deformableGroundSettings.EnableSnowDeformation != 0u
                ? snowMask * softGroundCoverage
                : 0.0f;
        mudActivation =
            SharedData::deformableGroundSettings.EnableMudDeformation != 0u
                ? softGroundCoverage * (1.0f - snowMask) *
                    GetMudWetnessActivation(cameraRelativePosition)
                : 0.0f;
    }

    void GetSurfaceActivations(
        float snowCoverage,
        out float snowActivation,
        out float mudActivation)
    {
        float snowMask = GetSnowSurfaceMask(snowCoverage);

        snowActivation =
            SharedData::deformableGroundSettings.EnableSnowDeformation != 0u
                ? snowMask
                : 0.0f;

        mudActivation =
            SharedData::deformableGroundSettings.EnableMudDeformation != 0u
                ? (1.0f - snowMask) * GetMudWetnessActivation()
                : 0.0f;
    }

    float GetUnifiedSurfaceThickness(float snowCoverage)
    {
        float snowActivation;
        float mudActivation;
        GetSurfaceActivations(
            snowCoverage,
            snowActivation,
            mudActivation);

        return
            max(GroundRuntimeSnowSurfaceThickness, 0.0f) *
                snowActivation +
            GetMudSurfaceThickness() *
                mudActivation;
    }

    // Compressed material never cuts the replay below Skyrim's base terrain.
    // A small packed floor remains, matching the visual target: deep snow leaves
    // a thin packed layer, while mud can compress almost entirely to the base.
    float GetUnifiedCompressedFloor(float snowCoverage)
    {
        float snowActivation;
        float mudActivation;
        GetSurfaceActivations(
            snowCoverage,
            snowActivation,
            mudActivation);

        float snowThickness =
            max(GroundRuntimeSnowSurfaceThickness, 0.0f);
        float mudThickness =
            GetMudSurfaceThickness();

        float snowFloor =
            min(
                snowThickness,
                max(0.75f, snowThickness * 0.12f));
        float mudFloor =
            min(
                mudThickness,
                max(0.08f, mudThickness * 0.02f));

        return
            snowFloor * snowActivation +
            mudFloor * mudActivation;
    }

    float GetUnifiedMaximumCompressionDepth(float snowCoverage)
    {
        float snowActivation;
        float mudActivation;
        GetSurfaceActivations(
            snowCoverage,
            snowActivation,
            mudActivation);

        float thickness =
            GetUnifiedSurfaceThickness(snowCoverage);
        float floorThickness =
            GetUnifiedCompressedFloor(snowCoverage);
        float physicalCapacity =
            max(thickness - floorThickness, 0.0f);

        // The user-facing depth controls are upper limits. They may not carve
        // through the packed material floor or below Skyrim's original terrain.
        float requestedDepth =
            max(
                SharedData::deformableGroundSettings.SnowMaximumDepth,
                0.0f) *
                snowActivation +
            max(
                SharedData::deformableGroundSettings.MudMaximumDepth,
                0.0f) *
                mudActivation;

        return min(physicalCapacity, requestedDepth);
    }

    float GetUnifiedSurfaceActivation(float snowCoverage)
    {
        float snowActivation;
        float mudActivation;
        GetSurfaceActivations(
            snowCoverage,
            snowActivation,
            mudActivation);
        return saturate(snowActivation + mudActivation);
    }

    bool HasExactTerrainClassification()
    {
        return IsRuntimeValid() &&
               GroundRuntimeTerrainSnowValid != 0u;
    }

    bool IsGeometryPass()
    {
        return IsRuntimeValid() &&
               GroundRuntimeTerrainGeometryPass != 0u;
    }
}

#endif
