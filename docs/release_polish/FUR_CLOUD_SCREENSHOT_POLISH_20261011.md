# Fur Shell and Volumetric Cloud Screenshot Polish

Date: 2026-10-11

## Findings

- The fur classifier combined the full diffuse-texture path with the complete scene/actor hierarchy. A parent directory or actor name could therefore opt an unrelated garment draw into shell rendering.
- The shell stack copied the base material with sparse, high-frequency coverage. This left broad substrate patches and read as a noisy duplicate mesh rather than a coat of short fibers.
- The cloud density field faded to zero at the world weather-map border. The map's half extent (524,288 units) was nearly equal to the default view distance (531,629 units), so the border fade projected into the sky as curved transparency traces.
- Most cloud controls had no help text explaining the visible effect or cost.

## Changes

- Fur classification now checks the local diffuse filename and draw-submesh name, not parent actor or texture-directory names. Hard surfaces are rejected first; generic apparel is rejected before species fallback. Explicit local fur/pelt material names and explicit Material Forge authoring remain supported.
- Khajiit anatomical fallback now excludes a wider set of equipment and apparel names before enabling shells.
- Shell coverage now forms denser inner strands and tapers toward the outer layers. A stable object-space seed produces mild color variation and deterministic vertex length variation to soften the uniform extrusion silhouette.
- Removed the weather-map-edge density fade. Cloud sampling continues through the map boundary using clamped edge values over the small distance outside the map.
- Added effect and performance tooltips for normal cloud controls and advanced lighting/temporal controls.
- Bumped the shared shader revision because `Lighting.hlsl` changes are included in global cached shader permutations. Incremented the cloud module version so only that module's pipeline stages are separately invalidated.

## Limits

- Classification still operates per material draw. If cloth and fur share one mesh, material, and texture region, PIXL has no authored per-pixel boundary to distinguish them. A companion fur mask or a separated material/submesh is needed for precise mixed-material clothing; automatic classification intentionally avoids guessing from a generic `fur` parent folder.
- Procedural shells change silhouette and surface breakup, but they do not synthesize authored strand direction from a fur texture. Live evaluation is needed to tune length and clump density across animals, hands, and garments.
- The sky traces are attributed to the map-border fade from the coordinate/range match. A live screenshot after this change is still required to confirm they are gone.

## Validation

- `cmake --preset PIXL-12C` regenerated module metadata, including VolumetricClouds `0.1.1`.
- `cmake --build --preset PIXL-12C` passed; the integrated renderer audit reported 45 shipping modules.
- FXC Shader Model 5.0 compiled all four cloud compute entry points, three FurShell geometry input variants, and the skinned AUTO_FUR Lighting pixel permutation.
- `git diff --check` found no whitespace errors. Its output only noted the repository's existing LF-to-CRLF normalization warnings.
- Deployed the DLL, Lighting shader, FurShell geometry shader, and volumetric cloud compute shader to the Steam Skyrim `Data` tree after backup at `build/deploy-backups/fur-cloud-polish-20261011-190202`; deployed SHA-256 hashes match their sources.
- `UserGraphics.json` was hash-checked before and after deployment and remained unchanged.
- The shader revision update will make the renderer rebuild its persistent shared shader cache on the next launch. This is intentional so cached Lighting permutations cannot mask the new AUTO_FUR response.
- Live-game image validation remains outstanding; the game was closed during deployment.
