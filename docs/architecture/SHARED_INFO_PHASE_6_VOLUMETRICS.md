# Shared Renderer Information — Phase 6: Atmosphere and Volumetrics

## Audit result

PIXL already had a complete DX11 froxel pipeline in `Atmosphere`: a material/extinction volume, conservative depth range, current and previous scattering volumes, and a front-to-back integrated scattering/transmittance volume. It also already injected directional shadowing, ambient IBL, SkyBounce and RadiantGrid local lights. Creating a second volume would have duplicated bandwidth, memory and temporal state.

The implementation therefore consolidates ownership without replacing the visual algorithm or changing shader registers, entry points, settings, hooks, or the Atmosphere constant-buffer ABI.

## Implementation

- Added `VolumetricContext`, a synchronized renderer-owned publication of the existing Atmosphere froxel resources and their dimensions, lighting inputs, temporal status, frame and estimated memory use.
- Atmosphere remains the sole owner and producer of its persistent textures. No texture, render pass, shader permutation, or runtime dependency was added.
- The Lighting path remains the first consumer through the established `Atmosphere::GetAtmosphere` composite. CPU binding now acquires the authoritative integrated volume and conservative depth from `VolumetricContext`.
- Replaced Atmosphere's duplicate camera-distance, projection, dynamic-resolution and worldspace history tests with its registered `TemporalContext` history.
- Preserved Atmosphere-specific invalidation for interior/sky/map classification and lighting-input changes.
- Connected Atmosphere grid workload to `GPUWorkloadBudgeter`. The controller is disabled by default, so existing grid dimensions and appearance are unchanged unless the developer explicitly enables Adaptive workload control.
- Added Developer diagnostics for froxel dimensions, estimated allocation, temporal acceptance and active lighting sources.

## Resource and ABI impact

The shared service retains COM references only; it allocates no GPU resources. Existing resources remain:

- `VBufferA`: `R16G16B16A16_FLOAT`, material/extinction data.
- `LightScattering` and history: `R16G16B16A16_FLOAT`.
- `IntegratedLightScattering`: `R16G16B16A16_FLOAT`.
- Conservative depth and history: `R32G32_FLOAT`.

Shader slots, shader profiles, HLSL entry points, Atmosphere settings keys, and the 272-byte settings ABI are unchanged. History memory reporting counts the scattering and conservative-depth history resources; total volume diagnostics include all six persistent froxel/depth resources.

## Temporal and reconstruction behavior

Broad frame continuity now comes from the renderer-wide temporal authority. Depth-aware history rejection remains in `VolumetricFogLightScatteringCS.hlsl`, including conservative current/previous depth comparisons and radiance clamping. A frame with missing resources, failed shader compilation, disabled volumetrics, map suppression, or zero density invalidates shared publication and retains the established safe fallback.

Per-material annotation rejection was intentionally not forced into this pass: Atmosphere executes before a guaranteed complete screen annotation resource in some Skyrim paths. Conservative depth rejection remains the safe release behavior; annotation-assisted rejection is deferred until pass ordering guarantees availability.

## Expected runtime impact

At default settings, dispatch dimensions and sampling are unchanged. Shared publication adds only small synchronized CPU metadata copies. When experimental adaptive workload control is enabled, the budgeter changes XY froxel footprint and Z slices discretely with hysteresis; every dimension change invalidates history and recreates the existing resources safely.

## Validation

- Release `PIXLRenderer.dll` build: passed.
- Portable CTest suite: 20/20 passed, including six C++ tests, twelve architecture/contract tests and two strict FXC tests.
- Strict compute-shader matrix: 636/636 FXC `/WX /Ges /O3` cases passed. This includes all Atmosphere material, conservative-depth, scattering define combinations and integration kernels.
- `PIXL-Audit`: passed with 42 shipping modules and one retired source-only module.
- `TestVolumetricContext.ps1`: passed. It checks shared publication/consumption, unified temporal use, removal of duplicate broad temporal state, depth-aware shader rejection, and Lighting integration.
- Atmosphere settings ABI: Release compile retained the existing `sizeof(Settings) == 272` assertion and shader metadata validation passed.
- `git diff --check`: passed; only repository line-ending conversion notices were emitted.
- The optional legacy `TestPixlAtmosphereDefaults.ps1` continues to report a pre-existing fallback/default discrepancy (`fogHeight` is `11304.1` in the C++ fallback and `22000` in shipped JSON). Neither value was changed in this phase. The shipped JSON remains authoritative after settings load; changing the release look was rejected as outside this architecture-only phase.
- Live Skyrim validation remains required for interior/exterior transitions, world map transitions, camera cuts, resolution/upscaler changes, weather changes and Adaptive workload transitions.

## Deferred work

- Dynamic emitter injection should consume the Phase 8 shared emitter registry rather than introducing another light list here.
- Surface fog and weather event exchange belongs to the requested Surface Response World phase, not this consolidation.
- Material-aware temporal rejection can be added once FrameAnnotations are guaranteed at this render point.
