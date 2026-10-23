#pragma once
#include <initializer_list>
#include <string_view>

namespace PhysicalMaterial
{
	// Allocation-free, case-insensitive veto. Check the draw's own name and
	// texture path, never an ancestor actor name that may describe other meshes.
	inline bool IsNonFurAnatomy(std::string_view name)
	{
		for (const std::string_view token : { "eye", "cornea", "iris", "pupil", "teeth", "tooth", "mouth", "oral", "gum", "tongue", "fang", "tusk", "claw", "talon", "hoof", "beak", "horn", "antler" }) {
			for (std::size_t i = 0; i + token.size() <= name.size(); ++i) {
				bool match = true;
				for (std::size_t j = 0; j < token.size(); ++j) {
					char c = name[i + j];
					if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
					if (c != token[j]) { match = false; break; }
				}
				if (match) return true;
			}
		}
		return false;
	}
}
