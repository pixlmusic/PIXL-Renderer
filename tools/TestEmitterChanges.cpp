#include "Renderer/EmitterChanges.h"
#include <cstdlib>
#include <iostream>

using namespace PIXL::Renderer;
static void Check(bool value) { if (!value) std::abort(); }
int main()
{
    EmitterChangeTracker tracker;
    std::array<EmitterObservation, 2> lights{{{1, {0,0,0,100}, {1,0,0}}, {2, {1000,0,0,50}, {0,1,0}}}};
    auto batch = tracker.Update(lights, 1, 0);
    Check(batch.count == 2 && batch.ValidFor(1,0) && !batch.ValidFor(2,0));
    Check(tracker.Update(lights, 2, 0).count == 0);
    std::swap(lights[0], lights[1]);
    Check(tracker.Update(lights, 3, 0).count == 0);
    lights[0].sphere[0] += 20;
    Check(tracker.Update(lights, 4, 0).count == 2);
    lights[0].energy[1] = 0;
    Check(tracker.Update(lights, 5, 0).count == 2);
    Check(tracker.Update(std::span(lights).first(1), 6, 0).count == 1);
    Check(tracker.Update({}, 7, 0).count == 1);
    Check(tracker.Update({}, 8, 0).count == 0);
    // RadiantGrid particle emitters now use their persisted scene-owner pointer
    // as an opaque identity token. Static lights must therefore stay quiet
    // across frames, while movement/removal publishes only their local bounds.
    EmitterChangeTracker particleTracker;
    std::array<EmitterObservation, 2> particles{{
        {0x1000, {25, 40, 10, 120}, {0.8f, 0.35f, 0.1f}},
        {0x2000, {-50, 80, 4, 65}, {0.2f, 0.45f, 1.0f}}
    }};
    Check(particleTracker.Update(particles, 1, 7).count == 2);
    Check(particleTracker.Update(particles, 2, 7).count == 0);
    particles[0].sphere[0] += 2.0f;
    Check(particleTracker.Update(particles, 3, 7).count == 2);
    Check(particleTracker.Update(std::span(particles).last(1), 4, 7).count == 1);
    Check(particleTracker.Update(std::span(particles).last(1), 5, 7).count == 0);
    tracker.Update(lights, 9, 0);
    for (int i = 0; i < 5; ++i) {
        lights[0].sphere[0] += 0.25f;
        batch = tracker.Update(lights, static_cast<std::uint64_t>(10+i), 0);
    }
    Check(batch.count == 2); // slow motion must accumulate beyond the deadband
    std::array<EmitterObservation, 200> many{};
    for (std::size_t i = 0; i < many.size(); ++i) many[i] = {i+10, {0,0,0,1}, {1,1,1}};
    batch = tracker.Update(many, 20, 1);
    Check(batch.overflow && batch.count == EmitterChangeBatch::Capacity);
    Check(tracker.Update(many, 21, 1).count == 0);
    EmitterChangeJournal journal;
    journal.Publish(batch);
    const auto first = journal.Acquire();
    journal.BeginFrame();
    Check(!journal.Acquire().ValidFor(20,1));
    EmitterChangeBatch next{};
    next.frame = 21; next.epoch = 1; next.published = true;
    journal.Publish(next);
    auto retained = journal.Acquire();
    Check(retained.ValidFor(21,1) && retained.overflow && retained.count == first.count);
    journal.Acknowledge(first.epoch, first.sequence);
    Check(journal.Acquire().count == first.count); // stale ack cannot lose updates
    journal.Acknowledge(retained.epoch, retained.sequence);
    Check(journal.Acquire().count == 0 && !journal.Acquire().overflow);
    next.count = 1; next.spheres[0] = {0,0,0,10};
    journal.Publish(next);
    next.epoch = 2; next.count = 0;
    journal.Publish(next);
    Check(journal.Acquire().count == 0); // epoch changes discard old-world bounds
    tracker.Reset(); journal.Reset();
    journal.Publish(tracker.Update(lights, 30, 3));
    auto published = FinalizeEmitterPublication(tracker, journal, 30, 3);
    Check(published.count == 2);
    journal.Acknowledge(published.epoch, published.sequence);
    journal.BeginFrame();
    auto missing = FinalizeEmitterPublication(tracker, journal, 31, 3);
    Check(missing.count == 2 && missing.ValidFor(31,3));
    Check(FinalizeEmitterPublication(tracker, journal, 31, 3).sequence == missing.sequence);
    journal.Acknowledge(missing.epoch, missing.sequence);
    journal.BeginFrame();
    Check(FinalizeEmitterPublication(tracker, journal, 32, 3).count == 0);
    journal.Publish(tracker.Update(lights, 33, 3));
    Check(FinalizeEmitterPublication(tracker, journal, 33, 3).count == 2);
    EnvironmentChangeTracker environment;
    std::array<float,9> energy{1,1,1,1,1,1,0,0,1};
    Check(!environment.Update(energy));
    Check(!environment.Update(energy));
    energy[0]=1.05f; Check(!environment.Update(energy));
    energy[0]=1.15f; Check(environment.Update(energy));
    Check(!environment.Update(energy));
    constexpr float radians = 0.017453292519943295f;
    energy[6] = std::sin(2.0f * radians); energy[8] = std::cos(2.0f * radians);
    Check(!environment.Update(energy)); // sub-threshold sun movement remains local/quiet
    energy[6] = std::sin(6.0f * radians); energy[8] = std::cos(6.0f * radians);
    Check(environment.Update(energy)); // angular change marks the world cache stale
    Check(!environment.Update(energy));
    energy[6] = std::sin(8.0f * radians); energy[8] = std::cos(8.0f * radians);
    Check(!environment.Update(energy)); // sub-threshold steps accumulate against accepted baseline
    energy[6] = std::sin(12.0f * radians); energy[8] = std::cos(12.0f * radians);
    Check(environment.Update(energy));
    environment.Reset(); Check(!environment.Update(energy));
    journal.BeginFrame();
    EmitterChangeBatch weather{}; weather.frame=34; weather.epoch=3; weather.published=true; weather.overflow=true;
    journal.Publish(weather);
    auto weatherAndMissing=FinalizeEmitterPublication(tracker,journal,34,3);
    Check(weatherAndMissing.overflow && weatherAndMissing.count>=2);

    // A global environment update and local emitter movement can be published
    // in either producer order before the cache-mark consumer runs. The journal
    // must retain the local bounds while promoting the batch to a global refresh.
    EmitterChangeTracker combinedTracker;
    EmitterChangeJournal combinedJournal;
    std::array<EmitterObservation, 1> combinedEmitter{{
        {0xCAFE, {12, 24, 36, 48}, {1.0f, 0.5f, 0.25f}}
    }};
    combinedJournal.Publish(combinedTracker.Update(combinedEmitter, 40, 9));
    EmitterChangeBatch environmentChange{};
    environmentChange.frame = 40;
    environmentChange.epoch = 9;
    environmentChange.published = true;
    environmentChange.overflow = true;
    combinedJournal.Publish(environmentChange);
    const auto coalesced = combinedJournal.Acquire();
    Check(coalesced.ValidFor(40, 9) && coalesced.overflow && coalesced.count == 1);
    Check(coalesced.spheres[0] == combinedEmitter[0].sphere);
    combinedJournal.Acknowledge(coalesced.epoch, coalesced.sequence);
    Check(combinedJournal.Acquire().count == 0 && !combinedJournal.Acquire().overflow);

    std::cout << "PASS: creation, removal, stable reorder, bounds, colour, slow movement, epoch and overflow\n";
}
