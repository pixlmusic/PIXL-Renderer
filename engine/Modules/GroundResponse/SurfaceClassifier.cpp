// PIXL Renderer
// Ground Response internal surface-classification utilities.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SurfaceClassifier.h"

#include <algorithm>
#include <cctype>

namespace PIXL::GroundResponseInternal
{
	std::string NormalizeTextureKey(std::string_view a_path)
	{
		std::string key;
		key.reserve(a_path.size());
		for (char c : a_path) {
			char out = c == '/' ? '\\' : c;
			if (out >= 'A' && out <= 'Z')
				out = static_cast<char>(out - 'A' + 'a');
			key.push_back(out);
		}

		const auto stripPrefix = [&key](std::string_view a_prefix) {
			if (key.starts_with(a_prefix))
				key.erase(0, a_prefix.size());
		};
		stripPrefix("data\\");
		stripPrefix("textures\\");
		if (key.ends_with(".dds"))
			key.resize(key.size() - 4u);
		return key;
	}

	bool HasSnowTextureHint(std::string_view a_text)
	{
		std::string normalized(a_text);
		std::ranges::transform(normalized, normalized.begin(), [](unsigned char a_character) {
			return static_cast<char>(std::tolower(a_character));
		});
		return normalized.find("snow") != std::string::npos ||
		       normalized.find("snw") != std::string::npos ||
		       normalized.find("glacier") != std::string::npos;
	}
}
