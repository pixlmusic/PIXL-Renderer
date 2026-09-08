# PIXL Renderer Module Release Review

Working module assessment, assembled from baseline reports and selected source checks on 2026-09-07. The `SHIPPED` and `VERIFIED` labels below are provisional baseline descriptions, not evidence that this pass implemented or independently verified five improvements for each module. Module-specific runtime, visual and performance validation remains outstanding. A successful whole-project C++ build or an older runtime log does not validate a module's image correctness. This document is a review worklist, not a release certificate; see `CURRENT_PASS_STATUS.md` for measured current-pass evidence.

## Actor Surface Effects

Purpose / position: actor-local snow, mud and wetness state is updated on the CPU and consumed during character material shading. Inputs are weather, Ground Response, actor/equipment identity and movement; outputs are bounded actor masks and material weights.

1. Unstable world projection -> actor-local mapping and equipment-bone ownership are `SHIPPED`; this prevents swimming masks with bounded update cost and no save ABI change (LOW).
2. Unbounded NPC work -> fixed actor capacity, distance selection and dirty updates are `SHIPPED`; expected CPU/VRAM savings, graceful fallback beyond capacity (LOW).
3. Snow/mud equivalence -> independent accumulation, melt, drying and appearance controls are `SHIPPED`; better material credibility with unchanged hook contract (LOW).
4. Perspective/equipment churn -> first/third-person and armour-change refresh paths are `SHIPPED`; fewer stale masks, negligible steady-state cost (MEDIUM).
5. Persistent world decals were investigated and remain `FUTURE`; they could enrich trails but add atlas lifetime, save and compatibility risk (HIGH).

Files / verification: `engine/Modules/ActorSurfaceEffects.*`, `pipeline/Actor Surface Effects/**`, character branches in `Lighting.hlsl`, quality/UI/default JSON. ABI assertions, Release build and live log passed; weather/equipment A/B remains human testing.

## AmbientProbe

Purpose / position: supplies diffuse/specular ambient lookup data before material lighting. Inputs are baked probe textures and environment state; outputs are ambient irradiance terms.

1. Missing/corrupt textures -> required asset and package checks are `SHIPPED`; the module fails visibly in audit rather than at a scene edge (LOW).
2. Unsafe lookup direction -> normalized/guarded sampling is `VERIFIED`; no extra samples or binding changes are justified (LOW).
3. Duplicate environment evaluation -> shared ambient terms are `VERIFIED`; retaining one authority avoids redundant ALU (LOW).
4. Higher-order dynamic probes were investigated and remain `FUTURE`; visual gain is uncertain against bandwidth and authoring cost (MEDIUM).
5. Compressed replacements were investigated and rejected for v1.0 because the current tiny fixed assets are stable and quality loss is unprofiled (LOW).

Files / verification: `engine/Modules/AmbientProbe.*`, `pipeline/AmbientProbe/**`; file signatures, include graph, package presence and Release build passed.

## Atmosphere

Purpose / position: evaluates fog and temporal volumetric scattering after scene depth/lighting and before final camera composition. Inputs include depth, shadows, weather, SkyBounce, World Probes and local lights; outputs are fog transmittance/scattering volumes and composite colour.

1. White/flat fog -> height-aware extinction and separately weighted lighting are `SHIPPED`; restores vertical depth without another pass (LOW).
2. Weather discontinuity -> automatic bounded weather profiles are `SHIPPED`; smoother transitions, only constant updates (LOW).
3. Stale history -> projection, worldspace and input-availability rejection are `SHIPPED`; less ghosting at small expected reset cost (MEDIUM).
4. Full-quality cost on all hardware -> real froxel XY/Z and miss-sample quality scaling is `SHIPPED`; lower tiers reduce compute/bandwidth without disabling fog (LOW).
5. A new physically complete sky atmosphere was investigated and remains `FUTURE`; broad sky/weather art changes are inappropriate for release polish (HIGH).

Files / verification: `engine/Modules/Atmosphere.*`, `pipeline/Atmosphere/**`, imagespace volumetric shaders and UI/defaults. Resource recreation paths, shader includes and build passed; fog/weather motion testing remains.

## Camera Suite

Purpose / position: owns exposure, local exposure, bloom, DOF, dialogue focus and Photo/Director finishing after reconstruction-safe scene inputs. Inputs are HDR colour, depth, exposure and camera state; outputs are the final camera image and captures.

1. Exposure toggle residue -> bloom now uses unity exposure when Physical Camera is off (`SHIPPED`); removes a visible jump with no added cost (LOW).
2. Firefly/bloom instability -> soft-knee, Karis filtering and bounded pyramid reconstruction are `SHIPPED`; stable highlights at existing sample count (LOW).
3. DOF depth/subject drift -> depth-aware focus, dialogue ownership and safe camera teardown are `SHIPPED`; better usability, modest enabled-only cost (MEDIUM).
4. Photo capture races -> transactional camera/input ownership and non-recursive robust resolve are `SHIPPED`; correctness over capture-time CPU cost, inactive in gameplay (MEDIUM).
5. Lens ghosts/diffraction were investigated and remain `FUTURE`; a coherent source/occlusion model is required to avoid full-screen haze (MEDIUM).

Files / verification: `engine/Modules/CameraSuite.*`, `engine/Modules/DialogueFocus.h`, `pipeline/Camera Suite/**`, `engine/Modules/PixelCapture.*`. Release build and the September 5 live log show repeated successful capture/teardown.

## Contact Shadows

Purpose / position: screen-space sun/moon and local contact ray marching augments engine shadows during lighting. Inputs are depth, normals and light direction; output is a visibility factor.

1. Contrast fallback forcing maximum -> range and shader fallback are corrected (`SHIPPED`); predictable shadow density, no cost change (LOW).
2. Invalid depth steps -> guarded thickness/depth comparisons are `SHIPPED`; fewer detached shadows and NaNs (LOW).
3. Fixed workload -> sample count scales by quality (`SHIPPED`); lower tiers reduce texture/ALU cost (LOW).
4. Edge instability -> bilinear threshold and bounded smoothing are `SHIPPED`; reduced shimmer for a small filter cost (MEDIUM).
5. Temporal contact-shadow reuse remains `FUTURE`; it could lower rays but requires motion/disocclusion data and ghosting validation (HIGH).

Files / verification: `engine/Modules/ContactShadows.*`, `pipeline/Contact Shadows/**`; CPU/HLSL settings, cache invalidation and Release build passed.

## Distance Blend

Purpose / position: reconciles terrain/object/snow LOD colour and tone in distant lighting. Inputs are LOD material colours and settings; output is matched distant shading.

1. Unsafe integer/bool UI alias -> typed control is `SHIPPED`; removes UB without visual cost (LOW).
2. Developer jargon -> player-facing brightness/tone labels are `SHIPPED`; configuration is clearer (LOW).
3. Invalid ranges -> all values clamp on UI/load (`SHIPPED`); prevents destructive distant output (LOW).
4. Extra pass proposal -> existing in-shader blend is `VERIFIED`; a pass would add bandwidth for no quality case (LOW).
5. Automatic histogram matching remains `FUTURE`; it risks weather-driven pumping and needs temporal design (MEDIUM).

Files / verification: `engine/Modules/DistanceBlend.*` and distant/LOD shader consumers; JSON round-trip and build passed.

## Foliage Dynamics

Purpose / position: supplies foliage material response and world-space hierarchical wind in grass/tree vertex and lighting paths. Inputs are weather, world position, object/card data and material textures; outputs are current/previous positions and stable foliage shading.

1. Camera-relative/synchronized motion -> world-space phase, gust and per-object variation are `SHIPPED`; coherent wind with matching temporal positions (MEDIUM).
2. Trees bending like grass -> stiffness/attachment and frequency separation are `SHIPPED`; more believable hierarchy at similar ALU cost (MEDIUM).
3. Complex Grass OOB reads -> packed-texture UV safety is `SHIPPED`; correctness with no extra sample (LOW).
4. Specular flicker -> normal strength, mirror handling and specular AA controls are `SHIPPED`; steadier highlights for small ALU cost (LOW).
5. A persistent 3D force field remains `FUTURE`; source-only `WorldWind` establishes a future contract without adding runtime volume cost (HIGH).

Files / verification: `engine/Modules/FoliageDynamics.*`, `pipeline/Foliage Dynamics/**`, `RunGrass.hlsl`, tree/lighting consumers. Source/live shaders match; build passed; diverse grass/tree mods need live A/B.

## Ground Response

Purpose / position: updates snow/mud deformation and material-aware ground geometry before terrain shading. Inputs are weather, actors/objects/spells, terrain classification and history; outputs are deformation/normal fields, tessellation displacement and mobility response.

1. Snow/mud shared behavior -> independent hardness, depth, recovery, wetness and compaction are `SHIPPED`; better physical distinction (MEDIUM).
2. Imprint aliasing -> bounded contact falloff, accumulated deformation and normal reconstruction are `SHIPPED`; stable edges at existing field resolution (MEDIUM).
3. Excess distant tessellation -> distance/fade and near/far quality tiers are `SHIPPED`; expected GPU savings without disabling geometry (LOW).
4. Unsuitable contributors -> actor/object/material exclusions and bounded scan cadence are `SHIPPED`; lower CPU work and fewer false imprints (MEDIUM).
5. Season-aware material signals are `SHIPPED`; hard-coded permanent snow assumptions were rejected, preserving seasonal-mod compatibility (LOW).

Files / verification: `engine/Modules/GroundResponse.*`, `pipeline/Ground Response/**`, `SeasonIntegration.*`, terrain/lighting consumers. Build and ABI checks pass; snow/mud traversal and spell tests remain.

## Hair Reconstruction (retired source-only ABI record)

Purpose / position: historical experimental card reconstruction retained only to document/reserve a 128-byte shared-buffer block. It is absent from the runtime module list and package descriptor catalog; Strand Shading is authoritative.

1. Cross-family ABI shift -> exact 128-byte reservation preserves later offsets (`SHIPPED` compatibility record, LOW).
2. Accidental runtime activation -> descriptor is marked `Pipeline = Retired` and excluded from generated module metadata (`SHIPPED` in this pass, LOW).
3. Guarded include omission -> dormant include is packaged without a descriptor (`SHIPPED` in this pass); namespace completeness with no active GPU cost (LOW).
4. Re-enabling heuristic classification was rejected after documented black/lavender cache failures (`VERIFIED`); avoids major compatibility risk (HIGH if revisited).
5. A future redesign requires isolated cache/ABI validation and is `FUTURE`; no v1.0 visual claim is made (HIGH).

Files / verification: `engine/Modules/HairReconstruction.*`, `pipeline/Hair Reconstruction/**`, reserved `PipelineBuffer`/`SharedData` fields. Generated tables contain no Hair entry; RC has the include but no INI; package audit passes.

## HorizonBlend

Purpose / position: automatic correctness adapter for horizon and distant geometry blending. Inputs are view/depth/LOD state; output is a continuous horizon transition.

1. Visible terrain seam reduction is `SHIPPED`; no user tuning is required (LOW).
2. Camera-altitude behavior is `VERIFIED`; current engine-space path avoids camera-relative drift (LOW).
3. Always-on UI control was rejected; it would expose a correctness service as style (`VERIFIED`, LOW).
4. Extra full-screen resolve was rejected because the current hook/in-shader path avoids bandwidth (`VERIFIED`, LOW).
5. Per-worldspace tuning remains `FUTURE` only if live evidence shows incompatible distant assets (MEDIUM).

Files / verification: `engine/Modules/HorizonBlend.*`, shared entry shaders; hook registration and Release build passed.

## Hybrid GI

Purpose / position: half/full-resolution screen-space indirect lighting, directional visibility, temporal reuse, upsample and reflection fallback after G-buffer/depth generation. Inputs are depth/normals/materials/history/probes; outputs are diffuse GI, AO/visibility and reflections.

1. Encoded-vector filtering -> octahedral directions are decoded before accumulation (`SHIPPED`); removes directional bias with small ALU cost (MEDIUM).
2. Weak history rejection -> geometry/angular confidence and disocclusion response are `SHIPPED`; less leaking/ghosting (MEDIUM).
3. Edge-bleeding upsample -> depth/normal-aware reconstruction is `SHIPPED`; stable thin geometry for existing taps (MEDIUM).
4. Fixed cost -> real resolution, step/slice and reflection budgets scale by quality (`SHIPPED`); substantial expected 3060 Ti scalability (LOW).
5. Architectural replacement with ray tracing/probe cascades remains `FUTURE`; DX11 compatibility and validation risk are too high (HIGH).

Files / verification: `engine/Modules/HybridGI.*`, `pipeline/Hybrid GI/**`, deferred consumers. Resource formats/bindings unchanged; Release build and include audit passed; timed captures remain required.

## ImageReconstruction

Purpose / position: prepares depth, motion, exposure, reactive/transparency guides and dispatches DLSS/DLAA, FSR, RCAS, optional neural rendering and frame generation immediately before presentation/camera finishing as required. Outputs reconstructed colour and presentation metadata.

1. Guide contract ambiguity -> explicit encoded textures and final input audit are `SHIPPED`; stable backend inputs (HIGH-risk area, validated conservatively).
2. Backend failure -> capability gates and graceful fallback are `SHIPPED`; unsupported hardware fails closed (MEDIUM).
3. Sharpening instability -> RCAS strength is bounded and reconstruction-aware (`SHIPPED`); avoids needless shimmer (LOW).
4. NR/FG provisioning -> isolated DX12 sidecar retains the DX11 renderer and stable ordering (`SHIPPED`); optional cost only (HIGH).
5. Exclusive-fullscreen sidecar support was rejected after a repeatable DXGI Alt-Tab crash (`VERIFIED`); windowed/borderless remains the safe contract (HIGH).

Files / verification: `engine/Modules/ImageReconstruction/**`, `pipeline/ImageReconstruction/**`, shared frame annotations and UI/defaults. Build, package dependencies and latest live log pass; DLSS modes/FG/NR need final hardware matrix.

## Interior Daylight

Purpose / position: corrects lighting/shadow behavior for interiors that expose exterior sky. Inputs are cell/interior classification and shadow state; outputs are compatible double-sided/shadow-distance behavior.

1. Sky-interior detection is `SHIPPED`; ordinary interiors retain vanilla assumptions (MEDIUM).
2. Double-sided option is `SHIPPED`; fixes authored open geometry while remaining user-controlled (MEDIUM).
3. Shadow distance bounds are `SHIPPED`; avoids runaway interior cost (LOW).
4. Per-cell hard-coded lists were rejected in favor of runtime signals (`VERIFIED`); better mod/worldspace compatibility (LOW).
5. Portal-aware daylight volumes remain `FUTURE`; engine geometry/access risk is high (HIGH).

Files / verification: `engine/Modules/InteriorDaylight.*`; settings/UI/hook paths and build passed; modded interiors require live checks.

## Light Volumes

Purpose / position: controls froxel dimensions and local/directional volumetric-light contribution. Inputs are scene classification and lights; outputs feed Atmosphere volumetrics.

1. Separate interior/exterior enable is `SHIPPED`; prevents unsuitable work (LOW).
2. Real quality dimensions are `SHIPPED`; tiers change dispatch/memory rather than labels (LOW).
3. Custom dimensions clamp and recreate resources safely (`SHIPPED`, MEDIUM).
4. Duplicate atmosphere volume proposal was rejected; shared ownership avoids VRAM/bandwidth (`VERIFIED`, LOW).
5. Visibility/indirect dispatch scheduling remains `FUTURE` pending profiling (MEDIUM).

Files / verification: `engine/Modules/LightVolumes.*` and volumetric consumers; resize/resource recreation and build passed.

## Linear Light Core

Purpose / position: centralizes linear-space light/color conversion and baseline light behavior before material BRDF evaluation. Inputs are Skyrim light state; outputs are physically coherent linear radiance.

1. Gamma-space lighting paths are normalized into linear evaluation (`SHIPPED`, MEDIUM).
2. Shared constants prevent divergent module conversions (`SHIPPED`, LOW).
3. HDR clamping was reviewed and kept conservative (`VERIFIED`); valid highlights are not destroyed (LOW).
4. Full textbook attenuation replacement was rejected because Skyrim art assumes legacy reach (`VERIFIED`, MEDIUM).
5. Calibrated photometric authoring remains `FUTURE` and needs content-side metadata (HIGH).

Files / verification: `engine/Modules/LinearLightCore.*`, `Common/Lighting*.hlsli`, `Lighting.hlsl`; colour-space paths and build passed.

## Material Layers

Purpose / position: provides complex materials, height blending, POM/Auto-POM, detail reconstruction and material shadowing during terrain/object shading. Inputs are authored textures/derivatives/view vector; outputs are displaced UVs, normals and material parameters.

1. Grazing instability -> adaptive steps, bounded texel shift and fade are `SHIPPED`; less shimmer and wasted work (MEDIUM).
2. Ray-march quality -> step tiers plus binary refinement are `SHIPPED`; better accuracy per sample (LOW).
3. Unsafe UV/mips -> derivative-aware sampling and bounds are `SHIPPED`; fewer invalid reads (LOW).
4. Microdetail shimmer -> distance/mip/anti-shimmer controls are `SHIPPED`; stable detail at modest optional sample cost (MEDIUM).
5. Global maximum samples were rejected; quality tiers preserve 3060 Ti scalability (`VERIFIED`, LOW).

Files / verification: `engine/Modules/MaterialLayers.*`, `pipeline/Material Layers/**`, `Common/PBR*`, `Lighting.hlsl`; shader include graph, UI wiring and build passed.

## MaterialForge

Purpose / position: classifies PBR/legacy materials and supplies metallic, roughness, AO, falloff and local-contact behavior before lighting. Inputs are shader/material/texture metadata; outputs are normalized physical material parameters and shader flags.

1. Authored-vs-inferred precedence is `SHIPPED`; authored conductor data is never overwritten (MEDIUM).
2. Legacy highlights -> bounded inference/maximums are `SHIPPED`; improved definition without plastic conversion (MEDIUM).
3. Local-light falloff -> continuous compatibility/physical blend is `SHIPPED`; preserves Skyrim reach with user control (MEDIUM).
4. Specular aliasing -> variance-based AA and GGX multiscatter controls are `SHIPPED`; steadier highlights for small ALU cost (LOW).
5. Broad automatic fur roughness reorder remains `FUTURE`; it requires targeted Lighting cache rebuild and visual A/B (MEDIUM).

Files / verification: `engine/MaterialForge*`, `engine/MaterialForge/**`, common physical-material shaders and Lighting. Registry diagnostics, ABI size and build passed.

## Natural Lighting

Purpose / position: managed correctness service for direct-light attenuation and ambient balance during material lighting. Inputs are engine lights and material response; outputs are naturalized direct/ambient terms.

1. Managed-service enable migration is `SHIPPED`; users cannot accidentally dismantle core lighting (LOW).
2. Attenuation balance is `SHIPPED`; physically motivated blend preserves authored Skyrim scale (MEDIUM).
3. Duplicate MaterialForge calculations are shared (`VERIFIED`); no second path added (LOW).
4. Aggressive photometric conversion was rejected as art-incompatible (`VERIFIED`, MEDIUM).
5. Per-light metadata remains `FUTURE`; requires authoring or robust runtime classification (HIGH).

Files / verification: `engine/Modules/NaturalLighting.*`, `NaturalLighting/Common.h`; module rules and build passed.

## Pixel Capture

Purpose / position: reads back and encodes screenshots/Photo Finish output outside normal gameplay work. Inputs are final colour and capture settings; outputs are local image files.

1. Concurrent capture prevention is `SHIPPED`; avoids lifetime races (MEDIUM).
2. Transactional state restore is `SHIPPED`; camera/input/reconstruction settings survive failures (MEDIUM).
3. Robust multi-frame resolve is `SHIPPED`; rejects isolated outliers at explicit capture-only CPU/memory cost (MEDIUM).
4. Output path handling is local and bounded (`VERIFIED` security); no upload/telemetry path exists (LOW).
5. GPU resolve remains `FUTURE`; it could reduce CPU traffic but requires a new validated compute path (MEDIUM).

Files / verification: `engine/Modules/PixelCapture.*`, Camera Suite capture integration; repeated September 5 live transactions completed and saved successfully.

## Pulse Profiler

Purpose / position: optional diagnostics collects CPU/GPU timing and presents it in Developer UI. Inputs are timestamp/query scopes; output is an on-screen/benchmark record.

1. Default-off diagnostics are `SHIPPED`; no normal-frame cost when disabled (LOW).
2. Bounded history avoids unbounded allocation (`SHIPPED`, LOW).
3. Developer gating prevents user-facing clutter (`SHIPPED`, LOW).
4. Per-frame verbose logging was rejected (`VERIFIED`); avoids I/O/frame pacing harm (LOW).
5. Exported reproducible capture sessions remain `FUTURE`; no timing claim is fabricated (LOW).

Files / verification: `engine/Modules/PulseProfiler/**`, `engine/Profiler.*`, Pulse UI; build and default checks passed.

## Radiant Grid

Purpose / position: gathers effect/particle emitters, builds clustered light data and contributes local radiance to scene/volumetrics. Inputs are particle submissions and camera clusters; outputs are light buffers/grid lists.

1. Duplicate emitter submissions -> owner aggregation is `SHIPPED`; lower cluster pressure without dimming unique sources (LOW).
2. Non-finite payloads are rejected (`SHIPPED`); prevents buffer contamination (LOW).
3. Capacity/culling is bounded and user-controllable (`SHIPPED`); stable VRAM/work (LOW).
4. Queue drains safely while disabled (`SHIPPED`); avoids stale bursts/re-enable spikes (LOW).
5. Finite-area LTC emitters remain `FUTURE`; broad BRDF/buffer changes are not release-safe (HIGH).

Files / verification: `engine/Modules/RadiantGrid.*`, `pipeline/Radiant Grid/**`; buffer contracts and build passed; particle-heavy scenes need timing.

## Rain Response

Purpose / position: derives wet surfaces, world precipitation, splashes and bounded roof runoff from weather/depth. Inputs are weather, world position, depth/normals and material state; outputs are wetness and precipitation/runoff composites.

1. Camera-relative precipitation -> world-space generation is `SHIPPED`; no rotation with view (MEDIUM).
2. Uniform streaks -> gust/variation and snow/rain separation are `SHIPPED`; more natural motion (MEDIUM).
3. Runoff overreach -> bounded detection/generation/resolve stages are `SHIPPED`; preserves coverage without large trace distance (MEDIUM).
4. Wetness invalid ranges -> min/max/load clamps are `SHIPPED`; stable material state (LOW).
5. Full roof drainage simulation remains `FUTURE`; geometry tracing and persistence cost are too high (HIGH).

Files / verification: `engine/Modules/RainResponse.*`, `pipeline/Rain Response/**`, Effect/Lighting consumers; build and world-space source review passed.

## SkinOptics

Purpose / position: adjusts skin micro-relief, F0, wetness/fuzz and eye-adjacent character response before/within lighting. Inputs are skin material, normals, weather and lights; outputs are stable skin lobes.

1. Synthetic smoothness -> bounded micro-relief/detail is `SHIPPED`; retains asset character (MEDIUM).
2. Dielectric F0 limits are `SHIPPED`; reduces plastic skin without suppressing wet highlights (LOW).
3. Wetness coupling is `SHIPPED`; environmental response reuses Rain Response authority (LOW).
4. Dialogue readability is integrated without globally boosting characters (`SHIPPED`, MEDIUM).
5. Per-character optical calibration remains `FUTURE`; asset/mod variability makes auto inference risky (MEDIUM).

Files / verification: `engine/Modules/SkinOptics.*`, `pipeline/SkinOptics/**`, Eye/Lighting; build passed, dialogue/wet skin A/B remains.

## Sky Continuity

Purpose / position: synchronizes sun/moon directions, phase and below-horizon fades for shadows and volumetrics. Inputs are sky/calendar/moon state; outputs are authoritative directional-light state.

1. Sun/lighting mismatch correction is `SHIPPED`; stronger spatial credibility (MEDIUM).
2. Smooth source transition is `SHIPPED`; avoids dawn/dusk popping (MEDIUM).
3. Moon phase/source controls are `SHIPPED`; consistent night lighting (LOW).
4. Altitude/horizon correction is `SHIPPED`; prevents apparent direction drift (LOW).
5. Astronomical replacement was rejected; Skyrim world/art assumptions take precedence (`VERIFIED`, HIGH).

Files / verification: `engine/Modules/SkyContinuity.*`; settings consumers and build passed; dawn/dusk/night weather testing remains.

## Sky Veil

Purpose / position: adds cloud body/haze, cloud shadows, phase response and sky integration. Inputs are sky/weather/time and cubemap data; outputs are clouds, haze and moving landscape shadow modulation.

1. World-space cloud movement is `SHIPPED`; stable under camera motion (MEDIUM).
2. Self-shadow/silver-lining bounds are `SHIPPED`; avoids blown white clouds (MEDIUM).
3. Horizon fade/ambient integration is `SHIPPED`; preserves depth and weather identity (LOW).
4. Cloud artistic settings remain user-owned rather than overwritten by quality (`SHIPPED`, LOW).
5. Volumetric weather simulation remains `FUTURE`; it would duplicate atmosphere volumes and exceed release risk (HIGH).

Files / verification: `engine/Modules/SkyVeil.*`, `pipeline/Sky Veil/**`, Sky shader; build and setting trace passed.

## SkyBounce

Purpose / position: captures and filters local sky irradiance for diffuse/specular ambient lighting. Inputs are local sky/environment visibility; outputs are probe-volume irradiance terms.

1. Minimum diffuse/specular visibility controls are `SHIPPED`; reduces leaks (LOW).
2. Zenith/contribution angle clamps and rebuild requests are `SHIPPED`; correct updates without stale probes (LOW).
3. Existing temporal amortization is `VERIFIED`; avoids per-frame full-volume updates (LOW).
4. Giant memory expansion was rejected; negligible visual gain would waste VRAM (`VERIFIED`, LOW).
5. Cascaded/toroidal probes with relocation/classification remain `FUTURE`; valuable but architectural (HIGH).

Files / verification: `engine/Modules/SkyBounce.*`, `pipeline/SkyBounce/**`; resource/rebuild paths and build passed.

## Strand Shading

Purpose / position: authoritative Marschner-inspired hair BRDF in Lighting hair permutations. Inputs are tangent/normal, base colour and lights; outputs are anisotropic R/TT/TRT plus diffuse scatter.

1. Safe tangent fallback/normalization is `SHIPPED`; avoids NaNs on weak assets (LOW).
2. Bounded base-colour sqrt/log inputs are `SHIPPED`; robust dark hair (LOW).
3. Lobe finite checks are `SHIPPED`; corrupt inputs cannot poison HDR output (LOW).
4. Self-shadow/sample quality scales with character tier (`SHIPPED`); expected cost control (LOW).
5. Groom/topology reconstruction is rejected for v1.0; card compatibility is authoritative (`VERIFIED`, HIGH).

Files / verification: `engine/Modules/StrandShading.*`, `pipeline/Strand Shading/**`, Lighting hair path; representative compile/build and source guards passed.

## Terrain Detail

Purpose / position: reduces distant terrain repetition within terrain shading. Inputs are terrain UV/material weights; output is varied, LOD-compatible sampling.

1. Distant tiling repair is `SHIPPED`; visible repetition falls without a new pass (LOW).
2. Weight normalization guards are `SHIPPED`; prevents zero-sum artifacts (LOW).
3. Material Layers compatibility is `VERIFIED`; no duplicate parallax path (LOW).
4. Always-maximum stochastic sampling was rejected to protect bandwidth (`VERIFIED`, LOW).
5. Blue-noise temporal rotation remains `FUTURE`; motion shimmer must be proven lower (MEDIUM).

Files / verification: `engine/Modules/TerrainDetail.*`, `pipeline/Terrain Detail/**`; shader guards and build passed.

## Terrain Field

Purpose / position: automatic terrain data/classification service and native plugin fallback used by terrain/ground modules. Inputs are runtime texture/material state; outputs are authoritative terrain signals/resources.

1. Runtime material derivation is `SHIPPED`; supports seasonal swaps (MEDIUM).
2. Bundled ESL TXST fallback is machine-validated (`SHIPPED`, LOW).
3. Managed-service enable migration is `SHIPPED`; prevents accidental pipeline dismantling (LOW).
4. Exact asset-name hardcoding was rejected where runtime classification exists (`VERIFIED`, LOW).
5. Streaming clipmap classification remains `FUTURE` pending worldspace/VRAM profiling (HIGH).

Files / verification: `engine/Modules/TerrainField.*`, bundled `PIXL-TerrainField.esp`, `SeasonIntegration.*`; TES4/ESL/form audit and build passed.

## Terrain Occlusion

Purpose / position: produces heightmap-driven terrain shadowing for lighting. Inputs are worldspace heightmaps and sun direction; output is terrain visibility.

1. Sun-change dirty updates are `SHIPPED`; avoids redundant work (LOW).
2. Resource absence fallback is `SHIPPED`; optional worldspaces continue rendering (LOW).
3. Master toggle has a real capture/shader consumer (`VERIFIED`, LOW).
4. Fixed resolution remains because unprofiled scaling could harm memory/quality (`VERIFIED`, LOW).
5. Clipmapped multi-worldspace height data remains `FUTURE` (HIGH).

Files / verification: `engine/Modules/TerrainOcclusion.*`, `pipeline/Terrain Occlusion/**`, heightmap assets; package and build passed.

## Terrain Seam

Purpose / position: blends terrain/object contacts using captured depth and terrain response. Inputs are scene/terrain depth; output is contact blending.

1. Depth-aware seam blend is `SHIPPED`; removes hard intersections (MEDIUM).
2. Managed-service enable migration is `SHIPPED`; correctness stays active (LOW).
3. Fixed bounded capture distance avoids unbounded cost (`VERIFIED`, LOW).
4. Extra full-resolution history was rejected; temporal state is unnecessary for the stable depth signal (`VERIFIED`, LOW).
5. User-scalable capture distance remains `FUTURE` after profiling (MEDIUM).

Files / verification: `engine/Modules/TerrainSeam.*`, `pipeline/Terrain Seam/**`; pass hooks/bindings and build passed.

## Thin Surface

Purpose / position: applies directional transmission/rim/fabric response to eligible lighting materials. Inputs are material flags, light/view directions and alpha; output is thin-surface transmission.

1. Clear opacity/edge/effect labels and clamps are `SHIPPED`; fewer invalid states (LOW).
2. Skinned-only/material overrides are `SHIPPED`; compatibility is opt-in/bounded (MEDIUM).
3. Lighting-only shader define scoping is `SHIPPED`; avoids unrelated cache invalidation (LOW).
4. Broad automatic glass classification was rejected; WindowLife owns architectural glass (`VERIFIED`, MEDIUM).
5. Acrylic/multi-layer models remain `FUTURE` (MEDIUM).

Files / verification: `engine/Modules/ThinSurface.*`, `pipeline/Thin Surface/**`, Lighting; UI/serialization and build passed.

## Tissue Diffusion

Purpose / position: separable, profile-aware skin/subsurface diffusion in character lighting. Inputs are skin colour/depth/normals and profile settings; outputs are diffused tissue lighting.

1. Lighting-only define/invalidation scope is `SHIPPED`; avoids recompiling unrelated families (LOW).
2. Burley sample tiers are `SHIPPED`; character quality scales real work (LOW).
3. Edge/depth awareness is `SHIPPED`; reduces background bleed (MEDIUM).
4. Profile controls remain bounded to prevent waxy skin (`SHIPPED`, MEDIUM).
5. Screen-space multi-layer random walk remains `FUTURE`; cost/ghosting risk is high (HIGH).

Files / verification: `engine/Modules/TissueDiffusion.*`, `pipeline/Tissue Diffusion/**`; kernel rebuild, cache scope and build passed.

## Volume Occlusion

Purpose / position: downsamples and filters shadow maps for transparent effects/decals/volumetrics. Inputs are cascaded shadow maps; outputs are compact blurred VSM resources.

1. Fixed compact resolutions are `SHIPPED`; bounded VRAM/bandwidth (LOW).
2. Separable Gaussian filtering is `SHIPPED`; quality per sample (LOW).
3. Shared reserved shadow slot is documented/verified (`SHIPPED`, MEDIUM).
4. Debug inspection is Developer-only (`SHIPPED`); no normal UI/resource overhead (LOW).
5. Adaptive resolution/softness controls remain `FUTURE` pending measurements (MEDIUM).

Files / verification: `engine/Modules/VolumeOcclusion.*`, `pipeline/Volume Occlusion/**`; register search, resource lifetime and build passed.

## Water Optics

Purpose / position: augments water/underwater shading with multi-scale normals, SSR, caustics, foam, absorption/refraction and atmospheric coupling. Inputs are water depth, flow, waves, scene colour/depth, weather and masks; outputs are final water colour/reflection/foam.

1. Repetitive waves -> directional multi-scale parallax and stable normal composition are `SHIPPED`; richer motion without a new pass (MEDIUM).
2. Screen/player-projected foam -> world-space flow/contact foam stencil is `SHIPPED`; removes tiling/projection artifacts (MEDIUM).
3. SSR fixed cost -> step/range/edge quality scaling and history reset are `SHIPPED`; expected 3060 Ti savings (LOW).
4. Receiver caustics -> bounded Jacobian focusing, dispersion and transmittance are `SHIPPED`; physically richer light with no new textures (MEDIUM).
5. FFT ocean replacement remains `FUTURE`; Skyrim rivers/lakes and DX11 cost make it unsuitable for release (HIGH).

Files / verification: `engine/Modules/WaterOptics.*`, `pipeline/Water Optics/**`, `Water.hlsl`, Waterbody/flow resources. Live/source shaders match and build/package audit passed; shoreline/underwater A/B remains.

## Waterbody

Purpose / position: owns water LOD mesh replacement, flowmap/cache/worldspace resources before Water shading. Inputs are worldspace/cell water data and flow assets; outputs are coherent meshes, height/flow lookup data.

1. Distant/near LOD mismatch correction is `SHIPPED`; consistent surface response (MEDIUM).
2. Optimized distant meshes are `SHIPPED`; expected draw/vertex savings (MEDIUM).
3. Flowmap tile/worldspace coherence is `SHIPPED`; avoids cell seams (MEDIUM).
4. Cache invalidation on worldspace change is `SHIPPED`; no stale water fields (MEDIUM).
5. Fully streamed vector-field mips remain `FUTURE`; needs memory/tile profiling (HIGH).

Files / verification: `engine/Modules/Waterbody/**`, Water assets and `Water.hlsl`; package assets, lifetime paths and build passed.

## WindowLife

Purpose / position: classifies architectural panes and applies old-glass optics, exact masks and fake-interior/occupant parallax in Lighting. Inputs are geometry/material/UV/view data and authored atlases; outputs are glass response and interior layers.

1. Resource/register contract is `SHIPPED` and documented; high texture slots do not collide (MEDIUM).
2. Oblique/world-aperture projection and exact pane masks are `SHIPPED`; stable framing/clipping (MEDIUM).
3. DDS atlases and duplicate-room repair are `SHIPPED`; quality and variety at bounded samples/VRAM (MEDIUM).
4. Auto-POM is selectively suppressed only on glass while frames retain relief (`SHIPPED`, MEDIUM).
5. Real room rendering/portal capture remains `FUTURE`; resource, hook and mod-compatibility risk is high (HIGH).

Files / verification: `engine/Modules/WindowLife.*`, `pipeline/WindowLife/**`, Lighting/material hooks. Register search, asset/package hashes and build passed; diverse architecture/night tests remain.

## World Probes

Purpose / position: captures cubemaps and builds diffuse/specular environment data used by materials, water and GI fallbacks. Inputs are scene capture and camera/worldspace; outputs are cubemap, inferred faces, irradiance and optional BC6H data.

1. Stale worldspace/camera data handling is `SHIPPED`; probe resets/repositions safely (MEDIUM).
2. Specular importance sampling and roughness mip filtering are `SHIPPED`; credible reflections (MEDIUM).
3. Pixel-stage cache invalidation scope is `SHIPPED`; avoids unrelated shader rebuilds (LOW).
4. Creator/debug controls are Developer-only (`SHIPPED`); no accidental authoring cost (LOW).
5. Cascaded/local visibility probes remain `FUTURE`; memory/update scheduling must be proven (HIGH).

Files / verification: `engine/Modules/WorldProbes.*`, `pipeline/World Probes/**`; resource formats, BC6H path, package and build passed.

## Build, Runtime Integration, UI and Packaging

Purpose / position: SKSE entry, hooks, D3D11 state, shared buffers, compiler/cache, quality/UI, packaging and deployment connect every module to Skyrim and presentation.

1. Module/settings controls are traced UI -> JSON -> runtime -> constant/resource/shader in `docs/PIXL_GUI_Audit/GUI_CONTROL_AUDIT.md` (`SHIPPED`, LOW).
2. Shader cache invalidation is module/family/stage aware and shared ABI tagged (`SHIPPED`, HIGH-risk area conservatively validated).
3. Optional modules fail closed with log entries; global renderer hooks remain unchanged (`SHIPPED`, MEDIUM).
4. Retired descriptors are now excluded from generated shipping metadata and package sets are exact (`SHIPPED` in this pass, LOW).
5. Package manifests hash every payload file; the RC is compile-on-device and excludes caches/PDBs/archives (`SHIPPED`, LOW).

Files / verification: root CMake/presets/batches, `engine/State.*`, `Hooks.*`, `Deferred.*`, `PipelineBuffer.*`, `ShaderCache.*`, `ShaderCompiler.*`, menu/runtime utilities and release PowerShell tools. Full Release/LTCG build, 37-module audit, archive integrity and manifest/hash checks passed.

## Future lists

The five visual, performance and feature/research directions requested for future modular development are consolidated by subsystem in `FUTURE_RENDERING_ROADMAP.md`. High-risk items above were deliberately not implemented during release polish.
