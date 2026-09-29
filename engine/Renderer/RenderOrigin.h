// PIXL Renderer - stable large-world render-origin interface and ABI.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional permissions are described in the repository EXCEPTIONS.md.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace PIXL::RenderOrigin
{
    // Absolute Skyrim coordinates. This service never writes to engine transforms.
    struct Position
    {
        double x{}, y{}, z{};
        Position operator+(Position b) const { return {x + b.x, y + b.y, z + b.z}; }
        Position operator-(Position b) const { return {x - b.x, y - b.y, z - b.z}; }
        bool operator==(const Position&) const = default;
        bool Finite() const { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z); }
        double MaxAbs() const { return std::max({std::abs(x), std::abs(y), std::abs(z)}); }
    };

    // Appended to SharedData b5; no new DX11 register is allocated.
    struct alignas(16) GPUData
    {
        std::array<float, 4> currentHigh{}, currentLow{};
        std::array<float, 4> previousHigh{}, previousLow{};
        std::array<float, 4> delta{};
        std::array<float, 4> engineToRender{}, previousEngineToRender{};
        std::array<float, 4> engineDelta{};
        std::array<std::uint32_t, 4> flags{}; // enabled, epoch low32, shifted, history valid
    };
    static_assert(sizeof(GPUData) == 144);
    static_assert(offsetof(GPUData, flags) == 128);

    // Render-thread owned. UI requests take effect only at the next frame update.
    // Session-only controls deliberately default OFF on every process launch.
    class Manager
    {
    public:
        bool requestedEnabled = false;
        bool smallGrid = false;
        bool forceShift = false;
        bool verbose = false;

        bool NeedsUpdate(std::uint64_t frame) const { return !initialized || frame != lastFrame; }

        bool Update(std::uint64_t frame, Position camera, std::uint64_t context)
        {
            if (initialized && frame == lastFrame)
                return false;
            const bool first = !initialized;
            const auto lastFrameBeforeUpdate = lastFrame;
            lastFrame = frame;
            const double nextGrid = smallGrid ? 256.0 : 4096.0;
            const bool changedMode = enabled != requestedEnabled || grid != nextGrid;
            const bool validCamera = camera.Finite() && camera.MaxAbs() <= 1.0e12;
            discontinuity = first || frame < lastFrameBeforeUpdate || context != worldContext || !validCamera ||
                (!first && (camera - absoluteCamera).MaxAbs() > 32768.0);
            previousOrigin = currentOrigin;
            enabled = requestedEnabled && validCamera;
            grid = nextGrid;
            if (changedMode || !enabled)
                debugOffset = {};
            if (validCamera) absoluteCamera = camera;
            if (!enabled) {
                currentOrigin = {};
            } else {
                // A forced test shift is persistent. The former one-frame offset
                // exceeded the normal recenter threshold, so every click produced
                // a second, unintended epoch transition on the following frame.
                if (forceShift)
                    debugOffset.x = debugOffset.x == 0.0 ? grid : 0.0;
                const Position logicalOrigin = currentOrigin - debugOffset;
                if (first || discontinuity || changedMode || forceShift ||
                    (camera - logicalOrigin).MaxAbs() > grid * 0.75) {
                    currentOrigin = Position{Snap(camera.x), Snap(camera.y), Snap(camera.z)} + debugOffset;
                }
            }
            forceShift = false;
            shifted = !(currentOrigin == previousOrigin);
            if (shifted || discontinuity || changedMode) ++epoch;
            historyValid = !discontinuity && !changedMode;
            if (!historyValid) previousOrigin = currentOrigin;
            originDelta = currentOrigin - previousOrigin;
            worldContext = context;
            initialized = true;
            return true;
        }

        Position WorldToRender(Position absolute) const { return absolute - currentOrigin; }
        Position RenderToWorld(Position relative) const { return relative + currentOrigin; }
        Position CurrentToPrevious(Position relative) const { return relative + originDelta; }
        Position GetAbsoluteCameraPosition() const { return absoluteCamera; }
        Position GetCurrentOrigin() const { return currentOrigin; }
        Position GetPreviousOrigin() const { return previousOrigin; }
        Position GetOriginDelta() const { return originDelta; }
        std::uint64_t GetOriginEpoch() const { return epoch; }
        bool ShiftedThisFrame() const { return shifted; }
        bool HistoryValid() const { return historyValid; }
        bool Enabled() const { return enabled; }
        bool Discontinuity() const { return discontinuity; }

        GPUData GetGPUData(Position engineOrigin, Position previousEngineOrigin) const
        {
            GPUData data{};
            if (!initialized || !engineOrigin.Finite() || !previousEngineOrigin.Finite() ||
                engineOrigin.MaxAbs() > 1.0e12 || previousEngineOrigin.MaxAbs() > 1.0e12) return data;
            Split(currentOrigin, data.currentHigh, data.currentLow);
            Split(previousOrigin, data.previousHigh, data.previousLow);
            data.delta = Floats(originDelta);
            data.delta[3] = static_cast<float>(grid);
            data.engineToRender = Floats(engineOrigin - currentOrigin);
            data.previousEngineToRender = Floats(previousEngineOrigin - previousOrigin);
            // Subtract in double on the CPU. Avoid (p + hugeCurrent) - hugePrevious.
            data.engineDelta = Floats(engineOrigin - previousEngineOrigin);
            data.flags = {enabled ? 1u : 0u, static_cast<std::uint32_t>(epoch),
                          shifted ? 1u : 0u, historyValid ? 1u : 0u};
            return data;
        }

    private:
        double Snap(double value) const { return std::floor(value / grid + 0.5) * grid; }
        static std::array<float, 4> Floats(Position p)
        { return {static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z), 0.0f}; }
        static void Split(Position p, std::array<float, 4>& high, std::array<float, 4>& low)
        {
            high = Floats(p);
            low = Floats(p - Position{high[0], high[1], high[2]});
        }
        Position absoluteCamera{}, currentOrigin{}, previousOrigin{}, originDelta{}, debugOffset{};
        std::uint64_t epoch{}, lastFrame{}, worldContext{};
        double grid = 4096.0;
        bool initialized{}, enabled{}, shifted{}, historyValid{}, discontinuity{};
    };

    Manager& Get();
    void DrawExperimentalPanel();
}
