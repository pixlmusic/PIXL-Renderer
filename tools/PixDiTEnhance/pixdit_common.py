"""Shared data, metric, and reproducibility utilities for PIXL PixDiT."""

from __future__ import annotations

import hashlib
import json
import math
import random
from dataclasses import asdict, is_dataclass
from pathlib import Path
from typing import Any

import numpy as np
import torch
from PIL import Image
from torch.utils.data import Dataset


def set_deterministic_seed(seed: int) -> None:
    """Seed every generator used by the validation pipeline."""

    random.seed(seed)
    np.random.seed(seed)
    torch.manual_seed(seed)
    if torch.cuda.is_available():
        torch.cuda.manual_seed_all(seed)
    torch.backends.cudnn.benchmark = False
    torch.backends.cudnn.deterministic = True


def choose_device(requested: str = "auto") -> torch.device:
    """Resolve a training device without making CUDA a runtime requirement."""

    if requested == "auto":
        return torch.device("cuda" if torch.cuda.is_available() else "cpu")
    device = torch.device(requested)
    if device.type == "cuda" and not torch.cuda.is_available():
        raise RuntimeError("CUDA was requested but is unavailable")
    return device


def load_rgb(path: Path) -> torch.Tensor:
    image = Image.open(path).convert("RGB")
    array = np.asarray(image, dtype=np.float32) / 255.0
    return torch.from_numpy(array).permute(2, 0, 1).contiguous()


def load_depth(path: Path) -> torch.Tensor:
    image = Image.open(path).convert("I;16")
    array = np.asarray(image, dtype=np.float32) / 65535.0
    return torch.from_numpy(array).unsqueeze(0).contiguous().clamp_(0.0, 1.0)


class PixDiTPairedDataset(Dataset):
    """Paired synthetic RGB/depth inputs and restoration targets."""

    def __init__(self, root: str | Path, split: str = "train") -> None:
        self.root = Path(root)
        manifest_path = self.root / "manifest.json"
        if not manifest_path.is_file():
            raise FileNotFoundError(f"Dataset manifest is missing: {manifest_path}")
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        self.entries = [entry for entry in manifest["samples"] if entry["split"] == split]
        if not self.entries:
            raise RuntimeError(f"Dataset split '{split}' is empty")

    def __len__(self) -> int:
        return len(self.entries)

    def __getitem__(self, index: int) -> dict[str, torch.Tensor | int]:
        entry = self.entries[index]
        raw = load_rgb(self.root / entry["raw"])
        target = load_rgb(self.root / entry["target"])
        depth = load_depth(self.root / entry["depth"])
        if raw.shape != target.shape or raw.shape[1:] != depth.shape[1:]:
            raise RuntimeError(f"Mismatched sample dimensions for index {index}")
        return {
            "rgbd": torch.cat((raw, depth), dim=0),
            "target": target,
            "index": index,
        }


def charbonnier_loss(prediction: torch.Tensor, target: torch.Tensor, epsilon: float = 1.0e-3) -> torch.Tensor:
    return torch.sqrt((prediction - target).square() + epsilon * epsilon).mean()


def image_psnr(prediction: torch.Tensor, target: torch.Tensor) -> float:
    mse = torch.mean((prediction.float() - target.float()).square()).item()
    if mse <= 1.0e-12:
        return 120.0
    return -10.0 * math.log10(mse)


def ensure_finite(name: str, value: torch.Tensor | float) -> None:
    tensor = value if isinstance(value, torch.Tensor) else torch.tensor(value)
    if not torch.isfinite(tensor).all():
        raise FloatingPointError(f"{name} contains NaN or Inf")


def write_json(path: str | Path, payload: Any) -> None:
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    if is_dataclass(payload):
        payload = asdict(payload)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True), encoding="utf-8")


def sha256_file(path: str | Path) -> str:
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest().upper()


def save_tensor_preview(tensor: torch.Tensor, path: str | Path) -> None:
    """Save the first RGB tensor in a batch as a deterministic PNG preview."""

    if tensor.ndim == 4:
        tensor = tensor[0]
    array = tensor.detach().float().clamp(0.0, 1.0).cpu().permute(1, 2, 0).numpy()
    image = Image.fromarray(np.round(array * 255.0).astype(np.uint8), mode="RGB")
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path, optimize=True)


def checkpoint_load(path: str | Path, map_location: str | torch.device = "cpu") -> dict[str, Any]:
    """Use weights_only when supported while retaining our plain metadata dict."""

    try:
        return torch.load(path, map_location=map_location, weights_only=True)
    except TypeError:
        return torch.load(path, map_location=map_location)

