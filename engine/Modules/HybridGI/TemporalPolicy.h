// PIXL Renderer
// Hybrid GI internal temporal-clock policy.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

namespace PIXL::HybridGIInternal
{
	[[nodiscard]] float SanitizeDeltaTime(float a_deltaSeconds);
	std::uint32_t AdvanceFixedRateClock(
		std::uint32_t a_clock,
		float& a_accumulator,
		float a_deltaSeconds,
		float a_ticksPerSecond);
}
