# PIXL Hair Reconstruction (retired experiment)

> Release status: this experimental module is not registered or shipped. PIXL uses
> the stable Strand Shading module for hair. This document is retained as engineering
> research and does not describe an active user-facing feature.

Hair Reconstruction is an integrated PIXL Renderer module that upgrades Skyrim's
existing hair cards at runtime. It requires no groom files, NIF patches, XML,
FormID lists or external hair/physics mod.

The module follows PIXL's normal design rule: observe what Skyrim is already
rendering, classify it conservatively, and enhance only the paths for which enough
evidence exists.

## Architecture

```text
Rendered Lighting geometry
  -> conservative material + hierarchy classifier
  -> native / automatic / diagnostic confidence tier
  -> Lighting VS: root/flex inference + current/previous secondary motion
  -> Lighting PS: direction reconstruction + card micro-fibres + wet material
  -> existing PIXL Strand Shading BRDF
  -> existing Actor Surface Effects snow/wet composition
  -> existing motion/depth/deferred outputs
  -> TAA / DLSS / FSR / frame-generation pipeline
```

The implementation is deliberately integrated into `Lighting.hlsl`. Creating a
second hair render pass would duplicate skinning, fight Skyrim transparency order
and require a separate velocity/depth path.

## Detection algorithm

Hair is classified in three tiers:

1. **Native** — Skyrim's Hair Lighting technique is authoritative.
2. **Automatic** — a normal skinned alpha Lighting material with strong hair,
   beard, moustache, sideburn, ponytail or braid evidence can be upgraded.
3. **Candidate** — uncertain material remains unchanged but can be displayed in
   the Hair Detection debug view.

Explicit face/skin/eye/mouth, helmet/hood/armour/cloth and furniture evidence is
rejected. The default threshold favors false negatives over damaged transparent
materials. No hairstyle or plugin name is hard-coded.

## Direction inference

An authored hair flow map remains authoritative. Otherwise the shader:

1. begins with the existing geometry bitangent;
2. reconstructs increasing texture-V direction from world-position and UV
   derivatives;
3. projects it onto the card plane;
4. aligns it to the authored tangent to prevent sign flips;
5. blends it by the Direction Inference control.

Degenerate UVs retain the authored direction. All normalization paths have finite
length guards.

## Hair lighting

Hair Reconstruction reuses PIXL Strand Shading's existing Kajiya-Kay/Marschner
implementation. It supplies better direction data, bounded transmission control,
virtual fibre breakup, rain/water absorption and a wet dielectric roughness target.
It does not replace the original albedo or turn hair metallic.

This preserves original colour and hairstyle identity while improving anisotropic
highlight flow, backlight and material depth.

## Simulation

The release implementation uses deterministic shader motion rather than a CPU
physics object per NPC. Root/tip and flexibility are approximated from UV location,
card edges and stable model-position variation. Motion responds to:

- PIXL's existing world-space vegetation wind/gust state;
- actor current/previous movement;
- damping and motion-strength settings;
- rain/water weight;
- screen-distance LOD.

Short/root regions remain restrained. Long/card-edge regions receive more motion.
Large actor deltas are rejected to avoid teleport, camera-rebase and ragdoll spikes.

## Temporal integration

The same motion function is evaluated for current and previous skinned positions
using the real frame delta. Those positions feed Lighting's existing motion-vector
calculation. Virtual fibre detail is UV anchored and derivative filtered.

This is the key difference from adding a camera-space animated hair overlay: PIXL's
reconstruction paths receive actual secondary hair velocity.

Runtime verification is still required for TAA, DLSS and the supported frame-
generation path. Static compilation proves shader validity, not absence of visual
ghosting on every hairstyle.

## Weather integration

Hair uses existing PIXL state only:

- Rain Response precipitation/wetness drives damp-to-wet response.
- Water data immediately supplies submerged wetness.
- Foliage Dynamics supplies world-space wind/gust timing.
- Actor Surface Effects remains responsible for snow accumulation and melting.

No second weather detector, camera-relative wind direction or independent snow
mask is introduced.

## Quality levels

- **Off**: runtime automatic-hair pixel classification is stripped and all helpers
  are inert; native hair returns to its established Strand Shading baseline.
- **Low**: classification, inferred direction, BRDF/environmental material only.
- **Medium**: Low plus restrained secondary motion. This is the clean-install
  default; procedural card-edge fibres are off.
- **High**: stronger filtered fibre detail, longer simulation range and limited
  edge reconstruction.
- **Ultra**: highest bounded virtual fibre density and simulation distance.

The Characters grouped-quality control maps to these tiers.

## GUI and diagnostics

The Hair Reconstruction module page contains:

- Appearance: anisotropic direction, strand detail, transmission, silhouette.
- Dynamics: secondary motion, wind response, strength, damping and distance.
- Environment: wet response and snow compatibility.
- Advanced: procedural micro-fibres, density and classifier thresholds.
- Debug: Detection, Direction, Root/Tip, Flexibility, Motion, Temporal Confidence
  and Micro-Fibres.

Every exposed field is serialized and consumed by either CPU classification,
quality mapping or HLSL. Frame delta and alignment padding are transient and are
not serialized.

## Caching and memory

The current implementation intentionally requires no per-mesh topology cache:
classification is attached to stable shader descriptors, while direction,
root/flexibility and fibre detail derive from data already present in each draw.

Consequently it adds no persistent actor pointers, mesh references, SRVs/UAVs,
render targets or unbounded allocations. Equipment changes and cell transitions
use newly constructed render passes and cannot leave a stale actor-resource cache.

## Compatibility

- Vanilla native Hair technique: authoritative enhancement.
- Ordinary mod hair on skinned alpha Lighting materials: automatic when evidence
  clears the threshold.
- Beard/facial hair: material enhancement; UV/root weighting keeps motion subtle
  unless the source cards clearly expose flexible tips.
- Eyebrows/lashes: conservative candidate by default, not automatically moved.
- Helmets/hoods: rejected by material evidence.
- Unsupported or ambiguous transparent geometry: original rendering.
- External SMP: not required; future coexistence needs live detection to avoid
  applying two secondary-motion systems to the same hair.

## Performance

Expected cost is negligible when no accepted hair is visible. Accepted hair adds
ALU and a few derivatives in existing Lighting draws, with no new draw call. Motion
is distance bounded. High/Ultra increase virtual-fibre work and simulation range.

Exact average/p95/p99 GPU cost is not claimed without live capture. PIXL's Pulse
Profiler measures the integrated work inside Lighting. The low-frequency CPU
classifier is intentionally not exposed as a fake per-frame/per-draw GPU pass.

## Current limitations

- UV V is the primary automatic root-to-tip convention; unusual atlases may invert
  or rotate it.
- There is no full topology/geodesic analysis or semantic lobe graph.
- Procedural strands are filtered virtual fibres within/at existing cards, not
  additional ribbon geometry.
- No dedicated hair OIT, collision primitives or persistent spring simulation.
- Snow-on-hair quality depends on Actor Surface Effects exposure/contact state.
- Visual and temporal acceptance remains pending live Skyrim testing.

## Future work

The safest next research steps are mesh-lifetime-safe topology caching, optional
per-component spring lobes, SMP double-motion detection, scalp-derived attachment
volumes, sparse generated edge ribbons, dedicated transparency ordering and
hair-specific shadow/strand visibility terms. None is required by the current
vanilla baseline.
