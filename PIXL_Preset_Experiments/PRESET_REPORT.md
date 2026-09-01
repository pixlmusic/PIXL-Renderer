# PIXL Renderer experimental preset report

## Baseline

- Source config: `H:\The Elder Scrolls - Skyrim - Special Edition\Data\SKSE\Plugins\PIXL\Config\UserGraphics.json`
- Identification: `Util::PathHelpers::GetSettingsUserPath()` resolves this exact file; `State::Load` merges it over `RendererDefaults.json` and every active `RenderModule::LoadSettings` consumes its named object. The current beta staging config is byte-identical.
- Format/schema: UTF-8 JSON, root object keyed by core settings and each module's `GetName()`.
- Baseline SHA-256: `CB7EFF2FBCC8BEB0226345E1624DC9BE253B6F267CB2132A61DDBCA54DF1490C`
- Reference copy SHA-256: `CB7EFF2FBCC8BEB0226345E1624DC9BE253B6F267CB2132A61DDBCA54DF1490C`
- Baseline modified: **NO**
- Baseline identity: current owner-tested Medium quality contracts, TAA, frame generation Off.

The shipped `SettingsDefault.json` and `PIXL-Renderer-Live-Tested.json` are byte-identical to each other but not to the current live baseline. They are defaults/package inputs, not the active user state.

### Generated artifacts

| File | Changed settings | SHA-256 |
| --- | ---: | --- |
| PIXL_Baseline_REFERENCE.json | 0 | `CB7EFF2FBCC8BEB0226345E1624DC9BE253B6F267CB2132A61DDBCA54DF1490C` |
| PIXL_Complete.json | 56 | `5E89E1F0F23CD00C82DF963CF96E9751E4529C8D81B7982799CFA682865D94E0` |
| PIXL_Extreme_Fidelity.json | 62 | `64BECA38C7A090BEB5A86659E759CD5DDAC618D7D9003A5DD4D84E875FFABCAF` |
| PIXL_Minimum.json | 101 | `62329F236810CC9AF5E81F6695063172FC57C2EEA98800BFE28A35F0468210B2` |
| PIXL_Linear_Diffuse.json | 86 | `32E926CF328536619A7FB296B9317B504E4E2DE2A1767A4693FDEE7C80A3E4B6` |

To test a profile, close Skyrim, retain the untouched reference in this directory, and copy the chosen profile to the live `UserGraphics.json` path. Each profile is a complete independent configuration, not a partial override. Returning the byte-identical reference restores this captured baseline.

## PIXL Extreme Fidelity

This is the reference/screenshot profile. It applies the source-defined Ultra contracts to Lighting, Materials, Atmosphere, Water, Terrain/Vegetation, Characters and Camera; uses DLSS Native AA (DLAA) with model preset F; retains frame generation Off; expands Actor Surface Effects' bounded nearby-NPC budget; and selects the maximum validated Photo Finish plan (4x, 24 real jittered samples). Experimental voxel reflections remain Off because their instability is not made higher quality merely by enabling them.

Bloom is deliberately reduced and its threshold raised rather than maximized. Strong broad bloom would erase local contrast and contaminate reconstruction. Artistic GI, fog, wetness, snow, vegetation and skin values otherwise remain the owner's accepted baseline.

Estimated relative GPU impact: **extreme** during Photo Finish; **high** during gameplay.

## PIXL Complete

This is the recommended coherent profile. It uses High quality contracts--the point before the last expensive ray/probe/tessellation increments--while keeping every production-ready visual module enabled. DLAA preset F provides a clean native-resolution temporal reference without frame-generation ambiguity. Actor Surface Effects receives a moderate 32-NPC/4000-unit budget. Bloom is tightened to protect image clarity.

Deliberately below maximum: GI rays/cache samples, POM steps, volumetric grid depth, water trace range, geometric-ground tessellation and SSS samples. Each is one tier below Ultra because its final increment has disproportionate cost. Reflection/voxel experiments, bodycam and frame generation are not force-enabled.

Estimated relative GPU impact: **high** versus Minimum, **moderate-to-high** versus the Medium baseline.

## PIXL Minimum

This profile applies the Low workload contract, then safely gates optional expensive shading/simulation branches without disabling renderer infrastructure at boot. Hybrid GI, contact shadows, light volumes, volumetric fog/clouds, enhanced water SSR/caustics, geometric ground response, actor accumulation, enhanced precipitation, WindowLife, enhanced foliage/wind, skin SSS/detail, strand shading and camera finishing effects are disabled through their normal runtime settings. Material Forge and shader replacement infrastructure remain loaded. FSR Ultra Performance supplies the largest practical resolution delta; frame generation remains Off.

Expected differences: flatter/less indirect lighting, no fake interiors or dynamic ground/actor accumulation, simpler water/vegetation/characters/weather, lower volumetric depth and substantially softer reconstruction. This is a scalability floor, not the intended PIXL look.

Estimated relative GPU impact: **large reduction expected**; exact timing requires live profiling.

## PIXL Linear Diffuse

Linear Light Core is a global color/energy conversion system, not a single diffuse checkbox. `Color.hlsli` changes diffuse decode, light color conversion, Lambert normalization, emissive/glow, ambient, fog, sky, water, effects and irradiance conventions across Lighting, Grass, Water and Effect shaders. Material Forge's linear branch also removes the legacy sRGB/Pi compensation used when the module is Off.

That explains why enabling it over an ordinary preset can look much too dark or over-contrasted: the module changes several energy conventions at once while the baseline exposure, ambient probe, GI, AO, bloom and per-domain gamma/multipliers were tuned for the Off path. Material Forge textures are already hardware-decoded/handled separately, so forcing every remaining domain to a textbook 2.2 exponent would double-darken parts of PIXL's mixed Skyrim inputs. The profile therefore uses conservative near-linear exponents, restrained 1.10 direct/local multipliers, 1.35 ambient support, reduced GI/AO, higher highlight protection, a less-negative exposure compensation and tighter bloom. TAA is retained to isolate lighting behavior from DLSS model differences.

No disconnected configuration binding was found. One implementation limitation remains: `isDirLightLinear` is a fixed false member in the current code, so all directional light input is treated as non-linear; this matches the current Skyrim input assumption but is not runtime-detected. Configuration can calibrate it, but cannot prove every modded light/weather source uses the same encoding. This profile is therefore a serious visual reference experiment, not a newly approved default.

Estimated relative GPU impact: **moderate-to-high**, close to the High contract; Linear Light Core itself is low overhead.

## Validation performed

- All five JSON files parse through PowerShell's standards-compliant JSON parser.
- Every generated profile has the same root schema and setting-path set as the live baseline.
- No duplicate JSON member names were emitted.
- Profile-controlled values are within the actual loader/UI ranges documented in source.
- DLSS/FSR/TAA enums, DLSS preset F, Photo Finish limits and actor budgets match loader clamps.
- Frame generation is Off in every profile to keep A/B captures deterministic.
- The baseline and reference hashes are identical; the source baseline hash is rechecked after generation.
- Internal `Magic`, `Version` and padding fields are untouched.
- Key ordering and array shapes match the baseline recursively; all numeric values are finite.

## Disconnected / compatibility-only settings

- Legacy Camera Suite enhanced-DOF values remain serialized but `LoadSettings` forces `enableEnhancedDepthOfField=false`; they are retained for schema compatibility and not used as preset controls.
- Pixel Capture's legacy Photo Lens DOF fields are likewise read/clamped but forced disabled.
- `Horizon Blend`, `Natural Lighting`, `Terrain Field` and `Volume Occlusion` serialize `null` because they are automatic/no-public-setting services.
- Several legacy `Disable at Boot` aliases are round-tripped for migration compatibility. PIXL-managed correctness services are forced enabled by `State::Load`.
- `Menu.*Quality` fields select/describe the coordinated contracts; module JSON contains the actual runtime settings. Both are synchronized in these profiles.

## Compact A/B test plan

Use the same save, resolution, weather/time commands, camera path and reconstruction warm-up. Test in this order: **Baseline -> PIXL Complete -> Extreme Fidelity -> Linear Diffuse -> Minimum**.

1. Sunny exterior: stone, foliage, shadow edges and camera rotation.
2. Overcast exterior: ambient/GI balance, fog, wetness and no radiance pumping.
3. Sunrise/sunset: sky continuity, WindowLife glass, reflections and exposure adaptation.
4. Snow: bright and shaded snow while strafing/rotating; ground deformation and precipitation.
5. Forest: alpha-tested foliage, wind motion, grass specular and temporal shimmer.
6. Water: grazing reflections, shore/caustics and camera motion.
7. Interior: candles/point lights, contact shadows, WindowLife and readable dark materials.
8. Dialogue close-up: skin, eyes, hair, SSS and actor accumulation.
9. Wet weather: rain, runoff, puddles, wet PBR and reconstruction trails.
10. High-motion benchmark fly-through: ghosting, disocclusion, volumetrics and frame pacing.

Allow temporal histories to settle before screenshots. For Linear Diffuse, compare noon exterior, candle interior, snow and skin first; those reveal color-space/energy mistakes fastest. Use Pulse Profiler only for measured comparisons and do not compare Photo Finish cost with normal gameplay.