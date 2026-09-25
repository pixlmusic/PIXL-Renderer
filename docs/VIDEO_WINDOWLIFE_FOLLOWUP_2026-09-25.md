# Video editor and WindowLife follow-up — 2026-09-25

## Baseline protection

The accepted 1.0.4 UI/renderer baseline is preserved at commit `f42345ff` on `backup/1.0.4-ui-baseline-20260925`. The pre-existing local FidelityFX DX11 build patch was excluded from that commit.

## Video Mode

The route editor, timeline, point inspector, point deletion and per-point speed controls already existed in the `PixelCapture` page. `OpenDirectorVideoMode()` hid the menu on entry, and that page was restricted to Developer/Lab navigation. This made those controls and the cursor unavailable in normal Video Mode. Entry now selects the native PIXL PixelCapture workspace, opens its panel and leaves the mouse with ImGui until Shift is held for native free-camera motion. Timeline-adjacent Save Camera, Delete POI and selected-point Speed controls make the main edit actions visible without scrolling to the detailed point inspector. The existing list and plan view still move the camera to a selected point.

The short-turn path problem was traced to sparse arc-length sampling on short curved segments and full authored speed through sharp corners. Rebuild now samples a minimum of 16 arc intervals per segment and computes a bounded effective corner speed based on adjacent point spacing and bend angle. Saved authored speeds and the path JSON schema are unchanged. This is a source-level correction; perceptual motion still needs live testing.

## WindowLife

The Steam user's `UserGraphics.json` Window Life block was compared by key and value with the shipped `SettingsDefault.json` block. All 56 values are represented in the default block, and the C++ fallback settings and PIXL Golden Baseline Window Life block were aligned with those values. The user's live configuration was not edited. Other quality presets remain separate authored presets.

The exterior-authored room floor now maps lower toward the sill without adding a texture sample. Authored and analytic occupants start lower relative to that floor. Silhouette Softness now applies a visible atlas mip bias and alpha feather within its existing 0.015–0.16 range. Room Emission permits up to 6x, with a modest night-weighted lift after recessed-room shading. WindowLife's module version was advanced from `0-7-16` to `0-7-17` to rebuild only affected shader permutations.

WindowLife installs a Lighting draw hook but does not issue any `Draw` or `Dispatch` calls. It binds six pixel SRVs for each relevant Lighting draw and uploads per-geometry data only when it changes. A per-window-draw lowercase-name allocation was replaced with reusable scratch storage. The reported draw-count increase remains unattributed without a PIX/RenderDoc frame or equivalent live profiler capture; do not interpret this CPU-side optimization as a measured GPU gain.

## Validation

- Release DLL build: passed after code changes; integrated PIXL audit passed for 37 shipping modules.
- Director deterministic path self-test: passed, including a short 90-degree-turn case.
- WindowLife shader test: nine pixel shader permutations passed with warnings treated as errors.
- In-game Video controls, WindowLife appearance and draw-call attribution: **manual test required**.

## Focused live checks

1. In the Steam game, open Video Mode with `Ctrl+Home`. Confirm the cursor and Director editor appear; click a timeline tick and a plan-view POI. The free camera should move to the chosen POI.
2. Hold Shift to reframe, release it, select a POI, change Speed, click Save Camera, then Delete POI. Verify the route and timeline update and the mouse controls the editor when Shift is released.
3. Preview a route with two nearby 90-degree turns and compare with a long straight. Check for continuous motion and no mouse-driven camera drift during Play.
4. Compare the same exterior authored window at dusk and night: room floor near the sill, occupants lower, emission brighter, and Silhouette Softness visibly affecting people without softening the frame.
5. Capture before/after draw and GPU timing data in the same scene with WindowLife enabled and disabled. Check actual draw calls separately from WindowLife pixel time and CPU submission cost.
