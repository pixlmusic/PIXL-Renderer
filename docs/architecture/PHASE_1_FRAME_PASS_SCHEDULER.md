# Phase 1 — PIXL Frame Pass Scheduler

## Status

Compile validated and static validated. Live Skyrim validation remains pending.

## Purpose

Phase 1 introduces a lightweight PIXL pass scheduler above Skyrim's existing DX11 hook points. It does not replace Skyrim's renderer, rename hooks, change shader entry points, alter shader registers, or migrate visual algorithms.

## Architecture Added

`RenderPassScheduler` provides:

- deterministic pass registration and stable dependency ordering;
- explicit semantic render phases while retaining the real Skyrim hook execution point;
- owning-module, resource-read, resource-write, temporal, optional, quality and profiling metadata;
- session-local optional-pass failure isolation for C++ exceptions;
- history invalidation, resolution-change and resource-recreation notifications;
- developer diagnostics for pass order, ownership, phase, resources, state and selected CPU timings;
- native-pass resource declaration checks through `RenderPassContext`;
- stable 64-bit pass identifiers derived from pass name and hook point.

The scheduler currently adapts the existing `ReflectionsPrepass`, `EarlyPrepass` and `Prepass` callbacks. Each hook keeps the original `RenderModule::ForEachLoadedModule` loop as a fail-safe when the scheduler registry is unavailable.

## Ordering and Compatibility

Legacy passes are registered from `RenderModule::GetModuleList()` and filtered into execution-point lists without changing module order. All existing public module methods remain authoritative. Loaded-state checks still occur immediately before invocation.

Representative scheduler scopes are connected to the existing PIXL profiler:

- `AmbientProbe::ReflectionsPrepass`;
- `GroundResponse::EarlyPrepass`;
- `Atmosphere::Prepass`.

The profiler now tracks nested timing scopes correctly, allowing a scheduler scope to contain existing module-owned subpasses. Other inherited legacy callbacks are deliberately not assigned GPU query pairs, avoiding query exhaustion and unnecessary release overhead.

## Resource Declarations

Legacy adapters expose conservative coarse declarations because their implementations still bind DX11 resources directly. `ModuleOwnedResources` is intentionally excluded from shared-write conflict checks. New native scheduler passes can use `RenderPassContext::Read` and `Write` to validate declared access in developer builds/runtime diagnostics.

Fine-grained SRV/UAV/RTV tracking is deferred until the shared GPU resource services in Phase 3. No existing register ownership changed in this phase.

## Lifecycle

- Scheduler registration occurs after module and deferred resource setup.
- A resource-recreation event is emitted after setup.
- output-resolution changes increment the scheduler resource epoch and notify registered native passes;
- RenderOrigin continuity feeds global history validity at frame start;
- history and resource epochs are visible in Developer tools.

Legacy modules do not receive new lifecycle callbacks automatically. This prevents accidental duplicate resets. Modules can opt into scheduler notifications when migrated.

## Files Added

- `engine/Renderer/RenderPassScheduler.h`
- `engine/Renderer/RenderPassScheduler.cpp`
- `tools/TestRenderPassScheduler.ps1`
- `docs/architecture/PHASE_1_FRAME_PASS_SCHEDULER.md`

## Files Modified

- `engine/Deferred.cpp`
- `engine/State.cpp`
- `engine/Profiler.h`
- `engine/Profiler.cpp`
- `engine/Menu/WorkshopToolsRenderer.cpp`
- `tools/TestReactiveFXShaders.ps1` (portable Windows SDK discovery required by PIXL-Audit)

## Shader and Cache Impact

None. No HLSL, shader define, shader profile, entry point, technique descriptor, constant-buffer layout, register assignment, module descriptor or shader-cache ABI value changed.

## Expected Runtime Impact

The existing three module loops now traverse precomputed index lists. Only three representative compatibility passes emit scheduler profiler scopes. Non-profiled passes avoid high-resolution timing calls. No filesystem work, GPU readback or GPU resource allocation was added to normal frame rendering.

## Validation

- Release target: passed.
- `PIXL-Audit`: passed; 42 shipping modules and one retired source-only module retained.
- Scheduler contract test: passed; phases, metadata, hook adapters, direct fallbacks, lifecycle notifications and nested-profiler contracts verified.
- Profiler WARP regression: passed; coherent totals, inactive passes, repeated names, nested scopes and query-ring reuse verified.
- Hybrid GI algebra regression invoked by the profiler harness: 10,000 cases passed.
- C++/HLSL ABI: unchanged by inspection; no shared structure or shader binding changed.
- Hook/API compatibility: original hook methods and direct-loop fallbacks retained.
- Shader permutations: no defines or descriptors changed; no permutation expansion expected.

## Live Skyrim Validation Pending

Verify exterior, interior, reflection cubemap, first-person and third-person rendering. In Developer Mode inspect **Frame Pass Scheduler** and confirm:

1. the registry reports ready;
2. module order matches the former module list;
3. history epoch advances on load/teleport/discontinuity;
4. resource epoch advances after resolution recreation;
5. representative scheduler timings appear in Pulse Profiler;
6. no optional pass reports a failure.

## Deferred Work

- Fine-grained resource hazard/state tracking belongs with Phase 3 resource ownership.
- More precise semantic phases will be assigned only as modules migrate internally.
- Presentation and reconstruction hook adapters remain on their established owners until their call paths are explicitly migrated.
- GPU marker cost and query counts require live profiling before enabling scheduler timing on additional passes.
