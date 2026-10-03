# Shared Information Upgrade — Phase 3: Reconstruction Context

## Implementation

`ReconstructionContext` now publishes the renderer's reconstruction backend, depth, motion, pixel annotations, reactive/transparency masks, temporal validity and render/output dimensions. Exposure is part of the stable API but remains absent until CameraSuite can publish its exposure view at the correct pass boundary.

Contained Liquids and Reactive FX are the first bounded reactive contributors. Their existing draw logic and mask shaders are unchanged; only discovery/invocation ownership moved from hard-coded calls to a 32-entry setup-time registry. RCAS already consumes the shared reactive/transparency views from Phase 2.

## Safety and cost

- Existing DLSS/FSR ordering and backend calls are unchanged.
- No new texture, pass, register, readback or per-frame filesystem work.
- Contributor callbacks execute from a bounded setup-time registry without a per-frame callback snapshot allocation.
- Missing inputs remain null and consumers must retain their existing fallback.

## Deferred

WaterOptics, rain, WindowLife and foliage can register only after their exact coverage resources/pass ownership are verified. DOF transition contribution is deferred to avoid broad false reactivity. Exposure publication is also deferred as noted above.

## Validation

Release build, CTest contracts and PIXL-Audit are required at the phase gate. Live validation remains pending across Native/TAA/DLSS/FSR and frame generation.
