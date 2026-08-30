# Actor Surface Effects Architecture

## Scope

Actor Surface Effects is a bounded, optional character-material layer driven by PIXL Ground Response. Version 1 implements localized snow, mud, and derived wetness for the player and nearby humanoid NPCs. The storage and event API deliberately reserve effect identity and composition space for later blood, frost, char, ash, poison, dust, water, and spell residue without implementing those effects in this release.

## Existing systems discovered

- `GroundResponse::QueueCollisions()` already enumerates the player plus nearby high-process actors, traverses their live third-person Havok bodies, and computes detailed world AABBs.
- `GroundProbeSurface()` already classifies the contacted LAND receiver as snow or wet mud, using Skyrim landscape data, PIXL's renderer-derived snow metadata, weather snow raise, terrain slope, hard-surface rejection, and Rain Response wetness.
- For each accepted body contact Ground Response already has actor identity, current/previous body centre, body half extents, contact bottom/top, receiver land height, raised surface height, pristine depth, actual penetration depth, radius, strength, and movement.
- The third-person/world graph is Ground Response's authoritative physical representation. First-person geometry is intentionally excluded from collision production.
- Lighting draw geometry exposes its owning `TESObjectREFR` through `BSGeometry::GetUserData()`. Actor-owned skinned passes can therefore bind per-actor state without mesh-name, texture, armour, race, or body-replacer assumptions.
- `DialogueFocus` already owns pixel-shader constant slot `b13` for actor character passes. Ground Response owns the same slot only for landscape. Actor Surface Effects must share the character ABI instead of claiming another saturated Lighting constant-buffer slot.
- Rain Response exposes authoritative live rain and wetness through `GetLiveRainIntensity()` and `GetCommonBufferData()`.
- Skin Optics and Tissue Diffusion already own skin classification and shading. Actor Surface Effects must modify material inputs conservatively and never replace those models.
- Ground Response's snow material uses porous-snow roughness, subtle cool scattering, compaction-dependent wetness and stable world detail. The actor variant reuses those physical cues but not terrain-only displacement assumptions.

## Chosen mask representation

Version 1 uses a **bounded actor-local analytical mask pool**.

Each accepted body contact becomes a filtered 3D lobe containing:

- actor-local contact centre;
- horizontal radius;
- contacted vertical band and soft boundary;
- snow, mud, and wetness amounts;
- lifecycle state and age;
- stable per-event breakup seed;
- last-contact and priority metadata.

The shader evaluates the small lobe set against camera-relative world position transformed into actor-local space. Multiple lobes accumulate with a bounded union rather than replacing each other. Stable low-frequency breakup perturbs only the edge and material micro-response, preventing horizontal cut lines without texture swimming.

This representation was chosen over one render target per actor because it:

- allocates no per-actor state for unaffected actors (two shared 752-byte neutral/dialogue payloads are retained for safe character binding);
- has a strict CPU and GPU memory ceiling;
- requires no per-frame CPU-to-GPU texture uploads;
- adds no mask raster draw calls or compute dispatches;
- remains stable under temporal reconstruction;
- follows actor movement and yaw;
- survives equipment and perspective changes because state belongs to the actor, not a mesh instance;
- maps naturally to the detailed contact bodies Ground Response already supplies.

The design can later add a sparse texture-mask tier for hero characters without changing the event API or lifecycle model.

## Memory model

- Fixed maximum active actor count, quality-scaled and user-bounded.
- Player receives permanent highest priority while the module is enabled.
- Nearby/recently contacted actors are retained ahead of distant or inactive actors.
- Each actor owns a fixed maximum number of contact lobes. New contacts merge with compatible nearby lobes; otherwise the weakest/oldest lobe is replaced.
- Each active actor owns one small combined character constant buffer, updated at most once per frame and rebound for all of that actor's skinned Lighting draws.
- States with no meaningful snow, mud, or wetness are reclaimed. Invalid/unloaded handles fail closed.

No full-resolution per-actor textures are allocated. The expected memory footprint is tens of kilobytes rather than megabytes.

## Event architecture

The renderer exposes a generalized internal API conceptually equivalent to:

```cpp
AddSurfaceEffect(actor, type, worldPosition, radius, intensity, contactDepth, verticalBand, velocity);
```

Ground Response is the initial producer. It emits the event only after the same contact has passed receiver classification, capacity checks, and successful deformation-stamp packing. Actor contamination therefore cannot invent a contact that Ground Response rejected. The existing deformation path is unchanged when Actor Surface Effects is disabled or unavailable.

Future producers can submit localized effects without accessing GPU buffers or actor state directly.

## Actor-local mapping strategy

Event positions are transformed from absolute world space into actor-root space using actor position and yaw at submission time. Shader pixels use the current actor position/yaw, converted to camera-relative coordinates with PIXL's cached `CameraPosAdjust`, to reconstruct the same actor-local frame.

Vertical coverage comes from the actual contacted body interval and receiver surface top, not a fixed actor-origin threshold. A shallow boot contact therefore remains near the sole, while deeper snow, crouching, falling, or a hand/body entering the material creates events at the corresponding body-local height.

The mapping is independent of UVs, skeleton bone names, body texture layout, race scale, armour mesh, and first/third-person render representation.

## Snow lifecycle

`Fresh Snow -> Melting Snow -> Wetness -> Dry/Normal`

- Ground-contact events add fresh snow.
- Falling-snow deposition is intentionally not implemented in version 1. It remains a separate future event producer so precipitation cannot masquerade as physical ground contact.
- Rain and warm/non-snow conditions accelerate melting.
- Snow melt transfers mass into wetness rather than disappearing instantly.
- Skin receives faster melt than armour/clothing through compile-time material classification.
- Snow appearance retains underlying material detail, uses a cool porous tint, high fresh roughness, subtle stable microdetail, and reduced roughness/darker response while melting.

## Mud lifecycle

`Wet Mud -> Drying Mud -> Dry Mud -> Fade/Normal`

- Mud contact adds wet mud and wetness.
- Rain rewets mud and delays drying.
- Wet mud is darker and smoother without becoming metallic.
- Dry mud becomes lighter, rougher, more broken up, then fades.
- Material response is modulated for skin and inferred metallic surfaces while preserving the original shader.

All transitions use elapsed seconds and bounded linear rates independent of frame rate.

## Wetness integration

Wetness is a shared auxiliary channel. Snow melt and wet mud feed it; rain can maintain it. The actor material response follows PIXL's existing wet-surface interpretation: modest albedo darkening, reduced microscopic roughness, and dielectric Fresnel visibility rather than metallic brightening.

## Character shader integration

- A shared character `b13` layout keeps the existing 96-byte Dialogue Focus prefix byte-for-byte intact and appends Actor Surface Effects data.
- Dialogue Focus retains its original 96-byte ABI and remains compatible with legacy Dialogue Focus-only permutations.
- Actor Surface Effects installs the final character binding hook and binds one full-size shared neutral/dialogue payload to clean actor draws. Contaminated actors receive their own full-size payload. Non-actor skinned geometry receives the shared neutral payload so a previous actor's state cannot leak across draws.
- The HLSL validates independent magic/version fields before reading appended data. Clean shared buffers and legacy 96-byte buffers therefore resolve to an inert surface effect while the immutable Dialogue Focus prefix remains functional.
- Landscape, foliage, particles, water, world map, and non-actor geometry are compile-time/runtime excluded.

## Material composition

The effect modifies the existing `MaterialProperties` rather than replacing the material:

- snow blends albedo, roughness, dielectric response, and restrained microdetail;
- wet/dry mud blends albedo and roughness with material-aware strength;
- wetness darkens diffuse slightly and smooths roughness while preserving conductor identity;
- skin keeps Skin Optics/Tissue Diffusion and receives reduced coverage plus faster lifecycle response;
- contamination behaves as a dielectric layer above metal, temporarily reducing the visible substrate metallic response while restoring it exactly as coverage fades;
- cloth, leather, fur, vanilla and Material Forge paths retain their underlying normals and lighting.

## Performance strategy

- No work beyond a master branch when disabled.
- No independent actor scan: events reuse Ground Response's accepted body contacts.
- Clean actors take one shared constant-buffer bind and an early invalid-magic branch; they perform no lobe loop or material-composition work.
- Fixed actor and lobe caps; distance/recent-contact/player priority; LRU-style reclamation.
- Per-actor constant buffers update at most once per frame, then reuse across body-part draws.
- Pixel work is restricted to skinned actor Lighting permutations and quality-scaled lobe counts.
- No new draw calls, compute dispatches, render targets, or per-frame texture uploads.

## DX11 resource bindings

- Pixel constant buffer: shared character `b13`, already reserved for actor-only Dialogue Focus and compile-time disjoint from Ground Response landscape `b13`.
- Payload size: 752 bytes. The first 96 bytes are the immutable Dialogue Focus v1 layout; the appended header begins at byte 96 and the twelve 48-byte events begin at byte 176.
- No new SRV, UAV, or sampler slot is required in version 1.
- Existing `b0-b12`, texture, sampler, and UAV contracts remain unchanged.

## First-person, equipment, NPC, and creature policy

- One logical state is keyed by actor FormID/handle.
- Third-person Havok bodies produce contacts. First-person meshes sample the same player state and never generate duplicate contacts.
- Equipment changes do not own or invalidate contamination; newly equipped geometry samples the actor's current state. This is intentionally actor-relative for v1.
- Nearby humanoid NPCs use the same path independently.
- Creature contacts may enter the generic state only when their live bounds are sane, but rendering is limited to compatible actor-owned skinned Lighting passes. Unsupported creatures fail inertly.

## Debug architecture

The module exposes debug modes for combined mask, snow, mud, wetness, contact lobes, local mapping, and allocation/lifecycle diagnostics. Shader debug colour is restricted to affected actor pixels. CPU diagnostics report active slots/events and bounded rejection/eviction counts without per-frame release-log spam.

## Runtime-safe fallback

If the module INI is missing or initial resource creation fails, the module marks itself unavailable before installing its late hook and PIXL continues without Actor Surface Effects. Once initialized, unresolved actor ownership or absent state binds the shared neutral payload and Lighting returns the original material. Ground Response, weather, Skin Optics, Dialogue Focus, and all other PIXL systems continue independently.

## Future extension path

The event type and lobe payload reserve generalized effect identity and auxiliary parameters. New effect implementations should add lifecycle/material composition policies while reusing actor allocation, local mapping, event merging, draw binding, quality caps, and diagnostics. A future high-quality sparse texture tier can consume the same events for decals or directional splashes without invalidating v1 state producers.
