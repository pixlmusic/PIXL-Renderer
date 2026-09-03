# Performance Release Report

## Neural rendering and capture

Real-time Neural Rendering remains opt-in. PIXL exposes only controls supported by the validated Feature 18 integration; it does not fabricate an INT4/FP8 selector or transformer-step count. The NR quality contract changes the real scene/guide workload, while the vendor output remains at its validated presentation extent.

Photo Finish deliberately trades time for quality. Its 8/16/24-frame convergence settings perform complete fresh evaluations with a frozen camera and valid depth, motion, jitter and history. The new robust resolve adds a second CPU pass over multi-frame samples, so high-resolution 24-frame captures will take longer and use more transient memory. It is inactive during gameplay and therefore has no normal-frame cost.

## Shader/cache behavior

The current runtime log reports disk-cache reuse, not continuous permutation compilation. This release changes selected atmosphere, vegetation, water, reconstruction-input and eye/lighting shaders; their source fingerprints invalidate only affected permutations. Deleting a healthy cache repeatedly would cause avoidable recompilation and is not recommended. The public package deliberately omits machine-specific cached pipelines and compiles once on the target system.

## Claims and validation

No GPU timing improvement is claimed without a controlled capture. Release validation should record real render FPS separately from presented/generated FPS, plus GPU frame time, p95/p99 pacing and VRAM at 1080p and 1440p on the RTX 3060 Ti target.
