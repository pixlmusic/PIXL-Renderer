#ifndef PIXL_GROUND_RESPONSE_RUNTIME_HLSLI
#define PIXL_GROUND_RESPONSE_RUNTIME_HLSLI

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
};

namespace GroundResponseRuntime
{
    static const uint RuntimeMagic = 0x47523330u;   // "GR30"
    static const uint RuntimeVersion = 0x00030100u;
    static const uint DebugOverlayBit = 1u << 0;
    static const uint GeometrySelfTestBit = 1u << 1;

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

    float GetTerrainSnowCoverage(float4 weights1, float2 weights2)
    {
        if (!IsRuntimeValid() || GroundRuntimeTerrainSnowValid == 0u)
            return 0.0f;

        float4 w1;
        float2 w2 = NormalizeTerrainWeights(weights1, weights2, w1);
        float coverage =
            dot(w1, saturate(GroundRuntimeTerrainSnow1to4)) +
            dot(w2, saturate(GroundRuntimeTerrainSnow5to6.xy));
        return saturate(coverage);
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
                SharedData::rainResponseSettings.Wetness));
        return smoothstep(
            max(threshold - 0.22f, 0.0f),
            min(threshold + 0.18f, 1.0f),
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
        const float aboveWater = smoothstep(-2.0f, 5.0f, terrainAboveWater);
        const float shorelineBand =
            1.0f - smoothstep(5.0f, 64.0f, terrainAboveWater);
        return aboveWater * shorelineBand;
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
