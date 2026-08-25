"""Minimal provider-only verifier used by an isolated DirectML environment."""

from __future__ import annotations

import argparse
import json
import statistics
import time
from pathlib import Path

import numpy as np
import onnxruntime as ort


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--provider", default="DmlExecutionProvider")
    parser.add_argument("--resolution", type=int, default=512)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--runs", type=int, default=3)
    args = parser.parse_args()

    providers = ort.get_available_providers()
    if args.provider not in providers:
        raise RuntimeError(f"{args.provider} unavailable; providers={providers}")
    options = ort.SessionOptions()
    options.enable_mem_pattern = False
    options.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
    options.log_severity_level = 3
    session = ort.InferenceSession(str(args.model), sess_options=options, providers=[args.provider])
    model_metadata = session.get_modelmeta().custom_metadata_map
    rng = np.random.default_rng(20260825)
    sample = rng.random((1, 4, args.resolution, args.resolution), dtype=np.float32)
    sample[:, 3:4] = np.linspace(0.0, 1.0, args.resolution, dtype=np.float32)[None, None, :, None]
    name = session.get_inputs()[0].name
    session.run(None, {name: sample})
    timings: list[float] = []
    result: np.ndarray | None = None
    for _ in range(args.runs):
        started = time.perf_counter()
        result = session.run(None, {name: sample})[0]
        timings.append((time.perf_counter() - started) * 1000.0)
    assert result is not None
    if result.shape != (1, 3, args.resolution, args.resolution):
        raise AssertionError(f"Unexpected DirectML output shape: {result.shape}")
    if not np.isfinite(result).all():
        raise FloatingPointError("DirectML output contains NaN/Inf")
    metrics = {
        "format": "PIXL.PixDiT.ProviderVerification.v1",
        "model_path": str(args.model),
        "model_precision": model_metadata.get("pixl.precision", "unknown"),
        "model_format": model_metadata.get("pixl.format", "unknown"),
        "provider": args.provider,
        "available_providers": providers,
        "input_shape": list(sample.shape),
        "output_shape": list(result.shape),
        "median_ms": statistics.median(timings),
        "min_ms": min(timings),
        "max_ms": max(timings),
        "runs": args.runs,
        "finite": True,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(metrics, indent=2), encoding="utf-8")
    print(json.dumps(metrics, indent=2))


if __name__ == "__main__":
    main()
