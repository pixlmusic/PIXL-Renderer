# Water Optics — current source review

## Purpose and pipeline position

WaterOptics loads receiver-caustics and foam textures during State::Setup's module resource stage, binds PS t65/t66 during Prepass, and packs a 64-byte settings block into FeatureData. It supplies pixel-stage code to Lighting, Water and image-space reflection tracing. Water motion/flow itself is shared with Waterbody and the main Water.hlsl; this report does not claim their entire review is finished.

Inputs: caustics DDS, linear foam PNG, directional light/weather, absolute/camera-relative world position, water surface height, scene depth, normals, reflection buffers and serialized settings. Outputs: receiver caustic modulation, parallax coordinates, water reflection/refraction weights and contact-foam contribution. No independent temporal history is owned by this C++ module. Texture ownership is via com_ptr; engine resource creation invokes State::Setup. Resize/world/temporal behavior also depends on the surrounding renderer.

## Five improvement investigations

1. **Independent water controls (LOW, implemented).** DrawSettings placed Reflection Balance and Water Tint inside the enhanced-SSR disabled scope. Water.hlsl reads ReflectionBrightness on the complete reflected lobe and WaterTintStrength in absorption independently of EnableEnhancedSSR. Moved EndDisabled before those two sliders. Bounds, defaults, serialization and FeatureData upload remain identical. Visible result: controls remain usable with enhanced SSR off. No GPU cost or shader behavior change. Human UI interaction verification remains required.
2. **Resource reload ownership (investigated; suspected leak rejected).** SetupResources supplies causticsView.put() without an explicit reset, unlike foamStencilView. The actual pinned C++/WinRT implementation calls release_ref() in put(), so adding a reset fixes no demonstrated leak. Retain ownership behavior. Missing caustics currently logs an error but null sampling can still modulate lighting; a genuinely neutral missing-asset fallback is a remaining opportunity, not a verified graceful fallback. The package audit requires the asset.
3. **Caustic stability/cost (investigated; deferred).** WaterCaustics.hlsli uses multi-scale focused samples, chromatic offsets, depth attenuation and bounded HDR modulation. SampleFocusedCaustics samples the center again through SampleCausticsDispersion; determine whether FXC already eliminates the duplicate before claiming fewer texture reads. normalize(DirLightDirection) lacks a local zero-length guard; trace the authoritative light initialization and test zero-light transitions before changing appearance. No sample-count or energy-law rewrite was made.
4. **Water parallax/mip stability (investigated; deferred).** WaterParallax.hlsli already bounds the grazing denominator, refines intersections with a denominator guard, and keeps dither phase fixed rather than frame-random. Its mip estimate derives an offset from texture width while ignoring rectangular height; a direct two-axis footprint is a future compatibility improvement, but square-texture magnification behavior must be compared before release. The flowmap solver has bounded 8–32 steps but some retained helpers appear unused; callsite proof is required before removal. No broad wave/parallax rewrite was justified by these checks.
5. **Settings, ABI and asset cost (verified boundary; retained).** All 16 CPU fields match the four-register HLSL WaterOpticsSettings order. Header asserts size 64/alignment16; retired PlayerWakeStrength remains zero on load. UI float bounds match load clamps; null legacy JSON maps to defaults. Numeric type errors/nonfinite upstream values still need full config exception-path review. Caustics DDS header is 256x256, 9 mips, DX10; foam PNG decodes to 2048x2048 ARGB. RGBA8 foam plus a full mip chain is approximately 21.33 MiB if that is the actual GPU format (estimate, not measured allocation). Converting to one-channel data could reduce bandwidth/VRAM, but needs format/appearance validation and is deferred.

## File-by-file findings

| Path | Reviewed scope | Changes | Dependencies / risk / validation | Future work |
| --- | --- | --- | --- | --- |
| engine/Modules/WaterOptics.cpp | Full source read | SSR-independent controls re-enabled | LOW UI-only; C++ build and shader-consumer tracing | Missing-texture neutral fallback; caustic control enable-state trace; native UI tests |
| engine/Modules/WaterOptics.h | Full | None | 64-byte ABI assert, com_ptr ownership, pixel-only invalidation contract | Per-field offset assertions if shared contract expands |
| pipeline/Water Optics/Module.ini | Full | None | Integrated, Id WaterOptics, version 1-3-0; package catalog | Avoid version bump for unchanged shader ABI |
| pipeline/Water Optics/Kernels/WaterOptics/WaterCaustics.hlsli | Full source read | None | t65, SharedData/FrameBuffer/sampler supplied by caller; numerical/cost findings above | Light-direction guard, compiler sample-count evidence, caustic toggle semantics |
| pipeline/Water Optics/Kernels/WaterOptics/WaterParallax.hlsli | Full source read | None | PS_INPUT, normal/flow textures, derivatives, fixed-noise mode | Rectangular mip footprint and genuinely dead helper proof |
| pipeline/Water Optics/Kernels/WaterOptics/watercaustics.dds | Asset boundary | None | DDS magic/header/dimensions/mips checked; staged SHA matches source | Inspect actual DXGI format and live GPU allocation |
| pipeline/Water Optics/Kernels/WaterOptics/FoamStencil2K.png | Asset boundary | None | Successfully decoded 2048x2048 ARGB; staged SHA matches source | One-channel GPU format experiment with A/B |
| distribution/Shaders/Water.hlsl | Selected consumers, NOT full file yet | None | Reflection/tint/foam consumers traced | Complete shader review and all permutation compilation |
| distribution/Shaders/Common/SharedData.hlsli | WaterOptics block only, NOT full file yet | None | 16-field order checked against CPU | Full global ABI and shader consumer review |

No shader source or texture was changed, so no altered shader interface requires deployment. CPU build and whole-project audit are the implemented change's automated checks. In-game off/on SSR slider interaction is pending; no measured fidelity/performance claim is made.

## 5 Future Visual Improvements

1. Rectangular normal-map mip stability; 2. neutral missing-caustics fallback; 3. zero-light initialization robustness; 4. shoreline temporal comparison; 5. validate caustic toggle semantics without changing the existing look.

## 5 Future Performance Improvements

1. Measure caustics sample common-subexpression elimination; 2. one-channel foam upload; 3. profile grazing parallax steps; 4. measure far-distance foam bandwidth; 5. retain pixel-only cache invalidation.

## 5 Future Feature / Research Ideas

1. Waterbody-driven nonperiodic wave variants; 2. receiver-aware caustic confidence; 3. authored flow seam tests; 4. automated underwater/camera-cut scenes; 5. GPU resource accounting UI. These are proposals, not implementations or promised release behavior.
