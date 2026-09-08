# Landscape and horizon review — September 7

## Scope

Reviewed Distance Blend, Terrain Detail, Horizon Blend, their settings ABI and module metadata, and the canonical `Lighting.hlsl` terrain entry path. Repository shaders are authoritative; repository and deployed `Lighting.hlsl` matched before this fix, so only the source copy was changed.

## Five release improvements investigated

1. **Independent Terrain Detail compilation (implemented, low risk).** Terrain Detail was registered independently, but `sharedOffset` was declared only inside `EMAT`; LANDSCAPE + TERRAIN_DETAIL without Material Layers therefore failed FXC. The declaration is now available for the independent path while preserving the zero-offset EMAT path.
2. **Distance Blend reload validation (implemented, low risk).** Reload now applies the same finite/range bounds as the UI, preventing invalid exponents and brightness from reaching lighting.
3. **Settings ABI protection (implemented, low risk).** Distance Blend and Terrain Detail settings now assert size/offset contracts and canonicalize boolean fields.
4. **Horizon lifecycle logging (implemented, low risk).** Disabled/unloaded Horizon Blend exits before probing/logging, avoiding a false enabled state.
5. **Temporal/LOD behavior review (documented, no speculative rewrite).** Terrain Detail's derivative footprint, stochastic jitter, distance blend dependency, and Horizon conventional-depth assumptions need motion captures across grazing angles, teleports, interiors, and reversed-Z variants before changing sampling or depth math.

## Validation

`tools/TestPixlLandscapeSettings.ps1` passed ABI, bounds, non-finite, round-trip, reset and lifecycle cases. `tools/TestPixlLandscapeShaders.ps1` passed 72 selected strict FXC VS/PS cases, including independent and Material Layers terrain permutations. This is selected coverage, not exhaustive runtime permutation proof.

## Remaining risk

No in-game capture or GPU timing is available. The high-risk future work is cascaded/relocated SkyBounce-style probe scheduling and more anisotropic terrain filtering; both remain roadmap items.
