#ifndef PIXL_EYE_RENDERING_HLSLI
#define PIXL_EYE_RENDERING_HLSLI

// PIXL Renderer - Eye Rendering optical core
// Phase 1: ABI-free. No resources, cbuffers, UAVs, SRVs, or register assignments.
//
// Supplies physical corneal optics, scope/close-up aware roughness,
// local-light-driven pupil targets, and continuous iris/pupil UV remapping.
// Persistent asymmetric adaptation is deliberately left to Phase 2 because it
// requires state across frames. This file computes the instantaneous TARGET.

#ifndef USE_PIXL_EYE_OPTICS
# define USE_PIXL_EYE_OPTICS 1
#endif

#ifndef USE_PIXL_DYNAMIC_PUPILS
# define USE_PIXL_DYNAMIC_PUPILS 1
#endif

#ifndef PIXL_EYE_CORNEA_IOR
# define PIXL_EYE_CORNEA_IOR 1.376f
#endif
#ifndef PIXL_EYE_CORNEA_ROUGHNESS_CLOSE
# define PIXL_EYE_CORNEA_ROUGHNESS_CLOSE 0.045f
#endif
#ifndef PIXL_EYE_CORNEA_ROUGHNESS_FAR
# define PIXL_EYE_CORNEA_ROUGHNESS_FAR 0.085f
#endif
#ifndef PIXL_EYE_CLOSEUP_FOOTPRINT_MIN
# define PIXL_EYE_CLOSEUP_FOOTPRINT_MIN 0.0015f
#endif
#ifndef PIXL_EYE_CLOSEUP_FOOTPRINT_MAX
# define PIXL_EYE_CLOSEUP_FOOTPRINT_MAX 0.0180f
#endif

// Conventional Skyrim eye-texture defaults. Eye replacers can tune these only.
#ifndef PIXL_EYE_IRIS_CENTER
# define PIXL_EYE_IRIS_CENTER float2(0.5f, 0.5f)
#endif
#ifndef PIXL_EYE_IRIS_RADIUS_UV
# define PIXL_EYE_IRIS_RADIUS_UV 0.235f
#endif
#ifndef PIXL_EYE_AUTHORED_PUPIL_RADIUS_UV
# define PIXL_EYE_AUTHORED_PUPIL_RADIUS_UV 0.082f
#endif
#ifndef PIXL_EYE_MIN_PUPIL_RADIUS_UV
# define PIXL_EYE_MIN_PUPIL_RADIUS_UV 0.046f
#endif
#ifndef PIXL_EYE_MAX_PUPIL_RADIUS_UV
# define PIXL_EYE_MAX_PUPIL_RADIUS_UV 0.145f
#endif
#ifndef PIXL_EYE_LIMBUS_BLEND_START
# define PIXL_EYE_LIMBUS_BLEND_START 0.90f
#endif

// Skyrim lighting is renderer-relative rather than calibrated lux; use a
// logarithmic response robust to its wide relative intensity range.
#ifndef PIXL_EYE_DIRECTIONAL_WEIGHT
# define PIXL_EYE_DIRECTIONAL_WEIGHT 0.70f
#endif
#ifndef PIXL_EYE_AMBIENT_WEIGHT
# define PIXL_EYE_AMBIENT_WEIGHT 1.00f
#endif
#ifndef PIXL_EYE_LOCAL_LIGHT_WEIGHT
# define PIXL_EYE_LOCAL_LIGHT_WEIGHT 1.15f
#endif
#ifndef PIXL_EYE_LIGHT_RESPONSE_SCALE
# define PIXL_EYE_LIGHT_RESPONSE_SCALE 4.0f
#endif
#ifndef PIXL_EYE_LOG_DARK
# define PIXL_EYE_LOG_DARK 0.20f
#endif
#ifndef PIXL_EYE_LOG_BRIGHT
# define PIXL_EYE_LOG_BRIGHT 3.50f
#endif
#ifndef PIXL_EYE_DILATION_CURVE
# define PIXL_EYE_DILATION_CURVE 0.70f
#endif
#ifndef PIXL_EYE_INTERIOR_DARK_BIAS
# define PIXL_EYE_INTERIOR_DARK_BIAS 0.08f
#endif

// Diagnostic override: set to 1, then value 0..1, to test UV geometry only.
#ifndef PIXL_EYE_DEBUG_FORCE_PUPIL
# define PIXL_EYE_DEBUG_FORCE_PUPIL 0
#endif
#ifndef PIXL_EYE_DEBUG_PUPIL_VALUE
# define PIXL_EYE_DEBUG_PUPIL_VALUE 1.0f
#endif

namespace EyeRendering
{
    static const float3 kLuminance = float3(0.2126f, 0.7152f, 0.0722f);

    float Luminance(float3 value)
    {
        return dot(max(value, 0.0f.xxx), kLuminance);
    }

    float CorneaF0()
    {
        const float n = PIXL_EYE_CORNEA_IOR;
        const float r = (n - 1.0f) / (n + 1.0f);
        return r * r;
    }

    // Smooth compact-support attenuation using Skyrim's bound light radius.
    float PointLightStimulus(float3 worldPosition, float4 lightPositionRadius, float3 lightColor)
    {
        const float radius = max(lightPositionRadius.w, 1e-3f);
        const float distanceToLight = length(lightPositionRadius.xyz - worldPosition);
        float x = saturate(1.0f - distanceToLight / radius);
        x = x * x * (3.0f - 2.0f * x);
        x *= x;
        return Luminance(lightColor) * x * PIXL_EYE_LOCAL_LIGHT_WEIGHT;
    }

    // 0 = maximally constricted, 1 = maximally dilated.
    float PupilDilationFromLight(float relativeLight, bool inInterior)
    {
        const float logLight = log2(1.0f + max(relativeLight, 0.0f) * PIXL_EYE_LIGHT_RESPONSE_SCALE);
        const float bright01 = saturate(
            (logLight - PIXL_EYE_LOG_DARK) /
            max(PIXL_EYE_LOG_BRIGHT - PIXL_EYE_LOG_DARK, 1e-4f));

        float dilation = pow(saturate(1.0f - bright01), PIXL_EYE_DILATION_CURVE);
        if (inInterior)
            dilation = saturate(dilation + PIXL_EYE_INTERIOR_DARK_BIAS * (1.0f - bright01));

#if PIXL_EYE_DEBUG_FORCE_PUPIL
        dilation = saturate(PIXL_EYE_DEBUG_PUPIL_VALUE);
#endif
        return dilation;
    }

    // Continuous radial remap. Only albedo UVs use this; outer corneal normal
    // and reflection-mask UVs remain untouched.
    float2 WarpPupilUV(float2 uv, float dilation)
    {
        const float2 centre = PIXL_EYE_IRIS_CENTER;
        const float irisRadius = max(PIXL_EYE_IRIS_RADIUS_UV, 1e-4f);
        const float sourcePupilRadius =
            clamp(PIXL_EYE_AUTHORED_PUPIL_RADIUS_UV, 1e-4f, irisRadius - 1e-4f);
        const float targetPupilRadius =
            clamp(lerp(PIXL_EYE_MIN_PUPIL_RADIUS_UV,
                       PIXL_EYE_MAX_PUPIL_RADIUS_UV,
                       saturate(dilation)),
                  1e-4f, irisRadius - 1e-4f);

        const float2 delta = uv - centre;
        const float radius = length(delta);
        if (radius >= irisRadius || radius <= 1e-7f)
            return uv;

        float sourceRadius;
        if (radius <= targetPupilRadius)
        {
            sourceRadius = radius * (sourcePupilRadius / targetPupilRadius);
        }
        else
        {
            sourceRadius = sourcePupilRadius +
                (radius - targetPupilRadius) *
                ((irisRadius - sourcePupilRadius) /
                 max(irisRadius - targetPupilRadius, 1e-4f));
        }

        const float2 direction = delta / max(radius, 1e-7f);
        const float2 warped = centre + direction * sourceRadius;
        const float limbusStart = irisRadius * saturate(PIXL_EYE_LIMBUS_BLEND_START);
        const float preserveOriginal = smoothstep(limbusStart, irisRadius, radius);
        return lerp(warped, uv, preserveOriginal);
    }

#if defined(PSHADER)
    float CloseUpFactor(float2 uv)
    {
        const float footprint = max(length(ddx(uv)), length(ddy(uv)));
        return 1.0f - saturate(
            (footprint - PIXL_EYE_CLOSEUP_FOOTPRINT_MIN) /
            max(PIXL_EYE_CLOSEUP_FOOTPRINT_MAX - PIXL_EYE_CLOSEUP_FOOTPRINT_MIN, 1e-5f));
    }

    float CorneaRoughness(float2 uv)
    {
        const float closeUp = CloseUpFactor(uv);
        return lerp(PIXL_EYE_CORNEA_ROUGHNESS_FAR,
                    PIXL_EYE_CORNEA_ROUGHNESS_CLOSE,
                    closeUp);
    }
#endif
}

#endif // PIXL_EYE_RENDERING_HLSLI
