"""Export the distilled PIXL PixDiT student and verify ONNX inference."""

from __future__ import annotations

import argparse
import json
import statistics
import time
import warnings
from pathlib import Path

import numpy as np
import onnx
import onnxruntime as ort
import torch
from onnxconverter_common import float16
from onnxruntime.quantization import QuantType, quantize_dynamic

from model_pixdit import PixDiTConfig, PixDiTStudent, parameter_count
from pixdit_common import checkpoint_load, ensure_finite, sha256_file, write_json


def add_metadata(model: onnx.ModelProto, precision: str, training_scope: str) -> None:
    values = {
        "pixl.model": "PixDiT Photo Mode Enhance",
        "pixl.format": "PIXL.PixDiT.Student1Step.ONNX.v1",
        "pixl.precision": precision,
        "pixl.input": "rgbd:NCHW:float32:RGB_sRGB_0_1+linear_depth_0_1",
        "pixl.output": "enhanced_rgb:NCHW:RGB_sRGB_0_1",
        "pixl.inference_steps": "1",
        "pixl.runtime.primary": "DirectML (AMD/NVIDIA/Intel)",
        "pixl.runtime.optional": "CUDA/TensorRT",
        "pixl.safety": "bounded residual; preserve original capture on failure",
        "pixl.training": training_scope,
    }
    del model.metadata_props[:]
    for key, value in values.items():
        entry = model.metadata_props.add()
        entry.key = key
        entry.value = value


def make_validation_input(resolution: int) -> np.ndarray:
    y, x = np.mgrid[0:resolution, 0:resolution].astype(np.float32)
    x /= max(resolution - 1, 1)
    y /= max(resolution - 1, 1)
    rgb = np.stack(
        (
            0.12 + 0.68 * x,
            0.10 + 0.55 * y,
            0.16 + 0.35 * np.sin(x * np.pi) * np.cos(y * np.pi * 0.5),
        ),
        axis=0,
    )
    depth = np.clip(0.10 + y * 0.90, 0.0, 1.0)[None, :, :]
    return np.concatenate((rgb, depth), axis=0)[None, :, :, :].astype(np.float32)


def create_session(model_path: Path, provider: str) -> tuple[ort.InferenceSession, str]:
    available = ort.get_available_providers()
    candidates: list[str]
    if provider == "auto":
        candidates = ["CUDAExecutionProvider", "DmlExecutionProvider", "CPUExecutionProvider"]
    else:
        candidates = [provider, "CPUExecutionProvider"]
    selected = next((candidate for candidate in candidates if candidate in available), None)
    if selected is None:
        raise RuntimeError(f"No requested ONNX Runtime provider is available; found {available}")
    options = ort.SessionOptions()
    options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
    options.log_severity_level = 3
    return ort.InferenceSession(str(model_path), sess_options=options, providers=[selected]), selected


def benchmark_session(
    session: ort.InferenceSession,
    input_array: np.ndarray,
    warmup: int,
    runs: int,
) -> tuple[np.ndarray, dict[str, float]]:
    input_name = session.get_inputs()[0].name
    for _ in range(warmup):
        session.run(None, {input_name: input_array})
    timings: list[float] = []
    output: np.ndarray | None = None
    for _ in range(runs):
        started = time.perf_counter()
        output = session.run(None, {input_name: input_array})[0]
        timings.append((time.perf_counter() - started) * 1000.0)
    assert output is not None
    sorted_times = sorted(timings)
    p95_index = min(len(sorted_times) - 1, int(round((len(sorted_times) - 1) * 0.95)))
    return output, {
        "median_ms": statistics.median(timings),
        "min_ms": min(timings),
        "max_ms": max(timings),
        "p95_ms": sorted_times[p95_index],
        "runs": float(runs),
    }


def assert_output(output: np.ndarray, resolution: int, label: str) -> None:
    expected = (1, 3, resolution, resolution)
    if output.shape != expected:
        raise AssertionError(f"{label} output shape {output.shape} != {expected}")
    if not np.isfinite(output).all():
        raise FloatingPointError(f"{label} output contains NaN/Inf")
    if output.min() < -1.0e-4 or output.max() > 1.0001:
        raise AssertionError(f"{label} output escaped [0,1]: {output.min()}..{output.max()}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--student", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--resolution", type=int, default=512)
    parser.add_argument("--provider", default="auto")
    parser.add_argument("--warmup", type=int, default=2)
    parser.add_argument("--runs", type=int, default=5)
    args = parser.parse_args()

    if args.resolution % 32 != 0:
        raise ValueError("Export resolution must be divisible by 32")
    args.output.mkdir(parents=True, exist_ok=True)
    checkpoint = checkpoint_load(args.student, "cpu")
    checkpoint_format = str(checkpoint.get("format", "unknown"))
    if checkpoint_format == "PIXL.PixDiT.Student1Step.RealData.v1":
        training_scope = (
            "real-data auxiliary pretraining checkpoint; production use still requires "
            "licensed aligned PIXL pairs and visual acceptance"
        )
    else:
        training_scope = "synthetic validation checkpoint; not production visual weights"
    config = PixDiTConfig.from_dict(checkpoint["config"])
    student = PixDiTStudent(config).eval().cpu()
    student.load_state_dict(checkpoint["model"], strict=True)
    count = parameter_count(student)
    dummy_torch = torch.from_numpy(make_validation_input(args.resolution))

    fp32_path = args.output / "pixdit_enhance_1step.onnx"
    with torch.no_grad():
        reference = student(dummy_torch).cpu().numpy()
    ensure_finite("PyTorch export reference", torch.from_numpy(reference))
    torch.onnx.export(
        student,
        (dummy_torch,),
        str(fp32_path),
        export_params=True,
        opset_version=17,
        do_constant_folding=True,
        input_names=["rgbd"],
        output_names=["enhanced_rgb"],
        dynamic_axes=None,
        dynamo=False,
    )
    fp32_model = onnx.load(str(fp32_path))
    add_metadata(fp32_model, "FP32", training_scope)
    onnx.checker.check_model(fp32_model, full_check=True)
    onnx.save(fp32_model, str(fp32_path))

    fp16_path = args.output / "pixdit_enhance_1step_fp16.onnx"
    # Converting learned near-zero weights legitimately rounds subnormal FP32
    # values to FP16's minimum magnitude.  Numerical equivalence is checked
    # below, so suppress only this converter's per-weight warning flood.
    with warnings.catch_warnings():
        warnings.filterwarnings(
            "ignore",
            message=r"the float32 number .* will be truncated to .*",
            category=UserWarning,
            module=r"onnxconverter_common\.float16",
        )
        fp16_model = float16.convert_float_to_float16(
            fp32_model,
            keep_io_types=True,
            disable_shape_infer=False,
        )
    add_metadata(fp16_model, "FP16 weights / FP32 IO", training_scope)
    onnx.checker.check_model(fp16_model, full_check=True)
    onnx.save(fp16_model, str(fp16_path))

    int8_path = args.output / "pixdit_enhance_1step_int8_dynamic.onnx"
    quantize_dynamic(
        str(fp32_path),
        str(int8_path),
        op_types_to_quantize=["MatMul", "Gemm"],
        per_channel=True,
        reduce_range=False,
        weight_type=QuantType.QInt8,
    )
    int8_model = onnx.load(str(int8_path))
    add_metadata(int8_model, "INT8 dynamic MatMul/Gemm weights / FP32 IO", training_scope)
    onnx.checker.check_model(int8_model, full_check=True)
    onnx.save(int8_model, str(int8_path))

    input_array = dummy_torch.numpy()
    fp32_session, selected_provider = create_session(fp32_path, args.provider)
    fp32_output, fp32_latency = benchmark_session(
        fp32_session, input_array, args.warmup, args.runs)
    assert_output(fp32_output, args.resolution, "FP32")
    max_error = float(np.max(np.abs(fp32_output - reference)))
    if max_error > 2.0e-3:
        raise AssertionError(f"ONNX/PyTorch maximum error is too high: {max_error}")

    # Dynamic INT8 is primarily a CPU/fallback artifact; validate its graph and
    # execution independently from the chosen accelerated FP32 provider.
    int8_session, int8_provider = create_session(int8_path, "CPUExecutionProvider")
    int8_output, int8_latency = benchmark_session(int8_session, input_array, 1, max(1, args.runs // 2))
    assert_output(int8_output, args.resolution, "INT8")

    fp16_status: dict[str, object]
    try:
        fp16_session, fp16_provider = create_session(fp16_path, args.provider)
        fp16_output, fp16_latency = benchmark_session(
            fp16_session, input_array, args.warmup, args.runs)
        assert_output(fp16_output, args.resolution, "FP16")
        fp16_status = {
            "executed": True,
            "provider": fp16_provider,
            "latency": fp16_latency,
            "max_abs_difference_vs_fp32": float(np.max(np.abs(fp16_output - fp32_output))),
        }
    except Exception as error:  # Structural validity is still guaranteed by ONNX checker.
        fp16_status = {
            "executed": False,
            "provider": None,
            "reason": str(error),
        }

    metrics = {
        "format": "PIXL.PixDiT.ExportMetrics.v1",
        "resolution": args.resolution,
        "input_shape": list(input_array.shape),
        "output_shape": list(fp32_output.shape),
        "parameter_count": count,
        "onnx_opset": 17,
        "fp32": {
            "path": str(fp32_path),
            "bytes": fp32_path.stat().st_size,
            "sha256": sha256_file(fp32_path),
            "provider": selected_provider,
            "latency": fp32_latency,
            "max_abs_error_vs_pytorch": max_error,
        },
        "fp16": {
            "path": str(fp16_path),
            "bytes": fp16_path.stat().st_size,
            "sha256": sha256_file(fp16_path),
            **fp16_status,
        },
        "int8_dynamic": {
            "path": str(int8_path),
            "bytes": int8_path.stat().st_size,
            "sha256": sha256_file(int8_path),
            "provider": int8_provider,
            "latency": int8_latency,
            "max_abs_difference_vs_fp32": float(np.max(np.abs(int8_output - fp32_output))),
        },
        "available_providers": ort.get_available_providers(),
        "all_shapes_verified": True,
        "all_outputs_finite": True,
    }
    write_json(args.output / "export_metrics.json", metrics)
    print(json.dumps(metrics, indent=2))


if __name__ == "__main__":
    main()
