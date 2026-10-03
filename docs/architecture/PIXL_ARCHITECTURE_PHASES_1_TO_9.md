# PIXL Renderer Architecture Evolution — Phases 1–9

## Status

Phases 1 through 9 are implemented and compile/static/shader validated. Phase 10 (GPU Scene / Distant World / indirect impostors) was cancelled by the maintainer and no Phase 10 runtime code was added. Live Skyrim validation remains required before release adoption.

## Phase summary

| Phase | Result | Compatibility position |
|---|---|---|
| 1 — Frame scheduler | Lightweight deterministic pass registry, lifecycle events, diagnostics and profiler integration | Existing Skyrim hooks and legacy callbacks remain authoritative with direct fallback |
| 2 — Internal ownership | Extracted focused CameraSuite, GroundResponse, HybridGI and ShaderCache helpers | Public module contracts and shader ABI unchanged |
| 3 — GPU resources | Bounded transient texture/buffer pools, guarded uploads and history registry | DX11 immediate-context fallback retained; persistent histories remain module-owned |
| 4 — Visibility | One shared max-depth pyramid and conservative shader helpers | Existing Foliage Optimizer implementation reused; false-visible fallback |
| 5 — Hooks | Central metadata, signature guarding, status reporting and runtime capabilities | Optional hooks fail closed; critical hook diagnostics are explicit |
| 6 — RenderOrigin | Strong coordinate-domain types and typed migration of representative world-space systems | Legacy overloads and shared GPU layout retained |
| 7 — Testing | CTest portable suite and Windows CI workflow | Existing PowerShell/FXC tests retained |
| 8 — Metadata | Shared setting ranges/defaults plus versioned shader-ABI records | JSON keys and C++/HLSL layouts unchanged |
| 9 — Light transport | Frame-scoped publication of compatible lights/probes; first shared consumer | Module algorithms remain separate; HybridGI retains direct RadiantGrid fallback |

## Before and after

Before:

```text
Skyrim hooks
  -> repeated module lifecycle scans
  -> module-owned resources, histories and visibility
  -> direct cross-module pointer/resource access
  -> dispersed hook and ABI diagnostics
```

After:

```text
Skyrim DX11
  -> validated PIXL hook layer
  -> PIXL frame scheduler (legacy adapters preserved)
       -> RenderOrigin / frame continuity
       -> shared visibility / Hi-Z
       -> bounded GPU resources and uploads
       -> PIXL modules
            -> shared LightTransportWorld publications
       -> image reconstruction
       -> CameraSuite / presentation

Developer diagnostics: pass order + hooks + resources + visibility + ABI + light transport
Portable validation: C++ policy/math + contracts + strict FXC
```

## Major files added and internal splits

- Renderer services: `RenderPassScheduler.*`, `GPUResourceServices.*`, `VisibilityContext.*`, `HookRegistry.*`, `LightTransportWorld.*`, `RendererMetadata.h`.
- Shared shader API: `distribution/Shaders/Common/PIXLVisibility.hlsli`.
- Internal components: `Modules/CameraSuite/CameraPolicy.*`, `Modules/GroundResponse/SurfaceClassifier.*`, `Modules/HybridGI/TemporalPolicy.*`, `ShaderCache/DescriptorUtilities.*`.
- Validation: `cmake/PIXLTests.cmake`, `.github/workflows/portable-validation.yml`, focused `tools/Test*.cpp/.ps1` additions.
- Architecture reports: `docs/architecture/PHASE_1...PHASE_9...`.

No public module was renamed or removed.

## New renderer services

| Service | Responsibility | Initial migrations |
|---|---|---|
| RenderPassScheduler | Deterministic compatibility pass order, declarations, lifecycle and diagnostics | Legacy ReflectionsPrepass, EarlyPrepass and Prepass adapters |
| GPUResourceServices | Bounded exact-descriptor transient reuse, guarded discard uploads, history validity | DistantLife upload path |
| VisibilityContext | Authoritative shared hierarchical depth and fail-open conservative queries | Foliage Optimizer |
| HookRegistry | Hook metadata, runtime capability and install/failure state | Core renderer, D3D11 IAT and optional foliage hooks |
| RenderOrigin | Absolute/engine/render/previous coordinate conversion and continuity epoch | State, DistantLife, SkyBounce; common GPU data remains available to other modules |
| RendererMetadata | Shared setting facts and versioned buffer/register records | MaterialForge, CameraSuite, HybridGI, GroundResponse, RainResponse, WaterOptics |
| LightTransportWorld | Frame-scoped compatible light/probe resource publication | RadiantGrid, AmbientProbe, SkyBounce and HybridGI producers; HybridGI local-light consumer |

## Legacy compatibility adapters still active

- Most module lifecycle execution remains behind scheduler legacy adapters.
- CameraSuite, GroundResponse, HybridGI and ShaderCache retain their public owners and the majority of implementation in their original translation units.
- Persistent CameraSuite and HybridGI history textures remain module-owned.
- Lighting consumers other than HybridGI retain established direct bindings.
- RenderOrigin legacy conversion overloads remain for unmigrated call sites.

These adapters are intentional release-safety boundaries, not abandoned migrations.

## GPU resource ownership map

| Resource | Authoritative owner | Shared exposure/lifetime |
|---|---|---|
| Transient textures/buffers | GPUResourceServices | Generation handle until explicit release; bounded 64-slot pools |
| DistantLife upload buffer | DistantLife | Upload operation delegated; buffer remains module-owned |
| Hi-Z pyramid | VisibilityContext using proven HiZPyramid implementation | Current-frame SRV; invalidated on discontinuity/recreation |
| Radiant clustered lights/grid | RadiantGrid | LightTransportWorld COM views until next frame/reset |
| Ambient SH | AmbientProbe | LightTransportWorld COM views until next frame/reset |
| Sky visibility volume | SkyBounce | LightTransportWorld COM view until next frame/reset |
| World irradiance cache | HybridGI | LightTransportWorld COM view until next frame/reset |
| Temporal GI/camera histories | Owning modules | Registered/invalidation-aware where migrated; no unsafe aliasing |

No blocking GPU readback or cross-frame raw render-pass pointer was introduced.

## Render pass/order map

The real hook order is preserved:

```text
Frame reset / continuity
  -> scheduler BeginFrame
  -> ReflectionsPrepass legacy adapters
  -> EarlyPrepass legacy adapters
  -> Prepass legacy adapters
  -> established deferred/image-space/reconstruction hooks
  -> CameraSuite/presentation
```

Representative scheduler profiler scopes currently cover AmbientProbe, GroundResponse and Atmosphere. Fine-grained native pass migration is deferred until each module can declare exact resource lifetimes without altering render timing.

## Hook/runtime compatibility

Hook records expose owner, runtime, relocation/offset, patch size, required/optional state, signature expectation, install result and failure detail. Statuses are `VALIDATED`, `INSTALLED`, `DISABLED`, `UNSUPPORTED`, `SIGNATURE_MISMATCH` and `RELOCATION_MISSING`. Optional Foliage hooks remain fail-closed to vanilla drawing where unsupported. Live verification across each supported executable build remains pending.

## RenderOrigin migration

RenderOrigin remains the authority for absolute and renderer-relative world coordinates. Typed domains prevent accidental implicit mixing in migrated code. State publishes unchanged high/low GPU origin data; DistantLife converts persistent absolute emitter positions immediately before upload; SkyBounce preserves absolute toroidal identity. GroundResponse intentionally retains absolute persistent-field addressing. View-space systems were not converted.

## Shader ABI and register report

Protected records currently include MaterialForge b6/80, CameraSuite HDR b0/352, CameraSuite Auto-DOF b1/128, HybridGI b1/416, GroundResponse b13/208, RainResponse b6/256 and WaterOptics b6/64. Existing offset assertions remain. Phase 4 added a shared visibility include but retained Foliage t2 and existing cbuffers. No Phase 9 register/layout change occurred.

The only intentional shader-cache revision change in Phases 1–9 is `PIXL.Shaders.20261003.Visibility1`, required because the Foliage visibility include changed. No permutation set was expanded.

## ModuleRules optimization

Module constraints now use an invalidatable indexed snapshot keyed by `featureShortName|settingPath`. Module loading, quality-profile application and the existing reactive UI scan invalidate the snapshot. First-wins conflict behavior and developer-mode bypass are preserved. This removes repeated full module scans from each constrained UI widget.

## Lighting.hlsl review

`distribution/Shaders/Lighting.hlsl` remains the required 5,872-line Skyrim entry point and currently uses 13 guarded includes. A broad split was deliberately deferred: it would touch many permutations and obscure visual parity during release polish. Future extraction should begin with pure material/direct/indirect helper blocks, measure preprocessing/compile/cache effects, and retain the filename, entry signatures, profiles and technique descriptors.

## Test coverage and validation

- Release `PIXLRenderer.dll`: passed.
- `PIXL-Audit`: passed with 42 shipping modules and one retired source-only module.
- Portable CTest: 14/14 passed (six C++ tests, six architecture contracts, two strict FXC tests).
- Strict visibility and RenderOrigin FXC checks: passed.
- Critical C++ structure sizes and reflected SharedData layout: passed where covered.
- Repository whitespace check: no whitespace errors; existing line-ending conversion notices remain.
- Package/runtime path validation: unchanged architecture paths are consumed by the existing build; release packaging itself was not rebuilt in this task.
- Live Skyrim validation: not performed and not claimed.

## Performance-sensitive items requiring live profiling

- Scheduler profiler query overhead when additional passes are opted in.
- Visibility pyramid cost and conservative culling effectiveness in dense grass scenes.
- Transient-pool reuse/VRAM counts across resolution changes.
- LightTransportWorld mutex/publication overhead and HybridGI emitter parity.
- ModuleRules UI-frame savings in the complete tuner.
- Origin-shift and temporal-history behavior during fast travel and camera-mode transitions.

## Live Skyrim checklist

1. Start/load/exit and interior-to-exterior transitions on each supported runtime.
2. Confirm all 42 modules report expected loaded/fallback status.
3. Inspect Developer diagnostics for pass order, hook status, visibility, resource pools, ABI and current-frame light publications.
4. Compare HybridGI emitter lighting before/after in torch, fireplace, particle-light and exterior scenes.
5. Exercise resolution changes, DLSS/DLAA/TAA, frame generation, Photo, Video and Director cameras.
6. Force fast travel, teleport, worldspace/interior changes and RenderOrigin shifts; check for stale histories or light pops.
7. Validate Foliage fail-open behavior and camera ownership through gameplay/photo transitions.
8. Check GroundResponse, water, atmosphere, materials and Auto-DOF for visual parity.
9. Capture Pulse Profiler data for scheduler, Hi-Z, HybridGI and resource counts.
10. Rebuild the release package only after live parity is accepted.

## Deferred work

- Phase 10 GPU Scene / Distant World / indirect impostors: cancelled by maintainer; no implementation present.
- Native scheduler migration beyond representative adapters.
- DX11.1 constant-buffer range binding, only behind capability detection and measured benefit.
- Reflection-backed validation for additional shader buffers.
- Further LightTransportWorld consumers and spatial bins only where semantics match.
- Progressive `Lighting.hlsl` extraction with permutation/cache measurements.
- Full live-runtime hook matrix and visual/performance profiling.
