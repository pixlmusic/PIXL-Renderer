# Directional Visibility 4.3.0 Report

## Existing implementation retained

PIXL already generated bent normals and directional visibility inside HybridGI, reprojected and temporally resolved the result, reconstructed it to full resolution, and consumed it in DeferredComposite for ambient direction, SkyBounce/AmbientProbe evaluation and rough-probe specular occlusion. No new pass, texture, constant-buffer field or setting was added. The reconstruction shader now binds HybridGI's existing normal pyramid at its previously unused `t6` slot.

## Defects corrected

### Octahedral history interpolation

The previous reprojection path bilinearly accumulated encoded octahedral `xy` values. Oct coordinates are not linear vectors and can interpolate incorrectly across the oct seam. Accepted history taps are now decoded to world-space vectors, confidence-weighted, accumulated, normalized with a strongest-sample cancellation fallback, then encoded once.

### Observation confidence

The stored confidence previously represented only HybridGI's depth-distance fade. It now also accounts for:

- clipped horizon footprints near viewport edges;
- reduced adaptive slice/step budgets;
- the existing distance validity range.

Viewport fading is bounded to at most 64 pixels so it behaves as a stability guard rather than a visible vignette.

### Single confidence application

Bent direction was formerly blended toward the geometric normal during generation and blended again by confidence in DeferredComposite. Direction confidence is now applied once by the consumer. Visibility continues to fade toward fully unoccluded when observation confidence is low.

### Geometry-reactive temporal response

Bent history formerly reused only the GI luminance blend factor. It now reacts to:

- angular disagreement;
- directional-visibility change;
- confidence change.

Near-opposite history/current vectors have a safe fallback instead of risking a zero-vector normalize.

### Normal-aware reconstruction

Half- and quarter-resolution reconstruction previously rejected only across depth discontinuities. Surfaces meeting at a crease can have nearly identical depth, allowing AO, radiance, reflections and bent direction to bleed between differently oriented receivers. Upsampling now compares the full-resolution receiver normal to the four low-resolution candidates and uses a steep normal-similarity weight whenever either depth or orientation indicates an edge. This reuses the normal pyramid HybridGI already creates; it allocates no resource and adds no pass.

## Validation completed

- Strict FXC `/Ges /WX /O3` passes full, half and quarter-resolution `gi.cs.hlsl` variants.
- Strict FXC passes full, half and quarter-resolution `radianceDisocc.cs.hlsl` variants.
- Strict FXC passes half and quarter-resolution normal-aware `upsample.cs.hlsl` variants.
- Adaptive ray allocation, temporal denoising, GI, Hybrid Reflections and non-reflection permutations are represented.
- No CPU/HLSL cbuffer, resource format, UAV register or dispatch dimension changed; the existing normal-pyramid SRV is additionally bound at upsample `t6`.

## Runtime validation pending

- camera orbit around railings, foliage and thin silhouettes;
- caves, eaves, stairs and overhangs;
- fast turns, FOV changes, teleports and cell transitions;
- full/half/quarter HybridGI modes;
- TAA and DLSS;
- rough metal and glossy stone across screen/world/probe reflection transitions;
- Bent Normal, Directional Visibility and Physical Specular Occlusion debug views.
