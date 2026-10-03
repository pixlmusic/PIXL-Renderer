// PIXL Renderer
// Shader Cache internal descriptor/path utilities.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../ShaderCache.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace PIXL::ShaderCacheInternal
{
	[[nodiscard]] std::wstring GetShaderPath(std::string_view a_name);
	[[nodiscard]] const char* GetShaderProfile(SIE::ShaderClass a_shaderClass);
	[[nodiscard]] std::uint32_t GetTechnique(std::uint32_t a_descriptor);
}
