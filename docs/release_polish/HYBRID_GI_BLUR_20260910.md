# Hybrid GI blur corrections — September 10

## Scope

Implemented the first two corrections from the read-only fidelity review.
No changes to frame generation, reconstruction sidecars, atmosphere settings,
shared buffers, resource slots or world-cache payloads. The unrelated local
FidelityFX build-path patch is preserved.

## Files and findings

- `pipeline/Hybrid GI/Kernels/HybridGI/blur.cs.hlsl`: active diffuse denoiser,
  dispatched once or twice by `HybridGI::Draw`'s blur section. Inputs are depth,
  normal/roughness, normalized history count, luminance SH and chroma. Outputs
  and b1/t0–t4/u0–u2 contracts remain unchanged. The live shader initially
  matched the repository by hash.
  - A: replaced `sign(N.z)` with a nonzero sign convention, preventing division
    by zero at equatorial normals while preserving the existing basis elsewhere.
  - B: decoded R8_UNORM history (`count / 255`) before comparing against
    `MaxAccumFrames`. Added saturation and a denominator guard. The converged
    normal-rejection half-angle is now 18 degrees rather than approximately
    85.5 degrees. History storage and spatial sample count remain unchanged.
  - Expected benefit: preserve settled indirect-light detail across normal
    boundaries and remove invalid basis calculations. Noise/crease contrast
    need visual retesting; no measured fidelity or GPU-time claim.
  - Rejected: changing filter radius, sample counts, or resource interfaces in
    this patch. Future: GPU image regression fixtures and material-edge tests.
- `tools/TestPixlHybridGIBlur.ps1`: new read-only source/reference-math and FXC
  test. Checks source expressions, 1,006 axis/equator/random normal cases,
  history monotonicity and convergence for budgets 1–64, and all 12 combinations
  of resolution, temporal filtering and atrous pass. Explicit compiler/include
  paths; artifacts only under a unique build directory. No game or cache access
  is modified. These are reference-math tests, not GPU numerical execution.
  Future: run production shader output against synthetic geometry with WARP.

## Validation

- All reference-math cases passed.
- All 12 blur variants passed FXC SM5 strictness and warnings-as-errors, using
  the active live include tree and repository shader as compilation input.
- Existing Release CMake build passed; no C++ changes required.
- CPU constant layout and shader register declarations are unchanged.
- No cache deletion or full-cache rebuild was requested. Test CSOs are separate
  build artifacts, not replacements for the game's shader cache.

## Deferred cache work

Brightening-response and competing-surface selection changes remain deferred.
The current cache has one payload per cell and no independent record proving
that a brightness change persists. Increasing response based only on existing
confidence could strengthen repeated outliers. Replacing the atomic winner
policy requires a separately validated selection/reduction strategy. Do not
mix these higher-risk changes into the denoiser correction.

Owner validation: compare stationary and slowly panning views of wall corners,
foliage and reflective surfaces, then moving torchlight. Check noise with DLSS
and TAA. This report does not certify the full Hybrid GI subsystem for release.
