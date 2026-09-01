# PIXL Dynamic Water Caustics Report

Date: 2026-08-31
Status: source prototype complete; strict shader validation passed; runtime/visual validation pending; not deployed

## Outcome

PIXL now has a bounded first native dynamic-focus prototype in the canonical Water pixel shader. It derives refractive focusing from the same final water normal used to shade the visible surface. That normal already contains Skyrim's three animated wave-normal layers and, where active, PIXL Waterbody flowmap normals and Rain Response ripple normals. The prototype therefore reacts to the live surface rather than synthesizing motion solely by panning `watercaustics.dds`.

The existing authored receiver-caustic path remains intact. The new term only modulates the already visible refracted receiver beneath the water surface; it does not alter reflections, fog, water alpha/fresnel, the t65 atlas binding, the 5x5 water-height grid, or opaque Lighting material output.

This is a **B — Controlled Improvement**. It is bounded and compile-safe, but in-game visual and temporal acceptance is still required before release/deployment.

## Canonical and live path audit

### Canonical source

- `distribution/Shaders/Water.hlsl` is the canonical top-level Water shader compiled for Skyrim Water permutations.
- `pipeline/Water Optics/Kernels/WaterOptics/WaterCaustics.hlsli` is included by `distribution/Shaders/Lighting.hlsl` and implements caustics on submerged opaque receivers.
- `pipeline/Water Optics/Kernels/WaterOptics/WaterParallax.hlsli` is included by Water permutations and samples the material-local animated height channels.
- `pipeline/Water Optics/Kernels/WaterOptics/watercaustics.dds` is the authored 43,852-byte caustic atlas (SHA-256 `5763E8A50745CF3994B499AA7216EFFD8E012A7190B2219C82D85305668EB5C4`).
- `engine/Modules/WaterOptics.cpp/.h` owns settings, module cache scope, DDS loading, and the Lighting pixel-stage t65 SRV binding.
- `distribution/Shaders/Common/SharedData.hlsli` mirrors the existing 48-byte `WaterOptics::Settings` payload.

`shadertoolsconfig.json` and CMake's feature-kernel discovery place the canonical pipeline kernel directories ahead of `distribution/Shaders` in shader include resolution. Packaged/runtime kernels are flattened beneath `Data/Shaders/WaterOptics`.

### Runtime state at audit time

Before this prototype, the relevant canonical/runtime copies were hash-identical, including `Water.hlsl`, `Lighting.hlsl`, and `Common/SharedData.hlsli`. Skyrim was running as PID 23912 during the work, so runtime Data, staging, the DLL, and shader caches were intentionally not changed. After the source edit, only canonical `distribution/Shaders/Water.hlsl` differs from the still-running live copy, as expected.

No deployment or cache deletion was performed.

## Existing caustic contract

The receiver path gets `waterData.w` from PIXL's 5x5 CPU-updated cell grid, projects world XY along the directional-light ray, samples two animated scales of the t65 authored atlas, reconstructs texture curvature, applies dispersion and focus controls, and bounds the result with sunlight and Beer-Lambert depth attenuation.

That path is spatially stable and useful, but it cannot see the actual live water surface:

- the animated wave textures are Water material SRVs t4, t5 and t6;
- the Waterbody flowmap normal is Water material SRV t9;
- Rain Response composes its procedural ripple normal inside `GetWaterNormal()`;
- submerged receivers are shaded by the opaque Lighting pass, where those per-water-draw bindings and UV transforms do not exist;
- Water Optics binds only the authored atlas at Lighting PS t65.

Adding those resources to Lighting would require new persistent bindings plus per-water-material UV/animation state, and still would not solve multiple visible water bodies cleanly. That was rejected for release polish.

## Implemented prototype

`GetWaterDiffuseColor()` now returns the reconstructed distance from the visible water surface sample to its refracted receiver. This is a local HLSL struct extension only; it is not a CPU/GPU buffer or shader ABI change.

`GetDynamicSurfaceCausticFocus()` then:

1. refracts the current directional sunlight through the completed live water normal using the air-to-water ratio `1 / 1.333`;
2. differentiates the refracted ray slope with coarse quad derivatives;
3. transforms those derivatives from screen space into the local horizontal water basis;
4. constructs the 2x2 refracted-footprint Jacobian at the reconstructed receiver distance;
5. uses bounded reciprocal footprint area as the focusing/spreading term;
6. fades the result at invalid bases, invalid transmission, low sun, negligible depth, and excessive depth;
7. scales it through the existing `CausticsFocus`, `CausticsStrength`, and `CausticsVisibility` controls.

The response is bounded to `0.72..1.55` before control blending. With the current release tuning, the actual modulation is deliberately narrower. The determinant, sun normalization, ray depth, receiver depth, and reciprocal area all have explicit numerical guards.

The modulation is applied only when enhanced caustics are enabled, the Water permutation has refractions, the camera is above water, and the water material is exterior. It touches only `diffuseOutput.refractionColor` before the existing water lighting/fog/fresnel composition.

## ABI, resources, cache scope, and performance

- CPU/HLSL FeatureData ABI: unchanged.
- Constant buffers: unchanged.
- SRV/UAV/sampler registers: unchanged.
- Render targets and passes: unchanged.
- Draws/dispatches: unchanged.
- Authored atlas and t65 binding: unchanged.
- Module version: unchanged. Bumping Water Optics would unnecessarily invalidate its Lighting and ImageSpace consumers.
- Eventual cache impact: the edited top-level `Water.hlsl` belongs to the Water shader family only; no global cache invalidation is required or requested.

Expected cost on affected exterior refraction pixels is one `refract`, coarse derivatives, a small 2x2 Jacobian, and scalar arithmetic. There are no new texture samples. No GPU timing claim is made because no runtime profiling was performed.

## Strict shader validation

Windows SDK 10.0.26100 x64 FXC compiled all affected pixel variants with `ps_5_0 /Ges /WX /O3`:

| Variant | Coverage | Bytecode |
|---|---|---:|
| `water-base` | Reflections + refractions + depth + Water Optics | 37,456 bytes |
| `water-flow-ripple` | Base plus flowmap, blended normals, and Rain Response ripples | 54,652 bytes |
| `water-vc-depth` | Vertex-colour/vertex-alpha-depth refraction path | 31,392 bytes |
| `water-interior` | Interior gate/fallback | 37,456 bytes |
| `water-underwater` | Underwater exclusion and local struct initialization | 30,148 bytes |

Artifacts are under `build/shader-validation-water-caustics-20260831`. Scoped `git diff --check` passed apart from the repository's existing LF-to-CRLF notice.

`tools/AuditPixlRenderer.ps1` also passed with all 38 integrated modules. A host DLL rebuild was not required for this shader-only, ABI-neutral prototype; the audit used the existing Release DLL and does not constitute runtime visual validation.

## Runtime acceptance queue

No visual success is claimed from compilation. Before deployment or release, test:

1. calm lake, river flowmap, and rain-ripple water under high and low sun;
2. shallow shore, 1-8 metre receiver depth, and deep water fade;
3. stationary camera followed by slow pan, fast traversal, and TAA/DLSS/DLAA comparison;
4. oblique viewing angles and silhouette/shore discontinuities for quad-derivative flashes;
5. exterior/interior and above-water/underwater transitions;
6. `Enhanced Caustics`, intensity, focus, and visibility at zero/default/maximum;
7. comparison with the authored receiver atlas to detect double contrast or pattern disagreement;
8. GPU capture/timing on a representative water-heavy exterior.

Reject or reduce the prototype if it produces sparkling, inversion at wave crests, visible 2x2-quad blocks, over-dark refraction, or a second obviously sliding caustic layer.

## Precise future full-receiver plan (not implemented)

True live-surface caustics on arbitrary opaque receivers need a dedicated cross-pass representation. A safe future prototype should:

1. allocate a camera-relative, half-resolution water-surface buffer containing linear water depth/height plus octahedral final water normal;
2. populate it from a dedicated Water-normal prepass using the exact material bindings and the same `GetWaterNormal()` wave/flow/ripple composition;
3. run a half-resolution compute resolve after water visibility is known, reconstruct receiver depth, project refracted solar footprints, and accumulate bounded irradiance/confidence;
4. reject silhouettes and missing/behind-camera receivers using both water depth and scene depth;
5. temporally reproject with water/receiver motion confidence and reset on camera cuts, FOV changes, teleport, interior transitions, or water-material changes;
6. composite the irradiance exactly once onto the refracted scene or a post-lighting caustic buffer;
7. assign SRV/UAV slots only after a complete active binding audit, add explicit unbind/lifetime/device-reset handling, and validate CPU/HLSL layouts with static assertions and shader reflection;
8. retain the atlas as distant/invalid-history fallback and amortize the new path by quality tier.

Repurposing the existing WaterMask target was considered but rejected: its stencil technique currently carries geometric surface information only and does not execute the animated normal/flow/ripple material path. Extending it would be a real pass/interface change, not a release-safe shader-local polish item.

This larger design is **C — Experimental** until resource ownership, pass order, cache scope, memory, temporal stability, and GPU cost are demonstrated.
