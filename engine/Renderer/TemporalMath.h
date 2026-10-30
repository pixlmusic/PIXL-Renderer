// PIXL Renderer - portable temporal continuity math.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace PIXL::Renderer
{
	// A single-frame rotation above 45 degrees is treated as a cut. Smaller
	// continuous pans stay on per-pixel reprojection and do not clear all history.
	inline bool IsAbruptCameraRotation(
		const std::array<float, 16>& currentView,
		const std::array<float, 16>& previousView) noexcept
	{
		double orientationTrace = 0.0;
		for (std::size_t row = 0; row < 3; ++row) {
			double currentLengthSquared = 0.0;
			double previousLengthSquared = 0.0;
			double rowDot = 0.0;
			for (std::size_t column = 0; column < 3; ++column) {
				const double current = currentView[row * 4 + column];
				const double previous = previousView[row * 4 + column];
				if (!std::isfinite(current) || !std::isfinite(previous))
					return true;
				currentLengthSquared += current * current;
				previousLengthSquared += previous * previous;
				rowDot += current * previous;
			}
			// View matrices should contain orthonormal camera axes. Invalid or
			// partially initialized matrices must not keep stale temporal history.
			if (currentLengthSquared < 0.5 || currentLengthSquared > 1.5 ||
				previousLengthSquared < 0.5 || previousLengthSquared > 1.5)
				return true;
			orientationTrace += rowDot / std::sqrt(currentLengthSquared * previousLengthSquared);
		}

		const double cosineAngle = std::clamp((orientationTrace - 1.0) * 0.5, -1.0, 1.0);
		return !std::isfinite(cosineAngle) || cosineAngle < 0.7071067811865476;
	}
}
