# PIXL Renderer quality-profile contract
The main Quality page is the normal user-facing control surface. Each group has
four workload tiers (`Low`, `Medium`, `High`, `Ultra`); `Ultra` preserves the
current live-tested PIXL visual baseline. Artistic intensity, colour and style
controls stay user-owned unless they are explicitly listed below.

| Group | Runtime fields controlled by the tier | Intended result |
| --- | --- | --- |
| Lighting | Radiance Weave slice/step density, world-cache samples/trace cadence/second bounce, reflection steps, temporal history, blur/firefly limits, Contact Shadows samples, Material Forge local contact-light count, native Light Volumes tier | Scales GI, reflection, volumetric-light and contact-shadow workload while retaining authored GI colour/strength/radius. |
| Materials | Specular AA strength, GGX multiscatter strength, complex material/POM/height blend/shadow gates, object and terrain POM step counts, refinement and detail reconstruction | Scales surface depth and BRDF stability. Ultra matches the shipped POM/material path. |
| Atmosphere | Volumetric grid XY/Z resolution and history-miss samples | Scales fog froxel cost and temporal quality without changing the user's cloud appearance or Light Volumes tier. |
| Water | Enhanced SSR/caustic gates, SSR trace distance/step budget, edge fade and caustic dispersion | Scales actual reflection trace work and caustic detail. Ultra retains the original 48-step enhanced SSR trace. |
| Terrain & Vegetation | Raised snow/mud render distance, fade envelope, near/far tessellation factors and terrain LOD tiling repair | Scales the expensive geometric-ground workload while preserving the user's vegetation lighting and wind character. |
| Characters | Skin detail/SSS gates, Burley sample count, strand mode and hair self-shadowing | Scales dialogue-character skin diffusion/detail and strand lighting. |
| Camera | Camera Suite sampling-quality tier | Scales camera sampling only; exposure, grade and enabled effects remain unchanged. |

Applying a tier updates the live C++ settings, required shader/history state,
GPU feature data and persisted user JSON immediately. The Quality page compares
the complete live runtime contract against the selected tier every frame. If an
Advanced control diverges, the affected group and coordinated profile report
`CUSTOM`; reapplying a group tier or `MATCH PROFILE` restores the full contract.
