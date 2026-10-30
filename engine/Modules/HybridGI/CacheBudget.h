#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace PIXL::HybridGIInternal
{
    inline std::uint32_t CacheMilliseconds(std::uint32_t clock, float fractionalTick) noexcept
    {
        const float fraction = std::isfinite(fractionalTick) ? std::clamp(fractionalTick, 0.0f, 1.0f) : 0.0f;
        return clock * 125u + static_cast<std::uint32_t>(fraction * 125.0f);
    }
    // Injection has its own measured domain. Invalid/dormant profiler data
    // restores the established full-visible 30 Hz path immediately.
    class CacheBudget
    {
    public:
        float Update(float measuredMs, bool valid) noexcept
        {
            if (!valid || !std::isfinite(measuredMs) || measuredMs <= 0.0f) {
                fraction = 1.0f;
                return fraction;
            }
            constexpr float targetMs = 0.15f;
            if (measuredMs > targetMs * 1.15f)
                fraction = std::max(0.25f, fraction - 0.0625f);
            else if (measuredMs < targetMs * 0.85f)
                fraction = std::min(1.0f, fraction + 0.0625f);
            return fraction;
        }
        void Reset() noexcept { fraction = 1.0f; }
    private:
        float fraction{1.0f};
    };
}
