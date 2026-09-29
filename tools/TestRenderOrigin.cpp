#include "../engine/Renderer/RenderOrigin.h"
#include <cassert>
#include <iostream>

using namespace PIXL::RenderOrigin;
static void Near(Position a, Position b, double epsilon = 1e-6)
{ if ((a-b).MaxAbs() > epsilon) { std::cerr << "coordinate mismatch\n"; std::abort(); } }
static Position XYZ(const std::array<float,4>& p) { return {p[0],p[1],p[2]}; }
int main()
{
    Manager m;
    m.Update(0, {1000000.25, -1000000.5, 30}, 1);
    assert(!m.Enabled() && !m.HistoryValid());
    Near(m.WorldToRender({2,3,4}), {2,3,4});
    m.requestedEnabled = true;
    m.Update(1, {1000000.25, -1000000.5, 30}, 1);
    assert(m.Enabled() && !m.HistoryValid());
    const Position p{1000031.125, -999930.25, 55.5};
    Near(m.RenderToWorld(m.WorldToRender(p)), p);
    auto oldOrigin = m.GetCurrentOrigin();
    m.Update(2, {oldOrigin.x+2049, oldOrigin.y, oldOrigin.z}, 1);
    assert(!m.ShiftedThisFrame()); // no chatter at the nearest-cell boundary
    const auto oldRelative = m.WorldToRender(p);
    m.Update(3, {oldOrigin.x+3073, oldOrigin.y, oldOrigin.z}, 1);
    assert(m.ShiftedThisFrame() && m.HistoryValid());
    Near(m.CurrentToPrevious(m.WorldToRender(p)), oldRelative);
    auto epoch = m.GetOriginEpoch();
    assert(!m.Update(3, {0,0,0}, 999)); // same-frame readers cannot change snapshot
    assert(m.GetOriginEpoch() == epoch);
    m.smallGrid = true;
    m.Update(4, {1000000, -1000000, 0}, 1);
    assert(!m.HistoryValid());
    Position engine{1000000, -1000000, 0};
    const Position point{1000000.125, -999999.75, 8.5};
    for (std::uint64_t f=5; f<10005; ++f) {
        Position previousEngine = engine;
        engine.x += 0.25;
        engine.y -= 0.125;
        m.forceShift = (f % 7 == 0);
        const Position previousOrigin = m.GetCurrentOrigin();
        m.Update(f, engine, 1);
        auto gpu = m.GetGPUData(engine, previousEngine);
        Position native = point-engine;
        Position render = native+XYZ(gpu.engineToRender);
        Near(render, point-m.GetCurrentOrigin(), 0.001);
        Position previous = render+XYZ(gpu.delta)-XYZ(gpu.previousEngineToRender);
        Near(previous, point-previousEngine, 0.001);
        // Zero apparent origin motion for a static receiver; the native camera
        // displacement remains physical, independent of snapped-origin movement.
        Near(native+XYZ(gpu.engineDelta), point-previousEngine, 0.001);
        Near(m.CurrentToPrevious(m.WorldToRender(point)), point-previousOrigin);
        Near(m.RenderToWorld(m.WorldToRender(point)), point);
        assert(gpu.flags[3] == 1);
    }
    m.Update(10005, engine, 2);
    assert(m.Discontinuity() && !m.HistoryValid());
    m.Update(10006, engine+Position{100000,0,0}, 2);
    assert(m.Discontinuity());
    m.Update(10007, {std::numeric_limits<double>::quiet_NaN(),0,0}, 2);
    assert(!m.Enabled());
    m.Update(10008, {-8193,-4097,-256}, 2);
    assert(m.Enabled());
    m.requestedEnabled = false;
    m.Update(10009, engine, 2);
    Near(m.GetCurrentOrigin(), {});
    assert(!m.Enabled());
    static_assert(sizeof(GPUData)==144 && offsetof(GPUData,engineDelta)==112);
    std::cout << "PASS: origin lifecycle, hysteresis, 10000 temporal shifts, fallback, invalid input, world transition and ABI\n";
}
