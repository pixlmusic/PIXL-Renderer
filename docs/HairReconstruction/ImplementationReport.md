# Hair Reconstruction Implementation Report (retired experiment)

> Release status: retired from the shipping module list after live compatibility
> testing. Reserved shared-buffer storage remains temporarily intact solely to
> preserve cached-shader ABI; Strand Shading is the active hair renderer.

## Implemented

- Registered an optional integrated `Hair Reconstruction` module.
- Added conservative native/automatic/diagnostic hair classification tiers.
- Added PIXL-owned shader descriptor bits for automatic hair and rejected
  diagnostic candidates.
- Preserved native Hair technique behavior and upgraded compatible ordinary
  skinned-alpha Lighting materials without FormID/mod lists.
- Added robust UV/card direction reconstruction while preserving authored flow
  maps.
- Reused PIXL Strand Shading for the actual anisotropic hair BRDF.
- Added derivative-filtered virtual fibre detail and quality-scaled silhouette
  breakup.
- Added restrained root/tip-weighted world-space wind and actor inertia.
- Evaluated current/previous procedural positions before existing Lighting motion
  vectors.
- Added rain, water, wet roughness/darkening/weight response.
- Connected the existing Actor Surface Effects snow response to a hair-specific
  compatibility switch.
- Added Low/Medium/High/Ultra Characters quality mapping.
- Added serialized GUI controls, reset behavior, defaults, translations and seven
  diagnostic views.
- Added safe rejection and material-only fallback behavior.

## New files

| File | Purpose |
| --- | --- |
| `engine/Modules/HairReconstruction.h` | Module contract, 128-byte reserved-slot CPU settings ABI and quality API. |
| `engine/Modules/HairReconstruction.cpp` | GUI, serialization, validation, real frame delta and quality tiers. |
| `pipeline/Hair Reconstruction/Module.ini` | Integrated module descriptor/version. |
| `pipeline/Hair Reconstruction/Kernels/HairReconstruction/HairReconstruction.hlsli` | Class confidence, root/flex, motion, direction, wet material, virtual fibres and debug views. |
| `docs/HairReconstruction/PipelineAudit.md` | Actual runtime/pipeline audit. |
| `docs/HairReconstruction/README.md` | Architecture and user/developer documentation. |
| `docs/HairReconstruction/ImplementationReport.md` | This validation and handoff report. |

## Modified files

| File | Hair Reconstruction change |
| --- | --- |
| `engine/Globals.h/.cpp` | Declared and constructed the module singleton. |
| `engine/RenderModule.cpp` | Registered the module after Strand Shading. |
| `engine/PipelineBuffer.cpp` | Replaced the historical eight-register reserved block in place with Hair settings. |
| `engine/ShaderCache.h/.cpp` | Added automatic/candidate descriptor flags and defines. |
| `engine/MaterialForge.cpp` | Classified live render-pass geometry and preserved/stripped flags safely for the pixel lookup. |
| `engine/MaterialForge/PhysicalMaterialRegistry.h/.cpp` | Added the reusable automatic hair evidence classifier. |
| `engine/Renderer/QualityProfiles.cpp` | Connected Characters grouped quality and tier detection. |
| `engine/Menu/PIXLRendererPage.h` | Added public module placement/name/description. |
| `distribution/Shaders/Common/SharedData.hlsli` | Replaced the historical reserved payload with a matching 128-byte HLSL settings layout without moving later fields. |
| `distribution/Shaders/Lighting.hlsl` | Integrated vertex motion, card appearance, direction, wet material and debug output. |
| `pipeline/Strand Shading/Kernels/Hair/Hair.hlsli` | Applied the module's bounded transmission multiplier to existing Marschner transmission. |
| `pipeline/Actor Surface Effects/Kernels/ActorSurfaceEffects/ActorSurfaceEffects.hlsli` | Honored hair snow compatibility while retaining the existing actor-surface owner. |
| `distribution/SKSE/Plugins/PIXLRenderer/SettingsDefault.json` | Added conservative Medium defaults. |
| `distribution/SKSE/Plugins/PIXLRenderer/Presets/PIXL-Renderer-Medium.json` | Added Medium hair settings. |
| `distribution/SKSE/Plugins/PIXLRenderer/Presets/PIXL-Renderer-Ultra.json` | Added Ultra hair settings. |
| `distribution/SKSE/Plugins/PIXLRenderer/Presets/PIXL-Renderer-Live-Tested.json` | Added conservative settings without claiming the new module is visually live-tested. |
| `distribution/SKSE/Plugins/PIXLRenderer/Translations/en.json` | Added module summary strings. |

Some listed files already contained unrelated active-development changes. No
unrelated user work was reverted.

## Rendering pipeline: before

```text
Skyrim hair cards
  -> Lighting skinning
  -> authored bitangent / optional flow map
  -> PIXL Strand Shading
  -> Lighting outputs + skeletal motion vectors
```

## Rendering pipeline: after

```text
Skyrim/native/mod hair candidate
  -> conservative descriptor classification
  -> current + previous skinning
  -> root/flex + deterministic secondary motion (both frames)
  -> existing motion-vector calculation
  -> authored tangent/flow map + robust derivative direction inference
  -> virtual fibre and wet card response
  -> existing PIXL Strand Shading BRDF
  -> existing Actor Surface Effects snow/wet composition
  -> Lighting/deferred output + reconstruction inputs
```

## CPU/GPU ABI

`HairReconstruction::Settings` is `alignas(16)`, exactly 128 bytes. Its C++ and HLSL
fields have identical order and scalar widths across eight `float4` registers. It
replaces the historical eight-register post-process reservation at the same position
in both `PipelineBuffer.cpp` and `SharedData::FeatureData`. The total shared-buffer
size and every following pre-Hair field offset therefore remain unchanged.

`HAIR_RECONSTRUCTION` is emitted only for native Hair, conservatively auto-classified
Hair, and diagnostic Hair-candidate descriptors. It is excluded from the generic
integrated-module define loop so unrelated world materials retain their established
permutations and cache entries.

No SRV, UAV, sampler, cbuffer register or render-target binding was added. Automatic
hair uses PIXL-owned descriptor flag bits 6 and 7, documented as unused by vanilla.

## Validation performed

### C++

Release target:

```text
cmake --build build/PIXL-12C --config Release --target PIXLRenderer --parallel 4
PASS -> build/PIXL-12C/Release/PIXLRenderer.dll
```

Integrated audit:

```text
cmake --build build/PIXL-12C --config Release --target PIXL-Audit --parallel 4
PASS -> 38 integrated modules
```

The remaining MSBuild `MSB8028` messages concern pre-existing shared FidelityFX
intermediate directories; no Hair Reconstruction compiler warning/error remains.

### HLSL

FXC shader model 5 validation passed for:

- native Hair pixel permutation;
- automatic Hair pixel permutation;
- rejected candidate/debug pixel permutation;
- native skinned Hair vertex motion permutation;
- automatic skinned Hair vertex motion permutation.

The two accepted pixel permutations produced identical bytecode, as did the two
accepted vertex permutations, confirming the auto-classified path reaches the same
implementation as native hair.

Standalone FXC's stock Lighting vertex source has a pre-existing `b12` collision
between `Common/FrameBuffer.hlsli` and `VS_PerFrame`. Vertex validation used a
temporary, deleted shim that supplied only FrameBuffer declarations parsed by
SharedData. Shipping source/registers were not changed. Runtime shader compilation
still requires live validation.

All edited default/preset/translation JSON files parse successfully. `git diff
--check` reports no whitespace errors.

## Performance

### CPU

Automatic classification runs when Lighting render passes are built, not during
every pixel or a global per-frame actor scan. It performs bounded normalized-string
evidence checks. No actor polling, topology traversal or persistent actor pointer
is added. It is not labelled as a synthetic GPU pass.

### GPU

Expected accepted-hair cost consists of:

- a small deterministic motion function in the skinned vertex shader at Medium+;
- screen derivatives and direction blending in the hair pixel path;
- a filtered analytic fibre term and wet material adjustment;
- no extra texture fetch for fibre detail;
- no new pass, draw, dispatch or resource transition.

Distance LOD removes secondary motion beyond the configured range. Low disables
motion; Medium keeps procedural silhouette changes off. Exact average/p95/p99 cost
is **not measured** and must be captured in-game. Integrated work appears under the
existing Lighting GPU category rather than a fake standalone pass.

## Memory and lifetime

- No new persistent GPU texture/buffer.
- No per-NPC allocation.
- No retained `NiAVObject`/actor pointer.
- No cache growth or LRU required for the current analytical implementation.
- Equipment/hair changes rebuild their normal render-pass descriptors.
- Paused frames use zero procedural delta; teleports/rebases are bounded out.

## Compatibility behavior

- Native Skyrim Hair technique: accepted.
- Conventional skinned-alpha mod hair with strong material/hierarchy evidence:
  automatically accepted.
- Unknown transparent material: unchanged.
- Weak eyebrow/lash/whisker candidate: diagnostic only at default threshold.
- Helmet/hood/armour/cloth/face/eye/mouth texture: rejected.
- Bald actor: no candidate and effectively zero cost.
- First/third person: uses the render representation and bone history already
  provided by Lighting; no duplicate logical actor state exists.
- External hair physics: not required; double-motion avoidance with SMP remains a
  future live-compatibility task.

## Known limitations and runtime test requirements

1. Visual correctness has not been claimed; live Skyrim testing is pending.
2. Unusual UV layouts may give imperfect root/tip or fibre direction inference.
3. `HairConfidence` uses runtime material/hierarchy/render-state evidence, not a
   full texture-alpha statistical analysis or topology graph.
4. Procedural micro-strands are virtual card fibres/edge breakup, not generated
   line/ribbon geometry.
5. No semantic connected-component lobe classifier or collision primitives exist
   in this release-safe implementation.
6. Native/mod asset alpha order remains Skyrim's existing order; no OIT was added.
7. DLSS/FSR/frame-generation stability must be judged while moving, turning and
   crossing disocclusions.

## Five major visual improvements

1. Anisotropic highlight direction can follow the card/UV fibres instead of only a
   possibly generic geometry bitangent.
2. Existing Marschner/Kajiya-Kay scattering now reaches strongly classified normal
   skinned-alpha mod hair, not only the native Hair technique.
3. UV-stable fibre breakup reduces a uniformly painted card appearance.
4. Rain/water produces restrained absorption, roughness and heavier motion rather
   than a plastic gloss multiplier.
5. Root-weighted, world-space motion adds subtle inertia/gust response while
   preserving short/scalp rigidity.

## Five major performance improvements

1. Reused the existing Lighting draw instead of a duplicate hair pass.
2. Reused Strand Shading instead of compiling a second BRDF stack.
3. Analytical reconstruction avoids per-NPC CPU simulation and allocations.
4. No additional texture samples/resources for virtual fibres.
5. Confidence, quality and distance gates avoid work on rejected/far hair.

## Ten future enhancements

1. Safe mesh-resource topology cache with connected card components.
2. Scalp/head-bone attachment volume for stronger root inference.
3. Per-component spring lobes with staggered compute updates.
4. Optional head/neck/shoulder primitive collision.
5. Sparse generated edge ribbons for cinematic close-ups.
6. Hair-only weighted blended OIT experiment.
7. Hair-specific visibility/self-shadow approximation.
8. Offline/runtime alpha texture structure statistics cached by resource hash.
9. SMP/HDT detection and automatic secondary-motion handoff.
10. Dedicated hair reactive/transparency mask tuning after reconstruction captures.

## Recommended live tests

1. Start at Medium and confirm bald, helmet, eye, face and translucent-cloth
   materials remain unchanged in Hair Detection debug view.
2. Check native short/long hair, beard and ponytail under sun, interior local light
   and strong backlight.
3. Walk/run/turn the actor, orbit the camera and jump; watch roots, motion vectors
   and settling for sliding or jelly motion.
4. Compare dry, rain, submerged and drying transitions.
5. Test TAA, DLSS and FSR while crossing foliage/high-contrast backgrounds.
6. Test several normal-path mod hairstyles and use the candidate view for false
   negatives rather than lowering the global threshold immediately.
7. Enable High/Ultra procedural fibres only after the Medium baseline is stable;
   inspect silhouettes for crawl, halos and alpha-order changes.
