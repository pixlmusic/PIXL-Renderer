# Atmosphere, CameraSuite and Stormglass reference review (2026-09-25)

## Source and build boundary

The active module sources are `engine/Modules/{Atmosphere,CameraSuite,RainResponse}.*` and their `pipeline/*/Kernels` trees. The standalone staging script copies tracked pipeline kernels into the runtime `Shaders` tree. CameraSuite compiles its Physical Camera compute shaders from `Data/Shaders/CameraSuite` at runtime. Atmosphere also participates in Lighting, Water, Sky, Grass and other cached shader permutations through `Atmosphere.hlsli`. The live game contained the same older Atmosphere and CameraSuite shader copies as the pre-review source. File timestamps alone were not used to select an implementation.

## Comparison and selection

| Reference | Difference from active source | Decision |
| --- | --- | --- |
| `_REFERENCE/Atmosphere` (7 shaders, 24 Sep) | Finite/depth guards in shared fog math, froxel integration, temporal history and conservative depth. No new lighting model or exposure algorithm. | Retain the active version for this build. The shared include reaches many raster permutations, so replacing it during an active cache build would impose a broad recompilation and add checks to hot pixel paths without evidence of invalid fog inputs. Revisit with targeted fog captures and profiling. |
| `_REFERENCE/CameraSuite` (3 shaders, 24 Sep) | Defensive exposure, histogram and local-exposure input validation; same scene-key and adaptation formulas. | Adopted with DX11 FXC-compatible bit tests for non-finite floats. Also exclude effectively black samples from the histogram so black borders/occlusion do not bias the scene meter toward its maximum. |
| `_REFERENCE/PIXL_Stormglass_RainResponse_CameraSuite_SAFE_UPDATE` (6 files, 24 Sep) | `CameraSuite.h` identical; other files predominantly add finite checks at many CPU and per-pixel sites. No demonstrated improvement to the normal rain/glass image. | Kept active Stormglass/RainResponse shader and CPU implementations to avoid unmeasured per-pixel cost and unrelated behavior changes. Adopted finite validation for CameraSuite exposure settings and external exposure input only. |

## Exposure path and finding

Skyrim's ISHDR output is selected by CameraSuite at presentation. CameraSuite dispatches a 256-bin log-luminance histogram, computes a 1x1 adapted exposure texture, optionally computes a 4x-downsampled local exposure texture, then `HDROutputCS.hlsl` applies both before the physical-camera response and final SDR/HDR output. The C++ `HDRDataCB` matches `PhysicalCameraCommon.hlsli`; this change adds no cbuffer fields or binding slots.

The old histogram placed zero-luminance pixels into its minimum bin. This can push the meter toward excessive brightening when a frame has substantial fully black coverage. This is a **code-path finding**, not a confirmed diagnosis of the reported live scene. The new histogram ignores measurements at or below `1e-5` linear luminance; if the whole frame has no valid samples, the exposure shader retains the previous exposure. Existing 2%-98% metering, exposure limits, adaptation speeds, compensation and highlight response remain intact. CameraSuite config and external exposure EV now reject NaN/INF before C++ clamping. The shader reference's `isfinite()` did not pass strict FXC compilation; the selected source uses exponent-bit tests instead.

## Video cell boundary

Video's working route is bound to the player's current cell FormID. A changed or unavailable cell stops preview and clears working POIs, spline samples, timeline markers, play time and point selection. Saved JSON routes remain on disk. The Director camera boundary is reset so a new route can be captured in the new cell. This runs during Video session maintenance and when Video is opened again after leaving a cell. Photo Mode keeps its existing lifecycle.

## Validation and live tests

- Strict FXC (`/WX /Ges /O3`) compiled the three changed Physical Camera compute shaders against the runtime include tree.
- Release C++ build passed (`cmake --build --preset PIXL-12C`); integrated pipeline audit passed with 37 shipping modules.
- `tools/TestPixlExposure.ps1` passed its 30/60/144 FPS adaptation checks and six strict FXC variants against the newly staged shader tree. FXC emitted its pre-existing internal optimizer warning for the two ISHDR BLEND variants; both compiled successfully under the test's established warning policy.
- Live-test package manifest verified (340 files). The deploy script copied the four CameraSuite shader files and the new DLL to the game, preserving user configuration and the existing permutation cache. Hash verification reported zero mismatches. Rollback manifest: `build/deployment-backups/Release-20260925-123940-ebdc67c7710e43ce89de7d56eb4d9bd7/deployment.csv`.
- Live Skyrim visual testing remains required. Compare a dark interior with a large black region, a bright exterior, and a fast doorway transition in SDR and HDR; verify exposure no longer rises from black coverage, no abrupt jumps, and no clipping of legitimately dim interiors.
- In Video Mode, capture 2-3 POIs, transition to another cell, verify the timeline and spline are empty, then capture new POIs. Repeat after a load screen and after reopening Video. Verify saved JSON paths are still available.
