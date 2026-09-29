# PIXL Renderer 1.0.4 Changelog

This document records the changes from the 1.0.3a baseline (`66c2fd8d`) through
the 1.0.4 release. **1.0.4 is now the PIXL Renderer baseline** for subsequent
modular development.

## Release status

1.0.4 is the current release baseline. The official FOMOD is built and
validated; the matching shader cache remains an optional follow-up package
item. Source publication/commit/push is intentionally pending owner approval.

## 1.0.3a → 1.0.4 at a glance

- Ground Response: basic deformation → persistent snow/mud/material-aware surface response.
- Material Forge: legacy attenuation → finite-emitter inverse-square PBR lighting with safer BRDF handling.
- Water/atmosphere: baseline effects → refined water optics, fog, exposure, HDR and weather integration.
- Ambient lighting: vanilla-strength interiors → dimmed indoor probes with controlled indirect energy.
- WindowLife: procedural-only glass → authored rooms, exact-mask clipping, stable parallax and filtered pane response.
- Camera systems: gameplay/photo tools → paused Photo/Video/Director workflows with route editing and capture controls.
- Liquids: prototype bottle pass → named beverage/potion matching, refraction, slosh, fill and emission profiles.
- DistantLife: static distant fixtures → occluded actor/campfire/spell lighting plus 100k-unit far-field activity.
- Vegetation: repeated wind phases → spatially coherent hierarchical motion and tuned grass/material response.
- UI: legacy tuner → PIXL workspace, Quick Setup, quality profiles, search, favourites, undo/redo and accessibility polish.
- Packaging: ad-hoc deployment → manifest-verified, rollback-safe official FOMOD packaging with GPL/provenance notices.
- Compatibility: experimental CSPOM path → dormant stable transport with no forced user permutation rebuild.

## Rendering and material improvements

- Added the Ground Response surface simulation expansion for snow, mud and
  terrain-aware contact response.
  - Surface state is derived from terrain/material evidence rather than one
    hard-coded ground type.
  - Added tiled surface simulation, persistent deformation, ground marks,
    compression/recovery behaviour and environmental wetness/snow state.
  - Added snow-normal reconstruction, material-specific resistance and safer
    save/load persistence.
  - Preserved existing exclusions and bounded updates for performance.
- Improved Material Forge and PBR lighting.
  - Added/retuned finite-emitter inverse-square local-light falloff with a
    user-controlled blend and minimum emitter distance.
  - Added robust attenuation clamping to prevent near-light amplification of
    Skyrim's authored practical lights.
  - Preserved the CPU/GPU five-register Material Forge ABI while making the
    final falloff field explicit.
  - Fixed complex-material specular cache behaviour and improved GGX/BRDF,
    multi-scatter and glint handling.
  - Added safer roughness, metalness and material-layer paths, including
    numerical guards and improved parallax/POM tuning.
- Refined vegetation and grass lighting/wind history setup to reduce repeated
  motion phases, stale history and temporal instability.
- Refined water, atmosphere, exposure, HDR output and post-processing paths,
  including safer shader permutations and improved fog/lighting integration.
- Added an interior AmbientProbe control and corrected the indoor path:
  interiors no longer use the static outside-world fallback, and diffuse probe
  energy is capped below vanilla ambient by default.
- Refined rain, roof-runoff, snow and weather response interactions while
  retaining bounded dispatches and existing compatibility guards.

## WindowLife and architectural glass

- Tightened procedural pane clipping and shared the final pane mask between
  the physical glass and authored room layers.
- Improved filtered mask sampling and mip selection to reduce fine flickering
  lines on native windows at distance.
- Reconstructed room-plane derivatives from camera-relative coordinates to
  avoid precision loss at large world positions.
- Replaced binary Auto-POM pane switching with a smooth coverage fade.
- Preserved complex/PBR parallax precedence, curtains, authored masks and
  compatibility fallbacks.
- Kept the existing `t122..t127` WindowLife resource contract and per-draw
  256-byte CPU/GPU data layout.

## Camera, Photo and Director Video

- Completed the Director Photo and Video capture pass.
- Restored the standalone Video editor and viewfinder workflow.
- Added route editing, POI timeline navigation, scrubbing, point deletion and
  smooth point-to-point playback.
- Added stable camera-path quaternion construction, roll handling and yaw seam
  unwrapping.
- Reduced speed through sharp route corners without rewriting authored route
  speeds.
- Preserved camera, HUD, FOV, player-alpha, game-time and SmoothCam ownership
  across mode transitions.
- Added restrained Photo/Video reticles, Shift guidance, quick effects access
  and clean-view behaviour.
- Improved capture HUD ownership and ensured hidden tuner/editor teardown
  clears movement state and restores native gameplay input.

## PIXL interface and accessibility

- Expanded the PIXL-native tuner into a shared workspace with clearer
  hierarchy, category navigation and module state presentation.
- Added cached module search aliases and friendlier feature names.
- Added Favorites, Recent and Modified views with persisted workspace state.
- Added module reset, undo/redo and A/B comparison through official
  SaveSettings/LoadSettings paths.
- Added contextual tooltips, dependency/scene-controlled status, diagnostics,
  contextual footer shortcuts and improved scrolling.
- Refined DPI-aware spacing, typography, status chips, focus scrim and public
  quality/post-processing layouts.
- Kept engineering/debug controls separated from normal user-facing controls.
- Preserved existing settings keys and preset compatibility.

## New and experimental module work

The following systems were integrated into the 1.0.4 release-candidate source
and shader payload. They remain opt-in or experimental where noted.

- **Contained Liquids** — bounded bottle-volume prototype with fill planes,
  slosh dynamics, absorption, refraction, ripples, potion colour profiles and
  separate magic/ordinary emission controls. The implementation preserves the
  original mesh/depth path and uses a bounded scene-colour crop. True hidden
  background transmission remains a documented limitation.
- **DistantLife** — expanded from static exterior lights to runtime-accurate
  campfires, actor-mounted torches and active spell lights. Sources use the
  actual `NiLight` transform when available, so elevated reference origins no
  longer place the distant glow above the fixture. Loaded actor lights are
  refreshed through the high-process list; no AI, save, or unloaded-world
  simulation is introduced.
- **Curved Surface Mapping / CSPOM** — held dormant for 1.0.4 after review;
  its compatibility define and constant-buffer transport remain stable so
  existing users do not receive a permutation rebuild. The runtime path is
  forced off and hidden from the shipped UI.
- **RenderOrigin groundwork** — append-only GPU data and coordinate helpers for
  large-world precision, history stability and future origin-aware modules.

## Stability, performance and compatibility

- Added more bounded update schedules, fixed-size histories and dirty-state
  handling across Ground Response, WindowLife, Director and new modules.
- Removed avoidable per-frame allocations in route playback, search metadata,
  module snapshots and selected render paths.
- Added graceful optional-module failure paths and clearer shader/resource log
  messages.
- Preserved DirectX 11, SKSE/CommonLibSSE integration, existing shader register
  conventions and CPU/GPU constant-buffer contracts.
- Added numerical guards for invalid normalization, non-finite settings,
  unsafe attenuation, invalid depth/UV inputs and stale history.
- Retained existing exclusions for unsupported render targets, loading screens,
  reflections, menus and off-screen/too-small effects.

## Release tooling and packaging

- Updated executable, interface, installer, module and package metadata to
  1.0.4.
- Added/updated manifest hashing, package self-containment checks, legal notice
  validation and FOMOD XML validation.
- Added the optional SurfaceTides 1.0.2 universal bridge selection and status
  reporting.
- Hardened deployment with target-path validation, timestamped rollback
  backups, hash verification and preservation of user settings/cache.
- Added release audit, source/provenance, shader staging and package review
  tooling.
- Removed obsolete Discord automation helpers from the shipped development
  tree; PIXL remains local-only with no telemetry, downloader or updater.

## Validation completed

- Release C++ target `PIXL-12C` built successfully.
- Core package manifest validation passed.
- Complete FOMOD XML, branding, disclosure, core payload and SurfaceTides
  bridge validation passed.
- Package archives passed 7-Zip integrity tests.
- Deployed DLL and updated shader/default files were hash-verified in the
  Steam Skyrim installation.
- Contained Liquids production-math and shader permutation checks passed where
  available; WindowLife and Director automated checks passed.

## Remaining 1.0.4 release checks

- Compile and insert the final matching shader cache into the Core package.
- Complete live Skyrim checks for interiors, WindowLife stability, Director
  transitions, Ground Response persistence, Contained Liquids optical fit and
  DistantLife visibility.
- Test native AA/TAA, DLSS/FSR, frame generation, first/third person, interiors,
  exteriors, loading screens, menus, cell transitions and resolution changes.
- Confirm the final public package and cache hashes before publishing.

## Upgrade notes

- Existing PIXL settings and presets remain loadable; new settings receive
  defaults through the existing JSON mechanism.
- The 1.0.4 candidate must rebuild or load a cache matching its shader ABI and
  revision. Do not reuse an old 1.0.3a preloaded cache as a final release
  cache.
- Optional/experimental modules remain disabled or opt-in by default unless a
  user explicitly enables them.
# Follow-up polish (2026-09-27)

- Expanded contained-liquid matching to named wine, mead, ale, beer, brandy,
  rum, and related beverage meshes that lack a literal `glass` filename token.
  Opaque/wicker/wood/wrapped geometry remains excluded.
- Increased authored glass retention in the liquid composite so refraction and
  liquid colour sit behind the bottle instead of flattening its surface.
- Director Camera and Video entry now own a paused game-time snapshot. Video
  remains paused during authoring and playback is the explicit action that
  resumes the original timescale; Photo Mode is always frozen.

- Tightened DistantLife screen-depth occlusion so distant torches, campfires,
  actor lights, and spell emitters cannot shine through buildings or terrain;
  only a small fixture-depth allowance remains.
- Enabled Contained Liquids by default for the release candidate. Potion and
  supported clear-container beverage draws retain their vanilla geometry while
  receiving the replayed liquid optics pass.
- Added one-time diagnostics for rejected scene captures and successful liquid
  replay, making format/viewport failures visible in the log without per-frame
  spam.
