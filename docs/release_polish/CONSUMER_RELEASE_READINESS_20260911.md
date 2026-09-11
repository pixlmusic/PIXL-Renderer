# Consumer release readiness — 2026-09-11

## Implemented in this pass

- Fresh installs now show the first-use setup card. The previously shipped default had `FirstTimeSetupCompleted = true`, which bypassed onboarding.
- The setup card now applies the existing coordinated quality profiles and offers native TAA or FSR 3.1 Quality. Enhanced + TAA remains the universal default; FSR is an explicit opt-in because it depends on the bundled FidelityFX runtime and a restart-safe display path.
- Added a persistent `QUICK SETUP` button to the public PIXL Renderer control centre.
- Added plain-language Photo Mode guidance: PIXL Director is on Insert and does not alter the normal gameplay profile.
- The release README now explains which sidecar changes require a Skyrim relaunch through Vortex or the user's launcher.
- A `RELEASE` package now refuses to stage without a validated preloaded PipelineLibrary. Compile-on-device output remains available under `RELEASE-CANDIDATE`.
- Validated preloaded `.pixlbin` files are timestamped after the shader tree is staged. This prevents a valid cache from being treated as older than its matching copied HLSL source by the runtime's fast mtime check. ABI, plugin-version, module, and family validation remain active.
- Added the project GitHub URL to the consumer credits document.

## Validation

- `cmake --build --preset PIXL-12C --config Release --parallel 8` — passed.
- Integrated PIXL audit — passed; 37 shipping modules and 1 retired source-only module reported.
- `tools/TestPixlPackageManifest.ps1` — all 14 manifest cases passed.
- `SettingsDefault.json` parse — passed.
- Full in-game/Vortex validation is still required for the final release candidate, including first-run interaction, FSR runtime selection, relaunch messaging, and cache-hit behavior after archive extraction.

## Release decision

Source is consumer-facing for a release candidate. A final public `RELEASE` archive must be generated only after the current plugin DLL is paired with a freshly validated PipelineLibrary containing the current shader ABI stamp `PIXL.SharedBuffers.20260902.1` and at least 3000 stage binaries. No such complete library was present in the repository/build output during this pass, so none is claimed or fabricated here.
