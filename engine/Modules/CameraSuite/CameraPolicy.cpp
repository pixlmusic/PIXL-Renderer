// PIXL Renderer
// Camera Suite internal physical-camera and output-state policy.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "CameraPolicy.h"

#include <algorithm>
#include <cmath>

namespace PIXL::CameraSuiteInternal
{
	float SanitizeFrameDelta(float a_deltaSeconds)
	{
		return std::isfinite(a_deltaSeconds) ? std::clamp(a_deltaSeconds, 1.0f / 240.0f, 0.1f) : 1.0f / 60.0f;
	}

	float ExponentialResponse(float a_deltaSeconds, float a_rate)
	{
		const float delta = std::isfinite(a_deltaSeconds) ? std::max(a_deltaSeconds, 0.0f) : 0.0f;
		const float rate = std::isfinite(a_rate) ? std::max(a_rate, 0.0f) : 0.0f;
		return 1.0f - std::exp(-delta * rate);
	}

	float ResolveMenuSceneEncoding(
		bool a_mainOrLoading,
		bool a_directorPhotoMode,
		bool a_pauseOrMap,
		float a_gameplay,
		float a_pauseMap,
		float a_photo,
		float a_mainLoading)
	{
		if (a_mainOrLoading)
			return a_mainLoading;
		if (a_directorPhotoMode)
			return a_photo;
		if (a_pauseOrMap)
			return a_pauseMap;
		return a_gameplay;
	}
}
