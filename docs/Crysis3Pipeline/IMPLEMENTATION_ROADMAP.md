# Implementation Roadmap

## Phase 1 — Directional Visibility correctness

Status: implemented in canonical source; runtime visual validation pending.

- Reuse existing HybridGI horizon masks; no duplicate pass.
- Correct octahedral bent-normal history interpolation.
- Add viewport/sample observation confidence.
- Apply confidence once rather than twice.
- Add geometry-reactive temporal blending and a cancellation-safe normalization fallback.
- Add normal-aware half/quarter-resolution reconstruction using HybridGI's existing normal pyramid.
- Preserve the existing `HybridGICB`, formats and pass order; bind the owned normal pyramid at local upsample `t6`.

## Phase 2 — Existing-system validation and consolidation

Risk: low.

- Verify Atmosphere shadow history across camera cuts, cells and FOV changes.
- Validate Particle/Effect light receive paths. CPU owner aggregation and aggregate snow-local-light energy compression are implemented; runtime brightness/count validation near carried lights and NPCs remains pending.
- Add material-classification debug confidence before changing normalization.
- Validate WindowLife reflection/grime/weather response at grazing angles and distant mips.
- Record GPU timings and resource memory from PIXL diagnostics.

## Phase 3 — High-value bounded prototypes

Risk: medium; each prototype remains independently disableable.

1. Live water-surface caustic focusing: a Water-family Jacobian prototype is implemented without extra samples/passes; a full cross-pass receiver field remains future work.
2. Shared deterministic world-wind sampling contract: implemented with byte-identical active foliage DXBC; adoption by hair/particles remains future work.
3. Effective-radius extended emitters using current RadiantGrid fields.
4. Multiple bounded parallax-correct local reflection probes.
5. Normal-aware bent-visibility reconstruction: implemented in Phase 1; runtime edge acceptance remains pending.

The active reflection hierarchy and the exact requirements for bounded local probes are documented in `REFLECTION_HIERARCHY_REPORT.md`; no duplicate global SSR pass is planned.

## Phase 4 — Architecture research

Risk: high; not part of the release-safe baseline.

- LTC rectangle/disc/tube/portal lights and shape inference.
- Constraint-chain grass patches and writable 3D force field.
- Persistent world material-state/decal atlas.
- Automatic light clipping/containment volumes.
- Adaptive silhouette relief with depth/reconstruction integration.
- Physically separated optical glare/diffraction/ghost systems.
- Unified material transmission profiles.

## Acceptance gates

Every phase must pass:

1. representative strict FXC permutations;
2. Release C++ build when C++ or packaged module metadata changes;
3. shader/register and CPU/HLSL ABI review;
4. selective cache invalidation, never an unexplained global rebuild;
5. TAA and DLSS motion tests;
6. interior/exterior, cave/eave, water, particles, foliage and reflective-material scenes;
7. warm-restart cache test;
8. clean PIXLRenderer log and runtime diagnostic views.
