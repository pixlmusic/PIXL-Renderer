#include "Modules/ReactiveFX/SpawnBatch.h"
#include <cstdint>
#include <vector>

struct Command { std::uint32_t slot; std::uint32_t value; };
int main()
{
	using ReactiveFXSafety::AllocateQuota;
	if (AllocateQuota(160000u, 160000u, 160000u) != 160000u) return 10;
	if (AllocateQuota(320u, 960u, 960u) != 320u) return 11;
	if (AllocateQuota(160000u, 262144u, 320000u) != 131072u) return 12;
	if (AllocateQuota(160000u, 0u, 160000u) != 0u) return 13;
	if (AllocateQuota(0u, 100u, 0u) != 0u) return 14;
	for (std::uint32_t budget : {1u, 17u, 1024u, 524288u}) {
		std::uint32_t remaining = budget;
		std::uint32_t total = 64u * 160000u;
		for (unsigned i = 0; i < 64; ++i) {
			const auto quota = AllocateQuota(160000u, remaining, total);
			if (quota > remaining || quota > 160000u) return 15;
			remaining -= quota;
			total -= 160000u;
		}
		if (remaining != 0u) return 16;
	}
	std::vector<Command> commands;
	ReactiveFXSafety::CompactSpawnBatch<4>(commands);
	if (!commands.empty()) return 1;
	commands = { {0, 1}, {1, 2}, {0, 3}, {4, 99}, {3, 4} };
	ReactiveFXSafety::CompactSpawnBatch<4>(commands);
	if (commands.size() != 3) return 2;
	if (commands[0].slot != 0 || commands[0].value != 3) return 3;
	if (commands[1].slot != 1 || commands[1].value != 2) return 4;
	if (commands[2].slot != 3 || commands[2].value != 4) return 5;
	commands.clear();
	for (std::uint32_t i = 0; i < 2048; ++i)
		commands.push_back({i % 4, i});
	ReactiveFXSafety::CompactSpawnBatch<4>(commands);
	if (commands.size() != 4) return 6;
	for (std::uint32_t i = 0; i < 4; ++i)
		if (commands[i].slot != i || commands[i].value != 2044 + i) return 7;
}
