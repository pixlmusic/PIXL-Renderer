#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ReactiveFXSafety
{
	// SpawnCS writes slots in parallel. Keep only the last authored command for
	// each slot so ring wrap cannot produce unordered writes to one particle.
	template <std::size_t Capacity, class Command>
	void CompactSpawnBatch(std::vector<Command>& commands)
	{
		std::array<std::int32_t, Capacity> indices;
		indices.fill(-1);
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
