# PIXL Optical Glare Audit

## Scope and decision

This audit cross-references CryEngine 3's HDR flare/glare philosophy with PIXL's active Camera Suite. The release-safe decision is to preserve PIXL's current bloom pipeline, correct one stale-exposure compatibility defect, and not add a second glare, lens-flare, ghost, diffraction, starburst, or halation pass during this batch.

PIXL already replaced the broad legacy bloom architecture with a compact scene-linear pyramid. The remaining Crytek-inspired opportunity is a separate, source-aware optical-glare system, but it needs reliable bright-source identification and occlusion before it can improve the image rather than recreate screen-wide haze.

## Active implementation traced

### Ownership and ordering

- `engine/Modules/CameraSuite.cpp` owns allocation, dispatch, settings, serialization, fallback behavior, and final composition.
- `RunCameraFinishingPasses()` operates on the resolved presentation scene supplied to Camera Suite. With Image Reconstruction loaded, its hooks own reconstruction before the Camera Suite present chain; bloom is therefore display-resolution finishing rather than unstable internal-resolution scene content.
- `DispatchHDROutput()` combines the bloom texture with exposure, local exposure, grading, environmental lens response, HDR transfer, and UI composition.
- Frame-generation presentation uses the same Camera Suite output contract and its explicit UI-composition path. The bloom pyramid itself has no temporal history, so it cannot retain stale highlights after cuts or disocclusions.
- When PIXL bloom is enabled, `ApplyPlayerPostProcessing()` zeroes Skyrim's bloom scale. When PIXL bloom is disabled, Skyrim's authored image-space bloom remains available. This prevents double bloom while retaining a compatibility fallback.

### Highlight extraction

`BloomPrefilterCS.hlsl`:

- decodes the source to non-negative scene-linear colour;
- uses PIXL exposure only while Physical Camera owns exposure, avoiding stale-exposure jumps after that subsystem is disabled;
- reduces a 2x2 source footprint with a Karis-weighted average;
- uses an exposure-relative quadratic soft knee;
- preserves highlight chroma;
- applies a bounded coherence guard to isolated subpixel fireflies.

This is materially different from a blur of the complete image. Sub-threshold scene detail contributes no bloom.

### Pyramid and reconstruction

- Half, quarter, eighth, and sixteenth-resolution levels form the bloom pyramid.
- `BloomDownsampleCS.hlsl` uses a rotationally symmetric 13-tap reduction rather than a four-tap box.
- `BloomUpsampleCS.hlsl` uses a full 3x3 tent and deliberately attenuates the broad low-resolution octave so it cannot dominate compact highlight structure.
- Intermediate resources use `R11G11B10_FLOAT` when supported and fall back to `R16G16B16A16_FLOAT`.
- Resources are persistent and recreated only with the owning presentation resources; there is no per-frame texture allocation.

## Crysis 3 comparison

Crytek's useful lesson is separation of optical phenomena, not maximum flare intensity. PIXL currently has:

| Optical phenomenon | Current PIXL status | Release decision |
|---|---|---|
| Bloom / veiling glow | Modern scene-linear soft-knee pyramid | Preserve |
| Body-camera halation | Small specialised finishing term | Preserve as experimental |
| Elemental lens frost/fire | Environment/gameplay-gated lens response | Preserve |
| Lens ghosts | Not implemented | Defer until source occlusion exists |
| Diffraction/starburst | Not implemented | Defer; must be aperture/source driven |
| Anamorphic glare | Not implemented | Optional future artistic control |
| General screen-space flare sprites | Not implemented | Reject as a default path |

## Correctness and stability findings

1. Bloom is generated after reconstruction from the resolved scene, which avoids feeding broad optical energy into DLSS/FSR history.
2. The pass has no temporal history and therefore needs no camera-cut reset.
3. Exposure ownership is now explicit. Physical Camera uses its adapted exposure; the compatibility path uses unity rather than a stale exposure texture. Previously the disabled-Physical-Camera path could retain the last value written to the persistent exposure texture and make bloom jump after the camera subsystem was toggled off.
4. Skyrim bloom is suppressed only while PIXL bloom is active, preventing duplicate highlight energy.
5. The broadest octave is intentionally energy-reduced, limiting image wash and loss of local contrast.
6. The module defaults PIXL bloom off. This retains the current accepted visual baseline unless the user intentionally enables it.

The stale-exposure defect was the only release-safe correctness issue found in the audited path and was fixed in `BloomPrefilterCS.hlsl`. Camera Suite was advanced from `1-6-3` to `1-6-4` for selective invalidation. The accompanying C++ change only removes a stale byte-size claim from a comment; it does not alter runtime behavior or ABI.

## Rejected release changes

### Adding procedural ghosts now

Rejected because the current finishing input does not carry a stable list of emitting sources, emitter angular extent, or per-source visibility. Deriving ghosts only from bright pixels would create flares from snow, UI-like emissives, reflections, and transient reconstruction noise.

### Adding a starburst convolution to every bright pixel

Rejected because a full-screen directional kernel would increase bandwidth, soften the image, and amplify unstable particles/specular fireflies. A future implementation should use a compact bright-source list or thresholded tile classification.

### Enabling or retuning bloom by default

Rejected because this would change the accepted renderer baseline without runtime A/B evidence. The present defaults are deliberately restrained.

### Moving bloom before reconstruction

Rejected. Reconstructing blurred emissive energy would increase temporal ambiguity and make particles, magic, precipitation, and rapidly changing highlights more likely to smear.

## Recommended future architecture

A future `PIXL Optical Glare` extension should remain downstream of reconstruction and use:

```text
Resolved scene-linear HDR
        |
        +-- existing compact bloom pyramid
        |
        +-- bright-tile classification
                |
                +-- coherent source extraction
                +-- depth/visibility rejection
                +-- source angular-size estimate
                |
                +-- restrained diffraction / ghost synthesis
        |
        +-- exposure-aware energy composition
        |
      display mapping / UI / presentation
```

The source list should reject incoherent one-pixel fireflies and carry colour, luminance, screen position, approximate radius, depth, and confidence. Diffraction and ghosts should remain opt-in and quality-scaled. Bloom should stay independent and restrained.

## Performance

Current cost consists of one half-resolution prefilter, three downsample dispatches, three upsample dispatches, and one sample during final composition. No measurements were taken during this source audit. Expected cost is bounded by the persistent low-resolution pyramid and occurs only while PIXL bloom is enabled.

A future source-aware glare pass should be tile classified and indirect/budgeted rather than a large full-screen convolution.

## Validation

- CPU ownership, pass ordering, fallback, resource creation, and composition were traced in `CameraSuite.cpp/.h` and `ImageReconstruction.cpp`.
- GPU extraction, downsample, upsample, and final composition were traced in the Camera Suite kernels.
- Strict Windows SDK FXC validation passed all three bloom compute stages after the exposure-ownership fix.
- Runtime visual validation remains required for threshold transitions, saturated fire/magic, sunlit snow, HDR output, DLSS/FSR, and frame generation.

## Status

**AUDITED / CORRECTED / PRESERVED / FUTURE EXTENSION DOCUMENTED**

Changed files are `pipeline/Camera Suite/Kernels/CameraSuite/BloomPrefilterCS.hlsl`, `pipeline/Camera Suite/Module.ini`, and a comment-only clarification in `engine/Modules/CameraSuite.cpp`. No setting, GPU ABI, resource binding, pass, resource, or default changed.
