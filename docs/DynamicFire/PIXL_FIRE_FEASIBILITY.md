# PIXL Dynamic Fire — Feasibility Review

## Executive finding

PIXL can reproduce and materially improve the player-facing behaviour of the Unity reference under DX11. The blockers are not flame rendering, compute simulation, weather, or clustered light injection. The two architectural uncertainties that must be proven before production work are:

1. a stable per-reference identity and coordinate mapping for arbitrary static Skyrim geometry whose meshes/materials are commonly shared; and
2. a clean render-stage/mask contract for custom translucent fire and smoke under DLSS/FSR/frame generation.

Those uncertainties are bounded and prototypeable. They justify a deliberate prototype gate, not a no-go.

## Current PIXL facilities inspected

| PIXL area | Files/functions inspected | Relevant capability |
| --- | --- | --- |
| Module lifecycle | `engine/RenderModule.h/.cpp`, `engine/Deferred.cpp`, `engine/Globals.cpp` | Ordered module registration; setup/reset/early/reflection/prepass hooks; settings and GUI conventions |
| D3D11 resources | `engine/Buffer.h`, existing compute modules | Constant/structured buffers, SRV/UAV textures, compute dispatch, named resources and safe module fallback patterns |
| Per-draw material interception | `engine/MaterialForge.cpp`, `engine/Modules/ActorSurfaceEffects.cpp`, `engine/State.cpp` | `BSLightingShader::SetupGeometry` hook chain; draw-specific b13 rebinding; material observation |
| Material records | `engine/MaterialForge/PhysicalMaterial*.h/.cpp` | Stable session material IDs and semantic textures; current traits include fur but not general burnability |
| Actor-local effects | `engine/Modules/ActorSurfaceEffects.h/.cpp`, `pipeline/Actor Surface Effects/Kernels/ActorSurfaceEffects/*.hlsli` | Bounded event API, temporal state, per-actor local lobes, nearby-actor budget, equipment/first-person ownership handling |
| Ground interaction | `engine/Modules/GroundResponse.h/.cpp`, `pipeline/Ground Response/Kernels/GroundResponse/*` | Actor/object/projectile/magic/shout event gathering, surface material classification, persistent world-space fields |
| Weather/wetness | `engine/Modules/RainResponse.h/.cpp`, `engine/WeatherManager.h/.cpp`, `engine/SeasonIntegration.h/.cpp` | Live rain/wetness, weather transitions, season context, outdoor state; no need to infer weather from season |
| Wind | `engine/Modules/FoliageDynamics.h/.cpp`, `pipeline/Foliage Dynamics/Kernels/FoliageDynamics/FoliageWind.hlsli`, Skyrim particle inputs | Stable world-space gust math with deterministic current/previous evaluation; weather wind must be exposed through a shared snapshot rather than copied from foliage code |
| Local lighting | `engine/Modules/RadiantGrid.h/.cpp`, `pipeline/Radiant Grid/Kernels/RadiantGrid/*` | 3D clustered local lights, particle-emitter discovery, light buffer upload, natural attenuation, room/portal handling |
| GI | `engine/Modules/HybridGI.h/.cpp`, `pipeline/Hybrid GI/Kernels/HybridGI/*` | Full-resolution radiance input and world cache; fire emission can enter GI through radiance if ordered correctly |
| Reconstruction | `engine/Modules/ImageReconstruction.h/.cpp`, `pipeline/ImageReconstruction/Kernels/ImageReconstruction/EncodeTexturesCS.hlsl`, `pipeline/ImageReconstruction/Kernels/ImageReconstruction/RCAS/RCAS.hlsl` | DLSS/FSR reactive and transparency masks, motion vectors, depth, camera-cut resets, unstable-surface-aware sharpening |
| Skyrim particles | `distribution/Shaders/Particle.hlsl`, Rain Response precipitation helpers | Existing particle draw path, world-space precipitation deformation and clustered lighting; not a general PIXL GPU-particle allocator |
| Debug/profiling | module overlays, PulseProfiler, PIXL D3D naming | A Dynamic Fire diagnostic overlay and pass timings fit existing conventions |

## Existing capability versus missing capability

### Already present

- DX11 compute, structured buffers, SRV/UAV management, and bounded resource ownership.
- A renderer module lifecycle and JSON/ImGui settings pattern.
- Hooks capable of identifying actors and modifying their material shading per draw.
- Event producers for projectile/magic/ground interaction.
- Weather, precipitation, wetness, season, and world-space wind ingredients.
- Clustered renderer-native local illumination and particle-light inference.
- Temporal reconstruction inputs and masks.
- Material semantic tables for current legacy/PBR surfaces.

### Must be added or proven

- Stable world-reference-to-render-draw identity for arbitrary non-actor geometry.
- A burnability material trait/classifier with user overrides and conservative defaults.
- Active-fire manager, sparse spatial index, sleeping/eviction policy, and optional save representation.
- Object-local burn state beyond actor-only analytical lobes.
- A renderer-owned GPU flame/smoke pass or a safe controlled extension of Skyrim particles.
- Explicit fire contribution to reconstruction masks and, for near geometry, motion vectors.
- Aggregate fire-light producer API for Radiant Grid.
- A surface-state composition include for non-actor Lighting permutations.

## Unity mechanism to PIXL mapping

| Unity implementation | Underlying behaviour | PIXL equivalent |
| --- | --- | --- |
| `MonoBehaviour.Update()` per flammable object | Advance only active fire state | One `DynamicFireManager` update over a fixed active set; sleeping states and variable cadence |
| `Physics.OverlapBox` per fire trigger | Discover nearby eligible receivers | One spatial hash/grid of active fire nodes and recently visible eligible references; event-driven Havok/projectile contacts where available |
| `TryToSetOnFire` | Accumulate ignition energy | Thread-safe `SubmitIgnitionEvent` queue; temperature/ignition scalar per active reference |
| `IncrementalExtinguish` | Local suppression/cooling | `SubmitSuppressionEvent` with water/frost/rain/volume source and energy |
| `Material.SetFloat/Vector` | Make state visible to material shader | Per-draw reference state binding for prototype; later structured buffer or sparse state atlas |
| Spherical `Fire_origin/Fire_spread` | Localized growing burn front | Multiple object-local analytical fire nodes for MVP; sparse mask/atlas for high-quality near objects |
| Global `Charred_wood_blend` | Progressive substrate damage | Local state channels: heat, active burn, char/ash, moisture |
| VFX Graph | GPU spawn/update/cull/render of particles | PIXL pooled compute particles plus instanced billboards/flipbooks; distant impostor/emissive fallback |
| VFX box and mesh sampling | Emit near burning surface | Spawn anchors from active fire nodes and conservative object bounds; mesh-surface sampling is a later enhancement |
| Unity child lights | Fire illuminates world | Aggregate nearby fire nodes into bounded Radiant Grid `LightData` producers; no Skyrim light per particle |
| WindZone sampler | Wind bends flame/smoke | Shared immutable PIXL `WindContext`; deterministic current/previous field evaluation |
| Unity Terrain tree conversion | Activate vegetation close to fire | Bounded vegetation-reference activation/classification; never mutate terrain/tree data |
| UnityEvents | Lifecycle notification | Internal typed event bus; Papyrus/gameplay bridge only if later justified |
| Component existence | Burnability authoring | Conservative material/reference classifier + explicit allow/deny rules + optional mod-authored metadata |
| No persistence | Runtime-only state | MVP resets on unload; optional compact active-reference serialization in a later phase |

## Subsystem feasibility ratings

| Subsystem | Rating | Reason |
| --- | --- | --- |
| Generic ignition/suppression event API | **Directly portable concept** | ActorSurfaceEffects and GroundResponse already prove queued localized event patterns |
| Active-fire lifecycle and budgets | **Directly portable concept** | Ordinary C++ state with elapsed-time integration and LRU/sleeping policy |
| Unity spherical surface burn | **Directly portable concept** | Straightforward HLSL, but should use multiple soft nodes and robust math |
| Local persistent char on arbitrary objects | **Requires redesign** | Needs stable per-reference draw identity and object-local mapping; shared materials cannot carry state |
| Flame/ember rendering | **Requires redesign** | DX11 GPU particles are viable, but VFX Graph cannot be ported and render ordering/masks must be designed |
| Smoke | **Possible with limitations** | Billboard/flipbook smoke is viable; volumetric self-shadowed smoke is expensive and reconstruction-sensitive |
| Steam bursts | **Directly portable concept** | Event-driven pooled particles |
| Conventional light per fire | **Not required** | Radiant Grid aggregate lights are more scalable |
| Renderer-native fire radiance | **Possible with limitations** | Clustered light injection is direct; GI response depends on pass ordering and radiance capture |
| Cross-object propagation | **Requires redesign** | Use sparse heat nodes/spatial hash, not per-trigger overlap polling |
| Topology-accurate surface spread | **Impractical** for initial release | Skyrim lacks universal authored UV adjacency and stable unique meshes; reserve for selected authored assets |
| Weather/wetness response | **Directly portable concept** | Existing rain/wetness/weather snapshots are available |
| Spatial wind response | **Directly portable concept** | Existing deterministic world-space field math can be shared |
| Snow melt near fire | **Possible with limitations** | GroundResponse already accepts elemental height deltas; must budget and avoid duplicate spell/fire stamping |
| Vegetation ignition | **Possible with limitations** | Classification and stable instance tracking are more difficult than rendering |
| Gameplay destruction | **Not required** | Renderer feature should not replace Skyrim gameplay/destruction systems |
| Save persistence | **Possible with limitations** | Compact active-reference state is possible; UV masks/textures should not be serialized directly |
| Unity assets | **Possible with limitations** | Technically convertible, but most supplied visual assets lack proven redistribution terms |

## Implementation-level feasibility

### Level A — dynamic surface burning

**Feasible as an isolated prototype.** A soft union of several object-local analytical nodes can drive heat, glow, char, roughness, and normals without a texture atlas. The gating uncertainty is binding the correct reference state to each static-world draw when the same mesh and material are instanced many times.

For selected actors, the existing ActorSurfaceEffects ownership route is already proven. For arbitrary static geometry, do not key state only by material pointer, shader property, mesh name, or texture because those can be shared.

### Level B — dynamic flames and smoke

**Feasible with a PIXL-owned pooled particle path.** Compute-managed particle records and instanced camera-facing quads are well within D3D11. The first version should use flipbooks, soft depth intersection, limited collision, deterministic wind, and strict budgets. It should not attempt Unity VFX feature parity.

Reusing Skyrim particle geometry avoids a custom renderer but gives PIXL weak control over spawning, per-particle state, motion vectors, masks, and persistence. It is useful for detecting ignition sources, not recommended as the core Dynamic Fire renderer.

### Level C — propagation

**Feasible using a sparse hybrid, not a world cellular simulation.** CPU tracks active references and broad-phase fire nodes. GPU evolves per-reference near masks later. Spatial hashing discovers a small set of nearby eligible receivers at a reduced rate. Propagation energy is distance/material/moisture/wind weighted and capped per update.

### Level D — environmental interaction

**Feasible incrementally.** Rain/wetness resistance, wind advection, snow melt stamps, and renderer-native light are high-value. Enclosed-space oxygen, full heat transfer, water volume flow, and structural destruction should remain out of initial scope.

## Fire-state representation alternatives

| Representation | Fidelity | CPU/GPU/VRAM | Compatibility | Assessment |
| --- | --- | --- | --- | --- |
| Per-object scalar | Low | Very low | High | Useful only for distant LOD |
| Object-local analytical nodes | Medium-high | Low, fixed node count | Medium-high if draw identity works | Best MVP and fallback |
| Per-object 2D burn texture | High on good UVs | Can become expensive; shared/overlap UV problems | Medium-low | Use only for selected/high-confidence meshes |
| Sparse atlas of local masks | High | Bounded pool; update bandwidth | Medium | Best later near-field path after identity/mapping prototype |
| World-space 2D field | Medium on terrain | Clipmap memory and projection artifacts | High for ground, poor for vertical/stacked surfaces | Ground-only interaction, not universal object burning |
| World-space 3D field/clipmap | Medium-high | High memory/bandwidth; leakage | Medium | Too costly/complex for first release |
| Sparse fire/heat nodes | Medium | Very low and scalable | High | Best broad simulation/lighting/particle anchor representation |
| Hybrid nodes + sparse local atlas | High | Bounded and scalable | Medium-high | Recommended production architecture |

## Material classification feasibility

Current MaterialForge records physically meaningful BRDF semantics and detects fur through conservative texture-path evidence. It does **not** currently expose a general wood/cloth/paper/organic/stone burnability class. Skyrim material IDs and shader features alone are not sufficiently reliable.

Recommended evidence order:

1. explicit PIXL allow/deny override by plugin/form, mesh, or material path;
2. mod-authored PIXL metadata if present;
3. reference/form archetype and base object class;
4. physical material/Skyrim material metadata;
5. mesh and diffuse/normal texture path tokens with boundary-aware matching;
6. shader/material flags and alpha/two-sided traits;
7. conservative fallback: nonflammable.

Classification should output a confidence plus properties, not only a boolean:

```text
ignitionTemperature
fuelLoad
spreadRate
charYield
smokeYield
moistureCapacity
flammabilityConfidence
```

Metal, stone, snow, water, and low-confidence surfaces must fail closed. Vegetation and cloth should have separate response presets. A debug overlay must show why a reference was accepted or rejected.

## Asset reuse decision

- Unity `.vfx`, `.shadergraph`, `.meta`, and generated URP source are reference-only.
- Flipbooks and char textures are technically reusable after colour-space review, edge dilation, mip generation, and BCn DDS conversion.
- No visual asset except the sound has adequate redistribution evidence in the supplied folder. Do not copy them into shipping PIXL data until provenance is documented.
- The fire sound is identified as CC BY 4.0; shipping it would require the exact source/author attribution from the provided README and an attribution entry.
- The safest release route is PIXL-owned/copyright-cleared fire, smoke, ember, char, and noise assets built to the same parameter contract.

## Performance feasibility

A production target is realistic if work is limited to active visible fire:

- no work beyond a tiny manager update when no fires exist;
- maximum active reference and fire-node budgets;
- 5–15 Hz propagation/classification, 30–60 Hz particle simulation only for visible near fires;
- sleeping fires outside simulation distance;
- no per-frame scan of loaded references;
- spatial hash queries around active nodes only;
- one pooled particle buffer and one/few instanced draws per material class;
- one aggregate Radiant Grid light for a coherent fire cluster, not one per particle;
- sparse atlas slots only for high-priority near objects;
- scalar/emissive distant fallback.

## Major technical uncertainties to prototype

1. **Static reference identity:** which current Skyrim draw data reliably identifies the placed reference rather than only a shared geometry/material instance?
2. **Object coordinates:** can PIXL obtain stable current and previous local-to-world transforms for all target Lighting draws, including movable statics?
3. **Shader coverage:** which Lighting permutations can safely consume surface-state data without multiplying the shader cache beyond acceptable limits?
4. **Transparent pass placement:** where should PIXL particles render so depth, fog, bloom, reconstruction and HUD ordering are correct?
5. **Temporal annotations:** how should the custom fire pass contribute selectively to the existing TAA/reconstruction masks and motion vectors?
6. **Scene ownership/lifetime:** what engine destruction/unload callbacks cover static, movable, actor, and temporary references without stale pointers?

## Decision

**PROTOTYPE FIRST — architecture viable but major unknowns remain**

The renderer already contains most enabling infrastructure, and DX11 is capable of the required visual result. A short, isolated prototype can resolve the identity/mapping and temporal-pass questions before a production module commits to an atlas or particle architecture. Implementing the full system without that gate would risk shared-material contamination, state leaks across placed objects, shader permutation growth, and reconstruction artifacts.
