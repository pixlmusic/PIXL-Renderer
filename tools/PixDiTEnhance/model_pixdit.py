"""Compact pixel-space diffusion transformer for PIXL Photo Mode Enhance.

The architecture keeps the requested 4x4 patchification but avoids global
quadratic attention at 512x512. Attention operates in 8x8 token windows, while a
depthwise convolution in every block transfers information across window edges.
This keeps inference practical for DirectML and CUDA without a VAE.
"""

from __future__ import annotations

import argparse
import math
from dataclasses import asdict, dataclass

import torch
from torch import nn
from torch.nn import functional as F


@dataclass(frozen=True)
class PixDiTConfig:
    patch_size: int = 4
    dim: int = 256
    depth: int = 8
    heads: int = 8
    window_size: int = 8
    mlp_ratio: float = 3.0
    input_channels: int = 7  # evolving RGB state + RGBD condition
    output_channels: int = 3
    max_residual: float = 0.35

    @classmethod
    def from_dict(cls, values: dict[str, object]) -> "PixDiTConfig":
        known = {field.name for field in cls.__dataclass_fields__.values()}
        return cls(**{key: value for key, value in values.items() if key in known})

    def to_dict(self) -> dict[str, object]:
        return asdict(self)


class SinusoidalTimeEmbedding(nn.Module):
    def __init__(self, dim: int) -> None:
        super().__init__()
        if dim % 2 != 0:
            raise ValueError("Time embedding dimension must be even")
        self.dim = dim

    def forward(self, timestep: torch.Tensor) -> torch.Tensor:
        half = self.dim // 2
        frequencies = torch.exp(
            -math.log(10000.0)
            * torch.arange(half, device=timestep.device, dtype=timestep.dtype)
            / max(half - 1, 1)
        )
        phase = timestep[:, None] * frequencies[None, :] * 1000.0
        return torch.cat((torch.sin(phase), torch.cos(phase)), dim=-1)


class WindowAttention(nn.Module):
    """Export-friendly multi-head self-attention within local token windows."""

    def __init__(self, dim: int, heads: int, window_size: int) -> None:
        super().__init__()
        if dim % heads != 0:
            raise ValueError("Embedding dimension must be divisible by head count")
        self.dim = dim
        self.heads = heads
        self.window_size = window_size
        self.head_dim = dim // heads
        self.scale = self.head_dim ** -0.5
        self.qkv = nn.Linear(dim, dim * 3)
        self.projection = nn.Linear(dim, dim)

    def forward(self, tokens: torch.Tensor, height: int, width: int) -> torch.Tensor:
        batch, token_count, channels = tokens.shape
        if not torch.jit.is_tracing():
            if token_count != height * width:
                raise RuntimeError("Token grid dimensions are inconsistent")
            if height % self.window_size or width % self.window_size:
                raise RuntimeError("Token grid must be divisible by the attention window")

        spatial = tokens.reshape(batch, height, width, channels)
        windows = (
            spatial.reshape(
                batch,
                height // self.window_size,
                self.window_size,
                width // self.window_size,
                self.window_size,
                channels,
            )
            .permute(0, 1, 3, 2, 4, 5)
            .reshape(-1, self.window_size * self.window_size, channels)
        )
        window_batch, window_tokens, _ = windows.shape
        qkv = (
            self.qkv(windows)
            .reshape(window_batch, window_tokens, 3, self.heads, self.head_dim)
            .permute(2, 0, 3, 1, 4)
        )
        query, key, value = qkv.unbind(dim=0)
        attention = torch.softmax(
            torch.matmul(query, key.transpose(-2, -1)) * self.scale,
            dim=-1,
        )
        mixed = (
            torch.matmul(attention, value)
            .transpose(1, 2)
            .reshape(window_batch, window_tokens, channels)
        )
        mixed = self.projection(mixed)
        return (
            mixed.reshape(
                batch,
                height // self.window_size,
                width // self.window_size,
                self.window_size,
                self.window_size,
                channels,
            )
            .permute(0, 1, 3, 2, 4, 5)
            .reshape(batch, height * width, channels)
        )


class PixDiTBlock(nn.Module):
    def __init__(self, config: PixDiTConfig) -> None:
        super().__init__()
        hidden = int(config.dim * config.mlp_ratio)
        self.dim = config.dim
        self.position_mixer = nn.Conv2d(
            config.dim, config.dim, kernel_size=3, padding=1, groups=config.dim)
        self.norm_attention = nn.LayerNorm(config.dim, elementwise_affine=False)
        self.attention = WindowAttention(config.dim, config.heads, config.window_size)
        self.norm_mlp = nn.LayerNorm(config.dim, elementwise_affine=False)
        self.mlp = nn.Sequential(
            nn.Linear(config.dim, hidden),
            nn.GELU(approximate="tanh"),
            nn.Linear(hidden, config.dim),
        )
        self.time_modulation = nn.Sequential(
            nn.SiLU(),
            nn.Linear(config.dim, config.dim * 4),
        )

    def forward(
        self,
        tokens: torch.Tensor,
        time_embedding: torch.Tensor,
        height: int,
        width: int,
    ) -> torch.Tensor:
        batch = tokens.shape[0]
        spatial = tokens.transpose(1, 2).reshape(batch, self.dim, height, width)
        tokens = tokens + self.position_mixer(spatial).flatten(2).transpose(1, 2)

        shift_attention, scale_attention, shift_mlp, scale_mlp = (
            self.time_modulation(time_embedding).chunk(4, dim=-1)
        )
        attention_input = self.norm_attention(tokens)
        attention_input = attention_input * (1.0 + scale_attention[:, None, :])
        attention_input = attention_input + shift_attention[:, None, :]
        tokens = tokens + self.attention(attention_input, height, width)

        mlp_input = self.norm_mlp(tokens)
        mlp_input = mlp_input * (1.0 + scale_mlp[:, None, :])
        mlp_input = mlp_input + shift_mlp[:, None, :]
        return tokens + self.mlp(mlp_input)


class PixDiTCore(nn.Module):
    """Shared transformer core used by the multi-step teacher and one-step student."""

    def __init__(self, config: PixDiTConfig) -> None:
        super().__init__()
        self.config = config
        self.patch_embedding = nn.Conv2d(
            config.input_channels,
            config.dim,
            kernel_size=config.patch_size,
            stride=config.patch_size,
        )
        self.time_embedding = nn.Sequential(
            SinusoidalTimeEmbedding(config.dim),
            nn.Linear(config.dim, config.dim * 2),
            nn.SiLU(),
            nn.Linear(config.dim * 2, config.dim),
        )
        self.blocks = nn.ModuleList(PixDiTBlock(config) for _ in range(config.depth))
        self.output_norm = nn.LayerNorm(config.dim)
        self.output_projection = nn.ConvTranspose2d(
            config.dim,
            config.output_channels,
            kernel_size=config.patch_size,
            stride=config.patch_size,
        )
        self.apply(self._initialize)

    @staticmethod
    def _initialize(module: nn.Module) -> None:
        if isinstance(module, (nn.Linear, nn.Conv2d, nn.ConvTranspose2d)):
            nn.init.xavier_uniform_(module.weight)
            if module.bias is not None:
                nn.init.zeros_(module.bias)

    def forward(
        self,
        state_rgb: torch.Tensor,
        condition_rgbd: torch.Tensor,
        timestep: torch.Tensor,
    ) -> torch.Tensor:
        time_embedding = self.time_embedding(timestep.reshape(state_rgb.shape[0]))
        return self.forward_with_time_embedding(state_rgb, condition_rgbd, time_embedding)

    def forward_with_time_embedding(
        self,
        state_rgb: torch.Tensor,
        condition_rgbd: torch.Tensor,
        time_embedding: torch.Tensor,
    ) -> torch.Tensor:
        """Run the transformer with a projected timestep embedding.

        The teacher uses the normal sinusoidal path.  The one-step student can
        supply its constant t=0 embedding directly, removing Sin/Cos operators
        from the deployed ONNX graph and improving DirectML portability.
        """
        if not torch.jit.is_tracing():
            if state_rgb.ndim != 4 or condition_rgbd.ndim != 4:
                raise RuntimeError("PixDiT expects NCHW tensors")
            if state_rgb.shape[1] != 3 or condition_rgbd.shape[1] != 4:
                raise RuntimeError("PixDiT expects state RGB plus condition RGBD")
            if state_rgb.shape[0] != condition_rgbd.shape[0] or state_rgb.shape[2:] != condition_rgbd.shape[2:]:
                raise RuntimeError("State and condition dimensions must match")

            patch_span = self.config.patch_size * self.config.window_size
            if state_rgb.shape[2] % patch_span or state_rgb.shape[3] % patch_span:
                raise RuntimeError(f"Height and width must be divisible by {patch_span}")

        features = self.patch_embedding(torch.cat((state_rgb, condition_rgbd), dim=1))
        batch, channels, height, width = features.shape
        tokens = features.flatten(2).transpose(1, 2)
        for block in self.blocks:
            tokens = block(tokens, time_embedding, height, width)
        features = self.output_norm(tokens).transpose(1, 2).reshape(batch, channels, height, width)
        return self.output_projection(features)


class PixDiTTeacher(nn.Module):
    """Flow-matching teacher whose velocity can be integrated over several steps."""

    def __init__(self, config: PixDiTConfig | None = None) -> None:
        super().__init__()
        self.config = config or PixDiTConfig()
        self.core = PixDiTCore(self.config)

    def forward(
        self,
        state_rgb: torch.Tensor,
        condition_rgbd: torch.Tensor,
        timestep: torch.Tensor,
    ) -> torch.Tensor:
        return torch.tanh(self.core(state_rgb, condition_rgbd, timestep)) * self.config.max_residual

    @torch.no_grad()
    def enhance(self, condition_rgbd: torch.Tensor, steps: int = 8) -> torch.Tensor:
        if steps < 1:
            raise ValueError("Teacher integration requires at least one step")
        state = condition_rgbd[:, :3]
        delta = 1.0 / float(steps)
        for index in range(steps):
            timestep = torch.full(
                (state.shape[0],),
                (index + 0.5) * delta,
                device=state.device,
                dtype=state.dtype,
            )
            state = (state + self(state, condition_rgbd, timestep) * delta).clamp(0.0, 1.0)
        return state


class PixDiTStudent(nn.Module):
    """One-step bounded residual enhancer exported to ONNX."""

    def __init__(self, config: PixDiTConfig | None = None) -> None:
        super().__init__()
        self.config = config or PixDiTConfig()
        self.core = PixDiTCore(self.config)
        # sin(0)=0 and cos(0)=1.  This non-persistent buffer is derived data,
        # so it neither changes checkpoint compatibility nor parameter count.
        half = self.config.dim // 2
        self.register_buffer(
            "zero_time_features",
            torch.cat((torch.zeros(half), torch.ones(half))),
            persistent=False,
        )

    def forward(self, rgbd: torch.Tensor) -> torch.Tensor:
        raw = rgbd[:, :3]
        time_features = self.zero_time_features.to(dtype=rgbd.dtype)
        time_features = time_features.unsqueeze(0).expand(rgbd.shape[0], -1)
        # Preserve the trained projection weights while bypassing only the
        # now-constant sinusoidal feature generator.
        time_embedding = self.core.time_embedding[1](time_features)
        time_embedding = self.core.time_embedding[2](time_embedding)
        time_embedding = self.core.time_embedding[3](time_embedding)
        residual = torch.tanh(
            self.core.forward_with_time_embedding(raw, rgbd, time_embedding)
        ) * self.config.max_residual
        return (raw + residual).clamp(0.0, 1.0)


def parameter_count(model: nn.Module) -> int:
    return sum(parameter.numel() for parameter in model.parameters())


def main() -> None:
    parser = argparse.ArgumentParser(description="Validate PIXL PixDiT tensor contracts")
    parser.add_argument("--resolution", type=int, default=64)
    args = parser.parse_args()
    model = PixDiTStudent()
    count = parameter_count(model)
    if not 5_000_000 <= count <= 15_000_000:
        raise AssertionError(f"Parameter target missed: {count:,}")
    dummy = torch.rand(1, 4, args.resolution, args.resolution)
    output = model(dummy)
    if output.shape != (1, 3, args.resolution, args.resolution):
        raise AssertionError(f"Unexpected output shape: {tuple(output.shape)}")
    if not torch.isfinite(output).all():
        raise FloatingPointError("Model self-test produced NaN/Inf")
    print(f"PixDiT self-test passed: {count:,} parameters, output={tuple(output.shape)}")


if __name__ == "__main__":
    main()
