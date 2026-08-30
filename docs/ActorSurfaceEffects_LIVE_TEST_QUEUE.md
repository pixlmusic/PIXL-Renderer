# Actor Surface Effects Live Test Queue

Static/build success is established. These checks require Skyrim and must be recorded before visual acceptance.

## Startup and fallback

- [ ] Launch with Actor Surface Effects enabled; confirm module initialization and zero shader failures.
- [ ] Confirm the shader-build panel completes and record disk-hit, built-now, failed, and elapsed totals.
- [ ] Warm restart; confirm no unexpected full-library rebuild.
- [ ] Disable only Actor Surface Effects; Ground Response snow/mud deformation and all existing PIXL character shading must remain unchanged.

## Player snow

- [ ] Walk through shallow deformable snow; coverage stays primarily on contacted soles/boots.
- [ ] Enter deeper snow; coverage rises naturally with contacted shins/knees rather than forming a fixed horizontal band.
- [ ] Crouch/fall so another body region contacts snow; verify a localized higher lobe appears.
- [ ] Leave snow; observe fresh snow -> melt -> temporary wetness -> original material.

## Player mud

- [ ] Step one boot into mud; contamination begins locally rather than coating both legs.
- [ ] Continue walking; left/right/repeated contacts accumulate.
- [ ] Run through mud; bounded splash rises slightly without coating the whole actor.
- [ ] Observe wet dark mud -> rougher/lighter dry mud -> fade.

## NPCs and capacity

- [ ] Follow one NPC through deformable snow and one through mud; verify state is independent from the player.
- [ ] Observe several nearby NPCs simultaneously.
- [ ] Move contaminated NPCs beyond Effect Distance and confirm no instability or unbounded allocation.

## Perspective and equipment

- [ ] Accumulate in third person, switch to first person, inspect hands/arms if contacted, then return to third person without reset or duplication.
- [ ] Change boots/armour/clothing while contaminated; verify no crash, stale mesh state, or corruption.
- [ ] Inspect vanilla armour, clothing, exposed skin, metal, leather/fur, and one common replacement body/armour.

## Weather and environment

- [ ] Snow contact during snowfall; verify slow melt after leaving contact.
- [ ] Snow followed by rain; verify faster melt and wetness transfer.
- [ ] Mud during rain; verify rewetting/slower drying.
- [ ] Mud in clear weather; verify normal dry/fade lifecycle.
- [ ] Move outdoors -> interior -> outdoors; verify stable lifecycle and faster indoor snow melt.
- [ ] Enter deep water; verify wash-off and temporary wetness.

## Reconstruction and edge cases

- [ ] Compare DLAA and at least one DLSS mode while walking/camera panning; masks must remain actor-anchored without shimmer, ghosting, or camera-space swimming.
- [ ] Test 30, 60, and high-refresh frame rates if practical; accumulation/decay timing should remain consistent.
- [ ] Test dialogue camera, cell transition, fast travel, actor unload/reload, pause/menu, and loading screen.
- [ ] Try one creature; unsupported geometry must fail inertly.

## Debug isolation

- [ ] Combined/Snow/Mud/Wetness views identify the expected channels.
- [ ] Contact Lobe view matches physical contact positions/depth.
- [ ] Actor-local Coordinates remain fixed to the actor while the camera moves.
- [ ] Active actor/lobe counters stay within the selected quality and NPC limits.

## Log review

- [ ] Exit cleanly and inspect `PIXLRenderer.log` for ActorSurfaceEffects initialization, shader failures, resource errors, fallback warnings, access violations, and cache finalization.
