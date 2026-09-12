# PIXL Renderer 1.0 final release preparation

Owner requested final packaging, no bundled UserGraphics, weather-responsive
denser rain/snow, clearer/funnier compiler tips, latest cache and source push.
No live config deletion or cache invalidation is performed by this task.

## Final changes

- Base fog density 0.239 -> 0.27; analytical start 13111.7 -> 10500 units.
  Retained owner height 22000, falloff 0.296, sky protection 0.20, volumetric
  extinction 2.55 and WindowLife tuning. Existing saved user config remains separate.
- Automatic weather now blends precipitation profiles alongside engine visibility:
  clear density 1.0/start 1.0, cloudy 1.10/0.92, rain 1.45/0.78, snow 1.65/0.65.
  Multipliers are relative to the same engine fogFar; not fixed visibility distances.
  Weather transition percentage is shared with the existing scattering response.
  Auto strength still scales the response and disabling auto retains manual values.
  Interior cells retain their existing early-out. No shader, cbuffer or resource
  changes were needed; the validated shader cache remains compatible.
- Replaced 30 dense/occasionally outdated feature claims with lighthearted,
  actionable tips: controls, cache reuse, fog thickness, image reconstruction,
  window brightness and a few patient-dragon/mudcrab jokes. No false progress claims.
- Public RELEASE staging rejects UserGraphics even when explicitly requested;
  requires cache enabled and provided; validates cache plugin/ABI/module versions;
  copies only Library.ini and stage .pixlbin files, not mod-manager markers.

## Cache provenance / source compatibility

Captured the latest live Data/PIXL/PipelineLibrary with Skyrim closed into
build/release-final-20260912/CacheSnapshot. Each of 3475 .pixlbin files plus Library.ini
was SHA256-checked against the live source after copying. The other 1236 live
files were extensionless Vortex folder markers and are not shader stages.

Live shader payload hashes matched the last verified StartupFog source package.
No HLSL changes have occurred since that package; final changes are CPU weather
constants/calculation, UI strings, defaults and packaging. WindowLife cache version
is 0-7-3. Cache plugin 1-0-0-0, layout PIXL.StageShard.v1 and ABI
PIXL.SharedBuffers.20260902.1 match the renderer. All shipped module descriptor
versions match the captured cache. Preloaded stages are not a claim of every
possible permutation: unseen combinations may still compile on demand.

## Validation

- VS2022/CMake Release PIXLRenderer target passed.
- Final DLL SHA256: `95B284C1780582F086471369A0CE9E8218A398EEAED67F3D21C225549078214E`.
- Native C++ TestAtmosphereWeather passed: ordering clear < cloudy < rain < snow at
  three visibility distances, 101-step monotonic transitions, endpoint and
  non-finite/range guards. Tests exercise the same helper used by runtime C++.
- Existing 14 manifest positive/negative cases passed.
- RELEASE explicitly rejects a UserGraphics input before staging.
- Preflight package audit passed: 37 active modules, one retired source-only module,
  3806 manifest payloads, 3475 preloaded shader stages, no UserGraphics or markers.
- git diff --check passed. Source default config checked for credential/personal
  path fields; none found by the scoped search.
- Dirty FidelityFX submodule is the existing intentional CMake-applied short-path
  compiler patch. Reverse-check against the tracked patch passed. Do not commit
  a new submodule revision or discard that reproducible local patch.

## Scoped file accounting

All below are active, reviewed, modified/added and scoped security, fidelity,
performance, correctness and maintainability reviewed. Earlier changed files are
accounted for in RC_WINDOWLIFE_DAY_NIGHT_20260912.md and
STARTUP_FOG_WINDOW_EMISSION_20260912.md. No fresh exhaustive whole-tree audit claim.

| Path | Purpose, dependencies, findings and validation |
| --- | --- |
| engine/Modules/AtmosphereWeather.h | Pure weather profile and bounded density math; no game resources, tests directly include it. Guards nonfinite transition/distance. |
| engine/Modules/Atmosphere.cpp | Resolves Skyrim blended weather to current CPU/GPU settings; auto/manual/interior behavior preserved. Native build and helper tests. |
| engine/Modules/Atmosphere.h | Slightly denser/nearer fallback values; GPU structure unchanged. Existing ABI assertions compile. |
| engine/Menu/OverlayRenderer.cpp | Rotating 30-tip text only; UI loop and timing unchanged. Compiled, public key guidance checked. |
| distribution/SKSE/Plugins/PIXLRenderer/SettingsDefault.json | Final density/start adjustments only this pass; fresh-user defaults without personal config. JSON/audit checks. |
| distribution/PIXL-RENDERER-README.md | Final user guidance, corrected startup/capability/cache expectations and limitations. Reviewed against code. |
| tools/StagePixlRendererStandalone.ps1 | Narrow public release/cache guards, version checks and filtered cache copying. Preflight and negative guard test passed. |
| tools/TestAtmosphereWeather.cpp | Build-only assertions of real helper; native test passed. No runtime cost or external I/O. |

## Remaining risks / release distinction

Owner has approved the preceding in-game look. This final weather adjustment has
not been visually profiled in game; compare clear/rain/snow transitions, high and
low elevations and sky horizon before broad rollout. It is a controlled class-B
visual change, not a performance claim. No new samples or GPU dispatches; CPU adds
a small bounded weather calculation. Optional DLSS-G/Neural Rendering retains its
documented hardware/long-session limitations and defaults off. A final package
label is not an exhaustive security or compatibility certification.

## 5 Future Visual Improvements

1. Matched weather screenshot baselines.
2. Storm-specific ground visibility calibration.
3. Artist-friendly half-density-height UI.
4. Outdoor atlas exposure under heavy clouds.
5. Fog/reflection sky comparisons at dawn and dusk.

## 5 Future Performance Improvements

1. Measure volumetric cost at real display resolutions.
2. Profile shader cache misses by family.
3. Reduce unnecessary startup permutation requests with evidence.
4. Measure window atlas bandwidth before changing compression.
5. Automate package/cache hash verification in CI.

## 5 Future Feature / Research Ideas

1. Weather-specific user density overrides.
2. In-game cache provenance display.
3. Automated first-run GUI regression tests.
4. Source-linked release artifact attestation.
5. A/B capture presets that preserve the gameplay config.
