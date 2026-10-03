// PIXL Renderer
// Hybrid GI internal temporal-clock policy.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TemporalPolicy.h"

#include <algorithm>
#include <cmath>

namespace PIXL::HybridGIInternal
{
	float SanitizeDeltaTime(float a_deltaSeconds)
	{
		return std::isfinite(a_deltaSeconds) ? std::clamp(a_deltaSeconds, 0.0f, 0.25f) : 0.0f;
	}

	std::uint32_t AdvanceFixedRateClock(
		std::uint32_t a_clock,
		float& a_accumulator,
		float a_deltaSeconds,
		float a_ticksPerSecond)
	{
		const float ticksPerSecond = std::isfinite(a_ticksPerSecond) ? std::max(a_ticksPerSecond, 0.0f) : 0.0f;
		a_accumulator = std::max(a_accumulator, 0.0f) + SanitizeDeltaTime(a_deltaSeconds) * ticksPerSecond;
		const auto elapsedTicks = static_cast<std::uint32_t>(a_accumulator);
		a_accumulator -= static_cast<float>(elapsedTicks);
		return a_clock + elapsedTicks;
	}
}
