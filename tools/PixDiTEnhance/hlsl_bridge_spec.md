# PIXL Photo Mode Enhance — D3D11 / DirectML Bridge Specification

Status: developer integration contract for the synthetic PixDiT validation
checkpoint. The checkpoint is not production visual content and must remain
disabled outside developer builds until real PIXL training/validation is complete.

## Goals

- Add a one-step neural finishing option to Director Photo Finish.
- Support AMD and NVIDIA through the same DirectML ONNX graph.
- Preserve CUDA/TensorRT as an optional NVIDIA acceleration path.
- Never mutate the live frame, block the render thread, or overwrite the original
  capture.
- Fail closed: any unavailable backend, model mismatch, allocation failure,
  inference error, or invalid output saves the original Photo Finish image.

## Validated model contract

| Item | Contract |
|---|---|
| Model | `PIXL.PixDiT.Student1Step.ONNX.v1` |
| Input | `rgbd`, float32 NCHW `(1,4,512,512)` |
| RGB | sRGB/display-referred, normalized `[0,1]` |
| Depth | linear view depth normalized to `[0,1]`; 0 near, 1 far/invalid |
| Output | `enhanced_rgb`, float32 NCHW `(1,3,512,512)` |
| Iterations | exactly one model invocation per tile |
| Model behavior | bounded residual around input RGB |
| Primary runtime | ONNX Runtime DirectML or direct DirectML graph execution |
| Optional runtime | CUDA/TensorRT using the same FP16 ONNX artifact |

The validation checkpoint supports SDR only. PIXL HDR PNG captures must bypass
the pass until a separately trained scene-linear/HDR checkpoint exists. Applying
an SDR model to PQ or scene-linear values is a colour-management defect.

## Integration point in PIXL Renderer

`PixelCapture::ScreenshotWorkerLoop` already calls `BuildPhotoFinishImage` off the
render thread. Neural enhancement belongs after:

1. temporal resolve;
2. optional motion finish;
3. jitter-aware super resolution;
4. bounded detail reconstruction;
5. PIXL Photo Lens depth of field;

and before `SaveScreenshotToDisk`.

The worker retains `finalWorking` as the immutable fallback. The enhancer writes
to a second `DirectX::ScratchImage` or GPU texture. Only a completely successful,
finite, correctly shaped result may become the `_PIXL-NEURAL` output.

Recommended file behavior:

- save the normal Photo Finish path unconditionally;
- when enabled and successful, save a sibling such as
  `PIXL_Photo_0001_NEURAL.png`;
- optionally put the enhanced path on the clipboard, but never delete or replace
  the original;
- include model SHA-256, backend, tile count, blend, and elapsed time in the log.

## Cross-vendor backend selection

1. **DirectML** — required shipping baseline. DirectML uses DirectX 12 and runs on
   supported AMD, NVIDIA, and Intel adapters. Select the same adapter LUID as the
   Skyrim D3D11 device.
2. **TensorRT/CUDA** — optional explicit NVIDIA preference. It must consume the
   same ONNX input/output contract and must not change image results beyond the
   documented FP16 tolerance.
3. **CPU** — developer/test fallback only. It is unsuitable for interactive Photo
   Finish latency but useful for graph verification.
4. **Disabled** — immediate passthrough when no valid provider exists.

Do not label CUDA as the generic path. AMD support is provided by DirectML, not a
CUDA emulation layer.

## D3D11 to DirectML resource flow

DirectML executes on D3D12. PIXL already owns DX12 interop infrastructure for
image reconstruction; reuse that adapter/device selection rather than creating a
second unrelated GPU stack.

1. Copy the finished SDR `ID3D11Texture2D` and linear WorkingDepth into immutable
   staging resources after the render-thread capture copy is complete.
2. For the first implementation, perform CPU packing on the existing screenshot
   worker into a contiguous float32 NCHW tile. This is slower but isolated and
   simple to validate.
3. The production fast path should use shared NT handles
   (`D3D11_RESOURCE_MISC_SHARED_NTHANDLE`) and open the resources on PIXL's D3D12
   device. Synchronize with an explicit fence; never infer completion from frame
   count.
4. Convert packed RGB to the model's sRGB `[0,1]` contract. Normalize linear view
   depth using the checkpoint metadata, for example:
   `d = saturate(log2(1 + viewDepth) / log2(1 + depthFar))`.
   Sky/invalid/non-finite depth is `1`.
5. Bind input/output tensors with fixed dimensions. DirectML tensor strides are
   contiguous NCHW: `{4*512*512, 512*512, 512, 1}` for input.
6. Dispatch once per tile. No diffusion scheduler is present at runtime.
7. Validate output dimensions and reject any NaN/Inf or unreasonable range before
   it reaches the compositor.
8. Composite into a separate texture with `PixDiT_Enhance_Shader.hlsl`.

## Arbitrary-resolution tiling

The exported graph is fixed at 512x512 to keep DirectML compilation and memory
predictable.

- Pad the image by reflection to cover 512-square tiles.
- Use 64-pixel overlap (effective stride 384) for production quality.
- Keep RGB and depth tiles exactly co-registered.
- Accumulate enhanced tiles with a separable raised-cosine/Hann weight.
- Normalize by accumulated weight after all tiles.
- Crop to the original Photo Finish dimensions.
- Batch size remains 1 on 8 GB GPUs. Larger adapters may batch tiles only after
  measuring memory and proving equivalent output.

For 2x/4x Photo Finish captures, run the model at the final reconstructed output
resolution. The network enhances; it does not replace PIXL's jitter-aware super
resolution.

## Blend and safety controls

Recommended developer defaults:

| Setting | Default | Range |
|---|---:|---:|
| Enable neural finish | false | boolean |
| Backend | DirectML | DirectML / CUDA-TensorRT / Auto |
| Blend | 0.35 | 0..1 |
| Maximum per-channel residual | 0.10 | 0.02..0.20 |
| Highlight protection | 0.75 | 0..1 |
| Shadow protection | 0.35 | 0..1 |
| Tile overlap | 64 | 32..128 |

The compositor clamps the model residual again even though the student already
has a bounded head. This is intentional defense in depth against incompatible or
corrupt model files.

## Model lifecycle and provenance

- Store production models under `Data/Models/PIXL/PixDiT/`, not `Data/Shaders`.
- Validate the embedded `pixl.format` metadata and a release-manifest SHA-256.
- Never silently download weights during gameplay.
- Document every training corpus and target-generation licence.
- The generated synthetic checkpoint in `build/pixdit-validation` proves the
  pipeline only and must not be promoted to beta as production visual weights.

## Runtime state machine

1. `Disabled` — no model/session allocation.
2. `Loading` — background model validation and provider compilation.
3. `Ready` — session cached for the current adapter LUID.
4. `Processing` — worker owns capture tiles; UI reports `Neural Enhance` progress.
5. `Complete` — enhanced sibling saved; original already safe.
6. `Fallback` — warning logged once, original saved, session disabled for the
   remainder of the process if the failure indicates model/device incompatibility.

Device reset, adapter change, model hash change, or renderer shutdown destroys
the session only after outstanding worker work is joined.

## Acceptance tests before live enablement

- DirectML FP16 inference on both a physical AMD adapter and a physical NVIDIA
  adapter.
- Exact output shape and finite/range checks for every tile.
- Seam test with high-contrast diagonals crossing tile boundaries.
- SDR colour round-trip and ICC/sRGB metadata preservation.
- Original-capture preservation under forced model/provider/allocation failures.
- Photo Finish 1x/2x/4x, crop, Photo Lens DOF, motion finish, and clipboard paths.
- Dialogue skin, foliage, fine hair, snow, windows, water, UI-free night scenes,
  and DLAA/DLSS source captures.
- No render-thread stall and no live-frame GPU resource mutation.
- Memory budget test on an 8 GB adapter.

