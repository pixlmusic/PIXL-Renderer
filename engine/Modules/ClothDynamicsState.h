#pragma once

#include "Buffer.h"

#include <cstdint>

/** Same-frame actor damage state appended to the established character b13 ABI. */
struct ClothingDamageState
{
	float4 Damage{};     // severity, health fraction, reserved, enabled
	float4 Parameters{}; // fabric tears, armor damage, actor seed, debug mode
	std::uint32_t frame = 0;
	std::uint32_t actorFormID = 0;
};
