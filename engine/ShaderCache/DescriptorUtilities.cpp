// PIXL Renderer
// Shader Cache internal descriptor/path utilities.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DescriptorUtilities.h"

#include <format>

namespace PIXL::ShaderCacheInternal
{
	std::wstring GetShaderPath(std::string_view a_name)
	{
		return std::format(L"Data/Shaders/{}.hlsl", std::wstring(a_name.begin(), a_name.end()));
	}

	const char* GetShaderProfile(SIE::ShaderClass a_shaderClass)
	{
		switch (a_shaderClass) {
		case SIE::ShaderClass::Vertex:
			return "vs_5_0";
		case SIE::ShaderClass::Pixel:
			return "ps_5_0";
		case SIE::ShaderClass::Compute:
			return "cs_5_0";
		}
		return nullptr;
	}

	std::uint32_t GetTechnique(std::uint32_t a_descriptor)
	{
		return 0x3Fu & (a_descriptor >> 24u);
	}
}
