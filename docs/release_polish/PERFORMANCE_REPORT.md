# Performance Release Report

September 7 evidence boundary: ranked systems and earlier release-optimization descriptions below are inherited static assessments, not measured improvements from this continuation. Current fixes reduce repeated failure logging, prevent unsafe compiler inputs and share immutable metadata; no GPU timing has been measured. The shared-metadata-only rebuild reduced DLL size by 32,256 bytes. RCAS fallback has no added normal-path copy; grass linear-light flags avoid redundant conversion for flagged lights. See current subsystem reports for exact changes/tests. WARP correctness tests are not hardware performance benchmarks.

## Neural rendering and capture

Real-time Neural Rendering remains opt-in. PIXL exposes only controls supported by the validated Feature 18 integration; it does not fabricate an INT4/FP8 selector or transformer-step count. The NR quality contract changes the real scene/guide workload, while the vendor output remains at its validated presentation extent.

Photo Finish deliberately trades time for quality. Its 8/16/24-frame convergence settings perform complete fresh evaluations with a frozen camera and valid depth, motion, jitter and history. The new robust resolve adds a second CPU pass over multi-frame samples, so high-resolution 24-frame captures will take longer and use more transient memory. It is inactive during gameplay and therefore has no normal-frame cost.

## Shader/cache behavior

The current runtime log reports disk-cache reuse, not continuous permutation compilation. This release changes selected atmosphere, vegetation, water, reconstruction-input and eye/lighting shaders; their source fingerprints invalidate only affected permutations. Deleting a healthy cache repeatedly would cause avoidable recompilation and is not recommended. The public package deliberately omits machine-specific cached pipelines and compiles once on the target system.

## Claims and validation

No GPU timing improvement is claimed without a controlled capture. Release validation should record real render FPS separately from presented/generated FPS, plus GPU frame time, p95/p99 pacing and VRAM at 1080p and 1440p on the RTX 3060 Ti target.

## Ranked probable GPU costs

These are ranked by static workload/resource inspection, not fabricated timings.

| Rank | Pass/system | Cost character | Resolution/cadence | Release optimization |
| ---: | --- | --- | --- | --- |
| 1 | Hybrid GI / directional visibility / reflections | Texture bandwidth plus ray-step compute | Quality-selectable quarter/half/full scene buffers; temporal | Real resolution, slice/step, reflection-step and history tiers; depth/normal-aware reconstruction |
| 2 | Atmosphere volumetrics / Light Volumes | 3D UAV bandwidth and lighting compute | Configurable froxel XY/Z; temporal | Grid dimensions and miss samples scale by quality; valid history is reused |
| 3 | Image Reconstruction / optional NR / frame generation | Vendor compute plus D3D11-D3D12 interop copies when enabled | Display/render resolution every presented frame | Capability gates, stable inputs, native/DLAA modes, optional NR/FG; inactive backends do not dispatch |
| 4 | Ground Response geometry | Hull/domain tessellation, displacement reads and deformation updates | Distance-limited terrain; dirty interaction fields | Near/far tessellation, range/fade, scan interval and interaction quality tiers |
| 5 | World Probes / SkyBounce | Scene capture, cubemap filtering and volume update bandwidth | Scheduled cubemap/probe updates | Dirty/temporal scheduling, bounded formats and no speculative volume expansion |
| 6 | Water SSR / optics | Ray-step texture reads and refraction/reflection bandwidth | Water pixels; quality-selectable steps/range | Bounded trace, edge fade, history reset and in-path caustics/foam without added passes |
| 7 | Contact Shadows | Divergent depth ray marching | Eligible lit pixels | Sample-count tiers, bounded thickness and filter thresholds |
| 8 | Tissue Diffusion | Separable filtering bandwidth | Eligible skin buffers | Sample-count tiers and Lighting-only permutations |
| 9 | Rain / roof runoff | Detection/generation/resolve UAV traffic | Weather-gated, bounded coverage | World-space bounded grids, early disable and no increased tracing distance |
| 10 | Camera DOF / bloom / local exposure | Multi-level sampling and post bandwidth | Enabled-only pyramids/fullscreen composite | Downsampled pyramids, Karis filtering, effect gates and quality tiers |

## Ranked probable CPU costs

| Rank | System | Frequency / pressure | Release optimization |
| ---: | --- | --- | --- |
| 1 | Runtime shader compilation/cache | Startup/background worker pool, file I/O and compiler processes | Disk fingerprints, stage/family invalidation, hardest-first queues, background worker caps |
| 2 | Ground interaction discovery | Periodic actor/object/spell scans | Bounded radius/capacity/interval, material exclusions and dirty field updates |
| 3 | Radiant Grid emitter collection | Per submitted effect/particle | Owner aggregation, finite validation, capacity culling and safe disabled draining |
| 4 | Actor Surface Effects | Periodic active-actor/equipment updates | Distance/capacity selection, dirty uploads and stable ownership |
| 5 | World Probe/SkyBounce scheduling | Frame updates plus occasional captures | Dirty/cadenced updates and resource reuse |
| 6 | Waterbody/flowmap streaming | Worldspace/cell transitions | Cache reuse and explicit invalidation rather than per-frame rebuild |
| 7 | Photo Finish resolve | Capture-only readback/resolve/encode | Isolated worker transaction; zero normal-gameplay cost |
| 8 | UI/profiler | Menu/developer-only | Diagnostics gated; no per-frame log dump in normal play |

## Persistent GPU memory review

- Hybrid GI owns resolution-scaled depth/normal/radiance/history intermediates. Cost scales with pixel count and quality; formats and history count were retained because precision reductions were not visually validated.
- Atmosphere and Light Volumes own froxel 3D textures. Their dimensions are real quality controls and resources are recreated only when required.
- World Probes owns cubemap capture, inference and irradiance/specular products; SkyBounce owns its probe volume. Both remain bounded and scheduled. Cascaded replacements were deferred.
- Ground Response owns persistent snow/mud deformation and reconstructed-normal fields. Actor Surface Effects owns bounded actor-mask layers. Capacity/distance constraints prevent unbounded growth.
- Image Reconstruction owns current/history/guide resources required by the selected backend; NR/FG sidecar resources exist only when provisioned. Photo Finish uses transient capture buffers and releases its override state.
- WindowLife atlases and AmbientProbe textures are fixed packaged assets. The package audit hashes them and no duplicate runtime copy is staged.

No format was lowered merely to claim VRAM savings. The highest-value next measurement is a PIX/RenderDoc or vendor-profiler capture on the RTX 3060 Ti at 1080p and 1440p, with p95/p99 pacing and committed/resident VRAM recorded for each global quality tier.
