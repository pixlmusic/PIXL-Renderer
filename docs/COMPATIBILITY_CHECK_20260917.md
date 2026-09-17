# PIXL Renderer 1.0.2 compatibility check

Checked against the active Steam runtime log from 2026-09-17 and the current
source/build. This is a release-readiness checklist, not a claim that a static
source review replaces movement/weather/camera testing in-game.

| Area | Status | Evidence / remaining test |
|---|---|---|
| Custom/modded weather detection and adaptation | Implemented; runtime pass still required | WeatherManager and shader weather inputs are active. Test a weather transition and a weather mod override after restart. |
| Roof runoff visibility/behaviour | Implemented and compiling | Steam log confirms all four RainResponse runoff shaders compiled successfully. Confirm visibility during sustained rain at a roof edge. |
| Bright/square rain micro-splash artefacts | Implemented mitigation | RainResponse and micro-splash guards are present. Confirm at low preset and with temporal upscaling enabled. |
| Atmosphere/fog obscuring skyboxes/clouds | Implemented mitigation | Atmosphere has bounded transmittance/map handling. Confirm at dawn, storm, and clear-sky transitions. |
| Overly bright night exposure | Implemented mitigation | Camera/exposure controls are active in the loaded Steam log. Confirm indoors and outdoors at night. |
| Excessive gloss/wetness | Implemented mitigation | Wetness preserves authored roughness floors and the GGX path now applies geometric specular AA to legacy materials too. Confirm Skyland/PBR stone and wood in rain. |
| Photo Mode vs external camera mods | Compatibility code present | CameraSuite is loaded. SmoothCam interaction still requires a live camera-mode test; no static log can certify its crosshair/arc draw path. |
| Contact/screen-space shadow popping | Implemented mitigation | Contact Shadows loaded and bounded; temporal behaviour requires camera-pan and cell-transition testing. |
| Horizon Fix glowing horizon | Compatibility is conditional | Current Steam log says HorizonFix was not detected, so compatibility was disabled for that run. Verify the actual Steam plugin is deployed and re-check the log. |
| WindowLife detection consistency | Implemented with authored masks/fallback | Steam log confirms 12 pane masks and layered resources loaded; test custom window texture packs and broken-roof meshes in Whiterun/Solitude. |
| GGX authored-material square highlights | Correctness fix included; visual confirmation pending | Authored/legacy normal variance is now filtered before GGX. The updated shader source must be allowed to compile in the Steam profile before comparison. |
| SurfaceTides + PIXL water | Updated bridge ready for retest | Previous log showed tessellation but the old domain output ABI did not match PIXL WATERBODY pixel input. Updated build matches PIXL's conditional fog, flow, and position semantics. |

## Validation

- PIXL Release build: passed integrated PIXL audit.
- SurfaceTides Windows Release build: passed.
- SurfaceTides tests: 4/4 passed, including Shader Model 5 validation.
- Review evidence was captured before the 1.0.2 release commit.

The SurfaceTides/PIXL bridge remains explicitly opt-in through
`[Compatibility] AllowPIXL=1`.
