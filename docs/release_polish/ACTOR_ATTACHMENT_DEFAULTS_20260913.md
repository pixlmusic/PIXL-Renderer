# Actor accumulation attachment and fog/POM promotion

## Evidence and scope

Inspected the owner's 7.77-second September 13 19:25 capture as a 2 fps player
crop sequence. Snow coverage on raised legs disappears and returns through the
gait/jump cycle. The existing shader evaluates posed world points against
actor-root-local lobes, so moving limbs pass through a stationary projection.
This explains why changing persistence alone cannot fix the attachment.

## Controlled implementation

Accepted contacts choose a nearby known humanoid skeleton bone within a bounded
radius. Each lobe retains a bone name (not a long-lived node pointer), its
initial reference frame and scale. At prepass, the current skeleton supplies
the inverse pose mapping back into that deposit frame. Event merges compare
contacts in that same frame and do not merge across different bone anchors.
Masks, breakup and normal detail now follow the contact frame. Lifetimes,
coverage amounts and original material treatment remain unchanged.

Unknown skeletons keep the previous root-local fallback. Missing named anchors
temporarily suppress their lobes rather than project them in the wrong space.
Finite/scale checks reject invalid transforms. Existing actor ownership and
equipment-skin resolution are retained. This remains an analytic per-bone lobe,
not per-texel paint: joint skin blending, cloth, adjacent-limb overlap and
non-humanoid skeletons still require live testing/future work.

## ABI and cache

Actor event payload grows 48 -> 96 bytes (three float4 world-to-deposit rows).
Character payload grows 752 -> 1328 bytes; the DialogueFocus 96-byte prefix and
event-array offset 176 remain unchanged. Static assertions and FXC reflection
verify the layout, including row offsets 224/240/256 for event zero. Runtime
version is 0x00010001. ActorSurfaceEffects module 1-0-1 selectively invalidates
actor-related Lighting PS permutations; unrelated caches are not cleared.

## Defaults promotion

Copied the current live Atmosphere section, Ambient Probe FogAmount and
PreserveFogLuminance, and Material Layers POM controls into source defaults.
Material-detail reconstruction, other modules, and ImageReconstruction remain
unchanged. Both default upscaling methods remain None (0). Normalized material
tuning Version to 3 for the existing host ABI. Live UserGraphics is not edited.
This does not update the previously packaged release ZIP.

## File accounting

All listed files active, reviewed and modified for this scope, including
correctness, security, fidelity, performance and maintainability.

| File | Purpose/dependencies, validation and future |
| --- | --- |
| engine/Modules/ActorSurfaceEffects.cpp | Event/lifecycle/actor ownership and b13 upload. Bone-name lookup and reference-frame merge/upload added; bounded 13-bone search, no retained raw bone pointers. CPU Release compile/size checks passed. Future: per-actor per-frame bone lookup cache and explicit skeleton generation tracking. |
| pipeline/Actor Surface Effects/Kernels/ActorSurfaceEffects/CharacterRuntime.hlsli | Actor-only b13 ABI; appended per-event rows, immutable dialogue prefix preserved. Reflection verified. Future: generated CPU/HLSL layout checks. |
| pipeline/Actor Surface Effects/Kernels/ActorSurfaceEffects/ActorSurfaceEffects.hlsli | Coverage/breakup/normal projection follows deposit frame, old root fallback retained. Six strict FXC cases passed. Future: joint-weight-aware projection and cloth attachment. |
| pipeline/Actor Surface Effects/Module.ini | Version 1-0-1 drives existing actor-stage cache predicate. Future: persistent include fingerprints. |
| distribution/SKSE/Plugins/PIXLRenderer/SettingsDefault.json | Owner-authorized scoped fog/POM promotion. Parsed expected-document equality checked; upscaling None/None preserved. Future: automate scoped promotion validation. |

Read-only trace also covered Lighting's Evaluate/ApplyMaterial call sites,
ActorSurfaceEffects public interface/cache predicate, NiMatrix3 operations and
the existing DialogueFocus-compatible buffer prefix.

## Validation and remaining risks

Final canonical PIXL-12C Release build passed. Six PS fixtures passed with
strictness, validation and warnings-as-errors: skin, skinned armour, hair, eye,
ordinary objects and GroundResponse terrain (actor b13 excluded). Initial HLSL
fixture found a reserved identifier, corrected before the passing run.
Shader evidence: build/actor-anchor-tests-7a66b15cef684e9a969524dd86c88c1b.
Twenty-seven independent mathematical pose/scale/camera-origin cases preserve
the same deposit coordinate. These are not substitutes for live animation tests.

No GPU timings or confirmed visual fix claimed. Costs: 576 extra bytes per
affected-actor buffer, bounded bone-name resolution, three dot products per
active anchored lobe plus normal-frame transformation. No additional textures,
passes, networking or telemetry. Recheck jumping, running, turning, equipment
swaps, first/third person, missing bones and natural melt/drying.

## 5 Future Visual Improvements

Joint-weighted attachment; cloth handling; cross-limb rejection; deposited normal
orientation validation; mesh-UV residue detail.

## 5 Future Performance Improvements

Per-frame bone lookup cache; dirty pose updates; event priority culling;
GPU buffer upload profiling; optional lower-lobe quality tiers.

## 5 Future Feature / Research Ideas

Skeleton profiles; persistent mesh-space paint; cloth residue API; explicit
equipment migration policy; automated animation regression captures.

## Live deployment

Deployed with Skyrim closed on 2026-09-13 at 19:52 local time. All five
destination SHA256 hashes matched their tested source payloads. Old targets
were moved before replacement to preserve existing deployment hardlinks.
Recoverable backup: `build/deployment-backups/Actor-Attachment-20260913-195239-bd6bee03adb3481eb643a8228502abf1`.

| Data-relative file | SHA256 |
| --- | --- |
| SKSE/Plugins/PIXLRenderer.dll | 5FE61F4241D10E30104FD94586AB77FF5676C4636674CBAA8A9207D9139BE877 |
| Shaders/ActorSurfaceEffects/ActorSurfaceEffects.hlsli | 37FDFECC67E837B81D3C30F7BA15B19672D8233A6A82C301217990C52C5415DD |
| Shaders/ActorSurfaceEffects/CharacterRuntime.hlsli | 71954C70166DDB392B379CDA94150CF0EAF8AEA8F9BCA0F5F11599A4805E6743 |
| Shaders/PIXL/Modules/ActorSurfaceEffects.ini | 5540BC63E58B4D970222C9173F21C970A54CE2EB8E4FB7F7DB2A7E62B189135F |
| SKSE/Plugins/PIXL/Config/RendererDefaults.json | 4AC742069A968F8FF89662ED0B4B4A4BA05EA1A5D25D18438ADC3A9945D1E7EF |

Live UserGraphics and shader cache were not modified. No release ZIP update
or source push performed. Visual animation validation remains with the owner.
