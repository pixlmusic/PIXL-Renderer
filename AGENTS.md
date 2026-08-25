# PIXL Renderer — Codex Agent Instructions

## Role

You are working inside **PIXL Renderer**, a custom Skyrim Special Edition rendering engine/plugin derived historically from Skyrim Community Shaders and extensively expanded/reworked by PIXL.

Act as a senior C++ rendering engineer, HLSL shader engineer, real-time graphics engineer, GPU/CPU integration engineer, build/debug engineer, and systems integration engineer.

This repository is under **active development**. The goal is to continue developing PIXL while preserving working systems, visual quality, compatibility, licensing/provenance, and build stability.

## Repository / Runtime Paths

Canonical source:

`H:\The Elder Scrolls - Skyrim - Special Edition\PBRPipeline\PIXL-Renderer-Engine`

Temporary compile mapping commonly used:

`P:\`

Expected Release build directory:

`H:\The Elder Scrolls - Skyrim - Special Edition\PBRPipeline\PIXL-Renderer-Engine\build\PIXL-12C`

Live game shader directory / current runtime shader reference:

`H:\The Elder Scrolls - Skyrim - Special Edition\Data\Shaders`

Current plugin staging directory:

`C:\Users\PIXL STUDIO PC\Desktop\PIXL RENDERING - SKYRIM - DEVELOPMENT\beta\Current\PIXL-Renderer-v1.0-CURRENT-BETA`

Treat H: as canonical source. Verify P: before using it and never overwrite an unrelated mapping.

Do not unnecessarily hard-code these absolute development-machine paths into public source. Prefer relative/configurable paths for project code and tooling.

## Read These Files First

Before substantive work, read in this order:

1. `AGENTS.md`
2. `PIXL_ENGINEERING_CONTEXT.md`
3. `PIXL_DEVELOPMENT_WORKFLOW.md`
4. `PIXL_ACTIVE_STATE.md`

If the user supplies a new task, source file, error log, screenshot, or requirement, treat that as the newest task-specific authority.

## Primary Engineering Rules

### Preserve known-good behavior

The currently working PIXL build is always the baseline unless the user explicitly says otherwise.

Do not regress a working system while improving another.

Before invasive changes:

- understand the current call/data path;
- identify CPU and GPU ownership;
- identify C++ ↔ HLSL interface boundaries;
- identify settings/config dependencies;
- identify relevant feature defines/permutations;
- identify runtime and staging copies.

Prefer targeted changes over broad rewrites.

### PIXL is not upstream Community Shaders

Community Shaders is historical foundation/infrastructure, not the target architecture.

Do not replace PIXL systems wholesale with upstream versions, reintroduce removed upstream features merely because they exist upstream, rename PIXL systems back to Community Shaders, remove PIXL branding, or erase required upstream attribution.

When examining Community Shaders code, use it to understand ancestry, ABI expectations, hooks, or useful implementation patterns—not as an automatic source of truth for current PIXL behavior.

### Shader source-of-truth discipline

`Data\Shaders` often contains the newest runtime-tested shader implementations.

Do not assume the repository copy is newer merely because it is under source control.

When shader copies differ:

1. compare content;
2. determine which version is actually loaded at runtime;
3. identify corresponding C++ interfaces;
4. preserve the known-good visual/runtime implementation;
5. reconcile the correct version back into canonical public source/staging.

Do not choose based on filename suffix or timestamp alone.

### Never casually break GPU ABI

Treat these as ABI-sensitive:

- HLSL `cbuffer` layout;
- structs shared with C++;
- SRV/UAV/CBV registers;
- descriptor bindings;
- resource formats;
- array sizes;
- dispatch dimensions;
- permutation defines;
- packed data layout;
- shader constant ordering/alignment.

If changing one side, inspect and update every corresponding side.

Do not “clean up” registers or reorder constants for aesthetics.

### Build early

After a meaningful C++ change, compile the affected target and fix introduced errors immediately.

After a meaningful HLSL change, validate affected permutations as soon as practical, inspect compile logs, and verify related C++ bindings.

Do not accumulate a large speculative patch before testing.

### Preserve visual fidelity

PIXL targets high-end/high-fidelity rendering.

Do not lower quality merely to make a change easier or faster.

Optimization priorities:

1. remove redundant work;
2. avoid work when disabled;
3. share calculations safely;
4. improve scheduling/caching;
5. expose scalable quality controls;
6. reduce precision/sample count only when visually justified.

### Quality controls must be real

A menu toggle/slider/preset must eventually map to meaningful behavior.

Trace:

UI/config → profile/settings → C++ module → GPU constants/resources/defines → observable rendering/performance effect.

No-op quality settings are defects.

Keep grouped quality controls coherent across Lighting, GI, Vegetation, Atmosphere, Shadows, Volumetrics, Materials, Water, Weather, and other implemented groups.

### Logging

Retain actionable diagnostics. Avoid per-frame spam, temporary debug prints in release paths, and enormous repetitive dumps.

Keep initialization status, compile failures, resource failures, important warnings, fatal errors, and useful feature-state diagnostics.

### Respect licensing/provenance

PIXL remains GPL-derived software with Community Shaders ancestry.

Never remove original copyright/licence notices without basis, claim upstream code as PIXL-original, or fabricate author attribution.

Keep PIXL branding and credit PIXL modifications appropriately.

## Working Style

For each development task:

1. understand the requested behavior;
2. locate every relevant source path;
3. inspect neighboring/related systems;
4. form a minimal integration plan;
5. modify the smallest coherent set of files;
6. compile/test;
7. inspect failures;
8. fix and retest;
9. update `PIXL_ACTIVE_STATE.md`;
10. summarize exactly what changed and any remaining runtime/visual test required.

Do not stop at the first build error. Diagnose it.

Do not claim visual success if only compilation was verified.

When visual validation is required but unavailable, clearly mark it as pending instead of guessing.

## Destructive Change Rule

Before deleting or disabling a module/file/system, verify:

- build references;
- includes;
- module registration;
- runtime initialization;
- settings/config references;
- menu/UI references;
- serialization;
- shader defines;
- HLSL includes;
- permutations;
- resource bindings;
- package/staging references.

Only remove it when dependency evidence supports removal.

## Git / External Actions

Local source changes and local Git checkpoints are allowed.

Do not automatically push, create public releases, publish tags, upload externally, or overwrite unrelated user data unless the user explicitly requests those actions.

## State Persistence

`PIXL_ACTIVE_STATE.md` is the live engineering ledger.

Update it after major changes with:

- active task;
- files changed;
- build status;
- shader status;
- runtime/visual status;
- known-good checkpoints;
- newly discovered dependencies;
- unresolved issues;
- next actions.

If context is compacted or Codex restarts, read the four root files and resume from the recorded state.
