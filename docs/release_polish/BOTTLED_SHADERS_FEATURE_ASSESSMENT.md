# Bottled Shaders Feature Assessment

Date: 2026-10-10  
Reference checkout: `H:\Bottled-Shaders-dev\Bottled-Shaders-dev`  
Scope: fur, snow cover, grass optimization; feasibility for PIXL Renderer 1.0.7+

## Summary

PIXL already has substantial implementations adjacent to all three requests. The Bottled checkout does not contain a dedicated fur-shell or snow-cover feature by those names. Its grass optimizer is based on the same Community Shaders lineage as PIXL's existing Foliage Optimizer, so it is a reference for incremental improvements, not a feature PIXL should transplant wholesale.

No runtime or shader code was changed during this assessment. PIXL 1.0.7 is live, and this checkout review does not provide in-game visual, compatibility, or performance validation for a new rendering path.

## Fur

### What Bottled contains

- `features/Hair Specular/Shaders/Hair/Hair.hlsli` implements tangent-based Kajiya-Kay and Marschner lighting for hair strands, including transmission and optional screen-space self-shadowing.
- `src/Features/HairSpecular.*` exposes hair-specific controls and binds a tangent-shift texture.
- This is a hair shading feature, not a fur geometry/shell system. The Kajiya-Kay source comment cites the classic fur rendering paper, but the implementation is applied to strand materials.

### What PIXL already contains

- `pipeline/Strand Shading/Kernels/Hair/Hair.hlsli`, `engine/Modules/StrandShading.*`, and the Lighting shader provide tangent-based hair shading.
- `engine/MaterialForge/PhysicalMaterialRegistry.cpp` classifies likely animal/pelt and fur-trimmed materials with false-positive exclusions.
- `AUTO_FUR` shader permutations in `distribution/Shaders/Lighting.hlsl` add a bounded, screen-space material response for classified legacy fur, with virtual fibre occupancy and fuzz lighting. This does not add geometry or actual fibre-scale parallax.
- Fur descriptor data includes shell-length/density/softness hints, with comments describing shell fur as a future backend.

### Assessment and recommendation

There is no need to port Bottled's hair model into PIXL. PIXL already has a dedicated hair path and a separate fur material response. The meaningful experiment would be to improve fur's existing `AUTO_FUR` treatment, keeping it limited to confidently classified fur and reusing the material descriptor. A geometry-shell or multi-layer approach is higher risk: it could change silhouettes, shadowing, motion vectors, draw cost, and compatibility with Skyrim creature meshes. Start with a material-aware fuzz/normal response or a tightly bounded shell prototype behind an opt-in setting, and validate on several creature and armor asset classes before considering a default.

## Snow cover

### What Bottled contains

The searched `features`, `src/Features`, and `package` shader/source trees contain no dedicated snow-cover or weather-driven snow accumulation feature. Bottled does have ordinary Skyrim snow-related shader support, but no analogous module that PIXL could adapt for dynamic surface coverage.

### What PIXL already contains

- `distribution/Shaders/Lighting.hlsl` consumes vanilla landscape snow masks and computes snow coverage for terrain materials.
- `engine/Modules/GroundResponse.*` and `pipeline/Ground Response` implement material-classified snow/mud ground response, deformation, snow surface shading, and weather-dependent environmental state.
- `engine/Modules/ActorSurfaceEffects.*` accumulates and melts snow on actor-local contact regions; `HairReconstruction` has a compatibility switch for detected hair to participate in that existing path.
- `distribution/Shaders/Common/PIXLAdvancedSnowMaterial.hlsli` centralizes shared snow optical properties.

### Assessment and recommendation

PIXL's existing systems cover terrain snow response and localized actor snow. A broader “snow cover” feature likely means persistent weather-driven accumulation on static objects, vegetation, and/or all exposed surfaces. That is not supplied by this Bottled checkout. A PIXL implementation should extend existing weather/material classification and snow appearance interfaces rather than fork another ground-deformation path. The largest unresolved issue is identifying exposed, snow-receiving material regions robustly across modded assets without coating undersides, interiors, emissive surfaces, or unsuitable objects. Prototype on a narrow receiver class first, with accumulation/melt state and history keyed to stable world/object identity; avoid a full-scene GPU state system until a target asset set and state lifetime are validated.

## Grass optimization

### What Bottled contains

- `src/Features/GrassOptimizations.*` and `features/Grass Optimizations/Shaders/GrassOptimizations/*` replace vanilla grass submission with captured buckets, compute culling, indirect drawing, Hi-Z occlusion, projected-size density reduction, optional mesh LOD, and simplified distant shading.
- The source includes substantial Skyrim-specific hooks and synchronization with grass collision and terrain blending. It is not a shader-only feature.

### What PIXL already contains

- `engine/Modules/FoliageOptimizer/FoliageOptimizer.*`, `GrassBucketStore.*`, `GrassMeshLibrary.*`, and `HiZPyramid.*` implement bucketed capture, frustum/instance culling, indirect draws, projected-size and distance controls, Hi-Z occlusion, mesh LOD, and settings.
- `pipeline/Foliage Optimizer/Kernels/FoliageOptimizer/GrassCullingCS.hlsl` documents Community Shaders 1.9.1 ancestry and contains PIXL-specific adaptations. The CPU/HLSL cbuffer layouts are guarded by `static_assert` in `FoliageOptimizer.h`.
- PIXL's culling shader currently binds its LOD counters through one shared UAV (`u7`) to remain within D3D11's UAV limit; Bottled's inspected version uses separate counter UAVs through `u8`. This is not a safe copy-over change and reflects different binding/ABI decisions.

### Assessment and recommendation

Do not port Bottled's optimizer over PIXL's current implementation. The architecture is already present, and PIXL's shader interface differs. Review the Bottled version for narrowly scoped fixes or techniques that PIXL lacks, compare the complete CPU/GPU binding and runtime hook behavior, and adapt only a demonstrable improvement. Potential candidates for a later comparison include its complex-mesh detection, LOD policy, or bug fixes made after the shared ancestry point. Any change must preserve PIXL's D3D11 resource limits, current user settings, collision behavior, and temporal stability. No performance delta can be claimed without an in-game GPU/CPU capture.

## Licensing and provenance

The Bottled checkout's `COPYING` is GPL-3.0. PIXL's existing optimizer identifies Community Shaders 1.9.1 as an ancestor and the repository has an `EXCEPTIONS.md` for the modding exception. Because the feature source has shared lineage and may include later upstream contributions, any direct code reuse or substantial translation should be checked against the relevant file headers, contributor notices, and PIXL's distribution obligations. This assessment recommends comparative review and independent implementation of any new behavior; it does not clear third-party code for copying.

## Suggested experiment order

1. Capture a PIXL baseline for representative grass-heavy scenes and record draw count, CPU submission, GPU timing, and image behavior.
2. Compare the current PIXL and Bottled grass implementations by change history or focused diffs, then select one isolated candidate. Keep existing defaults and shader ABI stable until tested.
3. Exercise PIXL's current fur classification on representative creatures and fur armor. If a real visual gap remains, prototype a separately controllable improvement to `AUTO_FUR` rather than duplicating Strand Shading.
4. Define a small snow-cover target set and success criteria (surface classification, accumulation, melt, persistence, motion, and cost) before implementing a separate opt-in prototype.
5. Test any prototype in game across camera motion, indoor/outdoor transitions, weather changes, modded assets, and temporal upscaling. Keep it out of release defaults until those checks pass.

## Files examined

### Bottled-Shaders

- `src/Features/GrassOptimizations.cpp`, `src/Features/GrassOptimizations.h`
- `src/Features/GrassOptimizations/HiZPyramid.cpp`, `GrassBucketStore.cpp`, `GrassMeshLibrary.cpp`
- `features/Grass Optimizations/Shaders/GrassOptimizations/GrassCullingCS.hlsl`, `GrassHiZCS.hlsl`, `DetectComplexCS.hlsl`
- `src/Features/HairSpecular.cpp`, `src/Features/HairSpecular.h`
- `features/Hair Specular/Shaders/Hair/Hair.hlsli`
- `COPYING`

## Follow-up: replacement request and clarified snow scope

The owner clarified that PIXL's fur shell shading currently does not work, requested a backed-up straight grass replacement experiment, and meant seasonal procedural snow coverage on objects.

### Fur diagnosis

The current implementation does not emit fur shells. `PhysicalMaterial::RasterFlag::FurShell` in `engine/MaterialForge/PhysicalMaterial.h` is explicitly reserved until a dedicated geometry path is installed and live-tested. The `AUTO_FUR` shader permutation adds virtual fuzz lighting to classified legacy fur; it cannot produce shell silhouette, layered geometry, or true strand parallax. This is why a lighting-only adjustment cannot fix the missing shell effect. A true shell needs a separate geometry/draw prototype with defined strand direction, spacing, shadowing, motion vectors, and scale handling. Keep that prototype opt-in. The existing fuzz response can be improved independently, but should not be presented as shell fur.

### Grass replacement decision

A direct overwrite of the active PIXL grass files with Bottled's files is incompatible with PIXL's current contracts:

- Bottled's `GrassCullingCS.hlsl` binds separate middle/far counters at `u5` and `u8`, requiring nine UAV slots. PIXL deliberately shares the counter buffer at `u7` to stay within its DX11 UAV interface; replacing only the shader would mismatch the current CPU bindings.
- Bottled's `RunGrass.hlsl` does not include PIXL's `FoliageDynamics` wind integration and has different compile-time/resource contracts. Replacing it would remove PIXL behavior.
- The CPU features also differ in hook setup, module integration, and runtime validation. PIXL currently quarantines an unvalidated SE hook profile; the Bottled implementation cannot be dropped in without reconciling those safeguards.

No backup or active replacement was made: the files were inspected before mutation, and the literal replacement would break the active shader ABI or remove PIXL behavior. A valid experiment should use an isolated branch, adapt Bottled's candidate changes to PIXL's eight-UAV layout, retain the wind path, and validate the runtime hooks before testing in game. There is no verified performance or visual result from this source comparison.

### Seasonal procedural object snow

The clarified request is weather/season-driven snow accumulation on exposed object surfaces, not another terrain deformation feature. Bottled provides no module to adapt. PIXL should build this as a separate opt-in receiver/material system that reuses `SeasonIntegration`, `WeatherManager`, and the existing snow material response, while keeping persistent object accumulation separate from Ground Response. A narrow first target set is needed to avoid coating undersides, interiors, emissive surfaces, or materials that should not receive snow. Accumulation, melt, save/session persistence, object identity, and temporal stability need explicit validation before broad coverage.

## Fur permutation eligibility follow-up

On 2026-10-10, `engine/MaterialForge.cpp` was changed so confidently classified, skinned legacy fur can select `AUTO_FUR` for `Envmap`, `Glowmap`, `Parallax`, `ParallaxOcc`, and `MultilayerParallax` permutations as well as `None` and `TreeAnim`. Specialized hair, face, landscape, and LOD techniques remain excluded. Previously fur classification was computed but only `None` and `TreeAnim` could receive the fur shader define, which left common legacy fur material variants without the fur lighting response.

Validation: `cmake --build build/PIXL-12C --config Release --target PIXLRenderer` succeeded and produced `build/PIXL-12C/Release/PIXLRenderer.dll`. In-game verification is still needed to confirm the affected material permutations and visual result. This change enables the existing virtual fuzz response; it does not implement layered geometry shells or shell silhouette expansion.

### Procedural extruding fur prototype

The owner clarified the visual target as dense, short fur visible on animal feet, rugs, and fur-trimmed equipment, without authored `_shell.dds` assets or pre-layered meshes. Bottled's inspected Hair Specular code does not provide that geometry path, so PIXL's prototype is independent.

The PIXL prototype adds `distribution/Shaders/FurShellGS.hlsl`, which receives the existing PIXL lighting vertex output, preserves the base mesh surface, and emits eight normal-extruded layers. `AUTO_FUR` permutations carry a shell-layer semantic and apply deterministic object-space stochastic coverage to the added layers. This creates a procedural silhouette and layered surface without requiring new meshes or texture maps. Shell length and density are currently fixed prototype values (0.70 Skyrim units and eight layers), not yet calibrated across mod assets.

`engine/Modules/FurShells.*` compiles the GS and hooks immediate-context draw calls. The BSLighting setup hook arms only confidently classified fur draws; the draw wrapper binds the GS and VS frame buffer for that draw, then restores the prior GS and GS b12 constant buffer. `MaterialForge` exposes a serialized `Procedural Fur Shells` toggle, enabled by default, for quick rollback. Classification now includes compatible static meshes with tangent-space normals as well as skinned meshes, allowing pelt rugs and static armor pieces while excluding model-space-normal permutations with no normal basis. The existing exclusions and confidence threshold remain in force.

Validation: Release build succeeded at `build/PIXL-12C/Release/PIXLRenderer.dll`. Windows `fxc` compiled the standalone GS, AUTO_FUR skinned VS, and AUTO_FUR skinned PS successfully. The first PS compile attempt lacked PIXL's pipeline include directories; rerunning with the full kernel include set succeeded. `git diff --check` passed. This is a render-path prototype and still needs in-game evaluation for silhouette quality, temporal stability, depth/shadow interaction, draw interception compatibility, and representative creature, rug, and outfit assets. No performance measurements or image-quality claim is made from source/build checks alone.

### PIXL Renderer

- `engine/Modules/FoliageOptimizer/FoliageOptimizer.cpp`, `FoliageOptimizer.h`, `GrassBucketStore.cpp`, `GrassMeshLibrary.cpp`, `HiZPyramid.cpp`
- `pipeline/Foliage Optimizer/Kernels/FoliageOptimizer/GrassCullingCS.hlsl`, `GrassHiZCS.hlsl`
- `engine/Modules/StrandShading.cpp`, `StrandShading.h`
- `pipeline/Strand Shading/Kernels/Hair/Hair.hlsli`
- `engine/MaterialForge/PhysicalMaterialRegistry.cpp`
- `engine/Modules/GroundResponse.cpp`, `engine/Modules/ActorSurfaceEffects.cpp`, `engine/Modules/HairReconstruction.cpp`
- `distribution/Shaders/Lighting.hlsl`, `distribution/Shaders/Common/PIXLAdvancedSnowMaterial.hlsli`
- `EXCEPTIONS.md`

