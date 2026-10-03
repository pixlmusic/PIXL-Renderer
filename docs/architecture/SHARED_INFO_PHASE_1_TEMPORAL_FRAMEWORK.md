# Shared Information Upgrade — Phase 1: Unified Temporal Framework

## Scope

Phase 1 establishes one renderer-owned temporal frame snapshot and selective history registry. It does not change Skyrim hooks, shader registers, HLSL constant-buffer layouts, reconstruction ordering, or visual algorithms.

## Architecture

- `TemporalContext` publishes current and previous camera matrices, absolute camera position, frame timing, FOV, camera mode, render/output dimensions, world context, RenderOrigin epoch and the authoritative Skyrim motion-vector SRV.
- Histories register an owner, invalidation mask, reset callback and estimated memory footprint.
- Invalidation reasons are explicit: camera cut, teleport, worldspace change, RenderOrigin shift, resolution change, settings change, module reset and device reset.
- Callbacks run after releasing the registry mutex. A reset therefore cannot deadlock by querying or updating another temporal history.
- `GPUResourceServices` retains its existing history API as a compatibility adapter.

## Initial migrations

- **HybridGI:** screen-space accumulation and world-space voxel/probe cache are separate histories. Camera cuts reset screen history without unnecessarily discarding the world cache; teleports/world changes/origin shifts reset both as appropriate.
- **CameraSuite Auto-DOF:** focus and CoC continuity now consumes the shared history decision while retaining first/third-person, enable-state and Director-specific resets.
- **ImageReconstruction:** DLSS/FSR reset requests consume shared temporal validity and no longer directly reset HybridGI.

## Runtime and resource impact

- No new GPU pass, texture, buffer, register or readback.
- One retained COM reference to Skyrim's current motion-vector SRV.
- CPU work is bounded by the number of registered histories and runs only once per rendered frame or invalidation event.
- Developer diagnostics report frame continuity, motion/disocclusion publication, registered history state, reset reasons and estimated history memory.

## Compatibility

- Existing module settings, module names, hooks, GUI controls and shader ABI remain unchanged.
- Legacy scheduler continuity notifications remain available, but no longer collapse typed events into an indiscriminate `ModuleReset`.
- If no history is registered, modules retain their existing reset flags and safe first-frame behavior.

## Validation

- Release plugin compile: passed after CMake source discovery.
- Static contract test: `tools/TestTemporalContext.ps1`.
- Portable CTest: 15/15 passed, including two strict FXC shader contracts and the temporal contract.
- PIXL-Audit: passed with 42 shipping modules and one retired source-only module.
- Live Skyrim validation remains pending. Required scenarios: load/save, interior/exterior transition, first/third person, fast travel, teleport, FOV change, resolution change, module toggle, DLSS/FSR/native switching and device/resource recreation.

## Deferred migration

- Shared disocclusion publication is supported, but an authoritative screen resource is intentionally not fabricated. Phase 3 will publish reconstruction-owned reactive/disocclusion information with defined semantics.
- Other module-local temporal filters remain compatibility users until their call paths are audited in their owning phase.
