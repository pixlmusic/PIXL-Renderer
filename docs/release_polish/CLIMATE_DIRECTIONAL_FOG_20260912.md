# Climate-aware directional fog live test

Classification B: controlled visual improvement, awaiting owner runtime validation.

The owner reported that disabling both directional fog sources removed moon-facing white mountain silhouettes, but daytime depth became too dark. This is useful diagnostic evidence, not proof that every fog/lighting issue is solved.

## Implementation

Automatic Weather Atmosphere now scales both directional fog source strengths using the active climate's sunrise/sunset intervals and current game hour. Cubic smoothstep ramps from 5% overnight to 100% after dawn, then back through dusk. Midnight wrapping supports climates whose dawn interval crosses midnight. Missing climate, nonfinite values or invalid timing ordering preserve the previous unscaled behavior. This is a climate-time approximation, not explicit celestial-caster detection or a physically calibrated moon irradiance model.

Existing automatic-weather strength blends the response; automatic mode off, zero strength and interior early-outs retain prior behavior. Density, extinction, ambient/local scattering, phase shape, geometry moonlight, and clouds are unchanged. No new GPU fields, resource slots, shader defines, or ABI changes. The runtime-resolved settings flow to both existing fog paths. Tooltip explains that sliders are daytime baselines and that the overnight factor is 5%.

Live test config restores analytical directional strength 0.70 and volumetric directional strength 0.80 (overnight 0.035/0.040 at full automatic strength). Previous weather-lit config edits retained. The public ZIP and shipping defaults are intentionally unchanged pending visual approval. No shader cache invalidation or permutation rebuild is required.

## Scoped file review

| File | Purpose/dependencies | Review/findings/change | Validation/future |
|---|---|---|---|
| engine/Modules/AtmosphereWeather.h | Pure CPU weather math; algorithm/cmath | Active, reviewed/modified; fidelity/performance/security reviewed. Adds finite-guarded climate-time scalar; constant CPU work, no I/O or new state. Invalid intervals fall back safely. | Native tests. Future: additional unusual-climate fixtures and calibrated source-aware response. |
| engine/Modules/Atmosphere.cpp | Resolves user settings to existing GPU payload; RE sky/climate and weather helper | Active, reviewed/modified; fidelity/performance/security reviewed. Applies scalar to the two source strengths only; documents UI semantics. No shared-layout changes or broad lighting retuning. | Release build and native helper tests. Future: expose a night-strength control if owner testing warrants it. |
| tools/TestAtmosphereWeather.cpp | Standalone helper regression harness | Active development test, reviewed/modified; correctness/security/performance/fidelity implications reviewed. Covers night/day, wrap, degenerate and NaN fallback, monotonic dawn/dusk, existing rain/snow ordering. | MSVC C++20 executable passes with assertions enabled. Future: endpoint derivative and integration tests. |

Rejected: fixed global clock times, disabling all fog at night, dimming surface moonlight, shader/ABI edits, or removing cache. No measured GPU performance claim; shader work is unchanged. No exhaustive repository-wide audit claimed by this scoped follow-up.

## Live validation required

Compare the same mountain view at midnight and midday; test dawn/dusk, rain/snow, fast travel and interiors. Let exposure and temporal histories settle after time jumps. Confirm the restored weak moon contribution does not recreate luminous mountain silhouettes and daylight regains depth. Shader/source inspection and passing CPU tests cannot certify the appearance.
