# Foliage Dynamics — current-pass engineering review

Purpose: grass/animated-leaf lighting, packed grass material interpretation and restrained world-space grass gusts. Preserve Skyrim's authored bend weights, structural tree motion and existing visual calibration.

## Pipeline / dependencies

```text
JSON / module UI -> Settings (80 bytes) -> PipelineBuffer / FeatureData
                -> TuningSettings (96 bytes) -> Prepass upload -> PS b13
GroundResponse BSGrassShader draw hook -> rebind foliage b13 after engine setup
Grass VS: authored wind + world-field gust -> current/previous world positions
Grass PS: cutout/material -> direct + clustered + ambient light -> deferred MRTs
TREE_ANIM Lighting PS: foliage BRDF / transmission / optional normal-Y flip
Deferred lighting / Hybrid GI -> image reconstruction -> presentation
```

This is the foliage dependency slice, not the entire renderer pass diagram. Source-authoritative shader tree was reconciled before edits. Only `distribution/Shaders/RunGrass.hlsl` has changed in this slice; current live shader was not overwritten. RC-03 contains its previous version and is superseded for this shader.

Inputs: instance transforms/vertex normals/alpha bend weights, current/previous timers and transforms, camera-origin adjustment, WindVector, base/packed-normal texture t0, shadow mask t1, shared weather/lighting data, optional clustered lights/probes/contact shadows/atmosphere and GroundResponse VS t100. Outputs: geometry positions/motion vectors, lit diffuse, specular, normal/gloss, albedo and masks. No foliage-owned history texture or simulation grid is created. Its only owned dedicated GPU resource is the tuning CB, 96 bytes rounded to 128 bytes by ConstantBufferDesc. Prepass updates it once per invocation; geometry draws rebind it without reallocation. No GPU memory reduction claimed.

## Five substantial improvement investigations

1. **Preset/config reload consistency — LOW, fixed.** Base settings reset through WITH_DEFAULT, but absent PIXLGrassTuning retained previous values. Reset the extension before optional parsing. Legacy presets now reproducibly use current defaults instead of stale alpha/normal/color overrides. Existing presets containing the object and saved schema remain compatible. One small assignment during reload, no frame cost. Exact structs, serialization and functions pass legacy/malformed-extension/default/round-trip/bounds tests.
2. **Linear local-light integration — MEDIUM / controlled visual correction, fixed.** NaturalLighting marks authored linear light records; RadiantGrid forwards that bit. The enhanced grass path omitted it when calling Color::PointLight, unlike the basic grass/atmosphere paths. Pass LightFlags::Linear so Linear Light Core does not apply gamma/calibration a second time. Expected visual result: consistent grass lighting around those flagged lights. Nonlinear lights and disabled Linear Light Core retain the prior equations. Removes unnecessary conversion for flagged lights, with a small flag test; no measured timing. No ABI/register/order change. Thirty-two strict FXC permutations pass; real tagged-light scene comparison remains required.
3. **Independent tree-normal UI control — LOW, fixed.** TreeFlipNormalY is consumed in TREE_ANIM normal decoding regardless of EnableEnhancedVegetation, but its control was disabled with that lighting toggle off. End the disabled group before this independent control. Existing values, defaults, serialization and shader behavior remain unchanged. Manual scope/callsite trace, production build; interactive in-game test pending.
4. **World-space wind and temporal history — investigated, preserved.** Shared WorldWind is a resource-free pure function. RunGrass transforms the instance anchor, restores CameraPosAdjust and samples the same field at WindTimer/PreviousWindTimer. Vertex alpha locks roots and separately bounds tip response. Default TREE_ANIM still uses authored tree motion: its optional USE_PIXL_MULTI_FREQUENCY_TREE_WIND block is disabled and references removed MultiScaleField helpers. Do not silently enable this broken historical experiment or replace trees with grass gust motion. Large-world float precision, rotated batch displacement basis and origin-shift/previous-transform tests remain future validation. No increased wave amplitudes, noise frequency or tracing cost.
5. **Stable materials versus raw detail cost — investigated, preserved and compile coverage added.** Current RunGrass already computes derivatives outside variable-length light loops, filters packed normals by projected footprint, smoothly retires unresolved detail/local lobes, and averages mirrored specular lobes rather than adding their energy. Preserve this policy. Three sentinel texture loads per grass pixel and repeated GGX/transmission work under dense local lights are probable costs, not measured bottlenecks. A per-material packed-layout cache or BRDF replacement requires asset/lifetime and scene validation. Exact compile tests now cover foliage on/off, depth/alpha variants and representative integrated dependencies. Numerical degenerate-normal and Oren-Nayar stress tests remain open; compilation alone does not validate those cases.

## UI/settings trace

Both 20-field base and 19-field serialized tuning payloads are copied by the current C++ serializer; Magic/Version are restored internally, not user configurable. Base bounds and tuning bounds match their intended visible ranges; shader tuning readers provide fallback values when b13 magic/version do not match. CPU/HLSL field order is unchanged and was compared line-by-line.

| Controls | Current consumer / dependency |
| --- | --- |
| Glossiness, Specular Strength, Specular AA, Grass Card Specular Coherence | RunGrass roughness/GGX/filtering/orientation; tree uses foliage specular helper |
| SSS Amount, Leaf Transmission | Directional/local grass transmission and tree helper |
| Diffuse Wrap | Tree/leaf GetFoliageDiffuseWrap; not a general grass diffuse control |
| Flip Tree Normal Y | TREE_ANIM Lighting normal decoding, independently of enhanced lighting |
| Enhanced Vegetation | Material/specular/transmission/AA gates; not wind toggle |
| Natural Grass Gusts, Wind Response, Gust/Flutter strengths, size/speeds | RunGrass CalculateWindDisplacement -> FoliageWind -> WorldWind; default tree experiment off |
| Complex Grass Mode / threshold | Three sentinel probes and layout/Y-convention selection |
| Override Complex / Basic Brightness | Enhanced grass albedo calibration, conditional on detected layout |
| Flip Grass X/Y, Mirror Y, normal strength/card blend/filter distance/softness | Packed normal decoding, specular orientation and continuous filtering; mirror/intensity effects require enhanced material |
| Alpha override, coverage, bias, shape, edge dither | Material cutout independent of wind/lighting; depth path shares it |
| Saturation / Contrast | Enhanced material color transform |
| Wet boost / normalized specular / specular-map influence | Enhanced wet roughness/normal/filter mask and lobe strength |
| Transmission / Local Light boosts | Enhanced SSS and clustered light color |

Some tuning controls remain visible while their enhanced-material consumers are inactive; their values are real and saved, not dead controls. Future UI dependency indication/localization should not hide normal convention or independent alpha controls. Exact full settings-page interaction and quality-profile coverage remain pending.

## File-by-file review

| File | Purpose / review / dependencies | Findings / changes / risk | Performance, fidelity, security, future and verification |
| --- | --- | --- | --- |
| engine/Modules/FoliageDynamics.h | Full; module registration/cache scope, two settings structs, resources/UI interface | 80/96-byte assertions retained; no edit | No network/process/file writes beyond normal config consumers. Raw tuningCB owns lifetime without explicit destructor/device replacement; investigate before changing lifecycle. Complete source read and native size tests, not runtime device-loss proof. |
| engine/Modules/FoliageDynamics.cpp | Full; resource setup/prepass/binding, all module controls and JSON | Fixed legacy reload and tree-normal UI scope, LOW | Prepass uses bounded upload, errors delete CB and stop subsequent uploads. Shared b13 rebound by GroundResponse; no per-draw allocation. Future dirty upload and device-loss handling only with profiling/tests. Build/native tests pass. |
| pipeline/Foliage Dynamics/Module.ini | Full; Integrated FoliageDynamics version 2-3-0 metadata | No change; active registry/catalog matches | No runtime memory cost; version update policy remains release-owner choice. Audit passes. |
| pipeline/Foliage Dynamics/Kernels/FoliageDynamics/WorldWind.hlsli | Full; noise/direction/temporal field | No change; bounded parameter floors and zero-direction fallback | No resources; four hash corners plus trigonometric field channels per evaluation. Default fallback normalized by convention; NaN/huge-time/world precision not stress-tested. Shared field preserved, future origin tests. |
| pipeline/Foliage Dynamics/Kernels/FoliageDynamics/FoliageWind.hlsli | Full; compatibility wrapper for shared field | No change; trivial forwarding preserves accepted signals | Compiler can inline wrappers; no duplication of resources. Missing historic MultiScaleField is only referenced by disabled tree experiment. Do not turn it on. |
| pipeline/Foliage Dynamics/Kernels/FoliageDynamics/FoliageTuning.hlsli | Full; PS b13 24-field layout and validated accessors | No change; Magic/Version align with CPU | Graceful mismatched-b13 defaults retained. Future reflected per-field layout test; no register reassignment. Compiled through grass PS. |
| pipeline/Foliage Dynamics/Kernels/FoliageDynamics/FoliageDynamics.hlsli | Full; GGX, transmission, wrapped diffuse, reconstructed tangent basis | No change. USE_PIXL_VEGETATION_BRDF=0 branch references HdotN declared only in the enabled branch; not a tested/supported shipped default | Positive sqrt/pow guards and half-vector fallback retained. TBN protects reciprocal but downstream normalize can still see degenerate inputs. Shared Oren-Nayar contains a zero-denominator direction limit worth WARP testing. Do not claim all macro experiments compile. |
| distribution/Shaders/RunGrass.hlsl | Full, 1516 lines after change; VS/PS engine entry | Linear point-light flag fix, MEDIUM | Dense grass overdraw, fixed layout probes and local-light loop are probable costs. Preserve world history, layout detection, cutout/AA, paired transforms. Potential zero light/normal/distance denominators, dynamic shadow index contract, diffuse/specular MRT calibration need numeric/capture tests. Strict 32-case compile passes; no scene timing or images. |
| tools/TestPixlFoliageSettings.ps1 | Full; actual structs/serialization/functions with existing nlohmann dependency | New offline regression tool | Local compiler, unique ignored build fixture; no live writes. Tests reload, malformed optional extension, bounds, boolean normalization, round-trip/defaults and sizes. Pass /W4 /WX /O2 /fp:fast. |
| tools/TestPixlGrassShaders.ps1 | Full; representative engine grass FXC matrix | New offline compiler test; optional canonical entry source over assembled includes | No live mutation. Results/logs/source hash stored in ignored build directory. Does not enumerate all module subsets, VR or custom experimental macros. |
| distribution/Shaders/Lighting.hlsl | Partial; TREE_ANIM wind/normal/BRDF callsites | No change; disabled historical wind block identified | Full engine material/permutation review remains open. No tree motion claim based on grass test. |
| distribution/Shaders/Common/Color.hlsli | Partial for current accounting; relevant gamma/linear and output helpers read | No change; confirmed isLinear controls gamma and point multiplier | Shader calibration retained. PQ conversion domain, duplicate matrix constants and negative handling need source-wide caller tests. |
| distribution/Shaders/Common/BRDF.hlsli | Partial; diffuse/Fresnel/GGX callers | No change; Oren-Nayar singular-direction denominator identified | Future full numeric/energy review. Not considered fully reviewed just because included by compile tests. |
| pipeline/Radiant Grid/Kernels/RadiantGrid/Common.hlsli | Full; flags, Light/LightGrid/cluster ABI declarations | No change; Linear bit 11 matches CPU enum | 80-byte Light and 16-byte LightGrid declared; CPU offsets beyond flag still require full module review. Preserve room mask/capacity. Compiled in integrated cases. |
| pipeline/Radiant Grid/Kernels/RadiantGrid/RadiantGrid.hlsli | Full; b3 strict light data, t35–37, cluster indexing/room filtering | No change; z/near/log require valid camera data; shadow index/room bounds rely on producers | Fixed loop over four room-mask words, no resource allocation. Further producer/capacity validation pending; full source read not full module validation. |
| engine/Modules/NaturalLighting.cpp | Partial; authored light flags and forwarding | No change; confirms linear bit propagation, not an inferred external behavior | Hook/attenuation remainder still pending. |
| engine/Modules/RadiantGrid.cpp | Partial; light collection and flag forwarding | No change | Complete threading/collection/capacity review pending. |
| engine/Modules/GroundResponse.cpp | Partial; final grass binding hook | No change | Confirms b13 rebind after engine setup. Full deformation/resize/hook review remains open; no changes to successful snow subsystem. |

## Evidence and remaining validation

- Settings tests: `build/foliage-settings-tests-9cb8f92597f3422d8874cad5068c5838`.
- Before shader correction, all 32 cases passed in `build/grass-shader-tests-ab6013f86a54412dae34c8bcdc619987`.
- Final source rerun: all 32 passed in `build/grass-shader-tests-9b0069a9829f432ab1b037f3d36f0f8e`; RunGrass SHA256 `310E4690E2D359C450BFB3EB78BFBE3F839584F8F7C4E3FEE72C21FFD27D3A2B`.
- Build logs: `build/final-release-records/foliage-settings-build-20260907.log` and `foliage-integration-build-20260907.log`, both exit 0. RC-04 contains the final source/DLL and passes payload/source/catalog audit and archive integrity test.
- In-game: test existing grass packs, warm/cool linear flagged lights with Linear Light Core on/off, reload legacy/custom presets, trees with enhanced lighting off and normal flip on, weather gusts, fast movement/origin shifts and DLSS motion. No hardware frame-time improvement is asserted.

## 5 Future Visual Improvements

1. Tagged-light reference scenes; 2. degenerate normal stress fixtures; 3. wind history/origin-shift captures; 4. mip/packed-atlas edge coverage tests; 5. MRT specular energy comparison.

## 5 Future Performance Improvements

1. Measure sentinel texture-read cost; 2. profile dense light loops; 3. measure repeated b13 uploads; 4. compare projected LOD occupancy/bandwidth; 5. profile foliage overdraw at 1080p/1440p on RTX 3060 Ti.

## 5 Future Feature / Research Ideas

1. Material-identified packed-layout cache; 2. isolated authored-tree wind experiment with correct helpers; 3. per-control dependency/localization UI; 4. D3D reflection ABI regression suite; 5. automated vegetation temporal capture benchmark.
