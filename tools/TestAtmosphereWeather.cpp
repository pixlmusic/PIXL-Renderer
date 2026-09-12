#include "Modules/AtmosphereWeather.h"
#include <cassert>
#include <limits>
#include <cstdio>

int main()
{
    using namespace PIXL::AtmosphereWeather;
    const auto clear = ForWeather(false, false, false);
    const auto rain = ForWeather(true, false, false);
    const auto snow = ForWeather(false, true, false);
    const auto cloudy = ForWeather(false, false, true);
    for (float distance : { 8000.0f, 60000.0f, 200000.0f }) {
        assert(DensityScale(distance, clear) < DensityScale(distance, cloudy));
        assert(DensityScale(distance, cloudy) < DensityScale(distance, rain));
        assert(DensityScale(distance, rain) < DensityScale(distance, snow));
        float last = DensityScale(distance, clear);
        for (int step = 0; step <= 100; ++step) {
            const auto blended = Blend(clear, snow, step / 100.0f);
            const float value = DensityScale(distance, blended);
            assert(std::isfinite(value) && value >= last && value <= 3.06f);
            assert(blended.startDistance >= snow.startDistance && blended.startDistance <= 1.0f);
            last = value;
        }
    }
    assert(Blend(clear, rain, 0).density == clear.density);
    assert(Blend(clear, rain, 1).density == rain.density);
    assert(std::isfinite(DensityScale(std::numeric_limits<float>::quiet_NaN(), rain)));
    assert(std::isfinite(Blend(clear, snow, std::numeric_limits<float>::quiet_NaN()).density));
    assert(DensityScale(-1, snow) <= 3.06f);
    std::puts("PASS weather density ordering, smooth transitions, range and nonfinite input guards");
}
