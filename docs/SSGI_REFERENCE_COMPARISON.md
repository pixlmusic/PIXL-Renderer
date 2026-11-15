# SSGI and Camera Accumulation Reference Comparison

Reviewed against:

- `_REFERENCE/UnitySSGIURP-main/UnitySSGIURP-main`
- `_REFERENCE/Camera Accumulation Renderer Feature/Camera Accumulation`
- `_REFERENCE/dx11screenspaceshadows/dx11win10tut41_src/source`

## Unity SSGI port

The Unity package is a renderer feature, not a standalone ray-march shader. Its active path schedules a camera-color split, traces diffuse rays, optionally renders backface depth/color, reprojects temporal diffuse history, applies spatial denoising, and composites the result. The tracing implementation also contains Unity-specific reflection-probe/APV fallbacks and rendering-layer support.

PIXL cannot reuse the URP pass code or its texture contracts. PIXL's active implementation is a DX11 compute pipeline in `HybridGI.cpp` with a screen-space compute tracer, game depth and normal resources, a canonical deferred radiance buffer, geometry-validated temporal reprojection, spatial denoising, and deferred composition. The current SSGI shader is an adapted screen-space method; it is not a line-for-line or feature-for-feature port of every URP mode. Unity-specific APV, rendering layers, forward GBuffer, backface lighting, and probe fallback are not active PIXL features. Those differences are intentional engine integration limits, and should not be described as already ported.

The PIXL UI exposes AO strength/reach, GI strength/reach/saturation, contact depth, sampling quality, and temporal/spatial denoising. These values reach the SSGI constant buffer. Before this change, however, changing those controls did not invalidate the denoiser's old accumulated radiance/AO. Since the temporal blend can favor history, a slider change could appear to do little until enough frames had elapsed. `HybridGI::DrawHybridGI` now detects meaningful changes to SSGI/AO artistic and sampling settings, clears the temporal signal once, and logs the invalidation. This preserves the normal temporal accumulation between setting changes.

## DX11 local-light shadow reference

The DX11 tutorial at `_REFERENCE/dx11screenspaceshadows/dx11win10tut41_src/source` implements conventional projected shadow mapping: a separate depth pass renders geometry from the light camera, then the receiver shader projects into that depth texture and compares light-space depth with a bias. It is not a screen-space shadow method and does not provide an algorithm that can be transplanted into PIXL's camera-depth path.

PIXL's point-light screen-space supplement is in `distribution/PIXL-Shaders/RadiantGrid/RadiantGrid.hlsli::GetScreenSpaceLocalShadow`. It projects a receiver-to-light segment through the game camera, samples the current scene depth, linearizes it with PIXL's camera parameters, and attenuates the local light when a visible surface lies in front of the ray. `Lighting.hlsl` ranks eligible lights per pixel, runs this supplement for up to four influential lights, and combines it conservatively with native/cached shadow visibility using `min`; it does not replace Skyrim's native shadow map. Its limits are inherent to screen-space tracing: off-screen and hidden blockers are unavailable, and thin blockers can fall between the fixed samples. The UI checkbox is serialized as `EnableScreenSpaceLocalShadows`, copied into Radiant Grid's per-frame shared data, and checked in the deferred lighting permutation.

The active Steam shader payload for `RadiantGrid.hlsli` was byte-identical to `distribution/PIXL-Shaders` and `pipeline/Radiant Grid/Kernels/RadiantGrid` during this review. The separate non-Steam `H:\The Elder Scrolls - Skyrim - Special Edition\Data\PIXL-Shaders` copy differed; the running game uses the Steam install under `H:\SteamLibrary\steamapps\common\Skyrim Special Edition`.

## Camera Accumulation reference

The Unity feature progressively blends the current post-processing frame into a persistent color target. It resets its sample count when camera matrices or visible-light/probe sets change, caps the running sample count according to color format, and optionally draws a convergence indicator.

PIXL Photo Finish uses a finite capture transaction rather than a permanent renderer pass: it can warm up lighting, captures distinct camera-jitter phases, resolves them into a frozen view, then performs optional output scaling and finishing. Its sample count is bounded by configured quality and a 512 MiB staging budget. Its resolve uses jitter-aware alignment/super-resolution and robust sample weighting. This is appropriate for still capture and avoids accumulating unrelated gameplay frames, but is not the same persistent progressive blend as Unity's renderer feature. Camera and world state are held by the photo-capture flow; dynamic scene changes during a burst remain a source of sample disagreement.

## Payload and validation contract

`HybridGI.cpp` compiles shaders from `Data\\PIXL-Shaders\\HybridGI`, which is packaged from `distribution/PIXL-Shaders/HybridGI`; the internal `pipeline/Hybrid GI/Kernels/HybridGI` tree is the editable mirror and must remain byte-matched for active shader files. The runtime C++ `HybridGICB` and HLSL `HybridGICB` are append-only ABI contracts with compile-time offset checks.

Validation completed for this pass: the Release renderer built, all 40 targeted runtime shader-matrix cases compiled from the Steam `Data/PIXL-Shaders` tree with `/WX /Ges`, the deployed DLL SHA-256 matches the Release output, and deployment verified all 316 selected DLL/shader entries (315 already current, one DLL replaced). The live shader cache and user settings were preserved. Runtime logging of an AO/GI slider change and visual effectiveness still require an in-game scene check; compile/readiness logs cannot prove the desired amount of bounce light or crevice occlusion.

## Known follow-up work

- Add bounded GPU readback diagnostics for SSGI hit rate, AO range, and indirect radiance range so an active pass that produces neutral output can be distinguished from a composition issue.
- Evaluate a more faithful multiscale screen-space tracing schedule and robust thin-geometry handling against Skyrim interiors and foliage.
- Consider a Photo Finish option to pause the burst when camera/light state changes, rather than resolving disagreeing samples into one still.
- Do not add Unity's reflection-probe/APV code paths to PIXL without equivalent Skyrim resource contracts.
