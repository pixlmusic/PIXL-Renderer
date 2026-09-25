# CS 1.9.0 selective adoption plan

## Guardrails

- The CS checkout is a read-only GPL-3.0-compatible reference.
- PIXL is not to be synchronized or merged with it.
- Ground Response and WindowLife are out of scope for source changes in this plan.
- PIXL's GUI, theme, menus, Tuner, configuration ownership, presets and naming remain
  the only user-facing framework.
- Every hook, CPU/HLSL ABI, register, resource lifetime and shader cache change must
  be validated at the corresponding PIXL call site.

## Proposed changes

| ID | Title | Class | Status |
|---|---|---:|---|
| UP190-001 | Full AE 1.7.99 relocation compatibility audit | B/P1 | Planned research; no partial port. |
| UP190-002 | Wine/CrossOver vtable-hook fallback evaluation | B/P2 | Planned research. |
| UP190-003 | PIXL-owned GPU grass culling feasibility prototype | E/P3 | Deferred; architecture required. |
| UP190-004 | Remove stale inactive `GrassCollision` disable-at-boot key | A/P1 | Implemented with a key-specific load migration; validation pending build/config checks. |
| UP190-005 | Complex-material GGX upstream mathematics comparison | D/P1 | Analysis only; tied to the existing PIXL artifact investigation. |
| UP190-006 | CMake/module manifest tooling comparison | D/P4 | Deferred. |

## UP190-001 - Full AE 1.7.99 relocation compatibility audit

**CS source:** `src/Utils/VersionedRelocation.h`, `Deferred.h`,
`Hooks.cpp`, Grass Collision, Unified Water, Upscaling, Terrain Blending and other
offset-based hook users.

**PIXL destination:** every offset-based hook under `engine/**` and active modules.

**Problem:** `REL::Relocate(SE, AE)` selects only two offsets. CS has
third values where the AE 1.7.99 executable changed call sites or Address Library
IDs.

**Required PIXL adaptation:**

1. Inventory every `REL::Relocate`, direct `+ 0x...`, `safe_write`, and
   `write_thunk_call` in active PIXL source.
2. Obtain verified addresses for each supported runtime, including every PIXL-only
   module hook. Never infer a third offset from an upstream module with different
   surrounding code.
3. Add a small shared PIXL versioned relocation helper only after the full matrix
   exists. Adopt it at all audited sites together.
4. Fail closed on an unsupported runtime before installing hooks.

**Regression risk:** High. An incorrect instruction offset is a crash/corruption
risk.

**Validation:** static address table review, supported-runtime startup, loading,
first/third person, exterior/interior, shadow pass, terrain replay, water and menu
tests. Build DLL. No shader cache rebuild unless shader source changes.

**GUI impact:** None.

## UP190-002 - Wine/CrossOver vtable-hook fallback evaluation

**CS source:** `src/Utils/VTableHookFallback.{h,cpp}`.

**PIXL destination:** hook helper layer, not individual rendering modules.

**Problem:** writable vtable assumptions can fail in host-mapped memory. Upstream
tries in-place patching and, if needed, an owned vtable clone.

**Required PIXL adaptation:** identify actual PIXL hook APIs that expose a Detours
failure result. Preserve hook chaining and plugin lifetime. Do not insert a clone
mechanism behind `write_vfunc` without proving its ownership and shutdown behavior.

**Regression risk:** Medium. Incorrect cloning can drop another plugin's hook or
retain an invalid object address.

**Validation:** normal Windows startup and any available Wine/CrossOver test. Verify
all chained vfunc hooks, especially grass, terrain, lighting, water and WindowLife.

**GUI impact:** None.

## UP190-003 - PIXL-owned GPU grass culling feasibility prototype

**CS source:** `src/Features/GrassOptimizations.{h,cpp}`, the
`GrassBucketStore`, `GrassMeshLibrary`, `HiZPyramid` helpers and its compute shader.

**PIXL destination:** a new module under PIXL vegetation performance architecture;
not `GroundResponse` and not a replacement `RunGrass.hlsl`.

**Problem:** vanilla grass can issue many small draws and shade occluded blades.
Upstream captures raw instances, builds type buckets, dispatches frustum/projected
size/Hi-Z culling and submits indirect draws.

**Required PIXL adaptation:**

1. Establish one hook owner for `BSGrassShader::SetupGeometry` and the raw grass
   draw interception points.
2. Preserve PIXL `RunGrass.hlsl` current/previous wind, Ground Response collision
   sampling at VS `t100`, Foliage Dynamics tuning, and existing motion-vector output.
3. Keep Ground Response's legacy collision field separate from snow/mud terrain
   fields. The new module must not write terrain `t101+` resources.
4. Audit all CS/UAV/SRV slots and render-state restoration before any indirect draw.
5. Begin with frustum/distance culling without mesh substitution or Hi-Z; prove
   output and motion-vector parity; then consider Hi-Z and LOD.

**Regression risk:** High: raw game hook changes, indirect draw ownership, culling
false positives, temporal artifacts, interaction loss, and mod mesh compatibility.

**Validation:** grass interaction with Ground Response on/off, moving player/NPCs,
first/third person, seasons, terrain transitions, weather, dense grass, occluders,
fast camera turns, cache rebuild, and D3D debug layer. Measure only with PIXL's
actual profiler; do not infer timings from source.

**GUI impact:** Future PIXL `Land & Vegetation` quality preset. Keep initial normal
controls limited to a quality selector and enable switch. Place pixel thresholds,
Hi-Z bias, and mesh LOD under Advanced only after the base path is stable.

## UP190-004 - stale `Disable at Boot.GrassCollision` setting cleanup

**CS source:** its former independent Grass Collision feature.

**PIXL destination:** `distribution/SKSE/Plugins/PIXLRenderer/SettingsDefault.json`
and shipped preset JSON only.

**Problem:** PIXL does not have an active standalone `GrassCollision` module. Grass
collision is implemented inside `GroundResponse` as `EnableGroundResponse`. The
inactive legacy key can confuse configuration readers.

**PIXL adaptation:** State now removes only the retired key while loading an existing
profile, logs the migration, and writes the cleaned map on the next settings save.
The key was removed from defaults and shipped presets. `GroundResponse` and every
public Ground Response setting remain unchanged.

**Regression risk:** Low after migration verification.

**Validation:** load old JSON containing the key, load a new default/preset, verify
Ground Response still loads, and verify the grass VS field binds with
`EnableGroundResponse=true`.

**GUI impact:** None; PIXL already exposes the relevant control within Ground
Response.

## UP190-005 - complex-material GGX mathematical comparison

**CS source:** `package/Shaders/Common/BRDF.hlsli`, `PBR.hlsli`,
`PBRMath.hlsli`, `Lighting.hlsl`, PBR module paths.

**PIXL destination:** PIXL BRDF/PBR helpers, `Lighting.hlsl`, MaterialForge and
Material Layers call sites.

**Problem:** PIXL has a reported complex-material specular pin artifact. The exact
helper comparison confirms PIXL already contains stronger finite-GGX and
normal/scalar-variance roughness safeguards than the upstream snapshot. A newer
upstream formula is therefore not automatically a fix because its material channels,
normal space, roughness conventions and lighting inputs differ.

**Required PIXL adaptation:** trace the PIXL complex-material inputs to the final
GGX response, compare guards and remapping in isolation, and build an A/B shader
permutation only if a specific numerical fault is found. Preserve normal-map,
parallax, MaterialForge and motion/depth behavior.

**Regression risk:** Medium. Incorrect BRDF adoption changes the entire material
look.

**Validation:** affected PBR meshes, non-PBR meshes, roughness extremes, grazing
angles, lights, wetness, parallax, DLAA/DLSS/TAA movement, and shader compilation.

**GUI impact:** None unless an existing PIXL material quality tier needs a documented
fallback.

## UP190-006 - CMake/module manifest comparison

**CS source:** top-level `CMakeLists.txt` feature manifest/version
scan.

**PIXL destination:** existing PIXL module audit/package workflow.

**Problem:** upstream has a strict feature-stage/version parser. PIXL has its own
module and shipping audit, plus a different package structure.

**Required PIXL adaptation:** compare output artifacts, release gate behavior,
module metadata and developer workflow first. Reuse only parser invariants that
increase release validation without adding upstream directory conventions.

**Regression risk:** Medium for build/release automation.

**Validation:** clean configure, regular build, package build, missing module INI,
malformed version, and shipping audit.

**GUI impact:** None.

## Explicit non-adoptions

- CS menus, NativeMenu, CSEditor, themes and translations.
- Remote Control/DevBench/MCP/REST bridge.
- Effects11/ENB extender architecture.
- Standalone Grass Collision alongside Ground Response.
- Any wholesale upstream water, terrain, GI, WindowLife, Ground Response, material,
  camera or GUI replacement.

## Build and deployment requirements

No source implementation is authorized by this plan without the named validation.
For an adopted C++ change: build `PIXLRenderer.dll`, inspect new warnings, and deploy
only after the build succeeds. For an adopted shader change: compile affected
permutations, validate CPU/HLSL ABI and resource slots, then intentionally invalidate
the PIXL pipeline library through the module/version mechanism. Ground Response and
WindowLife remain untouched.
