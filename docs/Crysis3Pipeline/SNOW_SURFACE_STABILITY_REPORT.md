# Snow Surface and Particle Stability

## Scope

This release-safe correction addresses three owner-captured defects while continuing the Crysis-inspired Particle Response work through bounded light-energy handling.

## Ground Response seasonal snow

`GroundTerrainTextureIsHard` previously accepted structural tokens such as `mountain` and `cliff` before testing whether the resolved runtime texture was explicitly snow-covered. Seasonal landscape packs commonly produce resolved names such as `mountainsnow` or `cliffsnow`; those layers were therefore encoded as hard/excluded even though Skyrim visibly rendered snow.

Explicit `snow`/`snw` evidence now wins over structural-name exclusion, except for negative forms such as `nosnow`, `no_snow`, and `snowless`. The existing terrain slope mask, land-height receiver test, roof/platform blocker test, and per-layer coverage remain authoritative, so this does not make arbitrary vertical rock deformable. Seasons of Skyrim remains optional and the resolved texture/material path remains the source of truth.

## Actor accumulation anchoring

Actor Surface Effects stored contact lobes in actor-relative coordinates but used `Actor::GetPosition()` as the origin. During airborne/root-motion animation that logical reference can remain on the support plane while the rendered skeleton has already moved. Lighting evaluates the live skinned world position, so the actor could move out of its own otherwise-persistent analytical mask.

Both event conversion and per-frame GPU data now use the loaded actor 3D root's world translation, with the logical actor position retained as a safe fallback. No event format, constant-buffer ABI, mask representation, resource, or save data changed.

## Snow particle light energy

Snow particles previously compressed every Radiant Grid local light independently and then added all compressed results. Multiple carried, character-adjacent, particle-derived, or incandescent-geometry lights could therefore exceed the intended ceiling and turn flakes into emissive white blocks.

The snow-only path now accumulates local illumination first and applies one luminance-preserving soft shoulder to the complete local term. Weak coloured light remains visible. Directional light, ambient light, rain particles, opaque lighting, and authored particle alpha are unchanged.

## Validation

- Release `PIXLRenderer` build: PASS.
- `PIXL-Audit`: PASS, 38 integrated modules.
- Strict FXC `/Ges /WX /O3` Particle PS: PASS for snow + Radiant Grid/Natural Lighting, rain + Radiant Grid/Natural Lighting, and base particle permutations.
- CPU/GPU ABI: unchanged.
- Live deployment: DLL and `Particle.hlsl` deployed with Skyrim closed; source/live hashes match.
- User configuration: byte-identical before and after deployment.
- Runtime visual acceptance: pending owner test.

## Runtime acceptance

1. Revisit the smooth seasonal snow patch: explicit snow-covered mountain/cliff landscape layers should now receive the same raised/deformable treatment as other eligible snow, subject to slope limits.
2. Accumulate snow on boots/lower clothing, then walk, sprint, rotate and jump: coverage must remain attached throughout airborne motion and after landing.
3. Stand near an NPC carrying a torch during snowfall: flakes may catch warm light but must not become large white emissive blocks.
4. Confirm rain and ordinary fire/opaque lighting retain their prior appearance.
