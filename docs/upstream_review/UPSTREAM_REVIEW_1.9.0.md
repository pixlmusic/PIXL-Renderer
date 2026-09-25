# PIXL Renderer vs CS 1.9.0

## Scope and method

This is a source-level upstream review of the CS 1.9.0 snapshot at
the local `_REFERENCE` snapshot.
The snapshot has no independent Git metadata, so this review compares actual source,
shader, CMake, configuration, hook, resource, and call-site content. It does not
assume that a newer upstream file is preferable.

PIXL was treated as the authority for product behaviour, module lifecycle, shader
bindings, visual systems, and the PIXL menu. Ground Response and WindowLife are
protected PIXL systems for this review: no upstream replacement or modification is
recommended.

No CS UI, NativeMenu, CSEditor, theme, menu renderer, Remote Control,
or DevBench code is eligible for import. Any future setting must be implemented by
the existing PIXL menu and configuration path.

## Executive summary

CS 1.9.0 contains useful post-1.8.3 infrastructure for a conventional
feature-based renderer, including AE 1.7.99 relocation support, a Wine-oriented
vtable fallback, GPU-driven grass culling, and a few independent engine fixes.
PIXL has diverged far beyond its inherited feature set: its render modules, shader
pipeline library, menu/Tuner, terrain replay, Ground Response, WindowLife, water,
weather, material, GI, and camera systems are materially broader.

The review found no safe wholesale code import. PIXL already contains several
stronger safeguards than the upstream snapshot, including guarded-include dependency
tracking across concurrent shader permutations, defensive settings-override I/O,
module-scoped pipeline invalidation, terrain/Ground Response resource ownership, and
explicit D3D11 hazard cleanup.

The highest-value upstream opportunities are deliberately deferred adaptations:

- complete AE 1.7.99 hook-offset coverage as a PIXL-wide compatibility project;
- GPU-driven grass culling as a new PIXL-owned module, after hook ownership and
  Ground Response interaction compatibility are designed;
- review of the standalone shadow cascade rasterizer tuning as an optional PIXL
  quality experiment, not a direct import.

## Source map

| PIXL area | Closest CS area | Classification | Finding |
|---|---|---:|---|
| `engine/RenderModule.*`, module registry, pipeline module INIs | `src/Feature.*`, feature INIs | D | Same ancestry; PIXL uses its own module identities, health checks, package versions, and UI. |
| `engine/Menu/**`, `TuningWorkspaceRenderer`, theme and translations | `src/Menu/**`, `NativeMenu/**`, `CSEditor/**` | C/F | PIXL is the protected frontend. Upstream UI must not be imported. |
| `engine/ShaderCache.*`, `ShaderFileWatcher.h`, `ShaderTools/**` | `src/ShaderCache.*`, `ShaderFileWatcher.h`, `ShaderTools/**` | C/D | PIXL adds pipeline-library sharding, ABI/layout validation, selective invalidation, and dependency-union tracking. |
| `engine/Deferred.*`, `State.*`, `FrameAnnotations.*`, `Hooks.*` | same-named upstream core files | D | Shared hook ancestry, but PIXL adds terrain replay, Ground Response, SkyBounce, Hybrid GI, PIXL capture and module ordering. |
| `engine/EngineFixes/**` | `src/EngineFixes/**` | D | PIXL retains the three fixes and adds null/range/device guards. AE 1.7.99 offsets remain an upstream-only compatibility addition. |
| `engine/Modules/GroundResponse.*`, `pipeline/Ground Response/**` | `Features/Grass Collision/**` | C/F | PIXL supersedes it with a separate grass interaction field plus independent world-space terrain fields. Do not install a second grass collision module. |
| `engine/Modules/FoliageDynamics.*`, `pipeline/Foliage Dynamics/**`, `RunGrass.hlsl` | `Grass Lighting`, `Grass Optimizations` | C/E | PIXL owns visual wind, interaction and motion vectors. Upstream GPU culling is a new, high-risk renderer rewrite. |
| `Hybrid GI`, `SkyBounce`, `AmbientProbe`, `World Probes` | `Screen Space GI`, `Skylighting`, `IBL`, `Dynamic Cubemaps` | C/D | PIXL's integrated multi-system path is broader. Individual sampling or reset ideas need separate profiling. |
| `Water Optics`, `Waterbody`, `Rain Response` | `Unified Water`, `Water Effects`, `Wetness Effects` | C/D | PIXL has split water ownership and weather integration; direct upstream water replacement would regress it. |
| `MaterialForge`, `Material Layers`, `Terrain Detail`, `Lighting.hlsl` | `PBR module`, `Extended Materials` | C/D | PIXL has material-specific integrations and must retain its GGX/PBR work. Upstream equations require isolated validation before use. |
| `Terrain Seam`, `Terrain Field`, `Terrain Detail`, `Terrain Occlusion` | `Terrain Blending`, `Terrain Helper`, `Terrain Shadows`, `Terrain Variation` | C | PIXL's replay and derived terrain state are more extensive. |
| `Atmosphere`, `Light Volumes`, `Volume Occlusion` | `Exponential Height Fog`, `Volumetric Lighting`, `Volumetric Shadows` | C/D | PIXL owns the atmospheric schedule and resources. |
| `Camera Suite`, `Image Reconstruction` | `HDR Display`, `Upscaling`, `RCAS` | C/D | PIXL has its own HDR/output and upscaler integration. The upstream TAA shader only differs by the HDR define name. |
| `SkinOptics`, `Tissue Diffusion`, `Thin Surface`, `Strand Shading` | `Skin`, `Subsurface Scattering`, `Extended Translucency`, `Hair Specular` | C/D | PIXL is more specialized; retain PIXL GUI and material paths. |
| `Sky Veil`, `Sky Continuity`, `HorizonBlend`, `InteriorDaylight` | `Cloud Shadows`, `Sky Sync`, `HorizonFix`, `Interior Sun` | C/D | PIXL has equivalent or expanded environment controls. |
| `Pulse Profiler`, PIXL capture | `Performance Overlay`, `Screenshot`, `RenderDoc` | C/F | Keep PIXL profiling/capture surfaces. External developer integrations are not release dependencies. |

## Highest-priority findings

| ID | Upstream source | PIXL destination | Class | Recommendation and reason |
|---|---|---|---:|---|
| UP190-001 | `src/Utils/VersionedRelocation.h`; core and feature hook users | all PIXL relocation users | B/P1 | Upstream has distinct offsets/IDs for AE 1.7.99. PIXL currently uses `REL::Relocate(SE, AE)` at many PIXL-specific hook sites. A partial port would be unsafe because unconverted hooks would still target wrong instructions. Create a complete runtime matrix before adopting. |
| UP190-002 | `src/Utils/VTableHookFallback.*` | PIXL hook infrastructure | B/P2 | A robust fallback for Detours/vtable protection failures, especially Wine/CrossOver. PIXL uses many direct `write_vfunc` hooks; importing the helper without integrating its failure path has no effect. Requires a focused hook-infrastructure project. |
| UP190-003 | `src/Features/GrassOptimizations/**` and compute shader | new PIXL vegetation performance module | E/P3 | Valuable GPU-driven culling, indirect draws, Hi-Z, mesh LOD. It replaces vanilla grass draw ownership and hooks raw instance streams. It conflicts with PIXL Ground Response `BSGrassShader::SetupGeometry`, `t100`, grass motion vectors, and Foliage Dynamics unless architected as a single PIXL path. Do not port directly. |
| UP190-004 | `src/Features/GrassCollision.*` | `GroundResponse.*`, `RunGrass.hlsl` | C/F | PIXL already owns the compatible collision field. Upstream's module would collide on vfunc slot `0x6`, grass `b5`, and VS `t100`. |
| UP190-005 | `src/EngineFixes/ShadowmapCascadeRasterizerFix.cpp` | `engine/EngineFixes/ShadowmapCascadeRasterizerFix.cpp` | C/D | PIXL already adds range clamps, null/device guards and safe hook installation. Upstream's 1.7.99 relocation extension belongs in UP190-001. Preserve PIXL bias policy unless a shadow A/B test justifies tuning. |
| UP190-006 | `src/EngineFixes/ShadowmapCascadeCullingFix.cpp` | matching PIXL file | C/D | PIXL retains upstream correction and adds a null guard. Only its AE 1.7.99 offset is missing. |
| UP190-007 | `src/EngineFixes/EffectShaderNoDecalsFix.cpp` | matching PIXL file | C | Functionally retained; no import required. |
| UP190-008 | `src/ShaderFileWatcher.h` | `engine/ShaderFileWatcher.h` | C | PIXL improves safety: include edges are accumulated across concurrent guarded permutations, avoiding false freshness after the last permutation omits an include. |
| UP190-009 | `src/ShaderCache.*` | `engine/ShaderCache.*` | C/D | PIXL adds sharded `Data/PIXL/PipelineLibrary`, shared ABI/layout metadata and selective family invalidation. It also protects engine macro capacity by combining engine and user defines in a vector. Retain PIXL. Audit individual filesystem calls separately if a concrete error is observed. |
| UP190-010 | `src/SceneSettingsManager.*`, `SettingsOverrideManager.*` | matching PIXL files | C | PIXL adds `error_code` handling for filesystem probes, readable-directory checks, stale override-index guards, safer user-override inspection, and clearer warning logs. |
| UP190-011 | `src/Features/Upscaling/**`, `package/Shaders/ISTemporalAA.hlsl` | `ImageReconstruction.*`, `ISTemporalAA.hlsl` | C | TAA source differs only by `HDR_OUTPUT` upstream versus PIXL `CAMERA_SUITE`; PQ conversion logic is otherwise equivalent. No quality or stability change to import. |
| UP190-012 | `src/Features/UnifiedWater/**` | `Waterbody.*`, `Water Optics/**` | C/D | Upstream has useful flow/cache reference material, but PIXL water and weather data ownership is substantially different. A direct port would bypass PIXL resource and GUI contracts. |
| UP190-013 | `src/Features/ScreenSpaceGI/**` | `HybridGI.*`, `SkyBounce.*` | C/D | PIXL's hybrid GI/probe architecture supersedes upstream SSGI. Consider individual numerical checks only after profiling a named artifact. |
| UP190-014 | `src/Features/DynamicCubemaps/**` | `WorldProbes.*` | C/D | Upstream is a reference for lifecycle/reset cases, but PIXL's world-probe ownership is broader. No direct replacement. |
| UP190-015 | `src/Features/PBR module/**`, shader BRDF/PBR helpers | `MaterialForge.*`, `Material Layers/**`, `Lighting.hlsl` | C/D | Exact helper comparison shows PIXL already adds finite GGX D/Smith guards, Schlick input saturation, normal and scalar-variance roughness filtering, and bounded multi-scatter compensation. Upstream remains a reference only; any remaining complex-material artifact must be traced through PIXL's own MaterialForge inputs. |
| UP190-016 | `src/Features/RemoteControl/**`, CMake `DEVBENCH_BRIDGE` | none | F | Adds external MCP/REST bridge and a host dependency. It conflicts with PIXL's local release requirements and is not a rendering improvement. |
| UP190-017 | `src/CSEditor/**`, `NativeMenu/**`, `Menu/**` | none | F | CS UI/editor is deliberately excluded. PIXL's GUI is the sole user-facing system. |
| UP190-018 | `src/Features/Effects11/**`, optional ENB Extender | none | F | An independent effects and external integration architecture that would duplicate or conflict with PIXL Camera Suite/material/post paths. |
| UP190-019 | upstream CMake feature manifest/version scan | PIXL module audit/package scripts | D/P4 | The parser and generated manifest ideas are useful, but PIXL already has its own audit and package flow. Adapt only after comparing the release tooling end to end. |
| UP190-020 | `src/Features/PerformanceOverlay/**` | `Pulse Profiler/**` | C/D | PIXL has its own profiling surface. Upstream A/B aggregation is a possible developer-only research reference, with no Community UI import. |

## Grass collision and Ground Response coexistence

This was traced because it is the most important compatibility boundary for a future
grass optimization feature.

- PIXL Ground Response owns **two different systems**. Its legacy, camera-relative
  `512 x 512` collision texture is sampled by grass in VS `t100`. Its absolute-world
  snow/mud surface state is independently sampled by terrain from `t101+`.
- `GroundResponse::Update()` calls `UpdateCollisionTexture()` separately from
  `UpdateSurfaceDeformationTexture()` and `UpdateGroundMarkField()`.
- The Ground Response grass setup thunk updates the field, calls vanilla setup, then
  rebinds the grass SRV to VS `t100` immediately before the draw. This protects it
  from vanilla state overwrites. `RunGrass.hlsl` applies current and previous
  collision displacement before Foliage Dynamics wind, so motion vectors remain
  coherent.
- Terrain intentionally receives a null PS `t100` and receives surface deformation
  at PS `t101`. This prevents old terrain permutations from interpreting the
  camera-relative grass field as terrain material data.
- The upstream standalone Grass Collision feature owns the same vfunc slot, update
  hook, VS `b5`, and VS `t100`. Loading it beside Ground Response would create hook
  and resource ownership conflicts. It is therefore rejected, rather than merged.

The active Ground Response setting `EnableGroundResponse` is the grass-collision
switch. The `Disable at Boot` JSON still contains the historical inactive key
`GrassCollision: false`; it does not identify an active standalone PIXL module. That
stale compatibility key is migrated away while reading old profiles and has been
removed from defaults/presets. This is a settings cleanup, not another collision
module.

## Runtime, resource, and shader contract observations

### Core lifecycle and hooks

PIXL runs its `RenderModule::PostPostLoad` sequence after core Deferred/Hooks and
engine fixes, before pipeline-cache validation. This lets a loaded PIXL module install
its own hooks and contribute cache validity. Its Deferred/State paths additionally
coordinate TerrainSeam replay, Ground Response final-depth synchronization, weather,
Foliage Dynamics, SkyBounce and Hybrid GI. Those integrations make file-level
upstream replacement unsafe.

Upstream's 1.7.99-specific relocation helper is a real compatibility improvement,
but must cover every PIXL hook that contains an instruction offset. Applying it to
only upstream-derived hooks would leave PIXL-added hooks invalid on that runtime.

### D3D11 resource safety

PIXL has explicit SRV/UAV unbinding before Ground Response compute writes, clears
the terrain-facing legacy `t100` binding, and restores stage resources after terrain
replay. It also retains CPU/HLSL version checks for expanded Ground Response runtime
data. These are stronger than the upstream Grass Collision path and must remain
unchanged.

### Shader cache and permutations

PIXL's shader cache separates source-independent product version changes from
shader ABI/layout changes, validates modules independently, and invalidates only
affected pipeline families when possible. The shader file watcher retains the union
of include dependencies across concurrent permutations, favouring safe
over-invalidation rather than stale output. Upstream does not include that union
behavior.

`ISTemporalAA.hlsl` was compared line by line. The material change is the feature
define rename `HDR_OUTPUT` -> `CAMERA_SUITE`; the PQ/BT.2020 working path matches.

### BRDF and complex materials

The current PIXL BRDF/PBR helpers contain numerical protections absent from the
upstream snapshot: an exact fifth-power Fresnel polynomial, finite GGX distribution
and Smith visibility floors, saturated Fresnel/normal inputs, safe IOR conversion,
and normal/scalar-variance roughness filtering. In particular, `Lighting.hlsl`
applies the scalar-variance filter to the complex-material smoothness channel before
the direct/indirect material evaluation. PIXL also bounds its optional GGX
multi-scatter compensation. These changes are directly relevant to the reported
white/black one-pixel complex-material glints, so importing upstream BRDF code would
remove protections rather than fix the artifact.

### Configuration and GUI

PIXL's scene settings and override managers retain the upstream model but add
filesystem error handling, directory validation, bounds checks, and warning-level
diagnostics. Any future upstream setting is mapped through `RenderModule` settings,
the existing PIXL menu/Tuner, translations, preset logic and persistence. No setting
from this review is currently being exposed.

## Protected PIXL systems that must not be replaced

- PIXL GUI, Tuner, theme, grouped navigation, presets, translations and camera
  compatibility workflow.
- Ground Response, its terrain replay ABI, collision field, derived fields, marks,
  material/Seasons/Material Forge paths and `t100-t111` contract.
- WindowLife and its per-geometry fake-interior coordinate system/resources.
- Water Optics/Waterbody/Rain Response resource ownership.
- Hybrid GI/SkyBounce/AmbientProbe/World Probes scheduling.
- MaterialForge, Material Layers, PIXL lighting, and the current complex-material
  GGX investigation.
- Image Reconstruction and Camera Suite output integration.

## New upstream features

| Feature | Value for PIXL | Difficulty | Risk | GUI impact |
|---|---|---:|---:|---|
| Grass Optimizations | Potentially large CPU draw-call and grass VS reduction through GPU culling/indirect draws | High | High | A future PIXL Vegetation performance section; likely one quality preset plus Advanced controls only. |
| AE 1.7.99 relocation layer | Broader runtime compatibility | Medium | High until every PIXL hook is audited | None. |
| Wine/CrossOver vtable fallback | Better hook resiliency where vtables cannot be made writable | Medium | Medium | None. |
| Remote Control / DevBench | No release value under PIXL's local/no-network policy | High | High | None; reject. |
| CSEditor / NativeMenu | Duplicates protected PIXL GUI | High | High | Reject. |
| Effects11 / ENB extender | Duplicates PIXL post/material architecture | High | High | Reject. |

## Recommended adoption order

### P0 - no code import justified

No upstream change qualified as an immediate P0 import after tracing PIXL ownership.
PIXL already contains the directly relevant safety improvements in the reviewed
paths.

### P1 - correctness and compatibility investigation

1. UP190-001: make a complete AE 1.7.99 relocation matrix for every PIXL hook before
   changing any individual offset.
2. UP190-015: isolate the complex-material GGX contract and compare BRDF inputs,
   roughness, normals, and output behavior against upstream only as a mathematical
   reference.
3. Remove the inactive `Disable at Boot.GrassCollision` compatibility key only with
   settings migration and a test of existing user JSON.

### P2 - contained resilience work

1. Evaluate the VTable fallback in a dedicated hook reliability branch.
2. Audit cache filesystem operations and use PIXL's existing error-code style where
   a concrete vulnerable call is found.

### P3 - optional new functionality

1. Design a PIXL-owned GPU grass culling module that explicitly preserves Ground
   Response VS `t100`, Foliage Dynamics wind/motion vectors, PIXL grass shaders,
   and the existing GUI.
2. Consider upstream flow/cubemap/SSGI algorithms only as individually profiled
   hybrid research tasks.

### P4 - tooling and cleanup

1. Compare upstream manifest generation with PIXL release auditing before any build
   script modernization.
2. Consider a PIXL-only developer A/B tool inside Pulse Profiler, without importing
   CS menus or Remote Control.

## Validation status

This report is an analysis checkpoint. No upstream renderer code has been imported,
no protected module has been changed, and no build is claimed for this review stage.
The companion adoption plan specifies the evidence and validation required before a
safe change can be made.
