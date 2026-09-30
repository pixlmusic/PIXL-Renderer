# PIXL Renderer quality-profile contract
The main Quality page is the normal user-facing control surface. Each group has
four workload tiers (`Low`, `Medium`, `High`, `Cinematic`). `High` preserves the
former Cinematic/live-tested PIXL presentation. The new `Cinematic` tier is an
intentional high-end/capture profile with up to three times the dominant ray,
bokeh and reconstruction work. Artistic intensity, colour and style controls
stay user-owned unless they are explicitly listed below.

| Group | Runtime fields controlled by the tier | Intended result |
| --- | --- | --- |
| Lighting | Radiance Weave slice/step density, world-cache samples/trace cadence/second bounce, reflection steps, temporal history, blur/firefly limits, Contact Shadows samples, Material Forge local contact-light count, native Light Volumes tier | Scales GI, reflection, volumetric-light and contact-shadow workload while retaining authored GI colour/strength/radius. |
| Materials | Specular AA strength, GGX multiscatter strength, complex material/POM/height blend/shadow gates, object and terrain POM step counts, refinement and detail reconstruction | Scales surface depth and BRDF stability. High matches the shipped POM/material path; Cinematic reaches the bounded SM5 loop ceilings. |
| Atmosphere | Volumetric grid XY/Z resolution and history-miss samples | Scales fog froxel cost and temporal quality without changing the user's cloud appearance or Light Volumes tier. |
| Water | Enhanced SSR/caustic gates, explicit SSR trace quality, distance and edge confidence | Low/Medium use 16/28 coarse steps, High preserves 48, Cinematic uses 144. Caustic dispersion and optical appearance remain user-owned. |
| Terrain & Vegetation | Snow/mud tessellation and retained history; grass projected-size culling, density floor, simple shading, mesh LOD, complexity bias and collision range | Low remains visibly PIXL but aggressively removes expensive distant grass. High preserves the release vegetation/ground presentation. Cinematic retains more distant instances and raises bounded terrain geometry. |
| Characters | Skin detail/SSS gates, Burley samples, strand mode, hair reconstruction tier, actor-surface capacity and range | Scales dialogue-character skin diffusion/detail, strand lighting and persistent actor interactions. |
| Camera | Camera Suite sampling-quality tier | Scales histogram density, local adaptation, physical-DOF gather and motion blur only; exposure, grade, lens parameters and enabled effects remain unchanged. |

Applying a tier updates the live C++ settings, required shader/history state,
GPU feature data and persisted user JSON immediately. The Quality page compares
the complete live runtime contract against the selected tier every frame. If an
Advanced control diverges, the affected group and coordinated profile report
`CUSTOM`; the per-group `REAPPLY` action restores only that group, while
`RESTORE PROFILE` restores all seven. Advanced artistic controls that are not
owned by a workload contract remain untouched. Config version 2 migrates the
old tier-3 Cinematic selection to High so an update never silently triples load.
