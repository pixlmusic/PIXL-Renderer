#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ReactiveFXSafety
{
	// Proportional allocation must include this event's demand. Use 64-bit
	// arithmetic because pressure bursts exceed a 32-bit product.
	inline std::uint32_t AllocateQuota(std::uint32_t requested, std::uint32_t budget, std::uint32_t total)
	{
		if (total == 0u || requested == 0u || budget == 0u)
			return 0u;
		const auto share = (static_cast<std::uint64_t>(requested) * budget + total - 1u) / total;
		return static_cast<std::uint32_t>(std::min<std::uint64_t>(std::min(requested, budget), share));
	}

	// SpawnCS writes slots in parallel. Keep only the last authored command for
	// each slot so ring wrap cannot produce unordered writes to one particle.
	template <std::size_t Capacity, class Command>
	void CompactSpawnBatch(std::vector<Command>& commands)
	{
		// Capacity can grow for explicit GPU pressure tests. Keep the slot map on
		// the heap so large pools do not overflow the game's limited thread stack.
		std::vector<std::int32_t> indices(Capacity, -1);
		std::size_t output = 0;
		for (std::size_t input = 0; input < commands.size(); ++input) {
			const auto command = commands[input];
			if (command.slot >= Capacity)
				continue;
			auto& index = indices[command.slot];
			if (index < 0)
				index = static_cast<std::int32_t>(output++);
			commands[static_cast<std::size_t>(index)] = command;
		}
		commands.resize(output);
	}
}
