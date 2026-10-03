# Phase 9 — Shared Light-Transport World

## Scope

Phase 9 adds a bounded renderer service for sharing compatible spatial-lighting resources without merging PIXL's distinct lighting algorithms. It changes resource discovery and ownership visibility only; lighting equations, shader registers, pass order, public module identities and settings remain unchanged.

## Architecture

`LightTransportWorld` is a frame-scoped rendezvous owned by the renderer. Producers publish reference-counted SRV views with dimensions and the current frame/epoch. Publications expire at the next frame and are invalidated on history discontinuity, resolution change and renderer-resource recreation. This prevents stale cross-frame resource ownership.

Published data currently includes:

- RadiantGrid clustered local lights, cluster indices and cluster grid.
- AmbientProbe environment and sky spherical-harmonic textures.
- SkyBounce's world-space sky-visibility probe volume.
- HybridGI's world irradiance cache surface.

HybridGI is the first migrated consumer. It obtains RadiantGrid emitter data through the shared service when the publication belongs to the current frame. If the service is unavailable or ordering has not produced a current publication, it retains the established direct RadiantGrid path. This compatibility fallback prevents a lighting regression while migration remains incremental.

## Files touched

- `engine/Renderer/LightTransportWorld.h/.cpp`
- `engine/State.cpp`
- `engine/Renderer/RenderPassScheduler.cpp`
- `engine/Modules/RadiantGrid.cpp`
- `engine/Modules/AmbientProbe.cpp`
- `engine/Modules/SkyBounce.cpp`
- `engine/Modules/HybridGI.cpp`
- `engine/Menu/WorkshopToolsRenderer.cpp`
- `tools/TestLightTransportWorld.ps1`
- `cmake/PIXLTests.cmake`

## Runtime and performance impact

The service uses a fixed probe array, one local-light record and short mutex-protected publication/acquisition calls. It performs no GPU copy, CPU readback, filesystem work, scene scan or unbounded allocation. COM references protect SRV lifetime only until the next frame/reset. Existing module-owned textures and buffers remain authoritative.

Developer diagnostics report the current light/emitter counts, published probe resources, dimensions, frame, epoch and invalidation reason.

## Shader/cache implications

No HLSL, resource register, cbuffer, shader entry point, profile, technique descriptor or shader-cache ABI changed in this phase. No permutation rebuild is required specifically for Phase 9.

## Validation

- Release plugin build: passed.
- Static publication/fallback contract test: passed through CTest.
- Portable CTest suite: 14/14 passed.
- `PIXL-Audit`: passed with all 42 shipping modules retained.
- C++/HLSL ABI: unchanged by this phase.
- Live Skyrim visual parity and profiler timing: pending.

## Deferred migration

- Consumers other than HybridGI remain on their established bindings.
- Shared environment/probe sampling semantics should be adopted only where coordinate domain, visibility meaning and temporal validity are demonstrably compatible.
- GPU spatial bins and compact emitter records are deferred until a real consumer can justify them without duplicating RadiantGrid.
