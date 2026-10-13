# Future Rendering Roadmap — 1.0.8 Prerelease

Release polish focused on safety, compatibility, current behavior, and package validation. Substantial architectural projects remain future work until they have measured visual and performance evidence.

1. Validate Atmosphere, Volumetric Clouds, and Volumetric Fog interactions in fixed scenes at day/night/storm states.
2. Improve cloud edge reconstruction and fog/cloud compositing only after temporal and GPU captures.
3. Expand authored and vanilla material classification diagnostics across actors, vegetation, terrain, statics, water, and thin surfaces.
4. Profile HybridGI, SkyBounce, World Probes, and cache behavior before any structural replacement.
5. Improve water multi-scale motion and vegetation hierarchical wind through captured baselines.
6. Keep snow deformation stable and material-aware; avoid redesign without a reproducible defect.
7. Add sustained gameplay tests for cell/worldspace transitions, long sessions, and shader-cache churn.
8. Extend Photo/Director integration tests to real camera ownership and optional camera plugins.
9. Track public module pages and advanced controls against a machine-readable settings contract.
10. Pursue only changes with reproducible image, stability, and performance validation.
