# PIXL Director 1.0.4 capture finish

## Trace and decisions

- Photo and Video share the guarded Director free-camera session. Video owns its compact editor and timeline; the full tuner is a temporary advanced view. SmoothCam cooperation, the game HUD lease, player alpha, FOV and game time still use the existing Director entry/exit path.
- Capturing, saving or deleting a POI calls `RebuildDirectorVideoPath`, which rebuilds spline samples, point times and preview geometry. Playback uses the cached path and allocates no route data per frame.
- The path-facing quaternion builder used a reflected camera basis. It is now right-handed; roll is applied around the world forward axis so it does not change the native pitch/yaw sent to Skyrim. Native yaw is unwrapped at the `atan2` seam.
- Short route corners now receive lower effective speed. POI pairs that require a large angular turn also receive enough travel time, without rewriting authored POI speeds or path JSON. Long gentle shots retain their pacing.
- Degenerate spline tangents use the nearest authored span rather than abruptly pointing toward world +Y.
- Photo opens its quick effects panel on entry. Photo and Video show distinct restrained center reticles, and their existing Delete clean-view state hides the reticle with the rest of the PIXL overlay. Shift guidance is highlighted in the Photo quick panel and Video editor.
- Closing an independent Director session's full tuner returns to its Director UI. Tuner-owned inspection still releases native free camera on close. Photo opened independently can still use Shift plus movement in the full tuner; a lost Shift release or closing the tuner clears that movement state. Escape, header close, mode switches and hidden-UI teardown follow the same ownership distinction.

## Validation

- Release `PIXL-12C` build and integrated 37-module audit: pass after all capture edits.
- Standalone `DirectorCameraPath::RunDeterministicSelfTest`: pass. Covers camera direction and roll, duplicate points, JSON compatibility, speed ramps, close POI turns and short path-facing continuity.
- No in-game visual test was performed for this candidate.

## Manual test before release

1. Enter Photo from gameplay and from the advanced tuner. Confirm quick panel opens, lens/capture controls respond, Shift guidance is readable, and Delete hides panel, HUD and center reticle.
2. Enter Video and capture short sharp turns plus long curves. Play through a 180-degree yaw seam, scrub/teleport to points, update a selected POI, and confirm smooth motion without mouse input moving the camera during playback.
3. Open the full tuner from each mode and close with X, Escape and the configured toggle. Confirm Director remains active, then exit Director and confirm player input, native camera, FOV, time, HUD, alpha and cursor restore.
4. Repeat with SmoothCam active and absent, loading screens, cell changes, save load and resolution changes. Video routes must clear on cell change; a rejected camera lease must leave gameplay unchanged.

This is a live-test candidate until the above runtime checks pass.
