// PIXL Renderer
// Ground Response internal surface-classification utilities.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <string_view>

namespace PIXL::GroundResponseInternal
{
	[[nodiscard]] std::string NormalizeTextureKey(std::string_view a_path);
	[[nodiscard]] bool HasSnowTextureHint(std::string_view a_text);
}
