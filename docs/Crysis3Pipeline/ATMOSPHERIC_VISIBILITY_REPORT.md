# PIXL Atmospheric Visibility Audit

## Outcome

PIXL already implements the useful architectural result of Crysis 3's shadowed
volumetric fog, but with a more suitable modern DX11 design: a camera-aligned 3D
froxel volume, conservative scene-depth coverage, cascaded directional shadows,
terrain and cloud visibility, clustered local-light scattering, temporal
reprojection, and depth-aware final reconstruction.

This pass therefore did **not** add a second fog system or reproduce Crysis 3's
old half-resolution interleaved sampling. It made a bounded release-safe fix to
the existing temporal validity contract. Volumetric history is now rejected on:

- vertical FOV/projection changes;
- exterior worldspace identity changes, even if the new camera happens to be
  less than the existing 4096-unit camera-cut threshold from the old origin;
- availability changes in the directional-shadow, conservative-depth, ambient
  probe, SkyBounce, or clustered local-light inputs.

There are no new passes, resources, settings, shader registers, feature defines,
or CPU/HLSL layout changes.

## Active Pipeline

```text
Skyrim scene depth
      |
      +--> conservative XY depth envelope (max/min view depth)
      |
PIXL height fog + optional rain aerosol
      |
      +--> VBuffer material volume (scattering + extinction)
      |
      +--> directional sun/moon scattering
      |       +--> Skyrim two-cascade directional shadow map (5-tap PCF)
      |       +--> Terrain Occlusion horizon visibility
      |       +--> Sky Veil cloud optical-depth visibility
      |
      +--> Ambient Probe / SkyBounce scattering
      +--> clustered local-light scattering
      |
      +--> temporal light-scattering resolve
      |       +--> unjittered previous camera reprojection
      |       +--> previous conservative-depth fixup
      |       +--> extinction rejection
      |       +--> depth-envelope rejection
      |       +--> bounded radiance clamp
      |       +--> finite-value sanitation
      |
      +--> front-to-back 3D integration
      |
      +--> depth-aware 2x2 final reconstruction at material shading
```

## GPU Resources

| Resource | Format | Role |
| --- | --- | --- |
| `VBufferA` | `R16G16B16A16_FLOAT` 3D | Per-froxel scattering RGB and extinction |
| `ConservativeDepth` | `R32G32_FLOAT` 2D | Current maximum/minimum scene depth per XY froxel |
| `ConservativeDepthHistory` | `R32G32_FLOAT` 2D | Previous depth envelope for temporal validation |
| `LightScattering` | `R16G16B16A16_FLOAT` 3D | Current resolved scattering/extinction |
| `LightScatteringHistory` | `R16G16B16A16_FLOAT` 3D | Previous resolved scattering/extinction |
| `IntegratedLightScattering` | `R16G16B16A16_FLOAT` 3D | Front-to-back radiance and transmittance |

The XY grid can be tied to display resolution so DLSS/FSR does not silently
reduce apparent volumetric quality. Allocation is capped at 16 million voxels;
resource recreation automatically invalidates all temporal state.

## Visibility Inputs

### Directional shadow map

`Atmosphere::CaptureDirectionalShadowMap()` captures Skyrim's active Utility
shadow-map SRV from pixel slot `t4`. `Deferred::DirectionalShadowLightData`
provides the matching two cascade matrices and split distances through `t98`.
The scattering compute shader performs a bounded five-tap comparison-PCF filter,
cross-fades the two cascades, and fades visibility at the shadow distance.

The directional term is additionally gated by PIXL's authoritative
`HasDirectionalShadows`, `HideSky`, and map/interior state. This prevents fake
unshadowed shafts in enclosed scenes while preserving supported sky-lit
interiors.

### Terrain visibility

Terrain Occlusion supplies a worldspace height/horizon texture at `t60`.
Atmosphere uses the current renderer-derived world position and the same
five-sample softened terrain visibility as other PIXL lighting paths. It is
disabled in interiors, map views, HideSky scenes, or when directional shadows
are unavailable.

### Cloud visibility

Sky Veil supplies cloud optical depth at `t25`. The shadow direction is projected
through the active directional-light vector and converted to transmittance with
a bounded Beer-Lambert response. This is coupled to PIXL's actual Sky Veil state,
not an unrelated scrolling shadow texture.

### Ambient and local lighting

Ambient Probe, SkyBounce, and clustered PIXL local lights contribute to the same
physical scattering volume. Their resource availability is now part of temporal
history validity, preventing a stale volume from surviving provider
initialization, shutdown, or a scene-dependent availability transition.

## Temporal History Audit

### Existing valid protections retained

- history exists and the previous prepass was exactly one renderer frame ago;
- temporal rendering is active;
- camera origin delta is no larger than 4096 Skyrim units;
- dynamic-resolution scale is unchanged;
- grid dimensions are unchanged (resource recreation performs a hard reset);
- interior/exterior, HideSky, and map-menu classification is unchanged;
- current and previous conservative depth remain compatible;
- fog extinction remains compatible;
- history radiance is finite, non-negative, and inside a current-frame envelope.

### Release-safe protections added

| Condition | Previous behavior | Current behavior |
| --- | --- | --- |
| FOV/photo-lens change | Reprojected a volume generated for a different projection | One cold frame, then history resumes with the new projection |
| Exterior worldspace change | Relied only on frame continuity and camera distance | Rejects history when the active exterior worldspace pointer changes |
| Lighting input availability change | Mixed old provider state at up to the configured history weight | Rejects history for that transition frame |

Normal camera motion, ordinary exterior cell traversal, gradual weather changes,
and ordinary shadow animation intentionally do **not** force resets. Reprojection,
extinction/depth rejection, and radiance clamping handle those cases without
destroying temporal convergence every frame.

## Files Reviewed

| File | Role | Result |
| --- | --- | --- |
| `engine/Modules/Atmosphere.h` | Settings, GPU constant layout, resources, temporal state | Reviewed; temporal state extended without GPU ABI change |
| `engine/Modules/Atmosphere.cpp` | Resource lifecycle, input binding, dispatch, history validity | Reviewed and modified |
| `pipeline/Atmosphere/Module.ini` | Runtime module identity/version | Reviewed; intentionally unchanged because no shader source changed |
| `VolumetricFogCSCommon.hlsli` | Froxel addressing and CPU/HLSL cbuffer contract | Reviewed; unchanged |
| `VolumetricFogCommon.hlsli` | Stable depth distribution, extinction, phase math | Reviewed; unchanged |
| `VolumetricFogMaterialCS.hlsl` | Height-fog/rain aerosol material volume | Reviewed; unchanged |
| `VolumetricFogConservativeDepthCS.hlsl` | Dynamic-resolution-aware depth envelope | Reviewed; unchanged |
| `VolumetricFogLightScatteringCS.hlsl` | Shadows, lighting, reprojection, rejection | Reviewed; unchanged |
| `VolumetricFogIntegrationCS.hlsl` | Front-to-back radiative integration | Reviewed; unchanged |
| `Atmosphere.hlsli` | Analytical/volumetric composition and bilateral sampling | Reviewed; unchanged |
| `SkyVeil/SkyVeil.hlsli` | Cloud optical-depth shadow input | Reviewed; unchanged |
| `TerrainOcclusion/TerrainOcclusion.hlsli` | Terrain horizon-shadow input | Reviewed; unchanged |
| `engine/Deferred.cpp/.h` | Directional shadow matrices/splits and prepass ordering | Reviewed; unchanged |
| `engine/State.cpp` | Directional shadow SRV capture hook | Reviewed; unchanged |

Canonical Atmosphere shader files were byte-identical to the active loose files
under `Data/Shaders/Atmosphere` before this patch.

## Change Classification

**A - Release Safe correctness/stability fix.** The change can only discard
invalid temporal history for a single transition frame. Steady-state rendering,
resource dimensions, shader output, and configured image character are
unchanged.

## Validation

- Release build: **PASS**
  - `build/PIXL-12C/Release/PIXLRenderer.dll`
  - SHA-256 at validation: `8DD429F0B3CCE6F6D32AF947E454D33E07AA7AEED58D90B73F8E03699C69C57F`
- Integrated source/build audit: **PASS**, 38 modules
- Strict FXC (`cs_5_0`, `/Ges /WX /O3`): **PASS**, 12 variants
  - material base and rain;
  - all eight combinations of Radiant Grid, Terrain Occlusion, and Sky Veil;
  - conservative depth;
  - integration.
- CPU/GPU ABI: **UNCHANGED**
- Live deployment: **NOT PERFORMED**
- Runtime visual validation: **PENDING**

## Runtime Acceptance Tests

1. Orbit through a strong sun shaft in an exterior while watching for stair-step
   or block-shaped persistence.
2. Change Photo Mode FOV rapidly, then exit Photo Mode. Fog should settle from
   current data immediately rather than stretching old shafts.
3. Fast travel between two exterior worldspaces, including locations whose local
   coordinates are similar. No old fog/cloud/terrain shadow should flash.
4. Transition exterior -> enclosed interior -> sky-lit interior -> exterior.
5. Enter and exit the world map with dedicated map atmosphere enabled.
6. Compare TAA, FSR 3, and DLSS while panning across a high-contrast cascade edge.
7. Test noon, sunset, and moonlight with Terrain Occlusion and Sky Veil toggled
   independently. No stale shadow should persist after a provider transition.
8. Test a rainy fog scene to verify rain aerosol remains lit and temporally stable.
9. Change internal resolution/upscaler quality at runtime. The first new frame may
   be less converged, but old-resolution fog must not bleed into it.

## Remaining Risks / Future Work

- Runtime performance and visual behavior were not measured in-game during this
  source pass; no timing claim is made.
- Full physically volumetric cloud self-shadowing would require a true cloud
  density volume and is an architectural future feature, not a release-safe patch.
- Shape-aware local-light volumetric shadowing would require additional light
  representation and visibility data. It should be developed with the proposed
  Extended Emitters work rather than added ad hoc to Atmosphere.
- A shared renderer-wide camera-discontinuity generation would eventually remove
  repeated camera/FOV checks across temporal modules. Introducing that contract
  immediately before release would be broader than this correction.
