# Natural Lighting and Thin Surface — current source review

These modules are reviewed here independently of the still-incomplete whole-renderer audit. Source reads include every file listed as full below and named integration callsites. No hook addresses, engine layout, GPU register or stored setting semantics were changed.

## Natural Lighting

Purpose/pipeline: PostPostLoad installs existing point-light creation/luminance hooks; light records carry inverse-square/linear metadata in a NiLight runtime overlay. RadiantGrid calls ProcessLight, then lighting/grass/atmosphere use NaturalLighting::GetAttenuation. Inputs are authored TESObjectLIGH flags, size/intensity/cutoff and light/target positions. Outputs are bounded influence radius, fade, size bias and light flags. No owned textures/history, resize resources, per-frame heap allocation, independent weather source or settings UI exists in these files. It is an IsCore module with broad shader define availability; keep registration intact.

### Five improvement investigations

1. **Engine overlay layout drift — LOW, implemented compile-time protection.** RuntimeLightDataExt aliases RE::NiLight::LIGHT_RUNTIME_DATA rather than allocating extra storage. Added size, alignment and diffuse/radius/fade/trailing-field offset assertions against the pinned CommonLib definition. Native build passed, including current cross-runtime configuration. Zero runtime/image cost; this verifies compile-time layout, not external engine versions or third-party overlay ownership.
2. **Linear-light propagation — MEDIUM integration fix in grass.** Traced TES flag -> RuntimeLightDataExt flags -> RadiantGrid Light -> Color::PointLight. Enhanced grass now honors bit 11; detailed change/validation is in FOLIAGE_REVIEW_20260907.md. No extra gamma conversion for already-linear light data; unflagged/default rendering retained.
3. **CPU versus shader near-field attenuation — investigated, deferred.** HLSL floors distance² + sizeBias to 1; CPU GetAttenuation does not. Small authored lights can therefore have higher CPU luminance estimates near their centers than shader intensity. CPU selection and final shading need not have identical thresholds, so changing selection without light/shadow priority captures is not justified. Header's claim that attenuation is [0,1] is inaccurate; inverse-square radiance can exceed 1. Preserve HDR range rather than saturating it.
4. **Finite/cutoff/range robustness — investigated, preserved.** CalculateRadius already guards negative radicand, zero cutoff and minimum radius; sizes/cutoffs from authored forms are bounded in SetExtLightData. Nonfinite mod-provided runtime values and the public CPU GetAttenuation(0, radius, 0) corner still require contract tests. No arbitrary retuning of the established 2.4x retirement range, which would affect light coverage and clustered cost.
5. **Hook/lifetime/compatibility — investigated, deferred.** Normal creation/luminance null checks and original-function fallback are retained. Get overlay assumes non-null NiLight, and the schema reuses ambient/radius fields by design. Do not remove it as suspicious memory access; do not widen it or claim compatibility with every other light plugin. No destructor/device-reset work needed for a module without owned graphics resources. Runtime mod combination tests remain HIGH-risk validation, not a reason to rewrite the module.

### File-by-file record

| File | Purpose / full review findings | Implemented / risk | Fidelity, performance, security, future / validation |
| --- | --- | --- | --- |
| engine/Modules/NaturalLighting.cpp | Full; hooks, record conversion, CPU radius/attenuation and luminance | Unchanged; existing engine integration HIGH validation sensitivity | No network/process/persistence or per-frame allocations. Retain reference behavior; near-field CPU/HLSL mismatch documented. Compiled and callers inspected; no light-scene runtime test. |
| engine/Modules/NaturalLighting.h | Full; module interface, constants, hook declarations | Unchanged | Attenuation range comment inaccurate; no UI exposed beyond module summary. Future contract documentation after capture tests; do not clamp HDR to match comment. |
| engine/Modules/NaturalLighting/Common.h | Full; flag extensions/runtime overlay | New compile-time layout checks, LOW | No runtime instructions/field reordering. Matches pinned NiLight 0x2C record layout; six assertions pass in Release. Future external overlay compatibility tests. |
| pipeline/Natural Lighting/Module.ini | Full; Integrated NaturalLighting 1-3-0 | Unchanged | Catalog/registry validation passes, no assets omitted. |
| pipeline/Natural Lighting/Kernels/NaturalLighting/NaturalLighting.hlsli | Full; shared branchless inverse-square/regular attenuation | Unchanged | Existing positive denominator floor and cubic radius fade retained. Computes both models, likely inexpensive arithmetic versus divergent branch; measure before changing. Caller must provide valid Light radius/fade data. Compiles through integrated grass/atmosphere tests. |
| extern/CommonLibSSE-NG/include/RE/N/NiLight.h | Targeted dependency reference, full file read within existing pinned boundary | Unmodified third-party source | Confirms 0x2C runtime record and runtime-specific accessor offsets. Not a review of all CommonLib internals; license/pin preserved. |

Verification: `build/final-release-records/light-overlay-abi-build-20260907.log`, exit 0. Grass and atmosphere compile tests cover the shared attenuation consumer without proving runtime hook correctness.

### 5 Future Visual Improvements

1. Near-source radiance fixtures; 2. light-retirement captures; 3. linear/nonlinear consistency scenes; 4. shadow-priority comparisons; 5. modded interior light coverage tests.

### 5 Future Performance Improvements

1. Measure light range cost; 2. measure shader attenuation branch options; 3. profile CPU luminance hook frequency; 4. audit uninitialized-light debug logging frequency; 5. measure portal/light list density.

### 5 Future Feature / Research Ideas

1. Overlay ownership diagnostics; 2. runtime schema tests; 3. CPU/GPU attenuation reference runner; 4. authored-light compatibility fixtures; 5. finite metadata validation at import boundaries.

## Thin Surface

Purpose/pipeline: existing BSLightingShader geometry hook maps alpha-blended eligible geometry or NIF AnisotropicAlphaMaterial tags into ExtraFeatureDescriptor bits 6–8. Lighting PS shapes alpha at the final opacity stage for eligible non-skin/non-hair/non-tree/non-eye/LOD permutations. Global settings supply a 16-byte FeatureData block; no owned GPU/history resources or extra fullscreen pass. Stored AlphaStrength is an inverse blend toward original alpha, not forward effect strength. Shipping RendererDefaults currently disables the module; preserving that default is intentional, but it remains a configurable runtime/build module and must be reviewed.

### Five improvement investigations

1. **Malformed config math — LOW, fixed.** Clamp reduction/softness/inverse blend to visible [0,1] ranges. This keeps SoftClamp's `2 - softness` denominator in [1,2]. Invalid global mode becomes Disabled, preserving fail-closed behavior instead of activating a different material. Normal values and schema unchanged; negligible reload-only cost. Native exact-source tests pass.
2. **Backward opacity explanation — LOW, fixed.** Fallback tooltip wrongly said 0 disables shaping; shader lerp does the opposite. Rename to Original Opacity Blend and explain 0 = full shaping, 1 = original opacity, in both C++ fallback and shipped English localization. No migration/inversion of saved values and no shader equation change. Interactive UI still needs in-game confirmation.
3. **Packed material ABI — LOW, implemented protection.** Added explicit 16-byte PerFrame size and final-field offset assertions, matching SharedData::ThinSurfaceSettings. SkinnedOnly remains CPU-only through GetCommonBufferData slicing. Bits 6–8 agree with State::ExtraFeatureDescriptors; no field/register changes. Build and exact-struct tests pass.
4. **Per-mesh override precedence — investigated, retained.** Descriptor 0 selects globals; explicit tags keep independent defaults for reduction/softness/inverse blend. The global label now says this explicitly. Integer tags are masked to three bits, tag 0 promotes to disabled, and wrong extra-data types leave disabled state. Changing all tagged meshes to obey globals would break authored assets; rejected. Pass/geometry pointer validity relies on the existing engine hook contract and remains a HIGH-risk test item.
5. **Transmission/optimization — investigated, deferred.** Current shader uses guarded optical-depth and grazing-cosine equations; naive/isotropic models currently share the same enabled equation. Anisotropic mode consumes tangent/binormal instead of texture normal. Preserve those existing visual choices. Skipping the final alpha work at inverse blend 1 or unifying duplicate helpers could save instructions, but requires full Lighting permutation/runtime-alpha testing. No new approximation or alpha behavior introduced for release polish.

### File-by-file record

| File | Purpose / review | Findings / changes / risk | Fidelity, performance, security, future / validation |
| --- | --- | --- | --- |
| engine/Modules/ThinSurface.h | Full; module/settings/descriptor interface | Added 16-byte/offset assertions, LOW | No resources/history to resize. Existing enum comment says >=5 disabled but shader uses >=4; explicit emitted value 7 remains compatible. Future documentation cleanup/descriptor tests. |
| engine/Modules/ThinSurface.cpp | Full; geometry metadata selection, hook, complete UI/JSON | Clamp reload settings and correct inverse-blend UI, LOW | No new networking/filesystem/hook access; existing per-geometry RTTI/extra-data lookups and descriptor update retained. No per-frame allocations added. Future geometry eligibility cache only after lifetime profiling. |
| pipeline/Thin Surface/Module.ini | Full; Integrated ThinSurface 1-0-0 | No change | Registered/configurable although disabled in shipped defaults. Audit passes. |
| pipeline/Thin Surface/Kernels/ThinSurface/ThinSurface.hlsli | Full; descriptor decode and alpha equations | No change | Positive optical-depth/cosine guards retained; SoftClamp requires positive limit now guaranteed by bounded global settings and explicit-tag defaults. No history/no textures. Future numeric grazing/zero-opacity tests and full Lighting compile cases. |
| distribution/Shaders/Lighting.hlsl | Partial; include exclusions and final alpha consumer | No source change for this module | Verified inverse lerp semantics, NIF precedence and model selection. Not a full-file review. |
| distribution/Shaders/Common/SharedData.hlsli | Partial; 16-byte settings/FeatureData member | No change | Names differ from C++ but order/types match. Entire packed FeatureData ABI still requires full audit. |
| distribution/SKSE/Plugins/PIXLRenderer/Translations/en.json | Partial; two Thin Surface strings and JSON parse | Updated matching label/tooltip, LOW | No execution/security capability. Parse test passes; remaining strings not certified. Other locale files are not shipped by current stage script. |
| tools/TestPixlThinSurfaceSettings.ps1 | Full; new exact-source structs/serializer/functions test | Bounds, invalid modes, preserved valid settings/semantics, round-trip/default tests | Offline existing compiler/nlohmann, unique ignored build fixture, no game writes. Native /W4 /WX /O2 /fp:fast tests pass; locale JSON parses. |

Verification: `build/thin-settings-tests-0a81db71e1d64850a0ae51ba8eabd92e`; production log `build/final-release-records/thin-surface-settings-build-20260907.log` (completed build/audit). NIF tags, skinned/global defaults and blend endpoints still require real in-game mesh tests. These later changes are newer than RC-04 and included from RC-05 onward.

Additional shader evidence: tools/TestPixlMaterialShaders.ps1 (full new script review, offline FXC, unique ignored fixture paths, no game writes) passed 32 ordinary Lighting VS/PS combinations of SKINNED, MODELSPACENORMALS, THIN_SURFACE and DO_ALPHA_TEST with /Ges /WX /O3. Evidence: build/material-shader-tests-be51bd7241754bdb8ddbe08e5ccc1921/results.csv. Initial redundant LIGHTING test macro collided with the source define; fixed only the test invocation. This is selected-permutation compile evidence, not full Lighting review or visual validation.

### 5 Future Visual Improvements

1. Grazing opacity reference curves; 2. woven fabric captures; 3. skinned tangent-frame tests; 4. explicit-tag compatibility assets; 5. fade/disocclusion reconstruction checks.

### 5 Future Performance Improvements

1. Profile geometry extra-data lookup; 2. skip shaping at original blend endpoint after tests; 3. compare exp/log implementation cost; 4. measure alpha overdraw; 5. verify excluded permutations compile away all work.

### 5 Future Feature / Research Ideas

1. Per-mesh override inspector; 2. material settings migration tests; 3. captured transparency-mask validation; 4. multilingual control descriptions; 5. shader-reflection ABI automation.
