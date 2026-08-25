"""Run PIXL PixDiT dataset, teacher, distillation, export, and runtime tests."""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

from pixdit_common import sha256_file


SCRIPT_DIR = Path(__file__).resolve().parent
REPOSITORY_ROOT = SCRIPT_DIR.parents[1]


def run(command: list[str]) -> None:
    print("\n>", subprocess.list2cmdline(command), flush=True)
    subprocess.run(command, cwd=REPOSITORY_ROOT, check=True)


def run_training_with_batch_fallback(
    script: str,
    common_arguments: list[str],
    output_directory: Path,
    initial_batch_size: int,
) -> int:
    candidates: list[int] = []
    batch = max(1, initial_batch_size)
    while batch not in candidates:
        candidates.append(batch)
        if batch == 1:
            break
        batch = max(1, batch // 2)

    last_error: subprocess.CalledProcessError | None = None
    for candidate in candidates:
        if output_directory.exists():
            shutil.rmtree(output_directory)
        try:
            run([
                sys.executable,
                str(SCRIPT_DIR / script),
                *common_arguments,
                "--batch-size",
                str(candidate),
            ])
            return candidate
        except subprocess.CalledProcessError as error:
            last_error = error
            print(f"{script} failed at batch={candidate}; retrying with a smaller batch.", flush=True)
            if "cuda" in " ".join(common_arguments).lower():
                try:
                    import torch
                    torch.cuda.empty_cache()
                except Exception:
                    pass
    assert last_error is not None
    raise last_error


def read_json(path: Path) -> dict[str, object]:
    return json.loads(path.read_text(encoding="utf-8"))


def validate_acceptance_metrics(
    workspace: Path,
    directml_requested: bool,
) -> None:
    """Turn the Phase 6 claims into executable acceptance criteria."""
    teacher = read_json(workspace / "teacher" / "teacher_metrics.json")
    student = read_json(workspace / "student" / "student_metrics.json")
    export = read_json(workspace / "onnx" / "export_metrics.json")

    if not teacher["finite"] or not student["finite"]:
        raise AssertionError("Teacher/student training produced non-finite values")
    if teacher["final_training_loss"] >= teacher["initial_training_loss"]:
        raise AssertionError("Teacher did not converge below its initial training loss")
    if student["final_training_loss"] >= student["initial_training_loss"]:
        raise AssertionError("Student did not converge below its initial training loss")
    if student["student_inference_steps"] != 1:
        raise AssertionError("Distilled student is not a one-forward-pass model")
    if not 5_000_000 <= student["parameter_count"] <= 15_000_000:
        raise AssertionError("Student parameter count escaped the compact 5M-15M target")
    if student["student_validation_psnr_db"] < student["baseline_validation_psnr_db"]:
        raise AssertionError("Student failed to improve the synthetic raw validation baseline")
    if not export["all_shapes_verified"] or not export["all_outputs_finite"]:
        raise AssertionError("ONNX output shape/finite validation failed")
    if not export["fp16"].get("executed"):
        raise AssertionError("FP16 ONNX artifact was not executed")

    directml_files = sorted((workspace / "onnx").glob("directml_*_metrics.json"))
    if directml_requested:
        if len(directml_files) != 2:
            raise AssertionError("DirectML FP32 and FP16 verification results are required")
        precisions = set()
        for path in directml_files:
            metrics = read_json(path)
            if metrics["provider"] != "DmlExecutionProvider" or not metrics["finite"]:
                raise AssertionError(f"DirectML provider validation failed: {path}")
            if metrics["output_shape"] != [1, 3, export["resolution"], export["resolution"]]:
                raise AssertionError(f"DirectML output contract mismatch: {path}")
            precisions.add(metrics["model_precision"])
        if precisions != {"FP32", "FP16 weights / FP32 IO"}:
            raise AssertionError(f"Unexpected DirectML precision coverage: {precisions}")


def build_summary(
    workspace: Path,
    teacher_batch: int,
    student_batch: int,
    directml_requested: bool,
) -> str:
    teacher = read_json(workspace / "teacher" / "teacher_metrics.json")
    student = read_json(workspace / "student" / "student_metrics.json")
    export = read_json(workspace / "onnx" / "export_metrics.json")
    directml_files = sorted((workspace / "onnx").glob("directml_*_metrics.json"))
    directml = [read_json(path) for path in directml_files]

    artifacts = [
        workspace / "teacher" / "teacher.pt",
        workspace / "student" / "student_1step.pt",
        workspace / "onnx" / "pixdit_enhance_1step.onnx",
        workspace / "onnx" / "pixdit_enhance_1step_fp16.onnx",
        workspace / "onnx" / "pixdit_enhance_1step_int8_dynamic.onnx",
    ]
    artifact_rows = []
    for path in artifacts:
        artifact_rows.append(
            f"- `{path.relative_to(REPOSITORY_ROOT).as_posix()}` — "
            f"{path.stat().st_size:,} bytes — SHA-256 `{sha256_file(path)}`")

    provider_rows = [
        f"- FP32: `{export['fp32']['provider']}`, median "
        f"{export['fp32']['latency']['median_ms']:.3f} ms at "
        f"{export['resolution']}x{export['resolution']}."
    ]
    fp16 = export["fp16"]
    if fp16.get("executed"):
        provider_rows.append(
            f"- FP16: `{fp16['provider']}`, median {fp16['latency']['median_ms']:.3f} ms.")
    else:
        provider_rows.append(f"- FP16 graph checked; accelerated execution skipped: `{fp16.get('reason')}`")
    provider_rows.append(
        f"- INT8 dynamic: `{export['int8_dynamic']['provider']}`, median "
        f"{export['int8_dynamic']['latency']['median_ms']:.3f} ms.")
    for result in directml:
        provider_rows.append(
            f"- DirectML {result['model_precision']}: `{result['provider']}`, median "
            f"{result['median_ms']:.3f} ms, output `{result['output_shape']}`.")
    if directml_requested and not directml:
        provider_rows.append("- DirectML verification was requested but produced no metrics (FAIL).")

    generated_code = sorted(
        path.relative_to(REPOSITORY_ROOT).as_posix()
        for path in SCRIPT_DIR.rglob("*")
        if path.is_file() and "__pycache__" not in path.parts
    )
    generated_rows = "\n".join(f"- `{path}`" for path in generated_code)

    return f"""# PIXL PixDiT Enhance — Build Summary

Generated by `run_full_pipeline_test.py`. All numeric results below are measured
from this validation run.

## Outcome

- End-to-end result: **PASS**
- Student execution: **one forward pass; no iterative scheduler**
- Pixel-space input: `(1, 4, {export['resolution']}, {export['resolution']})` RGB + normalized depth
- Pixel-space output: `(1, 3, {export['resolution']}, {export['resolution']})` enhanced RGB
- Final parameter count: **{student['parameter_count']:,}**
- Teacher batch after automatic fallback: `{teacher_batch}`
- Student batch after automatic fallback: `{student_batch}`
- Exported ONNX opset: `{export['onnx_opset']}`
- Outputs finite and shape-verified: `{export['all_outputs_finite'] and export['all_shapes_verified']}`

## Training and Distillation

- Teacher epochs: `{teacher['epochs']}`; flow integration steps: `{teacher['integration_steps']}`
- Teacher initial/final train loss: `{teacher['initial_training_loss']:.6f}` / `{teacher['final_training_loss']:.6f}`
- Teacher best validation loss: `{teacher['best_validation_loss']:.6f}`
- Teacher validation PSNR: `{teacher['teacher_validation_psnr_db']:.3f}` dB (raw baseline `{teacher['baseline_validation_psnr_db']:.3f}` dB)
- Student epochs: `{student['epochs']}`; inference steps: `{student['student_inference_steps']}`
- Student initial/final train loss: `{student['initial_training_loss']:.6f}` / `{student['final_training_loss']:.6f}`
- Student best validation loss: `{student['best_validation_loss']:.6f}`
- Student validation PSNR: `{student['student_validation_psnr_db']:.3f}` dB (raw baseline `{student['baseline_validation_psnr_db']:.3f}` dB)
- Perceptual backend: `{student['perceptual_backend']}`
- NaN/Inf checks: teacher `{teacher['finite']}`, student `{student['finite']}`

## ONNX Runtime Measurements

{chr(10).join(provider_rows)}

Latency is a development-machine measurement, not a shipping performance claim.
DirectML is PIXL's required cross-vendor path; CUDA/TensorRT is optional.

## Verified Artifacts

{chr(10).join(artifact_rows)}

## Generated Source and Bridge Files

{generated_rows}

## PIXL Integration Safety

- The synthetic checkpoint is an architecture/export validation artifact only.
- It is not copied into `Data`, enabled in Photo Mode, or staged for release.
- Production integration must preserve the original capture and save enhancement
  to a separate image; failure returns the unmodified Photo Finish image.
- The bridge tiles arbitrary captures into overlapping 512-square RGBD tensors,
  blends seams, and dispatches asynchronously after PIXL's existing temporal
  reconstruction/lens pipeline.
- SDR sRGB is the validated colour contract. HDR capture remains bypassed until a
  separately trained scene-linear/HDR model is available.

## Dataset Scope

The dataset is deterministic, procedural, and license-clean. It proves code,
training stability, one-step distillation, export, and runtime compatibility; it
does **not** establish production photorealistic quality. A shipping checkpoint
requires licensed real PIXL captures, matching depth, curated targets, temporal
validation, and visual sign-off.
"""


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--workspace",
        type=Path,
        default=REPOSITORY_ROOT / "build" / "pixdit-validation",
    )
    parser.add_argument("--dataset-count", type=int, default=24)
    parser.add_argument("--training-resolution", type=int, default=64)
    parser.add_argument("--export-resolution", type=int, default=512)
    parser.add_argument("--teacher-epochs", type=int, default=50)
    parser.add_argument("--student-epochs", type=int, default=50)
    parser.add_argument("--batch-size", type=int, default=4)
    parser.add_argument("--teacher-steps", type=int, default=8)
    parser.add_argument("--device", default="auto")
    parser.add_argument("--directml-python", type=Path)
    parser.add_argument("--fresh", action="store_true")
    args = parser.parse_args()

    workspace = args.workspace.resolve()
    build_root = (REPOSITORY_ROOT / "build").resolve()
    if not workspace.is_relative_to(build_root):
        raise ValueError(f"Workspace must remain below {build_root}")
    if args.fresh and workspace.exists():
        shutil.rmtree(workspace)
    workspace.mkdir(parents=True, exist_ok=True)

    dataset = workspace / "dataset"
    teacher_output = workspace / "teacher"
    student_output = workspace / "student"
    onnx_output = workspace / "onnx"

    run([
        sys.executable,
        str(SCRIPT_DIR / "generate_synthetic_data.py"),
        "--output", str(dataset),
        "--count", str(args.dataset_count),
        "--size", str(args.training_resolution),
        "--seed", "1337",
    ])

    teacher_batch = run_training_with_batch_fallback(
        "train_teacher.py",
        [
            "--data", str(dataset),
            "--output", str(teacher_output),
            "--epochs", str(args.teacher_epochs),
            "--integration-steps", str(args.teacher_steps),
            "--device", args.device,
        ],
        teacher_output,
        args.batch_size,
    )
    student_batch = run_training_with_batch_fallback(
        "distill_student.py",
        [
            "--data", str(dataset),
            "--teacher", str(teacher_output / "teacher.pt"),
            "--output", str(student_output),
            "--epochs", str(args.student_epochs),
            "--teacher-steps", str(args.teacher_steps),
            "--device", args.device,
        ],
        student_output,
        args.batch_size,
    )

    run([
        sys.executable,
        str(SCRIPT_DIR / "export_and_test_onnx.py"),
        "--student", str(student_output / "student_1step.pt"),
        "--output", str(onnx_output),
        "--resolution", str(args.export_resolution),
        "--provider", "auto",
    ])

    if args.directml_python:
        directml_python = args.directml_python.resolve()
        if not directml_python.is_file():
            raise FileNotFoundError(f"DirectML Python interpreter is missing: {directml_python}")
        for suffix, model_name in (
            ("fp32", "pixdit_enhance_1step.onnx"),
            ("fp16", "pixdit_enhance_1step_fp16.onnx"),
        ):
            run([
                str(directml_python),
                str(SCRIPT_DIR / "verify_onnx_provider.py"),
                "--model", str(onnx_output / model_name),
                "--provider", "DmlExecutionProvider",
                "--resolution", str(args.export_resolution),
                "--output", str(onnx_output / f"directml_{suffix}_metrics.json"),
            ])

    validate_acceptance_metrics(
        workspace,
        directml_requested=args.directml_python is not None,
    )
    summary = build_summary(
        workspace,
        teacher_batch,
        student_batch,
        directml_requested=args.directml_python is not None,
    )
    summary_path = SCRIPT_DIR / "BUILD_SUMMARY.md"
    summary_path.write_text(summary, encoding="utf-8")
    (workspace / "BUILD_SUMMARY.md").write_text(summary, encoding="utf-8")
    print(f"\nAll PixDiT phases passed. Summary: {summary_path}")


if __name__ == "__main__":
    main()
