#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <stdexcept>

namespace MaterialSurface
{
	// Explicit authoring convention. Never infer channel meaning from pixel colour.
	struct Profile
	{
		std::string packedMapLayout = "RMAOS";
		float wetResponse = 1.f;
		float porosity = -1.f;  // Negative selects the material-derived response.
		bool receiveSnow = true;
	};

	inline std::uint32_t LayoutIndex(const std::string& name)
	{
		if (name == "RMAOS") return 0;
		if (name == "ORM") return 1;
		if (name == "MRA") return 2;
		if (name == "RMA") return 3;
		if (name == "MaskMap") return 4;
		throw std::invalid_argument("Unknown Material Forge packedMapLayout: " + name);
	}

	// Matches Common/MaterialSurface.hlsli. Object: PBRFlags[31:16].
	// Landscape: two 16-bit profiles per word of the existing PBRParams2.xyz.
	inline std::uint32_t Pack(const Profile& profile, bool hasMap)
	{
		const float wet = std::isfinite(profile.wetResponse) ? std::clamp(profile.wetResponse, 0.f, 2.f) : 1.f;
		const auto wetCode = static_cast<std::uint32_t>(std::round(wet * 15.f));
		const auto poreCode = !std::isfinite(profile.porosity) || profile.porosity < 0.f ? 31u :
			static_cast<std::uint32_t>(std::round(std::clamp(profile.porosity, 0.f, 1.f) * 30.f));
		return LayoutIndex(profile.packedMapLayout) | (hasMap ? 0u : 8u) |
			(wetCode << 4) | (poreCode << 9) | (1u << 14) | (profile.receiveSnow ? 0u : (1u << 15));
	}
}
