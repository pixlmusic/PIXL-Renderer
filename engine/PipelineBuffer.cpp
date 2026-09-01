#include "PipelineBuffer.h"

#include <array>
#include <cstring>
#include <type_traits>

#include "Modules/SkyVeil.h"
#include "Modules/WorldProbes.h"
#include "Modules/Atmosphere.h"
#include "Modules/MaterialLayers.h"
#include "Modules/ThinSurface.h"
#include "Modules/FoliageDynamics.h"
#include "Modules/GroundResponse.h"
#include "Modules/StrandShading.h"
#include "Modules/HairReconstruction.h"
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
std::pair<unsigned char*, size_t> _GetPipelineBufferData(const Ts&... feat_datas)
{
	// The packed size is a compile-time constant, so reuse one aligned, thread-local buffer
	// instead of allocating/freeing every UpdateSharedData call. The returned pointer is
	// non-owning and must NOT be deleted by the caller.
	static_assert((std::is_trivially_copyable_v<Ts> && ...),
		"FeatureData blocks must remain trivially copyable across the CPU/GPU ABI");
	constexpr size_t totalSize = (... + sizeof(Ts));
	alignas(16) static thread_local std::array<unsigned char, totalSize> storage;
	size_t offset = 0;

	([&] {
		std::memcpy(storage.data() + offset, &feat_datas, sizeof(Ts));
		offset += sizeof(Ts);
	}(),
		...);

	return std::make_pair(storage.data(), storage.size());
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
		// Hair Reconstruction occupies the historical eight-register post-process
		// reservation. Its 128-byte contract keeps all later offsets byte-identical.
		globals::pipeline::hairReconstruction.GetCommonBufferData(),
		globals::pipeline::terrainSeam.settings,
		globals::pipeline::atmosphere.GetCommonBufferData(),
		globals::pipeline::materialForge.settings,
		globals::pipeline::skinOptics.GetCommonBufferData());
}
