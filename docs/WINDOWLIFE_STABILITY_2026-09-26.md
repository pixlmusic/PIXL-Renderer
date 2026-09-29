# WindowLife pane stability follow-up

## Scope and evidence

Owner reports fine flickering lines on vanilla Whiterun and Solitude windows,
both moving and standing still; patched/parallax assets reportedly avoid it.
This pass fixes demonstrated shader instability risks. It does not claim a
reproduced GPU depth conflict or an in-game visual pass.

## Traced connections

- `RenderModule.cpp` registers WindowLife last, after SkinOptics.
- `XSEPlugin.cpp` calls loaded modules' PostPostLoad; WindowLife installs the
  Lighting SetupGeometry vfunc hook. It calls the previous hook first, then
  binds its own resources. MaterialForge, RadiantGrid, LinearLightCore,
  ActorSurfaceEffects and SkinOptics retain their chained calls.
- `State.cpp` invokes module resource creation. WindowLife releases old views,
  clears classification and upload caches, and creates active/neutral buffers.
- `WindowLife.cpp` classifies diffuse/glow paths and optional exact masks;
  frame data selects room artwork outside and outdoor day/night artwork inside.
  Helper-mask meshes and boarded windows are excluded. Noncandidate draws bind
  neutral data so a previous window's state cannot leak into another material.
- CPU `PerGeometryData` and HLSL `PerDrawData` contain the same sixteen float4
  fields (256 bytes). Existing size assertions remain. PS t122..t127 match the
  C++ contiguous binding; no competing declarations were found in active shader
  source. t124 remains curtains outside / night outdoor atlas inside.
- `Lighting.hlsl` evaluates WindowLife within the existing material pixel
  shader. Authored color replaces window emission and reduces its painted
  diffuse contribution; glass reflection/weather optics are applied afterward.
  WindowLife creates no background geometry and issues no extra Draw/Dispatch.
  Thus its two color layers cannot independently compete in the depth buffer.
  Actual overlapping NIF geometry remains a possible separate cause.
- The existing authored complex/PBR parallax paths retain their precedence.
  Auto-POM runs only where its original eligibility and user setting allow it.
- WindowLife module version changes use existing selective shader-cache
  invalidation. No shader ABI, resource slot, global revision or config change.

## Changes

1. Pane erosion uses explicit footprint-selected mip levels instead of forcing
   twelve border samples to mip zero. Texture-tile and central coverage guards
   remain. Close-range mip-zero behavior is preserved; distant detail is filtered.
2. Room-plane derivatives use camera-relative position before the large world
   origin is added, avoiding precision loss in the reconstructed face normal.
   World-space anchoring and deterministic room selection remain in place.
3. Auto-POM relief fades smoothly over pane coverage 0.02..0.34 instead of
   switching all UV displacement at a binary threshold. UVs, strength and pixel
   offset use the same weight; zero-coverage opaque frames retain full relief.
4. Physical glass and authored background consume one shared final pane mask.
   This removes a duplicate erosion stencil and keeps their coverage identical.
   Candidate, map-menu and distance guards avoid sampling on inactive surfaces.
5. WindowLife version is `0-7-21`; the shader test adds MaterialLayers, glow,
   deferred environment, alpha-test and PBR combinations (14 cases total).

No depth bias, alpha-test override, texture replacement or default retuning was
introduced. Missing atlas behavior and interior curtain exclusion are retained.
The original material remains a compatibility fallback and restrained tint;
this patch does not force every detected window to become fully opaque.

## Validation and remaining risks

Release build and integrated 37-module audit passed. Shader compilation uses
FXC ps_5_0 with /WX /Ges /O3. Core staging/deployment use the existing scripts,
including manifest hashes and timestamped rollback. The new Core is a
compile-on-device release candidate: the previous Steam preloaded cache cannot
be labelled current after this shader change. Deployment preserves live cache
and user settings; startup invalidates affected stages using module metadata.

No GPU timing or Skyrim visual test was performed. Filtered native masks can
change coverage of distant thin mullions; inspect these alongside the original
reported flicker. If the problem persists, capture a frame or inspect the exact
NIF to distinguish coplanar surfaces from shader masking, projection or temporal
reconstruction. A global depth offset was rejected because it could break frame
occlusion and hide an unrelated mesh conflict.

## Live test

Deployment completed to the authorized live Skyrim installation with four
verified files: PIXLRenderer.dll, Lighting.hlsl, WindowLife.hlsli and
WindowLife.ini. Backup:
`build/deployment-backups/RC-DayNight-20260926-040555-f549604504ad43a693151a3ea28850bd`.
Final 14 shader cases passed with warnings treated as errors; archive integrity
and the 340-payload manifest passed. Package:
`dist/PIXL-Renderer-1.0.4-WindowLife-Stability-20260926-Core.zip`.
SHA-256: `CDFF82DDEF31524BDC9239596596B14CFDCD9F8D1A92C3C7063180EA6D69B6D0`.

1. Let affected shaders finish compiling. At the same vanilla Whiterun and
   Solitude windows, watch stationary for 15 seconds, then strafe and approach.
2. Check close, mid-distance and grazing views. Inspect lead, wood and stone
   borders for both flickering lines and background leaking onto frames.
3. Repeat indoors looking out, at day and night. Confirm outdoor refraction,
   emission and softness work, and curtains remain absent indoors.
4. Compare Auto-POM suppression enabled/disabled, then authored backgrounds
   enabled/disabled. Check a patched complex/PBR window as a regression control.
5. Compare native AA and the usual DLSS mode. If necessary use WindowLife's
   Pane Mask and Room Basis diagnostics to identify unstable coverage/basis.
6. Toggle WindowLife, load another cell and return. Verify no ghost room appears
   on non-window objects and no resource errors appear in PIXLRenderer.log.

## Future improvements

- Visual: exact-mask mip authoring, foreground depth cards, per-asset pane
  topology, authored room depth, independent frame-shadow filtering.
- Performance: profile border stencil cost, cache robust aperture fits,
  specialize exact-mask filtering, measure atlas bandwidth, amortize draw uploads.
- Research: capture-based overlap detection, temporal coverage validation,
  room motion vectors, patcher metadata, per-mesh compatibility fixtures.
