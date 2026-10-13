# PIXL Renderer 1.0.8 Prerelease UX and Compatibility Audit

## Scope and outcome

Reviewed the installed-package flow, first-run settings path, Quick Setup and feature pages, Photo Mode, Director Mode, module controls, configuration persistence, shader/package identity, and authored-versus-vanilla data handling. The repository audit reports 45 shipping modules and one intentionally retired source-only module. The release candidate is still labeled 1.0.7 in package metadata; this is a local 1.0.8 prerelease checkpoint and test build, not a public version bump.

The pass preserved current settings and visual defaults. It corrected one Atmosphere fallback mismatch, made settings and authored override writes atomic, and bounded Director path file size and point count. Existing UI and renderer tests remain green. Runtime checks involving Skyrim's live camera, real mod lists, and rendered image quality remain manual validation items.

## User flow

| Flow stage | Reviewed behavior | Result / edge case |
|---|---|---|
| Install | FOMOD/core payload, package manifest, version/channel, hashes, plugin identity, cache payload, optional Streamline sidecar | Manifest fixture suite covers valid payloads, missing/extra/duplicate entries, path traversal, alternate streams, empty paths, invalid hashes/channels, and missing executable. Release staging verifies module/cache identities and excludes machine-local untracked sources from the public release channel. |
| Deployment | Package manifest is checked before file copies; each target is constrained under `Data`; changed files are backed up; the deploy script refuses a running Skyrim process; user settings and live pipeline cache are intentionally preserved | Deployment fixture passes install/repeat-install, hardlink isolation, backup, cache/config preservation. Steam deployment completed after all checks; no game process was running. |
| First launch / Quick Setup | Renderer enablement, upscaler choice, unavailable-runtime handling, restart guidance, and initial configuration merge | UI setup contract inspected. Atmosphere's serialized defaults and C++ defaults now match for all 55 checked fields. User cloud settings and packaged defaults were compared and matched. |
| Feature pages | Public PIXL pages and advanced tuner/module settings; module registration versus page exposure; callbacks, disabled controls, scaling, and lifetime | GUI harness passed 15 scaled layouts plus disabled controls and callback exception/stack isolation. Config save now serializes to a sibling temporary file and atomically replaces the destination so a failed write cannot truncate the previous valid file. |
| Photo Mode | State ownership, active-camera checks, Skyrim menu/loading guards, restoration of renderer/game state, interaction with foreign free-camera state | Source guards and restoration paths reviewed. Automated UI layout checks pass; live transition testing inside Skyrim is still required. |
| Director Mode | Camera eligibility, route editing/import/export/playback, cell binding, SmoothCam/native camera lease, and cleanup | Native route evaluator self-test and existing UI contracts pass. Imported route data is now limited to 512 points and 4 MiB; non-finite exposure/focus values are sanitized; capture controls disable at the point cap. |
| Module settings | Default/user/override precedence, per-module serialization, weather and scene settings, cache identity, vanilla/authored content fallback | Existing default/user/override merge paths preserved. User override and applied-override tracking writes now use an adjacent temporary file and atomic publish. The 45-module registry and versions are listed in `MODULE_REVIEW.md`; module compatibility remains based on runtime-derived materials and fallbacks, so a finite test matrix cannot guarantee every authored asset. |

## Implemented changes

- `engine/Modules/Atmosphere.h`: aligned fallback fog height/falloff to the shipped default values (`22000.0f`, `1.283f`). `TestPixlAtmosphereDefaults.ps1` confirms 55 serialized fields match their C++ float32 defaults.
- `engine/State.cpp`: settings serialization completes before the destination is touched; a same-directory temporary file is flushed and atomically published with `MoveFileExW`. Write failures retain the previous config and clean up the temporary file.
- `engine/SettingsOverrideManager.cpp`: user-authored override and tracking JSON now use the same atomic-publish pattern; existing tracking backup remains.
- `engine/Director/DirectorCameraPath.*` and `engine/Menu/TuningWorkspaceRenderer.cpp`: reject oversized routes and cap rebuild complexity; UI communicates the cap by disabling capture actions.

## Compatibility policy and limits

Vanilla and mod-authored content both reach PIXL through engine runtime data and shader material flags, with module-level fallbacks when optional metadata is absent. This audit did not add a hard dependency on a specific weather, body, texture, or mesh mod. Cache/package validation confirms the tested package and cache share the current layout, shared-buffer ABI, shader revision, and enabled-module identities.

The review does not claim universal compatibility with all SKSE plugins, shader replacers, body replacers, weather records, animation/camera plugins, or authored material conventions. The live game list and its behavior cannot be represented fully by offline unit tests.

## Manual validation still needed in Skyrim

1. Launch from the tested Steam installation and confirm the installed plugin loads with the existing user configuration and shader cache intact.
2. Walk Quick Setup once with the installed upscaler available, then once with it unavailable; verify the displayed restart and fallback state matches runtime behavior.
3. Open every public page and the advanced tuner at the target UI scale; toggle one representative module and verify its setting persists after a clean restart.
4. Enter/exit Photo Mode through normal gameplay, dialogue, map/menu, loading, death, and first/third-person camera transitions; verify control ownership and time/weather/FOV/HUD restoration.
5. Exercise Director free-camera lease with and without SmoothCam; load a valid legacy route, reject a wrong-cell route, capture/play/pause/stop, hit the 512-point guard, and confirm native camera state returns after exit.
6. Repeat representative vanilla and authored mesh/material cases for terrain, grass, statics, skinned actors, water, and weather transitions. Validate no shader cache invalidation or unexpected compile storm.

These are remaining image/runtime gates, not blockers to package/deployment integrity.
