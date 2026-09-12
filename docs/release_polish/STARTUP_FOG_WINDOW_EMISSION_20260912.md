# Startup, current defaults, fog and interior-window emission

## Latest owner-default refresh

Copied the latest saved Atmosphere and Window Life sections verbatim into source
SettingsDefault.json and live RendererDefaults.json. Verified exact section
equality against UserGraphics and that every other default section is unchanged.
No further tuning: interior parallax 107.3, outdoor emission 8, outdoor visibility
1; fog settings include falloff 0.296, density 0.239, start 13111.7, sky protection
0.20 and volumetric extinction 2.55 (full float precision retained in JSON).
Backups: `build/config-snapshots/WindowLifeFog-1789205721489`. UserGraphics hash unchanged.
No DLL/shader edits, build, cache invalidation or existing archive replacement.
The source defaults will enter the next package; the previously built RC zip
still contains its earlier defaults. Scoped config correctness/fidelity/security
review: only the two requested sections changed, previous files backed up,
hardlink-safe live replacement and final JSON/hash checks passed. No performance
claim; future validation remains in-game fog/window review at multiple times.

## Scope

Owner-requested follow-up only. Preserved previous uncommitted work and exterior
window tuning. This is not a new exhaustive renderer-wide review.

## Changes and reasoning

Startup now enumerates the actual ImageReconstruction methods: Off=0, TAA=1,
FSR=2, DLSS=3. DLSS selection is capability-gated by the same runtime flag as
the main reconstruction UI, with an explanatory disabled tooltip on unsupported
devices. Saved DLSS uses upscaleMethod; non-DLSS fallback remains independent.
Off writes both settings to zero. FSR/DLSS select Quality mode. Accepting the same
quality profile no longer calls ApplyGlobal and overwrites tuned defaults.

Public Photo Mode titles/buttons replace Director labels. Home already opened
Photo Mode in the real input handler; corrected misleading startup Insert labels.
Insert still legitimately opens effects *inside* Photo Mode. Page Down is now the
C++ renderer-menu default as well as the owner's saved binding. Existing custom
bindings remain respected. Internal Director function/log identifiers retained.

## Config provenance and exact tuning

Snapshot: build/config-snapshots/20260912-StartupFog/UserGraphics.original.json.
SHA256: `3417EE953CB227F0B33F970198DD265CB719671FC1114E7E605414D9049AA930`.
Previous distribution defaults were also backed up in that directory.

Copied this current live config to distribution's SettingsDefault.json, changing
only the four tuning values below and setting FirstTimeSetupCompleted=false in
the *shipping default only*. The tuned live UserGraphics keeps that flag true.
All other settings match the snapshot, including exterior parallax 60.6, interior
depth 240, outdoor visibility 0.31, quality tiers and DLSS selection. Structural
JSON comparison confirmed exactly four live setting changes.

| Setting | Snapshot | Tuned | Reason |
| --- | ---: | ---: | --- |
| Atmosphere.fogHeightFalloff | 1.502 | 0.30 | Broader vertical layer without moving the height or raising base density |
| Atmosphere.startDistance | 24505 | 6000 | Earlier analytical distance fog; volumetrics still cover their own near range |
| Atmosphere.skyProtection | 0.15 | 0.25 | Slightly more sky retention, independent of terrain fog |
| Window Life.OutdoorViewEmission | absent | 2.5 | Independent brightness of the outdoor images seen from inside |

Fog height remains 22000; density remains 0.247. Extinction, scattering, automatic
weather strength and volume quality are unchanged. Code's fallback defaults align
for the three changed fog controls. Automatic weather can extend volumetric range
and analytic integration excludes the volume-covered range: Start Distance is not
a promise of fog starting at exactly that distance when volumes are active.

The shader uses exp2(-falloff * 0.001 * heightAboveLayer). Previously density halved
every ~666 units; now it halves every ~3333. At 5000 units above the layer, relative
density changes from ~0.55% to ~35.36%. These are analytical calculations, not
in-game measurements. The retained world-space height and smaller falloff should
give the requested taller coverage; at low elevations below the layer, base
density is unchanged. Added UI explanations for height versus thickness, plus
half-density height and weather-resolved density/range readouts. No atmosphere
shader, sample count, grid size or GPU resource layout changed in this follow-up.

## Window brightness path / ABI

OutdoorViewEmission -> serialized settings -> frameBaseData.Presentation0.y
(interior only) -> GetPresentation0().y -> linear day/night atlas color multiplier.
Slider range 0..8, initial 2.5. Existing exterior InteriorEmission remains unchanged
and its slider is clarified as Exterior View: Room Emission. Visibility/opacity
remain separate. No new SRV, cbuffer field, texture sample or atlas generation.
WindowLife payload remains 240 bytes. Existing day/night ratio is preserved.

WindowLife version becomes 0-7-3 for its existing selective invalidation:
Lighting pixel stages, supported static/object techniques, excluding skinned and
world-map descriptors. Vertex, compute and other families are unaffected by this
change. Fog config/UI changes do not require shader recompilation.

## File accounting

Each entry is active, reviewed, modified and scoped security/fidelity/performance
reviewed, with future suggestions and validation below. No third-party changes.

| Path | Purpose / findings / validation |
| --- | --- |
| engine/Menu/LaunchExperienceRenderer.cpp | Startup choices, runtime capability check, save mapping, preserved quality baseline and labels. C++ build; in-game selection test pending. |
| engine/Menu/PIXLRendererPage.cpp | Photo Mode entry labels only; existing action unchanged. C++ build. |
| engine/Menu/TuningWorkspaceRenderer.cpp | Public photo overlay/tuner labels; Home handler inspected and unchanged. C++ build. |
| engine/Menu.h | Page Down default uses existing VK_NEXT input path; custom config preserved. C++ build. |
| engine/Modules/Atmosphere.cpp | Explain real math and runtime weather values, sky fallback alignment. No GPU algorithm change. C++ build and numerical checks. |
| engine/Modules/Atmosphere.h | Matching tuned fallback height/start/falloff/protection; unchanged ABI. C++ build. |
| engine/Modules/WindowLife.cpp | Independent image emission UI/serialization/frame selection; exterior branch preserved. C++ build. |
| engine/Modules/WindowLife.h | New CPU setting, documented reuse of existing emission lane; unchanged GPU struct size. C++ static assertions. |
| pipeline/WindowLife/Kernels/WindowLife/WindowLife.hlsli | Interior-only scalar multiplication after linear day/night blend; bounded control. FXC tests. |
| pipeline/WindowLife/Module.ini | Version 0-7-3 triggers existing stage/descriptor-specific cache invalidation. Predicate traced. |
| distribution/SKSE/Plugins/PIXLRenderer/SettingsDefault.json | Owner config baseline plus documented tuning/startup flag; no unrelated reset. Structural comparison. |
| distribution/PIXL-RENDERER-README.md | Correct Home/Photo Mode description; unrelated existing edits preserved. Text check. |
| tools/DeployPixlRcFollowup.ps1 | Now skips identical files and NEVER clears/moves PipelineLibrary. Optional explicit tuned config requires original live hash match. Backups/hardlink-safe replacement retained. Script parsing and live hash checks. |

## Validation and remaining checks

Live deployment completed: five changed files (DLL, defaults, WindowLife descriptor,
WindowLife shader, tuned UserGraphics), all hash-verified. Backup:
`build/deployment-backups/RC-DayNight-20260912-162533-bd3d554810b94f3493bd18727b79f2e5`.
No cache paths were changed by deployment; before/after inventory checked separately.
New archive: `dist/PIXL-Renderer-RC-20260912-StartupFog.zip` (240,950,680 bytes),
SHA256 `FC113026D6E07C8AC8DD74BC3DEBAAD1DE3366D1346878004B80D95B5CD021F0`.
Staged audit and 330-payload manifest verification passed; 7-Zip integrity test passed.

Release CMake/VS2022 build succeeded. DLL SHA256:
`1FC8A6C6C8FF0AD8DDA9E7D886DF9B1795227D9DC11CB5518DA916B622BBD926`.
Six WindowLife Lighting FXC permutations and the integrated MaterialForge +
RadiantGrid + NaturalLighting + Atmosphere permutation succeeded. No new resource
bindings or CPU/GPU offsets. JSON structural comparison and git diff --check pass.
The source/package audit is also run during handoff; no whole-renderer security
or exhaustive shader-permutation claim is made.

In-game validation remains necessary: startup Off/DLSS, unsupported hardware
disabled option, Home/Page Down, unchanged-quality Save, low/high elevation fog,
clear/storm weather, sky horizon, and day/night interior brightness. The visual
tune is class B (controlled, needs game review), not a guaranteed final look.
No GPU timing or performance improvement is claimed. Scalar emission has no added
texture work; fog changes retain sample counts. Both settings remain adjustable.

## 5 Future Visual Improvements

1. Fog screenshots at matched weather, time and camera altitude.
2. Artist-friendly fog thickness control mapped to half-density height.
3. Outdoor background exposure matched to current weather luminance.
4. Optional separate night background exposure after motion comparison.
5. Refracted artwork visibility calibration across stained/clear windows.

## 5 Future Performance Improvements

1. Profile volumetric coverage at the retained quality tier.
2. Measure atlas sampling in window-heavy rooms.
3. Capture targeted shader rebuild counts after this module change.
4. Cache UI-only diagnostics if their CPU cost proves measurable.
5. Automate cache-preservation regression tests for deployment.

## 5 Future Feature / Research Ideas

1. Preset provenance shown in UI.
2. First-run automated ImGui selection tests.
3. Fog altitude diagnostics alongside the live preview.
4. A/B config snapshots without replacing user tuning.
5. Automatic transactional rollback for interrupted deployment.
