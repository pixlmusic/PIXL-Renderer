# PIXL Director Video Integration Map

## Audit scope

This map records the Director/Photo Mode ownership model before adding Video Mode. It covers the active PIXL source, the current release build, camera and input hooks, PixelCapture, CameraSuite, ImageReconstruction, and the bundled SmoothCam public API header.

**Baseline build:** `cmake --build --preset PIXL-12C` completed successfully on 2026-09-24. The build emitted its normal module audit and produced `build/PIXL-12C/Release/PIXLRenderer.dll`; no new warnings or errors were recorded.

### Pegasus reference availability

The full supplied reference set is now reviewed at `_REFERENCE/POI CAMERA VIDEO REFERENCE`, including every runtime and editor C# file. It is used as a behavioural reference only; no Unity code is copied into PIXL.

Useful concepts adopted for the native design are POI capture, ordered insertion/reordering, independent path and look-at targets, per-point speed and easing, loop/single-shot routes, path scrubbing, route statistics, and bounded automatic banking. PIXL replaces Pegasus' fixed samples-per-metre traversal with an adaptive arc-length LUT and uses quaternion interpolation rather than Euler interpolation.

The following reference-only systems are intentionally excluded: Unity `GameObject`/`MonoBehaviour` ownership, Terrain and Physics APIs, Unity Timeline/Playable graph code, scene gizmo objects, animation triggers, Helios fades, runtime scene mutation, and Pegasus' frame-rate forcing. Skyrim and PIXL own those responsibilities through native camera state, SKSE tasks, CameraSuite, PixelCapture and the existing overlay.

## Existing architecture

| Area | Active owner | Responsibility |
|---|---|---|
| Director session/UI | `engine/Menu/TuningWorkspaceRenderer.cpp` | Owns `DirectorPhotoModeState`, mode entry, native camera lease, HUD, world/FOV/weather snapshots, Director overlays and Photo Finish locking. |
| Native camera | `RE::PlayerCamera` / `RE::FreeCameraState` | PIXL asks Skyrim to enter/leave native free-camera mode through `ToggleFreeCameraMode(true/false)`. It does not patch camera transforms directly while inactive. |
| Input interception | `engine/Hooks.cpp`, `TuningWorkspaceRenderer` and `Menu` | Game input is filtered before vanilla receives it while a Director capture or free-camera navigation session owns it. Tuner inspection uses a separate Shift-held movement state. |
| Photo output | `engine/Modules/PixelCapture.*` | Processes capture requests after CameraSuite’s present processing. Photo Finish owns a bounded temporal staging transaction and sends encoding/reconstruction to its worker. |
| Presentation | `engine/Modules/CameraSuite.*`, `engine/Hooks.cpp` | CameraSuite runs in the swap-chain Present chain before PixelCapture reads the final source. HDR clean composite handling stays in CameraSuite. |
| Reconstruction | `engine/Modules/ImageReconstruction.*` | Provides the projection-jitter and optional neural output path. Photo Finish waits for the requested fresh jitter/neural output instead of copying an earlier frame. |
| SmoothCam | `engine/Menu/SmoothCamAPI.h`, `TuningWorkspaceRenderer.cpp` | Optional named SKSE API. Existing code registers during `kPostLoad`, requests during `kPostPostLoad`, and currently consumes V1 camera/crosshair ownership only. |
| Runtime lifecycle | `engine/XSEPlugin.cpp` | Starts optional SmoothCam negotiation from SKSE messages, installs rendering/input hooks at post-post-load, and refreshes renderer state on save/new-game events. |

## Current Director lifecycle

### Entry

1. `OpenDirectorPhotoMode`, the Home hotkey, or Tuner inspection calls `EnterDirectorPhotoMode`.
2. `EvaluateDirectorPhotoModeEligibility` rejects loading/main/menu/dialogue/death/unready camera states.
3. PIXL refuses to hijack an existing free-camera state it did not create.
4. Photo Mode snapshots time, weather, FOV, CameraSuite settings, player alpha, HUD visibility, and transient Director controls.
5. Entry is queued through `SKSE::TaskInterface`; the game-thread task obtains the current `PlayerCamera`.
6. When SmoothCam is installed, the existing V1 API must grant camera control before PIXL calls `ToggleFreeCameraMode(true)`.
7. The result is atomically published and consumed by `UpdateTunerInspection`.

### Runtime

- `RenderDirectorPhotoModeOverlayInternal` is driven from `OverlayRenderer` on the UI/render side. It verifies eligibility and native free-camera ownership every frame.
- The active session enforces a bounded free-camera radius and keeps player alpha stable for portrait work.
- The menu and Director HUD are separate. Tuner inspection may keep its panel open while free-camera movement is temporarily active.
- The `InputEvent` hook consumes capture-locked input and forwards the filtered free-camera event stream only when Director owns navigation.
- Photo Finish locks the camera pose, hides the game HUD for clean temporal samples, invokes `PixelCapture` after Present, then releases the lock once encoding/reconstruction has completed.

### Exit and recovery

1. `ExitDirectorPhotoMode` only raises an atomic exit request.
2. `UpdateTunerInspection` invokes `ProcessDirectorPhotoModeExit`.
3. A game-thread task clears free-camera input, exits native free-camera mode, restores world time/weather/FOV/player alpha, and releases SmoothCam camera/crosshair leases.
4. The UI/render side restores CameraSuite state and HUD visibility, clears flags and logs completion.
5. Invalid camera state, loading/menu transitions, death, or lost native free-camera ownership request the same teardown path rather than attempting reacquisition.

This delayed, two-thread teardown is the safety model Video Mode must reuse.

## Thread and render boundaries

| Boundary | Existing sequence | Director Video rule |
|---|---|---|
| Game/SKSE task thread | Native camera acquire/release, `FreeCameraState` manipulation, world state restore | Apply or restore a video path pose only after the same ownership/eligibility gate. No render-thread camera writes. |
| Input hook | Input is filtered before vanilla controls receive it | Playback/scrubbing must consume only explicitly owned controls. Normal gameplay gets the untouched event stream while Video is inactive. |
| Render/UI | Director overlay, settings UI, HUD rendering | Path authoring UI may update a small game-thread-safe pending pose, but must not perform file I/O or capture encoding. |
| Swap-chain Present | CameraSuite present work, then `PixelCapture::ProcessCaptureRequest` | Recording must remain separate from Present until a bounded capture design exists. Preview has no encoding work here. |
| PixelCapture worker | Photo Finish resolves/encodes staged stills off the hot path | Video paths own camera evaluation only; they do not add an unbounded frame queue or modify Photo Finish. |
| Image reconstruction jitter | `Main_UpdateJitter` arms deterministic Photo Finish samples | Video preview follows normal temporal rendering. A later recording mode must use a dedicated deterministic timeline rather than repurposing still capture jitter. |

## Existing hook locations

- `engine/Hooks.cpp`
  - `IDXGISwapChain::Present`: CameraSuite present handling then PixelCapture processing.
  - input event hook: Director/Tuner input ownership and DLSS frame-generation input protection.
  - `Main_Update`: normal PIXL per-frame lifecycle.
- `engine/XSEPlugin.cpp`
  - SKSE `kPostLoad` / `kPostPostLoad`: optional SmoothCam API registration/request.
  - `kDataLoaded`, `kPreLoadGame`, `kPostLoadGame`, `kNewGame`: renderer and cache lifecycle.
- `engine/Menu/OverlayRenderer.cpp`
  - calls `UpdateTunerInspection` before `RenderDirectorPhotoModeOverlay`.

## SmoothCam integration status

The API header provides V1, V2 and V3. The shipped Director currently asks only for V1 and stores `IVSmoothCam1*`.

- Existing safe behaviour: no static link; refusal to acquire cancels Photo Mode; camera/crosshair leases are released on native teardown.
- Required Video adaptation: put V1/V2/V3 negotiation behind a `SmoothCamIntegration` wrapper, request V3 then degrade gracefully, verify SmoothCam’s declared thread before calls, request V2 background interpolator updates when supported, and optionally call `SendToGoalPosition` before release. This wrapper must be shared by Photo and Video only after Photo parity is proven.

## Safest Video Mode insertion point

Introduce a mode field inside the existing Director state machine, with separate Photo and Video transient state. Reuse the proven native-camera session, eligibility, queued entry, and exit path first.

Video-specific path state must live in a new native subsystem, not in `PixelCapture`:

- `engine/Director/DirectorCameraPath.h/.cpp`: serializable POIs, spline evaluation, arc-length LUT, look-at, easing, roll/bank and validation.
- `engine/Director/DirectorVideoState.h/.cpp` or local `TuningWorkspaceRenderer` state initially: playback state, selected point, timeline and pending game-thread pose.
- `engine/Menu/SmoothCamIntegration.h/.cpp`: optional capability wrapper once existing Photo logic is covered by tests.

No HLSL or DX11 resource change is necessary for preview playback. World path debug geometry should use an existing non-capture debug overlay facility when one is identified; it must be excluded from Photo Finish and any future video output.

## Expected changed files

| File | Planned responsibility |
|---|---|
| `engine/Menu/TuningWorkspaceRenderer.h/.cpp` | Add explicit PHOTO/VIDEO mode selection, route existing session lifecycle safely, and render PIXL-styled Video controls. |
| `engine/Hooks.cpp` | Extend only the existing Director input filter for playback/scrub controls. |
| `engine/Menu/SmoothCamIntegration.h/.cpp` | Optional API negotiation/lease wrapper; no permanent SmoothCam link. |
| `engine/Director/DirectorCameraPath.h/.cpp` | Native, deterministic POI/spline/path serialization and maths. |
| `engine/Modules/PixelCapture.*` | No ownership change in early phases. Only a future explicit recording integration may add bounded capture support. |
| `engine/Menu/OverlayRenderer.cpp` | Render Video overlay through the existing Director call path. |
| distribution defaults/presets | Add only backward-compatible Video preferences after the runtime path exists. |

## Incompatibilities and guardrails

- Do not enter if Skyrim is loading, paused by another menu, in dialogue, at the main menu, the player is dead, or the native free camera is already owned by another feature.
- SmoothCam installed without a cooperative API remains a refusal, not a camera fight.
- Direct `FreeCameraState` writes must be game-thread-only and occur only after native camera ownership is established.
- Fast travel, load, cell/worldspace transitions and loss of free-camera state must stop playback and execute the existing restoration route.
- Photo Finish stays an immutable still-capture transaction. Video preview cannot arm, cancel or alter it.
- CameraSuite settings, HDR state, image reconstruction and jitter remain owned by their current modules.
- Video recording is intentionally deferred until a bounded, deterministic output path is designed; preview does not enqueue video frames.

## Baseline risks to address before generalising

1. Existing SmoothCam integration is V1-only even though the header supports V2/V3.
2. `DirectorPhotoModeState` currently combines session ownership, photo presentation and capture state. Video needs a sibling state before shared helpers are extracted.
3. Current path-like code in `WorldBenchmark` is fixed benchmark data and must not be promoted into user-facing Director logic.
4. The current production SmoothCam path is V1-only; V2/V3 negotiation remains a contained follow-up behind a PIXL wrapper.
