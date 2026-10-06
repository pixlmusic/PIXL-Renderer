#include "Modules/ReactiveFX/SpawnBatch.h"
#include <cstdint>
#include <vector>

struct Command { std::uint32_t slot; std::uint32_t value; };
int main()
{
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
