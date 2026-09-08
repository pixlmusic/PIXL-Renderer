# Background blur / live preview — September 7

Full source read: engine/Menu/BackgroundBlur.cpp/h and all three distribution/Shaders/Menu/BackgroundBlur*.hlsl. Related OverlayRenderer finalize and CameraSuite presentation/capture call sites inspected only. This is not a full CameraSuite or presentation audit.

## Architecture / dependencies

Menu initialization compiles horizontal VS_Main and four PS entries (horizontal/vertical PS_Main, composite PS_Main/PS_Clear). OverlayRenderer calls RenderBackgroundBlur after ImGui::Render and before ImGui DX11 draws, unless compiler-exclusive backdrop is active. Startup/loading shader-work guards prevent previous-UI feedback. Sources are CameraSuite HDR textures/separate UI, existing reconstruction bridge blur resources or current DX11 RTV. No DX12-only redesign introduced. Scene downsample → optional premultiplied HUD blend → horizontal/vertical filter → rounded-window composite → optional HUD clear. CPU b0 BlurConstants and b1 WindowConstants are each 32 bytes; t0/s0 used locally, no shader includes. Settings toggle → SetEnabled; dimensions/format cause resource reallocation, Cleanup releases resources and previews.

Live preview is separate: CameraSuite clean-scene snapshot → finishing compose → SRV copy, throttled to 30 Hz when requested. Effects-off temporarily changes CameraSuite settings under its settings mutex, restores them after compose and updates HDR data. Preview references are borrowed, backed by module-owned COM objects. Full exception/thread/resize safety is not established by this slice.

## Five investigations

1. **Off-screen cost/invalid scissors — implemented LOW.** Window bounds entirely outside the texture (or degenerate) previously still ran full downsample/blur and could generate inverted clipped scissors. A pure clipped-area test skips them. Partial edge windows remain eligible. Exact helper tested for visible, partly clipped, each off-screen edge, reversed and zero-size cases in TestPixlFonts.ps1. No change to visible blur math or shader source. Expected savings: four to six draw calls per skipped eligible window, no measured frame-time claim; shared source acquisition may still occur.
2. **CPU/HLSL contract — implemented LOW.** Added size and second-vector offset assertions for both buffers. HLSL float4/int4 map to 0/16, fixed b0/b1 bindings retained. Production build passes and seven strict FXC VS/PS cases pass, including two unused alternate VS entries. No ABI/layout modification.
3. **Pass state — HIGH investigation, unresolved.** PerformBlur saves only one RTV/DSV, one viewport and rasterizer. It changes shaders, constants, samplers, blend and scissor but does not restore all. Initial passes inherit blend/scissor/topology and other programmable stages. CameraSuite's inspected wrapper only saves viewport; ImGui backend runs afterward and cannot retroactively restore the pre-blur state. A dedicated WARP/render-state contamination fixture is required before choosing the scoped state contract. Do not claim generic state safety. No speculative all-pipeline state rewrite in this pass.
4. **Filter/color/performance — investigated, retained.** Positive weights normalize to unit DC gain, center-only mode downsamples and sample cap bounds weight indexing. Duplicate horizontal/vertical implementations are small and stage-specific. Four-sample rotated composite uses screen-fixed noise, not animated temporal noise. Comments claim Gaussian sigma=2 and normalized table, but truncated normalization is recomputed and weights warrant analytic reference checks. Eightfold bilinear downsample can alias; UI blur, not reconstruction input. Blend-factor alpha is supplied but SRC_ALPHA uses shader alpha instead, so changing it to the apparent comment intent would strongly change the existing look. No PQ/linear rewrite without CameraSuite format-space confirmation.
5. **Resource/lifecycle/preview — investigated, retained.** Three 1/8-dimension textures, single mip; sets release on partial allocation failure. Cached source SRV avoids repeat creation, but failed creation logs/retries; initialized failure remains latched until Cleanup. Live preview copy retains a full-resolution texture after the page closes and temporary camera-settings mutation lacks exception-guard restoration. Preview clock rollback/device identity and inherited GPU state need fixtures. These are not resolved by compile success. Architectural consolidation with CameraSuite is deferred until both full paths are reviewed.

## Per-file record

| File | Purpose / dependencies / quality | Findings and changes | Implications / risk / verification | Future |
| --- | --- | --- | --- | --- |
| engine/Menu/BackgroundBlur.cpp | Full; D3D11 shaders/resources, per-window passes, clean scene preview; Menu, CameraSuite, ImageReconstruction and globals | Added clipped-area skip and two buffer assertions; lifecycle/state concerns above remain | LOW implemented, HIGH open state/lifetime work. Build blur-guard-build-20260907.log exit 0. No networking/extra filesystem behavior | State-contamination rendering test, preview settings scope guard, allocation failure tests |
| engine/Menu/BackgroundBlur.h | Full; module API and borrowed LivePreviewFrame SRV/dimensions | Unmodified; comment describes presentation target but implementation uses clean CameraSuite compose | No new ABI/resources; source/build check | Clarify borrowing/invalidations, preview release API only after consumer trace |
| distribution/Shaders/Menu/BackgroundBlurHorizontal.hlsl | Full; fullscreen VS, normalized symmetric x-axis samples; b0/t0/s0 | Unmodified; no depth/world reconstruction, positive weight denominator, <=7 neighbor pairs | Two strict compile entries pass; valid HDR not clamped, source color space inherited | Filter impulse/DC image tests, fixed known-count permutation savings |
| distribution/Shaders/Menu/BackgroundBlurVertical.hlsl | Full; equivalent y-axis filter and duplicate unused VS | Unmodified; same numeric bounds/denominator | Two strict compile entries pass; no temporal history | Retain separate shaders unless measured benefit from common include |
| distribution/Shaders/Menu/BackgroundBlurComposite.hlsl | Full; rounded-rect SDF, 4 rotated filtered samples, PS_Clear transparent HUD mask; b1/t0/s0 | Unmodified; source dimensions positive from D3D, radius upper clamp, sample coordinates clamped by sampler, division guards rely on caller | Three strict compile entries pass; clear mask/edge opacity differs slightly by intended epsilon. No temporal random seed | Rounded-edge captures, negative-radius user-state guards, actual color-space/alpha reference tests |

Evidence: build/menu-shader-tests-37d859cb7a9b45d28c797fe824b0a784/results.csv, FXC /Ges /WX /O3; build/font-tests-89450af867a247bfa27f2e076e35c71c/test.log includes exact clipped-area cases and the font tests; native build/final-release-records/blur-guard-build-20260907.log exit 0. No WARP execution of blur shaders or in-game blur capture yet.

## Cost estimates (not measured)

Four draws/window without separate HUD (downsample, horizontal, vertical, composite), up to six with HUD blend/clear. H/V each nine samples at 1/64 pixel count; final composite four samples at window-covered full resolution plus SDF/trigonometry. Three persistent reduced textures cost `3 * max(1,W/8) * max(1,H/8) * bytesPerPixel`, excluding driver overhead: FP16 RGBA about 0.74 MiB at 1080p and 2.97 MiB at 4K. Full-resolution preview adds W*H*format bytes (about 63.3 MiB for 4K RGBA16F), plus CameraSuite-owned capture resources not charged twice here. Overlapping roots repeat expensive work and can sample earlier blurred results; preserve current layering until visual equivalence tests exist.

## 5 Future Visual Improvements

1. Color-space reference captures; 2. stable downsample filter; 3. rounded edge/HUD mask agreement; 4. overlap behavior; 5. effects-off preview equivalence tests.

## 5 Future Performance Improvements

1. Blur once/composite multiple windows after layering tests; 2. fixed nine-tap coefficient optimization; 3. preview idle release; 4. allocation retry backoff; 5. measured reduced-resolution preview quality.

## 5 Future Feature / Research Ideas

1. WARP state-contamination suite; 2. explicit scoped resource/state ownership; 3. failure-injected clean capture; 4. resize/device-reset automation; 5. unified presentation-space metadata.
