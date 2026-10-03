# Shared Information Upgrade — Phase 2: Pixel Annotations

## Audit result

PIXL already owns authoritative deferred material, normal/water, temporal-AA, reconstruction reactive and transparency masks. A new full-screen G-buffer would duplicate these facts and add a permanent bandwidth/VRAM cost. The existing `FrameAnnotations` name also belongs to PIXL's GPU-event hook module, so the renderer service is deliberately named `PixelAnnotations`.

## Implementation

- Added a common material taxonomy and compact flags for terrain, common physical materials, vegetation, characters, transparency, water, PIXL liquids, particles, sky and WindowLife.
- Added deterministic draw/material-time classification policy. Specific authored PIXL domains take priority over broad material hints.
- Added a frame-scoped view registry that publishes existing authoritative masks; it allocates no texture and performs no screen-space guessing pass.
- Added matching HLSL numeric constants without assigning a new register.
- ImageReconstruction now publishes and consumes the shared base views. RCAS consumes shared reactive/transparency views.
- Reactive coverage is also published as the current shared disocclusion-confidence input for Phase 1 diagnostics.

## Deferred migrations

HybridGI, Auto-DOF and ContactShadows retain their existing bindings until their shaders can consume annotation semantics without introducing a new register or permutation expansion. Draw-family packed classification output is likewise deferred until each authoritative producer can write it safely. Unknown/ambiguous pixels remain `Unknown` and retain existing rendering.

## Cost and ABI

- GPU time: no new pass.
- VRAM: no new allocation; retained COM references only.
- Shader ABI/registers: unchanged.
- Visual behavior: unchanged by design.

## Validation

Release build, CTest contract suite and PIXL-Audit are phase-gate requirements. Live-game validation remains pending for native, TAA, DLSS and FSR paths.
