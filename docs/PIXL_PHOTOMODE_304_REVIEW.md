# PhotoMode 3.0.4 reference review for PIXL Director

Date: 2026-09-25. Reference: `_REFERENCE/PhotoMode-3.0.4`. The folder named `PhotoMode-3.0.4 source` in the request is present under this shorter name. The reference is GPL-3.0; PIXL's guide implementation is native code written for the existing Director workspace, not a source port.

## Architecture compared

The reference owns a standalone ImGui photomode UI with icon tabs for Camera, Time, Character, Filters, and Overlays (`src/PhotoMode/Manager.cpp`). Camera includes FOV, view roll, movement speed, camera grids, and named persistent camera positions (`Tabs/Camera.cpp`, `CameraPositions.cpp`). Time snapshots freeze state, timescale, hour, and weather (`Tabs/Time.cpp`). Character provides pose, expression, visibility, and actor transforms (`Tabs/Character.cpp`). Filters use Skyrim image-space overrides (`Tabs/Filters.cpp`); Overlays load artist PNGs and composite them on screen (`Tabs/Overlays.cpp`). The screenshot/gallery subsystem manages DDS/PNG captures, loading-screen selection, and painting filters (`Screenshots/Manager.cpp`, `Gallery/Manager.cpp`). Its theme is a compact floating tab card with an icon row and contextual footer.

PIXL's `TuningWorkspaceRenderer.cpp`, `PixelCapture.cpp`, and `CameraSuite.cpp` instead share an explicit Director camera lease with Photo/Video, have a PIXL quick panel and photographic footer, transient time/weather and lens/look controls, a clean capture transaction, multi-frame Photo Finish, HDR/SDR output, neural-aware reconstruction, capture progress, and state restoration. PIXL's UI must keep its own theme. Importing the reference tab window or screenshot hook would create duplicate ownership of input, game time, image-space data, and capture resources.

| Reference idea | PIXL status | Decision |
|---|---|---|
| Composition grids | PIXL had a focus reticle only | Added PIXL-themed Rule of Thirds, Golden Sections, Diagonals, and Centre/Horizon guides; hidden during capture |
| Compact icon tabs and bottom hints | PIXL has themed Director workspace and bottom effect/status rail | Keep PIXL shell; consider smaller contextual sections after live UI feedback |
| Named camera positions | Video path has POIs, Photo has no independent named bookmarks | Valuable future feature; use Director-owned pose and FOV only, game-thread camera apply, sanitized JSON in PIXL data hierarchy; never call reference `ToggleFreeCameraMode` blindly |
| View roll | Video path has roll/banking; Photo has no dedicated roll control | Defer until Skyrim free-camera roll and SmoothCam ownership can be restored reliably |
| Time and weather | PIXL already snapshots and restores changes | Keep PIXL's ownership model |
| Image-space filter overrides | PIXL owns CameraSuite look, LUT and exposure | Do not add a second Skyrim overrideBaseData owner |
| Character posing/expressions | No PIXL-owned actor transaction | Separate feature project; must snapshot per actor, handle death/unload and restore on game thread |
| PNG overlays | PIXL has capture watermark, not arbitrary overlays | Future optional artwork system; must distinguish preview-only vs baked capture and load off the render hot path |
| DDS gallery/loading-screen integration | PIXL capture already has its own output pipeline | Keep separate; loading-screen mutation is outside Photo Finish's current contract |

## Implemented here

`DirectorPhotoModeState::compositionGuide` is a small presentation-only session preference. The Photo workspace exposes one PIXL-themed combo and a tooltip. The HUD draws low-opacity cyan guides before its reticle and panels. The existing early returns for capture delay, immutable Photo Finish sampling, hidden HUD, and menu ownership prevent guide primitives from entering saved output. No camera, world, renderer settings, serialization key, shader, resource, or capture path was changed.

Follow-up: Video Mode now has a separate `compositionGuide` choice and a cyan gate-and-diamond viewfinder with an explicit VIDEO MODE / FRAMING or PLAYING label. Its route and cinema-bar overlays remain independent of Photo. The public Post-processing page now uses IMAGE, LOOKS, and UPSCALING in-page navigation; existing controls and application paths remain in their original functions. The Quality preview card was shortened to reduce page scrolling without shrinking control targets.

## Validation and remaining live tests

The Release DLL and integrated pipeline audit passed after these changes. In-game, switch every guide in both modes at 16:9 and ultrawide sizes, hide/show the HUD, enter/exit Photo and Video repeatedly, and capture in Photo to confirm the image contains no lines. Check FOV and camera position are unchanged when switching guides. Inspect all three Post-processing tabs and Quality at 1080p/1440p and a non-100% UI scale for text clipping and scrolling. Existing Photo Finish, weather/time, and Video Mode playback tests remain required.

The highest-value next increment is Photo camera bookmarks. Before implementation, trace camera writes, SmoothCam optional ownership, the existing Video POI pose type, and game-thread restore. Do not reuse the reference's direct camera-mode toggle or actor/player teleport behavior.
