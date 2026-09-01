# World Force Field Foundation

## Scope and release classification

This pass implements the release-safe first step identified by the Crysis 3
cross-reference: one deterministic, resource-free world-space wind sampling
contract with explicit current/previous evaluation. It does **not** implement a
writable force texture, constraint-chain vegetation, collision impulses, a
compute dispatch, or a new runtime module.

Classification: **A — release safe** for the shared contract and the exact
Foliage compatibility adapter. A force grid or constraint solver remains
**C — experimental**.

## Existing PIXL paths audited

### Foliage Dynamics

- `distribution/Shaders/RunGrass.hlsl` receives Skyrim's `WindVector`,
  `WindTimer`, and `PreviousWindTimer` in its existing per-geometry buffer.
- Grass restores an absolute instance anchor with the current world matrix and
  `FrameBuffer::CameraPosAdjust`, then evaluates the accepted broad gust field
  independently at both engine timers. Skyrim's authored displacement remains
  structural; PIXL adds a bounded delta only.
- `distribution/Shaders/Lighting.hlsl` receives `TreeParams` and both
  `WindTimers`. The accepted legacy TREE_ANIM calculation already evaluates the
  current and previous lanes together. The optional multi-frequency block is
  compile-time disabled by default (`USE_PIXL_MULTI_FREQUENCY_TREE_WIND=0`) and
  was not activated by this work.
- `FoliageDynamics::Settings` remains the 80-byte global `b6` payload and the
  grass-only tuning block remains 96 bytes at `b13`. Wind direction and time are
  not duplicated into either structure.

### Hair Reconstruction

- The canonical/offline Hair Reconstruction source generates card motion from
  `SharedData::Timer`, a settings-supplied frame delta, absolute current and
  previous skinned positions, a fixed fallback wind axis, and Foliage Dynamics
  response tuning.
- Hair has no authoritative Skyrim wind-direction input in its Lighting draw
  contract. It therefore cannot truthfully share weather direction until a
  narrow CPU/runtime source is established.
- The current and previous hair samples use the same mathematical function, but
  both positions currently restore the current `CameraPosAdjust`. The existing
  actor-delta and teleport bounds reduce failure severity, but a future adapter
  must receive a matching previous-origin adjustment before claiming exact
  camera-rebase parity.
- Hair remains offline following its live rollback history. No Hair descriptor,
  shared-data layout, cache policy, shader entry point, or runtime file changed
  in this pass.

### Precipitation and particles

- Rain enhancement receives Skyrim's authored particle velocity. It derives a
  stable world-space travel axis from that velocity, restores absolute position
  with `FrameBuffer::CameraPosAdjust`, and evaluates clumping/gusts from
  `RainResponse::PerFrame::Time`.
- Snow enhancement receives the particle shader's existing `Wind` vector,
  restores absolute position, and evaluates drift/flutter/tumble from
  `SharedData::Timer`.
- Rain and snow currently emit no motion-vector target. They need only the
  current sample for raster placement; adopting a temporal pair becomes useful
  only if precipitation gains motion vectors or temporal simulation state.
- Generic non-precipitation particles retain Skyrim's existing `Wind` term and
  were intentionally not redirected through foliage math.

### Weather and time state

- PIXL has no renderer-wide CPU wind direction/velocity structure today.
  Direction is supplied independently by Skyrim's Grass, TREE_ANIM, and Particle
  per-draw buffers; Hair has no equivalent input.
- `State::timer` advances in seconds while gameplay is unpaused and is uploaded
  as `SharedData::Timer`. There is no global previous timer field.
- Rain Response maintains a separate pause-aware, millisecond-quantized timer in
  its existing 256-byte per-frame payload.
- Live rain/snow intensity comes from Skyrim sky/weather/precipitation state.
  Foliage response strength remains a user/material response rather than a
  canonical physical wind speed.

The important finding is that a shared **sampler** is safe now, while a shared
**state buffer** would require new authority, ABI, transition smoothing, and a
mapping from Skyrim's draw-local values. This pass therefore does not fabricate
one.

## Implemented contract

`pipeline/Foliage Dynamics/Kernels/FoliageDynamics/WorldWind.hlsli` defines the
resource-free `PIXLWorldWind` namespace:

- `Parameters` carries caller-owned direction, spatial scale, gust speed, and
  flutter speed;
- `FieldSample` returns normalized direction plus gust, signed crosswind, and
  signed flutter channels;
- `Evaluate` is a pure function of absolute world XY, seconds, parameters, and a
  stable caller-owned seed;
- `TemporalPair` and `EvaluateTemporalPair` evaluate current and previous world
  positions/times through the identical function;
- `Hash12`, `ValueNoise2D`, and `SafeDirection` centralize deterministic,
  numerically guarded primitives.

The contract deliberately excludes amplitude, stiffness, mass, root/tip masks,
wet weighting, and material response. Those are consumer semantics: sharing
them would make grass, trees, hair, rain, and snow move identically rather than
coherently.

### Coordinate contract

```text
camera-relative current position  + matching current origin  -> current absolute world position
camera-relative previous position + matching previous origin -> previous absolute world position

current absolute position  + current seconds  -> Evaluate(...)
previous absolute position + previous seconds -> Evaluate(...)
```

The sampler never reads a camera matrix, screen UV, frame count, random texture,
resource, or hidden global time. Camera motion therefore cannot rotate the field
and jitter cannot change its phase.

### Foliage compatibility adapter

`FoliageWind.hlsli` now forwards its hash, value-noise, safe-direction, and grass
gust evaluation to the shared contract. Its public `FoliageWind` functions and
`GrassGustField` remain unchanged, so `RunGrass.hlsl` requires no source change.

The contract intentionally reproduces the accepted Foliage Dynamics 2.x field
constants and operation order. A strict before/after compile of the active
RunGrass Foliage vertex variant produced byte-identical 28,284-byte DXBC with
SHA-256:

`5E44A1BA03B12907A42AA63A935251D82253548C4975A03DC026A4357F40AB4A`

This is stronger than a visual-equivalence assumption: the active consumer's
compiled program is unchanged.

## ABI, resource, and cache impact

- Source-of-truth check: before this edit, canonical and live FoliageWind were
  the same Git blob (`4bc00a869f5f1a7f5f8358ed6010963e9de4a770`). Canonical
  Rain precipitation and Hair kernels also matched their current live copies.
  The repository `pipeline/` + `distribution/` tree was therefore used as the
  authoritative implementation; the new canonical wind files were not copied
  back to live Data.
- No C++ structure, HLSL cbuffer, pack offset, register, SRV, UAV, sampler,
  render target, shader define, draw, pass, or dispatch was added or changed.
- No module descriptor was bumped. The only active consumer compiles to
  identical bytecode.
- The shared include is stored under the existing `FoliageDynamics` runtime
  kernel directory rather than as an unowned top-level/Common file. PIXL's file
  watcher can therefore attribute it to Foliage Dynamics and retain its
  family-scoped fallback instead of treating an unowned include with no known
  dependencies as grounds for a global pipeline clear.
- Normal include-dependency tracking still invalidates any future Hair or
  Particle entry point that explicitly consumes the contract.
- No live `Data\Shaders`, pipeline library, beta staging, configuration, DLL, or
  cache file was modified or deployed.

## Validation

Windows SDK 10.0.26100 FXC passed with `/Ges /WX /O3`:

| Variant | Profile | Result |
| --- | --- | --- |
| Shared current/previous contract harness | `cs_5_0` | PASS, 612 bytes |
| RunGrass + Foliage Dynamics | `vs_5_0` | PASS, 28,284 bytes; byte-identical before/after |
| RunGrass + Foliage Dynamics + Ground Response | `vs_5_0` | PASS, 47,388 bytes |
| RunGrass + Foliage Dynamics pixel path | `ps_5_0` | PASS, 43,664 bytes |
| Particle rain + Rain Response | `vs_5_0` | PASS, 28,088 bytes |
| Particle snow + Rain Response | `vs_5_0` | PASS, 10,840 bytes |
| Isolated canonical Hair motion path | `vs_5_0` | PASS, 21,704 bytes |

The standalone Hair harness is used because raw standalone compilation of the
whole Lighting vertex entry point exposes its known dual-`b12` harness conflict
between `FrameBuffer::PerFrame` and `VS_PerFrame`. PIXL's shipping permutation
compiler owns that entry contract; this work does not change it.

Runtime visual validation is not required to prove the active Foliage result is
unchanged because its DXBC is byte-identical. Runtime adoption by Hair or
precipitation remains pending and must be A/B tested when those consumers are
actually connected.

## Rejected changes

- No 3D force grid, UAV, clipmap, compute dispatch, impulse injection, or
  constraint chain was introduced during release polish.
- No global wind cbuffer was appended to `SharedData`; there is not yet one
  authoritative CPU wind direction and transition policy to upload.
- Hair was not connected speculatively. It lacks an authoritative draw-level
  weather direction and is currently isolated from the live baseline.
- Rain and snow were not retuned to the foliage waveform. Their authored
  velocity, density, visibility, and tumble calibrations are already distinct.
- TREE_ANIM's dormant multi-frequency compile path was not enabled.
- Module versions were not bumped for source-only indirection that produces
  identical active bytecode.

## Adoption plan

1. Establish a read-only CPU `WindState` from Skyrim weather/sky state with
   explicit units, normalized direction, transition interpolation, current and
   previous seconds, and interior/calm fallback behavior.
2. Prove it agrees with Grass/Particle per-draw values across weather mods before
   exposing it through an append-only or dedicated buffer.
3. Connect offline Hair first through an adapter, retaining its existing
   response and wet/damping amplitudes. Add matching previous-origin data or a
   rebase reset before enabling motion vectors.
4. Connect precipitation only if shared storm coherence is visibly beneficial;
   authored particle velocity remains the primary direction.
5. Consider a low-resolution writable force grid only after the analytical
   contract is runtime accepted and profiling identifies interactions that an
   analytical field cannot express.

## 5 Future Visual Improvements

1. Blend an authoritative weather direction with locally stable terrain/eave
   deflection while preserving broad storm coherence.
2. Add height-dependent shear so canopy, grass, hair, rain, and snow share one
   storm direction but retain plausible response at their physical scale.
3. Introduce bounded actor/explosion wake impulses only after stable world-space
   producer identity exists.
4. Couple snow tumble and rain streak lean to the shared field without replacing
   authored precipitation velocity.
5. Add developer visualization of direction, gust envelope, crosswind, flutter,
   current/previous delta, and camera-rebase confidence.

## 5 Future Performance Improvements

1. Evaluate broad field terms once per stable object/instance and interpolate
   them across vertices where author data permits.
2. Share hash/value-noise results when a consumer needs several response bands
   at the same absolute anchor.
3. Skip flutter evaluation for rigid/root vertices and distance-retired cards.
4. Quantize distant analytical updates temporally only with interpolation and
   measured TAA/DLSS stability.
5. If a force grid is eventually justified, use a camera-following toroidal
   volume with dirty slabs and bounded update cadence rather than a full clear.

## 5 Future Feature / Research Ideas

1. A coarse 3D force clipmap with analytical far-field fallback.
2. Time-sliced patch-level vegetation constraints inspired by Crysis 3, isolated
   behind a quality tier and strict displacement bounds.
3. Terrain/building-aware wind shadowing from a low-resolution occupancy source.
4. Stable gameplay-force producers for explosions, projectiles, actors, and
   large creatures with lifetime-safe identifiers.
5. A cross-system weather diagnostics panel comparing Skyrim Grass, Particle,
   TREE_ANIM, and proposed renderer wind state in common units.
