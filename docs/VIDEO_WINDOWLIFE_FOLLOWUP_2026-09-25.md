# Video editor and WindowLife follow-up — 2026-09-25

## Baseline protection

The accepted 1.0.4 UI/renderer baseline is preserved at commit `f42345ff` on `backup/1.0.4-ui-baseline-20260925`. The pre-existing local FidelityFX DX11 build patch was excluded from that commit.

## Video Mode

The route editor, timeline, point inspector, point deletion and per-point speed controls already existed in the `PixelCapture` page. The first attempt to expose them opened the whole tuner on Video entry. That suppressed the Video viewfinder, bars and full-width timeline, and closing the tuner requested Director exit. The Steam runtime log showed Video entry followed immediately by a Director exit request, with no captured POIs. Video now opens its own PIXL panel over the live scene, separate from the tuner. The cinema bars, world route and full-width timeline remain visible. The panel has a clickable timeline and POI list, Capture, Play, selected-point Speed, Save Camera and Delete controls. Shift-held motion releases the mouse to native free camera; releasing Shift returns it to the panel. Shift+Enter captures the current camera position; Insert hides or restores the panel without ending the camera session. The full PixelCapture workspace remains available through Advanced Director.

The follow-up clean-view pass moves the panel nearer the upper-left corner and makes Delete hide both the timeline and panel. Skyrim's HUD is hidden for the entire Photo or Video camera session, with its incoming visibility restored after exit, including when the HUD menu is recreated. This removes the compass and detection indicator during composition. PIXL's centered Photo and Video reticles were removed; the Video panel and timeline still identify the mode. The software mouse pointer remains available only while the interactive Video panel is visible.

The short-turn path problem was traced to sparse arc-length sampling on short curved segments and full authored speed through sharp corners. Rebuild now samples a minimum of 16 arc intervals per segment and computes a bounded effective corner speed based on adjacent point spacing and bend angle. Saved authored speeds and the path JSON schema are unchanged. This is a source-level correction; perceptual motion still needs live testing.

## WindowLife

The Steam user's `UserGraphics.json` Window Life block was compared by key and value with the shipped `SettingsDefault.json` block. All 56 values are represented in the default block, and the C++ fallback settings and PIXL Golden Baseline Window Life block were aligned with those values. The user's live configuration was not edited. Other quality presets remain separate authored presets.

The exterior-authored room floor now maps lower toward the sill without adding a texture sample. Authored and analytic occupants start lower relative to that floor. Silhouette Softness now applies a visible atlas mip bias and alpha feather within its existing 0.015–0.16 range. Room Emission permits up to 6x, with a modest night-weighted lift after recessed-room shading. WindowLife's module version was advanced from `0-7-16` to `0-7-17` to rebuild only affected shader permutations.

WindowLife installs a Lighting draw hook but does not issue any `Draw` or `Dispatch` calls. It binds six pixel SRVs for each relevant Lighting draw and uploads per-geometry data only when it changes. A per-window-draw lowercase-name allocation was replaced with reusable scratch storage. The reported draw-count increase remains unattributed without a PIX/RenderDoc frame or equivalent live profiler capture; do not interpret this CPU-side optimization as a measured GPU gain.

## Validation

- Release DLL build: passed after code changes; integrated PIXL audit passed for 37 shipping modules.
- Director deterministic path self-test: passed, including a short 90-degree-turn case.
- WindowLife shader test: nine pixel shader permutations passed with warnings treated as errors.
- In-game Video controls, WindowLife appearance and draw-call attribution: **manual test required**. The earlier tuner-based Video entry was broken in the Steam build; verify the separate panel deployment before treating Video as fixed.

## Focused live checks

1. In the Steam game, open Video Mode with `Ctrl+Home`. Confirm the separate Video panel, software pointer, cinema bars and full-width timeline appear together, and stay visible. Confirm Skyrim's compass and detection indicator and PIXL's centered reticle are absent. Capture two POIs with Enter or the panel button; click a timeline tick and a POI row. The free camera should move to the chosen POI.
2. Hold Shift to reframe, capture with Shift+Enter, then release Shift, select a POI, change Speed, click Save Camera, then Delete POI. Verify the route and timeline update and the mouse controls the editor when Shift is released. Hide and reopen the panel with Insert; the timeline and route must persist.
3. Press Delete to hide all PIXL Video UI, including the editor, timeline and software mouse pointer. Press Delete again to restore it. Exit Video and Photo separately, including after a loading/menu transition, and confirm Skyrim's original HUD visibility returns.
4. Preview a route with two nearby 90-degree turns and compare with a long straight. Check for continuous motion and no mouse-driven camera drift during Play.
5. Compare the same exterior authored window at dusk and night: room floor near the sill, occupants lower, emission brighter, and Silhouette Softness visibly affecting people without softening the frame.
6. Capture before/after draw and GPU timing data in the same scene with WindowLife enabled and disabled. Check actual draw calls separately from WindowLife pixel time and CPU submission cost.

## Indoor outdoor view and cell-bound route follow-up

The indoor WindowLife path already selected the outdoor day/night atlases, but sampled them as a narrow flat backplane. It now traces a wider and deeper volume behind the existing window aperture and applies the same transmitted-glass refraction to the outdoor image. No new texture slot or constant-buffer field was added. The outdoor atlas remains one color image, so individual outdoor objects do not have independent physical depth. The exterior-to-interior room projection is unchanged. `WindowLife.ini` is now `0-7-19`, causing selective WindowLife Lighting shader invalidation; the rest of the live shader cache was left untouched.

Director open paths previously duplicated missing endpoint control points, which made the first and last segment lurch. They now use extrapolated endpoint knots. Arc-length samples now follow the same position easing as playback and retain at least 64 subdivisions on short bends. A preview frame-time cap limits visible camera jumps after shader or streaming stalls without changing authored speeds or saved route timing.

New route saves include a Skyrim cell Form ID and live under `Director/Paths/<eight-digit cell ID>/`. The Video editor lists routes from the current cell and explicitly labelled legacy unbound routes. A route bound to another cell is rejected; loading an old unbound route explicitly binds the working copy to the current cell. New saves choose a unique filename and never silently overwrite an earlier flythrough. The working route still clears on cell changes, while saved routes remain available when returning to the cell. This binds the route to the player cell used by the Director session; it does not infer a different cell from a free camera that has crossed a distant cell boundary.

Release `PIXLRenderer.dll` built successfully. The Director deterministic self-test passed, including path JSON round-trip and a first-segment departure check. Nine focused WindowLife Lighting pixel shader permutations compiled with warnings treated as errors. The DLL, WindowLife include, and module INI were deployed with SHA-256 verification and a rollback copy under `build/deployment-backups/Focused-20260925-210008`. Visual motion and authored windows still require in-game validation.

### Steam install correction

The next in-game test still showed a flat indoor window. `PIXLRenderer.log` identified the running game as `H:/SteamLibrary/steamapps/common/Skyrim Special Edition`, while the focused deployment above targeted the separate `H:/The Elder Scrolls - Skyrim - Special Edition` tree. The Steam runtime still loaded WindowLife `0-7-18`; therefore that test did not exercise the new `0-7-19` indoor projector. The Steam outdoor atlases were already present and hash-identical to the other game's atlases. WindowLife `0-7-20` adds stable dry old-glass transmission warp and applies Interior Softness to the outdoor atlas, without moving the pane mask or rendering curtains indoors. Six focused Lighting pixel shader permutations passed with warnings treated as errors; the Release DLL built. The DLL, include and module INI were then deployed to the actual Steam installation with SHA-256 verification and backups under `build/deployment-backups/Steam-WindowLife-20260925-214453`. All 22 shader/INI files compared against the release script's file list match their Steam copies; the Steam DLL also matches the latest Director build. The live shader cache and user configuration were left untouched. Re-test the pictured window after the next Steam launch; the previous screenshot cannot validate this build.
