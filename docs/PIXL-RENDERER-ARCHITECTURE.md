# PIXL Renderer v1.0 architecture

PIXL Renderer is shipped as one renderer, not a collection of separately installed shader add-ons. The inherited feature short names and `Shaders/Features/*.ini` files remain compatibility adapters: Skyrim's hook discovery, saved settings, shader defines, register bindings, and material providers depend on those stable identities.

## Unified source map

```text
PIXL-Renderer-Engine/
|-- engine/        C++ renderer, hooks, modules, GUI, runtime services
|-- pipeline/      module descriptors, kernels, and module-owned assets
|-- distribution/ tracked runtime template and shared Skyrim shaders
|-- installer/     FOMOD metadata and installer artwork
|-- include/       external public headers
|-- extern/        pinned third-party source dependencies
|-- cmake/         build integration, ports, patches, and templates
|-- tools/         portable build, test, audit, package, and export tools
|-- docs/          public architecture, compatibility, and release guidance
|-- build/         generated local build output (ignored)
`-- dist/          generated release staging and archives (ignored)
```

- `engine/Renderer/` owns cross-system policy such as Low/Medium/High/Ultra quality contracts.
- `engine/Modules/` contains runtime hook adapters and module-specific resource lifetimes.
- `engine/Hooks`, `engine/Runtime`, `engine/Utils`, and the root-level engine coordinators own shared SKSE, DX11, lifecycle, and utility infrastructure. Code belongs here only when multiple renderer modules depend on it.
- `engine/Menu/` contains the Essentials, Advanced, and Developer presentation layers.
- `distribution/Shaders/Common/` is the shared physical shading ABI and BRDF library.
- `distribution/Shaders/` contains Skyrim entry-point overrides whose names and signatures must remain unchanged.
- `pipeline/*/Kernels/` is the authoring location for integrated subsystem shaders. Packaging merges every module into one `Data/Shaders` tree.
- `distribution/SKSE/Plugins/PIXLRenderer/` owns settings, the tested preset, theme, and translations.
- `installer/` owns FOMOD metadata and installer presentation assets.
- `tools/` contains portable build, validation, packaging, and source-export tooling. Personal deployment or service-administration scripts are not public repository content.
- `extern/` and `include/` contain third-party dependencies and their stable integration surface. Their licences and upstream history remain separate from PIXL-owned code.

## Source, build, staging, and release boundaries

| Boundary | Location | Contract |
|---|---|---|
| Source | repository root, `engine/`, `pipeline/`, `distribution/`, `installer/`, `cmake/` | Tracked, reviewable inputs only. No compiler output, caches, captures, credentials, or machine-specific paths. |
| Build | `build/<preset>/` | CMake, compiler, test, audit, and temporary staging output. Entire tree is generated and ignored. |
| Runtime template | `distribution/` plus module assets selected from `pipeline/` | Authoritative tracked inputs used to assemble a Skyrim `Data` layout. It is not itself a developer build directory. |
| Release staging | `dist/` through `tools/StagePixlRendererStandalone.ps1` | Generated, ignored, manifest-backed package content. Only runtime files and deliberate user/legal documents are admitted. |
| Deployment | Game roots supplied through `PIXL_SKYRIM_ROOT_1/2/3` | Local operation outside the source tree. Paths are environment configuration and are never committed. |

The existing directory names are runtime contracts in several places. Shader includes, module descriptors, package overlays, and Skyrim resource lookup all depend on them. Structural cleanup therefore preserves these stable roots instead of creating parallel `src/`, `modules/`, or `shaders/` trees.

## Adding a renderer module

1. Put the C++ owner and lifetime code in `engine/Modules/<ModuleName>.h/.cpp`. Put reusable renderer infrastructure in `engine/Renderer/` only when it serves more than one module.
2. Add `pipeline/<Display Name>/Module.ini` with the stable runtime ID. Place shader sources below `pipeline/<Display Name>/Kernels/<RuntimeId>/` so the existing compiler and staging discovery can find them.
3. Use `distribution/Shaders/Common/` for genuinely shared shader ABI and math. Keep module-only helpers with that module.
4. Add module controls through the established `engine/Menu/` framework and keep settings in the module's authoritative C++ settings structure and descriptor.
5. Add portable validation under `tools/`; generated test output must stay below `build/`.
6. Run the canonical `PIXL-Audit` target. It verifies module registration, descriptors, shader dependencies, runtime assets, repository hygiene, and package contents.

## Player quality contract

| Group | Low | Medium | High (default) | Cinematic | Apply path |
|---|---|---|---|---|---|
| Lighting | 2x4 horizon rays, sparse cache, 12 reflection steps | 4x8 horizon rays, balanced cache, 24 reflections | Former Cinematic 6x12 rays, 48 reflections, 4x directional shadows | 10x20 rays, 64 reflections, 12x directional shadows and maximum cache cadence | Recompile HybridGI/reflection permutations, invalidate contact raymarch, reset history |
| Materials | Physical BRDF; expensive POM disabled | 6/12 object and 6/14 terrain POM | Former Cinematic 12/24 object and 10/30 terrain POM | Bounded 24/32 object and 30/64 terrain POM plus maximum refinement | Shared feature constants; restart only when the underlying feature was boot-disabled |
| Atmosphere | 64 px x 24-slice froxels | 40 px x 36 slices | Former Cinematic 24 px x 64 slices | 16 px x 80 slices and 8 history-miss samples | Recreate active volumetric targets; appearance controls are live and independent |
| Water | 16-step enhanced SSR | 28-step enhanced SSR | Former Cinematic 48-step enhanced SSR | 144-step SSR with 10 hit-refinement steps | Reset reflection history; boot-disabled Water Optics still requires restart |
| Terrain & Vegetation | Aggressive size/density/mesh LOD plus 4x ground tessellation | Balanced culling and 7x ground tessellation | Release foliage density plus 10x/2.5x ground tessellation | Maximum retained grass and 16x/6x bounded tessellation | Shared constants are live; initial shader integration requires cache build |
| Characters | 6-tap Burley, 8 nearby actors | 12-tap Burley, 16 actors | 24-tap Burley, 48 actor capacity and former maximum hair | 64-tap Burley, 64 actors and extended reconstruction distance | Refresh SSS kernels; feature boot changes require restart |
| Camera | Sparse metering, 6-tap DOF | Balanced metering, 10-tap DOF | Former Cinematic metering and 16-tap DOF | 4x histogram density, 24 local-exposure and 48 DOF taps | Camera constants are live; display-mode changes may require restart |

Changing Global applies the selected quality to all seven groups. Changing an individual group produces a Custom global state without altering the other categories. Low changes cost, not the fundamental PIXL colour/lighting identity.

## Hook and parameter contract

The following runtime contracts are intentionally preserved:

- All HLSL entry functions named `main`, shader filenames, engine technique descriptors, vertex/pixel/compute profiles, and `VSHADER`/`PSHADER`/`CSHADER` compile defines.
- Every explicit `register(bX/tX/uX/sX)` assignment and all C++/HLSL constant-buffer layouts. `MaterialForge::Settings` remains an 80-byte five-register ABI.
- Feature short names such as `MaterialForge`, `HybridGI`, and `WaterOptics`, because they are serialized and used as hook defines.
- Material Layers and authored Material Forge as material providers; legacy inference never overrides authored conductor data.
- HybridGI as the authoritative indirect-light path; the PIXL world cache augments misses and temporal stability.

Quality controls are allowed to modify existing runtime settings, request a feature-owned resource recreation, invalidate a compiled internal shader, rebuild an SSS kernel, or reset temporal history. They must not mutate register ownership, hook signatures, vertex layouts, or saved feature identities.

## Shipped subsystem inventory

The standalone bundle includes every active graphical renderer component present in this source tree. Player-facing systems are grouped into Lighting, Materials, Atmosphere, Water, Terrain & Vegetation, Characters, and Camera. PulseProfiler, PixelCapture, benchmark, and diagnostics interfaces are gated by their runtime settings or Developer tools. PDBs and retired developer integrations are not player-package content. ImageReconstruction remains an Advanced display choice and is not silently enabled by a quality profile.

## Compiler policy

- Startup compilation automatically uses `hardware_concurrency - 2` workers (or reserves one worker on low-core CPUs).
- Background compilation is capped to half the detected performance cores to protect gameplay frame pacing.
- Hardest shader permutations are dispatched first to reduce the long tail.
- Disk cache, asynchronous compilation, and skip-unchanged behavior are enabled by default.
- Cache files and `Info.ini` live under `Data/ShaderCache/PIXLRenderer`, so PIXL cache cleanup does not delete another renderer's cache.
- Developer mode deliberately emits debug shaders and is slower; it is not enabled by Advanced mode.

## Preview images

Essentials mode reserves a comparison-preview area. Future Low/Medium/High/Cinematic images should be authored below `distribution/Interface/PIXLRenderer/Previews/<group>/` and selected by the persisted group quality. Preview assets are illustrative only; the renderer policy in `engine/Renderer/QualityProfiles.cpp` remains authoritative.
# Future: PIXL Distant World

GPU-driven static-world rendering is a viable future subsystem, but it is not enabled in PIXL Renderer 1.0. Skyrim exposes Direct3D 11 LOD-object and distant-tree passes; it does not provide a general indirect-instance pipeline that a shader define can simply enable.

The safe implementation order is:

1. Build a read-only registry for repeated, static distant trees, rocks, and clutter. Exclude actors, animated/skinned meshes, unique quest objects, alpha-blended geometry, and anything with mutable per-reference state.
2. Group compatible instances by mesh, material, shader permutation, and shadow policy. Preserve the original vanilla draw whenever a batch cannot prove semantic parity.
3. Upload camera-relative transforms and bounds to compact structured buffers. Share one source mesh and material set per batch; instancing alone is not described as a VRAM saving.
4. Run frustum, distance, and conservative hierarchical-depth culling into visible-instance lists and `D3D11_DRAW_INDEXED_INSTANCED_INDIRECT_ARGS` buffers.
5. Submit `DrawIndexedInstancedIndirect` only for validated static batches. Keep vanilla CPU draws for unsupported objects and for the whole feature when resources, hooks, or shaders fail.
6. Add tiered mesh pages, texture residency budgets, and eviction before increasing draw distance. Farther geometry without residency control increases VRAM rather than saving it.
7. Validate shadows, snow, wetness, PBR materials, terrain occlusion, LOD transitions, cell streaming, save/load, and worldspace changes before exposing distance controls.

The first publishable target should be repeated distant vegetation and rock LOD, where draw-call reduction is high and gameplay semantics are low-risk. Arbitrary game objects and skinned meshes remain out of scope until the static path has live-game parity and bounded memory use.
