# Phase 6 — RenderOrigin Coordinate Authority

## Outcome

The existing RenderOrigin implementation remains the authoritative PIXL world-coordinate service. Its proven double-precision origin, high/low GPU split, epoch and history behavior were preserved. This phase adds domain-safe CPU types, explicit discontinuity reasons and representative consumer migrations without enabling the experimental rebase by default.

## Coordinate Domains

`RenderOrigin.h` now distinguishes:

- `AbsoluteWorldPosition`
- `EngineRelativePosition`
- `RenderRelativePosition`
- `PreviousRenderPosition`

The wrappers use the existing `Position` storage but do not implicitly convert between domains. Explicit manager conversions cover absolute-to-render, render-to-absolute, current-to-previous, engine-to-render, render-to-engine and absolute-to-engine operations. The legacy `Position` overloads remain intact for source compatibility while migration proceeds.

## Temporal Continuity

RenderOrigin now reports why continuity was invalidated:

- startup/load initialization;
- frame regression;
- worldspace or interior-cell context change;
- invalid camera data;
- teleport/large camera jump;
- RenderOrigin mode change.

The scheduler continues to receive the authoritative history-valid bit at frame start. It invalidates registered histories and shared visibility when continuity cannot be guaranteed. Resolution changes remain scheduler-owned because they are not a world-coordinate event.

## Consumer Audit and Migration

- `State` now constructs typed absolute current/previous engine origins before producing the unchanged shared GPU ABI.
- `DistantLife` converts persistent absolute emitter positions to native engine-relative positions through the typed service immediately before upload.
- `SkyBounce` preserves absolute toroidal probe identity and converts its absolute cell origin to the engine-relative domain explicitly.
- Atmosphere and RainResponse already use `Common/PIXLRenderOrigin.hlsli` for current/previous conversion.
- GroundResponse intentionally retains its absolute persistent-field addressing; converting that field to camera-relative space would break persistence.
- View-space systems remain view-space and were not rebased.

HybridGI, WorldProbes, WaterOptics, foliage and other consumers that currently consume shared frame coordinates remain compatible through the existing common buffer/include. Deeper internal migrations are deferred where domain intent cannot be proven without live captures.

## ABI and Shader Cache

- `GPUData` remains 144 bytes with the same field offsets.
- SharedData remains 848 bytes with RenderOrigin fields at the same reflected offsets.
- No HLSL source, register, cbuffer, entry point, profile or shader revision changed in this phase.
- Existing settings and the release-default disabled state are unchanged.

## Validation

- Standalone MSVC `/W4 /WX` RenderOrigin test passed.
- The test covers typed round trips, rejected implicit conversions, hysteresis, 10,000 temporal shifts, world transitions, teleport detection, invalid camera fallback and GPU ABI.
- Strict FXC validation passed and reflection confirmed SharedData offsets 704–832 and total size 848 bytes.
- Release `PIXLRenderer.dll`: built successfully.
- `PIXL-Audit`: passed; all 42 shipping modules retained.
- `git diff --check`: no whitespace errors (repository line-ending conversion notices remain).
- Forced-origin-shift live Skyrim validation: pending.

## Deferred Work

- Migrate additional module internals only after each coordinate contract is demonstrated.
- Add explicit FOV/camera-projection discontinuity signaling to the scheduler when a reliable central event is available.
- Keep RenderOrigin experimental until boundary tests cover gameplay, Photo, Video and Director cameras in live Skyrim.
