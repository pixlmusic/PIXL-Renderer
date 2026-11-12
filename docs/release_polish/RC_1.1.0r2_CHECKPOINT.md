# PIXL Renderer 1.1.0r2 Release Candidate Checkpoint

Date: 2026-11-12

## Candidate artifacts

- Staged release candidate: `dist/PIXL-Renderer-1.1.0r2-RC2-20261112`
- Core plus optional PGLite FOMOD: `dist/PIXL-Renderer-1.1.0r2-FOMOD-20261112-223110.zip`
- Archive SHA-256: `9B4717D2E1A65E56192434B40DF58CF038D39CAD215BDFA65EFE2A90E6ED64AF`
- Renderer DLL SHA-256: `2697730EF78F8577365ABF39B29E7CFB9F0A145D788A146EC7889716F5E5D0CD`
- The FOMOD archive passed 7-Zip integrity validation and the PIXL package/FOMOD validator, including the FOMOD 5 schema.

The installer always installs PIXL Core. Its optional PIXL-aware PGLite runtime checks for an active `SKSE/Plugins/PGLite.dll`; if that file is missing or inactive, the addon is unavailable and the installer explains that PGLite must be installed and activated first. The existing `PGLite.ini` is preserved. The adapter source and setup guide remain a separate optional choice. PGLite is not included in PIXL Core.

## Live visual checkpoint

The owner confirmed the Steam build's latest neural Photo Finish looked best so far. The active log records DLSSNR 310.8.0 validation and initialization, Photo Finish selecting the neural source, and 32/32 samples completed with 32 unique projection jitters. Screenshot: `H:/SteamLibrary/steamapps/common/Skyrim Special Edition/Screenshots/PIXL-R-1.1.0-0010.png`.

The Steam installation was not changed for the hair adjustment in this candidate. The latest image therefore validates the pre-adjustment live build; a same-scene capture is still needed to judge the hair change below.

## Hair shading adjustment

The live user settings had direct `SpecularMult=0` but `SpecularIndirectMult` near 1.0, so the environment sheen could remain visible even with direct highlights disabled. In addition, all four tangent-shift sampling paths forced mip 0. That keeps the highest-frequency flow variation on small hair cards and can produce thin, noisy specular edges.

The candidate hair include samples at LOD 0.75, blending slightly toward the first authored mip. This is a deliberately small filter adjustment: no resource binding, constant-buffer layout, or material-setting ABI changed. The targeted `StrandShading.Hair` DX11 permutation compiled with FXC `/WX /Ges`. The new hair permutation will recompile on first launch because its include content differs from the live-cache artifact; the rest of the validated cache remains available.

## Cache and build provenance

The cache was copied from the active Steam installation at `Data/PIXL/PipelineLibrary` after the latest live session: 3,577 `.pixlbin` stages and 3,577 matching identity sidecars. The release candidate manifest reports `cacheMode=preloaded` and `preloadedPipelineStages=3577`. The product build is `1-1-0-0` with the cache's current layout and shader ABI.

The C++ Release build was completed before this checkpoint while resolving the Neural Rendering runtime path and status reporting. The hair change is shader-only. A broader shader-matrix script stopped at its DistantLife case because that unreleased module is intentionally omitted from the release candidate; the targeted shipping hair permutation passed.

## Remaining release-test items

- A/B the adjusted hair in the same close-up scene, especially under interior warm lights and cool exterior light.
- The owner noted possible SSGI performance cost; it remains unchanged in this checkpoint and needs separate performance analysis.
- Runtime testing of the FOMOD's PGLite file detection through MO2 and Vortex remains advisable; the file dependency and package paths validate structurally.

## Source snapshot note

The package manifest records source commit `589c257604b43c8f9b6b5e160d81a78123a1af26` and `sourceWorkingTreeDirty=true`. This is an artifact checkpoint from the current working-tree snapshot, not a Git commit. The repository contained substantial pre-existing user changes, so they were not folded into a broad commit.