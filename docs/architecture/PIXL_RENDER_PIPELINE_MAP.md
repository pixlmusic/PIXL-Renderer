# PIXL Renderer — End-to-End DX11 Pipeline Map

Status: current-source structural map, generated from the repository at commit `129fe8f4`.

This document describes the shipping Skyrim SE/DX11 path without changing runtime behavior. It is intended as a guardrail for future agents: before adding a hook, pass, buffer, temporal consumer, or reconstruction contributor, locate it in this map and preserve the ownership and ordering contracts below.

The companion graph is in [`PIXL_RENDER_PIPELINE_GRAPH.mmd`](PIXL_RENDER_PIPELINE_GRAPH.mmd).

## 1. Authority and configuration

### Source authority

- C++ runtime: `engine/`.
- Packaged shader source: `distribution/Shaders/` and module kernels under `pipeline/*/Kernels/`, staged by the existing build/package process.
- Module discovery/version metadata: `pipeline/*/Module.ini` and `engine/ModuleVersions.h`.
- Shipped default configuration: `distribution/SKSE/Plugins/PIXLRenderer/SettingsDefault.json`.
- Runtime output must be verified from the active CMake/build configuration; the historic release output is `build/PIXL-12C/Release/PIXLRenderer.dll`.

Do not treat similarly named files under old staging trees or a game installation as authoritative without tracing the build/deployment copy step.

### Configuration precedence

`engine/State.cpp` loads the default JSON, applies user settings, applies settings overrides, applies the `Disable at Boot` map, and then loads each registered `RenderModule`. Module loading additionally requires the matching `Data/Shaders/PIXL/Modules/<ShortName>.ini` and a compatible version.

The resulting states are distinct:

1. registered in `RenderModule::GetModuleList()`;
2. `.ini` present and version-compatible (`RenderModule::loaded`);
3. not disabled at boot;
4. module setting enabled for the current frame/context;
5. shader class/permutation available and resources valid.

An agent must not infer “active” from the presence of a module directory alone.

### Default configuration facts

- `FoliageOptimizer` is disabled at boot.
- `ReactiveFX.Enabled` is false by default.
- `Curved Surface Mapping.Enabled` is false by default.
- `Contained Liquids.Enabled` is true by default.
- `Hybrid GI.Enabled` is true by default, with world cache, temporal denoising, emitter injection, bent-normal lighting, contact depth, and experimental specular enabled; voxel reflections are disabled.
- `ImageReconstruction` is loaded, but `upscaleMethod` and `upscaleMethodNoDLSS` are `0` in the shipped JSON (`kNONE`). The runtime may select TAA/FSR/DLSS through user settings and capability detection.
- `Camera Suite` is loaded by default; HDR/presentation work remains capability and setting gated.
- `Hair Reconstruction` has a module file and reserved ABI storage but is intentionally absent from the shipping registry. `StrandShading` is the active hair path.

The exact active set can still change through user configuration, missing module files, version mismatch, menu overrides, or resource failure. All optional failures must preserve Skyrim’s original rendering.

## 2. Startup and lifetime flow

### Plugin startup

`engine/XSEPlugin.cpp` is the outer lifecycle owner:

1. SKSE loads the DLL and initializes logging.
2. PIXL rejects known incompatible renderer conflicts, initializes the trampoline, and calls the renderer load path.
3. Early hooks are installed before D3D device creation where required.
4. On `kPostPostLoad`, PIXL installs deferred/core hooks, frame annotation hooks when enabled, engine fixes, and module post-load work.
5. On `kDataLoaded`, settings-dependent data, seasons, shader-menu state, and module `DataLoaded()` callbacks are processed.
6. Shader-cache validation occurs after module loading. The disk cache records each loaded module’s version and enabled state; a mismatch invalidates the cache rather than silently mixing permutations.
7. On renderer/device creation or reset, `globals::ReInit()` and `State::Setup()` recreate resources and notify scheduler/temporal services.
8. At each swap-chain Present, `State::Reset()` begins the next PIXL frame, invalidates frame-scoped publications, resets module frame state, and increments the frame counter.

### Resource recreation contract

`BSShaderRenderTargets::Create` calls the original Skyrim resource creation first, then `globals::ReInit()` and `State::Setup()`. Modules must treat resource recreation as a complete epoch change. Cached raw views, temporal histories, scene copies, and module-owned buffers must either be recreated or invalidated.

`RenderPassScheduler::NotifyResourcesRecreated()` invalidates GPU resource services, light transport, and temporal history. This is the boundary future passes should use instead of retaining resources across device/target recreation.

## 3. Hook inventory and legal insertion points

### Early and renderer hooks

Installed by `engine/Hooks.cpp::Hooks::Install` / `InstallEarlyHooks`:

| Hook family | Role | Safe use |
|---|---|---|
| D3D11 device/swap-chain IAT | Captures device creation and can modify swap-chain format/flags when Camera Suite is loaded | Device-dependent setup only; never assume resources exist before `InitD3D` |
| `BSShader::LoadShaders` | Generates PIXL shader permutations and resolves cache entries | Compile/permutation registration; no per-frame rendering |
| `BSShader::BeginTechnique` | Selects custom VS/PS for the current descriptors | Per-draw shader selection; preserve vanilla fallback if cache lookup fails |
| VS/PS setter call sites | Replaces the shader actually bound after Skyrim state application | Do not retain render-pass pointers; update only current draw state |
| `BSGraphics::SetDirtyStates` | Calls `State::Draw()` after Skyrim reapplies dirty GPU state | Per-draw resource rebinding, permutation CB update, current draw classification |
| render-target creation | Promotes/extends target formats and binds UAV/SRV-capable targets | Modify descriptors temporarily and restore them after the original call |
| image-space/compute dispatch | Observes or substitutes selected Skyrim compute shaders | Preserve original dispatch counts and shader fallback |
| geometry `SetupGeometry` hooks | Adds per-draw classification/permutation data for effect, sky, grass, particles | Per-draw metadata only; do not move frame passes here |
| post-process call sites | Inserts reconstruction and camera processing at Skyrim’s image-space boundary | Maintain target ownership and temporal ordering |
| input/window hooks | Menu, camera, Reflex, focus and shutdown behavior | No render-resource ownership |

### Deferred hooks

Installed by `engine/Deferred.cpp::Deferred::Hooks::Install`:

- `Main_RenderShadowMaps`: original shadow rendering, then `EarlyPrepasses`.
- `Main_RenderWorld`: sets `State::inWorld` around Skyrim’s world render.
- `Main_RenderWorld_Start`: calls `StartDeferred()` before Skyrim world batches.
- `Main_RenderWorld_BlendedDecals`: terrain-seam pass, original blended decals, final depth synchronization, `EndDeferred()`.
- cubemap/reflection hook: keeps reflection-compatible state for module reflection paths.
- renderer reset hook: restores shared/permutation/module bindings after Skyrim state resets.

### Presentation hooks

`CameraSuite` installs its suppression/bottom swap-chain hook before the core Present hook. The top-level `IDXGISwapChain::Present` hook executes first, calls `State::Reset()`, applies frame pacing, delegates to Camera Suite, then runs capture/profiling work.

This ordering matters:

```text
CameraSuite bottom Present suppression
        ^
IDXGISwapChain::Present top hook
        -> State::Reset()
        -> CameraSuite::HandleSwapChainPresent()
        -> optional HDR/output dispatch
        -> original/suppressed Present chain
```

## 4. Per-frame execution timeline

### Frame start

`State::Reset()`:

- ends previous profiler scopes;
- updates workload budgeting;
- begins GPU resource and light-transport frame services;
- calls loaded-module `Reset()` callbacks;
- resets menu/timer/descriptor tracking;
- increments the frame counter;
- resets vanilla reflection/snow state where required.

`State::UpdateSharedData()` is called at early/deferred boundaries and post-display. It snapshots:

- camera matrices and inverse view;
- current and previous camera position adjustment;
- render/output dimensions and dynamic-resolution ratios;
- sun/moon directions and colors;
- directional light and ambient SH;
- water tile data;
- HDR/camera data;
- player water position/velocity;
- render-origin high/low values and epoch;
- temporal frame input and motion-vector view;
- frame timer/count and interior/map/sky flags.

The CPU updates `SharedDataCB` (`b5`), `FeatureData` (`b6`), and binds the current scene-depth SRV to PS `t17`.

### Shadow and early preparation

`Main_RenderShadowMaps` calls `EarlyPrepasses` after Skyrim’s shadow work. PIXL updates shared data, unbinds conflicting output state, copies/exports directional shadow information, begins the scheduler frame, and executes the `EarlyPrepass` order.

Typical early consumers include:

- Ground Response deformation/surface state;
- Interior Daylight;
- Light Volumes;
- Sky Veil;
- Terrain Occlusion;
- terrain/field preparation;
- module-specific shadow/depth captures.

The scheduler is a deterministic compatibility adapter over the existing module callbacks. It does not replace Skyrim’s render loop.

### Deferred scene capture

At `Main_RenderWorld_Start`, `StartDeferred()`:

1. updates shared data for world rendering;
2. saves Skyrim’s first four forward target identifiers;
3. remaps target slots 2–7 to PIXL-owned deferred targets;
4. clears/marks the extra targets;
5. sets `deferredPass = true`;
6. binds the per-frame CB to compute `b12`;
7. executes the scheduler’s `Prepass` order;
8. installs PIXL blend states so normals/motion/deferred targets can be written independently.

Skyrim then renders normal geometry through its original draw path. PIXL shader substitutions write the additional material data while the original bottle, actor, terrain, glass, label, cork, and other geometry draws remain authoritative.

### Blended decals and depth finalization

At `Main_RenderWorld_BlendedDecals`:

1. Terrain Seam renders its special terrain passes where active.
2. Ground Response synchronizes final hardware depth into Terrain Seam depth resources when required.
3. Skyrim’s original blended decals render.
4. `EndDeferred()` restores Skyrim’s forward targets and unbinds PIXL outputs.
5. Deferred lighting/composite runs while the captured G-buffer resources are still available.
6. Contained Liquids replays captured original bottle draws after deferred lighting.

The post-z-prepass depth copy is made after blended decals and before water, so later water/optical consumers can use scene depth that includes the intended pre-water scene boundary.

## 5. Deferred targets and data flow

`engine/Deferred.h` aliases Skyrim render targets for PIXL’s private deferred data:

| Semantic | Skyrim target alias | Format created by PIXL | Main consumers |
|---|---|---|---|
| Albedo | `kINDIRECT` | `R10G10B10A2_UNORM` | Deferred composite, GI/material consumers |
| Specular | `kINDIRECT_DOWNSCALED` | `R11G11B10_FLOAT` | Deferred composite, reflection/GI |
| Reflectance | `kRAWINDIRECT` | `R11G11B10_FLOAT` | World Probes/composite |
| Normal + roughness | `kRAWINDIRECT_DOWNSCALED` | `R10G10B10A2_UNORM` | GI, contact shadows, SSS, Reactive FX collision, composite |
| Material masks | `kRAWINDIRECT_PREVIOUS` | `R11G11B10_FLOAT` | Material classification and reconstruction base mask |
| Secondary masks / vertex AO | `kRAWINDIRECT_PREVIOUS_DOWNSCALED` | `R16G16_FLOAT` | Material Layers effects depth, composite masks2 |
| Main color | original forward target | Skyrim-created format | Deferred composite output and later forward/replay consumers |
| Motion vectors | Skyrim `kMOTION_VECTOR` | Skyrim-created format | Reconstruction, temporal context, upscalers |
| Final normals/TAA mask | Skyrim normal target | UAV-enabled by target hook | Reconstruction and post effects |

Deferred composite binds 18 CS SRVs (`t0..t17`) and three UAVs (`u0..u2`). It combines authored material data with Hybrid GI, probes, SkyBounce, directional/ambient terms, and motion output. It clears all SRVs/UAVs/CBs after dispatch.

## 6. Depth and coordinate contracts

### Depth resources

| Resource | Produced/owned by | Used by | Contract |
|---|---|---|---|
| Main depth / `kMAIN` | Skyrim, with Terrain Seam/ground integration | composite, GI, liquids, particles, camera DOF, reconstruction | authoritative live scene depth; must not be simultaneously bound as conflicting DSV/SRV |
| `kPOST_ZPREPASS_COPY` | Skyrim/PIXL depth copy boundary | `Util::GetCurrentSceneDepthSRV`, water/particles/contact shadows/fallback consumers | pre-water scene depth; preferred 16-bit-compatible scene query where valid |
| `kMAIN_COPY` | reconstruction/refraction path | refraction/underwater upscaling | copied depth used when original main depth cannot remain bound |
| Material Layers effects depth | Material Layers resolve pass | deferred composite/GI | effects-only depth resolved against scene depth and masks2 |
| Terrain Seam depth | Terrain Seam | terrain seam and terrain/ground consumers | temporary replacement/copy; must restore Skyrim depth SRV aliases after pass |
| Normal + roughness | deferred G-buffer | GI, SSS, collision and reconstruction | packed normals/roughness; decode through existing helpers, not guessed channels |
| Refraction normals | Skyrim target hook | reconstruction/refraction paths | optional optical normal resource; absence must disable only the optical path |
| Precipitation/shadow depth | Skyrim target hooks | snow/rain/SkyBounce/volumetrics | separate depth conventions and dimensions; never substitute for main scene depth without explicit conversion |

`Util::GetCurrentSceneDepthSRV(prefer16bit)` selects enabled Terrain Seam blended depth, otherwise `kPOST_ZPREPASS_COPY`; it does not select live main depth. The copy performed after `EndDeferred()` cannot supply same-invocation final opaque depth to `DeferredPasses()`, which runs inside `EndDeferred()`. Future effects must identify the intended depth epoch rather than assuming this helper always returns final live depth.

### Coordinate and temporal rules

- Skyrim world/render coordinates are camera-relative in the GPU frame data; `RenderOrigin` supplies absolute/current/previous conversion and an epoch.
- `FrameBuffer` (`b12`) owns current/previous view, projection, view-projection, unjittered matrices, inverse matrices, camera adjustment, frame parameters, and dynamic-resolution ratios.
- `SharedData` (`b5`) owns renderer-wide camera/light/water/HDR/origin data and binds scene depth at `t17`.
- `TemporalContext` receives one authoritative frame snapshot per frame. It tracks current/previous matrices, motion SRV, disocclusion, absolute camera position, world context, render-origin epoch, render/output sizes, camera mode, delta time, and FOV.
- History invalidation reasons include camera cut, teleport, worldspace change, render-origin shift, resolution change, settings/module reset, and device reset.
- The current service is a single rendered-view context; it is not a per-eye VR temporal model.

Never build a new temporal consumer from a raw camera matrix or an absolute world position without also handling render-origin epoch and history invalidation.

## 7. CPU/GPU ABI map

### Shared constant buffers

| Register | CPU owner | HLSL owner | Purpose |
|---|---|---|---|
| `b4` | permutation/feature descriptors | shared shader includes | shader-class/permutation metadata |
| `b5` | `State::SharedDataCB` | `Common/SharedData.hlsli` | camera, light, water, HDR, origin, frame state |
| `b6` | `GetPipelineBufferData()` | `FeatureData` in `SharedData.hlsli` | concatenated module settings/common data |
| `b12` | Skyrim per-frame buffer plus PIXL frame contract | `Common/FrameBuffer.hlsli` | matrices, camera adjustment, dynamic resolution, temporal frame data |
| `b13` | module-specific tuning/debug buffers | module HLSL includes | extensions that must not expand the stable b6 ABI |
| `b9` and private slots | module-owned | module shaders | narrow per-draw/material extensions, e.g. Material Layers/WindowLife |

### Stable `b6` order

`engine/PipelineBuffer.cpp` packs, in order:

1. Foliage Dynamics
2. Ground Response
3. Material Layers
4. World Probes
5. Terrain Occlusion
6. Radiant Grid
7. Rain Response
8. SkyBounce
9. Sky Veil
10. Water Optics
11. Camera Suite post-process data
12. Distance Blend
13. Strand Shading
14. Terrain Detail
15. Ambient Probe
16. Thin Surface
17. Linear Light Core
18. reserved Hair Reconstruction ABI block
19. Terrain Seam
20. Atmosphere
21. MaterialForge
22. Skin Optics

The HLSL `FeatureData` declaration must remain byte-identical to this sequence. Foliage Dynamics, Rain Response, Water Optics, Material Layers, WindowLife, and other newer extensions deliberately use dedicated buffers or historical padding where documented; do not grow/reorder `b6` casually.

`PipelineBuffer.cpp` uses a thread-local aligned packed buffer to avoid per-update heap allocation. Any ABI change requires CPU `static_assert`, HLSL reflection/compile validation, shader-cache invalidation policy, and review of every module offset after the changed field.

## 8. Module/pass placement

The canonical registration/order is `RenderModule::GetModuleList()` in `engine/RenderModule.cpp`. The scheduler adapts the module virtual methods into three real Skyrim hook points:

### Complete registry and default-state index

The table below follows the actual registry order. “Boot allowed” means the shipped `Disable at Boot` map does not suppress loading; it does not guarantee that a module’s own settings, resource checks, world context, or shader permutations make it visually active every frame.

| Registry order | Module | Default state | Main connection |
|---:|---|---|---|
| 1 | MaterialForge | Boot allowed | per-draw material resources / b6 settings |
| 2 | VolumeOcclusion | Boot allowed | shadow/volume preparation |
| 3 | FoliageDynamics | Boot allowed | shader draw path / main prepass |
| 4 | FoliageOptimizer | Boot disabled | optional grass hooks and visibility; vanilla fallback |
| 5 | GroundResponse | Boot allowed | early prepass, terrain/depth, shader path |
| 6 | ActorSurfaceEffects | Boot allowed | main prepass and actor material data |
| 7 | ContactShadows | Boot allowed | main prepass and scene-depth trace |
| 8 | MaterialLayers | Boot allowed | main prepass, effects-depth resolve |
| 9 | RainResponse | Boot allowed | main prepass and post-composite runoff |
| 10 | RadiantGrid | Boot allowed | light setup interception and main prepass |
| 11 | WorldProbes | Boot allowed | reflection/preparation, composite inputs, post-deferred publication |
| 12 | SkyVeil | Boot allowed | reflections/early preparation and sky shader path |
| 13 | WaterOptics | Boot allowed | main prepass and water shader path |
| 14 | PulseProfiler | Boot allowed | diagnostics/profiler only |
| 15 | TissueDiffusion | Boot allowed | deferred SSS after GI preparation |
| 16 | TerrainOcclusion | Boot allowed | reflections/early preparation |
| 17 | HybridGI | Boot allowed; `Enabled=true` | deferred GI and reflection context |
| 18 | SkyBounce | Boot allowed | early/prepass lighting and probe data |
| 19 | SkyContinuity | Boot allowed | sky/weather state and temporal continuity |
| 20 | TerrainSeam | Boot allowed | terrain render/depth replacement boundary |
| 21 | TerrainField | Boot allowed | terrain resources and shader path |
| 22 | LightVolumes | Boot allowed | early volumetric-light preparation |
| 23 | DistanceBlend | Boot allowed | material/LOD shader data |
| 24 | NaturalLighting | Boot allowed | post-load/managed lighting state |
| 25 | StrandShading | Boot allowed | hair/strand shader path |
| 26 | InteriorDaylight | Boot allowed | early interior-light preparation |
| 27 | TerrainDetail | Boot allowed | terrain shader path/post-load setup |
| 28 | AmbientProbe | Boot allowed | reflections prepass and composite probe inputs |
| 29 | ThinSurface | Boot allowed | thin-surface shader path |
| 30 | ImageReconstruction | Boot allowed | encode/upscale/post-processing hook |
| 31 | PixelCapture | Boot allowed | capture/presentation diagnostics |
| 32 | LinearLightCore | Boot allowed | main lighting shader path/prepass |
| 33 | Waterbody | Boot allowed | water data, flowmap and water hooks |
| 34 | HorizonBlend | Boot allowed; managed service | horizon/lighting state |
| 35 | Atmosphere | Boot allowed | early atmosphere/volumetric pass |
| 36 | CameraSuite | Boot allowed | HDR, exposure, camera finishing and Present |
| 37 | SkinOptics | Boot allowed | character shader path/prepass |
| 38 | WindowLife | Boot allowed | per-draw window/interior shader path |
| 39 | ContainedLiquids | Boot allowed; `Enabled=true` | draw capture and post-deferred replay |
| 40 | CurvedSurfaceMapping | Boot allowed; `Enabled=false` | optional material/parallax shader path |
| 41 | DistantLife | Boot allowed; `Enabled=true` | prepass and post-composite distant effects |
| 42 | ReactiveFX | Boot allowed; `Enabled=false` | event simulation, post-composite particles |

Hair Reconstruction remains a reserved ABI block in `b6` but is not in this registry. Legacy names in the default JSON may be compatibility aliases and must not be mistaken for active module instances.

### Reflections prepass

Runs from deferred reflection/cubemap preparation before the main world deferred capture where the engine exposes that boundary.

Typical consumers: Ambient Probe, Sky Veil, Terrain Occlusion, reflection-related module resources.

### Early prepass

Runs after shadow-map rendering and before deferred world capture.

Typical consumers: Ground Response, Interior Daylight, Light Volumes, Sky Veil, Terrain Occlusion, terrain field/seam preparation, shadow/light captures.

### Main prepass

Runs inside `StartDeferred()` after private target remapping and before Skyrim geometry.

Typical consumers: Actor Surface Effects, Contact Shadows, Foliage Dynamics, Material Layers, Rain Response, Radiant Grid, Tissue Diffusion, Hybrid GI preparation, SkyBounce, Strand Shading, Water Optics, Skin Optics, and other module-owned preparation.

### Deferred/post-composite order

`Deferred::DeferredPasses()` currently performs:

1. Material Layers effects-depth resolve.
2. Hybrid GI and reflection-context acquisition.
3. Tissue Diffusion SSS.
4. World Probes cubemap update.
5. Deferred composite into main color, final normals/TAA mask, and motion vectors.
6. Distant Life.
7. Reactive FX, when loaded/enabled.
8. Rain Response roof runoff.
9. World Probes post-deferred publication.

Then `EndDeferred()` restores Skyrim state and invokes Contained Liquids replay. This is a deliberate ordering: liquids replay over the already-lit authored bottle scene, while Reactive FX collision sees final opaque depth/normals.

### Special direct hooks

Some systems are not ordinary scheduler passes:

- Material/shader features use `State::Draw()` and shader descriptor hooks.
- Radiant Grid intercepts point-light setup and selected batch-render call sites.
- Terrain Seam intercepts terrain batch rendering and temporarily substitutes depth.
- WindowLife, Contained Liquids, and similar systems use per-draw capture/classification hooks.
- Camera Suite and Image Reconstruction own post-processing/presentation hooks.
- Frame Annotations, when explicitly enabled, wraps many engine rendering entry points for diagnostics; it is not part of the default visual path.

## 9. Lighting and indirect-light connections

The lighting graph is intentionally layered rather than a single lighting pass:

```text
Skyrim directional shadows / ambient SH / local light setup
        -> RadiantGrid / LightVolumes / VolumeOcclusion
        -> material and G-buffer shader substitutions
        -> HybridGI screen-space trace + world cache + optional specular
        -> SkyBounce / AmbientProbe / WorldProbes
        -> DeferredCompositeCS
        -> optional forward/replay effects
```

`LightTransportWorld` is a frame-scoped rendezvous for current published SRVs. It prevents consumers from using stale cross-frame light resources but does not replace module-owned resources.

Hybrid GI consumes effects depth, normal/roughness, motion, material data, radiance inputs, and available emitter/light publications. Its outputs are bound into the deferred composite as AO, diffuse radiance, CoCg, optional specular radiance, and bent visibility.

The default config enables the main Hybrid GI path and world cache, but not voxel reflections. Future GI additions should document whether they contribute to the shared composite, reflection context, or only a module-owned effect target.

## 10. Annotation and reconstruction flow

### Base annotation publication

After deferred lighting is complete, `ImageReconstruction::Upscale()` acquires:

- final main depth;
- motion vectors;
- final normal/TAA mask;
- deferred material mask;
- normal/water mask;
- temporal-AA mask.

It publishes these through `PixelAnnotations::PublishBase()` and binds them to `EncodeTexturesCS`.

`PixelAnnotations::Classify()` defines an explicit material priority, including Contained Liquid, Window Interior, Water, Sky, Skin/Hair/Grass and other classes. Flags include transparent, reactive, fast-temporal, thin-surface, deformable, and emissive. This is a CPU classification helper, not a demonstrated per-pixel runtime producer: the current reconstruction path publishes existing G-buffer/mask SRVs, not a newly rasterized `MaterialClass` image.

### Reactive publication

`ReconstructionContext` owns a bounded setup-time contributor list. Current contributors include:

- Contained Liquids;
- Reactive FX.

The encode/dilation stage creates or updates reconstruction reactive/transparency resources, then invokes contributors against the reactive UAV. It publishes the complete reconstruction frame with depth, motion, masks, annotations, backend, dimensions, frame index, and temporal validity. Reactive coverage is deliberately not published as geometric disocclusion; a disocclusion SRV is only valid when a producer establishes that semantic.

The max-with-current-target policy preserves stronger reactive information from other PIXL systems. Contributors must only write their own bounded contribution and must not clear the shared target.

### Reconstruction branches

`ImageReconstruction::Main_PostProcessing` selects the method at runtime:

- `kNONE`: no external upscaler path;
- `kTAA`: temporal path and Skyrim/PIXL temporal handling;
- `kFSR`: FSR resources and depth guide;
- `kDLSS`: Streamline/DLSS resources and motion guide;
- optional neural rendering: capability-gated and bypasses safely to ordinary DLSS on failure;
- optional frame generation: separate sidecar/shared-resource path, not the core DX11 render path.

Upscaling occurs before Skyrim’s HDR image-space operation. The hook redirects the framebuffer to an HDR-capable target when Camera Suite is active, then restores it after the original image-space call.

## 11. Final output path

The final output chain is:

```text
DeferredCompositeCS -> main color / final normals / motion
        -> late PIXL effects and Contained Liquids replay
        -> remaining Skyrim forward water/transparency/effects
        -> ImageReconstruction encode + optional upscale/sharpen
        -> Skyrim ISHDR image-space operation
        -> CameraSuite HDR/camera finishing:
             exposure, local exposure, bloom, lens effects, DOF, look/LUT, UI
        -> HDR output compute or safe copy fallback
        -> swap-chain back buffer / frame-generation wrapped buffer
        -> IDXGISwapChain::Present
```

`CameraSuite::ApplyHDR()` selects the HDR scene texture when available, otherwise the vanilla framebuffer, binds optional UI/bloom/local-exposure/stormglass/lens/DOF inputs, dispatches its output compute, and copies the result to the swap-chain buffer. If the output shader/resources are unavailable, it copies the safe framebuffer instead.

The final presentation path may suppress the underlying Present chain so HDR composition can occur before the actual Present. UI is redirected to a separate buffer when required and is cleared/restored after output.

## 12. Resource hazards and fallback rules

Future passes must observe these rules:

- A D3D11 resource cannot be simultaneously bound as a conflicting RTV/DSV and SRV/UAV. Explicitly unbind before copy/read/write and restore the exact target set afterward.
- Main depth, main color, normals, motion, and deferred targets have different lifetime boundaries. A pass must not assume a target remains bound after `EndDeferred()`.
- Scene depth helpers encode a boundary decision; do not substitute a different depth copy merely because it is available.
- Optional normal/roughness and scene-color inputs must be capability-gated. Collision or optical failure must not suppress core particles, geometry, or vanilla rendering.
- Shader-cache lookup failure falls through to Skyrim’s original shader.
- Module `.ini` absence/version mismatch marks that module unloaded rather than partially executing it.
- Scheduler compatibility passes are optional and retain the direct legacy fallback when the registry is unavailable.
- Temporal history must be invalidated on device/resource recreation, resolution changes, world/camera discontinuities, and settings changes that alter reconstruction semantics.
- Reconstruction publications are cleared at frame start so switching from DLSS/FSR to native/TAA cannot expose stale masks, SRVs, dimensions, or backend diagnostics.
- No frame-to-frame raw `BSRenderPass*` ownership is valid.

## 13. Future-agent hook selection guide

| Desired feature | Preferred boundary | Why |
|---|---|---|
| Per-material classification | shader setup/permutation + G-buffer write | sees authored geometry and preserves vanilla draw ownership |
| Deferred opaque lighting | `Deferred::DeferredPasses()` before composite or inside composite inputs | has complete G-buffer, depth, shadows, GI and probe data |
| Terrain/deformation depth | early prepass / Terrain Seam boundary | depth can be corrected before consumers sample it |
| Particle collision | after deferred opaque composite, before reconstruction | final depth/normals are available; reactive contribution remains possible |
| Water/refraction | water/pre-water depth boundary and reconstruction optical stage | separates opaque scene depth from water/optical history |
| Transparent/liquid replay | `EndDeferred()` replay boundary | authored shell/labels/corks remain original and are lit-context aware |
| Reactive mask contribution | `ReconstructionContext` contributor | preserves shared max-composition and backend independence |
| Temporal history | `TemporalContext` history registry | receives typed invalidation and render-origin continuity |
| Final grading/display effect | CameraSuite presentation chain | operates on final HDR scene/UI with explicit output fallback |
| Developer instrumentation | FrameAnnotations or scheduler diagnostics | avoids adding normal-path visual work |

Avoid inserting effects into `State::Draw()` unless the effect is genuinely per-draw. Avoid adding full-screen work before the deferred composite when the required G-buffer/depth facts are only finalized afterward.

## 14. Validation checklist

Before modifying a future subsystem:

- Locate its module registration, `.ini`, settings key, shader source, and loaded-state gate.
- Identify its exact hook and whether it runs before or after `StartDeferred`, `EndDeferred`, reconstruction, or Present.
- Record every SRV/UAV/RTV/DSV and constant-buffer register it touches.
- Verify CPU/HLSL structure size, alignment, field order, and shader permutation define.
- Decide whether the resource is frame-local, module-owned, temporal, or recreated on device/resolution changes.
- Use `TemporalContext` for continuity and invalidation rather than private camera-history heuristics.
- Use `PixelAnnotations`/`ReconstructionContext` for classification/reactivity rather than introducing a duplicate full-screen mask.
- Preserve vanilla/original rendering when classification, resource creation, shader lookup, capture, or replay is unsafe.
- Validate first-person, third-person, interiors, exteriors, water, menus/loading, world transitions, camera cuts, dynamic resolution, TAA/FSR/DLSS, and resource recreation.

## 15. Known limits and deliberate non-goals

- The scheduler remains a compatibility adapter; most module callbacks still own their direct D3D11 bindings.
- The temporal context is single-view, not stereo/per-eye.
- Legacy Frame Annotations remain opt-in diagnostics; the newer compact opaque `PixelAnnotations` GPU texture is produced by `TemporalValidityGPU` when its typed UAV is supported. Its taxonomy is intentionally incomplete until explicit forward/material producers exist.
- The default configuration does not activate Reactive FX, Foliage Optimizer, or Curved Surface Mapping.
- Frame generation and neural rendering are capability-gated side paths and are not required for the native DX11 output chain.
- No new G-buffer, lighting architecture, ray tracing path, or renderer replacement is introduced by this map.

## 16. Verified handoff contracts (2026-10-05 follow-up)

| Boundary | Required contract | Enforcement in this pass |
|---|---|---|
| Late effects to reconstruction encode | `SharedData` CS `b5` supplies depth-linearization parameters; late effects may clear it | Encoder explicitly binds its shared CB, then clears it after dispatch |
| Encode to backend | Mask/depth/motion guides must be current-frame and valid | Preflight precedes output unbinding and reset consumption; backend success propagates to caller |
| Backend to depth/sharpen/NR guide copy | No dependent work on failed dispatch | `Upscale`/`PerformUpscaling` return success; dependent stages are gated |
| Target/device recreation | Same backend does not imply same resource epoch | Guide/intermediate textures recreated; annotation/reconstruction publications invalidated |
| Capture rectangle to liquid reactive mask | Both use active-region pixel coordinates, not normalized backing coordinates | Removed second dynamic-resolution scale; clamp rectangles to encode extent |
| Temporal snapshot | External reset between snapshots must remain visible in next snapshot | Pending invalidation mask; frame-index discontinuities reject history |
| Annotation publication | Base and reconstruction masks must describe the same extent | Reject mismatched reconstruction publication; base refresh clears previous reconstruction guides |

These are source-verified contracts, not a certification of every module's GPU behavior. Single-view snapshot ordering relative to auxiliary cameras, depth-copy validity on unsupported descriptors, and backend partial-write recovery still need live/debug-layer investigation. No measured GPU timing or full visual certification is implied.

## 17. Typed handoffs under staged migration (2026-10-05)

The frame scheduler now issues `FrameToken{frame, ResourceEpoch, ViewType, viewSerial}` and a `RenderExtent` with backing size and active rectangle. EarlyPrepass and Prepass reuse the same MainWorld view token. Reflection/cubemap work receives a distinct view; only MainWorld advances the shared temporal snapshot. `DepthView` publications identify their depth epoch, format, extent and view token. Native pass declarations may state view, depth, temporal and resolution requirements; legacy module callbacks retain their previous ordering and fallback.

The scoped DX11 copy binding helper removes conflicting RTV/DSV/SRV/UAV bindings and restores exactly the bindings it changed. It is not a blanket interception layer for every legacy module. The bounded `OpticalCompositeQueue` holds only same-frame liquid replay callbacks; Contained Liquids still owns its scene crop and the original Skyrim draw still runs. Shared optical scene capture and other forward owners are not yet migrated.

After the opaque deferred composite, `TemporalValidityGPU` resolves a two-channel R8G8 validity texture (geometric disocclusion, history confidence), a packed opaque-surface history, and a compact R16/R32 integer annotation texture when the format is supported. This is a new full-active-region compute pass; its result is published through typed temporal/annotation contexts. It is not yet a substitute for the established module-specific temporal rejection, and most material classes cannot be inferred safely from the available G-buffer masks. Forward glass, WindowLife, contained liquids and foliage still need explicit class contributors. Reactive coverage remains separate from geometric disocclusion.

`ReconstructionContext` now validates frame/view/resource epoch for typed publications while preserving its max-composition contributors. `LightTransportWorld` publishes typed metadata with local lights and probe SRVs; Hybrid GI accepts typed same-view lights and falls back to RadiantGrid's module-owned buffers when unavailable. Other consumers and the legacy acquisition API remain. The existing `GPUResourceServices` bounded transient pool is retained, with full-pool reuse corrected to pick an idle entry; temporal histories remain persistent. `b6 FeatureData` and the CPU/HLSL G-buffer ABI are unchanged. The normal/roughness target stores 10-bit normal X/Y and 10-bit glossiness in RGB; its 2-bit alpha is not roughness.

Adaptive GPU workload remains an opt-in developer control. Missing timer samples no longer count as headroom. Hybrid reflection timing is budgeted separately from diffuse GI and may reduce reflection ray steps when enabled. Existing quality contracts already scale substantial GI, atmosphere, water and material work. There is no measured frame-time or visual certification for this migration; in-game DX11 debug-layer, dynamic-resolution and TAA/FSR/DLSS validation remain required before release.

The disk shader cache already validates layout, shared ABI, shared shader revision and module revisions. No new cache-key salt was added for these standalone extension shaders because no existing shader ABI/permutation changed; changing the global salt would force a full user cache rebuild. Future cache work should persist include dependency fingerprints without invalidating unrelated prebuilt permutations.

## Primary source index

- `engine/XSEPlugin.cpp`
- `engine/Hooks.cpp`, `engine/Hooks.h`
- `engine/State.cpp`, `engine/State.h`
- `engine/Deferred.cpp`, `engine/Deferred.h`
- `engine/PipelineBuffer.cpp`, `engine/PipelineBuffer.h`
- `engine/RenderModule.cpp`, `engine/RenderModule.h`
- `engine/Renderer/RenderPassScheduler.*`
- `engine/Renderer/TemporalContext.*`
- `engine/Renderer/PixelAnnotations.*`
- `engine/Renderer/ReconstructionContext.*`
- `engine/Utils/D3D.*`
- `engine/Modules/ImageReconstruction.*`
- `engine/Modules/CameraSuite.*`
- `distribution/Shaders/Common/SharedData.hlsli`
- `distribution/Shaders/Common/FrameBuffer.hlsli`
- `distribution/Shaders/DeferredCompositeCS.hlsl`
- `distribution/SKSE/Plugins/PIXLRenderer/SettingsDefault.json`
- `pipeline/*/Module.ini`
