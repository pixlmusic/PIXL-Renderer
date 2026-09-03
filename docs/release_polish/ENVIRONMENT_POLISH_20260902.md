# PIXL Environment Polish — 2026-09-02

## Outcome

This controlled release-polish pass fixes Complex Grass texture safety and directional screen-space shadowing, adds an automatic weather-driven atmosphere profile, and introduces world-space dynamic water foam and player wakes. The implementation preserves the existing DX11 renderer and reuses PIXL's current depth, flowmap, weather, shadow, and temporal infrastructure.

## Foliage / Grass

### Architecture and current behaviour

`RunGrass.hlsl` consumes Skyrim's grass card geometry, the optional vertically packed Complex Grass texture layout, Contact Shadows, SkyBounce, directional light state, and Foliage Dynamics settings. `FoliageDynamics.cpp/.h` owns the serialized mode and GUI.

### Changes

- Modes 2 and 3 now override only the packed normal's Y convention after safe multi-sample layout detection. Ordinary full-height diffuse textures can no longer be cropped or interpreted as normals.
- Directional screen-space visibility is evaluated on both faces of thin grass. Backlit/transmitted blades no longer bypass sun contact shadows because their diffuse `N.L` is negative.
- SkyBounce visibility softly multiplies the near-field shadow rather than replacing it with `max()`, preserving screen-space sun shadows while retaining a safe unconverged-probe floor.
- GUI labels and help now describe the safe semantics.

### Fidelity, performance, security, stability

The change adds no resources, registers, passes, allocations, filesystem access, or external communication. Contact-shadow lookup coverage increases for back-facing grass pixels, an expected small pixel cost. Strict FXC passes fallback, depth, and the complete Foliage/SkyBounce/ContactShadows permutation.

### Rejected changes

Global forced-up normals and unconditional packed-layout forcing were rejected because both destroy authored vegetation structure and mod compatibility.

### 5 Future Visual Improvements

1. Per-species leaf/card rigidity inference.
2. Directional penumbra width from sun angular size.
3. Alpha-coverage-aware shadow receiver filtering.
4. Distant grass shadow energy matching against terrain LOD.
5. Species-aware transmission colour.

### 5 Future Performance Improvements

1. Reuse one contact-shadow sample across diffuse/specular/transmission.
2. Distance-rate the complex-layout probes through material metadata caching.
3. Reduce far grass BRDF permutations after measured A/B testing.
4. Wave-level sharing for card material terms.
5. Profile half-resolution directional visibility for far grass.

### 5 Future Feature / Research Ideas

1. Material classifier for grass versus broadleaf ground cover.
2. Shared world-force-field coupling.
3. Wind-aware temporal confidence.
4. Snow-weighted blade stiffness.
5. Flowing-water shoreline vegetation response.

## Atmosphere

### Architecture and current behaviour

`Atmosphere.cpp/.h` owns analytical exponential-height fog plus froxel material, light-scattering, integration, depth-aware reconstruction, history rejection, directional shadow, SkyBounce, AmbientProbe, and RadiantGrid integration. Skyrim's `Sky::fogNear`, `fogFar`, transition percentage and weather flags are already the authoritative live weather state.

### Changes

- `Automatic Weather Atmosphere` is enabled by default and resolves a runtime copy of settings; stored user values remain the artistic baseline.
- Live blended Skyrim visibility drives conservative density, start range, froxel range, and near fade.
- Henyey–Greenstein/Mie anisotropy transitions between clear, cloudy, rain, and snow profiles without popping between outgoing and incoming weather.
- Skyrim fog colour is retained as the art-direction bridge into PIXL scattering.
- `Minimum World Visibility` bounds analytical and volumetric opacity so sky, mountains, and navigation silhouettes cannot become a featureless wall.
- CPU and HLSL Atmosphere settings are fixed at 272 bytes with a compile-time assertion.

### Fidelity, performance, security, stability

No new render target, dispatch, sample, or history resource was added. Automatic resolution is scalar CPU work once per shared-data update. Weather changes remain temporally filtered by the existing volume history and transition state. Strict FXC passes all four Atmosphere compute kernels plus Water/Grass consumers.

### Rejected changes

A replacement atmosphere, camera-relative fog height, unrelated cloud-shadow texture, and automatic high extinction were rejected. They would break world consistency or obscure gameplay.

### 5 Future Visual Improvements

1. Worldspace sea-level metadata for fog-layer anchoring.
2. Cloud optical-depth coupling from the visible cloud model.
3. Humidity-dependent spectral aerial perspective.
4. Local low-lying mist volumes near water and waterfalls.
5. Moon-specific phase and colour calibration.

### 5 Future Performance Improvements

1. Dirty-update froxels when weather and camera are stationary.
2. Adaptive Z slices from measured depth occupancy.
3. Empty-space froxel classification.
4. Time-sliced distant local-light injection.
5. Quantized weather-state history invalidation.

### 5 Future Feature / Research Ideas

1. Weather-mod-provided humidity interface.
2. Valley fog using TerrainOcclusion height context.
3. Interior portal fog exchange.
4. Aurora scattering coupling.
5. Cinematic-only high-order multiple scattering.

## Water Flow and Contact Foam

### Architecture and current behaviour

`Water.hlsl` already reconstructs the refracted receiver, animated normals, Rain Response ripples, and Waterbody flowmap. The release correction keeps foam owned by the water surface: WaterOptics exposes enable, presence and detail controls, while the historical player-wake lane remains forced to zero only to preserve the shared-buffer and configuration ABI.

### Changes

- Shallow/object contact foam now uses unrefracted per-pixel scene-depth separation. Refracted receiver distance was piecewise constant on some coarse Skyrim water cells and exposed the mesh as large rectangular tiles.
- Fixed flowmap-texel gradients detect current direction changes independent of screen resolution.
- Foam breakup uses a generated 2048x2048 seamless linear stencil sampled in two rotated, absolute-world layers and advected by current direction.
- Player-centred wake projection has been removed. Player position, movement, and camera rotation do not participate in foam coverage.
- Foam is composited before the existing atmospheric fog, keeping depth and weather coherence.
- The path safely compiles away for LOD, simple, underwater, non-refractive, and specular-only permutations.
- Shared b5 is asserted at 704 bytes, with player fields at HLSL-reflected offsets 672 and 688. Water settings are asserted at 64 bytes.

### Fidelity, performance, security, stability

Flowmap water adds two texture samples for fixed-neighbour current-change detection. Visible foam adds two derivative-aware stencil samples; generated mips suppress distant shimmer. There is no persistent simulation texture, CPU trail allocation, or player wake work. The authored mask is loaded locally through DirectXTK/WIC as linear shader data. No network, process, registry, or unrelated filesystem behavior is introduced.

### Rejected changes

A camera-centred foam texture, player-projected plume and unbounded per-object particle simulation were rejected. Persistent compute foam remains a future controlled feature because it needs explicit lifetime, motion-vector, and reset contracts.

### 5 Future Visual Improvements

1. Persistent low-resolution world wake atlas.
2. Waterfall/rapid aeration from vertical velocity.
3. Boat and creature wake emitters.
4. Temperature-dependent foam lifetime.
5. Shore material influence on foam colour and persistence.

### 5 Future Performance Improvements

1. Share neighbour flow samples with flow-normal evaluation.
2. Skip foam math from a material-level no-flow/no-contact classification.
3. Distance-rate high-frequency foam detail.
4. Pack interaction actors into a small culled GPU list.
5. Use flowmap mips for far current-change detection.

### 5 Future Feature / Research Ideas

1. Rapids/whitewater classifier.
2. Persistent advected foam clipmap.
3. Projectile and explosion water impulses.
4. Ice-edge foam response.
5. Foam contribution to reconstruction reactive confidence.

## Validation

- Canonical Release `PIXLRenderer` build: PASS.
- PIXL integrated module audit: PASS, 38 modules.
- Strict FXC Grass permutations: PASS.
- Strict FXC Water base, flow/ripple, VC and underwater permutations: PASS.
- Strict FXC Atmosphere conservative depth, integration, light scattering and material kernels: PASS.
- Shader reflection: shared player offsets 672/688, WaterOptics FeatureData size 64, Atmosphere FeatureData size 272.
- `git diff --check`: PASS apart from expected repository LF/CRLF notices.

Runtime visual validation remains required for weather-mod extremes, shallow shore geometry, fast-flowing rivers, third-person camera orbit, TAA/DLSS/DLAA, and dense grass under a low sun.
