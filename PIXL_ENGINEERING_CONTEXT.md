# PIXL Renderer — Engineering Context

This document gives Codex persistent architectural context for active PIXL Renderer development.

It is intentionally broader and more stable than a single task. Update it only when project architecture or major known-good assumptions change.

## 1. Project Identity

**PIXL Renderer** is a custom high-fidelity renderer/plugin for Skyrim Special Edition.

It began from Skyrim Community Shaders infrastructure but has evolved into a much larger PIXL-specific rendering stack.

The renderer should be treated as a coherent engine, not a collection of unrelated shader mods.

Major engineering priorities are realism and physically plausible rendering, strong visual fidelity, stable integration with Skyrim, scalable quality controls, coherent CPU/GPU architecture, compatibility between modules, sensible performance, robust shader permutation management, and correct public-source provenance.

## 2. Canonical Paths

Source:

`H:\The Elder Scrolls - Skyrim - Special Edition\PBRPipeline\PIXL-Renderer-Engine`

Temporary build alias:

`P:\`

Release build:

`H:\The Elder Scrolls - Skyrim - Special Edition\PBRPipeline\PIXL-Renderer-Engine\build\PIXL-12C`

Live game shaders:

`H:\The Elder Scrolls - Skyrim - Special Edition\Data\Shaders`

Plugin staging:

`C:\Users\PIXL STUDIO PC\Desktop\PIXL RENDERING - SKYRIM - DEVELOPMENT\beta\Current\PIXL-Renderer-v1.0-CURRENT-BETA`

## 3. Historical Upstream

Historical baseline:

**Skyrim Community Shaders v1.8.3**

Repository:

`https://github.com/community-shaders/skyrim-community-shaders`

PIXL may still depend on upstream-derived plugin/core infrastructure, shader interception/compilation infrastructure, engine hooks, CommonLib/SKSE integration, resource plumbing, settings infrastructure, and build infrastructure.

Do not assume an upstream-looking file is obsolete. Likewise, do not assume an upstream feature remains active just because its source still exists. Trace actual dependencies.

## 4. Renderer System Map

The exact filenames may evolve. Treat this as conceptual architecture and verify against current source.

### Lighting

PIXL contains or has recently worked on systems including:

- Natural Lighting
- direct lighting extensions
- Radiance Weave
- world/probe lighting
- sky bounce
- ambient probes
- contact shadows
- distance blending
- light volumes
- projected UV/light interactions
- volumetric/atmospheric light integration

Lighting systems should avoid double-counting energy, conflicting ambient terms, duplicate shadow attenuation, stale temporal history, and inconsistent interior/exterior behavior.

### Global Illumination / Probes

PIXL has multiple GI paths and supporting systems, including screen-space and world/probe-based techniques.

Potential integration points include SSGI, world probes, ambient probes, sky bounce, Radiance Weave, probe updates, temporal accumulation, and denoising/filtering.

When changing GI, inspect all contributors to final indirect light.

### Atmosphere / Sky / Volumetrics

Consider exterior vs interior, weather, time of day, sky visibility, fog/volumetric integration, sun/moon/direct-light coupling, sky bounce, and probe lighting.

### Materials

Recent PIXL shader permutations/logs have included systems such as:

- MATERIAL_FORGE
- RAIN_RESPONSE
- WATER_OPTICS
- VOLUME_OCCLUSION
- WATERBODY
- TERRAIN_DETAIL
- TERRAIN_OCCLUSION
- TISSUE_DIFFUSION
- PIXL_SKIN
- CONTACT_SHADOWS
- DISTANCE_BLEND
- NATURAL_LIGHTING
- RADIANT_GRID
- STRAND_SHADING
- FOLIAGE_DYNAMICS
- GROUND_RESPONSE
- THIN_SURFACE
- MATERIAL_LAYERS
- ATMOSPHERE_PIPELINE
- SKY_VEIL
- TERRAIN_SEAM
- SKY_BOUNCE
- AMBIENT_PROBE
- WORLD_PROBES
- PROJECTED_UV

Do not assume every permutation is always active, but use this as a warning that `Lighting.hlsl` and shared includes are heavily cross-coupled.

### GroundResponse / Deformable Surfaces

A critical known-good checkpoint exists for PIXL GroundResponse.

#### Known-good 13BE checkpoint

13BE fully fixed the deformable snow/mud hull rendering issue.

Known-good behavior includes:

- raised hull has correct textures;
- hull is fully opaque;
- hull correctly occludes player/objects using HS/DS raster depth;
- top face receives correct lighting;
- directional/world-space shadows work;
- buried-player/contact-shadow bleed-through is resolved.

Treat this as a protected baseline.

Do not regress this behavior while changing terrain, lighting, shadows, material systems, spells, snow, or precipitation.

Recent follow-up development has included shouts, spells/fire, dragon fire, snow deformation, and hull shadow quality.

### Weather / Precipitation

Recent PIXL precipitation goals include:

- rain and snow should behave in world space rather than rotating with the camera;
- precipitation should maintain convincing distance coverage;
- snow enhancement should work with PIXL snow deformation and vanilla game behavior;
- density should be appropriate to weather intensity where supported.

Do not reintroduce abandoned precipitation brainstorming unless the user explicitly asks.

### Vegetation / Wind

Known issue area from recent development:

- advanced wind previously made vegetation appear shiny;
- wind behavior did not always match expectations;
- RunGrass shader compilation/integration has required investigation.

When touching vegetation, inspect normals, roughness/specular changes, world-space deformation, shadow/depth passes, velocity/history, and foliage material response.

### Eyes

PIXL has active eye optics/rendering work.

Treat eye rendering as its own high-fidelity material/optics subsystem rather than a generic material tweak.

### Skin / Tissue / Hair

Recent PIXL permutations indicate dedicated PIXL skin, tissue diffusion, strand/hair shading, and thin-surface behavior.

Inspect their lighting integration when changing shared lighting code.

### Water

Water-related systems may include water optics, waterbody handling, lighting/atmosphere integration, and depth/occlusion behavior.

Shared lighting changes must be checked for water-specific paths.

### Director / Photo Mode / DOF

PIXL has active Director/Photo mode and depth-of-field work.

Temporal accumulation/history changes must consider camera transitions, photo/director mode, DOF accumulation, history resets, and exposure/focus changes.

## 5. Important Shared Shader Areas

Files commonly requiring extra care include current equivalents of:

- `Lighting.hlsl`
- `SharedData.hlsli`
- `LightingCommon.hlsli`
- `LightingEval.hlsli`
- `PBRMath.hlsli`
- `PhysicalMaterial.hlsli`
- shadow sampling includes
- contact shadow includes
- probe/update compute shaders
- GI compute shaders
- terrain/ground response shader files
- grass/vegetation shaders

A single change in a shared include can affect hundreds or thousands of permutations.

Prefer narrowly scoped feature guards and test representative permutations.

## 6. Quality Profile Architecture

PIXL has grouped quality settings/presets.

These should progressively control real cost/fidelity characteristics.

Existing visuals are generally a strong medium/high baseline depending on module.

Quality levels should normally be monotonic:

- lower quality: lower cost / reduced fidelity;
- higher quality: higher cost / increased fidelity.

Do not make all quality levels resolve to the same effective values.

Audit the end-to-end path whenever adding or modifying a quality setting.

Known area that previously required work:

- Radiance Weave quality selection appeared to have little/no effect;
- Lighting quality grouping has been an active integration target.

## 7. Build / Shader Validation Expectations

The active build directory is commonly:

`build\PIXL-12C`

The project may use Visual Studio/MSBuild/CMake/CommonLib infrastructure.

If MSBuild is unavailable in ordinary PowerShell, use a Visual Studio Developer PowerShell / x64 Native Tools environment or correctly locate the toolchain.

Do not “fix” source because the shell environment itself is missing MSBuild.

For shader validation:

- treat runtime compiler logs as primary evidence;
- resolve every active compile error;
- beware stale shader cache;
- use clean/cold compilation when validating broad shader changes.

## 8. Debugging Principles

When a compile error occurs:

1. read the exact error;
2. inspect the type/member declaration from the actual dependency version;
3. do not assume an API from a newer/older Community Shaders/CommonLib version exists;
4. repair against the installed/current dependency ABI.

A recent example pattern involved expecting a member on `RE::MenuTopicManager` that did not exist in the installed CommonLib version. The correct response is to inspect the actual header/API, not force code written against a different version.

## 9. Development Philosophy

PIXL development favors physically plausible behavior, strong high-end visuals, coherent systems, runtime scalability, safe integration, and evidence-driven fixes.

Avoid cosmetic rewrites, broad refactors without test value, speculative API changes, shader-register churn, unnecessary feature deletion, blindly copying upstream, and optimizations that visibly degrade the renderer.

## 10. What Counts as Done for a Development Task

A task is complete only when the relevant subset is satisfied:

- requested behavior implemented;
- affected code paths understood;
- build succeeds;
- affected shaders compile;
- no obvious cross-module regression introduced;
- settings/config are wired if needed;
- staging/runtime copy is updated if needed;
- logs inspected where relevant;
- runtime/visual validation performed or explicitly marked pending;
- `PIXL_ACTIVE_STATE.md` updated.

Compilation alone does not prove a visual rendering task is correct.
