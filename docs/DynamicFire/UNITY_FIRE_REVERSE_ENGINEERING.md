# Unity Fire Reference — Reverse Engineering

## Scope and method

The reference at `_REFERENCE/PIXL-Flame-Unity` was treated as behavioural evidence, not as source to transplant. All 53 files were inventoried: 19 C# scripts, 9 VFX Graphs, one custom VFX operator, one Shader Graph, 14 image assets, one sound, four metadata files, and four text documents. Generated Shader Graph/VFX Graph data was reduced to its exposed contracts and operative math.

The supplied system is an object-centric Unity effect. It does not simulate thermodynamics, volumetric combustion, structural damage, or a persistent world fire field. One `FlammableObject` owns one expanding spherical burn origin, one spherical extinguished region, a set of collider-local VFX instances, material parameters, optional lights, audio, and event callbacks.

## Runtime dependency and data flow

```text
Ignition source
  (particle collision / sphere overlap / sphere cast / fire-trigger overlap)
        |
        v
FlammableObject.TryToSetOnFire(position, power)
        |
        +-- accumulates ignition progress
        +-- records one local ignition origin
        +-- starts material/VFX/light/audio/event state
        |
        v
FlammableObject.Update()
        |
        +-- expands Fire_spread scalar
        +-- advances char/emission/burnout scalars
        +-- updates per-collider VFX properties
        +-- updates optional lights and audio
        |
        v
FireTrigger periodic OverlapBox
        |
        +-- heats neighbouring FlammableObjects in FixedUpdate
        +-- invokes generic IInteractWithFire callbacks
        |
        v
Flammable Shader + four VFX systems
  surface emission/char | flames | smoke | embers | steam
```

## Implemented state model

The reference does not declare a formal state enum. The effective state machine is inferred from its booleans, timers, and transitions:

| Effective state | Concrete representation | Transition |
| --- | --- | --- |
| Unburned | `onFire == false`, ignition progress zero | Receives ignition power |
| Heating | `currentIgnitionProgress_s > 0`, not on fire | Continued heat reaches `ignitionTime` |
| Ignited/burning | `onFire == true`, `onFireTimer` advances | Spread, emission, VFX and triggers update |
| Locally extinguished | `putOutRadius > 0` around one local centre | Extinguish inputs expand sphere; fire can back-spread after cooldown |
| Burnout | timer passes `burnOutStart` | Burnout event, then VFX/material intensity declines |
| Burnt out | timer passes `burnOutStart + burnOutLength` | Fire stops; optional callbacks/destruction |
| Extinguished | extinguish radius exceeds object-size-derived threshold | Timer jumps into burnout; `extinguished` set |
| Re-ignited | governed by `ReIgnitable` policy | A later ignition may restart state |

Ignition progress has roughly one second of cooling grace. It is not a temperature field. Extinguishing is also not a water/heat simulation: all ray, particle, and sphere mechanisms modify one common spherical put-out state.

## Spread and propagation

### Across one object

The burn front is a world-distance sphere centered at the recorded ignition point. Its radius grows each frame:

```text
fireSpread += fireCrawlSpeed * dt *
              (0.95 + PerlinNoise(onFireTimer, 0) * 0.10)
```

The noise only modulates scalar growth by a small amount. It does not route around geometry or follow UV/topology. Concave and thin objects can therefore burn through themselves.

### Between objects

`FireTrigger` periodically performs `Physics.OverlapBox`. Candidate objects are checked against the source spread radius using collider closest points. `FixedUpdate` repeatedly calls the target's ignition method while a candidate remains exposed. Propagation is therefore:

- per Unity object;
- collision/proximity based;
- timer/threshold driven;
- not energy conserving;
- not mediated by a shared heat field;
- not persistent outside active objects.

`collisionDetectionFrequency` controls the overlap query rate, while ignition accumulation occurs in fixed updates. This makes cost and some response timing dependent on active trigger count and Unity timing configuration.

## Surface-burning shader model

The generated Shader Graph reduces to a small set of behaviours.

### Base material and char

The base albedo and normal are sampled normally. Both are globally blended toward charred textures using one scalar `Charred_wood_blend`. The char boundary does not independently follow the spherical fire front.

### Emissive burn mask

The operative mask is approximately:

```text
inside = distance(positionWS, Fire_origin) < Fire_spread
direction = Fire_side_multiplier + saturate(dot(normalWS, Fire_dir))
brightness = lerp(Fire_min, Fire_max, Fire_bright)
animated = Remap(sample(Fire_map, UV * Fire_tile + Fire_move_dir * time))
fresnel = pow(1 - saturate(dot(normalWS, viewWS)), Fire_softness)
crackle = animated * fresnel / (voronoiA + voronoiB)
emission = inside * direction * crackle * brightness * Fire_color
```

Important consequences:

- `Fire_softness` is a Fresnel exponent, not a spatially soft burn edge.
- `Fire_move_dir` scrolls a texture; it does not control spread direction.
- `Fire_dir` weights surface orientation.
- Two animated Voronoi terms break up the emissive response.
- Division by `voronoiA + voronoiB` has a divide-near-zero/bright-firefly risk.
- The hard spherical inside test can alias or pop at the front.
- Surface char is global per material instance rather than local to burned texels.

The useful concept is progressive, localized modification of albedo/normal/roughness/emission around a burn front. The generated URP pass boilerplate is not useful to PIXL.

## Flame, smoke, ember, and steam rendering

Each flammable collider receives a VFX Graph instance plus a box trigger. Mesh mode can sample mesh points. The graphs expose a common property contract including:

`Box`, `Rotation`, `Spread`, `PutOutArea`, `BurnOutMultiplier`, `WindForce`, `WindMultiplier`, `FlameColor`, `FlameLength`, `FlameLiveliness`, `FlameLivelinessSpeed`, particle size/speed/multipliers, smoke colour/alpha, ember burst controls, `SteamPos`, LOD distance, culling distance, and camera position.

The graphs contain four conceptual systems:

1. flame billboards/flipbooks;
2. smoke billboards/flipbooks;
3. embers;
4. event-driven steam bursts.

They use GPU initialize/update/output stages, box/surface spawning, turbulence/curl/noise, force and drag, spread-sphere gating, put-out-sphere rejection, camera-distance LOD, and billboard/flipbook outputs. Variant capacities range from low thousands to approximately 75,000 particles. These capacities are authoring maxima, not evidence that all particles are continuously alive.

The variants (`gentle`, `wild`, `simple`, `oldSchool`, `lightweight`, `textured`, `custom`, `only smoke`) mostly change assets, capacities, turbulence, rates, and presentation. They do not represent different fire simulation architectures.

## Heat and illumination

The system does not solve heat transport. Heat is represented by ignition progress, spread radius, and material/VFX intensity.

Optional Unity lights are authored children. A light activates when its position lies within the current spread and outside the put-out sphere. `FlameLightFlicker` applies a smooth ramp plus Perlin-driven intensity. Illumination is therefore conventional local lighting, not emissive injection into GI.

## Wind

`WindRetrieve` produces one current wind velocity. It either reads a third-party vegetation-engine global or uses a one-particle Unity simulation as a WindZone velocity sampler. Fire VFX consume the resulting vector. The reference has no spatially varying fire-weather field and no deterministic previous-frame wind evaluation.

## Extinguishing

Particle collisions, ray/sphere casts, and overlap spheres all converge on `IncrementalExtinguish`. The affected sphere expands around one local centre. Steam is emitted at the hit position. Once its radius exceeds an object-size/toughness threshold, the object enters burnout. After a cooldown the extinguished sphere can shrink, allowing back-spread.

This common event contract is valuable. The Unity physics mechanisms are not.

## Persistence

No supplied script serializes burn state. State persists only while the Unity component exists. Material instances, VFX objects, timers, and spheres are runtime-only. There is no save-game schema, streamed-world rehydration, or deterministic restoration.

## File-by-file review

### Runtime and engine scripts

| File | Actual role | PIXL relevance |
| --- | --- | --- |
| `Scripts/Engine/FlameEngine.cs` | Singleton prefab/config owner; pause, global multipliers, trigger cadence, culling and terrain/VSP opt-ins | Preserve budgets, feature ownership and pause semantics; redesign all Unity object management |
| `Scripts/Engine/FireTrigger.cs` | Periodic overlap-box discovery plus fixed-update heat transfer | Preserve proximity/eligibility behaviour; replace per-fire physics polling with bounded spatial event processing |
| `Scripts/Engine/FlameCollisionCallbacks.cs` | Deprecated collision callback bridge | Behaviour can be represented by PIXL events; no direct port |
| `Scripts/Engine/IgnisUnityTerrain.cs` | Converts nearby Terrain tree instances into prefab objects, hiding originals by scale mutation | Conceptually useful for near-field vegetation activation; implementation is unsafe/inapplicable to Skyrim |
| `Scripts/Engine/WindRetrieve.cs` | Produces one wind velocity for VFX | Replace with PIXL/Skyrim world-space weather-wind snapshot |

### Flammable-object scripts

| File | Actual role | PIXL relevance |
| --- | --- | --- |
| `Scripts/FlammableObject/FlammableObject.cs` | Owns ignition, spread, put-out sphere, burnout, material parameters, VFX, lights, sound, and events | Primary behavioural source; must become bounded manager + GPU state, not one per-object update |
| `Scripts/FlammableObject/FlameEventInvoker.cs` | Unity events for ignite, extinguish, burnout, brightness and lifecycle milestones | Map to internal typed events; gameplay scripting integration is optional later |
| `Scripts/FlammableObject/FlameTriggerCallbacks.cs` | Delayed trigger callbacks | Optional event consumer; no rendering dependency |
| `Scripts/FlammableObject/DebugFlammableShader.cs` | Clones material/object and previews fire shader parameters | Useful for a PIXL diagnostic test scene/debug view only |
| `Scripts/FlammableObject/DebugFlammableVFXBox.cs` | Previews VFX box/spread/put-out properties | Useful contract evidence; replace with PIXL debug overlay |

### Interaction scripts

| File | Actual role | PIXL relevance |
| --- | --- | --- |
| `Scripts/Interact/Interfaces/IInteractWithFire.cs` | Generic collision-with-fire callback | Inspires generic `SubmitSurfaceEffect`/`SubmitIgnition` API |
| `Scripts/Interact/ParticleIgnite.cs` | Converts particle collisions into ignition power | Map Skyrim projectile/effect impacts to events |
| `Scripts/Interact/ParticleExtinguish.cs` | Converts particle collisions into put-out growth | Map water/frost/rain impacts to suppression events |
| `Scripts/Interact/RaycastIgnite.cs` | Sphere-cast ignition with occlusion check | Map aimed fire spells/projectile hits; do not poll arbitrary rays every frame |
| `Scripts/Interact/RaycastExtinguish.cs` | Sphere-cast extinguish with occlusion check | Same event mapping for water/frost sources |
| `Scripts/Interact/SphereIgnite.cs` | Volume overlap ignition | Map explosions/area effects to bounded spatial events |
| `Scripts/Interact/SphereExtinguish.cs` | Volume overlap extinguish | Map rain bursts/water volumes/area effects |
| `Scripts/Interact/SimpleInteractWithFire.cs` | Example implementation that logs interactions | Documentation/example only |

### Additional script

| File | Actual role | PIXL relevance |
| --- | --- | --- |
| `Scripts/Additional/FlameLightFlicker.cs` | Smooth light activation/fade plus Perlin flicker | Reuse concept for aggregate fire radiance, with deterministic energy flicker |

### Rendering/VFX files

| File | Actual role | PIXL relevance |
| --- | --- | --- |
| `Shaders/Flammable.shadergraph` | Authored graph for base-to-char blend and spherical emissive burn mask | Extracted math only; Unity graph is not portable |
| `SHADER CODE - PLEASE READ.txt` | Generated URP shader output | Confirms exact math and pass expansion; reference only |
| `Shaders/Shader documentation.txt` | Shader property descriptions | Useful terminology/contract reference |
| `VFX/Flame_box.vfx` | Baseline four-system flame effect | Visual/parameter reference |
| `VFX/Flame_box_gentle.vfx` | Softer baseline variant | Quality/style reference |
| `VFX/Flame_box_wild.vfx` | High-energy/high-capacity variant | Upper visual target; unsuitable as direct budget |
| `VFX/Flame_box_LW.vfx` | Lower-capacity variant | Useful Low/Medium target evidence |
| `VFX/Flame_box_textured.vfx` | Texture-focused variant | Asset sampling reference |
| `VFX/Flame_box_simple.vfx` | Simplified variant | MVP billboard reference |
| `VFX/Flame_box_oldSchool.vfx` | Legacy/simple presentation | Reference only |
| `VFX/Only_smoke.vfx` | Smoke-only path with shared interface | Supports independent smoke quality/enablement |
| `VFX/Custom_flame.vfx` | User-customizable variant | Supports data-driven presets rather than shader permutations |
| `VFX/Visual_effect_additions/Periodic Vector Animation.vfxoperator` | Periodic vector/liveliness graph helper | Replace with deterministic HLSL noise/oscillation |
| `VFX/VFX_Parameter_Documentation.txt` | Common exposed-property definitions | Canonical VFX contract evidence |

### Asset files

| Asset | Size | Assessment |
| --- | ---: | --- |
| `Burningtexture.png` | 1024² | Useful burn/noise reference; provenance must be established before shipping |
| `Firetext.png` | 256² | Small animated fire mask candidate; convert to DDS only if licensed |
| `FlameSheet_2x2.png` | 128² | Low-cost flipbook candidate; provenance required |
| `FlameTextureAtlasWild.png` | 1024² | Higher-detail flame reference/candidate; provenance required |
| `StylizedFlame.png` | 512² | Style reference; likely not suitable for PIXL realism target |
| `Flame03-hollow-temperature_16x4.exr` | 2048x1024 | Technically useful temperature flipbook; would need compact linear/HDR DDS conversion and provenance |
| `DiscSmoke01_16x4.tga` | 2048x1024 | Smoke flipbook candidate; convert/compress only if licensed |
| `WispySmoke03b_8x8.png` | 1024² | Smoke variant candidate; provenance required |
| `TexturesCom_CharredWood_*` | 1024² each | Strong visual reference; filename alone is not a redistributable license |
| `TexturesCom_WoodSiding6_*` | 1024² each | Test substrate/reference only unless license confirmed |
| `TexturesCom_Metal_AluminumBrushed_*` | 1024² each | Control/nonflammable test material; license confirmation required |
| `Sounds/Fire-sound.wav` | ~6.2 MB | README identifies CC BY 4.0 and a freesoundslibrary source; attribution must ship if used |

`.meta` files are Unity import identifiers/settings and have no PIXL runtime value. The README/documentation text files are provenance and behavioural evidence and should remain with the reference, not enter the PIXL distribution automatically.

## Performance characteristics of the Unity design

Likely hot costs are:

- one `Update` per active `FlammableObject`;
- one periodic overlap query per active trigger/collider;
- dictionary churn during contact discovery;
- one or more VFX instances per collider;
- continuous material-property mutation/material instances;
- optional conventional lights;
- very high aggregate GPU-particle capacities;
- terrain-tree scans and prefab conversions.

The design is reasonable for a controlled Unity scene but does not scale directly to Skyrim's streamed, heavily modded world. PIXL needs fixed budgets, event-driven activation, sleeping states, spatial hashing, pooled GPU resources, and LOD.

## Defects and limitations discovered

- `FireTrigger.ProcessCollidedObject` appears to test `onTouchObjs.ContainsKey(...)` before adding, where the expected guard is likely the inverse. The on-touch path may never populate correctly.
- One ignition sphere and one put-out sphere cannot represent multiple independent burn sites.
- Hard world-distance masks ignore surface topology and UV seams.
- Char is a global scalar material blend rather than a local persistent burn mask.
- VFX motion is not accompanied by explicit reconstruction motion/reactive data.
- There is no save persistence or streamed-world policy.
- Conventional child lights do not scale to hundreds of fire patches.
- Material eligibility is author opt-in through Unity components, not classification.
- No energy or heat conservation is attempted.
- No structural damage or gameplay destruction model exists.
- Shader crackle division is numerically unsafe without a denominator floor and luminance clamp.

## Behaviour worth preserving

The strongest reusable ideas are:

1. all ignition sources converge on one typed event API;
2. ignition requires accumulated exposure rather than a binary touch;
3. burn visuals have a progressive front, emissive edge, char, burnout, and cooling;
4. extinguishing is localized and can permit later back-spread;
5. flames, smoke, embers, steam, light, and material state are driven by one fire state;
6. quality/presentation variants share one parameter contract;
7. update and render distances are separately controllable;
8. wind and weather should influence presentation and propagation without owning fire state.

## Explicit disposition of remaining reference files

The preceding tables cover all executable scripts, shader/VFX graphs, generated shader text, primary flipbooks, smoke/fire textures, and debug helpers. The following files were also opened or identified during the recursive audit and are stated explicitly here so no reference input is silently omitted:

| File | Finding / disposition |
| --- | --- |
| `Shaders/Flammable.shadergraph.meta` | Unity import/GUID metadata only. It helps Unity resolve the Shader Graph but contains no portable rendering algorithm. |
| `Shaders/Shader documentation.txt.meta` | Unity import/GUID metadata only; no runtime behaviour. |
| `Sounds/Fire-sound.wav.meta` | Unity audio importer settings/GUID. Not an audio algorithm and not useful to the renderer architecture. |
| `Sounds/README.txt` | Identifies the supplied fire sound as CC BY 4.0 material. It is reusable only with the required attribution and provenance record; audio is optional to the renderer prototype. |
| `Sounds/README.txt.meta` | Unity import/GUID metadata for the README; no runtime behaviour. |
| `Textures/TexturesCom_CharredWood_1.2x1.2_1K_albedo.tif` | Charred albedo reference. Technically convertible to mipmapped DDS, but redistribution permission must be proven separately; otherwise recreate a PIXL-owned char material. |
| `Textures/TexturesCom_CharredWood_1.2x1.2_1K_normal.tif` | Matching char normal reference. Same provenance restriction and conversion requirement as the albedo. |
| `Textures/TexturesCom_Metal_AluminumBrushed_1K_albedo.tif` | Example/base test material, not evidence that metal should burn. It should not enter the proposed flammable classifier or shipping assets without provenance. |
| `Textures/TexturesCom_Metal_AluminumBrushed_1K_normal.tif` | Matching test normal; useful only for understanding the reference demo's material setup. |
| `Textures/TexturesCom_WoodSiding6_2x2_1K_albedo.tif` | Example unburned wood material. The PIXL module must modify the actual Skyrim/mod material instead of replacing it with this texture. |
| `Textures/TexturesCom_WoodSiding6_2x2_1K_normal.tif` | Matching base normal; useful as comparison/reference only and not required by the architecture. |

All 53 files under `_REFERENCE/PIXL-Flame-Unity` are therefore accounted for: 19 C# scripts, 9 VFX graphs, 1 custom VFX operator, 1 Shader Graph, 2 shader/documentation text files, 1 VFX parameter document, 14 image/texture assets, 1 sound, 1 sound license README, and 4 Unity metadata files.
