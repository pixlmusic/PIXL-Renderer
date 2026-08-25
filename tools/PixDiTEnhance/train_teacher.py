"""Train the compact PIXL PixDiT flow-matching teacher."""

from __future__ import annotations

import argparse
import time
from pathlib import Path

import torch
from torch.nn import functional as F
from torch.utils.data import DataLoader

from model_pixdit import PixDiTConfig, PixDiTTeacher, parameter_count
from pixdit_common import (
    PixDiTPairedDataset,
    charbonnier_loss,
    choose_device,
    ensure_finite,
    image_psnr,
    save_tensor_preview,
    set_deterministic_seed,
    write_json,
)


def gradient_loss(prediction: torch.Tensor, target: torch.Tensor) -> torch.Tensor:
    pred_x = prediction[:, :, :, 1:] - prediction[:, :, :, :-1]
    target_x = target[:, :, :, 1:] - target[:, :, :, :-1]
    pred_y = prediction[:, :, 1:, :] - prediction[:, :, :-1, :]
    target_y = target[:, :, 1:, :] - target[:, :, :-1, :]
    return F.l1_loss(pred_x, target_x) + F.l1_loss(pred_y, target_y)


@torch.no_grad()
def validate(
    model: PixDiTTeacher,
    loader: DataLoader,
    device: torch.device,
    integration_steps: int,
) -> tuple[float, float, float, torch.Tensor]:
    model.eval()
    losses: list[float] = []
    baseline_psnr: list[float] = []
    enhanced_psnr: list[float] = []
    last_output: torch.Tensor | None = None
    for batch in loader:
        rgbd = batch["rgbd"].to(device)
        target = batch["target"].to(device)
        raw = rgbd[:, :3]
        output = model.enhance(rgbd, integration_steps)
        loss = charbonnier_loss(output, target)
        ensure_finite("teacher validation loss", loss)
        losses.append(float(loss.item()))
        baseline_psnr.append(image_psnr(raw, target))
        enhanced_psnr.append(image_psnr(output, target))
        last_output = output
    assert last_output is not None
    return (
        sum(losses) / len(losses),
        sum(baseline_psnr) / len(baseline_psnr),
        sum(enhanced_psnr) / len(enhanced_psnr),
        last_output,
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--epochs", type=int, default=50)
    parser.add_argument("--batch-size", type=int, default=4)
    parser.add_argument("--learning-rate", type=float, default=2.0e-4)
    parser.add_argument("--integration-steps", type=int, default=8)
    parser.add_argument("--device", default="auto")
    parser.add_argument("--seed", type=int, default=20260825)
    args = parser.parse_args()

    if args.epochs < 1 or args.batch_size < 1:
        raise ValueError("Epochs and batch size must be positive")
    set_deterministic_seed(args.seed)
    device = choose_device(args.device)
    train_dataset = PixDiTPairedDataset(args.data, "train")
    val_dataset = PixDiTPairedDataset(args.data, "val")
    train_loader = DataLoader(
        train_dataset, batch_size=args.batch_size, shuffle=True, num_workers=0)
    val_loader = DataLoader(
        val_dataset, batch_size=args.batch_size, shuffle=False, num_workers=0)

    config = PixDiTConfig()
    model = PixDiTTeacher(config).to(device)
    count = parameter_count(model)
    if not 5_000_000 <= count <= 15_000_000:
        raise AssertionError(f"Teacher parameter target missed: {count:,}")
    optimizer = torch.optim.AdamW(
        model.parameters(), lr=args.learning_rate, betas=(0.9, 0.95), weight_decay=0.01)
    use_amp = device.type == "cuda"
    scaler = torch.amp.GradScaler("cuda", enabled=use_amp)

    args.output.mkdir(parents=True, exist_ok=True)
    best_loss = float("inf")
    initial_loss: float | None = None
    last_epoch_loss = float("inf")
    max_gradient_norm = 0.0
    started = time.perf_counter()

    for epoch in range(args.epochs):
        model.train()
        epoch_losses: list[float] = []
        for batch in train_loader:
            rgbd = batch["rgbd"].to(device, non_blocking=True)
            target = batch["target"].to(device, non_blocking=True)
            raw = rgbd[:, :3]
            timestep = torch.rand((rgbd.shape[0],), device=device)
            mix = timestep[:, None, None, None]
            velocity_target = target - raw
            state = raw + mix * velocity_target

            optimizer.zero_grad(set_to_none=True)
            with torch.autocast(
                device_type=device.type,
                dtype=torch.float16 if use_amp else torch.float32,
                enabled=use_amp,
            ):
                velocity = model(state, rgbd, timestep)
                endpoint = (state + velocity * (1.0 - mix)).clamp(0.0, 1.0)
                flow_loss = charbonnier_loss(velocity, velocity_target)
                reconstruction_loss = charbonnier_loss(endpoint, target)
                structure_loss = gradient_loss(endpoint, target)
                loss = flow_loss + reconstruction_loss * 0.30 + structure_loss * 0.08
            ensure_finite("teacher training loss", loss)
            if initial_loss is None:
                initial_loss = float(loss.detach().item())

            scaler.scale(loss).backward()
            scaler.unscale_(optimizer)
            gradient_norm = torch.nn.utils.clip_grad_norm_(model.parameters(), max_norm=1.0)
            ensure_finite("teacher gradient norm", gradient_norm)
            max_gradient_norm = max(max_gradient_norm, float(gradient_norm.item()))
            scaler.step(optimizer)
            scaler.update()
            epoch_losses.append(float(loss.detach().item()))

        last_epoch_loss = sum(epoch_losses) / len(epoch_losses)
        validation_loss, baseline_psnr, teacher_psnr, preview = validate(
            model, val_loader, device, args.integration_steps)
        ensure_finite("teacher epoch loss", last_epoch_loss)
        if validation_loss < best_loss:
            best_loss = validation_loss
            torch.save(
                {
                    "format": "PIXL.PixDiT.Teacher.v1",
                    "config": config.to_dict(),
                    "model": model.state_dict(),
                    "epoch": epoch + 1,
                    "validation_loss": validation_loss,
                },
                args.output / "teacher.pt",
            )
            save_tensor_preview(preview, args.output / "teacher_preview.png")

        if epoch == 0 or (epoch + 1) % 5 == 0 or epoch + 1 == args.epochs:
            print(
                f"teacher epoch {epoch + 1:03d}/{args.epochs}: "
                f"train={last_epoch_loss:.6f} val={validation_loss:.6f} "
                f"PSNR={teacher_psnr:.2f}dB")

    elapsed = time.perf_counter() - started
    if initial_loss is None:
        raise RuntimeError("Teacher received no training batches")
    checkpoint = args.output / "teacher.pt"
    if not checkpoint.is_file():
        raise RuntimeError("Teacher checkpoint was not produced")
    metrics = {
        "format": "PIXL.PixDiT.TeacherMetrics.v1",
        "device": str(device),
        "epochs": args.epochs,
        "batch_size": args.batch_size,
        "integration_steps": args.integration_steps,
        "parameter_count": count,
        "initial_training_loss": initial_loss,
        "final_training_loss": last_epoch_loss,
        "best_validation_loss": best_loss,
        "baseline_validation_psnr_db": baseline_psnr,
        "teacher_validation_psnr_db": teacher_psnr,
        "max_preclip_gradient_norm": max_gradient_norm,
        "elapsed_seconds": elapsed,
        "finite": True,
    }
    write_json(args.output / "teacher_metrics.json", metrics)
    print(f"Teacher training complete in {elapsed:.1f}s; best validation={best_loss:.6f}")


if __name__ == "__main__":
    main()

