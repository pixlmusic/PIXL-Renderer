# Future Rendering Roadmap

## Visual research

1. Vendor-documented semantic or material guides for Neural Rendering, if NVIDIA exposes them publicly.
2. GPU-side robust Photo Finish resolve to reduce capture latency and CPU memory traffic.
3. Motion-compensated particle accumulation for unlocked or animated cinematic captures.
4. Validated vendor precision selection only if a supported Feature 18 API appears.
5. Per-material temporal confidence shared by DLSS, GI, reflections and sharpening.

## Performance research

1. Pool Photo Finish conversion surfaces and stream robust statistics without repeated conversion.
2. Profile the NR scene/guide quality contracts on RTX 3060 Ti, 4060, 4070 and 4090.
3. Add measured VRAM accounting for every sidecar surface to Pulse Profiler.
4. Investigate asynchronous photo resolve without weakening input/camera locking.
5. Validate clean-cache permutation warm-up tooling independently of user runtime caches.

These are post-release modular investigations, not hidden prerequisites for the current build.
