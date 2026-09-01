# PIXL Neural Rendering Pipeline Audit — 2026-09-01

## Current frame path

```text
Skyrim opaque / alpha / effects
  -> PIXL lighting, materials, weather and post inputs
  -> EncodeTexturesCS
       R8 reactive mask
       R8 transparency/composition mask
       normalized-UV, 5x5 dilated motion
       typed depth copy where required
  -> Streamline DLSS at render extent -> display-resolution color
  -> optional RCAS
  -> Camera Suite / Skyrim image-space work
  -> D3D11/D3D12 shared proxy color
  -> Feature 18 neural evaluation
       display-resolution post-DLSS color
       render-resolution raw depth subrect
       render-resolution dilated normalized-UV motion subrect
       display-resolution neural output
  -> optional FidelityFX frame generation with separate raw guides/UI
  -> presentation
```

## Confirmed input contracts

- Standard Streamline motion is normalized UV, so `mvecScale={1,1}` is correct according to the bundled Streamline programming guide.
- PIXL's DLSS motion output is already 5x5 dilated. `motionVectorsDilated` was corrected from false to true.
- Motion is produced from unjittered transforms, so `motionVectorsJittered=false` remains correct.
- Skyrim device depth is non-reversed and clears to far/1; `depthInverted=false` remains correct for the current path.
- Streamline receives explicit input/output extents, current jitter, camera matrices, near/far data, reset state, HDR declaration, auto exposure, reactive mask and transparency hint.
- Feature 18 receives display color separately from render-resolution depth/motion. Its generic `Width/Height` and private input fields consistently describe the guide extent; output fields describe the display-resolution neural output.

## Fixed defects

1. Scaled DLSS previously published display size as both input and output size while tagging render-resolution depth/motion. DLAA hid the contradiction. Input and output domains are now explicit.
2. Streamline was told PIXL motion was undilated despite `EncodeTexturesCS` performing a 5x5 closest/longest dilation.
3. Photo Finish was repeatedly saved with `neural source off`. The preference now defaults on, remains capability-gated, has an explicit final-composite label, and the current live configuration is enabled.
4. Feature 18 is now gated until a valid encoded guide extent exists. An initial Present can no longer submit a manufactured 1x1 depth/motion contract and latch the optional runtime off for the session.

## Rejected workload experiment

An experimental independent Feature 18 output extent was tested to reduce RTX 3060 Ti cost. The 310.8 private runtime rejected `1920x1080 color -> 1440x808 output` with `0xBAD00005`. PIXL retained ordinary DLSS, but the selector was not a valid vendor contract. The reduced surface, bilinear reconstruction pass, serialization and GUI control were removed. Future workload scaling must use a documented/vendor-supported quality contract or change the upstream DLSS render resolution; PIXL does not expose the rejected option as a no-op.

## Motion and classification assessment

The shared motion texture covers camera and conventional geometry motion. PIXL's reactive and transparency masks cover Skyrim's temporal-AA mask and the deferred water/transparency classification. Procedural deformation, particles, alpha vegetation, WindowLife, precipitation, volumetrics and animated shader UVs still require category-by-category runtime visualization before claiming true object/material velocity coverage. Supplying a confidently wrong synthetic vector would be worse than a reactive/no-history classification, so no speculative global velocity was added in this pass.

## Frame generation and pacing

PIXL currently uses FidelityFX frame generation after neural reconstruction, with separate raw depth/motion resources and a registered UI resource. Pulse Profiler already distinguishes real/render cadence from post-FG estimates. Hardware NVIDIA frame generation is not spoofed onto unsupported RTX 30-series devices. Runtime p50/p95/p99 Present and GPU measurements remain required; compilation cannot establish pacing quality.

## Memory and bandwidth

The existing path owns full-size R8 reactive/transparency masks, a DLSS motion copy, shared raw FG depth/motion, separate neural depth/motion, two full neural presentation outputs, and backend-owned histories. Full outputs remain double-buffered to avoid D3D11 photo capture racing D3D12 presentation. The rejected reduced-output experiment adds no resources to the corrected build.

Further memory reduction should target conditional Photo Finish double buffering and duplicated guide resources only after combined NR+FG lifetime captures prove that aliasing is safe. No unverified format/precision reduction was made.

## Required runtime matrix

1. DLAA / Full NR: baseline fidelity and cost.
2. DLSS Quality / Full NR: scaled-contract stability, GPU time, fine detail and motion.
3. DLSS Balanced / Full NR: scaled-contract stability and performance.
4. Rapid camera rotation, foliage, hair, particles, snowfall, water and WindowLife.
5. Photo Finish: log must report `neural source on`; saved image must match the neural presentation.
6. NR + FidelityFX FG: inspect HUD separation, frame pacing and generated-frame artifacts.

No runtime performance figure is claimed by this static/build validation pass.
