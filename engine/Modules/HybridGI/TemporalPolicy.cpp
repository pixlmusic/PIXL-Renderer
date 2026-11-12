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
		// External frame deltas and reloaded state must never turn a float-to-uint
		// conversion into undefined behaviour. The production clock uses 8 Hz;
		// clamp corrupted rate inputs without changing that normal path.
		const float ticksPerSecond = std::isfinite(a_ticksPerSecond) ? std::clamp(a_ticksPerSecond, 0.0f, 1000000.0f) : 0.0f;
		const float safeAccum = std::isfinite(a_accumulator) ? std::clamp(a_accumulator, 0.0f, 1000000.0f) : 0.0f;
		const double accumulated = static_cast<double>(safeAccum) +
			static_cast<double>(SanitizeDeltaTime(a_deltaSeconds)) * ticksPerSecond;
		const auto elapsedTicks = static_cast<std::uint32_t>(accumulated);
		a_accumulator = static_cast<float>(accumulated - elapsedTicks);
		return a_clock + elapsedTicks;
	}
}
