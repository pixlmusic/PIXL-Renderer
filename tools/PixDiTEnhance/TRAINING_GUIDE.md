# Training PIXL PixDiT: Beginner's Guide

This guide explains how to train PIXL's Photo Mode Enhance model without
assuming previous machine-learning experience.

## What training actually does

The model learns from matching sets of three images:

1. **Raw RGB** - the normal PIXL/game image that needs enhancement.
2. **Target RGB** - the cleaner reference result we want the model to approach.
3. **Depth** - the same frame's distance information, where `0` is near and `1`
   is far away or sky.

Every pixel must describe the same camera, geometry, animation, weather and
moment. If the target has moved objects or a different camera, the model learns
ghosts and hallucinations instead of restoration.

Training happens in two stages:

- The **teacher** learns a multi-step flow from raw image to target.
- The **student** learns to reproduce that result in exactly one forward pass.

Only the distilled student is intended for Photo Mode inference.

The new real-data pretraining path trains that same one-step student directly on
restoration tasks before true PIXL pairs are available. This does not replace
the teacher/distillation route for later production refinement; it gives the
student useful RGB/depth structure using the material already downloaded.

## Try the safe synthetic exercise first

The repository already contains a working Python environment and validation
pipeline. From the repository root, run:

```powershell
build\pixdit-env\Scripts\python.exe tools\PixDiTEnhance\run_full_pipeline_test.py `
  --workspace build\pixdit-tutorial `
  --fresh `
  --teacher-epochs 50 `
  --student-epochs 50 `
  --device cuda `
  --directml-python build\pixdit-dml-env\Scripts\python.exe
```

This generates procedural training images, trains both networks, exports ONNX,
and tests CUDA, CPU and DirectML. `--fresh` deletes only the named workspace
below the repository's `build` directory.

The useful outputs are:

```text
build/pixdit-tutorial/
  teacher/teacher.pt
  student/student_1step.pt
  onnx/pixdit_enhance_1step.onnx
  onnx/pixdit_enhance_1step_fp16.onnx
  onnx/pixdit_enhance_1step_int8_dynamic.onnx
  BUILD_SUMMARY.md
```

This exercise proves that everything works; it does not create production
quality because its images are simple procedural scenes.

## Use the downloaded Sintel and TartanAir material

The current dataset root is expected to contain:

```text
H:\sintel training data\
  MPI-Sintel-training_images.zip
  MPI-Sintel-depth-training-20150305.zip
  TartanAirV2\
    AncientTowns\Data_easy\P000\image_lcam_front\...
    AncientTowns\Data_easy\P000\depth_lcam_front\...
    ...
```

Sintel stays inside its ZIP files. TartanAir's RGBA depth PNG bytes are decoded
as the dataset's little-endian float depth values and then robustly normalized.
Training/validation separation is by whole Sintel scene or TartanAir trajectory,
not adjacent frame, to prevent leakage.

First perform the read-only inventory:

```powershell
build\pixdit-env\Scripts\python.exe tools\PixDiTEnhance\train_real_data.py `
  --data-root "H:\sintel training data" `
  --output build\pixdit-real-inventory `
  --inventory-only `
  --tartanair-stride 8 `
  --reference "C:\Users\PIXL STUDIO PC\Desktop\CS_2026-08-24_23-25-41_352.png" `
  --reference "C:\Users\PIXL STUDIO PC\Desktop\8c3566e3-263c-4349-86d0-9432c9a325ab.png"
```

The two reference images are hashed into the inventory as visual holdouts. They
are not a pixel-aligned pair and are never loaded by the training dataset.

After reviewing the Sintel and TartanAir terms for this local experiment, run a
bounded pilot at 256x256:

```powershell
build\pixdit-env\Scripts\python.exe tools\PixDiTEnhance\train_real_data.py `
  --data-root "H:\sintel training data" `
  --output build\pixdit-real-pilot `
  --resolution 256 `
  --epochs 20 `
  --batch-size 1 `
  --gradient-accumulation 4 `
  --max-samples 1800 `
  --tartanair-stride 8 `
  --device cuda `
  --accept-external-dataset-terms `
  --reference "C:\Users\PIXL STUDIO PC\Desktop\CS_2026-08-24_23-25-41_352.png" `
  --reference "C:\Users\PIXL STUDIO PC\Desktop\8c3566e3-263c-4349-86d0-9432c9a325ab.png"
```

The terms flag records that the operator reviewed the external dataset terms; it
does not grant permission to redistribute the data or trained weights. Start
with this capped pilot rather than all 8,389 indexed frames. Inspect
`preview_raw.png`, `preview_enhanced.png`, `preview_target.png`,
`dataset_inventory.json`, and `real_data_metrics.json` before increasing epochs
or sample count.

The checkpoint is `student_1step_real.pt`. It is still a pretraining checkpoint,
not a release asset. Export it for compatibility testing with:

```powershell
build\pixdit-env\Scripts\python.exe tools\PixDiTEnhance\export_and_test_onnx.py `
  --student build\pixdit-real-pilot\student_1step_real.pt `
  --output build\pixdit-real-pilot\onnx `
  --resolution 512 `
  --provider auto
```

## Creating a real PIXL dataset

### Recommended capture pair

For each scene, capture the exact same frozen frame twice:

- **Raw:** the gameplay-quality configuration the enhancer will normally receive.
- **Target:** native resolution or DLAA, maximum relevant PIXL quality, high
  Photo Finish sampling, optionally captured at 2x/4x and carefully downsampled.

Keep camera position, field of view, exposure, time, weather, animation and all
scene objects unchanged. Capture depth from the raw frame at the same time.

The target should recover legitimate detail, stable edges, material response and
lighting. It should not redesign faces, invent architecture, change the colour
grade dramatically, or replace the image with unrelated generative artwork.

PIXL does not yet have a finished aligned raw/target RGB/depth capture utility.
That capture/export path is the next required engineering step before the
auxiliary checkpoint can be refined into a serious production model. Ordinary
screenshots alone do not contain the co-registered linear depth channel.

### What scenes to include

Use broad, balanced coverage:

- exterior and interior locations;
- daylight, night, firelight and mixed illumination;
- clear, rain, fog and snow;
- faces, skin tones, hair, armour and clothing;
- vegetation, water, windows, terrain and architecture;
- close, middle and far distances;
- DLAA and the DLSS modes the production feature will accept.

Avoid hundreds of nearly identical adjacent frames. Split validation data by
entire location/capture session so neighbouring frames cannot appear in both
training and validation. A useful pilot may start with a few hundred carefully
aligned pairs; a robust production corpus will normally need thousands of varied
tiles. Quality and variety matter more than raw screenshot count.

Only use captures and target-generation material that PIXL has permission to use
for model training and distribution. Record the game/mod configuration, asset
provenance and target process for every capture session.

## Dataset layout

All RGB and depth files for a run must have matching dimensions. Dimensions must
be divisible by 32. Production training should ultimately use 512x512 aligned
tiles; start at 128 or 256 while testing memory and data correctness.

```text
PIXL-PixDiT-Dataset/
  manifest.json
  raw/
    capture_000001.png
  target/
    capture_000001.png
  depth/
    capture_000001.png
```

RGB files are ordinary 8-bit sRGB PNGs. Depth files are 16-bit grayscale PNGs,
normalized so `0` is the near plane and `65535` is far/sky.

A minimal manifest looks like this:

```json
{
  "format": "PIXL.PixDiT.PairedCaptures.v1",
  "license": "See dataset provenance record",
  "rgbSpace": "sRGB normalized to [0,1]",
  "depthConvention": "0 near, 1 far/sky; 16-bit PNG",
  "size": 512,
  "samples": [
    {
      "raw": "raw/capture_000001.png",
      "target": "target/capture_000001.png",
      "depth": "depth/capture_000001.png",
      "split": "train"
    },
    {
      "raw": "raw/capture_000900.png",
      "target": "target/capture_000900.png",
      "depth": "depth/capture_000900.png",
      "split": "val"
    }
  ]
}
```

Keep roughly 15-20% of locations as validation data. Never tune the model by
silently moving difficult validation images back into training.

## Training a real dataset

Use a separate output folder so the validated synthetic artifacts remain intact.
On the current RTX 3060 Ti, begin with batch size 1 for 512x512 tiles:

```powershell
build\pixdit-env\Scripts\python.exe tools\PixDiTEnhance\train_teacher.py `
  --data "D:\PIXL-PixDiT-Dataset" `
  --output build\pixdit-production\teacher `
  --epochs 50 `
  --batch-size 1 `
  --integration-steps 8 `
  --device cuda
```

Then distil the best saved teacher:

```powershell
build\pixdit-env\Scripts\python.exe tools\PixDiTEnhance\distill_student.py `
  --data "D:\PIXL-PixDiT-Dataset" `
  --teacher build\pixdit-production\teacher\teacher.pt `
  --output build\pixdit-production\student `
  --epochs 50 `
  --batch-size 1 `
  --teacher-steps 8 `
  --device cuda
```

Finally export and verify the one-step model:

```powershell
build\pixdit-env\Scripts\python.exe tools\PixDiTEnhance\export_and_test_onnx.py `
  --student build\pixdit-production\student\student_1step.pt `
  --output build\pixdit-production\onnx `
  --resolution 512 `
  --provider auto

build\pixdit-dml-env\Scripts\python.exe tools\PixDiTEnhance\verify_onnx_provider.py `
  --model build\pixdit-production\onnx\pixdit_enhance_1step_fp16.onnx `
  --provider DmlExecutionProvider `
  --resolution 512 `
  --output build\pixdit-production\onnx\directml_fp16_metrics.json
```

If CUDA runs out of memory, reduce batch size before changing the architecture.
If batch size is already 1, pilot with 256x256 tiles or add gradient accumulation
in a subsequent training-tool revision. Do not lower production image quality or
remove protected PIXL features merely to make training fit.

## Reading the results

Healthy training normally shows:

- training loss trending downward;
- validation loss improving and then levelling out;
- enhanced validation PSNR exceeding the raw baseline;
- no `NaN`, `Inf`, sudden colour shifts, doubled edges or invented structures;
- useful improvement on locations never seen during training.

Stop and investigate if training loss falls while validation becomes steadily
worse. That is overfitting. More epochs are not automatically better; the scripts
save the checkpoint with the best validation loss.

PSNR and loss are engineering signals, not final artistic judgment. Always review
held-out screenshots at 100% and compare faces, foliage, windows, fine geometry,
fog, particle edges, depth discontinuities and DLSS/DLAA stability.

## Before a model can ship

- Dataset provenance and permissions are documented.
- Held-out locations demonstrate a real visual improvement.
- Original Photo Finish images are always preserved.
- SDR/HDR colour contracts are separately trained and validated.
- FP16 DirectML is tested on physical AMD and NVIDIA hardware.
- Tiled high-resolution output has no seams.
- Failure, cancellation and low-memory paths return the untouched original.
- The model hash and metadata match the release manifest.
- The feature remains opt-in until visual acceptance is complete.

See `MODEL_CARD.md`, `hlsl_bridge_spec.md`, and `BUILD_SUMMARY.md` for the current
architecture, integration contract and measured validation results.
