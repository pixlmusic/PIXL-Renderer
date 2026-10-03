# Shared Renderer Information — Phases 1–6

The completed architecture now provides authoritative shared answers for temporal continuity, pixel classification, reconstruction inputs, adaptive workload, hybrid reflections, and Atmosphere froxel resources.

| Phase | Shared service | Initial migrations |
|---|---|---|
| 1 | `TemporalContext` / history registry | HybridGI, Auto-DOF, ImageReconstruction, Atmosphere |
| 2 | `PixelAnnotations` | ImageReconstruction, RCAS |
| 3 | `ReconstructionContext` | ImageReconstruction, Contained Liquids, Reactive FX |
| 4 | `GPUWorkloadBudgeter` | HybridGI, Atmosphere |
| 5 | `ReflectionContext` | HybridGI producer, Deferred consumer |
| 6 | `VolumetricContext` | Atmosphere producer, Lighting binding/composite |

All migrations retain compatibility paths and existing module identities. No GPU Scene, Distant World, or impostor work was introduced. Phases 7 and 8 (Surface Response World and Dynamic Emitter Registry) remain deferred because the maintainer ended this implementation pass after the Phase 6 validation gate.
