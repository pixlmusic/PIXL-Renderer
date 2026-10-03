#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../engine/Modules/CameraSuite/CameraPolicy.h"
#include "../engine/Modules/GroundResponse/SurfaceClassifier.h"
#include "../engine/Modules/HybridGI/TemporalPolicy.h"

#include <cassert>
#include <cmath>
#include <limits>

int main()
{
	using namespace PIXL;
	assert(GroundResponseInternal::NormalizeTextureKey("Data/Textures/Landscape/SNOW01.DDS") ==
	       "landscape\\snow01");
	assert(GroundResponseInternal::HasSnowTextureHint("Landscape\\GlacierRock.dds"));
	assert(!GroundResponseInternal::HasSnowTextureHint("Landscape\\PineForest.dds"));

	assert(std::abs(CameraSuiteInternal::SanitizeFrameDelta(1.0f) - 0.1f) < 1.0e-6f);
	assert(std::abs(CameraSuiteInternal::SanitizeFrameDelta(std::numeric_limits<float>::quiet_NaN()) -
	                1.0f / 60.0f) < 1.0e-6f);
	assert(CameraSuiteInternal::ResolveMenuSceneEncoding(false, true, true, 0.0f, 1.0f, 2.0f, 3.0f) == 2.0f);
	assert(CameraSuiteInternal::ResolveMenuSceneEncoding(true, true, true, 0.0f, 1.0f, 2.0f, 3.0f) == 3.0f);
	assert(CameraSuiteInternal::ExponentialResponse(0.0f, 4.0f) == 0.0f);

	float accumulator = 0.0f;
	std::uint32_t clock = 10u;
	for (int i = 0; i < 60; ++i)
		clock = HybridGIInternal::AdvanceFixedRateClock(clock, accumulator, 1.0f / 60.0f, 8.0f);
	assert(clock == 18u);
	assert(accumulator >= 0.0f && accumulator < 1.0f);
	assert(HybridGIInternal::SanitizeDeltaTime(2.0f) == 0.25f);
	return 0;
}
