# Phase 4 — Shared Hierarchical Depth and Visibility

## Outcome

The proven Foliage Optimizer max-depth pyramid is now owned through renderer-level `VisibilityContext`. This avoids introducing a second full depth reduction and establishes one conservative visibility source for later PIXL consumers.

## Architecture

- Renderer setup initializes the shared context.
- `Build` is frame-indexed and cannot dispatch the same pyramid twice in one frame.
- The service exposes the full-chain SRV, valid dimensions, mip count, texel scale and explicit depth convention.
- Current Skyrim depth is confirmed as standard 0-near/1-far for this path; maxima are stored for fail-open conservative tests.
- Resolution changes, history invalidation and camera-ownership discontinuities invalidate visibility.
- Resource setup drops stale pyramid resources before recreating them.
- `Common/PIXLVisibility.hlsli` centralizes conservative depth-query semantics.
- Developer UI reports validity, dimensions, mip count, scale, builds and invalidation reason.

## Consumer Migration

Foliage Optimizer no longer owns a `HiZPyramid` member. It builds and samples the renderer service while preserving its existing settings, culling constants, t2 binding, dispatch order and vanilla fallback. If depth, shaders or resources are unavailable, `HiZEnabled` remains zero and grass is not occlusion-culled.

The existing pyramid implementation remains in its historical source directory as a compatibility implementation detail. Moving files physically is deferred to avoid noisy provenance changes; ownership is now renderer-level.

## Shader and Cache Impact

`GrassCullingCS.hlsl` consumes the shared helper without changing its entry point, profile, cbuffer or resource registers. The shared shader revision was deliberately bumped to `PIXL.Shaders.20261003.Visibility1` because a compiled permutation include changed.

## Validation

- Release `PIXLRenderer.dll`: built successfully.
- `PIXL-Audit`: passed; 42 shipping modules retained.
- Strict FXC `/WX /Ges` validation: `GrassHiZCS` and `GrassCullingCS` passed as SM5 compute shaders.
- Static consumer validation confirms no private `hiZ` calls remain in Foliage Optimizer.
- C++/HLSL culling buffer layout and shader registers: unchanged.
- Package source path: shared include is under authoritative `distribution/Shaders/Common`; module kernels remain under `pipeline/Foliage Optimizer/Kernels` and are staged by the existing pipeline copier.
- Live Skyrim occlusion and camera-transition validation: pending.
