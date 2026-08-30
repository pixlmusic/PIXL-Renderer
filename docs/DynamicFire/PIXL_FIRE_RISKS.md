# PIXL Dynamic Fire — Risk Register

## Risk-adjusted decision

**PROTOTYPE FIRST — architecture viable but major unknowns remain**

DX11 can support the proposed system, and PIXL already provides most surrounding infrastructure. The two release-blocking unknowns are stable per-reference/local-surface identity for arbitrary Skyrim geometry and correct temporal annotation/order for PIXL-owned transparent fire under all reconstruction/frame-generation paths.

## Risk matrix

| Risk | Likelihood | Impact | Mitigation / required proof |
| --- | --- | --- | --- |
| Shared NIF/material state contaminates every instance | High | Critical | Key persistent state by validated reference handle/generation plus draw identity; Phase 1 shared-mesh test is a hard gate |
| Raw Skyrim pointers become stale after unload/reuse | High | Critical | Never persist raw pointers; validate handles/generations; immutable render snapshot; erase on lifecycle notification |
| Object-local mapping swims or jumps | Medium-High | Critical | Validate world/local transforms per supported geometry class; default-deny unsupported transforms; analytical debug overlay |
| Skinned/movable geometry lacks stable previous transform | Medium | High | Limit MVP to proven static references; add current/previous object transforms before enabling movable targets |
| Hook-chain or per-draw constant collision | Medium | Critical | Audit `SetupGeometry` owners and binding lifetime; allocate an explicit slot/payload; CPU/HLSL size/offset assertions; neutral rebinding every draw |
| SRV/UAV/register collision | Medium | Critical | Central binding inventory, fixed documented ranges, FXC permutation checks, D3D debug names, explicit unbind after passes |
| Transparent fire is inserted at the wrong frame stage | Medium | Critical | Phase 3 isolated pass; document inputs/outputs; verify before reconstruction encoding and before final presentation |
| DLSS/FSR ghosting or trails | High without masks | High | Current/previous particle positions; selective reactive/transparency merge; disocclusion/camera-cut reset; live matrix is a hard gate |
| Frame generation produces duplicated/broken flames | Medium-High | High | Deterministic motion, valid velocity/masks, UI separation, cut/reset handling; allow feature fallback or disable transparent Dynamic Fire with incompatible path |
| Emissive fireflies destabilize bloom/exposure | Medium | High | Scene-linear energy cap, area-aware aggregate radiance, bloom soft-knee/firefly rejection, exposure-relative validation |
| Particle overdraw dominates GPU time | High | High | Fixed pool, depth-aware culling, near/medium/far LOD, low-rate smoke, indirect/batched draw, quality budgets |
| Smoke causes severe bandwidth/temporal artifacts | Medium-High | High | Separate low-frequency pool, restrained opacity, half/quarter-resolution only if reconstruction is proven, hard distance/budget caps |
| GPU pool exhaustion corrupts state | Medium | High | Fixed capacities, alive/dead bounds checks, generation-safe handles, deterministic drop/merge policy, debug counters |
| CPU scans all loaded objects or all fire pairs | Medium | High | Event-driven active set, spatial hash, variable cadence, sleeping state, bounded neighbour candidates |
| Propagation crosses walls or ignites unrelated objects | Medium-High | High | Conservative evidence, proximity/contact/visibility constraints, default deny, explicit override tables, no material-only propagation |
| Material classifier burns stone/metal | Medium | High | Ordered evidence hierarchy and negative evidence; user/mod overrides; diagnostic reason; initial allowlist |
| Modded shaders/meshes cannot accept material injection | High | Medium | Fail closed per material path; retain original shading; support explicit compatibility profiles; no universal shader assumption |
| Fire lighting double counts with emission/GI | Medium | High | Define one energy source, aggregate candidates, restrained GI injection, trace direct/emissive/indirect composition |
| Radiant Grid block/capacity artifacts recur | Low-Medium | High | Keep accepted projection contract; aggregate and cap fire candidates before upload; test dense indoor/outdoor movement |
| GI history lags after ignition/extinguishing | Medium | Medium-High | Confidence/reset signal for abrupt energy changes; clamp temporal contribution; avoid forcing global reset for each spark |
| One Skyrim light per patch destroys CPU/GPU performance | High if naïve | High | Prohibit it; aggregate renderer-native candidates, optional single representative shadowed light only after profiling |
| Wind becomes camera-relative or diverges from vegetation | Medium | Medium | Shared immutable world-space `WindContext`; deterministic current/previous evaluation; do not reuse camera-relative noise |
| Rain/wetness/snow signals conflict | Medium | Medium-High | Weather is actual weather, season is context, resolved material is truth; consume existing wetness/classification rather than duplicate it |
| Snow melt/mud drying regresses Ground Response | Medium | High | Event API only; material-validity checks; disabled module leaves Ground Response unchanged; dedicated regression scene |
| Persistent fire survives on unloaded/replaced gear/object | Medium | High | Reference-local state, lifecycle cleanup, no mesh-pointer ownership, documented version-one reset policy |
| Save serialization bloats/corrupts saves | Medium | Critical | No save persistence in version one; later compact semantic state only, versioned and bounded |
| Shader permutation explosion forces full cache churn | Medium | Medium-High | Prefer runtime data/branches in new isolated passes; minimize material defines; validate dependency graph before shared includes |
| Asset licensing is insufficient | High for reference visuals | High | Treat Unity assets as visual references unless provenance grants redistribution; use PIXL-owned/licensed replacements; preserve CC BY attribution for sound if used |
| Reference assets are poorly compressed/formatted | Medium | Medium | Convert only licensed assets to suitable DDS/BC formats with mipmaps; validate linear/sRGB and alpha semantics |
| Debug/system logs spam every frame | Medium | Low-Medium | Transition-only logging, counters in diagnostics UI, rate-limited warnings |
| Optional module failure crashes PIXL | Low if designed correctly | Critical | Allocate transactionally; neutral bindings; disable subfeature/module on failure; never make Dynamic Fire a required startup dependency |

## DX11 constraints

- No bindless resource model: atlas/pool and register ownership must be explicit.
- UAV/RTV/SRV hazards require deliberate unbinding and pass ordering.
- Readback is too expensive for routine GPU-to-gameplay fire propagation; CPU owns semantic active state while GPU owns visual detail.
- Indirect draws and append/consume buffers are possible but need strict bounds and feature-level validation.
- A large per-object texture allocation model is not viable; use analytical patches and a sparse optional atlas.
- True volumetric combustion, arbitrary mesh destruction, and a world-scale cellular solver are outside the safe first-release scope.

## Skyrim engine constraints

- NIFs, materials, and shaders are widely shared between placed references.
- Render geometry does not always expose a clean persistent gameplay owner.
- Static, movable, skinned, first-person, third-person, particle, effect, and terrain paths differ.
- Havok collision callbacks are not a complete, cheap surface-contact stream for every rendered object.
- Cells unload/reload and form handles can become invalid; state must be generation-safe.
- Vanilla/modded fire content is heterogeneous; generic source detection needs conservative evidence and overrides.
- Arbitrary material destruction or mesh replacement would conflict with mods and game state.

## Compatibility risks

### Material and texture mods

Texture paths are useful evidence but never sufficient by themselves. A replacer can rename or repurpose assets; unknown materials remain nonflammable unless stronger evidence or an explicit rule exists.

### Mesh replacers and shared UVs

Analytical object-local patches tolerate missing/overlapping UVs better than an atlas. Sparse mask tiles must be restricted to UV layouts that pass validation.

### ENB/ReShade/post-processing

PIXL cannot assume another post stack's exposure, bloom, or transparency conventions. Fire output should remain scene-linear and avoid manipulating final display-space color directly.

### Upscalers/frame generation

Capabilities and active backends vary. Dynamic Fire must query PIXL's own reconstruction state and provide a safe surface-only fallback when required temporal annotations cannot be delivered.

### Other fire/magic mods

Do not suppress or replace their particles by default. PIXL may recognize them as ignition sources; visual replacement requires an explicit compatibility mode.

## Performance and memory ceilings

Exact budgets must be profiled, not fabricated. The architecture nevertheless requires fixed ceilings for:

- active reference count;
- patches per reference;
- flame/ember/smoke/steam particles;
- aggregate fire lights;
- propagation neighbours per update;
- sparse atlas dimensions/tiles;
- simulation and render distance.

Quality settings change these ceilings and update rates. They must never remove bounds.

Expected cost centres, highest first:

1. transparent flame/smoke overdraw;
2. surface-material shader instructions on affected draw paths;
3. particle simulation/buffer traffic;
4. optional sparse atlas updates;
5. propagation/classification CPU work;
6. aggregate Radiant Grid candidates.

When no active fire exists, all new simulation/render/light work should early-out and persistent overhead should be limited to tiny manager/module state.

## Temporal and visual artifact risks

- High-frequency flame animation can become DLSS fireflies.
- Thin ember particles can shimmer or disappear without stable pixel coverage.
- Smoke alpha can ghost if history confidence is too high.
- Abrupt char masks can crawl along geometry under reconstruction.
- Object-local patches can reveal sphere/projection shape at grazing angles.
- Sparse UV masks can seam on mirrored/overlapping UVs.
- Strong independent flicker lights can make the scene appear disconnected from the flame.

Mitigations are derivative-filtered masks, stable seeded noise, smooth state transitions, area-aware luminance caps, deterministic current/previous evaluation, and explicit reactive/transparency annotations.

## Persistence and save-game risks

Version one should persist only while relevant references remain loaded or while compact runtime state remains within the active pool. It should not serialize GPU textures, particles, or large masks.

Future save support requires:

- a versioned record;
- reference/form resolution that survives load-order changes;
- compact patch/state data;
- hard record/count limits;
- safe discard of missing references;
- migration and corruption handling.

## Asset provenance

Technical convertibility is not redistribution permission. Before any Unity-reference asset enters PIXL distribution:

1. identify its original author/source/license;
2. confirm modification and redistribution permission;
3. record conversion and color-space/alpha treatment;
4. preserve attribution/license text;
5. exclude it if provenance is uncertain.

The reference sound's text indicates CC BY 4.0 and would require attribution if used. The remaining textures/VFX assets should be considered unapproved until their provenance is documented.

## Explicit non-goals for the first release

- Destructible world geometry.
- Fire simulation across unloaded cells or the entire world.
- Per-triangle combustion physics.
- Full NavMesh/gameplay AI fire avoidance.
- One shadowed game light per flame.
- A physically complete fluid/combustion or volumetric-smoke solver.
- Automatic ignition of every texture that happens to contain the word `wood`.
- Save-game persistence of GPU masks or particles.
- Replacing all vanilla/mod fire effects.

## Prototype exit criteria

Proceed from prototype to production only when all are true:

- reference identity survives shared-mesh, unload/reload, and handle-reuse tests;
- local burn state remains anchored on static geometry and explicitly supported movable geometry;
- material classification produces no unsafe broad false positives in representative modded scenes;
- surface shading preserves the original material stack;
- transparent proof particles have correct current/previous motion and selective masks;
- DLAA, DLSS, FSR, and supported frame generation pass live temporal tests;
- module disable/failure is pixel-identical to the accepted PIXL baseline;
- fixed CPU/GPU/VRAM ceilings are enforced;
- asset provenance is complete for anything shipped.
