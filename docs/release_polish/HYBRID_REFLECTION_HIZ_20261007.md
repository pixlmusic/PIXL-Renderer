# Hybrid Reflection hierarchical-Z traversal

## Scope and source ownership

Controlled improvement (B), on the 1.0.6 safe-performance source. Authoritative
shaders are `pipeline/Hybrid GI/Kernels/HybridGI`; the standalone staging script
combines module Kernels with `distribution/Shaders` into runtime `Data/Shaders`.
No live files, shader cache, public version, hook ordering or settings are changed.

## Implementation

- Replaced quadratic fixed-distance samples and heuristic mip selection with
  projected cell-boundary traversal. Empty cells ascend; possible intersections
  descend to full-resolution depth. Reciprocal depth gives perspective-correct
  intersection positions, including camera-facing rays.
- Retained the trace test limit, distance, roughness, thickness, normal-facing
  rejection and viewport fade. Misses still use the existing world-cache and
  environment fallback; denoising and history reconstruction are unchanged.
- GI's existing LINEAR_FILTER pyramid sometimes averages smooth depth. It is
  not a conservative occlusion bound. Reflections therefore use a separate
  five-level R32_FLOAT MIN_FILTER pyramid, lazily allocated once when needed
  and rebuilt only on reflection-active frames. R32 avoids upward R16 rounding.
- The producer uses exact pixel Loads, far-depth padding for inactive lanes,
  and bounds-checked mip writes. All padded lanes still execute group barriers.
  NPOT trailing columns/rows descend to finer levels rather than clamping to an
  unrelated coarse cell. Active UVs use FrameDim, not the backing dimensions.
- CS t11 is local to the reflection dispatch, inside the existing t0..t16
  reset range. Existing resetViews unbinds pyramid UAVs before SRV consumption.
  Constant buffers b1/b5/b6 and the CPU/HLSL ABI are unchanged.
- Resource setup/recreation drops the pyramid and resets the one-attempt lazy
  creation guard. Creation/compiler failure suppresses this optional reflection
  path while preserving diffuse GI and ordinary environment reflection.

## Cost and rejected alternatives

This adds one profiled `HybridGI::ReflectionHiZ` compute dispatch and about
10.54 MiB at 1920x1080 (five R32 mips). No recurring texture allocation or
CPU readback. The hierarchy is retained when toggled off, but no build/trace
dispatch runs while disabled. Reusing the averaged GI pyramid was rejected:
it cannot prove empty space. Changing GI's depth semantics or shared ABI was
also rejected to protect diffuse and world-cache lighting.

Traversal can skip many empty pixels, but it is bounded by the existing
8..64 tests. Dense overlapping silhouettes can exhaust that budget; misses
retain existing fallback behavior. No measured performance win or identical
live image is claimed. Compare the sum of ReflectionHiZ and HybridReflections
GPU timings against the previous reflection trace, not the trace alone.

## Validation

CPU reference tests compare hierarchical traversal against pixel-cell traversal
for thin silhouettes, slopes, depth edges, positive/negative/diagonal rays,
camera-facing rays, 100/66.67/50% active size, NPOT tails, stationary projected
rays and empty-space skipping. They also check perspective depth and viewport
clipping. These tests validate the algorithm contract, not GPU execution.

Strict FXC permutations include MIN_FILTER and unchanged LINEAR_FILTER producers,
Full/Half/Quarter reflection kernels and all runtime module switches. The
reflection publication contract test checks the actual producer and t11 binding.
Final validation: Release `PIXLRenderer` build succeeded; full CTest 22/22
passed (21 portable plus repository audit); CPU reference 648 traversal
comparisons passed; strict FXC `/WX /Ges /O3` matrix 684/684 passed, followed
by 48/48 final reflection permutations after the distance-thickness refinement.
`git diff --check` passed. The initial incomplete test staging omitted
RadiantGrid includes; staging all module Kernels corrected that harness issue
before the successful matrix run. No shader/compiler failure remains.

## LIVE VALIDATION REQUIRED

Check thin rails and door frames, mirrors/grazing surfaces, first-person geometry,
near-plane crossings, interiors/exteriors, water-adjacent objects, fast turns,
teleports, dynamic resolution and Full/Half/Quarter GI modes. Confirm no new
reflection leaking, self-hits, edge popping or temporal instability. Confirm
diffuse SSGI and voxel/world-cache lighting remain present. Capture GPU timings
for both hierarchy build and traversal before judging net benefit.
