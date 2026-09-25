# PIXL Director Video Mode — Initial Native Implementation

## What changed

- Reviewed every supplied Pegasus runtime and editor reference file in `_REFERENCE/POI CAMERA VIDEO REFERENCE`.
- Added a native `PHOTO` / `VIDEO` Director mode switch. Both modes reuse PIXL's existing guarded Skyrim free-camera lease, input filtering, eligibility checks, cleanup path, FOV controls and CameraSuite compatibility.
- Added a native `DirectorCameraPath` subsystem with backward-compatible versioned JSON, centripetal Catmull-Rom evaluation, adaptive arc-length lookup, quaternion interpolation, captured camera rotation, independent look-at targets, spline-facing pitch/yaw, per-point speed/hold/easing, route duration, loop support and future-safe roll data.
- Added a PIXL-styled Video workspace: clear, name, save, load, select, duplicate, delete and reorder points; capture/update a point from the live fly camera; move to a point; capture a target; set per-point FOV, speed, hold, framing mode and easing; set saved route duration or live preview rate; preview, pause, stop, loop and scrub paths. The route canvas visualises the spline, selected POI, enabled targets and live playhead without ever being drawn into a capture.
- Video can enter with the world running for live shots or frozen for controlled composition. Photo Mode remains frozen exactly as before.
- Added the user-assignable **Video Mode** binding to the PIXL Hotkeys page. Its default is `Ctrl+Home`; existing `Home` Photo Mode behavior is preserved. The setting is saved as `VideoModeKey` and older configuration files retain their defaults.
- Video Mode blocks Photo Finish and photo-only quick-panel controls. Opening Video never begins recording or queues capture work.
- Video Mode opens its own PIXL Director panel over the Video viewfinder and full-width timeline, with a mouse cursor. `Enter` or `Shift+Enter` captures a point, `Space` previews or pauses a valid route, `Backspace` returns it to the start, and `Insert` hides or restores the panel. The user-assignable Video Mode hotkey exits safely.

## Storage

User-authored path JSON files are written only after an explicit Save action:

`Data/SKSE/Plugins/PIXL/Director/Paths/<path-name>.json`

The path document carries the native schema/version produced by `DirectorCameraPath`. Path names are sanitized before they are used as filenames. Loading validates the schema and rejects malformed paths without touching Skyrim camera ownership.

## Safety model

- Video uses the same delayed game-thread exit path as Photo Mode.
- If Skyrim leaves free camera, enters an incompatible state, loads, fast-travels, dies, or otherwise fails Director eligibility, the existing safe teardown path runs.
- Video preview only writes `FreeCameraState` while PIXL already owns the native camera.
- Camera playback stops on invalid paths/poses and does not create a frame queue, readback, encoder task or render-path dependency.
- SmoothCam behavior remains the existing cooperative V1 path; Video does not introduce a new hard dependency or camera hook.

## Deliberately deferred

- Deterministic video recording/encoding. This needs a bounded frame pipeline and a separate fixed-timeline design; it is not folded into Photo Finish.
- SmoothCam V2/V3 negotiation wrapper.
- Optional collision correction, in-world spline gizmos, and camera-channel animation for exposure/DoF/LUT.
- Raycast-driven look-target placement. Current camera capture stores a stable world-space target ahead of the lens, avoiding a persistent reference that can unload.

## Validation performed

`cmake --build --preset PIXL-12C` completed successfully after the implementation. The build produced `build/PIXL-12C/Release/PIXLRenderer.dll` and the PIXL module audit passed.

## Live-game validation

1. Press `Ctrl+Home` in a safe exterior gameplay state. Confirm native free camera enters, the PIXL Video editor and mouse cursor appear, and no photo is taken.
2. Capture four points, holding Shift while moving the free camera and pressing Shift+Enter at each framing. Confirm each capture keeps the current position, pitch/yaw framing and lens. Hide and reopen the panel with Insert; confirm the Video crosshair, cinema bars and full-width marker timeline remain visible.
3. Check the route visualiser, click each POI, use Go To Point, then compare captured framing, independent look-target framing and Follow Route Direction. Verify spline-facing mode produces a smooth pitch/yaw fly-by.
4. Vary Segment Speed, Hold, Route Duration and Preview Rate; scrub the timeline, then preview once and loop it. Verify motion remains smooth and reaches each point.
5. Save, exit Director, reopen Video Mode, refresh the path list, load the path and preview again. Also load a prior version-1 path to confirm it remains valid.
6. Switch `VIDEO -> PHOTO -> VIDEO`; verify camera/FOV remain stable and neither transition invokes Photo Finish.
7. Enter Video Mode with SmoothCam absent and present. With another camera owner active, confirm PIXL declines the session cleanly.
8. Test escape, loading, fast travel, cell/worldspace changes and game exit during preview. Confirm free camera/input/FOV restore once and no playback continues.
