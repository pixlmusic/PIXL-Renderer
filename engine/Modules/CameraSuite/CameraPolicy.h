// PIXL Renderer
// Camera Suite internal physical-camera and output-state policy.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace PIXL::CameraSuiteInternal
{
	[[nodiscard]] float SanitizeFrameDelta(float a_deltaSeconds);
	[[nodiscard]] float ExponentialResponse(float a_deltaSeconds, float a_rate);
	[[nodiscard]] float ResolveMenuSceneEncoding(
		bool a_mainOrLoading,
		bool a_directorPhotoMode,
		bool a_pauseOrMap,
		float a_gameplay,
		float a_pauseMap,
		float a_photo,
		float a_mainLoading);
}
