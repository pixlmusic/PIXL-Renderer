# GUI polish performance notes

## Changes made

- The public Quality page now aligns its setup and tuner actions to the first navigation column and uses the interface body font for both PIXL wordmarks.
- The Quality preview places its description beside the profile name, uses a centered full-image fit at the source aspect ratio, and reaches the end of the adjacent quality controls. Duplicate page headings were removed.
- The custom quality action is labelled `RESTORE PROFILE` with a tooltip explaining that it reapplies the selected profile to custom adjustments.
- Post-processing and Upscaling now occupy separate nested page tabs. Post-processing has Camera and Looks views; switching either level resets the page scroll to its primary controls.
- Bloom sits directly under Exposure; the water lens follows Occlusion & Reflections, with Depth of Field below it. The live preview places comparison toggles in its header, displays an uncropped image, and collapses to a compact status when no game scene exists.
- Director quick effects render only the visible focus window (6–12 rows depending on available height), rather than constructing every readout in a tall overlay.
- The shared Video timeline draws with the current ImGui draw list and keeps point timing in the path-rebuild cache.
- Director and tuner footers use fixed-size state arrays and a single Camera Suite settings copy.
- The preset translator scans only on explicit refresh; it has no startup or frame-loop filesystem work.

## Limits

No frame-time claim is included here. These are reduced UI-side allocations, locks and draw-list primitives based on the code path. GPU cost still needs in-game capture with PIXL's existing profiler.

The layout edits are UI-only. Both preview images now use full UVs with aspect-preserving fit, with no extra texture or render pass. The camera control column uses 40% of the two-column workspace and the preview/effects column uses 60%. An unavailable menu preview now says it is available in game rather than appearing stuck initializing.

The final GUI Release build and integrated 37-module audit passed on 2026-09-25. `PIXL-Renderer-1.0.4-GUI-Preview-TEST-20260925-Core.zip` passed manifest and 7-Zip integrity checks; its DLL and the Steam installation DLL match the build hash. The Steam deployment preserved user settings and shader cache. In-game layout still needs checking at 720p, 1080p, 1440p and a non-default UI scale. No GPU or draw-count measurement was made.
