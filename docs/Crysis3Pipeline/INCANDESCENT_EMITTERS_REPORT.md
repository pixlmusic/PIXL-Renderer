# PIXL Incandescent Geometry Emitters

## Diagnosis

The supplied dungeon captures showed candle and brazier pixels reaching emissive composition and bloom while nearby stone, furniture, and characters received little or no corresponding illumination.

PIXL's existing Radiant Grid particle-light path only accepted supported `BSEffectShader` billboard geometry. The installed candle meshes instead use ordinary `BSLightingShader` glow-map materials such as `textures\smim\candles\Candles01.dds` with `Candles01_g.dds`. Those draws never entered the particle-light queue, so they could glow without creating a local-light record.

## Implementation

`RadiantGrid::BSLightingShader_SetupGeometry_Before` now observes ordinary lighting geometry and queues a bounded emitter only when all of the following are true:

- the material is an actual glow-map material;
- the glow texture slot is populated;
- the resolved diffuse/glow paths identify a candle, wick, torch, sconce, brazier, bonfire, campfire, hearth, fire, flame, ember, or burning surface;
- the existing **Enable Particle Lights** master setting is enabled.

Classification is cached per geometry and material identity. A material swap invalidates the cached decision naturally, and geometry destruction removes both particle and incandescent cache entries.

Each accepted source derives:

- world position from the geometry world-bound centre;
- bounded radius from its architectural family and world bound;
- warm default chromaticity appropriate to candle/torch/fire families;
- optional authored-colour preservation for deliberately coloured flames;
- bounded intensity response from the material emissive multiplier.

The emitter then uses the existing Radiant Grid list. It therefore participates in opaque local lighting, Material Forge response, particles, water, Atmosphere local-light scattering, and subsequent HybridGI radiance reuse without a new render pass.

## Safety

- No C++/HLSL shared ABI changed.
- No shader register, descriptor, resource, render target, or permutation define changed.
- No global emissive multiplier was raised.
- Windows, generic glow maps, magic, crystals, UI, and unrelated emissive materials are not promoted.
- Existing particle-owner deduplication and finite-value guards remain active.
- Developer statistics now distinguish particle emitters from glow-mapped geometry emitters.

## Validation

- Release `PIXLRenderer` build: PASS.
- `PIXL-Audit`: PASS, 38 integrated modules.
- Shader validation: not required; no HLSL changed.
- Runtime visual validation: pending.

## Runtime Acceptance

In the same dungeon scene:

1. candle groups should produce warm gradients on their table and adjacent stone;
2. the brazier should illuminate the nearby pedestal, wall, floor, and skeleton;
3. smoke/fog around the sources should receive coherent local scattering;
4. moving the camera must not cause lights to pop between candles;
5. windows and unrelated glowing objects must not begin emitting false local lights;
6. disabling **Enable Particle Lights** must restore the previous emissive-only behaviour.

The generated sources currently inherit the established Radiant Grid containment behaviour. Automatic geometry-derived portal/light clipping remains future work and should be validated separately for light leaking across thin walls.
