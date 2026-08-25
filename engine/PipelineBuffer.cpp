#include "PipelineBuffer.h"

#include <array>

#include "Modules/SkyVeil.h"
#include "Modules/WorldProbes.h"
#include "Modules/Atmosphere.h"
#include "Modules/MaterialLayers.h"
#include "Modules/ThinSurface.h"
#include "Modules/FoliageDynamics.h"
#include "Modules/GroundResponse.h"
#include "Modules/StrandShading.h"
#include "Modules/CameraSuite.h"
#include "Modules/AmbientProbe.h"
#include "Modules/DistanceBlend.h"
#include "Modules/RadiantGrid.h"
#include "Modules/LinearLightCore.h"
#include "Modules/SkinOptics.h"
#include "Modules/SkyBounce.h"
#include "Modules/TerrainSeam.h"
#include "Modules/TerrainOcclusion.h"
#include "Modules/TerrainDetail.h"
#include "Modules/WaterOptics.h"
#include "Modules/RainResponse.h"
#include "MaterialForge.h"

template <class... Ts>
std::pair<unsigned char*, size_t> _GetPipelineBufferData(Ts... feat_datas)
{
	// The packed size is a compile-time constant, so reuse one aligned, thread-local buffer
	// instead of allocating/freeing every UpdateSharedData call. The returned pointer is
	// non-owning and must NOT be deleted by the caller.
	constexpr size_t totalSize = (... + sizeof(Ts));
	alignas(16) static thread_local std::array<unsigned char, totalSize> storage;
	size_t offset = 0;

	([&] {
		*reinterpret_cast<decltype(feat_datas)*>(storage.data() + offset) = feat_datas;
		offset += sizeof(decltype(feat_datas));
	}(),
		...);

	return std::make_pair(storage.data(), storage.size());
}

namespace
{
	// Retains the established shared-buffer offsets while the reserved post-process
	// payload is inactive. All fields are zero, so compatibility reads
	// are inert and every following PIXL block remains ABI-identical.
	struct alignas(16) ReservedPostProcessData
	{
		std::array<std::uint32_t, 32> blocks{};
	};
	static_assert(sizeof(ReservedPostProcessData) == 128);
}

std::pair<unsigned char*, size_t> GetPipelineBufferData(bool a_inWorld)
{
	return _GetPipelineBufferData(
		globals::pipeline::foliageDynamics.settings,
		globals::pipeline::groundResponse.GetGroundData(),
		globals::pipeline::materialLayers.settings,
		globals::pipeline::worldProbes.settings,
		globals::pipeline::terrainOcclusion.GetCommonBufferData(),
		globals::pipeline::radiantGrid.GetCommonBufferData(),
		globals::pipeline::rainResponse.GetCommonBufferData(),
		globals::pipeline::skyBounce.GetCommonBufferData(a_inWorld),
		globals::pipeline::skyVeil.GetCommonBufferData(),
		globals::pipeline::waterOptics.settings,
		globals::pipeline::cameraSuite.GetPostProcessData(),
		globals::pipeline::distanceBlend.settings,
		globals::pipeline::strandShading.settings,
		globals::pipeline::terrainDetail.settings,
		globals::pipeline::ambientProbe.GetCommonBufferData(),
		globals::pipeline::thinSurface.GetCommonBufferData(),
		globals::pipeline::linearLightCore.GetCommonBufferData(),
		ReservedPostProcessData{},
		globals::pipeline::terrainSeam.settings,
		globals::pipeline::atmosphere.GetCommonBufferData(),
		globals::pipeline::materialForge.settings,
		globals::pipeline::skinOptics.GetCommonBufferData());
}
