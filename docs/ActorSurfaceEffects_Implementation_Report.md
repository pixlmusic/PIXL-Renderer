# Actor Surface Effects Implementation Report

## Status

The version 1 implementation is complete in canonical source and passes the PIXL Release build, module audit, scoped whitespace validation, and strict FXC validation of the affected character and legacy Lighting permutations. Live Skyrim visual validation remains required; this report does not claim in-game success.

## Files added

- `engine/Modules/ActorSurfaceEffects.h`
- `engine/Modules/ActorSurfaceEffects.cpp`
- `pipeline/Actor Surface Effects/Module.ini`
- `pipeline/Actor Surface Effects/Kernels/ActorSurfaceEffects/CharacterRuntime.hlsli`
- `pipeline/Actor Surface Effects/Kernels/ActorSurfaceEffects/ActorSurfaceEffects.hlsli`
- `distribution/Shaders/Common/PIXLAdvancedSnowMaterial.hlsli`
- `docs/ActorSurfaceEffects_Architecture.md`
- `docs/ActorSurfaceEffects_Implementation_Report.md`
- `docs/ActorSurfaceEffects_LIVE_TEST_QUEUE.md`

## Files modified

- `engine/Globals.h` and `engine/Globals.cpp`: global module ownership.
- `engine/RenderModule.cpp`: module registration in the Characters group.
- `engine/State.cpp`: final character binding-hook installation after resource setup.
- `engine/Renderer/QualityProfiles.cpp`: Characters quality profile and detection mapping.
- `engine/Modules/GroundResponse.cpp`: event sharing after an accepted deformation stamp.
- `engine/Modules/DialogueFocus.h`: reusable construction of the existing 96-byte dialogue payload.
- `distribution/Shaders/DialogueFocus/DialogueFocus.hlsli`: shared character runtime ABI.
- `distribution/Shaders/Lighting.hlsl`: stable mask evaluation and material composition.
- `distribution/SKSE/Plugins/PIXLRenderer/Translations/en.json`: user-facing module name.

## Architecture

Actor Surface Effects is a generic, optional RenderModule. Ground Response submits localized events through `AddSurfaceEffect`/`AddGroundContact`; the manager owns bounded per-actor temporal state; actor-local analytical lobes are packed into the shared character pixel constant buffer; Lighting applies snow, mud, and wet-film response to the existing material.

The persistent representation is actor-local rather than camera-space, screen-space, UV-space, bone-name dependent, or tied to one armour mesh. It therefore supports replacement bodies and equipment without author masks. The event type and lifecycle layer are generic enough for later blood, frost, char, ash, poison, dust, and spell residue producers.

## Snow flow

```text
Ground Response body contact
 -> accepted snow deformation stamp
 -> actor-local contact lobe
 -> fresh porous snow
 -> melting snow
 -> dielectric wetness
 -> dry/original material
```

The vertical centre and radius come from the contacted Havok body interval and the probed surface top. Shallow snow therefore targets soles/boots; deeper penetration or a contacted knee, hand, or torso can create a higher localized lobe. Snow tint, roughness, dielectric F0, and restrained backscatter use shared `PIXLAdvancedSnowMaterial` helpers so actor and terrain snow belong to the same material family.

## Mud flow

```text
Ground Response body contact
 -> accepted wet-mud deformation stamp
 -> actor-local contact lobe
 -> wet dark mud + wetness
 -> lighter rough dry mud
 -> fade/original material
```

Movement speed produces a bounded upward splash contribution. Rain rewets dry mud and delays drying; deep water washes mud and snow while leaving temporary wetness. Mud remains dielectric rather than metallic.

## Wetness integration

Wetness is an auxiliary channel fed by wet mud, snow melt, rain acting on an existing contaminated lobe, direct future events, and water immersion. It modestly darkens the substrate and lowers microscopic roughness/F0 as a thin dielectric water film. It does not multiply material specular brightness or replace PIXL's existing rain response.

## GPU resources and ABI

- One shared 752-byte neutral character constant buffer.
- One shared 752-byte focused-dialogue character constant buffer.
- One 752-byte constant buffer per active contaminated actor.
- No new SRVs, UAVs, samplers, render targets, textures, draw calls, or compute dispatches.
- Pixel constant slot `b13` is shared with Dialogue Focus on character permutations and remains compile-time disjoint from Ground Response's landscape `b13`.
- The first 96 bytes are byte-for-byte Dialogue Focus v1. Actor Surface Effects starts at byte 96; the twelve 48-byte events start at byte 176; total size is 752 bytes.
- C++ `static_assert` checks the prefix offset and total structure size.

## VRAM estimate

The default Ultra configuration permits `min(32 quality slots, 24 configured nearby NPCs) + 1 player reserve`, or 25 allocated actor payloads. Including both shared payloads, raw constant data is 20,304 bytes (about 19.8 KiB), excluding normal D3D allocation overhead.

The maximum exposed configuration is still quality-bounded to 32 nearby slots plus one player reserve. Including shared payloads, raw constant data is 26,320 bytes (about 25.7 KiB), excluding allocation overhead. CPU vectors/maps add a small bounded amount. No full-resolution mask texture exists.

## CPU performance

Ground contact generation reuses Ground Response's existing actor/body traversal and accepted receiver probes. It does not add another all-actor scan. Per-frame lifecycle work is bounded to active contaminated states (at most 33) and their quality-limited 4/6/8/12 lobes. Events are merged or replace the weakest/oldest lobe. Elapsed time, not frame count, drives lifecycle transitions.

No CPU timing has been measured in Skyrim yet.

## GPU performance

Clean actor draws bind a shared buffer and exit on invalid surface-effect magic before the event loop. Contaminated actor pixels evaluate at most 4, 6, 8, or 12 analytical lobes according to quality. Stable trigonometric breakup and material composition occur only on contaminated actor permutations.

No GPU timing has been measured in Skyrim yet.

## Maximum active actors

| Quality | Nearby actor limit before user cap | Lobe limit per actor |
| --- | ---: | ---: |
| Low | 8 | 4 |
| Medium | 14 | 6 |
| High | 20 | 8 |
| Ultra | 32 | 12 |

The user-facing NPC cap is 1-64 and defaults to 24. One additional slot is reserved for the player. Distant new NPC events are rejected; eviction favors retaining the player and recent/nearby contacts.

## First-person and third-person behaviour

Third-person/world Havok bodies are the sole contact producer. Both first- and third-person character render representations resolve the player's one actor-keyed state when Skyrim supplies actor ownership to the draw. This prevents duplicate event generation and perspective-switch resets. First-person mesh ownership must be confirmed in the live test because that is runtime/asset dependent.

## NPC and creature behaviour

Nearby actor-owned skinned Lighting geometry follows the same path as the player. Each NPC has independent bounded state. Creatures are best-effort: sane body contacts may produce state and compatible actor-owned skinned passes can consume it, but humanoids are the supported version 1 target. Unsupported geometry receives an inert payload and cannot crash or inherit another actor's contamination.

## Equipment-change behaviour

State belongs to the actor, not a mesh or equipped object. Newly equipped actor geometry therefore samples current contamination. This avoids stale mesh references and permanent contaminated replacement gear. It also means version 1 contamination is not remembered separately for an unequipped individual item.

## Weather integration

- Snow weather slows environmental melting.
- Clear exterior conditions permit normal melt/dry recovery.
- Interiors accelerate snow melt.
- Rain accelerates snow melt, rewets dry mud, maintains wetness, and slows mud drying.
- Deep water washes snow/mud and leaves temporary wetness.
- Falling-snow deposition is deliberately deferred as a distinct future producer; version 1 snow accumulation remains physical-contact driven.
- Nearby individual heat-source coupling is not implemented in version 1.

## Ground Response integration

Ground Response remains authoritative for receiver classification, raised snow/mud surface height, interaction depth, contact body, radius, velocity, and accepted stamp capacity. An Actor Surface event is submitted only after the corresponding ground stamp is accepted. Disabling or failing Actor Surface Effects does not alter deformation.

## Advanced Snow Material integration

`PIXLAdvancedSnowMaterial.hlsli` centralizes actor/world snow tint, packed/thin roughness, dielectric F0, and backscatter tint. Terrain-specific displacement and compaction assumptions remain in Ground Response; the actor path uses a thin-accumulation adaptation over the original character material.

## Skin integration

Skin Optics remains authoritative. Actor contamination modifies material inputs without replacing skin diffusion. Exposed skin accepts reduced coverage, transfers part of fresh snow immediately toward meltwater, and retains the underlying normal/detail and lighting response. Eye acceptance is strongly reduced; hair response is mildly reduced.

## GUI and quality integration

The Characters panel exposes:

- Enable Actor Surface Effects
- Snow Accumulation
- Mud Accumulation
- Effect Quality: Low/Medium/High/Ultra
- Persistence
- Accumulation Strength

Advanced controls expose maximum NPCs, effect distance, lifecycle rates, mask softness, and edge breakup. Developer mode exposes channel/mapping debug views. The Characters grouped quality profile changes the real actor/lobe limits rather than only changing a label.

## Debug modes

- Combined Mask
- Snow
- Mud
- Wetness
- Contact Lobes/channel allocation
- Actor-local Coordinates
- CPU counters for active actors and lobes

These isolate failures between contact generation, actor mapping, accumulation/lifecycle, shader sampling, and material composition. Debug state is never serialized and defaults Off.

## Runtime-safe fallback

Initialization failure marks only Actor Surface Effects unavailable and prevents its late hook from installing. Ground Response and all other modules continue. After successful initialization, clean/unresolved/non-actor skinned draws receive a neutral full-size payload to prevent stale `b13` state. Shader magic/version validation makes absent or legacy data inert.

## Known limitations

- Live Skyrim visual and performance validation is pending.
- Version 1 analytical lobes approximate body contact; they are not per-triangle collision decals.
- Actor mapping uses actor root position/yaw rather than a full bone-local deformation map. Extreme poses require live inspection.
- First-person arm/hand consumption depends on Skyrim exposing player ownership on those Lighting draws and needs runtime confirmation.
- Falling precipitation alone does not deposit snow.
- Nearby heat-source melt is not yet connected.
- State is intentionally not serialized into savegames and is reclaimed after fade/unload.
- Creature presentation is not guaranteed.
- Material tint/coverage defaults require live calibration across skin, cloth, fur, leather, and metals.

## Recommended in-game tests

Use `docs/ActorSurfaceEffects_LIVE_TEST_QUEUE.md`. The highest-value first pass is: shallow/deep snow on player boots/legs; independent snow on a nearby NPC; mud on left/right steps; lifecycle under clear/rain/interior/water conditions; first/third-person switching; equipment swap; DLAA/DLSS stability; and clean-render regression with the module disabled.

## Future expansion

- **Blood:** submit directional impact lobes with a future colour/coagulation/dry state; a sparse hero texture tier could preserve splatter direction while keeping the same actor manager.
- **Frost magic:** add cold deposition and crystalline roughness/scattering, driven by spell-hit events and temperature-aware thaw.
- **Fire/char:** add bounded thermal/char state and heat-driven removal of snow/wetness; avoid altering base emissive unless a real burn event exists.
- **Ash:** add dry powder accumulation with movement shedding and weather wash-off.
- **Poison:** add tinted wet residue with timed reaction/evaporation, restricted to explicit gameplay effect events.
- **Spell impact masks:** translate collision position/radius/direction into the existing event API; never let spell modules manipulate actor buffers directly.
- **Environmental dust:** add low-intensity dry accumulation from biome/impact events with water/rain cleaning.
- **High-quality hero tier:** optional sparse actor-local texture masks for the player/dialogue NPCs, fed by the same events and bounded by a fixed atlas pool.

## Static validation completed

- PIXL Release C++ compilation and link: PASS.
- `PIXL-Audit`: PASS with all 37 integrated modules.
- Strict Windows SDK FXC `/Ges /WX /O3`: PASS for eight affected character/Material Forge/legacy/landscape/static Lighting pixel-shader permutations.
- CPU/GPU ABI: PASS through C++ offset/size assertions and FXC reflection (Dialogue prefix 0-95, surface header at 96, event array at 176, total 752 bytes).
- No external deployment, push, tag, or release is implied by these checks.
