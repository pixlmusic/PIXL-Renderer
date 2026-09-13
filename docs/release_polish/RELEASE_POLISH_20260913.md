# September 13 release polish / owner live test

## Changes and bounds

- Defaults copied from latest owner settings, preserving ImageReconstruction settings except both upscaling methods set to None (0); first-run setup remains incomplete. Live upscaling stays unchanged. Ground tessellation alone increases from 14/3 to 16/4, without extending coverage. Increased geometry work is expected; no GPU timing claim.
- Auto-POM disabled on alpha-tested architectural surfaces. Synthesized height cannot infer transparent atlas topology. Opaque materials and dedicated authored parallax remain enabled. This is a conservative cutout safeguard, not a confirmed asset-specific Markarth fix; the owner must retest the pictured wall and provide its mesh/texture if still broken.
- WindowLife uses calendar hour and active climate twilight for both interior outdoor-view artwork and exterior room warmth. Interior night emission is capped separately (max 0.35 versus day max 8). Outdoor atlas mip selection now responds to refraction/softness. Existing resource bindings t124/t126 and 240-byte payload unchanged.
- Curtains keep an aperture-space central opening, even with closed atlas tiles. Auto sizing requires stronger confidence and rejects fits outside 0.5x–2x calibrated dimensions. Moving foreground layers require high confidence. Owner manual sizing and curtain strength defaults retained. Automatic sizing tooltip moved to its actual checkbox.
- Both public and advanced FG enable controls arm low-refresh override immediately. One restart is still required to create the presentation sidecar, not two. Hardware/backend/borderless requirements remain enforced.
- Main public panels share compact spacing/padding; camera controls gain width relative to preview. Shortened photo header avoids collision. Theme unchanged. Advanced tuner window can grow/move within viewport bounds while preserving its minimum usable canvas and fixed control scale. Visual usability still needs in-game validation.
- Cache plugin-version bumps no longer force a clear when ABI/layout match. Module versions select affected shader stages. See INCREMENTAL_CACHE_UPDATES.md. Ownership markers are skipped. No claim of persistent per-permutation include hashing.

## DLSSG distribution

The NVIDIA Streamline DX12/DLSSG runtime and notices are already part of the plugin. The user's root version.dll and dlssg_sm86.ini proxy already exist and are not replaced. The optional SM86 proxy is not bundled: upstream THIRD_PARTY_NOTICES identifies separately licensed NVIDIA-derived assets without establishing complete redistribution permission. Install it separately from https://github.com/sdli1995/dlssg_for_sm86 if needed. Do not overwrite an unrelated version.dll proxy. This package includes installation guidance, not a silent updater/downloader.

## Scoped file accounting

Owner repro: `coc MarkarthOrigin`, building opposite arrival, right-hand window. Exact mesh/texture unknown.

Completed checks: Release C++ build; six WindowLife pixel shader cases after curtain/sizing changes; 32 strict material VS/PS cases; dependency tracker union/removal/32 concurrent-writer test; all 14 package-manifest regression cases. Shader tests compile fixtures only, not the user's full cache.

All files below are active and reviewed/modified for this scope; security, fidelity, correctness, performance and maintainability implications were reviewed. No unrelated third-party changes or removed renderer modules.

| File | Purpose/dependencies and findings | Validation / future |
|---|---|---|
| distribution/SKSE/Plugins/PIXLRenderer/SettingsDefault.json | Owner graphics defaults; setup/ImageReconstruction exceptions and bounded ground tessellation adjustment | JSON diff assertions; fresh-start owner test pending |
| distribution/Shaders/Lighting.hlsl | Auto-POM eligibility; cutout fallback preserves alpha topology | 32 VS/PS material cases; exact Markarth asset pending |
| pipeline/Material Layers/Module.ini | Shader change identity; existing conservative pixel-stage contract | Cache export/version checks; future finer contracts |
| pipeline/WindowLife/Kernels/WindowLife/WindowLife.hlsli | Atlas brightness, mip blur, curtains and sizing fit; existing ABI/samplers unchanged | Six WindowLife PS cases; day/night/view-angle test pending |
| pipeline/WindowLife/Module.ini | Shader update identity | Selective cache export; future version diff automation |
| engine/Modules/WindowLife.cpp | Calendar/climate transition plus sizing tooltip | Release build; future dedicated day/night control and unusual climate tests |
| engine/Modules/ImageReconstruction.cpp | Advanced FG selection now arms low-refresh override | Release build; one-restart live FG test pending |
| engine/Menu/PIXLRendererPage.cpp | Public FG selection and compact shared layout | Release build; three-panel navigation/resize validation pending |
| engine/Menu.cpp | Advanced canvas size persistence/constraints | Release build; DPI/multimonitor visual testing pending |
| engine/ShaderCache.cpp | Product version provenance, ABI/module compatibility and marker-file safety | Release build, dependency tests and exporter checks; persistent include manifests deferred |
| tools/ExportPixlPolishCache.ps1 | Scoped read-only-source cache export, version checks and copied hashes | 3131 reusable / 360 omitted; future generic ABI-aware planner |
| tools/DeployPixlRcFollowup.ps1 | Adds MaterialLayers module identity to explicit deployment allowlist; retains hash verification, backups and untouched cache | Manifest-verified deployment; future generic manifest-diff deployment |
| tools/StagePixlRendererStandalone.ps1 | Records dirty source state honestly in manifest | Package manifest/readback checks; reproducible clean commit release pending |

Prior atmosphere CPU changes and owner defaults reports remain in the worktree and are preserved. This is not an exhaustive whole-renderer audit. All visual changes are controlled improvements (B), except configuration copying and straightforward FG toggle/tooltip fixes (A). No RDR2 parity, zero-regression or measured performance claim.
