# Weather and Atmosphere Integration

## Scope

This live-test pass connected Atmosphere's automatic weather response to the
central `WeatherManager` transition snapshot and classifier. It also traced the
existing cloud, fog, vanilla sky and water-reflection paths to check where the
proposed shared behavior is already present.

## Integration matrix

| Concern | Existing PIXL path | Live-test result |
|---|---|---|
| One weather authority | `WeatherManager::GetContext()` supplies active endpoints, transition progress, semantic class, precipitation, fog, wind and sun visibility. | Atmosphere now consumes its protected endpoints and transition progress, matching Volumetric Clouds during interrupted transitions. |
| Weather-mod classification | `WeatherManager::ClassifyWeather()` combines record flags with names/editor IDs for unrecognized forms. | Atmosphere now uses the same classifier for fog density/start distance and Mie scattering profiles; unknown forms still fall back to TESWeather flags. |
| Cloud weather templates | Volumetric Clouds derives coverage, density, type, height, wind and detail from each endpoint, then transitions and eases each channel independently. | Kept intact, including the tuned 1.5 default detail scale and user configuration. |
| Vanilla/volumetric sky composition | Sky Veil scales native cloud visibility while PIXL Volumetric Clouds renders its world-space layer. | Kept intact; no shader binding or shared-buffer changes. |
| Atmosphere, cloud shadows and lighting | Volumetric fog evaluates Skyrim's directional light and Sky Veil's cloud-shadow map. Volumetric Clouds use the same scene light and weather sun-visibility signal. | Terrain cloud shadows now project toward the active sun and evaluate a local procedural cloud mask, avoiding the previous coverage-only blanket darkening. Fog still uses Sky Veil's shadow cubemap. |
| Weather effects | Skyrim weather records expose lightning color/frequency, thunder sounds and aurora metadata. PIXL adds procedural bolts and aurora in its sky composite. | Lightning is enabled for rainy/storm weather and uses the active record's color/frequency as available. Native Skyrim thunder audio remains the audio authority. Aurora is independently night/intensity gated; user-approved authored aurora doubling is retained. |
| Distant water reflection | Water Optics binds the resolved cloud radiance/transmittance image and composites it over the environment-cubemap fallback where the reflected ray projects into the sky image. | Kept intact; resource slot t68 and existing CPU binding were verified. |

## Changes

- `engine/Modules/Atmosphere.cpp` now reads the transition endpoints and
  interpolation factor from `WeatherManager` rather than independently reading
  `sky->lastWeather/currentWeather`.
- Automatic fog and Mie profiles use the shared classifier. This gives custom
  weather records a name/editor-ID fallback and preserves the manager's
  interrupted-transition source weather.
- Volumetric cloud CPU constants carry procedural weather-effect state and
  projected strike position. CPU/HLSL constant-buffer size is guarded by a
  static assertion; no shader resource registers were added.
- `CompositeCS` evaluates cloud shadows only where projected local cloud
  density exists and the sun is above the horizon. Its source and shipped
  shader copies are kept identical by the pipeline audit.
- Aurora and lightning intensity controls default to 0.35 and 1.0. Existing
  cloud shape, movement and user-config defaults are preserved.

## Limits for live testing

- Terrain cloud shadows are a procedural single-sample approximation. They do
  not yet provide a filtered world-space shadow map to volumetric fog; fog's
  cloud-occlusion term continues to use Sky Veil's vanilla cloud shadow
  cubemap. Sharing a volumetric shadow field needs GPU cost and temporal review.
- Enviro's useful architectural lesson is independently smoothed weather
  channels: cloud coverage/height, fog density/range, Mie scattering and
  lighting should each ease at a rate suited to the effect. PIXL already does
  this for cloud profiles and fog/Mie; lightning remains event based.
- A future unified weather output can expose climate values (cloud coverage and
  type, precipitation, fog extinction, Mie strength, sun visibility, aurora and
  lightning eligibility) to atmosphere, clouds, precipitation and water. Keep
  local Microclimate optional rather than a dependency.
- A filtered cloud-shadow representation could eventually serve terrain and fog
  together. Profile it against the current procedural approximation before
  introducing a full shadow volume.
- Water reflection of clouds is a screen-projected fallback for distant sky
  rays; it does not replace a full cubemap update or trace cloud geometry through
  the reflection path.
- Runtime visual checks still need gameplay weather transitions, high-altitude
  locations, night lighting, cloud-shadow shape and water horizons. The build
  and shader compilation are validated, but those checks require the live game.

## Validation

- Release build: `cmake --build --preset PIXL-12C` — passed; 45 shipping modules.
- Volumetric cloud compute shader entry points — all four compiled with FXC:
  `WeatherMapCS`, `RaymarchCS`, `TemporalCS`, `CompositeCS`.
- Pipeline audit confirms the shipped cloud shader and kernel overlay match.
- No game/Steam deployment was performed for this source validation pass.
