# PIXL Renderer v1.0 architecture

PIXL Renderer is shipped as one renderer, not a collection of separately installed shader add-ons. The inherited feature short names and `Shaders/Features/*.ini` files remain compatibility adapters: Skyrim's hook discovery, saved settings, shader defines, register bindings, and material providers depend on those stable identities.

## Unified source map

- `engine/Renderer/` owns cross-system policy such as Low/Medium/High/Ultra quality contracts.
- `engine/Modules/` contains runtime hook adapters and module-specific resource lifetimes.
- `engine/Menu/` contains the Essentials, Advanced, and Developer presentation layers.
- `distribution/Shaders/Common/` is the shared physical shading ABI and BRDF library.
- `distribution/Shaders/` contains Skyrim entry-point overrides whose names and signatures must remain unchanged.
- `pipeline/*/Kernels/` is the authoring location for integrated subsystem shaders. Packaging merges every module into one `Data/Shaders` tree.
- `distribution/SKSE/Plugins/PIXLRenderer/` owns settings, the tested preset, theme, and translations.

## Player quality contract

| Group | Low | Medium | High (default) | Ultra | Apply path |
|---|---|---|---|---|---|
| Lighting | Quarter GI, 3x6 horizon tracing, 20 reflection steps | Half GI, 4x8, 28 steps | Half GI, 5x10, 36 steps, two contact samples | Full GI, 6x12, 48 steps, maximum contact sampling | Recompile HybridGI/reflection permutations, invalidate contact raymarch, reset history |
| Materials | Physical BRDF with conservative specular AA | Adds parallax and material shadows | Adds height blending and authored GGX compensation | Maximum stable specular filtering | Shared feature constants; restart only when the underlying feature was boot-disabled |
| Atmosphere | Low volumetric target, reduced cloud detail | Medium targets and cloud body | High targets and full self-shadowing | High targets plus maximum procedural cloud detail | Recreate active volumetric targets; cloud constants are live |
| Water | Base renderer path | Enhanced SSR and caustics at bounded range | Authored reflection reach and dispersion | Extended ray reach and caustic dispersion | Reset reflection history; a boot-disabled Water Optics hook requires restart |
| Terrain & Vegetation | Enhanced leaf response without costly interaction | Wind, snow deformation | Adds mud deformation and authored specular response | Maximum stable flutter/specular detail | Shared constants are live; first installation requires shader-cache build |
| Characters | Physical skin/hair base layer | Detail skin, SSS, hair self-shadow | 16-tap Burley and Marschner hair | 21-tap Burley maximum | Refresh SSS kernels; feature boot changes require restart |
| Camera | Physical exposure without enhanced DOF | Adds depth-aware DOF | Authored local exposure and edge protection | Maximum local exposure and bokeh edge protection | Camera constants are live; display-mode changes may require restart |

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

Essentials mode reserves a comparison-preview area. Future Low/Medium/High/Ultra images should be authored below `distribution/Interface/PIXLRenderer/Previews/<group>/` and selected by the persisted group quality. Preview assets are illustrative only; the renderer policy in `engine/Renderer/QualityProfiles.cpp` remains authoritative.
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
