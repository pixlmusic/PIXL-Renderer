# Neural Rendering Mask and Terrain Audit — 2026-09-02

## Scope

This audit traces the installed NVIDIA DLSS Neural Rendering 310.8 Feature 18 integration from Skyrim's D3D11 scene through PIXL's DX12 sidecar. It focuses on what the runtime calls its automatic mask, how terrain is represented, and which Skyrim-specific adaptations are safe for the release path.

## Actual PIXL → NR Contract

```text
Skyrim opaque, alpha-tested, water and effect passes
                         |
                  deferred composite
                         |
       final depth + current/previous geometry motion
                         |
                 EncodeTexturesCS
          - bounded reactive/current bias
          - water transparency hint
          - depth-aware silhouette velocity
                         |
                  normal DLSS SR
                         |
       CameraSuite SDR final scene / fake swap chain
                         |
          D3D11-to-D3D12 shared guide copies
                         |
     DLSSNR.Color + DLSSNR.Depth + DLSSNR.MVec
                         |
                  Feature 18 output
```

`NeuralRendering.cpp` submits four documented-by-observation Feature 18 resources: display-resolution color, render-resolution depth subrect, render-resolution motion-vector subrect, and display-resolution output. It also supplies input/output extents, normalized-motion scaling, reset state, intensity, local tone, local structure, skin structure, style, automatic-mask state and UI correction.

## What the Automatic Mask Is

`DLSSNR.UseAutoMask` enables the runtime's learned **character mask**. The disabled RenoDX controller installed alongside the runtime describes the same control as a learned character mask and states that `SkinStructureStrength` operates inside it. PIXL does not create, read back, or post-process this mask.

Consequences:

- it is not a terrain/material segmentation mask;
- `SkinStructureStrength` is character-local when automatic masking succeeds;
- `LocalToneStrength` and `LocalStructureStrength` remain global and therefore affect terrain, architecture, foliage, equipment and characters;
- PIXL cannot guarantee the learned classifier's behavior on unusual Skyrim races, creatures, armor or heavily modded bodies.

## How Terrain Reaches NR

Feature 18 receives no PIXL terrain mesh, height map, landscape layer IDs, material IDs, normals, albedo or PBR classification. Terrain reaches the model as:

1. its already-lit pixels in `DLSSNR.Color`;
2. its device-depth samples in `DLSSNR.Depth`;
3. its current-to-previous screen motion in `DLSSNR.MVec`.

The installed binary contains private `DLSSNR.ControlMask` parameter names. Its channel meaning, format, normalization, feature-creation requirements and interaction with `UseAutoMask` are not published. Supplying an inferred terrain mask would therefore be an ABI guess capable of misclassifying world pixels or failing Feature 18. This is classified **C — Experimental** and is not enabled in the release path.

## Defect Found in the Inherited Guide Conditioning

The inherited DLSS motion conditioning selected the longest vector from any closer sample in a 5×5 neighborhood using non-linear device depth. It then blended back to the original vector in the near field but trusted the dilated result increasingly at distance.

That policy is poorly matched to Skyrim:

- far exterior terrain occupies a very compressed device-depth range;
- adjacent pixels on slopes can compare as different surfaces despite belonging to one stable landscape;
- the longest-vector policy can import actor, foliage or silhouette velocity into terrain;
- Feature 18 interprets this unstable guide as world motion and may reconstruct different local structure while the camera moves.

## Release-Safe Adaptation Implemented

`EncodeTexturesCS.hlsl` now:

- converts candidate depths to PIXL linear world depth before accepting dilation;
- requires an absolute and distance-relative separation, suppressing far-terrain depth quantization noise;
- chooses the nearest true foreground surface rather than the numerically longest motion vector;
- dilates only across validated foreground/background silhouettes, including geometry against sky;
- adds a restrained current-color bias only when the foreground and background motion disagree;
- leaves static depth edges temporally stable;
- saturates the reactive and water transparency outputs before their R8 writes.

This improves both ordinary DLSS and NR because Feature 18 consumes the post-encoded DLSS motion guide after normal reconstruction.

## Existing Skyrim-Specific World Signals

The current masks already cover useful categories without inventing private inputs:

- Skyrim's temporal-AA mask supplies the base current-color bias used by DLSS;
- the water stencil writes water coverage into the normal/TAA/SSR target and becomes the transparency hint;
- skinned geometry, grass, effects and sky write motion through their existing PIXL/Skyrim shader paths;
- camera cuts, loading transitions, FOV changes and dynamic-resolution changes reset DLSSNR history;
- Feature 18 receives the active render subrect dimensions instead of incorrectly treating the full texture allocation as valid geometry.

Coverage is not complete. Particle, animated-UV, volumetric and procedural surface motion remains limited by what the originating Skyrim pass writes. These are future per-material motion-confidence tasks, not reasons to fabricate a private Feature 18 mask.

## Future Controlled Experiment

A future developer build may evaluate `DLSSNR.ControlMask` only after obtaining a supported parameter contract or constructing a guarded A/B harness that can prove:

- accepted resource format and channel semantics;
- deterministic behavior with automatic masking on and off;
- correct behavior across interiors, exteriors, actors, water, foliage and sky;
- no runtime fault on the validated 310.8 DLL;
- no regression to the normal-DLSS fallback.

If the contract becomes known, PIXL's best candidate would be a compact confidence/classification guide derived from existing depth discontinuity, motion disagreement, water, alpha/effect and material metadata—not a hard-coded list of Skyrim terrain texture names.

## Validation

- DLSS `EncodeTexturesCS` permutation: FXC shader model 5.0 compile PASS.
- FSR + typed-depth `EncodeTexturesCS` permutation: FXC shader model 5.0 compile PASS.
- No new texture, UAV, SRV or constant-buffer register was introduced.
- Feature 18's private resource contract was not expanded.
- Runtime visual validation is still required for fast camera rotation, distant terrain, actor silhouettes, grass, water and sky boundaries.

