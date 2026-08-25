"""Train PIXL's one-step bounded residual enhancer on real RGB/depth data.

TartanAir and MPI-Sintel are auxiliary restoration/geometry pretraining sources:
their intact RGB frame is the target and a deterministic, restrained degradation
is the input. They are not presented as aligned Skyrim/remaster pairs. True PIXL
raw/target/depth captures can be supplied through the existing manifest format and
are never synthetically degraded.

Owner-supplied visual references are holdouts. The script records their hashes in
the run inventory but deliberately never places them in the training dataset.
"""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import math
import struct
import time
import zipfile
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any, Iterable

import numpy as np
import torch
from PIL import Image
from torch.nn import functional as F
from torch.utils.data import DataLoader, Dataset

from model_pixdit import PixDiTConfig, PixDiTStudent, parameter_count
from pixdit_common import (
    charbonnier_loss,
    checkpoint_load,
    choose_device,
    ensure_finite,
    image_psnr,
    save_tensor_preview,
    set_deterministic_seed,
    sha256_file,
    write_json,
)


SINTEL_IMAGE_ZIP = "MPI-Sintel-training_images.zip"
SINTEL_DEPTH_ZIP = "MPI-Sintel-depth-training-20150305.zip"
TARTAN_IMAGE_SUFFIX = "_lcam_front.png"
TARTAN_DEPTH_SUFFIX = "_lcam_front_depth.png"


@dataclass(frozen=True)
class RealSample:
    source: str
    group: str
    split: str
    target: str
    depth: str
    raw: str | None = None
    target_archive: str | None = None
    depth_archive: str | None = None


def stable_split(group: str, validation_percent: int) -> str:
    value = int(hashlib.sha256(group.encode("utf-8")).hexdigest()[:8], 16) % 100
    return "val" if value < validation_percent else "train"


def index_tartanair(
    root: Path,
    stride: int,
    validation_percent: int,
) -> list[RealSample]:
    samples: list[RealSample] = []
    if not root.is_dir():
        return samples
    for environment in sorted(path for path in root.iterdir() if path.is_dir() and not path.name.startswith(".")):
        easy = environment / "Data_easy"
        if not easy.is_dir():
            continue
        for trajectory in sorted(path for path in easy.iterdir() if path.is_dir()):
            image_dir = trajectory / "image_lcam_front"
            depth_dir = trajectory / "depth_lcam_front"
            if not image_dir.is_dir() or not depth_dir.is_dir():
                continue
            group = f"tartanair/{environment.name}/{trajectory.name}"
            split = stable_split(group, validation_percent)
            images = sorted(image_dir.glob(f"*{TARTAN_IMAGE_SUFFIX}"))
            for image_path in images[::stride]:
                frame = image_path.name.removesuffix(TARTAN_IMAGE_SUFFIX)
                depth_path = depth_dir / f"{frame}{TARTAN_DEPTH_SUFFIX}"
                if depth_path.is_file():
                    samples.append(
                        RealSample(
                            source="tartanair_auxiliary",
                            group=group,
                            split=split,
                            target=str(image_path),
                            depth=str(depth_path),
                        )
                    )
    return samples


def index_sintel(
    data_root: Path,
    passes: Iterable[str],
    validation_percent: int,
) -> list[RealSample]:
    image_zip_path = data_root / SINTEL_IMAGE_ZIP
    depth_zip_path = data_root / SINTEL_DEPTH_ZIP
    if not image_zip_path.is_file() or not depth_zip_path.is_file():
        return []

    requested_passes = {value.lower() for value in passes}
    samples: list[RealSample] = []
    with zipfile.ZipFile(image_zip_path) as image_zip, zipfile.ZipFile(depth_zip_path) as depth_zip:
        depth_members = {
            info.filename for info in depth_zip.infolist()
            if not info.is_dir() and info.filename.lower().endswith(".dpt")
        }
        for info in image_zip.infolist():
            if info.is_dir() or not info.filename.lower().endswith(".png"):
                continue
            parts = info.filename.replace("\\", "/").split("/")
            if len(parts) != 4 or parts[0] != "training" or parts[1].lower() not in requested_passes:
                continue
            render_pass, scene, filename = parts[1], parts[2], parts[3]
            depth_member = f"training/depth/{scene}/{Path(filename).stem}.dpt"
            if depth_member not in depth_members:
                continue
            group = f"sintel/{scene}"
            samples.append(
                RealSample(
                    source=f"sintel_{render_pass.lower()}_auxiliary",
                    group=group,
                    split=stable_split(group, validation_percent),
                    target=info.filename,
                    depth=depth_member,
                    target_archive=str(image_zip_path),
                    depth_archive=str(depth_zip_path),
                )
            )
    return samples


def index_pixl_pairs(root: Path | None) -> list[RealSample]:
    if root is None:
        return []
    manifest_path = root / "manifest.json"
    if not manifest_path.is_file():
        raise FileNotFoundError(f"PIXL pair manifest is missing: {manifest_path}")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    samples: list[RealSample] = []
    for index, entry in enumerate(manifest.get("samples", [])):
        raw = root / entry["raw"]
        target = root / entry["target"]
        depth = root / entry["depth"]
        if not raw.is_file() or not target.is_file() or not depth.is_file():
            raise FileNotFoundError(f"PIXL aligned sample {index} has a missing file")
        samples.append(
            RealSample(
                source="pixl_aligned",
                group=str(entry.get("group", entry.get("session", f"pixl/{index:06d}"))),
                split=str(entry.get("split", "train")),
                raw=str(raw),
                target=str(target),
                depth=str(depth),
            )
        )
    return samples


def balanced_limit(samples: list[RealSample], maximum: int, seed: int) -> list[RealSample]:
    if maximum <= 0 or len(samples) <= maximum:
        return samples
    by_source: dict[str, list[RealSample]] = {}
    for sample in samples:
        by_source.setdefault(sample.source, []).append(sample)
    generator = np.random.default_rng(seed)
    for group in by_source.values():
        generator.shuffle(group)

    selected: list[RealSample] = []
    sources = sorted(by_source)
    while len(selected) < maximum and sources:
        next_sources: list[str] = []
        for source in sources:
            group = by_source[source]
            if group and len(selected) < maximum:
                selected.append(group.pop())
            if group:
                next_sources.append(source)
        sources = next_sources
    return selected


def ensure_both_splits(samples: list[RealSample]) -> list[RealSample]:
    if len(samples) < 2:
        raise RuntimeError("At least two indexed samples are required")
    has_train = any(sample.split == "train" for sample in samples)
    has_val = any(sample.split == "val" for sample in samples)
    if has_train and has_val:
        return samples
    replacement = samples[-1]
    desired = "val" if not has_val else "train"
    return [*samples[:-1], RealSample(**{**asdict(replacement), "split": desired})]


def pil_to_rgb_tensor(image: Image.Image) -> torch.Tensor:
    array = np.asarray(image.convert("RGB"), dtype=np.float32).copy() / 255.0
    return torch.from_numpy(array).permute(2, 0, 1).contiguous()


def normalize_metric_depth(depth: np.ndarray) -> torch.Tensor:
    depth = np.asarray(depth, dtype=np.float32)
    valid = np.isfinite(depth) & (depth > 0.0)
    if not np.any(valid):
        return torch.ones((1, depth.shape[0], depth.shape[1]), dtype=torch.float32)
    valid_values = depth[valid]
    near = float(np.percentile(valid_values, 0.5))
    far = float(np.percentile(valid_values, 99.5))
    if far <= near + 1.0e-6:
        far = near + 1.0
    safe = np.where(valid, depth, far)
    log_near = math.log1p(max(near, 0.0))
    log_far = math.log1p(max(far, near + 1.0e-6))
    normalized = (np.log1p(np.maximum(safe, 0.0)) - log_near) / max(log_far - log_near, 1.0e-6)
    return torch.from_numpy(np.clip(normalized, 0.0, 1.0)).unsqueeze(0).float().contiguous()


def decode_tartanair_depth(path: Path) -> torch.Tensor:
    rgba = np.asarray(Image.open(path).convert("RGBA"), dtype=np.uint8).copy()
    depth = rgba.reshape(-1, 4).view("<f4").reshape(rgba.shape[:2])
    return normalize_metric_depth(depth)


def decode_sintel_depth(payload: bytes) -> torch.Tensor:
    if len(payload) < 12:
        raise ValueError("Truncated Sintel .dpt payload")
    tag, width, height = struct.unpack("<fii", payload[:12])
    if abs(tag - 202021.25) > 0.01 or width <= 0 or height <= 0:
        raise ValueError("Invalid Sintel .dpt header")
    expected = 12 + width * height * 4
    if len(payload) != expected:
        raise ValueError(f"Sintel .dpt size mismatch: expected {expected}, received {len(payload)}")
    depth = np.frombuffer(payload, dtype="<f4", offset=12).reshape(height, width).copy()
    return normalize_metric_depth(depth)


def decode_normalized_depth(path: Path) -> torch.Tensor:
    image = Image.open(path)
    array = np.asarray(image).copy()
    if array.ndim == 3:
        array = array[..., 0]
    if np.issubdtype(array.dtype, np.integer):
        maximum = float(np.iinfo(array.dtype).max)
        depth = array.astype(np.float32) / maximum
    else:
        depth = array.astype(np.float32)
    return torch.from_numpy(np.clip(depth, 0.0, 1.0)).unsqueeze(0).float().contiguous()


class PixDiTRealDataset(Dataset):
    def __init__(self, samples: list[RealSample], resolution: int, training: bool, seed: int) -> None:
        self.samples = samples
        self.resolution = resolution
        self.training = training
        self.seed = seed
        self.epoch = 0
        self._archives: dict[str, zipfile.ZipFile] = {}

    def __del__(self) -> None:
        for archive in self._archives.values():
            archive.close()

    def set_epoch(self, epoch: int) -> None:
        self.epoch = epoch

    def __len__(self) -> int:
        return len(self.samples)

    def _archive(self, path: str) -> zipfile.ZipFile:
        archive = self._archives.get(path)
        if archive is None:
            archive = zipfile.ZipFile(path)
            self._archives[path] = archive
        return archive

    def _load_rgb(self, locator: str, archive_path: str | None) -> torch.Tensor:
        if archive_path is None:
            with Image.open(locator) as image:
                return pil_to_rgb_tensor(image)
        payload = self._archive(archive_path).read(locator)
        with Image.open(io.BytesIO(payload)) as image:
            return pil_to_rgb_tensor(image)

    def _load_depth(self, sample: RealSample) -> torch.Tensor:
        if sample.depth_archive is not None:
            return decode_sintel_depth(self._archive(sample.depth_archive).read(sample.depth))
        path = Path(sample.depth)
        if sample.source == "tartanair_auxiliary":
            return decode_tartanair_depth(path)
        return decode_normalized_depth(path)

    @staticmethod
    def _random(generator: torch.Generator, low: float = 0.0, high: float = 1.0) -> float:
        return low + (high - low) * float(torch.rand((), generator=generator).item())

    def _aligned_crop(
        self,
        raw: torch.Tensor | None,
        target: torch.Tensor,
        depth: torch.Tensor,
        generator: torch.Generator,
    ) -> tuple[torch.Tensor | None, torch.Tensor, torch.Tensor]:
        if target.shape[1:] != depth.shape[1:] or (raw is not None and raw.shape != target.shape):
            raise RuntimeError("RGB/depth dimensions are not aligned")
        height, width = target.shape[1:]
        scale = max(self.resolution / float(height), self.resolution / float(width), 1.0)
        if scale > 1.0:
            new_height = max(self.resolution, int(round(height * scale)))
            new_width = max(self.resolution, int(round(width * scale)))
            target = F.interpolate(target[None], (new_height, new_width), mode="bicubic", align_corners=False)[0]
            depth = F.interpolate(depth[None], (new_height, new_width), mode="bilinear", align_corners=False)[0]
            if raw is not None:
                raw = F.interpolate(raw[None], (new_height, new_width), mode="bicubic", align_corners=False)[0]
            height, width = new_height, new_width

        if self.training:
            top = int(torch.randint(0, height - self.resolution + 1, (), generator=generator).item())
            left = int(torch.randint(0, width - self.resolution + 1, (), generator=generator).item())
        else:
            top = (height - self.resolution) // 2
            left = (width - self.resolution) // 2
        target = target[:, top:top + self.resolution, left:left + self.resolution]
        depth = depth[:, top:top + self.resolution, left:left + self.resolution]
        if raw is not None:
            raw = raw[:, top:top + self.resolution, left:left + self.resolution]
        if self.training and self._random(generator) < 0.5:
            target = target.flip(-1)
            depth = depth.flip(-1)
            if raw is not None:
                raw = raw.flip(-1)
        return raw, target, depth

    def _degrade(self, target: torch.Tensor, generator: torch.Generator) -> torch.Tensor:
        image = target[None]
        size = target.shape[-1]
        scale = self._random(generator, 0.52, 0.92)
        reduced = max(16, int(round(size * scale)))
        image = F.interpolate(image, (reduced, reduced), mode="area")
        image = F.interpolate(image, (size, size), mode="bicubic", align_corners=False)

        if self._random(generator) < 0.72:
            radius = 1 if self._random(generator) < 0.72 else 2
            kernel = radius * 2 + 1
            image = F.avg_pool2d(image, kernel_size=kernel, stride=1, padding=radius)

        exposure = self._random(generator, 0.88, 1.10)
        gamma = self._random(generator, 0.90, 1.12)
        saturation = self._random(generator, 0.86, 1.08)
        image = image.clamp(0.0, 1.0).pow(gamma) * exposure
        luma = image.mean(dim=1, keepdim=True)
        image = luma + (image - luma) * saturation
        levels = 255.0 if self._random(generator) < 0.65 else 127.0
        image = torch.round(image.clamp(0.0, 1.0) * levels) / levels
        noise_strength = self._random(generator, 0.0, 0.012)
        noise = torch.randn(image.shape, generator=generator, dtype=image.dtype) * noise_strength
        return (image + noise).clamp(0.0, 1.0)[0]

    def __getitem__(self, index: int) -> dict[str, torch.Tensor | str | int]:
        sample = self.samples[index]
        generator = torch.Generator().manual_seed(
            self.seed + index * 104729 + (self.epoch if self.training else 0) * 1_000_003)
        target = self._load_rgb(sample.target, sample.target_archive)
        raw = self._load_rgb(sample.raw, None) if sample.raw is not None else None
        depth = self._load_depth(sample)
        raw, target, depth = self._aligned_crop(raw, target, depth, generator)
        if raw is None:
            raw = self._degrade(target, generator)
        return {
            "rgbd": torch.cat((raw, depth), dim=0),
            "target": target,
            "depth": depth,
            "source": sample.source,
            "index": index,
        }


def image_gradients(image: torch.Tensor) -> tuple[torch.Tensor, torch.Tensor]:
    return image[:, :, :, 1:] - image[:, :, :, :-1], image[:, :, 1:, :] - image[:, :, :-1, :]


def fidelity_loss(
    output: torch.Tensor,
    target: torch.Tensor,
    raw: torch.Tensor,
    depth: torch.Tensor,
    max_residual: float,
) -> tuple[torch.Tensor, dict[str, torch.Tensor]]:
    reconstruction = charbonnier_loss(output, target)
    output_x, output_y = image_gradients(output)
    target_x, target_y = image_gradients(target)
    gradient = F.l1_loss(output_x, target_x) + F.l1_loss(output_y, target_y)

    depth_x = F.pad((depth[:, :, :, 1:] - depth[:, :, :, :-1]).abs(), (0, 1, 0, 0))
    depth_y = F.pad((depth[:, :, 1:, :] - depth[:, :, :-1, :]).abs(), (0, 0, 0, 1))
    edge_weight = 1.0 + (depth_x + depth_y).clamp(0.0, 1.0) * 4.0
    geometry = ((output - target).abs() * edge_weight).mean()

    low_output = F.avg_pool2d(output, kernel_size=16, stride=16)
    low_target = F.avg_pool2d(target, kernel_size=16, stride=16)
    low_frequency = F.l1_loss(low_output, low_target)

    output_luma = output.mean(dim=1, keepdim=True)
    target_luma = target.mean(dim=1, keepdim=True)
    chroma = F.l1_loss(output - output_luma, target - target_luma)
    residual_excess = F.relu((output - raw).abs() - max_residual * 0.72).mean()

    total = (
        reconstruction
        + gradient * 0.16
        + geometry * 0.10
        + low_frequency * 0.08
        + chroma * 0.05
        + residual_excess * 0.04
    )
    return total, {
        "reconstruction": reconstruction,
        "gradient": gradient,
        "geometry": geometry,
        "low_frequency": low_frequency,
        "chroma": chroma,
        "residual_excess": residual_excess,
    }


@torch.no_grad()
def validate(
    model: PixDiTStudent,
    loader: DataLoader,
    device: torch.device,
    max_residual: float,
) -> dict[str, Any]:
    model.eval()
    losses: list[float] = []
    baseline_psnr: list[float] = []
    output_psnr: list[float] = []
    residuals: list[float] = []
    preview: tuple[torch.Tensor, torch.Tensor, torch.Tensor] | None = None
    for batch in loader:
        rgbd = batch["rgbd"].to(device)
        target = batch["target"].to(device)
        depth = batch["depth"].to(device)
        raw = rgbd[:, :3]
        output = model(rgbd)
        loss, _ = fidelity_loss(output, target, raw, depth, max_residual)
        ensure_finite("real-data validation loss", loss)
        losses.append(float(loss.item()))
        baseline_psnr.append(image_psnr(raw, target))
        output_psnr.append(image_psnr(output, target))
        residuals.append(float((output - raw).abs().mean().item()))
        if preview is None:
            preview = (raw.cpu(), output.cpu(), target.cpu())
    if preview is None:
        raise RuntimeError("Validation loader produced no batches")
    return {
        "loss": sum(losses) / len(losses),
        "baseline_psnr_db": sum(baseline_psnr) / len(baseline_psnr),
        "output_psnr_db": sum(output_psnr) / len(output_psnr),
        "mean_absolute_residual": sum(residuals) / len(residuals),
        "preview": preview,
    }


def build_inventory(
    samples: list[RealSample],
    references: list[Path],
    data_root: Path,
    resolution: int,
    stride: int,
) -> dict[str, Any]:
    counts: dict[str, dict[str, int]] = {}
    groups: dict[str, set[str]] = {}
    for sample in samples:
        counts.setdefault(sample.source, {"train": 0, "val": 0})[sample.split] += 1
        groups.setdefault(sample.source, set()).add(sample.group)
    reference_records = []
    for reference in references:
        if not reference.is_file():
            raise FileNotFoundError(f"Holdout reference does not exist: {reference}")
        with Image.open(reference) as image:
            reference_records.append(
                {
                    "path": str(reference),
                    "sha256": sha256_file(reference),
                    "size": [image.width, image.height],
                    "role": "holdout_aesthetic_reference_not_training_data",
                }
            )
    return {
        "format": "PIXL.PixDiT.RealDataInventory.v1",
        "data_root": str(data_root),
        "resolution": resolution,
        "tartanair_frame_stride": stride,
        "sample_counts": counts,
        "group_counts": {key: len(value) for key, value in groups.items()},
        "total_samples": len(samples),
        "train_samples": sum(sample.split == "train" for sample in samples),
        "validation_samples": sum(sample.split == "val" for sample in samples),
        "references": reference_records,
        "safety_contract": {
            "auxiliary_sources": "self-supervised restoration/geometry pretraining only",
            "pixl_pairs": "must be pixel-aligned raw/target/depth captures",
            "reference_images": "hash-only holdouts; never indexed as training targets",
            "output": "single-forward bounded residual; original capture remains authoritative",
            "distribution": "review every source dataset and target-generation term before shipping weights",
        },
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--pixl-pairs", type=Path)
    parser.add_argument("--reference", type=Path, action="append", default=[])
    parser.add_argument("--sintel-passes", nargs="+", default=["clean", "final"], choices=["clean", "final"])
    parser.add_argument("--tartanair-stride", type=int, default=8)
    parser.add_argument("--validation-percent", type=int, default=18)
    parser.add_argument("--max-samples", type=int, default=0)
    parser.add_argument("--resolution", type=int, default=256)
    parser.add_argument("--epochs", type=int, default=20)
    parser.add_argument("--batch-size", type=int, default=1)
    parser.add_argument("--gradient-accumulation", type=int, default=4)
    parser.add_argument("--learning-rate", type=float, default=8.0e-5)
    parser.add_argument("--max-residual", type=float, default=0.18)
    parser.add_argument("--init", type=Path)
    parser.add_argument("--device", default="auto")
    parser.add_argument("--seed", type=int, default=20260825)
    parser.add_argument("--inventory-only", action="store_true")
    parser.add_argument("--accept-external-dataset-terms", action="store_true")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if args.resolution <= 0 or args.resolution % 32:
        raise ValueError("Resolution must be positive and divisible by 32")
    if args.tartanair_stride < 1 or not 1 <= args.validation_percent <= 40:
        raise ValueError("Invalid dataset stride or validation percentage")
    if args.epochs < 1 or args.batch_size < 1 or args.gradient_accumulation < 1:
        raise ValueError("Epoch, batch size and gradient accumulation must be positive")
    if not 0.02 <= args.max_residual <= 0.35:
        raise ValueError("Max residual must remain within the protected 0.02-0.35 range")

    set_deterministic_seed(args.seed)
    tartan_root = args.data_root / "TartanAirV2"
    samples = index_tartanair(tartan_root, args.tartanair_stride, args.validation_percent)
    samples.extend(index_sintel(args.data_root, args.sintel_passes, args.validation_percent))
    pixl_samples = index_pixl_pairs(args.pixl_pairs)
    # True aligned PIXL training pairs receive extra sampling weight. Keep each
    # validation pair exactly once so reported metrics are not quietly skewed by
    # duplicated holdouts.
    samples.extend(pixl_samples)
    samples.extend([sample for sample in pixl_samples if sample.split == "train"] * 3)
    samples = balanced_limit(samples, args.max_samples, args.seed)
    samples = ensure_both_splits(samples)

    args.output.mkdir(parents=True, exist_ok=True)
    inventory = build_inventory(
        samples, args.reference, args.data_root, args.resolution, args.tartanair_stride)
    write_json(args.output / "dataset_inventory.json", inventory)
    print(json.dumps(inventory["sample_counts"], indent=2, sort_keys=True))
    print(
        f"Indexed {inventory['total_samples']} samples: "
        f"{inventory['train_samples']} train / {inventory['validation_samples']} validation")
    if args.inventory_only:
        print(f"Inventory written to {args.output / 'dataset_inventory.json'}")
        return
    if any("auxiliary" in sample.source for sample in samples) and not args.accept_external_dataset_terms:
        raise RuntimeError(
            "Training uses external auxiliary datasets. Review their terms, then pass "
            "--accept-external-dataset-terms. This flag is recorded but does not grant redistribution rights.")

    device = choose_device(args.device)
    config = PixDiTConfig(max_residual=args.max_residual)
    model = PixDiTStudent(config).to(device)
    if args.init is not None:
        checkpoint = checkpoint_load(args.init, "cpu")
        init_config = PixDiTConfig.from_dict(checkpoint["config"])
        # Only max_residual may intentionally change; tensor architecture must match.
        architecture_fields = {key: value for key, value in config.to_dict().items() if key != "max_residual"}
        init_fields = {key: value for key, value in init_config.to_dict().items() if key != "max_residual"}
        if init_fields != architecture_fields:
            raise RuntimeError("Initial checkpoint architecture does not match the real-data model")
        model.load_state_dict(checkpoint["model"], strict=True)

    count = parameter_count(model)
    if not 5_000_000 <= count <= 15_000_000:
        raise AssertionError(f"PixDiT parameter target missed: {count:,}")

    train_samples = [sample for sample in samples if sample.split == "train"]
    val_samples = [sample for sample in samples if sample.split == "val"]
    train_dataset = PixDiTRealDataset(train_samples, args.resolution, True, args.seed)
    val_dataset = PixDiTRealDataset(val_samples, args.resolution, False, args.seed + 1)
    train_loader = DataLoader(train_dataset, batch_size=args.batch_size, shuffle=True, num_workers=0)
    val_loader = DataLoader(val_dataset, batch_size=args.batch_size, shuffle=False, num_workers=0)

    optimizer = torch.optim.AdamW(
        model.parameters(), lr=args.learning_rate, betas=(0.9, 0.95), weight_decay=0.01)
    use_amp = device.type == "cuda"
    scaler = torch.amp.GradScaler("cuda", enabled=use_amp)
    best_loss = float("inf")
    started = time.perf_counter()
    max_gradient_norm = 0.0
    initial_loss: float | None = None
    final_training_loss = float("inf")
    validation: dict[str, Any] = {}

    for epoch in range(args.epochs):
        train_dataset.set_epoch(epoch)
        model.train()
        optimizer.zero_grad(set_to_none=True)
        epoch_losses: list[float] = []
        for step, batch in enumerate(train_loader):
            rgbd = batch["rgbd"].to(device, non_blocking=True)
            target = batch["target"].to(device, non_blocking=True)
            depth = batch["depth"].to(device, non_blocking=True)
            raw = rgbd[:, :3]
            with torch.autocast(
                device_type=device.type,
                dtype=torch.float16 if use_amp else torch.float32,
                enabled=use_amp,
            ):
                output = model(rgbd)
                loss, _ = fidelity_loss(output, target, raw, depth, args.max_residual)
                scaled_loss = loss / args.gradient_accumulation
            ensure_finite("real-data training loss", loss)
            if initial_loss is None:
                initial_loss = float(loss.detach().item())
            scaler.scale(scaled_loss).backward()
            should_step = (step + 1) % args.gradient_accumulation == 0 or step + 1 == len(train_loader)
            if should_step:
                scaler.unscale_(optimizer)
                gradient_norm = torch.nn.utils.clip_grad_norm_(model.parameters(), max_norm=1.0)
                ensure_finite("real-data gradient norm", gradient_norm)
                max_gradient_norm = max(max_gradient_norm, float(gradient_norm.item()))
                scaler.step(optimizer)
                scaler.update()
                optimizer.zero_grad(set_to_none=True)
            epoch_losses.append(float(loss.detach().item()))

        final_training_loss = sum(epoch_losses) / len(epoch_losses)
        validation = validate(model, val_loader, device, args.max_residual)
        if validation["loss"] < best_loss:
            best_loss = float(validation["loss"])
            torch.save(
                {
                    "format": "PIXL.PixDiT.Student1Step.RealData.v1",
                    "config": config.to_dict(),
                    "model": model.state_dict(),
                    "epoch": epoch + 1,
                    "validation_loss": best_loss,
                    "inference_steps": 1,
                    "bounded_residual": True,
                    "dataset_inventory": "dataset_inventory.json",
                },
                args.output / "student_1step_real.pt",
            )
            raw_preview, output_preview, target_preview = validation["preview"]
            save_tensor_preview(raw_preview, args.output / "preview_raw.png")
            save_tensor_preview(output_preview, args.output / "preview_enhanced.png")
            save_tensor_preview(target_preview, args.output / "preview_target.png")
        if epoch == 0 or (epoch + 1) % 5 == 0 or epoch + 1 == args.epochs:
            print(
                f"real-data epoch {epoch + 1:03d}/{args.epochs}: "
                f"train={final_training_loss:.6f} val={validation['loss']:.6f} "
                f"PSNR={validation['output_psnr_db']:.2f}dB "
                f"residual={validation['mean_absolute_residual']:.5f}")

    if initial_loss is None or not (args.output / "student_1step_real.pt").is_file():
        raise RuntimeError("Real-data training did not produce a checkpoint")
    elapsed = time.perf_counter() - started
    metrics = {
        "format": "PIXL.PixDiT.RealDataMetrics.v1",
        "device": str(device),
        "parameter_count": count,
        "epochs": args.epochs,
        "batch_size": args.batch_size,
        "gradient_accumulation": args.gradient_accumulation,
        "max_residual": args.max_residual,
        "initial_training_loss": initial_loss,
        "final_training_loss": final_training_loss,
        "best_validation_loss": best_loss,
        "baseline_validation_psnr_db": validation["baseline_psnr_db"],
        "enhanced_validation_psnr_db": validation["output_psnr_db"],
        "mean_absolute_residual": validation["mean_absolute_residual"],
        "max_preclip_gradient_norm": max_gradient_norm,
        "elapsed_seconds": elapsed,
        "student_inference_steps": 1,
        "external_dataset_terms_acknowledged": args.accept_external_dataset_terms,
        "references_used_for_training": False,
        "finite": True,
    }
    write_json(args.output / "real_data_metrics.json", metrics)
    print(f"Real-data training complete in {elapsed:.1f}s; best validation={best_loss:.6f}")


if __name__ == "__main__":
    main()
