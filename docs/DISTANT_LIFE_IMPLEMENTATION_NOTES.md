# DistantLife / WorldEmitterLOD implementation notes

> Completion update, 2026-09-27: the static exterior-light MVP described below
> is implemented and now also consumes genuine runtime NiLights for campfires,
> actor torches, and active spell casters. It performs bounded loaded-reference
> discovery, high-process actor refresh, capped RenderOrigin-aware upload,
> depth-aware optical-mask generation, and post-deferred HDR composition. It
> never creates gameplay lights or simulates unloaded actors. Validation and
> future work are recorded in
> `docs/release_polish/DISTANT_LIFE_AND_CONTAINED_LIQUIDS_20260926.md`.

## Confirmed integration contracts

- DistantLife will be an opt-in Experimental module. `RenderModule::Prepass` is the correct per-real-frame update boundary; it is not invoked for generated frames.
- `Deferred::PrepassPasses` runs with render targets deliberately unbound. Provider discovery, immutable snapshot creation and GPU uploads belong there; HDR composition must use a later explicit render boundary.
- `RE::TES::ForEachReferenceInRange` is already used by Ground Response and is the safe starting point for amortized nearby static-reference discovery. It must never become a global per-frame world scan.
- Actor Surface Effects demonstrates resolving live actors only while valid, copying POD render data, and never retaining skeleton or actor pointers through lifecycle boundaries.
- `PIXL::RenderOrigin::Manager` is the single coordinate authority. Emitter snapshots will store absolute source identity/position on CPU and convert to render-relative coordinates immediately before GPU upload.

## MVP scope

1. Static exterior fire/light references discovered on a timed, player-local scan.
2. Stable form/worldspace identity, conservative exterior-only lifecycle, and explicit world/load reset.
3. Compact immutable render snapshot, GPU frustum/depth culling, HDR optical splat composition.
4. Debug source markers/counters and profiler pass labels.

Actor and spell lights are accepted only from genuine, currently represented
actors and runtime NiLights. Persistent actor-mounted lights receive a longer
stale grace period while the high-process provider refreshes them; transient
spell lights retire quickly after their caster light disappears. The provider
does not synthesize movement or inspect unloaded-cell actors.

## Composition constraint

The current deferred prepass has no render targets bound. The MVP must therefore use a dedicated post-deferred composition point that restores the correct forward HDR targets and leaves ordinary geometry/depth ownership intact. It must not draw during provider discovery or hijack CameraSuite presentation.

## Explicit non-goals for the MVP

- No NPC AI, schedule, navmesh, persistence, or save data changes.
- No global world scan or offline emitter database.
- No WindowLife, water reflection, or bokeh provider until static source stability is demonstrated.
- No cache or shader ABI change outside the DistantLife module's private resources.
# Occlusion policy

Distant emitters are depth-tested against the screen before being composited.
The depth tolerance is intentionally limited to a small 8–24 world-unit range
for the source fixture itself; it is not scaled into a wall-sized exemption.
Debug marker modes may still show diagnostic markers through geometry by design.
