# PIXL PixDiT Photo Mode Enhance - Validation Model Card

## Status

This is a **synthetic engineering-validation model**, not a production visual
checkpoint. It validates the PIXL-specific architecture, training stability,
single-pass distillation, ONNX exports, and AMD/NVIDIA DirectML contract. It is
not installed under `Data`, enabled in Photo Mode, or copied to release staging.

## Architecture

- Pixel-space conditional transformer; no VAE.
- Input: one float32 NCHW RGBD tile `(1,4,512,512)`.
- Output: one float32 NCHW RGB tile `(1,3,512,512)`.
- Four-pixel patchification, local 8x8-token attention, depthwise cross-window
  mixing, and a bounded residual output.
- Teacher: conditional flow matching with multi-step Euler integration.
- Student: exactly one forward invocation with a fixed distilled timestep.
- Size: 7,689,731 learned parameters.

## Data and provenance

The validation corpus is created entirely by `generate_synthetic_data.py` from
deterministic shapes, gradients, procedural noise, and image operations. It does
not download, scrape, trace, or embed third-party images. The generated corpus
simulates medieval structures, warm windows, terrain, foliage, haze, linear
depth, and a deliberately degraded raw game render.

No claim is made that this corpus represents final Skyrim or PIXL output. A
production checkpoint needs a separately documented, licensed dataset of PIXL
captures with co-registered depth, curated target policy, held-out locations,
character/skin coverage, weather/time-of-day coverage, and temporal tests.

## Runtime portability

DirectML is the required Windows backend and consumes the same FP16 ONNX model
on supported AMD, NVIDIA, and Intel adapters. CUDA/TensorRT is an optional NVIDIA
optimization, never the generic runtime. The validation suite executes FP32 and
FP16 via `DmlExecutionProvider`; physical AMD hardware remains a release-test
requirement because the development machine currently has an NVIDIA adapter.

## Safety and limitations

- Disabled by default until production weights pass visual acceptance.
- SDR sRGB only; HDR must bypass this checkpoint.
- The original Photo Finish capture is immutable and saved independently.
- Enhancement runs after PIXL reconstruction/lens work and off the render thread.
- Any model/hash/provider/allocation/inference/finite-output failure returns the
  original image.
- Arbitrary resolutions require overlapping 512x512 RGBD tiles and seam-aware
  accumulation as specified in `hlsl_bridge_spec.md`.
- Synthetic validation metrics are pipeline checks, not perceptual-quality or
  shipping-performance claims.

Measured results and artifact hashes are in `BUILD_SUMMARY.md`.
