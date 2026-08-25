# PIXL PixDiT Enhance

This directory contains the reproducible research and export pipeline for PIXL
Renderer's optional **Photo Mode Enhance** pass.

The model is deliberately designed around PIXL's constraints:

- pure pixel-space RGB + linear-depth input (no VAE reconstruction);
- one student forward pass per 512x512 tile;
- local-window transformer attention with convolutional cross-window mixing;
- bounded residual output so the original PIXL image remains authoritative;
- portable ONNX export for DirectML on AMD/NVIDIA and optional CUDA/TensorRT;
- asynchronous, separate-file Photo Finish integration with failure fallback.

The synthetic dataset and resulting checkpoint are validation assets, not a
production-quality restoration model. They must not be enabled by default or
shipped as a finished visual model. Production training requires licensed,
representative PIXL RGB/depth capture pairs and independently reviewed targets.

`train_real_data.py` is the safe bridge from the downloaded MPI-Sintel and
TartanAir V2 RGB/depth material into PIXL's one-step student. Those datasets are
used only for auxiliary restoration and geometry pretraining: the intact frame
is the target and a restrained deterministic degradation is the input. They are
not treated as Skyrim remaster pairs. The protected PIXL base and visual target
screenshots are recorded as holdout references and never enter the optimizer.

If model training is new to you, start with
[`TRAINING_GUIDE.md`](TRAINING_GUIDE.md). It explains the dataset, commands,
metrics, GPU-memory limits, and production-capture workflow in plain language.

## Run

From the repository root, using the isolated environment created for this task:

```powershell
build\pixdit-env\Scripts\python.exe tools\PixDiTEnhance\run_full_pipeline_test.py
```

Outputs are written below `build/pixdit-validation` by default. The master
runner generates `BUILD_SUMMARY.md` in this directory from measured results.
To exercise the required cross-vendor path as part of the same run, create the
isolated environment in `requirements-directml.txt` and pass:

```powershell
--directml-python build\pixdit-dml-env\Scripts\python.exe
```

Do not install `onnxruntime` and `onnxruntime-directml` into the same virtual
environment; they provide the same Python package and can mask each other.

## Safety contract

- Never overwrite the original capture.
- Never run inference on the render thread.
- Disable or bypass the pass for unsupported HDR colour spaces.
- On load, inference, device, allocation, or validation failure, save the
  unmodified Photo Finish result.
- Treat DirectML as the portable baseline. CUDA/TensorRT is an optional backend,
  never a requirement for the feature.
