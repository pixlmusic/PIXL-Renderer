# PIXL Renderer — Active Development Workflow

This file defines the normal workflow Codex should use during ongoing PIXL Renderer development.

It replaces the release-preparation mentality with a fast but disciplined engineering loop.

## 1. Start of Every Task

Before changing code:

1. Read the user's newest requirement/error/log.
2. Read `PIXL_ACTIVE_STATE.md`.
3. Identify the subsystem(s) involved.
4. Locate the active files in canonical source.
5. Check whether `Data\Shaders` contains a newer runtime copy.
6. Identify C++ ↔ HLSL/settings/build dependencies.
7. Preserve known-good behavior relevant to the task.

Do not spend excessive time writing a giant plan.

For normal tasks, a short plan is enough.

For invasive system-wide work, record a larger plan in `PIXL_ACTIVE_STATE.md`.

## 2. Source-of-Truth Decision Tree

### C++

Default source of truth:

`PIXL-Renderer-Engine`

Do not edit generated build copies as the canonical implementation.

### Shaders

When repository and `Data\Shaders` versions differ:

- diff both;
- determine which version is actually loaded and most current;
- check integration with current C++;
- preserve known-good runtime behavior;
- reconcile intentionally.

### Staging

Staging is packaging/output, not canonical development source unless a specific asset/config exists only there and must be brought back into source.

## 3. Development Loop

### A. Inspect

Read the smallest complete set of relevant files.

Include neighboring/shared code when needed.

### B. Explain the fault

Identify the actual failure mode.

Examples:

- missing API/member;
- stale shader declaration;
- disconnected setting;
- wrong coordinate space;
- incorrect resource binding;
- duplicate lighting contribution;
- cache/history not reset;
- unsupported permutation;
- stale file copied at runtime;
- quality profile mapped to identical values.

### C. Patch

Make the smallest coherent change that solves the system problem.

Do not stack unrelated cleanup unless it directly reduces risk or fixes the same root cause.

### D. Compile

C++ changes: build affected target/configuration.

Shader changes: compile/test affected permutations.

### E. Diagnose

If compilation fails, use the failure as evidence.

Do not blindly revert unless the approach is fundamentally wrong.

### F. Runtime validate

When possible:

- stage the DLL/shaders;
- run the existing workflow;
- inspect `PIXLRenderer.log`;
- validate behavior.

### G. Record

Update `PIXL_ACTIVE_STATE.md`.

## 4. C++ Change Checklist

When modifying C++ renderer code, check as applicable:

- include dependencies;
- ownership/lifetime;
- initialization order;
- shutdown/reset;
- null handling;
- thread safety;
- render-thread assumptions;
- per-frame allocations;
- static/global state;
- resource recreation;
- resolution changes;
- device reset;
- settings reload;
- interior/exterior transitions;
- photo/director mode;
- serialization/config persistence.

For GPU-facing structures also verify alignment, packing, field order, size, register/binding index, and resource dimension/format.

## 5. HLSL Change Checklist

For shader changes, check as applicable:

- all permutations;
- feature guards;
- include order;
- macros/defines;
- stage-specific availability;
- resource declarations;
- cbuffer layout;
- coordinate space;
- normal space;
- depth convention;
- motion/history data;
- NaN/divide-by-zero risk;
- bounds/UV clamping;
- alpha behavior;
- shadow pass consistency;
- water/material special cases;
- interior/exterior behavior;
- DLSS/upscaler interaction where relevant.

Do not assume a pixel shader change is isolated if the same data is generated in VS/HS/DS/CS stages.

## 6. Shared Lighting / Material Change Rule

Changes to shared lighting/material code receive extra scrutiny.

Before modifying shared lighting:

1. identify every major feature guard involved;
2. inspect the final composition order;
3. determine whether the new contribution is additive, replacing, attenuating, or modulating;
4. avoid double-counting;
5. inspect representative terrain, vegetation, skin, hair, water, snow, and standard material paths.

If a change touches `Lighting.hlsl` or a widely included `.hlsli`, prefer feature-local code where possible.

## 7. GroundResponse Protection Rule

The known-good GroundResponse 13BE behavior is protected.

Any change affecting terrain depth, hull/tessellation, contact shadows, directional shadows, material lighting, snow/mud, fire/spell interaction, or deformation must be reviewed for GroundResponse regression risk.

Do not alter the established raster-depth authority without a strong reason and explicit validation.

## 8. Quality-System Workflow

When adding or fixing a quality setting:

1. find UI/config declaration;
2. find persistence/default;
3. find profile mapping;
4. find C++ runtime consumer;
5. find GPU consumer;
6. verify that changing the setting modifies real parameters/resources/work;
7. ensure presets are meaningfully different;
8. ensure defaults preserve the desired baseline;
9. test live changes if supported;
10. ensure restart/reload behavior is sane.

Do not ship sliders that only update JSON.

## 9. Performance Workflow

When optimizing, identify the expensive operation first when possible.

Prefer disabling work when a feature is off, scalable resolution/settings, fewer redundant passes, shared intermediate results, better temporal reuse, fewer unnecessary resource transitions, fewer unnecessary texture reads, and safe branch/permutation reductions.

Avoid random precision reduction or sample-count cuts without visual justification.

## 10. Error-Log Workflow

For C++ compiler errors:

- inspect the exact header/type in the current dependency;
- fix to the installed API version.

For HLSL compiler errors:

- inspect the exact permutation/defines;
- locate the declaration under those guards;
- confirm stage and include order;
- fix the actual active permutation.

For runtime logs:

- distinguish initialization failure from warning/noise;
- trace the first causal error rather than secondary spam.

## 11. Build Environment Rule

If MSBuild/CMake/tooling is not found:

- diagnose environment first;
- use Visual Studio Developer PowerShell / x64 Native Tools when appropriate;
- locate the existing installed toolchain;
- do not modify PIXL source to compensate for a shell PATH problem.

## 12. Staging Rule

When a change is ready for game testing:

- copy only intended runtime artifacts;
- ensure the DLL corresponds to the build just produced;
- ensure shader copy corresponds to the shader just edited;
- avoid stale duplicate DLLs/shaders;
- preserve user configs unless the task explicitly migrates them.

## 13. Regression Review

Before finishing a task, ask whether it could affect:

- shared lighting;
- GroundResponse;
- vegetation/wind;
- precipitation;
- water;
- skin/eyes/hair;
- interiors;
- photo/director mode;
- temporal history/upscaling;
- quality presets.

Investigate deeply only where technically relevant, but do not ignore obvious cross-system coupling.

## 14. Active-State Update Format

After major work, update `PIXL_ACTIVE_STATE.md` with:

- Active task
- Root cause
- Changes
- Build
- Shader validation
- Runtime validation
- Known-good checkpoint
- Remaining work

## 15. Session End

Do not leave the ledger saying “in progress” if the task was actually completed.

If runtime visual testing still needs the user, say exactly what they should look for in-game.

Keep the state file useful to the next Codex session rather than turning it into a full chat transcript.
