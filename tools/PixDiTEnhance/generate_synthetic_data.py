"""Generate deterministic, license-clean validation pairs for PIXL PixDiT.

The generator is intentionally procedural. It verifies the complete learning and
export pipeline without copying Skyrim assets or pretending that synthetic art is
a substitute for a curated production capture corpus.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageEnhance, ImageFilter


def vertical_gradient(size: int, top: tuple[int, int, int], bottom: tuple[int, int, int]) -> Image.Image:
    y = np.linspace(0.0, 1.0, size, dtype=np.float32)[:, None, None]
    top_color = np.array(top, dtype=np.float32)[None, None, :]
    bottom_color = np.array(bottom, dtype=np.float32)[None, None, :]
    pixels = top_color * (1.0 - y) + bottom_color * y
    pixels = np.repeat(pixels, size, axis=1)
    return Image.fromarray(np.round(pixels).astype(np.uint8), mode="RGB")


def generate_scene(size: int, rng: np.random.Generator) -> tuple[Image.Image, Image.Image]:
    horizon = int(size * rng.uniform(0.38, 0.55))
    sky_top = tuple(int(v) for v in rng.integers([16, 20, 30], [55, 70, 95]))
    sky_bottom = tuple(int(v) for v in rng.integers([70, 65, 62], [145, 125, 105]))
    target = vertical_gradient(size, sky_top, sky_bottom)
    draw = ImageDraw.Draw(target, "RGBA")

    depth = np.ones((size, size), dtype=np.float32)
    ground_y = np.arange(size, dtype=np.float32)[:, None]
    ground_depth = np.clip(1.0 - (ground_y - horizon) / max(size - horizon, 1), 0.06, 1.0)
    depth[horizon:, :] = ground_depth[horizon:, :]

    ground_color = tuple(int(v) for v in rng.integers([32, 28, 22], [70, 58, 46]))
    draw.rectangle((0, horizon, size, size), fill=ground_color + (255,))

    # Perspective road/river with low-frequency material structure.
    road_center = int(size * rng.uniform(0.42, 0.58))
    road_top = max(2, int(size * 0.035))
    road_bottom = int(size * rng.uniform(0.32, 0.52))
    road = [
        (road_center - road_top, horizon),
        (road_center + road_top, horizon),
        (road_center + road_bottom, size),
        (road_center - road_bottom, size),
    ]
    draw.polygon(road, fill=(58, 52, 47, 255))

    # Distant silhouettes establish a depth hierarchy.
    for _ in range(int(rng.integers(3, 7))):
        x = int(rng.integers(0, size))
        height = int(rng.integers(max(3, size // 10), max(4, size // 3)))
        width = max(1, int(height * rng.uniform(0.10, 0.22)))
        draw.rectangle((x - width, horizon - height, x + width, horizon), fill=(18, 25, 25, 220))
        for level in range(3):
            branch_y = horizon - int(height * (0.25 + level * 0.22))
            branch = int(width * (2.4 - level * 0.45))
            draw.polygon(
                ((x, branch_y - height // 5), (x - branch, branch_y + height // 10),
                 (x + branch, branch_y + height // 10)),
                fill=(17, 28, 25, 210),
            )

    # One or two block/timber buildings with warm authored-window targets.
    for building_index in range(int(rng.integers(1, 3))):
        bw = int(size * rng.uniform(0.24, 0.42))
        bh = int(size * rng.uniform(0.22, 0.38))
        bx = int(rng.integers(-bw // 5, max(-bw // 5 + 1, size - bw + bw // 5)))
        by = horizon - bh + int(size * rng.uniform(-0.02, 0.07))
        facade_depth = float(rng.uniform(0.32, 0.68))
        stone = tuple(int(v) for v in rng.integers([48, 45, 42], [92, 82, 70]))
        draw.rectangle((bx, by, bx + bw, by + bh), fill=stone + (255,))
        roof_h = int(bh * rng.uniform(0.28, 0.44))
        draw.polygon(
            ((bx - bw // 12, by), (bx + bw // 2, by - roof_h), (bx + bw + bw // 12, by)),
            fill=(35, 28, 25, 255),
        )
        x0, x1 = max(bx, 0), min(bx + bw, size)
        y0, y1 = max(by - roof_h, 0), min(by + bh, size)
        if x1 > x0 and y1 > y0:
            depth[y0:y1, x0:x1] = np.minimum(depth[y0:y1, x0:x1], facade_depth)

        # Stone courses and timber framing are target-only coherent detail.
        course = max(2, bh // 8)
        for yy in range(by + course, by + bh, course):
            draw.line((bx, yy, bx + bw, yy), fill=(24, 23, 23, 85), width=1)
        draw.line((bx + bw // 2, by, bx + bw // 2, by + bh), fill=(31, 24, 20, 180), width=max(1, size // 96))

        window_count = 2 if bw > size * 0.28 else 1
        for wx_index in range(window_count):
            ww = max(3, bw // (window_count * 5))
            wh = max(4, int(bh * 0.24))
            wx = bx + int((wx_index + 0.5) * bw / window_count - ww / 2)
            wy = by + int(bh * 0.30)
            glow = tuple(int(v) for v in rng.integers([170, 82, 24], [255, 188, 88]))
            draw.rectangle((wx, wy, wx + ww, wy + wh), fill=glow + (245,), outline=(25, 22, 20, 255))
            draw.line((wx + ww // 2, wy, wx + ww // 2, wy + wh), fill=(35, 29, 24, 230), width=1)
            draw.line((wx, wy + wh // 2, wx + ww, wy + wh // 2), fill=(35, 29, 24, 230), width=1)

    # Road stones and near-ground material cues.
    for _ in range(int(size * 1.8)):
        y = int(rng.integers(horizon, size))
        perspective = (y - horizon + 1) / max(size - horizon, 1)
        radius_x = max(1, int(1 + perspective * size * 0.025))
        radius_y = max(1, int(radius_x * 0.45))
        center_span = int(road_top + perspective * (road_bottom - road_top))
        x = int(road_center + rng.integers(-max(center_span, 1), max(center_span, 1) + 1))
        value = int(rng.integers(62, 100))
        draw.ellipse((x - radius_x, y - radius_y, x + radius_x, y + radius_y),
                     fill=(value, value - 7, value - 12, 180), outline=(25, 25, 24, 150))

    # Subtle mystical haze/light rather than franchise-specific iconography.
    if rng.random() < 0.65:
        glow_layer = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        glow_draw = ImageDraw.Draw(glow_layer, "RGBA")
        gx = int(rng.integers(size // 6, max(size // 6 + 1, size * 5 // 6)))
        gy = int(rng.integers(max(1, horizon // 4), max(2, horizon)))
        radius = max(3, int(size * rng.uniform(0.08, 0.18)))
        for r in range(radius, 0, -1):
            alpha = int(3 + 22 * (1.0 - r / radius) ** 2)
            glow_draw.ellipse((gx - r, gy - r, gx + r, gy + r), fill=(95, 135, 175, alpha))
        target = Image.alpha_composite(target.convert("RGBA"), glow_layer).convert("RGB")

    # Fine film-like target variation is coherent and low amplitude.
    target_array = np.asarray(target, dtype=np.float32) / 255.0
    yy, xx = np.mgrid[0:size, 0:size].astype(np.float32)
    micro = np.sin(xx * 0.31 + yy * 0.17) * np.sin(xx * 0.07 - yy * 0.19)
    target_array = np.clip(target_array + micro[..., None] * 0.008, 0.0, 1.0)
    target = Image.fromarray(np.round(target_array * 255.0).astype(np.uint8), mode="RGB")

    depth_image = Image.fromarray(np.round(np.clip(depth, 0.0, 1.0) * 65535.0).astype(np.uint16), mode="I;16")
    return target, depth_image


def degrade_target(target: Image.Image, rng: np.random.Generator) -> Image.Image:
    size = target.width
    scale = float(rng.uniform(0.46, 0.72))
    small = max(8, int(size * scale))
    raw = target.resize((small, small), Image.Resampling.BILINEAR).resize(
        (size, size), Image.Resampling.BILINEAR)
    raw = raw.filter(ImageFilter.GaussianBlur(radius=float(rng.uniform(0.35, 0.95))))
    raw = ImageEnhance.Contrast(raw).enhance(float(rng.uniform(0.78, 0.94)))
    raw = ImageEnhance.Color(raw).enhance(float(rng.uniform(0.72, 0.92)))

    pixels = np.asarray(raw, dtype=np.float32) / 255.0
    cast = rng.uniform(-0.025, 0.025, size=(1, 1, 3)).astype(np.float32)
    noise = rng.normal(0.0, rng.uniform(0.006, 0.016), size=pixels.shape).astype(np.float32)
    banding = np.round((pixels + cast) * 63.0) / 63.0
    pixels = np.clip(banding + noise, 0.0, 1.0)
    return Image.fromarray(np.round(pixels * 255.0).astype(np.uint8), mode="RGB")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--count", type=int, default=24)
    parser.add_argument("--size", type=int, default=64)
    parser.add_argument("--seed", type=int, default=1337)
    args = parser.parse_args()

    if args.count < 6:
        raise ValueError("At least six samples are required for train/validation splits")
    if args.size < 32 or args.size % 32 != 0:
        raise ValueError("Synthetic image size must be >=32 and divisible by 32")

    for folder in ("raw", "target", "depth"):
        (args.output / folder).mkdir(parents=True, exist_ok=True)

    samples: list[dict[str, object]] = []
    val_count = max(2, int(round(args.count * 0.20)))
    for index in range(args.count):
        sample_seed = args.seed + index * 7919
        rng = np.random.default_rng(sample_seed)
        target, depth = generate_scene(args.size, rng)
        raw = degrade_target(target, rng)
        name = f"sample_{index:04d}.png"
        raw.save(args.output / "raw" / name, optimize=True)
        target.save(args.output / "target" / name, optimize=True)
        depth.save(args.output / "depth" / name, optimize=True)
        samples.append({
            "raw": f"raw/{name}",
            "target": f"target/{name}",
            "depth": f"depth/{name}",
            "seed": sample_seed,
            "split": "val" if index >= args.count - val_count else "train",
        })

    manifest = {
        "format": "PIXL.PixDiT.SyntheticPairs.v1",
        "license": "Original deterministic procedural validation data",
        "rgbSpace": "sRGB normalized to [0,1]",
        "depthConvention": "0 near, 1 far/sky; 16-bit PNG",
        "size": args.size,
        "seed": args.seed,
        "samples": samples,
    }
    (args.output / "manifest.json").write_text(
        json.dumps(manifest, indent=2), encoding="utf-8")
    print(f"Generated {args.count} synthetic RGB/depth pairs at {args.output}")


if __name__ == "__main__":
    main()

