# PIXL Volumetric Clouds — Architecture and Reference Audit

## Scope and classification

- **Module:** PIXL Volumetric Clouds (`VolumetricClouds`)
- **Target:** Skyrim Special Edition 1.5.97, DirectX 11
- **Release classification:** C — experimental, isolated module intended for live testing
- **Reference:** local Enviro 3 package, inspected as a feature and design reference only
- **Provenance rule:** no Enviro source code, shader code, textures, noise assets, profiles, or serialized data are copied into PIXL.

## Existing PIXL architecture audit

| Area | Active PIXL implementation | Cloud integration decision |
|---|---|---|
| Skyrim weather | `WeatherManager` reads `RE::Sky`, `TESWeather`, transitions, precipitation, fog, wind, sun visibility, and classifies unknown or mod-added weather records. It also supports plugin/FormID overrides. | Reuse as the single semantic weather authority. Interpolate cloud targets continuously; do not maintain a second Skyrim weather parser. |
| Sky and vanilla clouds | Skyrim sky geometry contains authored 2D cloud layers; PIXL also has `SkyVeil` layered shading plus the new 3D volume. | While PIXL Volumetric Clouds is enabled, fade vanilla cloud color behind the volume to avoid doubled silhouettes. A live visibility control retains optional distant cirrus. Preserve Sky Veil's separate cloud-shadow output and restore vanilla clouds when the module is disabled. |
| Atmosphere and fog | `Atmosphere` supplies height fog, volumetric fog, weather adaptation, aerial perspective, and temporal history through `VolumetricContext`. | Composite clouds into the main HDR target after deferred opaque composition. Sample the published integrated froxel scattering/transmittance at representative cloud depth; avoid a duplicate fog march. |
| World-space weather | `Microclimates` contains a useful regional field design, but the module is currently retired explicitly by `RenderModule::Load`. | No hard dependency. Generate an internal persistent RGBA weather map. Keep the binding contract isolated so an active regional provider can replace it later. |
| Render origin | PIXL maintains camera-relative transforms and render-origin state through the renderer/frame data. | Reconstruct rays in camera-relative space and evaluate cloud noise from a split absolute camera position plus bounded periodic coordinates to remain stable far from origin. |
| Temporal state | `TemporalContext` owns frame index, previous matrices, jitter, discontinuity detection, and history invalidation. | Register cloud history, clear it on discontinuity, and use the shared frame index/jitter. Do not create a parallel camera-history authority. |
| Scheduling/resources | Modules own resources and shaders; `Deferred::DeferredPasses` is the stable point after opaque composition and before later transparent/weather response work. | Dispatch weather map, low-resolution raymarch, temporal resolve, and full-resolution composite from one isolated module call at this point. |
| Depth/reconstruction | Scene depth, inverse matrices, motion information, and image reconstruction infrastructure are already available. | Use full-resolution scene depth to stop cloud marching at opaque geometry. Reproject cloud history with both camera and cloud-wind motion; full-resolution composition currently uses linear filtering. |
| Sun/ambient/exposure | Skyrim sky exposes sun direction, time/weather colors, ambient colors, and fog response. Natural lighting/exposure consume the resulting HDR frame. | Derive cloud sun and ambient colors from the live sky/weather state and write HDR values before exposure. |
| SkyBounce/WorldProbes/HybridGI | These are independent irradiance/GI systems with sensitive update and binding contracts. | Do not change their ABI in this prototype. Provide cloud transmittance and screen/world shadowing locally; direct probe injection is future work after validation. |
| Frame annotations | PIXL identifies Skyrim volumetric lighting and reconstruction-sensitive passes. | Keep cloud rendering in the explicit module pass, with no heuristic pass interception. |

## Render architecture

1. **Weather synthesis** updates a persistent 512x512 world-space RGBA map at a low cadence. Channels are coverage, cloud type/vertical development, precipitation potential, and turbulence. Large fronts advect with wind while independent octave phases evolve, so the density field does not move as one texture.
2. **Low-resolution raymarch** intersects camera rays with a bounded spherical/curved cloud layer, samples multiple analytic noise scales under weather-map control, integrates Beer-Lambert transmittance, and accumulates direct plus ambient scattering.
3. **Lighting** uses a stable dual-lobe Henyey-Greenstein phase approximation, short light-cone sampling, height-dependent density, powder response, silver lining, and an inexpensive multi-scattering octave approximation.
4. **Temporal resolve** reprojects cloud samples through previous view-projection state, rejects invalid/disoccluded history, clips history to a local neighborhood, and accounts for cloud advection as well as camera motion.
5. **Composite** linearly filters cloud radiance/transmittance into the main HDR target. Terrain and geometry remain in front of the cloud layer. A low-frequency cloud-shadow approximation attenuates distant outdoor lighting without modifying Skyrim shadow maps.

The raymarch uses adaptive distance steps, jittered sample origins, empty-region acceleration, mip-like analytic noise reduction with distance, and early termination. The implementation does not allocate or stream proprietary 3D noise textures.

## Enviro 3 feature audit and PIXL feature matrix

The inspected Enviro package contains its volumetric cloud module, runtime profile/settings types, raymarch and blend shaders, base/detail 3D noise assets, curl and blue-noise textures, weather-map generation, temporal buffers, downsampled depth, and cloud-shadow support. The following matrix records observed features and the independent PIXL design.

| Enviro feature | Observed Enviro approach | PIXL equivalent | Reuse PIXL system? | PIXL implementation |
|---|---|---|---|---|
| Coverage/type | Moving weather map, per-layer coverage/type controls | Persistent world map plus Skyrim-weather targets | WeatherManager | RGBA map and continuous weather transition response |
| Dual layers | Two independently configured cloud layers | One physical layer with two formation bands | No | Low/mid formations and optional upper wisps in one march |
| Base/detail shape | Authored 3D base and detail noise assets | Procedural multi-scale gradient/cellular FBM | No | Independent world-space large, medium, and erosion scales |
| Curl/distortion | Curl texture and wind offsets | Analytic domain warp/turbulence | No | Bounded low-frequency curl-like warp |
| Wind/evolution | Per-layer horizontal/upward wind and moving weather map | TESWeather wind plus evolution rates | WeatherManager | Separate front advection, shape drift, and erosion evolution |
| Altitude/thickness | Per-layer bottom/top height | Weather-responsive base/top altitude | Renderer world state | Curved layer intersection and normalized height profile |
| Density/erosion/anvil | Profile controls and height/type shaping | Weather-class profile with user multipliers | WeatherManager + module settings | Height gradients, type interpolation, erosion, storm vertical bias |
| Direct/ambient light | Sun/moon/ambient gradients and scattering controls | Live Skyrim sun/sky/weather colors | Skyrim sky state | HDR direct and ambient in-scattering |
| Phase/silver lining | HG scattering and silver-line controls | Stable dual-lobe HG | No | Forward/back lobes with user-controlled silver response |
| Self shadow | Short cloud light march/cone samples | Short adaptive light cone | No | Four to six offset density samples toward sun |
| Powder | Density/view dependent brightening | Powder approximation | No | Energy-limited edge/internal brightening |
| Multiple scattering | Multi-octave attenuation approximation (A/B/C controls) | Energy-conserving octave approximation | No | Three inexpensive scattering orders |
| Low-resolution render | Configurable downsampling, commonly quarter resolution | Half/quarter resolution quality tiers | Renderer dimensions | Dynamic low-resolution UAV resources |
| Temporal accumulation | Double buffered reprojection with blend time | Shared TemporalContext history | TemporalContext | Matrix reprojection, rejection, neighborhood clamp |
| Depth handling | Downsampled depth and depth-aware final blend | Full-resolution depth sampled during march | Deferred depth | Opaque geometry rejects cloud intervals; current final cloud upsample is linear |
| Horizon behavior | Curved planet/layer ray intersections and adaptive steps | Curved cloud shell approximation | No | Stable horizon intersection and longer horizon steps |
| Cloud shadows | Optional cloud-shadow texture/intensity | Weather-map and density-derived world shadow | No | Screen/world low-frequency shadow approximation |
| Volumetric shafts through cloud gaps | No separate cloud-gap shaft control was found in the inspected cloud module; the package has a distinct volumetric fog/light system | PIXL `Atmosphere` and native `LightVolumes` run before cloud composition | Reuse `VolumetricContext` for fog; preserve native light-volume contribution in the background | Cloud radiance receives fog scattering/extinction and remaining cloud transmittance attenuates existing shafts, leaving gaps bright. True cloud shadows in froxel/shaft lighting need an early weather-field provider before those passes. |
| Weather transitions | Cloud profile values blended by weather system | Semantic profile targets blended through TESWeather transition | WeatherManager | Current/incoming transition plus damped temporal evolution |
| Storm/overcast | High density/coverage and deeper layer profiles | Automatic class-driven profiles | WeatherManager | Clear through supernatural profiles with precipitation coupling |
| Day/night and sunset | Sun/moon gradients | Skyrim sun visibility, sky colors, and time | Skyrim sky | Direct color and extinction track live sky state |
| Quality presets | Downsampling, steps, LOD distance | Performance/Balanced/High/Ultra | Module UI/settings | Resolution scale, primary steps, light steps, history settings |
| Floating origin | Unity floating-origin handling | PIXL renderer origin/camera-relative state | Existing renderer | Split-coordinate world sampling and periodic domains |

## Weather classification and overrides

`WeatherManager` already performs the required resilient classification using weather flags, precipitation state, cloud/fog/sun signals, and authored exact overrides. The module converts that semantic context into clear, mostly clear, partly cloudy, cloudy, overcast, foggy, rain, heavy rain, snow, heavy snow, storm, and special cloud profiles. Thresholds within Rain/Snow/Storm refine the broad class without relying on editor IDs or vanilla FormIDs.

Exact plugin/FormID overrides continue through PIXL's existing weather override system. All registered cloud controls can therefore be overridden in weather JSON without requiring a profile for every weather mod. Unknown records always receive an automatic profile.

## Compatibility and failure behavior

- Interiors, invalid camera state, unavailable depth, missing shaders, and unavailable UAV targets skip the pass safely and log one actionable error.
- The module owns no global shader permutation defines, so enabling or updating it does not invalidate Skyrim's persistent shader cache.
- Vanilla cloud geometry remains available, which preserves mod-authored sky layers and provides a fallback if the module is disabled.
- The module does not require Microclimates, HybridGI, SkyBounce, DLSS, FSR, ENB, Community Shaders add-ons, network access, or authored cloud assets.
- All DX11 resources are recreated on resolution change and unbound after dispatch.

## Validation plan

- Compile all compute entry points with the repository shader compiler path.
- Build the engine in the active release configuration.
- Check CPU/HLSL constant-buffer sizes and field order with `static_assert`.
- Verify no new shader permutation dependency or persistent shader-cache invalidation.
- Live-test clear/cloudy/storm transitions, unknown mod weather, interiors/exteriors, dawn/sunset/night, mountain altitude, fast travel, resolution changes, TAA/DLSS/FSR, and camera cuts.

