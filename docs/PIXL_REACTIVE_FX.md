# PIXL Reactive FX

PIXL Reactive FX is an experimental, standalone PIXL Renderer extension for
surface-aware combat, magic and environmental reactions. It augments Skyrim's
existing events and rendering; it does not replace combat, quests, animations,
projectiles, impact art, or the original draw path.

## Architecture

The first milestone uses a bounded CPU event queue and a persistent DX11
structured particle buffer. Existing Skyrim hit, spell-cast and projectile
impact information is classified into a source and surface, resolved through a
common recipe, then uploaded as exact-slot spawn commands. Compute shaders
simulate particles against PIXL's final displaced depth and normal buffers and
composite the visible result into the HDR scene before rain and forward
presentation layers.

Large events also write a fixed-size world-space impulse set. The compute particle
simulation samples the current and previous impulse ages so sparks, smoke, dust,
leaves and fragments can respond coherently without a CPU readback. The field is
analytical rather than a camera-scrolling texture, avoiding swimming and update
bandwidth for the current strict sixteen-event budget. Vegetation remains owned by
PIXL's normal wind and Ground Response paths.

## Current milestone

- Compute-driven sparks, fragments, dust, embers and distinct fire, frost and
  shock populations.
- Material classification for stone, dirt, mud, snow, ice, grass, wood, metal,
  flesh, water, sand and ash using Skyrim collision material IDs with a guarded
  name fallback.
- Depth/normal collision with restitution, friction, bounded bounce count and a
  quality-scaled collision budget distributed across the particle pool.
- Surface-aware GPU stone chips and settling leaf particles for soft-surface
  shout/heavy-impact events, with bounded collision and lifetime budgets.
- Directional spark streaks and HDR-preserving additive highlights.
- Current particle coverage merged into PIXL's reconstruction reactive mask with
  a one-pixel edge expansion for DLSS/FSR/TAA stability.
- Radial, directional and travelling world-space impulses for GPU particle
  interaction, including current/previous-frame evaluation for temporal stability.
- Low, Medium, High and Ultra budgets, distance rejection, event caps and a true
  idle path after reactions expire.
- Developer test actions for metal, fire, frost, shock and shout scenarios plus
  separate GPU profiler passes for spawn, simulation, raster and composite.

The module defaults off while it remains experimental. Secondary event recursion
and real Hero Havok debris are deliberately hard-disabled. Their object
ownership, deduplication, save/load behavior and cell-transition lifetime must be
validated before they can safely touch Skyrim physics. Vegetation displacement is
not part of ReactiveFX; tree, branch and grass motion remain with the existing
PIXL vegetation systems.

## Integration and fallback behavior

- Projectile impacts reuse Ground Response's already-installed exact impact
  hook, including collision material, instead of installing a duplicate hook.
- The heavy particle pass runs after opaque deferred lighting and DistantLife,
  and before Rain Response.
- ReactiveFX does not bind a vertex-stage resource or alter `RunGrass`; Ground
  Response and PIXL vegetation resources retain ownership of that path.
- A shader or resource failure leaves the Skyrim scene unchanged and suppresses
  stale Reactive FX buffers.
- Disabling the feature clears particle, impulse and queued-event history before a
  later re-enable.

## Validation

- PIXLRenderer Release build: passed.
- ReactiveFX `SpawnCS`, `SimulateCS`, `BuildMaskCS`, `CompositeCS` and
  `WriteReactiveMaskCS`: passed
  strict FXC SM5 compilation with warnings treated as errors.
- RunGrass remains covered by the standalone grass shader test matrix; ReactiveFX
  no longer adds grass permutations.

Visual quality, event placement and GPU timings still require live in-game
capture before the feature is promoted from experimental status.

## 5 Future Visual Improvements

1. Surface-normal-oriented chip and shard shapes rather than point impostors.
2. Soft smoke volumes and bounded heat distortion integrated with atmosphere.
3. Branch-weighted shrub and tree response layered over existing wind.
4. Decal recipes for scorch, frost and chipped surfaces with strict lifetime caps.
5. Reconstruction-aware emissive and distortion masks for DLSS/FSR/TAA.

## 5 Future Performance Improvements

1. GPU alive-list compaction and indirect simulation/draw dispatch.
2. Half-resolution soft-particle raster for smoke while retaining full-resolution sparks.
3. Tile rejection for particle populations outside the active depth hierarchy.
4. Amortized material/profile lookup caches for high-frequency impacts.
5. GPU timing-driven budget adaptation within each user quality tier.

## 5 Future Feature / Research Ideas

1. Validated pooled Hero Havok debris with conservative secondary impacts.
2. Animation-event profiles for dragon landings, wing beats and giant impacts.
3. Data-authored quest and cutscene presentation profiles.
4. Shared impulse consumers for smoke, embers, snow and future fluid particles.
5. Authorable Dwemer, restoration, conjuration and illusion recipe libraries.
