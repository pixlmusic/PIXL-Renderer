# PIXL Glints2023 / Complex Materials Artifact Repair

## Confirmed root cause

`GLINT` is a shader permutation selected through the PBR reuse of the legacy
`AnisoLighting` technique bit.  A Material Forge draw could retain that bit even
when it had no explicit PIXL Glint trait.  In a `GLINT` permutation,
`Lighting.hlsl` initialized `glintParameters` to zero and decoded it as:

```hlsl
logDensity = clamp(40 - glintParameters.y, 1, 40);
```

Consequently, an ordinary material with no glint payload decoded as density 40.
`PBR::SpecularMicrofacetWithGlint` then replaced the continuous GGX NDF with the
stochastic Glints2023 NDF.  The resulting sparse high energy microfacets and
intervening dark samples match the white/dark square speckles on ordinary wood.

This was separate from the Complex Material GGX fixes already present in
`Lighting.hlsl`: the screenshot was exercising an unintended Glint path, rather
than a valid GGX lobe on wood.

## Complete active path

* `engine/MaterialForge.cpp` turns an explicit `PhysicalMaterial::Glint` trait
  into `PBRShaderFlags::Glint` and writes its parameters to
  `MultiLayerParallaxData`.  Landscape layers use their six per-layer glint
  constants.
* `engine/ShaderCache.cpp` maps Material Forge's reused `AnisoLighting` bit to
  the `GLINT` shader define.
* `distribution/Shaders/Lighting.hlsl` reconstructs PBR material data and
  prepares a cached glint footprint.
* `distribution/Shaders/Common/PBR.hlsli` calls
  `Glints::SampleGlints2023NDF` only when the decoded log density exceeds 1.1.
  That sample **replaces** `BRDF::D_GGX`, which is correct only for a declared
  stochastic glint material.
* `distribution/Shaders/Common/Glints/Glints2023.hlsli` samples the generated
  `t20` Glint noise map.  `engine/MaterialForge.cpp` creates it as
  `R32G32B32A32_FLOAT`, 128x128, with SRV and UAV access, and dispatches the
  32x32 noise compute shader as four groups in each dimension.

## Conventions verified

* `material.Roughness` is perceptual roughness from the RMAOS material channel;
  `BRDF::D_GGX` performs its own GGX alpha conversion.
* The Glints function's `roughness` parameter is instead the explicit authored
  **microfacet slope roughness** (`GlintMicrofacetRoughness`), clamped to the
  Glint range.  It was not receiving the material's perceptual roughness, so a
  gloss/roughness double conversion was not the fault in this scene.
* `targetNDF` is the continuous GGX D at the current half vector and `maxNDF`
  is GGX D at `NdotH = 1`.  Their ratio is a normalized glint selection input.
* The glint sampler expects a tangent-space half vector and uses its `xy`
  components as the algorithm's orthographic slope grid.  The prior caller
  supplied only `.x`, which HLSL splatted into a diagonal vector.

## Repair

1. Material Forge now clears its inherited `AnisoLighting` bit for each PBR
   draw before adding it back only for an explicit Glint trait.  This avoids
   compiling the costly glint permutation for ordinary PBR surfaces.
2. `Lighting.hlsl` tracks explicit glint payload coverage independently.  Zero
   constants now resolve to the disabled density sentinel, even if a shared
   `GLINT` permutation is encountered.
3. The complete tangent-space half vector is passed to Glints2023.
4. Glints2023 now rejects degenerate UV derivative ellipses and falls back to
   continuous GGX for that pixel.  Its inverse-CDF, determinant, eigenvalue,
   reciprocal, LOD, probability, variance and NDF-ratio domains are protected
   before an invalid value can reach deferred lighting.
5. Tetrahedron selection now normalizes barycentric probabilities by their sum,
   rather than by Euclidean vector length.
6. The generated noise shader bounds-checks arbitrary dimensions, uses explicit
   unsigned wrapping for its hash salt, and keeps inverse-CDF samples inside the
   open interval.

## Expected visual result

Ordinary wood, stone and standard Complex Materials retain their normal
continuous GGX response with the existing Complex Material specular-AA work.
Explicitly authored Glint materials retain their stochastic NDF, now with a
stable footprint, valid tangent-space slope, and bounded numerical domains.

## Validation performed

* `noisegen.cs.hlsl` compiled with `fxc /T cs_5_0`.
* `Lighting.hlsl` compiled with the `GLINT` define using the valid deferred
  legacy shader permutation.
* `cmake --build --preset PIXL-12C` succeeded and produced
  `build/PIXL-12C/Release/PIXLRenderer.dll`.
* The Material Forge module version is now `1-1-2`, requiring a fresh affected
  runtime shader cache entry.

## Live test sequence

1. Inspect the supplied log-wall scene with Material Forge enabled: square
   white and black pins must be gone from ordinary wood.
2. Repeat at grazing view angles and while moving the camera; no flickering
   pepper pattern should appear.
3. Test a declared Glint material, if installed: it should retain controlled
   sparkling detail without black punctures.
4. Toggle Complex Materials and Specular AA independently to verify their GGX
   behaviour remains stable.
