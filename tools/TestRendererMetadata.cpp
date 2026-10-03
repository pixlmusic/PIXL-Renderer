#ifdef NDEBUG
#undef NDEBUG
#endif

#include "Renderer/RendererMetadata.h"

#include <cassert>
#include <cmath>
#include <set>
#include <string_view>

int main()
{
	using namespace PIXL::Metadata;
	std::set<std::string_view> settingIds;
	for (const auto& setting : Settings::All) {
		assert(!setting.id.empty() && !setting.module.empty() && !setting.label.empty());
		assert(std::isfinite(setting.defaultValue) && std::isfinite(setting.minimum) && std::isfinite(setting.maximum));
		assert(setting.minimum <= setting.defaultValue && setting.defaultValue <= setting.maximum);
		assert(settingIds.insert(setting.id).second);
	}

	std::set<std::pair<std::string_view, std::string_view>> buffers;
	for (const auto& abi : ABI::All) {
		assert(!abi.module.empty() && !abi.buffer.empty() && !abi.version.empty());
		assert(abi.registerSlot < 14u);
		assert(abi.sizeBytes > 0u && abi.sizeBytes % 16u == 0u);
		assert(abi.alignment == 16u);
		assert(buffers.emplace(abi.module, abi.buffer).second);
	}
	assert(ShaderABIHash() != 0u);
	assert(Settings::CameraDofFStop.defaultValue == 3.4);
	return 0;
}
