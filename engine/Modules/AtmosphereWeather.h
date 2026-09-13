#pragma once

#include <algorithm>
#include <cmath>

namespace PIXL::AtmosphereWeather
{
    // Climate-defined twilight, not fixed clock hours. Only the directional fog
    // source is attenuated: moon surface lighting and fog extinction stay intact.
    inline float DirectionalFogScale(float hour, float dawnBegin, float dawnEnd, float duskBegin, float duskEnd)
    {
        if (!std::isfinite(hour) || !std::isfinite(dawnBegin) || !std::isfinite(dawnEnd) ||
            !std::isfinite(duskBegin) || !std::isfinite(duskEnd)) return 1.0f;
        auto wrap = [](float value) { return value - 24.0f * std::floor(value / 24.0f); };
        const float dawn = wrap(dawnEnd - dawnBegin);
        const float dusk = wrap(duskBegin - dawnBegin);
        const float night = wrap(duskEnd - dawnBegin);
        if (!(dawn > 0.0f && dawn < dusk && dusk < night)) return 1.0f;
        const float t = wrap(hour - dawnBegin);
        auto smooth = [](float x) { x = std::clamp(x, 0.0f, 1.0f); return x * x * (3.0f - 2.0f * x); };
        const float daylight = t < dawn ? smooth(t / dawn) :
            t < dusk ? 1.0f : t < night ? 1.0f - smooth((t - dusk) / (night - dusk)) : 0.0f;
        return 0.05f + 0.95f * daylight;
    }

    struct Profile
    {
        float density;
        float startDistance;
    };

    constexpr Profile ForWeather(bool rain, bool snow, bool cloudy)
    {
        if (snow) return { 1.65f, 0.65f };
        if (rain) return { 1.45f, 0.78f };
        if (cloudy) return { 1.10f, 0.92f };
        return { 1.0f, 1.0f };
    }

    inline Profile Blend(Profile previous, Profile current, float transition)
    {
        const float t = std::isfinite(transition) ? std::clamp(transition, 0.0f, 1.0f) : 1.0f;
        return { previous.density + (current.density - previous.density) * t,
                 previous.startDistance + (current.startDistance - previous.startDistance) * t };
    }

    inline float DensityScale(float fogFar, Profile weather)
    {
        const float distance = std::isfinite(fogFar) ? std::clamp(fogFar, 8000.0f, 200000.0f) : 60000.0f;
        const float visibilityScale = std::clamp(std::sqrt(60000.0f / distance), 0.55f, 1.85f);
        return std::clamp(visibilityScale * weather.density, 0.55f, 3.06f);
    }
}
