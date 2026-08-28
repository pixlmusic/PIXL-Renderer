# AGENTS.md — PIXL Renderer

## Project

**PIXL Renderer**

PIXL Renderer is a custom real-time rendering enhancement framework for **The Elder Scrolls V: Skyrim Special Edition**, built around SKSE, C++, HLSL, and DirectX 11.

It is derived historically from Community Shaders architecture but has substantially expanded into its own rendering system with custom modules for lighting, global illumination, materials, atmosphere, vegetation, water, weather, deformation, post-processing, character rendering, parallax effects, and other visual systems.

This repository is the active PIXL Renderer engine source.

The project is currently entering its:

# FINAL PRE-RELEASE POLISH PHASE

After this work, development should move primarily toward smaller incremental modular updates rather than large whole-renderer changes.

Treat this repository as a real public-facing graphics product approaching release.

---

# Primary Repository

Primary source repository:

`H:\The Elder Scrolls - Skyrim - Special Edition\PBRPipeline\PIXL-Renderer-Engine`

The repository may sometimes be mirrored or mounted to another drive for build purposes.

Historically a temporary `P:\` source mapping has been used.

Do not assume `P:\` is authoritative.

The canonical development source is the repository above unless the current environment/build files clearly indicate otherwise.

---

# Shader Source

The most current Skyrim shader tree may exist at:

`H:\The Elder Scrolls - Skyrim - Special Edition\Data\Shaders`

The repository may also contain shader sources under internal pipeline directories.

Before changing shaders:

1. determine which shader copy is authoritative,
2. determine which copy is compiled or loaded,
3. trace deployment,
4. avoid modifying a stale duplicate.

Never assume similarly named HLSL files are equivalent.

---

# Release Output

A known release build location has historically been:

`H:\The Elder Scrolls - Skyrim - Special Edition\PBRPipeline\PIXL-Renderer-Engine\build\PIXL-12C`

Expected primary binary:

`PIXLRenderer.dll`

Verify the active build output rather than relying blindly on this historic path.

---

# Staging

A release/beta staging tree may exist under:

`C:\Users\PIXL STUDIO PC\Desktop\PIXL RENDERING - SKYRIM - DEVELOPMENT\beta\Current\PIXL-Renderer-v1.0-CURRENT-BETA`

Treat this as staging, not necessarily authoritative source.

Do not modify staged output instead of source unless specifically performing packaging/deployment work.

---

# Technology

PIXL Renderer primarily uses:

- C++
- HLSL
- DirectX 11
- SKSE
- CommonLibSSE / Skyrim runtime integration
- CMake
- Visual Studio 2022
- MSBuild
- ImGui or equivalent runtime configuration/UI systems
- Skyrim render hooks
- custom render passes
- compute shaders
- pixel shaders
- vertex shaders
- shared HLSL includes

The target environment remains:

# SKYRIM SPECIAL EDITION + DIRECTX 11

Do not silently convert existing rendering systems into DX12-only implementations.

Optional experimental sidecars or wrappers must remain isolated from the stable DX11 renderer unless explicitly requested.

---

# Agent Behaviour

When operating in this repository:

## DO

- read this file first,
- inspect current repository state,
- inspect build configuration,
- understand a subsystem before modifying it,
- trace runtime usage,
- trace shader bindings,
- validate CPU ↔ GPU interfaces,
- build after meaningful changes,
- prefer safe incremental improvements,
- preserve current functionality,
- improve visual fidelity and performance together,
- document decisions,
- leave the repository cleaner than you found it.

## DO NOT

- blindly rewrite working systems,
- remove code solely because its purpose is unclear,
- assume unused means dead,
- make speculative architecture changes during release polish,
- change public behaviour without documenting it,
- fabricate benchmarks,
- fabricate successful builds,
- hide failures,
- modify third-party code for style alone,
- introduce unnecessary dependencies,
- introduce internet/network requirements,
- introduce telemetry,
- introduce unrelated system access,
- add invasive hooks unrelated to PIXL Renderer.

---

# Core Goal

PIXL Renderer prioritizes:

1. **Realism**
2. **Visual fidelity**
3. **Temporal stability**
4. **Performance**
5. **Compatibility**
6. **User control**
7. **Maintainability**

A rendering change should ideally improve more than one category without substantially regressing another.

The preferred optimization is:

> Same or better visual quality for less GPU/CPU work.

The preferred visual change is:

> More realistic and stable without destroying the existing PIXL look.

---

# Project Philosophy

PIXL Renderer is not intended to make Skyrim look generically cinematic.

It should improve the physical credibility of the existing world.

Prefer improvements based on:

- lighting behaviour,
- material response,
- optical behaviour,
- atmospheric behaviour,
- geometric cues,
- environmental interaction,
- motion,
- spatial consistency,
- physically motivated approximation.

Avoid effects that merely increase:

- bloom,
- contrast,
- saturation,
- sharpness,
- noise,
- distortion,

without improving realism.

---

# Current Development Phase

This repository is undergoing a comprehensive unattended final pre-release review.

The active autonomous task must:

- inspect every active build/runtime file,
- improve safe opportunities,
- polish visual systems,
- improve performance,
- improve UI and accessibility,
- audit security,
- remove genuinely unused code,
- preserve functionality,
- build and validate,
- generate reports.

No active build file should be skipped.

A file may be inspected and intentionally left unchanged.

That is acceptable.

A file may not be silently ignored.

---

# Mandatory File Accounting

Maintain:

`docs/release_polish/FILE_REVIEW_MATRIX.md`

Every build/runtime-relevant file must appear in this matrix.

Required status information should include:

- path,
- subsystem,
- active/inactive,
- reviewed,
- modified,
- security reviewed,
- fidelity reviewed,
- performance reviewed,
- future suggestions written.

The final state must contain:

**zero active files marked unreviewed.**

---

# Per-File Review Requirement

For every active source/runtime file, document:

- purpose,
- how it participates in the renderer,
- dependencies,
- current implementation quality,
- visual-fidelity opportunities,
- performance opportunities,
- correctness/stability findings,
- security findings,
- maintainability findings,
- changes implemented,
- changes rejected,
- future improvements,
- validation.

This requirement intentionally exists to prevent broad subsystem reviews from missing individual files.

---

# Active PIXL Renderer Areas

PIXL contains or may contain rendering systems covering areas such as:

- Hybrid GI
- SkyBounce
- World Probes
- Water Optics
- Flowmaps
- Atmosphere
- Volumetrics
- Rain
- Snow precipitation
- Snow deformation
- Mud deformation
- Ground Response
- Vegetation
- Vegetation Wind
- Skin Optics
- Eye rendering
- Thin Surfaces
- WindowLife
- Fake/parallax interiors
- PBR
- Parallax / POM
- Camera systems
- Exposure
- Depth of Field
- post-processing
- RCAS/sharpening
- weather response
- lighting
- shadows
- screen-space effects
- material response
- quality settings
- configuration UI
- debugging tools

Do not assume this list is exhaustive.

Discover the actual active modules from source and build files.

---

# Known Priority Areas

The following systems are already known to deserve particular attention during final polish.

## Water

Water requires higher-fidelity wave behaviour.

Focus on realistic improvements such as:

- multi-scale waves,
- directional variation,
- better normal composition,
- flow integration,
- micro-ripples,
- crest response,
- shoreline attenuation,
- turbulence,
- Fresnel,
- absorption,
- refraction,
- reflection roughness.

Avoid repetitive sine-wave motion.

Avoid synchronized wave patterns.

Avoid large performance regressions.

---

# Vegetation Wind

Vegetation Wind requires final refinement.

Wind must appear spatially coherent and remain world-space rather than behaving as camera-relative noise.

Prefer hierarchical motion:

### Low frequency
Whole plant / trunk movement.

### Medium frequency
Branch movement.

### High frequency
Leaf/needle flutter.

Pay attention to:

- attachment points,
- stiffness,
- gusts,
- per-object phase variation,
- weather coupling,
- distance,
- temporal stability,
- specular flickering.

Do not make trees behave like flexible grass.

Do not make all vegetation move with identical phase.

---

# Snow Deformation

Snow deformation is already considered a successful subsystem.

Preserve it.

Polish rather than redesign.

Areas worth reviewing include:

- compression profile,
- contact falloff,
- imprint edges,
- accumulated deformation,
- normal reconstruction,
- distance fade,
- compressed snow material appearance,
- surface thickness,
- scattering response,
- temporal persistence.

Avoid destabilizing a system that already works well.

---

# Ground Response

Ground Response should behave according to material.

Snow and mud should not use identical physical assumptions.

Review:

- surface hardness,
- moisture,
- compression,
- recovery,
- accumulation,
- drainage,
- wetness,
- depth.

Small vegetation, characters, and unsuitable geometry may require exclusions depending on current implementation.

Preserve existing correct exclusions.

---

# Lighting

Lighting is always eligible for mathematical review.

Check:

- diffuse response,
- Fresnel,
- GGX,
- Smith masking,
- roughness,
- energy conservation,
- grazing angles,
- specular/diffuse balance,
- light attenuation,
- indirect lighting,
- environment lighting,
- normal handling.

Do not introduce textbook PBR changes without considering Skyrim art and material assumptions.

---

# PBR

Review PBR and material calculations carefully.

Check:

- linear vs gamma operations,
- albedo,
- metallic,
- roughness,
- normal maps,
- tangent space,
- wetness,
- snow,
- vegetation,
- skin,
- thin surfaces,
- water-adjacent materials,
- window materials.

Prefer physically credible approximations appropriate for real-time DX11.

---

# Parallax / POM

PIXL makes extensive use of parallax techniques.

Review:

- view-angle stability,
- adaptive samples,
- ray marching,
- binary refinement,
- distance fading,
- UV safety,
- derivative/mip behaviour,
- shimmer,
- self-shadowing where appropriate.

Do not use excessive sample counts when improved math produces comparable results.

---

# WindowLife / Fake Interiors

WindowLife may contain:

- silhouettes,
- blur/refraction,
- parallax interiors,
- room textures,
- glass interaction.

Pay careful attention to:

- SRV register allocation,
- cbuffer/register conflicts,
- shader ABI,
- perspective,
- UV projection,
- view angle,
- layer ordering,
- distance fallback.

Historic development has involved high-numbered texture slots.

Do not casually move shader resources without checking the entire binding pipeline.

---

# Hybrid GI

Hybrid GI is a major PIXL Renderer subsystem.

Review:

- sampling,
- tracing,
- temporal reuse,
- screen-space confidence,
- probe interaction,
- light leaking,
- history rejection,
- material awareness,
- resolution,
- update cost.

Large architectural replacements should normally be proposed for future work rather than implemented during release polish unless clearly safe.

---

# SkyBounce

SkyBounce is a local sky/ambient irradiance system.

Historically it has used a large uniform probe volume.

Known improvement directions may include:

- cascaded/toroidal probe volumes,
- prioritized updates,
- visibility information,
- relocation/classification,
- improved directional radiance representation,
- better temporal amortization.

These are substantial architectural changes.

During pre-release polish, do not replace the entire current working implementation unless validation is strong.

Document high-risk architecture ideas for future work.

---

# World Probes

Review:

- probe placement,
- interpolation,
- visibility,
- update scheduling,
- memory,
- radiance storage,
- stale-data handling,
- interior/exterior behaviour.

Avoid excessive GPU memory for negligible visual gain.

---

# Flowmap

Flowmap systems may influence water movement.

Review:

- tile seams,
- vector reconstruction,
- mip generation,
- velocity representation,
- streaming,
- worldspace awareness,
- resource upload.

Directional flow must remain coherent across cell/tile boundaries.

---

# Camera Systems

PIXL camera/post effects may include:

- exposure,
- local exposure,
- depth of field,
- stormglass/lens weather response,
- dialogue focus,
- photo/cinematic systems.

These must be evaluated for:

- physical plausibility,
- temporal stability,
- depth correctness,
- subject stability,
- gameplay usability,
- performance.

Do not make normal gameplay look permanently cinematic.

---

# RCAS / Sharpening

Sharpening must not amplify:

- shimmer,
- disocclusion,
- specular noise,
- foliage flicker,
- temporal instability.

Where current data allows, consider confidence-based or material-aware control.

Preserve upscaler compatibility.

---

# Atmospheric Rendering

Review atmosphere and volumetrics for:

- fog scattering,
- height behaviour,
- extinction,
- weather response,
- temporal reprojection,
- noise,
- checkerboarding,
- upsampling,
- interior transitions,
- sky interaction.

Avoid excessive white fog or flattened vertical depth.

---

# Rain / Snow Precipitation

Precipitation should remain world-space.

It must not rotate incorrectly with the camera.

Review:

- particle orientation,
- gusts,
- visibility distance,
- accumulation interaction,
- weather strength,
- roof interaction,
- splash response,
- snow deformation interaction.

Roof runoff and similar systems may be performance-sensitive.

Do not dramatically increase coverage or tracing distance without profiling.

---

# Character Rendering

Character-focused features may include:

- Skin Optics
- Eye rendering
- thin-surface transmission
- dialogue enhancement

Preserve compatibility with Skyrim character assets.

Avoid overly smooth or synthetic appearance.

---

# User Interface

The PIXL Renderer configuration interface is part of the shipped product.

Treat UI bugs as release bugs.

Every exposed control must be traced:

UI
→ setting
→ runtime state
→ constant/resource
→ shader/module.

Check every:

- slider,
- checkbox,
- combo,
- quality option,
- debug toggle,
- module enable switch.

If a control does nothing:

- fix it,
- connect it,
- or remove it if genuinely obsolete.

---

# UI Philosophy

The normal interface should be understandable by an advanced Skyrim user without requiring shader-development knowledge.

Use friendly names.

Examples:

Prefer:

`Wave Detail`

over:

`FFT Octave Weight B`

Prefer:

`Indirect Lighting Quality`

over:

`GI Dispatch Multiplier`

Engineering controls may remain under:

`Advanced`

or:

`Debug`.

---

# Tooltips

Where practical, tooltips should explain:

1. what the setting affects,
2. visible result,
3. performance impact.

Example:

`Wave Detail — Adds smaller-scale water motion. Higher values improve close-range detail but increase shader cost.`

---

# Defaults

Default values should be release-quality.

Prioritize:

- stability,
- realistic appearance,
- reasonable performance.

Do not ship with experimental/debug values.

---

# Quality Presets

If quality presets exist, verify they actually influence underlying systems.

Potential tiers may include:

- Performance
- Balanced
- High
- Ultra

Do not create meaningless presets where multiple levels have the same effective configuration.

---

# Shader Rules

For shader work:

## Always inspect

- include dependencies,
- resource bindings,
- cbuffer layout,
- sampler binding,
- CPU-side constant structure,
- caller/pass setup.

Do not treat HLSL files as isolated.

---

# Shader Numerical Robustness

Check for:

- `normalize(0)`
- division by zero
- `sqrt(negative)`
- `pow()` with unsafe inputs
- invalid logarithms
- INF
- NaN
- overflow
- underflow
- bad UV assumptions
- invalid history values.

Use mathematical guards where useful.

Do not aggressively clamp valid HDR lighting.

---

# Shader Optimization

Prefer algorithmic savings before degrading image quality.

Useful areas:

- fewer redundant samples,
- adaptive samples,
- early-outs,
- branch simplification,
- cached calculations,
- shared math,
- reduced duplicate normalization,
- lower precision when proven safe,
- lower-frequency updates,
- reduced-resolution intermediate passes with high-quality reconstruction.

Be mindful of:

- register pressure,
- occupancy,
- texture bandwidth,
- divergence.

---

# CPU ↔ GPU ABI

This is a critical project rule.

Whenever changing any shared CPU/shader structure verify:

- size,
- alignment,
- padding,
- field order,
- register binding.

Never casually reorder fields in constant structures.

Use `static_assert` or equivalent verification where appropriate.

---

# Resource Binding

PIXL is a large renderer with many shader resources.

Before assigning new:

- `t#`
- `s#`
- `u#`
- `b#`

registers, search the entire active shader/runtime pipeline.

Avoid collisions.

Document unusual reserved slots.

---

# Performance Philosophy

PIXL should aim for high-end visuals without wasting GPU work.

Look for:

- full-screen passes that can be reduced,
- giant dispatches,
- redundant per-frame work,
- resources updated when unchanged,
- excessive history storage,
- oversized volume textures,
- redundant clears,
- unnecessary CPU allocations.

Prefer:

- dirty updates,
- camera-relative regions,
- update scheduling,
- visibility scheduling,
- temporal amortization,
- clipmaps,
- indirect dispatch,
- cached data,

when appropriate and low risk.

---

# Performance Claims

Never invent timing numbers.

If profiling is unavailable, say:

`Expected improvement`

rather than:

`0.4 ms improvement`.

Measured claims require actual measurement.

---

# Temporal Stability

The renderer is commonly used with temporal upscalers.

All effects should be evaluated for:

- DLSS interaction,
- TAA interaction,
- frame generation interaction where applicable,
- shimmer,
- ghosting,
- history errors,
- disocclusion,
- FOV changes,
- teleport,
- fast camera movement,
- cell transitions.

A visually strong still frame that flickers in motion is not a successful rendering improvement.

---

# Skyrim Runtime Edge Cases

Consider:

- first person,
- third person,
- interiors,
- exteriors,
- caves,
- cities,
- wilderness,
- snow regions,
- rain,
- clear weather,
- underwater,
- nighttime,
- daylight,
- dawn/dusk,
- menus,
- loading,
- fast travel,
- worldspace changes.

Do not assume a system that works in exterior Tamriel works everywhere.

---

# Build Discipline

Use the repository's real build process.

First inspect:

- `CMakeLists.txt`
- CMake presets if present
- Visual Studio configuration
- output paths
- generated files.

Do not invent a new build system.

---

# Visual Studio / MSBuild

The expected Windows build environment commonly uses Visual Studio 2022.

If `MSBuild` is not available in ordinary `PATH`, locate Visual Studio's developer tools or use the appropriate developer environment.

Do not incorrectly conclude the source cannot build merely because the shell lacks MSBuild in PATH.

---

# Build Cadence

Build after:

- structural changes,
- CPU/shader ABI changes,
- module changes,
- major shader groups,
- UI/configuration changes.

Do not modify dozens of unrelated modules before attempting a compile.

---

# Compile Errors

When a build fails:

1. identify the first relevant error,
2. determine whether it comes from your change,
3. fix it,
4. rebuild.

Do not paper over compiler errors with broad disabling macros.

Do not disable modules merely to obtain a green build unless they were already obsolete and proven unused.

---

# Existing Warnings

Distinguish:

- pre-existing warnings,
- newly introduced warnings.

Do not claim to have introduced zero warnings without comparing the baseline where practical.

---

# Dead Code Removal

Dead-code cleanup is encouraged for release but must be conservative.

A candidate may only be removed after checking:

- direct references,
- indirect references,
- build files,
- macros,
- factory/registration systems,
- virtual dispatch,
- shader selection,
- string/reflection lookup,
- runtime config,
- plugin hooks.

If uncertain:

# KEEP IT

and document it as a cleanup candidate.

---

# Community Shaders Legacy

PIXL historically derives from Community Shaders.

Some legacy systems may no longer be relevant.

Remove old Community Shaders code only when:

- PIXL no longer calls it,
- the build does not require it,
- shaders do not depend on it,
- compatibility does not depend on it.

Do not remove shared foundational infrastructure merely because its naming predates PIXL.

---

# Security

Public release requires a defensive security audit.

Search source and packaging for:

- networking,
- telemetry,
- downloaders,
- arbitrary process execution,
- command execution,
- PowerShell invocation,
- shell invocation,
- dynamic DLL loading,
- unrelated filesystem access,
- persistence mechanisms,
- registry writes,
- credentials,
- tokens,
- secrets,
- private keys,
- personal information,
- hard-coded development paths,
- suspicious binary blobs.

SKSE/game hooking is expected.

Do not classify normal renderer hooks as malicious simply because they interact with game memory.

Understand intent first.

---

# Security Change Rule

Do not introduce:

- telemetry,
- remote communication,
- automatic update downloaders,
- arbitrary command execution,
- external script execution,

during this release polish unless explicitly required by project design.

PIXL Renderer should function locally.

---

# C++ Safety

Audit relevant code for:

- null access,
- uninitialized state,
- use-after-free,
- stale references,
- bad casts,
- integer overflow,
- invalid array indexes,
- resource lifetime bugs,
- race conditions,
- deadlocks,
- exception safety,
- thread-safety.

Prefer low-risk fixes.

---

# Third-Party Code

Identify third-party directories and files.

Avoid rewriting them.

Preserve:

- licenses,
- copyrights,
- notices.

Only modify external code for:

- required compatibility,
- demonstrated bugs,
- serious security issues.

Document such modifications.

---

# Local Paths

Do not introduce personal development machine paths into permanent source.

Where an existing hard-coded local path is not required:

- replace it with configurable/relative behaviour,
- or remove it if obsolete.

Do not break build logic where a path is intentionally supplied externally.

---

# Logging

Keep useful release logging.

Recommended examples:

`[PIXL] Initializing WaterOptics`

`[PIXL] HybridGI shader compilation failed`

`[PIXL] WindowLife disabled: missing resource`

Avoid:

- per-frame spam,
- massive state dumps,
- temporary diagnostics in normal mode.

Debug logging may remain behind a clear debug setting.

---

# Debugging

Useful debug views may remain.

They must:

- default OFF,
- not silently consume significant resources when OFF,
- be clearly separated from normal graphics controls.

Remove obsolete debug paths.

---

# Error Handling

Optional PIXL features should fail gracefully where practical.

Prefer:

affected module fails
→ module disables
→ clear log entry
→ renderer continues

rather than:

optional module failure
→ entire renderer crashes.

---

# Compatibility

Avoid breaking:

- existing PIXL configurations,
- external mod compatibility,
- SKSE expectations,
- shader resource conventions,
- saved settings,
- presets.

If a breaking change is unavoidable, document:

- what changed,
- why,
- migration requirements.

---

# Seasonal / Environmental Compatibility

PIXL Renderer may coexist with seasonal Skyrim systems such as:

- Seasons of Skyrim SKSE
- Turn of the Seasons
- seasonal terrain/landscape swaps.

Ground Response and snow/deformation logic should not assume a permanently fixed ground material where runtime texture/material state can change.

Prefer runtime-derived material/environment signals over hard-coded assumptions.

Do not hard-depend on optional seasonal mods unless implementing isolated compatibility integration.

---

# Future-Proofing

Prefer interfaces and detection methods that survive:

- mod updates,
- altered load orders,
- new texture packs,
- additional worldspaces,
- alternate weather mods.

Avoid hardcoding exact asset names when more general runtime classification is available.

---

# Documentation Output

The release polish task should maintain:

`docs/release_polish/`

Expected reports include:

- `ACTIVE_BUILD_INVENTORY.md`
- `FILE_REVIEW_MATRIX.md`
- `VISUAL_FIDELITY_REPORT.md`
- `PERFORMANCE_REPORT.md`
- `UI_ACCESSIBILITY_REPORT.md`
- `SECURITY_AUDIT.md`
- `DEAD_CODE_AUDIT.md`
- `REMOVED_CODE.md`
- `FUTURE_RENDERING_ROADMAP.md`
- `RELEASE_SCORECARD.md`

Additional subsystem reports are encouraged.

---

# Module Reports

Each major module should receive a report containing:

- architecture,
- files,
- current behaviour,
- changes,
- fidelity improvements,
- performance improvements,
- stability improvements,
- UI changes,
- remaining issues,
- future ideas.

Each module should end with:

## 5 Future Visual Improvements

## 5 Future Performance Improvements

## 5 Future Feature / Research Ideas

These are primarily for future modular development.

Do not implement high-risk ideas simply because they appear promising.

---

# Git Discipline

Before changing anything, inspect:

`git status`

Do not overwrite unrelated existing user changes.

Do not revert files that were modified before your task merely because they differ from HEAD.

Treat existing uncommitted work as potentially intentional.

---

# Patch Discipline

Prefer small coherent changes.

Avoid huge mechanical rewrites unrelated to the requested task.

Keep functional changes separable enough that regressions can be diagnosed.

---

# Comments

Use comments for:

- non-obvious rendering math,
- architectural assumptions,
- unusual engine constraints,
- resource binding contracts,
- intentional approximations.

Do not fill obvious code with explanatory noise.

---

# Naming

Follow the existing PIXL code style unless a local inconsistency causes genuine confusion.

Do not perform repository-wide renames during release polish merely for aesthetic consistency.

---

# Refactoring

Refactor when it:

- fixes a real issue,
- reduces duplication,
- improves safety,
- enables a rendering/performance improvement,
- materially improves maintainability.

Avoid architectural refactoring with no release benefit.

---

# Visual Change Classification

Before implementing rendering changes classify them:

## A — Release Safe

- obvious correctness fix,
- stable visual improvement,
- negligible regression risk.

Implement.

## B — Controlled Improvement

- good expected benefit,
- requires validation.

Implement carefully.

## C — Experimental

- substantial architectural risk,
- uncertain compatibility,
- difficult to validate.

Document for future work.

---

# Regression Rule

A change is not automatically successful because it is more mathematically sophisticated.

Reject or revise changes that introduce:

- shimmer,
- flicker,
- ghosting,
- light leaking,
- unstable silhouettes,
- excessive GPU cost,
- mod incompatibility,
- broken edge cases.

---

# Release Bar

At the end of the pre-release pass PIXL Renderer should:

- compile cleanly enough for release,
- retain all intended modules,
- expose functioning controls,
- contain no obvious malicious/suspicious code,
- contain no known secrets,
- contain less genuine dead code,
- have improved fidelity,
- have improved or maintained performance,
- provide graceful fallbacks,
- be understandable to public users,
- be maintainable for future modular development.

---

# Unattended Mode

The project owner may not be available while work is being performed.

Do not stop for routine questions.

When choices arise:

1. inspect evidence,
2. choose the safest compatible option,
3. record the decision.

Do not ask for confirmation for normal engineering decisions.

Only stop when continuing could reasonably:

- destroy user data,
- overwrite significant unknown work,
- require unavailable external credentials,
- require a fundamentally subjective product decision that cannot safely default,
- or make validation impossible.

Otherwise continue autonomously.

---

# If You Discover Something Broken

Do not ignore it because it is outside the subsystem currently being reviewed.

Classify it.

If it is:

### Critical / High
Fix it if safely within scope.

### Medium
Fix if low risk or document.

### Low / Future
Document.

---

# If You Discover A Better Architecture

Do not automatically implement it.

Ask:

- Is the existing implementation broken?
- Can the new architecture be validated?
- Is the release risk justified?
- Is the performance impact known?
- Does it require broad ABI/resource changes?

If the answer is uncertain:

document it in the future roadmap.

---

# Final Agent Mindset

PIXL Renderer has already undergone extensive development.

Treat existing systems as intentional until proven otherwise.

Your responsibility is not to show how much code you can rewrite.

Your responsibility is to make the shipping renderer better.

Every change should answer at least one of these:

- Does this make the image more realistic?
- Does this make the renderer faster?
- Does this make behaviour more stable?
- Does this make the renderer safer?
- Does this make the UI easier to use?
- Does this remove proven obsolete code?
- Does this make future maintenance safer?

If the answer to all is no:

do not change it.

---

# Completion Criteria

Do not consider the final pre-release pass complete until:

- all active build/runtime files are inventoried,
- all active files are reviewed,
- no active file remains silently skipped,
- changes are documented,
- high-priority modules are polished,
- UI controls are traced,
- security audit is complete,
- dead-code audit is complete,
- project has been built after final changes,
- modified shader interfaces are validated,
- remaining risks are explicitly documented,
- future improvements are recorded.

PIXL Renderer should emerge from this work as the stable foundation for its public release and subsequent modular updates.