# Module Registry and Compatibility Review — 1.0.8 Prerelease

## Registry and package coverage

The registered PIXL pipeline descriptors were reconciled with the engine module registry, generated shader catalog, build audit, and staged package. The release build reports 45 shipping modules and one source-only retired module. The full file-level source/build inventory is in `FILE_REVIEW_MATRIX.md`; each descriptor, C++ source/header, shader source/include, and packaging input is individually listed there.

| Module | ID | Descriptor version |
|---|---|---:|
| Actor Surface Effects | `ActorSurfaceEffects` | 1-0-1 |
| Ambient Probe | `AmbientProbe` | 1-3-0 |
| Atmosphere | `Atmosphere` | 1-3-1 |
| Camera Suite | `CameraSuite` | 2-0-3 |
| Cloth Dynamics | `ClothDynamics` | 0-2-0 |
| Contact Shadows | `ContactShadows` | 2-1-5 |
| Contained Liquids | `ContainedLiquids` | 0-5-3 |
| Curved Surface Mapping | `CurvedSurfaceMapping` | 0-1-0 |
| Distance Blend | `DistanceBlend` | 1-0-0 |
| Distant Life | `DistantLife` | 0-6-0 |
| Foliage Dynamics | `FoliageDynamics` | 2-3-1 |
| Foliage Optimizer | `FoliageOptimizer` | 1-0-0 |
| Ground Response | `GroundResponse` | 3-3-11 |
| Horizon Blend | `HorizonBlend` | 1-0-1 |
| Hybrid GI | `HybridGI` | 4-4-2 |
| Image Reconstruction | `ImageReconstruction` | 1-4-2 |
| Interior Daylight | `InteriorDaylight` | 1-1-0 |
| Light Volumes | `LightVolumes` | 1-1-0 |
| Linear Light Core | `LinearLightCore` | 1-2-0 |
| Material Forge | `MaterialForge` | 1-1-2 |
| Material Layers | `MaterialLayers` | 1-3-9 |
| Microclimates | `Microclimates` | 0-1-0 |
| Natural Lighting | `NaturalLighting` | 1-3-0 |
| Pixel Capture | `PixelCapture` | 1-1-0 |
| Pulse Profiler | `PulseProfiler` | 1-2-0 |
| Radiant Grid | `RadiantGrid` | 3-2-1 |
| Rain Response | `RainResponse` | 3-2-0 |
| Reactive FX | `ReactiveFX` | 0-1-2 |
| Skin Optics | `SkinOptics` | 0-3-3 |
| Sky Bounce | `SkyBounce` | 1-5-2 |
| Sky Continuity | `SkyContinuity` | 1-3-0 |
| Sky Veil | `SkyVeil` | 1-5-0 |
| Strand Shading | `StrandShading` | 1-1-2 |
| Terrain Detail | `TerrainDetail` | 1-1-0 |
| Terrain Field | `TerrainField` | 1-0-1 |
| Terrain Occlusion | `TerrainOcclusion` | 1-2-0 |
| Terrain Seam | `TerrainSeam` | 1-2-0 |
| Thin Surface | `ThinSurface` | 1-0-0 |
| Tissue Diffusion | `TissueDiffusion` | 3-3-0 |
| Volume Occlusion | `VolumeOcclusion` | 2-1-0 |
| Volumetric Clouds | `VolumetricClouds` | 0-1-1 |
| Water Optics | `WaterOptics` | 1-3-1 |
| Waterbody | `Waterbody` | 0-2-1 |
| Window Life | `WindowLife` | 0-7-22 |
| World Probes | `WorldProbes` | 2-4-3 |

`Hair Reconstruction` is the retired source-only pipeline and is excluded from shipping. Module descriptors and enabled cache versions passed the release-candidate stage validation. The 3,681-stage installed library remained untouched during deployment.

## Compatibility findings

- Existing compatibility policy keeps vanilla and ordinary authored mesh/texture/weather mods in place; PIXL layers rendering behavior on runtime material, shader, weather, and scene data rather than replacing those assets.
- Cache identity guards ensure the package's shader ABI/revision and enabled module versions match the compiled cache. The FOMOD test validates its three-page install flow, regular defaults, cache, and absence of unrelated SurfaceTides payload.
- Settings continue through default → user → override → user-authored override precedence. Atomic writes protect those authored settings from partial write failure.
- Compatibility is not universal. An authored shader replacer can bypass PIXL hooks, and authored materials may omit the metadata a feature uses. No offline audit can certify every mesh/body/weather/plugin combination.
- No broad material classifier changes were made during this release polish because doing so without real authored mod fixtures would be speculative and could regress vanilla paths.

## Validation performed

`PIXL-Audit` confirmed module registration/package structure; CTest passed all 28 tests. Selected exact-source tests passed for Foliage settings/ABI, Landscape settings and optional plugin detection, Thin Surface settings, RCAS fallback/dispatch behavior, icon loading, fonts, shader define/dependency/include handling, and reconstruction controls. Staged shader validation passed the recorded FXC matrices in `RELEASE_SCORECARD.md`.

Manual live-load-order checks are listed in `UX_AND_COMPATIBILITY_AUDIT.md`. Passing the registry/cache checks proves package consistency, not that every module has been visually tested with every authored and vanilla asset.

## 5 Future Visual Improvements

1. Create a vanilla/authored reference scene per visual domain: terrain, vegetation, static, skinned, water, and atmosphere.
2. Add a debug indicator for the material/classification branch selected for a draw.
3. Expand authored texture metadata fallbacks for missing or unusual channel layouts.
4. Record image comparisons at fixed weather, sun, exposure, and camera values.
5. Track edge cases such as LOD transitions and mesh swaps per module.

## 5 Future Performance Improvements

1. Profile module cost by feature and material class with a repeatable route.
2. Audit shader permutation counts against actually requested variants.
3. Measure memory and update cadence for probe/volume modules over long sessions.
4. Validate that optional modules do not retain resources while disabled.
5. Use cache reports to identify unusually large or redundant module payloads.

## 5 Future Feature / Research Ideas

1. Optional module compatibility profiles keyed by authored conventions, without hard dependencies.
2. A module health view for missing resources, unsupported data, and graceful fallback state.
3. Compatibility test fixtures contributed by mod authors.
4. Machine-readable settings and cache contract coverage for every registered module.
5. Automatic diagnostic summaries for user-reported compatibility failures.
