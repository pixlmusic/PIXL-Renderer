#pragma once

#include <cstdint>

namespace PIXLRenderer::QualityProfiles
{
	enum class Group : std::uint8_t
	{
		Lighting,
		Materials,
		Atmosphere,
		Water,
		TerrainVegetation,
		Characters,
		Camera,
		Count
	};

	inline constexpr int Low = 0;
	inline constexpr int Medium = 1;
	inline constexpr int High = 2;
	inline constexpr int Ultra = 3;

	/** Applies one renderer-owned quality contract, synchronises its menu tier and performs required invalidation. */
	void Apply(Group group, int quality);
	/** Applies a coordinated quality level to every visual group and persists its menu state. */
	void ApplyGlobal(int quality);
	/** Returns the active unified Lighting tier, or -1 when advanced values are custom. */
	int DetectLightingTier();
}
