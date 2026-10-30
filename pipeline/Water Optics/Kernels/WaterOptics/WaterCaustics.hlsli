// PIXL Water Optics v2.0 - receiver-aware, bounded, derivative-safe caustics.
// Public hooks, texture register t65, sampler, and constant-buffer ABI are unchanged.
Texture2D<float4> WaterCaustics : register(t65);

#ifndef USE_PIXL_PHYSICAL_CAUSTICS
#define USE_PIXL_PHYSICAL_CAUSTICS 1
#endif
#ifndef USE_PIXL_JACOBIAN_CAUSTICS
#define USE_PIXL_JACOBIAN_CAUSTICS 1
#endif
// 0 = minimum-cost; 1 = balanced release default; 2 = cinematic.
#ifndef PIXL_WATER_CAUSTICS_QUALITY
#define PIXL_WATER_CAUSTICS_QUALITY 1
#endif
#ifndef PIXL_WATER_CAUSTICS_VALIDATE_FOOTPRINT
#define PIXL_WATER_CAUSTICS_VALIDATE_FOOTPRINT 1
#endif

namespace WaterOptics
{
    // Set once in uniform pixel-shader flow, before divergent local-light loops.
    // No implicit derivatives are taken inside the caustic sampling functions.
    static float2 ReceiverCausticDx = 0.0f.xx;
    static float2 ReceiverCausticDy = 0.0f.xx;
    static const float CausticAtlasMean = 0.1401f; // Existing authored atlas contract.

    void SetReceiverFootprint(float3 worldPosition)
    {
        ReceiverCausticDx = ddx_coarse(worldPosition.xy) * 0.005f;
        ReceiverCausticDy = ddy_coarse(worldPosition.xy) * 0.005f;
    }

    float2 PanCausticsUV(float2 uv, float speed, float tiling)
    {
        return frac(uv * tiling + float2(SharedData::Timer * speed, 0.0f));
    }

    float2 PanCausticsUV(float2 uv, float2 velocity, float tiling)
    {
        return frac(uv * tiling + velocity * SharedData::Timer);
    }

    // Camera-independent water-normal proxy. The actual water simulation is not
    // exposed to this header; never claim this is a simulated Jacobian solution.
    float2 WarpReceiverUV(float2 waterWorldXY, float2 uv, float receiverHeight)
    {
        float2 phase = float2(
            dot(waterWorldXY, float2(0.012f, 0.007f)),
            dot(waterWorldXY, float2(-0.006f, 0.015f)));
        float2 bend = sin(phase + SharedData::Timer * float2(0.43f, -0.31f));
        float heightFade = smoothstep(0.0f, 300.0f, abs(receiverHeight));
        return uv + bend * lerp(0.003f, 0.008f, heightFade);
    }

    float CausticFootprint(float tiling)
    {
        float2 dx = ReceiverCausticDx * abs(tiling);
        float2 dy = ReceiverCausticDy * abs(tiling);
        return sqrt(max(dot(dx, dx), dot(dy, dy)));
    }

    float SampleCausticsScaled(float2 uv, float tiling)
    {
        float scale = max(abs(tiling), 1.0e-4f);
        return WaterCaustics.SampleGrad(
            SampColorSampler, uv,
            ReceiverCausticDx * scale, ReceiverCausticDy * scale).x;
    }

    float SampleCaustics(float2 uv)
    {
        return SampleCausticsScaled(uv, 1.0f);
    }

    float3 SampleCausticsDispersionScaled(float2 uv, float2 dispersionOffset, float tiling)
    {
        float center = SampleCausticsScaled(uv, tiling);
#if PIXL_WATER_CAUSTICS_QUALITY >= 2
        // Reduce separation as one screen pixel covers increasingly many folds.
        float dispersionFade = 1.0f - smoothstep(0.008f, 0.045f, CausticFootprint(tiling));
        float2 chromaticOffset = clamp(dispersionOffset, -0.009f.xx, 0.009f.xx) * dispersionFade;
        float3 dispersed = float3(
            SampleCausticsScaled(uv - chromaticOffset * 0.75f, tiling),
            center,
            SampleCausticsScaled(uv + chromaticOffset, tiling));
        return lerp(center.xxx, dispersed, 0.45f);
#else
        return center.xxx;
#endif
    }

    float3 SampleCausticsDispersion(float2 uv, float2 dispersionOffset)
    {
        return SampleCausticsDispersionScaled(uv, dispersionOffset, 1.0f);
    }

    float3 SampleFocusedCausticsScaled(float2 uv, float2 dispersionOffset, float tiling)
    {
        float center = SampleCausticsScaled(uv, tiling);
#if PIXL_WATER_CAUSTICS_QUALITY >= 1
        float footprint = CausticFootprint(tiling);
        // Balanced: center + a mirrored diagonal pair (3 samples vs 5).
        // Cinematic restores a five-tap cross for angularly uniform focus.
        float2 tap = clamp(max(float2(0.0017f, 0.0023f), footprint.xx * 0.60f),
            0.0017f.xx, 0.022f.xx);
#if PIXL_WATER_CAUSTICS_QUALITY >= 2
        float px = SampleCausticsScaled(uv + float2(tap.x, 0.0f), tiling);
        float nx = SampleCausticsScaled(uv - float2(tap.x, 0.0f), tiling);
        float py = SampleCausticsScaled(uv + float2(0.0f, tap.y), tiling);
        float ny = SampleCausticsScaled(uv - float2(0.0f, tap.y), tiling);
        float filtered = center * 0.50f + (px + nx + py + ny) * 0.125f;
        float curvature = (4.0f * center - px - nx - py - ny) * 0.125f;
#else
        float diagonalA = SampleCausticsScaled(uv + tap, tiling);
        float diagonalB = SampleCausticsScaled(uv - tap, tiling);
        float filtered = center * 0.50f + (diagonalA + diagonalB) * 0.25f;
        float curvature = (2.0f * center - diagonalA - diagonalB) * 0.25f;
#endif
        // Signed focus is energy-centered: no additive glow at empty texels.
        float focus = saturate(SharedData::waterOpticsSettings.CausticsFocus * 0.5f);
        float focusVisibility = 1.0f - smoothstep(0.008f, 0.040f, footprint);
        float intensity = max(0.0f, filtered + curvature * focus * focusVisibility);
#if PIXL_WATER_CAUSTICS_QUALITY >= 2
        float3 dispersive = SampleCausticsDispersionScaled(uv, dispersionOffset, tiling);
        return clamp(lerp(intensity.xxx, dispersive, 0.25f), 0.0f.xxx, 1.5f.xxx);
#else
        return min(intensity, 1.5f).xxx;
#endif
#else
        return center.xxx;
#endif
    }

    float3 SampleFocusedCaustics(float2 uv, float2 dispersionOffset)
    {
        return SampleFocusedCausticsScaled(uv, dispersionOffset, 1.0f);
    }

    float3 CombineCausticLayers(float3 firstLayer, float3 secondLayer)
    {
        // An arithmetic mixture has a stable mean. The legacy max-biased blend
        // systematically over-brightened intersecting patterns.
        return (firstLayer + secondLayer) * 0.5f;
    }

    float3 SampleMovingReceiverCaustics(
        float2 uv, float2 dispersionOffset, float2 velocity, float tiling, float seed)
    {
        float2 animated = PanCausticsUV(uv, velocity, tiling);
        float3 focused = SampleFocusedCausticsScaled(animated, dispersionOffset, tiling);
#if PIXL_WATER_CAUSTICS_QUALITY >= 2
        float phase = dot(uv, float2(0.071f, 0.053f)) +
            SharedData::Timer * (0.19f + seed * 0.07f);
        float focusWeight = lerp(0.65f, 0.95f, 0.5f + 0.5f * sin(phase));
        float softScale = tiling * 0.46f;
        float3 soft = SampleCausticsDispersionScaled(
            PanCausticsUV(uv + float2(0.013f, -0.009f), velocity * 0.42f, softScale),
            dispersionOffset * 0.45f, softScale);
        return lerp(soft, focused, focusWeight);
#else
        return focused;
#endif
    }

    float3 SampleWaterPattern(float2 projectedUV, float2 dispersion, float2 motionScale)
    {
        float3 a = SampleMovingReceiverCaustics(
            projectedUV, dispersion, float2(0.10f, 0.02f) * motionScale, 1.0f, 0.0f);
#if PIXL_WATER_CAUSTICS_QUALITY >= 1
        float3 b = SampleMovingReceiverCaustics(
            projectedUV + float2(0.17f, 0.31f), dispersion * 0.75f,
            float2(-0.065f, 0.085f) * motionScale, -0.57f, 1.0f);
        return CombineCausticLayers(a, b);
#else
        return a;
#endif
    }

    float3 CausticExtinction(float depthInSkyrimUnits)
    {
        // Tunable approximation to Beer-Lambert absorption; existing unit scale.
        float distanceMetres = max(depthInSkyrimUnits, 0.0f) * (1.0f / 70.0f);
        return exp(-float3(0.055f, 0.028f, 0.018f) * distanceMetres);
    }

    bool ValidWaterHeight(float waterHeight)
    {
        return isfinite(waterHeight) && waterHeight > -1.0e8f;
    }

    bool WaterFootprintMatches(float2 pointXY, float waterHeight)
    {
#if PIXL_WATER_CAUSTICS_VALIDATE_FOOTPRINT
        float4 footprint = SharedData::GetWaterData(float3(pointXY, waterHeight));
        return ValidWaterHeight(footprint.w) && abs(footprint.w - waterHeight) <= 8.0f;
#else
        return true;
#endif
    }

    float3 CausticEnergy(float3 atlasPattern)
    {
        // Keep the response mean-centered around 1.0 in light multiplier space.
        return clamp(atlasPattern * rcp(CausticAtlasMean), 0.30f.xxx, 3.0f.xxx);
    }

    float3 ComputeCaustics(float4 waterData, float3 worldPosition, float3 receiverNormal)
    {
        if (!ValidWaterHeight(waterData.w) || !all(isfinite(worldPosition)) ||
            !all(isfinite(receiverNormal)))
            return 1.0f.xxx;

        float3 sun = SharedData::DirLightDirection.xyz;
        float sunLengthSq = dot(sun, sun);
        if (!all(isfinite(sun)) || sunLengthSq < 1.0e-6f)
            return 1.0f.xxx;
        sun *= rsqrt(sunLengthSq);
        float sunVisibility = smoothstep(0.035f, 0.22f, sun.z);
        if (sunVisibility <= 0.0f)
            return 1.0f.xxx;

        float depth = waterData.w - worldPosition.z;
        float maxReach = clamp(SharedData::waterOpticsSettings.ProjectedCausticsDistance,
            64.0f, 800.0f);
        float safeSunZ = max(sun.z, 0.20f);
        float2 projectedXY = worldPosition.xy + sun.xy * (abs(depth) / safeSunZ);
        float3 normal = normalize(receiverNormal);
        float2 absoluteWaterXY = projectedXY + FrameBuffer::CameraPosAdjust.xy;

        if (depth < 0.0f)
        {
            // Reflected, above-water bounce is intentionally limited to nearby
            // walls/ceilings and an actual water footprint (not the whole water tile).
            if (SharedData::waterOpticsSettings.EnableEnhancedCaustics == 0)
                return 1.0f.xxx;
            float height = -depth;
            if (height >= maxReach)
                return 1.0f.xxx;
            float receiverFade = 1.0f - smoothstep(16.0f, maxReach, height);
            receiverFade *= 1.0f - smoothstep(65.0f, maxReach * 0.92f,
                length(projectedXY - worldPosition.xy));
            float3 towardWater = normalize(float3(-sun.xy, -sun.z));
            float normalResponse = smoothstep(0.0f, 0.45f,
                saturate(dot(normal, towardWater)));
            if (receiverFade * normalResponse <= 1.0e-4f ||
                !WaterFootprintMatches(projectedXY, waterData.w))
                return 1.0f.xxx;

            float2 uv = WarpReceiverUV(absoluteWaterXY, absoluteWaterXY * 0.005f, height);
            float2 dispersion = float2(0.006f, 0.008f) *
                saturate(height / maxReach) *
                saturate(SharedData::waterOpticsSettings.CausticsDispersion);
            float3 energy = CausticEnergy(SampleWaterPattern(uv, dispersion, 1.0f.xx));
            float strength = clamp(SharedData::waterOpticsSettings.ProjectedCausticsStrength,
                0.0f, 3.0f);
            float footprintFade = 1.0f - smoothstep(0.018f, 0.065f,
                CausticFootprint(1.0f));
            float3 bounce = max(energy - 1.0f.xxx, 0.0f.xxx) *
                strength * receiverFade * normalResponse * sunVisibility * footprintFade * 0.32f;
            return 1.0f.xxx + min(bounce, 1.20f.xxx);
        }

        // Underwater refracted light. Preserve coverage against dry neighboring
        // tiles, attenuate the modulation with optical path and pixel footprint.
        if (depth <= 0.0f || depth > 1800.0f)
            return 1.0f.xxx;
#if USE_PIXL_JACOBIAN_CAUSTICS
        if (SharedData::waterOpticsSettings.EnableEnhancedCaustics != 0 &&
            !WaterFootprintMatches(projectedXY, waterData.w))
            return 1.0f.xxx;
#endif
        float nearWaterFade = smoothstep(0.0f, 32.0f, depth);
        float deepFade = 1.0f - smoothstep(320.0f, 1450.0f, depth);
        if (nearWaterFade * deepFade <= 1.0e-4f)
            return 1.0f.xxx;
        float2 uv = absoluteWaterXY * 0.005f;
        float dispersionScale = saturate(depth / 256.0f) *
            saturate(SharedData::waterOpticsSettings.CausticsDispersion);
        float2 dispersion = float2(0.006f, 0.008f) * dispersionScale;
        float3 pattern = SampleWaterPattern(uv, dispersion, 1.0f.xx);
#if USE_PIXL_PHYSICAL_CAUSTICS
        float3 energy = CausticEnergy(pattern);
        float strength = clamp(SharedData::waterOpticsSettings.CausticsStrength,
            0.0f, 2.0f);
        float visibility = clamp(SharedData::waterOpticsSettings.CausticsVisibility,
            0.0f, 2.0f);
        float orientation = lerp(0.30f, 1.0f,
            saturate(dot(normal, sun)));
        float footprintFade = 1.0f - smoothstep(0.022f, 0.075f,
            CausticFootprint(1.0f));
        float3 modulation = (energy - 1.0f.xxx) * CausticExtinction(depth) *
            (strength * visibility * orientation * nearWaterFade * deepFade *
            sunVisibility * footprintFade);
        return clamp(1.0f.xxx + modulation, 0.35f.xxx, 3.25f.xxx);
#else
        return lerp(1.0f.xxx, pattern * 4.0f, nearWaterFade * deepFade * sunVisibility);
#endif
    }

    float3 ComputeLocalReceiverCaustics(
        float4 waterData,
        float3 worldPosition,
        float3 receiverNormal,
        float3 lightPosition,
        float3 lightColor,
        float lightRadius)
    {
        if (SharedData::waterOpticsSettings.EnableEnhancedCaustics == 0 ||
            !ValidWaterHeight(waterData.w) || !all(isfinite(worldPosition)) ||
            !all(isfinite(lightPosition)) || !all(isfinite(lightColor)) ||
            !isfinite(lightRadius) || lightRadius <= 1.0f)
            return 1.0f.xxx;

        float waterHeight = waterData.w;
        float receiverHeight = worldPosition.z - waterHeight;
        float lightHeight = lightPosition.z - waterHeight;
        float reach = clamp(SharedData::waterOpticsSettings.ProjectedCausticsDistance,
            64.0f, 800.0f);
        if (receiverHeight > reach || receiverHeight < -560.0f ||
            abs(lightHeight) <= 0.5f)
            return 1.0f.xxx;

        // Cheap rejection BEFORE water lookups and animated atlas samples.
        float3 toLight = lightPosition - worldPosition;
        float radius = max(lightRadius * 1.25f, 40.0f);
        if (dot(toLight, toLight) > radius * radius * 3.0f)
            return 1.0f.xxx;
        float luminance = dot(max(lightColor, 0.0f.xxx),
            float3(0.2126f, 0.7152f, 0.0722f));
        if (luminance <= 1.0e-5f)
            return 1.0f.xxx;

        // Refraction when emitter and receiver lie on opposite sides of water;
        // virtual-source reflection when they occupy the same side.
        float3 mirrorLight = float3(lightPosition.xy,
            2.0f * waterHeight - lightPosition.z);
        float3 projectedSource = receiverHeight * lightHeight < 0.0f
            ? lightPosition : mirrorLight;
        float3 ray = worldPosition - projectedSource;
        if (abs(ray.z) < 1.0e-4f)
            return 1.0f.xxx;
        float t = (waterHeight - projectedSource.z) / ray.z;
        if (t <= 0.0f || t >= 1.0f)
            return 1.0f.xxx;
        float3 waterPoint = projectedSource + ray * t;
        float sourcePath = length(waterPoint - lightPosition);
        float coverage = 1.0f - smoothstep(radius * 0.20f, radius * 1.20f,
            sourcePath);
        float receiverFade = receiverHeight > 0.0f
            ? 1.0f - smoothstep(16.0f, reach, receiverHeight)
            : 1.0f - smoothstep(280.0f, 560.0f, -receiverHeight);
        float3 toSurface = normalize(waterPoint - worldPosition);
        float facing = receiverHeight > 0.0f
            ? saturate(dot(normalize(receiverNormal), toSurface))
            : lerp(0.25f, 1.0f,
                saturate(abs(dot(normalize(receiverNormal), -toSurface))));
        if (coverage * receiverFade * facing <= 1.0e-4f ||
            !WaterFootprintMatches(waterPoint.xy, waterHeight))
            return 1.0f.xxx;

        float2 absoluteXY = waterPoint.xy + FrameBuffer::CameraPosAdjust.xy;
        float2 uv = absoluteXY * 0.0042f;
        if (receiverHeight > 0.0f)
            uv = WarpReceiverUV(absoluteXY, uv, receiverHeight);
        float2 dispersion = float2(0.006f, 0.008f) *
            saturate(SharedData::waterOpticsSettings.CausticsDispersion);
        // Balanced tier: two focused wave bands instead of three multi-octave
        // bands, each with an extra soft/chromatic pass inside every light loop.
        float3 energy = CausticEnergy(SampleWaterPattern(uv, dispersion, 1.0f.xx));
        float receiverStrength = receiverHeight > 0.0f
            ? clamp(SharedData::waterOpticsSettings.ProjectedCausticsStrength,
                0.0f, 3.0f)
            : clamp(SharedData::waterOpticsSettings.CausticsVisibility *
                SharedData::waterOpticsSettings.CausticsStrength, 0.0f, 3.0f);
        float footprintFade = 1.0f - smoothstep(0.020f, 0.070f,
            CausticFootprint(1.0f));
        float amount = receiverStrength * coverage * receiverFade * facing *
            footprintFade;
        if (receiverHeight > 0.0f)
        {
            float3 reflected = max(energy - 1.0f.xxx, 0.0f.xxx) * amount * 0.72f;
            return 1.0f.xxx + min(reflected, 1.25f.xxx);
        }
        float3 transmitted = (energy - 1.0f.xxx) * CausticExtinction(-receiverHeight) * amount;
        return clamp(1.0f.xxx + transmitted, 0.35f.xxx, 2.35f.xxx);
    }
}
