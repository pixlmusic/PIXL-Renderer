#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>

namespace PIXL::Renderer
{
    struct EmitterObservation
    {
        std::uint64_t identity{}; // comparison token only; never dereferenced
        std::array<float, 4> sphere{}; // absolute engine world xyz, radius
        std::array<float, 3> energy{};
    };

    struct EmitterChangeBatch
    {
        static constexpr std::size_t Capacity = 128;
        std::array<std::array<float, 4>, Capacity> spheres{};
        std::uint32_t count{};
        bool overflow{};
        std::uint64_t frame{}, epoch{}, sequence{};
        bool published{};
        bool ValidFor(std::uint64_t f, std::uint64_t e) const { return published && frame == f && epoch == e; }
    };

    // Keep unconsumed changes across skipped render passes. Only a matching
    // generation acknowledgement after the GPU mark dispatch retires evidence.
    class EmitterChangeJournal
    {
    public:
        void BeginFrame() { pending.published = false; }
        void Reset() { pending = {}; }
        void Publish(const EmitterChangeBatch& delta)
        {
            if (pending.epoch != delta.epoch) pending = {};
            pending.frame = delta.frame; pending.epoch = delta.epoch;
            pending.sequence = ++serial; pending.published = delta.published;
            pending.overflow |= delta.overflow || delta.count > EmitterChangeBatch::Capacity;
            for (std::uint32_t i = 0; i < std::min<std::size_t>(delta.count, EmitterChangeBatch::Capacity); ++i) {
                if (pending.count == EmitterChangeBatch::Capacity) { pending.overflow = true; break; }
                pending.spheres[pending.count++] = delta.spheres[i];
            }
        }
        EmitterChangeBatch Acquire() const { return pending; }
        void Acknowledge(std::uint64_t epoch, std::uint64_t sequence)
        {
            if (pending.epoch == epoch && pending.sequence == sequence) {
                pending.count = 0; pending.overflow = false;
            }
        }
    private:
        EmitterChangeBatch pending{};
        std::uint64_t serial{};
    };

    // Fixed storage: no GPU readback, per-frame allocation, or retained engine
    // object ownership. Ambiguous identities conservatively remove/add bounds.
    class EmitterChangeTracker
    {
    public:
        static constexpr std::size_t Capacity = 1024;
        EmitterChangeBatch Update(std::span<const EmitterObservation> observations,
            std::uint64_t frame, std::uint64_t epoch)
        {
            EmitterChangeBatch result{};
            result.frame = frame; result.epoch = epoch; result.published = true;
            result.overflow = observations.size() > Capacity;
            if (initialized && epoch != previousEpoch) previousCount = 0;
            const auto count = std::min(observations.size(), Capacity);
            std::copy_n(observations.begin(), count, current.begin());
            auto order = [](const auto& a, const auto& b) { return a.identity < b.identity; };
            std::sort(current.begin(), current.begin() + count, order);
            auto append = [&](const EmitterObservation& value) {
                if (!std::all_of(value.sphere.begin(), value.sphere.end(), [](float v) { return std::isfinite(v); }) || value.sphere[3] <= 0) {
                    result.overflow = true; return;
                }
                if (result.count == result.Capacity) { result.overflow = true; return; }
                result.spheres[result.count++] = value.sphere;
            };
            std::size_t old = 0, now = 0;
            while (old < previousCount || now < count) {
                if (now == count || (old < previousCount && previous[old].identity < current[now].identity)) {
                    append(previous[old++]);
                } else if (old == previousCount || current[now].identity < previous[old].identity) {
                    append(current[now++]);
                } else {
                    // Compare against the last accepted observation, so a slow
                    // move or fade eventually exceeds the threshold too.
                    const auto& a = previous[old]; const auto& b = current[now];
                    bool changed = a.identity == 0;
                    for (std::size_t axis = 0; axis < 4; ++axis)
                        changed |= !std::isfinite(b.sphere[axis]) || std::abs(a.sphere[axis] - b.sphere[axis]) > 1.0f;
                    for (std::size_t channel = 0; channel < 3; ++channel)
                        changed |= !std::isfinite(b.energy[channel]) || std::abs(a.energy[channel] - b.energy[channel]) > 0.02f * std::max({std::abs(a.energy[channel]), std::abs(b.energy[channel]), 0.01f});
                    if (changed) { append(a); append(b); }
                    else current[now] = a;
                    ++old; ++now;
                }
            }
            std::copy_n(current.begin(), count, previous.begin());
            previousCount = count; previousEpoch = epoch; previousFrame = frame; initialized = true;
            return result;
        }
        void Reset() { previousCount = 0; initialized = false; }
        bool PublishedFor(std::uint64_t frame, std::uint64_t epoch) const { return initialized && previousFrame == frame && previousEpoch == epoch; }
    private:
        std::array<EmitterObservation, Capacity> previous{}, current{};
        std::size_t previousCount{};
        std::uint64_t previousEpoch{};
        std::uint64_t previousFrame{};
        bool initialized{};
    };

    inline EmitterChangeBatch FinalizeEmitterPublication(EmitterChangeTracker& tracker,
        EmitterChangeJournal& journal, std::uint64_t frame, std::uint64_t epoch)
    {
        if (!tracker.PublishedFor(frame, epoch))
            journal.Publish(tracker.Update({}, frame, epoch));
        return journal.Acquire();
    }

    class EnvironmentChangeTracker
    {
    public:
        bool Update(const std::array<float,9>& energy)
        {
            if (!std::all_of(energy.begin(), energy.end(), [](float v) { return std::isfinite(v); })) return false;
            bool changed = false;
            if (initialized) {
                // Ambient L0 and direct-light energy use a relative threshold.
                // Direction is a unit world-space vector, so compare its angle
                // instead of applying a per-component deadband near zero.
                for (std::size_t i = 0; i < 6; ++i)
                    changed |= std::abs(energy[i] - previous[i]) >
                        0.10f * std::max({ std::abs(energy[i]), std::abs(previous[i]), 0.05f });
                const float oldLengthSq = previous[6] * previous[6] + previous[7] * previous[7] + previous[8] * previous[8];
                const float newLengthSq = energy[6] * energy[6] + energy[7] * energy[7] + energy[8] * energy[8];
                if (oldLengthSq > 1e-6f && newLengthSq > 1e-6f) {
                    const float dot = previous[6] * energy[6] + previous[7] * energy[7] + previous[8] * energy[8];
                    changed |= dot / std::sqrt(oldLengthSq * newLengthSq) < 0.9961947f; // five degrees
                } else {
                    changed |= (oldLengthSq > 1e-6f) != (newLengthSq > 1e-6f);
                }
            }
            // Accumulate slow weather/ambient changes against the last accepted
            // baseline, not against a moving per-frame deadband.
            if (!initialized || changed) previous = energy;
            initialized = true;
            return changed;
        }
        void Reset() { initialized = false; }
    private:
        std::array<float,9> previous{};
        bool initialized{};
    };
}
