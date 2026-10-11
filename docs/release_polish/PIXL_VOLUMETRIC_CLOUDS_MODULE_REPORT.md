# PIXL Volumetric Clouds — Module Report

## Architecture

PIXL Volumetric Clouds is an isolated DirectX 11 compute module. It renders after the deferred opaque composite, when the final displaced depth and HDR scene target are available, and before DistantLife, ReactiveFX, and RainResponse presentation work.

The frame pipeline is:

1. Low-frequency persistent 512x512 world weather map synthesis and advection.
2. Adaptive low-resolution volumetric density raymarch.
3. Direct and ambient cloud lighting with short self-shadow rays.
4. Matrix-reprojected double-buffer temporal resolve.
5. Full-resolution HDR composite with PIXL froxel-fog integration and broad world-space cloud shadows.

It consumes PIXL's `WeatherManager`, shared b5 lighting state, Skyrim b12 camera state, `TemporalContext`, the published `VolumetricContext` fog volume, final scene depth, current HDR target, and profiler. Cloud radiance is fog-attenuated with the same integrated froxel transmittance/scattering used by scene materials. Existing light-volume shafts remain in the background scene and are attenuated by cloud transmittance during compositing, preserving shafts through cloud gaps. It has no Skyrim shader permutation dependency and does not invalidate the persistent game shader cache.

## Files

| File | Purpose | Dependencies | Review result and validation |
|---|---|---|---|
| `engine/Modules/VolumetricClouds.h` | Public module, user settings, CPU/GPU ABI | `RenderModule`, `Buffer` | 240-byte constant ABI is 16-byte aligned and statically asserted. Reviewed for ownership, defaults, cache behavior, and UI exposure. |
| `engine/Modules/VolumetricClouds.cpp` | Weather classification, resource lifecycle, dispatches, temporal registration, UI | WeatherManager, TemporalContext, Deferred, State, D3D utilities | Resource failures disable or skip safely. Every bound SRV/UAV/CB is unbound after use. Release build passed. Clock no longer wraps; frame motion is supplied for temporal reprojection. |
| `pipeline/VolumetricClouds/Module.ini` | Integrated alpha module identity | ModuleVersions generation, packaging | CMake generated module metadata and included the module in the build. |
| `pipeline/VolumetricClouds/Kernels/VolumetricClouds/VolumetricCloudsCS.hlsl` | Weather, density, lighting, temporal, composite kernels | SharedData b5, FrameBuffer b12 | All four entry points compile with FXC `cs_5_0`. Numerical guards cover normalization, division, projection, transmittance, and non-finite output. |
| `engine/Globals.h/.cpp` | Global module declaration and instance | module lifetime | The instance participates in normal PIXL lifetime and is not dynamically loaded. |
| `engine/RenderModule.cpp` | Module registry | module load, settings, UI | Registered beside sky/atmosphere systems. It declares no permutation dependencies. |
| `engine/Deferred.cpp` | Render integration | final opaque color/depth | Dispatch occurs after deferred UAV cleanup and before later transparent weather effects. |
| `distribution/SKSE/Plugins/PIXLRenderer/SettingsDefault.json` | Shipped defaults | settings loader | Valid JSON. The cloud profile matches the active UserGraphics cloud block field-for-field; C++ fallback defaults use the same values. |
| `docs/release_polish/PIXL_VOLUMETRIC_CLOUDS_ARCHITECTURE.md` | PIXL and Enviro reference audit, feature matrix | local source audits | Records source provenance and integration decisions before implementation. |

## Current behavior

- Renders actual three-dimensional density between weather-responsive cloud base and top altitudes.
- Shows cloud bottoms from the ground and cloud tops when the camera rises above the layer.
- Uses large, medium, and fine procedural scales for fronts, billows, cavities, erosion, and wisps.
- Anchors formation sampling and the weather map in absolute world space while camera matrices remain in Skyrim's camera-relative space.
- Reads current/outgoing `TESWeather`, transition percentage, authored 32-layer cloud alpha, weather flags, precipitation, fog, wind, sun visibility, exterior state, and world/camera continuity.
- Interpolates semantic and authored weather signals continuously. It does not switch profiles at the midpoint of a weather transition.
- Clear TESWeather records now drive global coverage to zero; the weather map scales its local variation by that coverage, so its noise cannot leave isolated clouds on otherwise clear days. The configured user coverage multiplier remains the cloudy-weather baseline.
- Cloud coverage, density, type, precipitation, cloud-base offset, and layer thickness use separate response times. Height and thickness settle over longer intervals than coverage so rain/storm transitions feel gradual without making clearings sluggish.
- The raymarch is clipped by scene depth and the full-resolution composite now draws only cloud segments in front of terrain/objects. This lets the player enter low cloud volumes while nearby mountains still occlude clouds behind them.
- CameraSuite DOF receives the resolved cloud transmittance mask and suppresses CoC only on cloud-covered pixels, keeping cloud detail crisp without exempting the complete sky or horizon.
- Procedural fur is conservatively inferred from dog, wolf, fox, bear, rabbit, and hare coat/body material names, or explicitly named fur/pelt sub-materials. Actor race no longer enables broad humanoid anatomy; apparel paths and hard anatomy are excluded before classification.
- Night cloud lighting uses Skyrim's phase-aware Masser and Secunda colors and directions for a low-energy directional fill. It fades in with the sun below the horizon and leaves the daytime and sunset lighting path unchanged.
- Handles unrecognized weather mods through record data and PIXL's automatic classifier. PIXL weather overrides can tune coverage, density, base, thickness, erosion, and shadow strength by plugin/FormID.
- Fades Skyrim's 2D cloud layers behind the volume to prevent doubled silhouettes. A live visibility control can retain distant cirrus; Sky Veil keeps its separate cloud-shadow capture.
- Uses the current user profile's tuned settings as the shipped baseline. The cloud base is 18,124 units and thickness remains 40,893 units, preserving the user's preferred mountain-crossing cloud height.
- Supports active directional light color and direction, including PIXL's sun/moon switching.

## Fidelity improvements

- Beer-style optical extinction creates depth instead of alpha-layer fading.
- Height profiles form darker bases, illuminated tops, storm vertical development, and eroded boundaries.
- Dual-lobe Henyey-Greenstein scattering produces forward glow and restrained backward response.
- Short light-cone sampling creates cloud self-shadowing.
- Powder, silver lining, and three-order multiple-scattering approximation improve dense formations without flattening them to white.
- Large fronts and local cells evolve independently of fine density motion.
- Cloud shadows use the persistent weather field, so terrain darkening moves coherently with the cloud system.

## Performance improvements

- Performance/Balanced render at quarter resolution; High/Ultra render at half resolution.
- Primary ray steps are 32/48/80/96 and light steps are 3/4/5/6.
- Empty runs advance by larger distances.
- Dense rays terminate once transmittance is negligible.
- Geometry depth shortens or rejects cloud intervals before marching.
- Weather-map persistence now responds on a practical weather-transition timescale instead of retaining stale calm-weather coverage for close to a minute.
- Weather synthesis runs at a maximum of 5 Hz. Camera recentering is folded into that cadence, avoiding extra full-map dispatches when crossing snapped map texels.
- Temporal reconstruction recovers detail from stable jittered low-resolution samples.
- The default quality is High, matching the active user profile. The volume has a live enable switch; vanilla cloud geometry is suppressed only while it is enabled.

## Stability improvements

- `TemporalContext` invalidates history on camera cuts, teleport, worldspace changes, origin shifts, resolution changes, settings changes, module reset, and device reset.
- Reprojection uses Skyrim's current and previous camera-relative coordinate contract.
- Procedural shape advection and weather-map fronts use a consistent wind direction. Temporal reprojection compensates for wind motion as well as camera/origin motion.
- History is neighborhood clipped and rejected by representative cloud-depth mismatch and weather motion.
- Fogged cloud compositing samples the existing integrated atmosphere volume at representative cloud depth; if that volume is unavailable or disabled, the lookup returns neutral transmittance.
- The weather field recenters in snapped texel increments and remaps its prior field, avoiding camera-relative swimming.
- Interruption-safe WeatherManager transitions prevent stale A-to-B state during an A-to-B-to-C weather sequence.
- Interiors and hidden/unavailable render state skip cleanly.

## UI changes

Normal controls use visible-result names: Cloud Quality, Coverage, Density, Cloud Base, Cloud Thickness, View Distance, Shape Scale, Edge Detail, Erosion, Turbulence, Cloud Movement, and lighting controls. Phase anisotropy, temporal stability, and diagnostic views stay in developer controls.

The module is marked Alpha. Its controls are in the main tuner under Lighting → Sky & Atmosphere → Volumetric Clouds.

## Validation

- FXC Shader Model 5.0: `WeatherMapCS`, `RaymarchCS`, `TemporalCS`, and `CompositeCS` passed after the motion ABI update.
- FXC Shader Model 5.0: smooth-normal, static model-space, and skinned model-space Fur Shell geometry variants passed.
- The active AUTO_FUR skinned environment-map pixel permutation compiled under FXC against the deployed shader tree. Runtime compile failures now report HRESULT and defines for diagnosis.
- Visual Studio 2022 Release target: `PIXLRenderer.dll` built successfully.
- Standalone LIVE-TEST stage: manifest validation passed with the descriptor and shader in their runtime paths.
- Default settings JSON parsed successfully.
- CPU/HLSL constant layout is statically asserted at 240 bytes.
- `git diff --check` reported no patch whitespace errors in the cloud changes.

Live Skyrim evaluation remains necessary for final art tuning and measured GPU timings. No timing claim is made without capture data.

Cloud/grass follow-up: the sky shader fades vanilla cloud color while preserving the Sky Veil shadow target. Grass shadow-map draws bypass the camera-frustum-culled optimizer and use Skyrim's native instanced grass draw for cascade coverage.

## Remaining issues

- Broad cloud shadows are screen/world approximations and do not yet inject into Skyrim's cascaded shadow atlas or modulate froxel in-scattering before cloud composition.
- SkyBounce and WorldProbes do not yet receive cloud-filtered irradiance; changing those ABIs is deferred until the cloud prototype is visually validated.
- The prototype uses procedural noise rather than artist-authored 3D textures. This avoids licensed reference assets and streaming dependencies, but profile art direction currently comes from settings and weather records.
- Vanilla cloud geometry remains visible through holes. A future compatibility control can fade individual vanilla layers after mod-weather testing proves a safe hook.

## 5 Future Visual Improvements

1. Move a low-cost cloud-visibility/weather field update ahead of the Atmosphere and LightVolumes passes so clouds can modulate directional froxel illumination and native shaft generation in the same frame.
2. Add separate high cirrus and low stratus density bands with independent wind shear.
3. Derive localized precipitation shafts from the cloud weather map and RainResponse.
4. Add terrain-aware orographic lift around mountain ranges through a low-frequency height field.
5. Add artist profile curves for cumulus, stratus, cirrus, cumulonimbus, and supernatural formations.

## 5 Future Performance Improvements

1. Build a hierarchical empty-space map from the weather field for coarse interval skipping.
2. Checkerboard the low-resolution raymarch while preserving full-rate temporal resolve.
3. Update distant weather-map tiles at reduced cadence with a clipmap.
4. Use wave occupancy captures to tune quality-specific register pressure and step distribution.
5. Reuse Atmosphere's conservative depth pyramid when its binding contract is published to external modules.

## 5 Future Feature / Research Ideas

1. Publish average cloud transmittance to SkyBounce and WorldProbes through a versioned renderer context.
2. Connect an active future Microclimate provider to weather-map source terms without making it mandatory.
3. Add distant lightning illumination inside storm density and synchronize it with TESWeather thunder events.
4. Add an optional cloud-shadow clipmap for consistent lighting beyond screen space.
5. Evaluate sparse 3D noise bricks generated at install/runtime for richer fine structure without shipping third-party assets.

## Fog, light-volume, and frame-spike follow-up

- The deferred pipeline produces the froxel fog before cloud composition. Clouds now sample the published integrated scattering/transmittance at their representative depth, so the same PIXL fog colour and extinction soften distant cloud forms.
- Skyrim's LightVolumes are generated earlier and already contribute to the scene behind the clouds. The cloud's remaining transmittance preserves shafts in clear gaps and attenuates them under dense cloud regions. This is a compositing connection, not cloud-shadow injection into the shaft raymarch.
- The active runtime log confirms cloud resource setup and resolution but contains no per-pass GPU timings or frame-spike capture. There is no evidence of cloud memory growth or an accumulating allocation path. The bounded high-cost work is the per-frame raymarch/composite plus the 512x512 weather dispatch; weather dispatch is capped at its existing 0.20-second cadence, including camera recentering. This makes clouds a plausible periodic GPU-spike contributor, but does not prove they caused the reported large spikes.
- `EnsureFrameResources` recreates six raymarch/history textures when dynamic render dimensions or quality change. The inspected log records one 1280x720-to-640x360 transition at quality 2; that can cause an isolated allocation hitch if the upscaler changes internal resolution, but the log does not show repeated resizing.
- PIXL's `VolumetricClouds::Weather`, `Raymarch`, `Temporal`, and `Composite` profiler scopes can isolate costs in a live GPU capture. A frame-time spike comparison with clouds disabled is still required to establish causation.

## 2026-10-11 release polish follow-up

- Added Masser/Secunda phase scattering to the cloud raymarch. Lunar color and direction provide restrained ambient and directional fill gated by sun elevation.
- Synchronized the shipped cloud profile and C++ fallback defaults with the active UserGraphics cloud block; the user's file itself remains untouched.
- FXC compiled all four cloud compute entry points and the PIXL-12C build plus integrated renderer audit passed (45 shipping modules).

