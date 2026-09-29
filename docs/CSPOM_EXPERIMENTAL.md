# Curved Surface Mapping (CSPOM) — experimental prototype

## Status

Curved Surface Mapping is an opt-in Alpha module. It is disabled in the normal
renderer configuration and appears under **Experimental > Materials**.

The implementation is safe for live visual and performance testing. It has not
been visually validated in Skyrim. It is not classified as release ready.

## Architecture

CSPOM owns its user settings and module lifecycle. Material Layers remains the
owner of height textures, the established POM call sites, PS constant buffer
`b9`, and effects-depth reconstruction. This avoids another material resource
binding and preserves the existing renderer path when CSPOM is disabled.

The CPU copies the authoritative CSPOM settings into the appended, 16-byte
aligned `MaterialLayers::TuningSettings` transport before uploading PS `b9`.
The buffer signature was advanced from version 3 to version 4 and its C++ size
is statically asserted at 432 bytes.

Material precedence is:

1. WindowLife glass remains owned by WindowLife.
2. Ground Response remains authoritative for deformable snow and mud.
3. Water, actors, hair, eyes, grass, particles, alpha-tested geometry and LOD
   remain excluded.
4. Valid authored height from legacy parallax, Complex Material or
   MaterialForge is used first.
5. PIXL Auto POM remains the fallback height source for eligible incomplete
   opaque materials.
6. CSPOM changes the displacement trace and downstream relief signals; it does
   not replace MaterialForge or Complex Material BRDF shading.

## Implemented prototype path

- Distance, mip, angle and projected-shift quality scaling.
- A bounded 4–32 step CSPOM request with 2–6 hit refinement steps. The shared
  Material Layers path may retain its established 40-step safety ceiling when
  an existing material requests more work, so enabling CSPOM never lowers an
  authored material's prior trace quality.
- Existing authored displacement normalization and Auto POM structural
  confidence remain intact.
- Curvature correction derives a bounded local metric from normal derivatives.
- Silhouette clipping limits unsafe texel travel and fades extreme edge rays
  back toward the original surface.
- A reconstructed macro height gradient augments sampled normal-map detail.
- Conservative cavity occlusion and heightfield directional self shadowing.
- Existing PIXL effects/hardware-depth reconstruction, controlled by CSPOM's
  depth setting.
- Debug views for eligibility, hit height, step demand, curvature, edge
  confidence and distance LOD.

## Quality levels

- **Low:** adaptive relief with curvature, clipping, occlusion and CSPOM self
  shadow disabled.
- **Medium:** adds guarded silhouette clipping and cavity occlusion.
- **High:** adds curvature correction, reconstructed macro normals and
  conservative self shadowing. This is the intended test baseline.
- **Ultra:** raises adaptive trace demand within the same 32-step safety cap.

All levels fade to the established PIXL POM result at range. Turning the module
off uses the original Material Layers implementation.

## Deliberately withheld

True outward silhouette expansion is not implemented. A pixel shader cannot
rasterize outside the original primitive, and the active Skyrim Lighting path
does not provide a safe universal hull/domain boundary with reliable material
qualification. Global tessellation or geometry amplification would risk grass,
actors, transparent geometry, shadow passes and draw-call cost. The current
prototype implements inward/edge-safe clipping and leaves outward coverage for
a future qualified-geometry path.

Exact separation of static rocks from architecture is also unavailable in all
Lighting permutations. The UI exposes their verified common class as **Static
architecture, rocks and cliffs** instead of presenting controls that cannot be
honoured reliably.

## Performance model

The module exits before extra work when disabled, the material is excluded,
the surface is beyond maximum distance, the sampled mip is too coarse, or the
visible projected displacement is below the existing texel threshold. Curvature
and self shadowing require High or Ultra. Terrain is off by default.

No GPU timing claim is made because this pass could not run an in-game GPU
capture. High should be compared against CSPOM Off using PIXL's existing frame
timing display on an RTX 3060 Ti class target.

## Live test checklist

1. Open **Advanced Tuner > Experimental > Materials > Curved Surface Mapping**.
2. Enable the module and select High.
3. Compare Off/High on an authored stone wall at frontal and grazing angles.
4. Repeat on a cliff or rock, wooden beam, tree trunk, rounded pillar and roof
   tiles.
5. Confirm glass/window panes, water, grass, actors, first-person arms and
   alpha-tested foliage remain unchanged.
6. Walk from close range beyond Maximum Distance and check for a smooth fade.
7. Compare DLSS/DLAA while strafing slowly; look for shimmer, crawling, UV
   islands, depth trails and disocclusion artifacts.
8. Enable each debug view if the effect is unclear. **Eligibility** should mark
   the active opaque family; **Height** confirms a displaced hit.
9. Test High and Ultra frame time in a dense city and rocky exterior.
10. Toggle Correct effects depth if a specific material shows screen-space
    depth instability, and record the mesh/material involved.

## Validation completed

- Release C++ build: pass.
- Eleven representative Lighting PS permutations under FXC `/WX`: pass.
- Repository pipeline/include audit: pass.
- Runtime Skyrim visual validation: required.

## Future visual improvements

1. Add qualified outward silhouette coverage for a tightly proven static-mesh subset.
2. Derive curvature from a more stable mesh-space signal where Skyrim exposes one.
3. Feed displaced position confidence into contact shading and local reflections.
4. Improve UV-island boundary awareness using authored material metadata.
5. Add material-specific depth response for bark, masonry, roof tile and cliff families.

## Future performance improvements

1. Add measured GPU timing around eligible Lighting permutations.
2. Cache stronger material eligibility when a stable draw/material identity is available.
3. Use projected triangle coverage to reject sub-pixel relief earlier.
4. Reduce shadow work using light-angle and receiver-confidence gates.
5. Evaluate a wave-friendly fixed quality ladder after live GPU captures.

## Future feature and research ideas

1. Investigate a conservative tessellation path for explicitly tagged static assets.
2. Add author metadata for height convention, scale and safe silhouette behavior.
3. Explore displaced-depth contribution to temporal reactive or disocclusion masks.
4. Test Ground Response snow layering over CSPOM on non-deformable architecture.
5. Research capture-assisted material validation scenes for repeatable comparison.
