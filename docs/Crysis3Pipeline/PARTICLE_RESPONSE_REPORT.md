# Particle-Derived Local-Light Response

## Scope

This release-safe pass audits PIXL's existing particle-derived fire, candle,
torch, sconce, brazier, bonfire, campfire, hearth, flame, ember, and burn
emitters. It does not add particle shadow maps, area-light resources, shader
defines, render passes, or a new GPU light representation.

## Existing Runtime Path

1. `BSBatchRenderer::RenderPassImmediately` hooks inspect eligible soft,
   depth-tested effect geometry.
2. `GetParticleLightConfig` rejects effects that already own Skyrim light data,
   requires a billboard owner, resolves optional `Data/ParticleLights` culling
   configuration, and recognizes a deliberately narrow set of incandescent
   texture-name profiles.
3. `QueueParticleLight` derives a world-space position, material/vertex tint,
   bounded profile radius, and intensity.
4. The next Radiant Grid prepass moves queued representatives into the normal
   CPU `LightData` list before its existing upload and cluster build/cull.
5. Lighting, effects, water, atmosphere, and other existing Radiant Grid
   consumers continue to read the unchanged `t35`/`t36`/`t37` resources.

Attached Skyrim lights remain authoritative. PIXL does not create an additional
particle light when `BSEffectShaderProperty::lightData` is present.

## Finding

The queue previously appended one `ResolvedParticleLight` for every eligible
immediate render submission. It had no owner identity or per-frame duplicate
test. The same billboard owner can be submitted through repeated immediate
paths or contain layered eligible geometry, so submission count could multiply
otherwise identical local-light energy and consume clustered-light capacity.
That makes brightness and light-list pressure depend on render plumbing rather
than the physical emitter.

The disabled path also returned before draining an already populated producer
queue. Turning particle lights off and later on could therefore replay stale
representatives for one update.

## Implemented Stabilization

`RadiantGrid` now maintains a CPU-only per-frame map from the eligible
billboard owner to its queued representative:

- the first submission creates the representative;
- later submissions from the same owner do not add energy;
- the brightest submitted tint is retained without summing layered colours;
- the largest submitted bound supplies the conservative radius and position;
- non-finite position, colour, alpha, or radius values are rejected;
- negative RGB is removed and alpha is constrained to its physical coverage
  range;
- the owner map is cleared atomically with the queue handoff; and
- the queue is drained even while the feature is disabled.

The aggregation is O(1) expected work per queued submission and allocates no
new persistent GPU resource. Separate billboard owners remain separate lights;
there is intentionally no distance-based merging that could fuse adjacent
torches or change authored spatial response.

## Compatibility and ABI

- `RadiantGrid::LightData` is unchanged.
- The HLSL `Light` structure and `MAX_CLUSTER_LIGHTS` contract are unchanged.
- No SRV, UAV, sampler, cbuffer, register, descriptor, shader define, or shader
  source changed.
- Existing settings and `Data/ParticleLights` first-win configuration behavior
  are unchanged.
- Existing particle culling behavior is unchanged.
- `pipeline/Radiant Grid/Module.ini` remains `3-2-0`; bumping it would only
  invalidate shader cache entries for a CPU-only behavioral correction.
- No live or staged runtime file was modified.

## Validation

- `git diff --check` passes for the two implementation files.
- Canonical Release `PIXLRenderer` build passes. The resulting DLL is
  19,609,088 bytes with SHA-256
  `CAB88F2DD4F3332199C37CC49D9788639406DC73340C476065A38C6371243D4E`.
- The `PIXL-Audit` target passes with all 38 integrated modules. The only build
  diagnostics are the inherited FidelityFX shared-intermediate `MSB8028`
  warnings.
- Shader compilation is not applicable because no HLSL or shader-facing
  contract changed.
- Runtime visual validation remains pending. Compare a single candle, held
  torch, multi-layer brazier/campfire, and a dense interior while orbiting the
  camera. Brightness should remain stable as draw paths change, separate nearby
  emitters must remain spatially distinct, configured culling must still work,
  and disabling/re-enabling particle lights must not produce a stale flash.

## Rejected Higher-Risk Changes

- Distance-based clustering across unrelated billboard owners was rejected; it
  can merge distinct authored fixtures and move their lighting centroid.
- Temporal history/smoothing keyed by raw scene pointers was rejected because
  object lifetime and cell transition behavior require a broader ownership
  contract.
- New shadow-casting particle lights were rejected because they would add
  passes, shadow policy, and substantial runtime cost.
- A new area-light/shape ABI was rejected for release polish. Effective-radius
  and future LTC work remain separate research items.

## Future Work

1. Add developer-only counters for raw submissions, aggregated owners, rejected
   non-finite inputs, and final uploaded particle representatives.
2. Capture runtime counts for candles, held torches, braziers, large fires, and
   heavily modded interiors before considering a configurable queue budget.
3. Investigate a lifetime-safe emitter identifier if temporal smoothing is
   still visually necessary after owner aggregation.
4. Profile conservative screen-space or spatial aggregation for unusually dense
   particle systems without merging separate fixtures.
5. Prototype finite-emitter specular broadening through existing radius/size
   data before considering an append-only shaped-light ABI.
