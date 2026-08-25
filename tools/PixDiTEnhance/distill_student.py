"""Distil the PIXL PixDiT teacher into a one-forward-pass student."""

from __future__ import annotations

import argparse
import time
from pathlib import Path

import torch
from torch import nn
from torch.nn import functional as F
from torch.utils.data import DataLoader

from model_pixdit import PixDiTConfig, PixDiTStudent, PixDiTTeacher, parameter_count
from pixdit_common import (
    PixDiTPairedDataset,
    charbonnier_loss,
    checkpoint_load,
    choose_device,
    ensure_finite,
    image_psnr,
    save_tensor_preview,
    set_deterministic_seed,
    write_json,
)


class MultiScalePerceptualLoss(nn.Module):
    """Frozen, dependency-free perceptual structure loss.

    Production training can replace this module with DINOv2 features. This
    validation backend intentionally avoids network downloads while retaining
    multi-scale colour, edge, and local-structure supervision.
    """

    def __init__(self) -> None:
        super().__init__()
        sobel_x = torch.tensor(
            [[-1.0, 0.0, 1.0], [-2.0, 0.0, 2.0], [-1.0, 0.0, 1.0]]) / 8.0
        sobel_y = sobel_x.t()
        self.register_buffer("sobel_x", sobel_x.view(1, 1, 3, 3).repeat(3, 1, 1, 1))
        self.register_buffer("sobel_y", sobel_y.view(1, 1, 3, 3).repeat(3, 1, 1, 1))

    def forward(self, prediction: torch.Tensor, target: torch.Tensor) -> torch.Tensor:
        loss = prediction.new_zeros(())
        pred_scale = prediction
        target_scale = target
        for scale_index in range(3):
            weight = 1.0 / float(2 ** scale_index)
            loss = loss + F.l1_loss(pred_scale, target_scale) * weight
            pred_x = F.conv2d(pred_scale, self.sobel_x, padding=1, groups=3)
            target_x = F.conv2d(target_scale, self.sobel_x, padding=1, groups=3)
            pred_y = F.conv2d(pred_scale, self.sobel_y, padding=1, groups=3)
            target_y = F.conv2d(target_scale, self.sobel_y, padding=1, groups=3)
            loss = loss + (F.l1_loss(pred_x, target_x) + F.l1_loss(pred_y, target_y)) * weight * 0.5
            if scale_index < 2:
                pred_scale = F.avg_pool2d(pred_scale, kernel_size=2, stride=2)
                target_scale = F.avg_pool2d(target_scale, kernel_size=2, stride=2)
        return loss


@torch.no_grad()
def build_teacher_targets(
    teacher: PixDiTTeacher,
    dataset: PixDiTPairedDataset,
    device: torch.device,
    steps: int,
    batch_size: int,
) -> dict[int, torch.Tensor]:
    loader = DataLoader(dataset, batch_size=batch_size, shuffle=False, num_workers=0)
    targets: dict[int, torch.Tensor] = {}
    teacher.eval()
    for batch in loader:
        rgbd = batch["rgbd"].to(device)
        output = teacher.enhance(rgbd, steps).cpu()
        for position, index in enumerate(batch["index"].tolist()):
            targets[int(index)] = output[position]
    if len(targets) != len(dataset):
        raise RuntimeError("Teacher target cache is incomplete")
    return targets


@torch.no_grad()
def validate(
    student: PixDiTStudent,
    loader: DataLoader,
    device: torch.device,
    perceptual: MultiScalePerceptualLoss,
) -> tuple[float, float, float, torch.Tensor]:
    student.eval()
    losses: list[float] = []
    baseline_psnr: list[float] = []
    student_psnr: list[float] = []
    last_output: torch.Tensor | None = None
    for batch in loader:
        rgbd = batch["rgbd"].to(device)
        target = batch["target"].to(device)
        output = student(rgbd)
        loss = charbonnier_loss(output, target) + perceptual(output, target) * 0.10
        ensure_finite("student validation loss", loss)
        losses.append(float(loss.item()))
        baseline_psnr.append(image_psnr(rgbd[:, :3], target))
        student_psnr.append(image_psnr(output, target))
        last_output = output
    assert last_output is not None
    return (
        sum(losses) / len(losses),
        sum(baseline_psnr) / len(baseline_psnr),
        sum(student_psnr) / len(student_psnr),
        last_output,
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", type=Path, required=True)
    parser.add_argument("--teacher", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--epochs", type=int, default=50)
    parser.add_argument("--batch-size", type=int, default=4)
    parser.add_argument("--learning-rate", type=float, default=1.5e-4)
    parser.add_argument("--teacher-steps", type=int, default=8)
    parser.add_argument("--device", default="auto")
    parser.add_argument("--seed", type=int, default=20260826)
    args = parser.parse_args()

    set_deterministic_seed(args.seed)
    device = choose_device(args.device)
    checkpoint = checkpoint_load(args.teacher, "cpu")
    config = PixDiTConfig.from_dict(checkpoint["config"])
    teacher = PixDiTTeacher(config).to(device)
    teacher.load_state_dict(checkpoint["model"], strict=True)
    teacher.eval()
    for parameter in teacher.parameters():
        parameter.requires_grad_(False)

    student = PixDiTStudent(config).to(device)
    # The velocity teacher and direct-residual student share the same tensor
    # structure. Warm-starting accelerates the small validation distillation.
    student.core.load_state_dict(teacher.core.state_dict(), strict=True)
    count = parameter_count(student)
    if not 5_000_000 <= count <= 15_000_000:
        raise AssertionError(f"Student parameter target missed: {count:,}")

    train_dataset = PixDiTPairedDataset(args.data, "train")
    val_dataset = PixDiTPairedDataset(args.data, "val")
    train_loader = DataLoader(
        train_dataset, batch_size=args.batch_size, shuffle=True, num_workers=0)
    val_loader = DataLoader(
        val_dataset, batch_size=args.batch_size, shuffle=False, num_workers=0)
    teacher_targets = build_teacher_targets(
        teacher, train_dataset, device, args.teacher_steps, args.batch_size)
    perceptual = MultiScalePerceptualLoss().to(device)
    optimizer = torch.optim.AdamW(
        student.parameters(), lr=args.learning_rate, betas=(0.9, 0.95), weight_decay=0.01)
    use_amp = device.type == "cuda"
    scaler = torch.amp.GradScaler("cuda", enabled=use_amp)

    args.output.mkdir(parents=True, exist_ok=True)
    best_loss = float("inf")
    initial_loss: float | None = None
    final_loss = float("inf")
    max_gradient_norm = 0.0
    started = time.perf_counter()

    for epoch in range(args.epochs):
        student.train()
        epoch_losses: list[float] = []
        for batch in train_loader:
            rgbd = batch["rgbd"].to(device)
            target = batch["target"].to(device)
            distilled_target = torch.stack(
                [teacher_targets[int(index)] for index in batch["index"].tolist()]
            ).to(device)

            optimizer.zero_grad(set_to_none=True)
            with torch.autocast(
                device_type=device.type,
                dtype=torch.float16 if use_amp else torch.float32,
                enabled=use_amp,
            ):
                output = student(rgbd)
                teacher_loss = charbonnier_loss(output, distilled_target)
                reconstruction_loss = charbonnier_loss(output, target)
                perceptual_loss = perceptual(output, target)
                loss = teacher_loss * 0.50 + reconstruction_loss * 0.30 + perceptual_loss * 0.20
            ensure_finite("student training loss", loss)
            if initial_loss is None:
                initial_loss = float(loss.detach().item())
            scaler.scale(loss).backward()
            scaler.unscale_(optimizer)
            gradient_norm = torch.nn.utils.clip_grad_norm_(student.parameters(), max_norm=1.0)
            ensure_finite("student gradient norm", gradient_norm)
            max_gradient_norm = max(max_gradient_norm, float(gradient_norm.item()))
            scaler.step(optimizer)
            scaler.update()
            epoch_losses.append(float(loss.detach().item()))

        final_loss = sum(epoch_losses) / len(epoch_losses)
        validation_loss, baseline_psnr, student_psnr, preview = validate(
            student, val_loader, device, perceptual)
        if validation_loss < best_loss:
            best_loss = validation_loss
            torch.save(
                {
                    "format": "PIXL.PixDiT.Student1Step.v1",
                    "config": config.to_dict(),
                    "model": student.state_dict(),
                    "epoch": epoch + 1,
                    "validation_loss": validation_loss,
                    "inference_steps": 1,
                },
                args.output / "student_1step.pt",
            )
            save_tensor_preview(preview, args.output / "student_preview.png")
        if epoch == 0 or (epoch + 1) % 5 == 0 or epoch + 1 == args.epochs:
            print(
                f"student epoch {epoch + 1:03d}/{args.epochs}: "
                f"train={final_loss:.6f} val={validation_loss:.6f} "
                f"PSNR={student_psnr:.2f}dB")

    elapsed = time.perf_counter() - started
    if initial_loss is None:
        raise RuntimeError("Student received no training batches")
    student_path = args.output / "student_1step.pt"
    if not student_path.is_file():
        raise RuntimeError("One-step student checkpoint was not produced")

    # Architectural assertion: the exported model has one input-to-output call,
    # with no iterative scheduler state or loop argument.
    sample = next(iter(val_loader))["rgbd"][:1].to(device)
    with torch.no_grad():
        single_output = student(sample)
    if single_output.shape != (1, 3, sample.shape[2], sample.shape[3]):
        raise AssertionError("Student single-forward output contract failed")

    metrics = {
        "format": "PIXL.PixDiT.StudentMetrics.v1",
        "device": str(device),
        "epochs": args.epochs,
        "batch_size": args.batch_size,
        "teacher_steps": args.teacher_steps,
        "student_inference_steps": 1,
        "perceptual_backend": "frozen_multiscale_edges_and_colour",
        "parameter_count": count,
        "initial_training_loss": initial_loss,
        "final_training_loss": final_loss,
        "best_validation_loss": best_loss,
        "baseline_validation_psnr_db": baseline_psnr,
        "student_validation_psnr_db": student_psnr,
        "max_preclip_gradient_norm": max_gradient_norm,
        "elapsed_seconds": elapsed,
        "finite": True,
    }
    write_json(args.output / "student_metrics.json", metrics)
    print(f"One-step distillation complete in {elapsed:.1f}s; best validation={best_loss:.6f}")


if __name__ == "__main__":
    main()

