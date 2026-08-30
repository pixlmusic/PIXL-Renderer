# PIXL Dynamic Fire — Ordered Implementation Plan

## Delivery principle

Dynamic Fire should be introduced as a sequence of isolated, reversible capabilities. The first two technical gates are stable object-local identity/mapping and correct reconstruction annotations. If either cannot be demonstrated safely, PIXL should retain the successful surface-only prototype and defer transparent flame/smoke rendering rather than destabilize the shipping renderer.

No phase below should delete or replace Skyrim fire effects. The module remains optional, defaults off during development, and failure disables Dynamic Fire alone.

## Phase 0 — Instrumentation and controlled test scene

### Work

- Add a development-only module skeleton and diagnostics without changing final pixels.
- Record a stable reference handle, geometry/material identity, world transform, bounds, visibility, distance, and resolved physical-material evidence for selected draws.
- Add PIXL GPU event names and CPU timing around future simulation/render points.
- Establish test references: a unique wood object, repeated/shared wood meshes, cloth, foliage, stone, metal, an actor, a movable object, a campfire, a fire spell, rain, snow, and wet ground.

### Validation gate

- Release build and renderer audit pass.
- Debug overlay selects only explicitly targeted objects.
- Shared meshes can be distinguished by placed reference.
- No render-state, shader, cache, or frame-time change while disabled.

### Rollback

Remove only the module registration and diagnostics. No existing module ABI is changed.

## Phase 1 — Stable identity, local coordinates, and classification prototype

### Work

- Implement `DynamicSurfaceObjectKey` from a validated Skyrim reference handle/generation plus draw identity; never use a raw pointer as persistent identity.
- Produce stable world-to-local data for static, movable, and supported skinned geometry.
- Implement conservative burnability evidence in this order: explicit override, PIXL Physical Material descriptor, Skyrim material/shader flags, normalized texture/mesh-path evidence, form/reference context, default deny.
- Add positive and negative diagnostic reasons and opt-in/opt-out rules.
- Create a small test-only `AddSurfaceEffect()` API and visualize local-space event centres/radii.

### Hard go/no-go gate

Across cell reload, first/third person change, camera movement, and repeated shared meshes:

- an event stays anchored to the same object and surface region;
- two instances of one mesh do not share state;
- unloaded/reused handles do not inherit stale fire;
- stone, metal, snow, water, and unknown materials remain nonflammable by default.

If this fails, do not proceed to persistent world-object burning. Restrict the first release to explicitly registered actors/references or stop at documentation.

## Phase 2 — Analytical surface burn prototype

### Work

- Add bounded per-reference `FirePatch` state: local centre, radius, heat, fuel, moisture, age, state, directional bias, and deterministic seed.
- Advance state with elapsed time, not frame count.
- Bind a compact per-draw fire payload through a new audited slot or an intentionally extended compatible material payload. Do not reuse Actor Surface Effects `b13` without proving hook ordering and ownership.
- Add HLSL helpers for heating, drying, ember front, albedo darkening, roughness evolution, char normal/microdetail, cooling, and original-material recovery policy.
- Keep normal maps, PBR, parallax, Skin Optics, wetness, snow, and existing lighting intact.
- Add one aggregate, non-shadowed fire-light candidate through the Radiant Grid producer path for the test object.

### Acceptance criteria

- The burn front is object-anchored and remains stable under camera motion.
- Wood progresses through original, heated, glowing edge, charred, and cooling states.
- Metal/stone remain unchanged unless explicitly opted in.
- Disabling the module restores the exact baseline material path.
- No NaN/INF, cbuffer mismatch, register collision, or new full cache invalidation beyond touched permutations.

## Phase 3 — Reconstruction and custom-transparent-pass prototype

### Work

- Reserve an explicit render point after suitable opaque lighting and before reconstruction encoding.
- Add a small custom billboard proof: current/previous world positions, deterministic animation, depth test, HDR emission, and premultiplied transparency.
- Merge selective fire coverage into PIXL's reactive and transparency/composition inputs before `ImageReconstruction::EncodeTextures`.
- Verify jitter convention, internal/display resolution mapping, reversed/nonlinear depth expectations, camera cuts, teleports, FOV changes, and module toggles.
- Ensure no second sharpening or bloom path is introduced.

### Hard go/no-go gate

With DLAA, DLSS, FSR, and supported frame generation:

- the proof billboard has no long trails, double images, frozen history, or severe edge breakup;
- masks cover only changing transparent/emissive pixels;
- opaque char remains temporally stable;
- menus, loading, teleport, Photo Mode, and camera cuts reset history safely.

If this fails, ship surface burning without PIXL-owned transparent flames and continue using Skyrim particles until the mask/render-order contract is solved.

## Phase 4 — Persistent bounded fire state

### Work

- Add an active-reference pool, spatial hash, event queue, sleeping states, LRU/priority eviction, and immutable render snapshot.
- Prioritize player interaction, visible/near references, active heat, and recent events.
- Use fixed ceilings for active references and patches; allocate nothing for untouched objects.
- On unload, retain only compact state when justified; release GPU allocations immediately.
- Do not serialize state into saves in version one.

### Acceptance criteria

- CPU does not scan all loaded references each frame.
- Pool exhaustion degrades by merging, sleeping, or evicting low-priority states rather than allocating without bound.
- Loading/unloading and reference deletion do not crash or transfer fire to another object.

## Phase 5 — GPU flames and embers

### Work

- Create fixed-capacity particle buffers, alive/dead lists, indirect draw arguments where supported by the established DX11 path, and deterministic current/previous integration.
- Spawn particles from active patch surfaces, not one VFX system per object.
- Render soft depth-aware flame/ember billboards from licensed or PIXL-owned flipbooks.
- Add near/medium/far LODs and an emission/firefly clamp suitable for reconstruction and bloom.

### Acceptance criteria

- Particle count and VRAM stay within the selected tier's fixed budget.
- Flames remain world-space stable and follow supported movable objects.
- Camera motion, DLSS/FSR, and frame generation do not create excessive trails.
- Distant fire becomes a cheap emissive proxy rather than disappearing abruptly.

## Phase 6 — Smoke and steam

### Work

- Add a separate lower-frequency smoke pool and short-lived steam for wet suppression.
- Use depth-aware soft intersection, restrained opacity, wind advection, buoyancy, turbulence, and coarse self-fade.
- Mark changing transparency selectively for reconstruction.
- Avoid a full volumetric smoke solver in the first release.

### Acceptance criteria

- Smoke does not wash the whole scene, clip sharply into geometry, or dominate overdraw.
- Rain/water suppression produces brief steam and reduced smoke/flame energy.
- Interior and exterior density remain controllable and stable.

## Phase 7 — Renderer-native lighting and GI

### Work

- Cluster nearby patches into a small number of aggregate `FireLightCandidate`s.
- Inject candidates before Radiant Grid's light upload; never create a Skyrim light per patch.
- Derive flicker from aggregate heat/combustion with deterministic low-frequency noise.
- Let Hybrid GI consume fire radiance only at its valid pipeline point and invalidate/clamp history on abrupt ignition/extinguishing.
- Optionally evaluate one restrained shadowed representative light for Ultra only after profiling.

### Acceptance criteria

- Nearby materials receive coherent warm direct/specular response.
- Fire light does not appear in hard Radiant Grid blocks, penetrate rooms incorrectly, or double-count through GI/emissive terms.
- Hundreds of patches do not become hundreds of CPU/game lights.

## Phase 8 — Propagation

### Work

- Use a CPU spatial hash of active fire patches/references at a low variable frequency.
- Compute transfer from distance/contact evidence, material fuel, heat, moisture, wind direction, and explicit compatibility rules.
- Spread within one object by adding/merging local patches; spread between references only through bounded neighbour candidates.
- Add cooldown and deterministic stochastic variation to prevent synchronous ignition.

### Acceptance criteria

- Fire spreads across eligible nearby wood/cloth but not through stone walls or arbitrary shared materials.
- Wet/rained-on surfaces resist ignition.
- Update cost follows active fire count and configured distance, not world size.
- Fire cannot grow beyond the global active-reference/particle/light budgets.

## Phase 9 — Weather, wind, wetness, snow, and Ground Response

### Work

- Introduce a shared read-only `WindContext` derived from the accepted PIXL world-space wind/weather state; do not couple Dynamic Fire to foliage deformation internals.
- Rain and water raise moisture and suppress heat; existing material wetness informs ignition resistance.
- Wind bends particles/smoke and biases propagation using stable world-space sampling.
- Submit heat interactions to Ground Response for bounded snow melt/drying where its material classifier confirms a compatible surface.
- Keep season as context and resolved material as truth.

### Acceptance criteria

- Wind direction remains world-anchored, rain visibly suppresses fire, and snow melt occurs only near sufficiently energetic fires.
- Ground Response, snow/mud deformation, Seasons integration, and Actor Surface Effects behave identically with Dynamic Fire disabled.

## Phase 10 — Skyrim ignition and suppression sources

### Work

- Reuse Ground Response's existing projectile/magic/shout interaction observations where suitable.
- Add conservative detection for campfires, braziers, torches, fire spells/projectiles/explosions, burning actors, and scripted/mod events.
- Expose a native internal API for explicit ignition/suppression and optional future mod-author registration.
- Avoid hard-coded coverage of every FormID; combine semantic effect/archetype/material evidence with overrides.

### Acceptance criteria

- Common vanilla sources ignite eligible nearby targets without modifying vanilla assets.
- Non-fire spells and decorative emissives do not ignite objects accidentally.
- Unknown/modded sources fail safely and can be configured explicitly.

## Phase 11 — Optional sparse near-field burn masks

### Work

- Only after the analytical path is stable, evaluate a sparse pooled atlas for high-priority, UV-suitable objects.
- Store coverage/heat/char/moisture with generation-safe tile ownership and dirty-region compute updates.
- Fall back to analytical patches for shared, overlapping, missing, or unsuitable UVs.

### Acceptance criteria

- Atlas allocation has a fixed VRAM ceiling and no cross-object tile leakage.
- UV seams/overlaps do not corrupt unrelated surfaces.
- The visual improvement justifies bandwidth and complexity over analytical patches.

## Phase 12 — GUI, quality tiers, packaging, and release polish

### User controls

- Enable Dynamic Fire.
- Quality: Low / Medium / High / Ultra.
- Simulation Distance.
- Maximum Active Fires.
- Fire Spread.
- Wind Response.
- Weather Response.
- Flame Quality.
- Smoke Quality.
- Lighting Contribution.
- Charred Surface Detail.

Technical pool sizes, time steps, thresholds, and register details remain internal. Debug views are separated from user controls.

### Quality scaling

| Tier | Surface | Particles | Smoke | Propagation | Lighting |
| --- | --- | --- | --- | --- | --- |
| Low | Analytical, coarse | Low fixed budget | Minimal | Low-frequency/short range | Few aggregate lights |
| Medium | Analytical | Moderate | Moderate | Bounded | Aggregate direct light |
| High | Analytical + selected detail | High | High | Longer range/frequency | Direct + restrained GI |
| Ultra | Optional sparse masks | Highest fixed budget | Highest bounded quality | Highest bounded settings | Optional representative shadow experiment |

### Release checks

- Every GUI value traces through serialization, CPU state, GPU constants/resources, and visible behavior.
- Missing assets/resources disable only the affected subfeature.
- Logs are concise and no per-frame spam remains.
- Third-party licenses/provenance are complete.
- Release build, shader permutations, renderer audit, cold initialization, cache behavior, and live test matrix pass.

## Suggested change groups

Keep changes independently reviewable:

1. module skeleton/diagnostics;
2. identity/classification;
3. surface shading;
4. reconstruction annotations;
5. state pool;
6. flames/embers;
7. smoke/steam;
8. lighting/GI;
9. propagation/environment;
10. Skyrim event sources;
11. GUI/quality/docs.

## Final implementation gate

The first public version should not advance beyond the **Recommended PIXL implementation** until the object-identity and reconstruction gates pass in live Skyrim. Compilation alone cannot validate either risk.
