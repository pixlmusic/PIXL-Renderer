#pragma once

#include <algorithm>
#include <cmath>

namespace PIXL::AtmosphereWeather
{
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
