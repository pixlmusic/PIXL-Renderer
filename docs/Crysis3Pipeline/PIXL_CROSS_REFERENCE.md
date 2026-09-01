# PIXL Cross-Reference

| Crysis-inspired target | Current PIXL source of truth | Current state | Highest-value gap | Release disposition |
| --- | --- | --- | --- | --- |
| Directional visibility / bent normals | `HybridGI`, `Deferred`, `DeferredCompositeCS.hlsl` | Implemented end-to-end: horizon masks, oct bent normal, visibility/confidence, reprojection, temporal resolve, depth-aware reconstruction, ambient/probe/reflection consumers and debug views. | Correct encoded-normal reprojection, confidence, temporal geometry response and later normal-aware reconstruction. | Correctness work implemented in 4.3.0; visual validation required. |
| Thin auxiliary scene buffer | `Deferred::SetupResources`, Lighting MRT writers | Implemented as albedo, specular, reflectance, normal/gloss and mask targets, plus post-geometry visibility state. | Formal semantic ownership and spare-channel inventory. | Document first; do not add MRT bandwidth. |
| Area / extended emitters | `RadiantGrid::LightData`, Skyrim light hooks | Point/sphere-like clustered lights; particle fire lights; room/portal/shadow flags. | Shape/orientation/dimensions and area-light BRDF. | Effective-radius prototype first; full LTC is experimental. |
| Particle response | `Effect.hlsl`, `Particle.hlsl`, `RadiantGrid` | Smoke/snow/rain/fire can receive sun, shadows, probes, SkyBounce and clustered local lights; incandescent billboards can emit representative lights. | Stable aggregation/deduplication and shadow policy. | Release-safe after CPU/runtime profiling. |
| Dynamic caustics | `Water.hlsl`, `WaterCaustics.hlsli`, `WaterOptics` | Physical receiver with depth, sun, extinction, dispersion and layered authored caustic texture. | Source focusing still comes from a texture rather than live waves/ripples. | Low-resolution live focus source is the recommended prototype. |
| Shadowed volumetrics | `Atmosphere` froxel compute pipeline | 3D froxel material/scattering/integration, directional cascades, TerrainOcclusion, SkyVeil, local RadiantGrid lights, history rejection and depth-aware reconstruction. | Validate camera cuts, capture state and cloud-shadow coherence. | Preserve architecture; tune/repair only. |
| Foliage force simulation | `FoliageDynamics`, `FoliageWind.hlsli` | Stable world-space layered gust/trunk/branch/leaf response, not constraint physics. | Shared wind sampling contract; later patch/constraint simulation. | Shared deterministic field first; compute constraints post-release. |
| Localized IBL + SSR | `HybridGI` reflections, `WorldProbes`, `AmbientProbe`, water SSR | Screen hit/history to world-cache/probe/sky fallback exists. | Multiple bounded parallax-correct local probes. | Prototype separately; do not disturb current fallback. |
| SSS / transmission | `TissueDiffusion`, `SkinOptics`, `ThinSurface`, foliage/snow material helpers | Specialized high-quality paths already exist. | Common profile vocabulary and energy policy. | Unify incrementally, not as one global shader rewrite. |
| Material normalization | `PhysicalMaterial.hlsli`, `MaterialForge`, registry | Legacy shininess/F0/roughness/metal inference and multiscatter are implemented conservatively. | Semantic class confidence and diagnostics. | Add diagnostics before retuning values. |
| Advanced glass | `WindowLife`, Lighting integration | Fresnel, probe reflection, transmission, refraction, grime, weather response and sun glint exist. | Oblique/mip stability and thick-glass ordering. | Stability is release-safe; thick glass is future work. |
| Optical glare | `CameraSuite` bloom pyramid | Scene-linear exposure-relative soft knee, firefly rejection, progressive pyramid and controlled upsample already replace poor broad bloom. | Separate diffraction/glare/ghosts only if physically bounded. | Preserve restrained bloom defaults. |
| Material decals / contamination | `ActorSurfaceEffects`, `MaterialForge` decal hooks | Bounded actor-local snow/mud/wetness lifecycle with future effect-type space. | Unified world/local multi-property persistent state. | Extend existing event API; defer a world atlas. |
| Light containment | RadiantGrid room/portal flags, shadow masks | Partial containment exists. | Automatic clipping-volume inference. | High-risk future research. |

## Directional Visibility ownership

```text
Skyrim depth + normals + motion
             |
             v
HybridGI average-depth/normal hierarchy
             |
             v
horizon masks -----> AO / contact depth
             |
             +-----> bent direction + hemisphere visibility + confidence
                              |
                 motion/depth/normal reprojection
                              |
                   temporal geometry response
                              |
                 depth-aware full-res reconstruction
                              |
                              v
                   DeferredComposite t17
                     |       |       |
                     |       |       +--> rough probe fallback visibility
                     |       +----------> SkyBounce / AmbientProbe direction
                     +------------------> ambient direction
```

Direct sun/moon and local light visibility remain owned by Contact Shadows, Skyrim shadow masks, RadiantGrid local shadow data, VolumeOcclusion and TerrainOcclusion. Multiplying those paths by the broad bent-visibility scalar would double-shadow and create halos.

## Important limitations

- HybridGI's five-mip depth hierarchy is linear/average depth, not conservative min/max Hi-Z.
- The full-resolution RGBA16F bent-history ping-pong costs about 133 MiB at 4K. Format changes require measured visual validation and are not part of the correctness patch.
- The current ambient replacement preserves the original stored ambient luminance for subtraction safety, so bent direction affects chroma more than luminance. A physically stronger replacement is a future A/B-tested baseline change.
- Forward particles, vegetation and water cannot consume same-frame post-deferred bent visibility without reordering or using prior-frame state.
