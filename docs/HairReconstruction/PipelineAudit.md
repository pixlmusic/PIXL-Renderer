# Hair Reconstruction Pipeline Audit (historical)

> This audit documents a retired experiment. It is not an active PIXL module.

## Scope and source of truth

This audit traces the active PIXL Renderer build rather than assuming a historical
upstream layout. The implementation was checked through the render-pass
descriptor path, shader cache, shared pipeline buffer, Lighting vertex/pixel
shaders, Strand Shading, Actor Surface Effects, weather data and quality/menu
registration.

## Existing actor/hair path

```text
NiAVObject / BSTriShape
  -> BSLightingShaderProperty::GetRenderPasses
     -> Lighting technique + descriptor flags
     -> MaterialForge runtime material inspection
  -> State::ModifyShaderLookup
  -> ShaderCache::GetLightingShaderDefines
  -> Lighting.hlsl
       VS: current + previous skinning palettes -> world positions -> motion
       PS: card alpha/base colour/normal -> material -> direct/indirect lighting
       PS: Strand Shading hair tangent + Kajiya-Kay/Marschner evaluation
  -> deferred/forward lighting outputs + motion-vector target
  -> PIXL temporal reconstruction / TAA / DLSS / FSR path
```

Hair Reconstruction is inserted into this existing Lighting path. It does not
create a parallel character pass or replace Strand Shading.

## Active files and responsibilities

| Area | Active source | Relevant responsibility |
| --- | --- | --- |
| Render-pass classification | `engine/MaterialForge.cpp` | Has live geometry, shader property, material, hierarchy and final render-pass descriptor. |
| Material evidence | `engine/MaterialForge/PhysicalMaterialRegistry.*` | Resolves diffuse texture paths and conservative hair/fur confidence. |
| Descriptor metadata | `engine/ShaderCache.h/.cpp` | Converts PIXL-owned descriptor bits into HLSL permutation defines. |
| Descriptor normalization | `engine/State.cpp`, `engine/MaterialForge.cpp` | Separates vertex/pixel flags and preserves PIXL classification bits. |
| Module lifecycle | `engine/RenderModule.cpp`, `engine/Globals.*` | Constructs and registers optional integrated modules. |
| Settings upload | `engine/PipelineBuffer.cpp` | Packs module state into FeatureData once per frame. |
| CPU/GPU ABI | `engine/Modules/HairReconstruction.h`, `distribution/Shaders/Common/SharedData.hlsli` | Matching 128-byte, eight-register settings layout occupying the historical reserved post-process block in place. Existing later FeatureData offsets and the total buffer size remain unchanged. |
| Card rendering | `distribution/Shaders/Lighting.hlsl` | Current/previous skinned positions, base colour/alpha, direction and material integration. |
| Hair scattering | `pipeline/Strand Shading/Kernels/Hair/Hair.hlsli` | Existing PIXL hair BRDF remains authoritative. |
| Contamination | `pipeline/Actor Surface Effects/Kernels/ActorSurfaceEffects/ActorSurfaceEffects.hlsli` | Existing snow/wet/mud actor-local material composition. |
| Weather/wind | shared `RainResponseSettings`, `FoliageDynamicsSettings`, `WaterData` | Existing world state; no duplicate weather detector or camera-space wind. |
| Reconstruction | `Lighting.hlsl` motion output and current/previous positions | Secondary motion is evaluated for both frames before the existing velocity calculation. |
| User interface | `HairReconstruction::DrawSettings`, `PIXLRendererPage.h` | Serialized module page and Characters quality mapping. |

## Data available at classification time

The active `BSLightingShaderProperty_GetRenderPasses` hook provides:

- the `BSGeometry` and its parent `NiAVObject` hierarchy;
- the `BSLightingShaderProperty` and `BSLightingShaderMaterialBase`;
- resolved diffuse texture identity;
- current Lighting technique;
- skinned, alpha and material/render flags;
- stable render-pass descriptors used by the shader cache.

It does not expose a cheap, universally reliable CPU copy of every skinned card's
topology, connected components or scalp bones. Reading those from arbitrary modded
meshes on the render thread would add lifetime and synchronization hazards. The
release-safe implementation therefore uses strong material/hierarchy evidence,
then performs direction/root/flexibility reconstruction in the shader from card UV,
current geometry derivatives and the existing tangent frame.

## Hair identification

### Authoritative tier

Skyrim's native Lighting `Hair` technique is accepted as hair. It receives the
module's material, direction and (when skinned) restrained motion path.

### Automatic tier

Ordinary mod hair may be upgraded when all of these are true:

1. it is skinned Lighting geometry;
2. it has an alpha property;
3. it uses the ordinary Lighting technique rather than an unrelated specialist
   technique;
4. material/hierarchy evidence clears the configurable detection threshold.

Strong evidence includes hair, beard, moustache, sideburn, ponytail and braid
resource vocabulary. Brow/lash/whisker evidence is deliberately below the default
accept threshold and is diagnostic-only. Skin, face, eye, mouth, helmet, hood,
armour, cloth and furniture texture evidence rejects the candidate. Texture names
are evidence, not a FormID or mod allow-list.

### Diagnostic tier

Plausible but rejected skinned-alpha candidates receive a shader-cache diagnostic
bit. They keep original rendering unless the Hair Detection debug view is active.
False negatives are therefore safer than false positives.

## Vertex data and motion

Lighting exposes model position, UV, bone weights/indices, current bone palette and
previous bone palette. PIXL already calculates exact current and previous skinned
world positions. Hair Reconstruction adjusts both positions with the same stable
world-space function evaluated at current and previous time.

The deformation combines:

- UV-derived root-to-tip weight;
- card-edge flexibility;
- stable model-position variation;
- PIXL world-space wind strength, gust speed and flutter speed;
- bounded actor current/previous displacement for subtle inertia;
- wet-weight damping;
- distance LOD and a teleport/rebase guard.

Because modified positions feed the existing Lighting motion-vector calculation,
secondary movement is not represented by actor skeletal velocity alone.

## Direction reconstruction

The existing path first selects an authored hair flow map when present, otherwise
the geometry bitangent, then applies Strand Shading's tangent reorientation.

Hair Reconstruction preserves an authored flow map. Without one it reconstructs
the direction of increasing texture V using screen derivatives of world position
and UV, projects that vector onto the card plane, aligns its sign to the authored
tangent and blends by `DirectionBlend`. Degenerate UV/tangent cases use robust
length guards and retain the authored tangent.

## Material and lighting

The module augments, rather than duplicates, PIXL Strand Shading:

- inferred direction drives the existing anisotropic hair response;
- Strand Shading continues to own Kajiya-Kay/Marschner lobes, direct/indirect
  lighting and self-shadowing;
- `Transmission` scales the existing Marschner transmission contribution;
- derivative-filtered virtual fibre variation breaks up card-uniform albedo;
- rain/water darkens hair and moves roughness toward a bounded wet dielectric;
- metallic is forced off only on accepted hair;
- Actor Surface Effects remains the owner of snow accumulation, with a module
  compatibility toggle for hair.

## Transparency and silhouette

No global OIT replacement was introduced. Existing Skyrim alpha test/blend and
depth ordering remain intact. High/Ultra can apply subtle derivative-filtered alpha
edge breakup to existing card coverage. The effect is bounded and off in the
default Medium preset. It does not emit independent geometry, so it retains the
existing depth, animation and reconstruction behavior.

## Temporal/reconstruction boundary

```text
current bones + previous bones + deterministic current/previous hair motion
    -> Lighting world positions
    -> existing clip-space velocity calculation
    -> Lighting motion-vector target
    -> PIXL reconstruction inputs
    -> TAA / DLSS / FSR and optional frame-generation consumers
```

Virtual fibre and silhouette terms are UV/object anchored and filtered by `fwidth`.
There is no screen-space persistent hair mask. The debug Temporal view displays
classification confidence and motion magnitude for live verification.

## Quality scaling and cost model

| Quality | Behavior |
| --- | --- |
| Low | Detection, direction/material enhancement; no secondary motion or silhouette breakup. |
| Medium | Low plus restrained secondary motion and wet response; procedural edge fibres remain off by default. |
| High | More fibre detail, longer simulation distance and limited virtual silhouette fibres. |
| Ultra | Highest bounded virtual fibre density, motion distance and silhouette detail. |

There are no new render targets, UAVs, draw calls, compute dispatches or persistent
per-actor allocations. GPU cost is extra ALU/derivatives only on accepted hair
permutations. CPU classification occurs only when render passes are constructed.
Integrated Lighting work remains measured inside PIXL's Lighting category because
it is not a separable pass; no misleading synthetic sub-pass marker is emitted.

## Deliberately deferred unsafe/experimental work

- True ribbon/line geometry generation and indirect strand draws.
- A topology/connected-component cache keyed to arbitrary mod mesh lifetimes.
- Per-lobe spring state and skeleton collision primitives.
- Weighted blended OIT dedicated to hair.
- Per-strand self-shadow maps.

These need live temporal/performance evidence and stronger mesh-lifetime ownership.
The current hybrid card reconstruction supplies the release-safe P0-P6 foundation
without making those experiments prerequisites for vanilla compatibility.
