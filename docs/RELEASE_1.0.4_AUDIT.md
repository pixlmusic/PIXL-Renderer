# PIXL Renderer 1.0.4 release audit

## 2026-09-25 live-test follow-up

Director Video input, POI navigation, point-speed timing and working-spline deletion were refined after user testing. WindowLife now passes dry-glass warp to the exterior recessed room and recognizes a window-specific glow guide on architectural diffuse materials. The Camera/Post-processing page was reordered around a wider upper-right preview, and the Tuner footer was shortened. The exact window scene, playback stability and GUI layout still require in-game confirmation. See `release_polish/DIRECTOR_WINDOWLIFE_GUI_FOLLOWUP_20260925.md` for the focused trace and test cases. Shader revision `PIXL.Shaders.20260925.2` requires another cache build. This follow-up does not close the exhaustive source review gate.

## Scope and evidence

The canonical source is this repository. The baseline `PIXL-12C` Release build passed before 1.0.4 edits. The source tree contained substantial intentional uncommitted renderer, GUI, Director, material, and WindowLife work; none was reset. `docs/release_polish/ACTIVE_BUILD_INVENTORY.md` enumerates 735 PIXL-owned build/runtime candidates, including one retired module. The accompanying matrix records an automated text/pattern scan. It does **not** establish the requested per-file semantic review: callers, ownership, resource bindings, and runtime behavior remain unverified across the complete file set. This is an open release gate, separate from in-game visual testing.

## Changes in this pass

- Set the active product, executable resource, package, and FOMOD version to 1.0.4. Custom theme metadata now uses the generated product version.
- Added a shared shader revision in pipeline-library metadata. A 1.0.3a cache with no matching revision is invalidated on first 1.0.4 launch; unchanged CPU/HLSL buffer ABI retains its own separate key. The stage tool reads all three cache keys from `ShaderCache.cpp` and refuses a stale preloaded cache.
- Closed a preloaded-package validation gap: the stage tool now checks `ShaderRevision` as well as layout and ABI against the runtime constants. A previous game cache without the revision was rejected in a packaging test.
- Added a SurfaceTides entry to the existing PIXL Extensions pillar. It checks the loaded DLL without loading it or inventing an ABI. The UI reports **Detected** until SurfaceTides confirms the V1 water-draw handshake in its own log.
- Retained the existing optional SurfaceTides 1.0.2 bridge and its exact-version FOMOD requirement. The local bridge DLL matches the main game's installed DLL by SHA-256.
- Built the matching modified SurfaceTides 1.0.2 source companion beside the FOMOD to keep the optional bridge's source and notices available.
- Removed one machine-specific path default from a development-only Discord maintenance script.
- Corrected stale WindowLife diagnostics/comments that still described the old 240-byte per-draw field; the current CPU/HLSL payload is 256 bytes.
- Stopped Video path playback when an active Director session switches to Photo Mode. The authored path and scrub position remain available when returning to Video; live camera restoration still requires game testing.
- Reused the existing release staging, manifest verification, deployment, source-export, and FOMOD tools.

## Architecture decisions

PIXL's native water remains authoritative unless the optional SurfaceTides bridge positively matches a live draw. `PIXL_QueryWaterDrawV1` remains the existing render-thread contract. Ground Response and WindowLife were not redesigned in this pass. The PIXL GUI remains the only user-facing framework.

## Risks and required manual tests

- A loaded SurfaceTides DLL does not prove its simulation, geometry hook, or V1 handshake is active. Inspect `SurfaceTides.log` while viewing a river or lake.
- The requested exhaustive review of every active C++ and HLSL file is incomplete. The automated matrix must not be used as evidence that a file was semantically reviewed.
- Photo/Video Mode, shader permutations, material visuals, startup, fast travel, resize, and all supported Skyrim runtimes require live validation. Compilation alone cannot clear these gates.
- The public 1.0.4 candidate uses compile-on-device shader cache mode. First launch will discard an incompatible old library and rebuild. A fully preloaded release cache must be built from this exact source and descriptor set before labeling that package `RELEASE`.

## Validation record

The 1.0.4.0 Release DLL and integrated audit passed. Strict FXC coverage passed 733 selected cases. Core, Source and FOMOD archives passed. The Core was deployed to both `H:\The Elder Scrolls - Skyrim - Special Edition\Data` and `H:\SteamLibrary\steamapps\common\Skyrim Special Edition\Data`; all 340 manifest payloads match by SHA-256 in each location. Separate rollback backups are under `build/deployment-backups`. The main game's installed SurfaceTides DLL matches the local 1.0.2 source build; the optional bridge was not newly installed into Steam. See `RELEASE_CHECKLIST_1.0.4.md` for live tests still required. Test output is retained under ignored `build/` paths.

## Exact live-game validation sequence

1. Launch the intended game through its matching SKSE, finish the first 1.0.4 shader compilation, and confirm `PIXLRenderer.log` reports version 1.0.4 with no compile failures. Repeat startup; the second load should reuse the new revision cache. Do this separately for the main and Steam installations.
2. Open PIXL at 1080p and 1440p (plus 4K or non-default UI scale if available). Visit each category, change and restore a representative slider/toggle, save and reload a look, inspect tooltips and text clipping, then close the tuner and confirm Skyrim input is normal.
3. In the main installation, view one river and one lake with Waterbody on/off. Check the SurfaceTides panel shows `DETECTED` and inspect `SurfaceTides.log` for a successful `PIXL water draw handshake V1` with failed draws at zero. Compare shoreline, refraction, underwater, city and interior water. The Steam Core deployment alone does not install the optional SurfaceTides bridge.
4. Enter Photo Mode from first and third person. Move the camera, change FOV/time/weather/exposure, hide UI, capture, then exit. Confirm HUD, player alpha, time, weather, FOV, input and camera ownership return to their starting state. Repeat with SmoothCam present/enabled and absent if both setups are available.
5. Enter Video Mode, capture four POIs from Shift/freecam positions, visualize the route, delete and reorder a POI, scrub, play a single shot and a loop, pause and exit during playback. Confirm no stuck freecam or world freeze. Test a load screen or fast travel while Video Mode is active on a disposable save.
6. Revisit the reported wood/cobblestone white-and-dark pin scenes. Compare Legacy GGX Highlight Scale at its normal value and zero, then DLSS Quality, DLAA and native if available. Record screenshots or a short moving clip; compilation does not certify that artifact resolved.
7. View a replacement WindowLife window from close and far exterior positions, inspect glass refraction against the interior image and edge shape, and compare softness at its minimum/maximum. Check rain and night lighting.
8. Walk through snow and mud, NPC tracks and grass collision. Test weather accumulation, a Seasons change if installed, a bridge/rock contact, and prolonged walking across clipmap wrap. Confirm Ground Response remains separate from grass collision.
9. Repeat an interior/exterior transition, fast travel, save/load, a crowded NPC area, dialogue, first/third person, shader reload, resolution/fullscreen change and module enable/disable. Watch for stale temporal histories, missing resources and UI ownership issues.
10. If a D3D11 debug-layer setup is available, inspect warnings while toggling SurfaceTides/Waterbody, Ground Response and WindowLife. Capture PIXL profiler timings before/after 1.0.4 only in the same scene and settings; no timing delta is claimed without this measurement.
