// Standalone WARP regression test for the production timestamp ring.
#define NOMINMAX
#include <Windows.h>
#include <cassert>
#include <cmath>
#include <iostream>
#include <random>
namespace logger {
template<class... T> void warn(const char*, T&&...) {}
template<class... T> void info(const char*, T&&...) {}
}
#include "../engine/Profiler.cpp"

int main()
{
    std::mt19937 random(42);
    Profiler::RollingHistory history;
    for (unsigned count = 1; count <= Profiler::kHistorySize; ++count) {
        history.PushSample(static_cast<float>(random() % 10000) / 100.0f);
        std::vector<float> reference(history.history, history.history + history.count);
        std::sort(reference.begin(), reference.end());
        for (float p : {0.0f, 50.0f, 95.0f, 99.0f, 100.0f}) {
            float position = (p / 100.0f) * (count - 1);
            unsigned lo = static_cast<unsigned>(position);
            unsigned hi = std::min(lo + 1, count - 1);
            float fraction = position - lo;
            float expected = reference[lo] * (1 - fraction) + reference[hi] * fraction;
            assert(history.GetPercentile(p) == expected);
        }
    }
    winrt::com_ptr<ID3D11Device> device;
    winrt::com_ptr<ID3D11DeviceContext> context;
    const auto hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, device.put(), nullptr, context.put());
    if (FAILED(hr)) return 2;
    Profiler profiler;
    profiler.Initialize(device.get(), context.get());
    bool sawFirst = false;
    bool sawInactive = false;
    for (int frame = 0; frame < 240; ++frame) {
        profiler.BeginFrame();
        const char* name = frame < 100 ? "Test::First" : "Test::Second";
        // Repeated names must aggregate into one per-frame sample.
        for (int i = 0; i < 2; ++i) {
            profiler.BeginPass(name);
            profiler.EndPass();
        }
        profiler.EndFrame();
        context->Flush();
        Sleep(1);
        float sum = 0.0f;
        for (const auto& result : profiler.GetResults()) {
            assert(std::isfinite(result.gpuTimeMs));
            if (result.valid) sum += result.gpuTimeMs;
            if (result.name == "Test::First") {
                sawFirst |= result.valid;
                if (!result.valid) {
                    assert(result.gpuTimeMs == 0.0f && result.cpuTimeMs == 0.0f);
                    sawInactive = true;
                }
            }
        }
        assert(std::abs(sum - profiler.GetTotalTimeMs()) < 0.001f);
    }
    assert(sawFirst && sawInactive);
    profiler.Release();
    assert(profiler.GetResults().empty());
    std::cout << "Profiler WARP regression passed: coherent totals, inactive passes, repeated names, query-ring reuse.\n";
}
