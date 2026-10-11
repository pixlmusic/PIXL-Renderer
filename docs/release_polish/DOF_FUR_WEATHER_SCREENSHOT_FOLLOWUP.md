# DOF, Fur Classification, and Weather Follow-up

Date: 2026-10-11

## Findings

- The live `UserGraphics.json` has `Procedural Fur Shells: 1`.
- The live log reports all three FurShell geometry shader variants initialized, an eligible animal draw, a classified draw reaching D3D11, and Khajiit anatomical fallback classification.
- Two early `AUTO_FUR` pixel shader requests returned `E_ABORT` while the shader cache generation was being invalidated. Later `AUTO_FUR` permutations appear in shader timing records, so these entries indicate canceled compilation, not HLSL syntax errors. Some permutations took tens of seconds to compile; a cold-cache first encounter may therefore delay fur becoming visible.
- Fur classification previously accepted broad parent-path fur evidence before checking anatomical exclusions. The exclusion order is fixed so eyes, teeth, claws, horns, antlers, tusks, skulls, bones, hooves, and beaks cannot be marked as fur solely because a parent directory contains `fur`.
- DOF sky protection uses a continuous absolute-elevation fade from 12,000 to 40,000 Skyrim units and attenuates sky CoC by at most 55%. Clouds keep their separate cloud-coverage protection path.
- Weather synchronization reads `Sky::currentWeather`, `lastWeather`, and `currentWeatherPct`, with a cached outgoing weather for interrupted transitions. Volumetric clouds blend authored cloud alpha from both records and fall back to the weather classifier when a record has no usable cloud layers. Coverage, density, type, precipitation, storm, fog, sun visibility, cloud base, and thickness are smoothed independently. This confirms the code path is hooked; the available log does not record an observed in-game weather transition, so end-to-end visual confirmation remains for live testing.

## Changes and validation

- Reordered high-confidence anatomical exclusions in `PhysicalMaterialRegistry.cpp` ahead of positive fur path evidence; preserved the later clothing/equipment-specific logic so authored fur armor remains eligible.
- Kept sky DOF suppression gradual and partial to avoid a hard blurred-terrain/protected-sky boundary.
- `cmake --build --preset PIXL-12C` completed and the integrated renderer audit passed (45 shipping modules).
- Compiled `DOFCoCCS.hlsl` with FXC (`cs_5_0`).
- Deployed the DLL, Lighting shader, FurShell geometry shader, and DOF compute shader to the Steam Skyrim `Data` tree after backup at `build/deploy-backups/dof-fur-weather-20261011-173339`; SHA-256 hashes match source for each deployed file.

## Remaining validation

- Confirm the new exclusion with the same mammoth mesh and confirm nearby fur-bearing surfaces still receive shells.
- Compare the Khajiit actor with shells toggled both ways after shader compilation has settled; the existing evidence confirms classification and draw-stage activation, but not the final perceived strand quality.
- Capture one live weather transition from clear to overcast/rain and confirm the cloud base and cover ease without a discontinuity.
- Review the DOF treatment on a distant skyline with foreground vegetation and on a close-focus shot; shader compilation alone cannot validate image-space silhouette quality.

## 2026-10-11 gameplay follow-up

- The user's active gameplay DOF values had `dofFStop = 3.4`, `dofFarBlurDistance = 26000`, `dofFarBlurIntensity = 0.90`, `dofStrength = 1.0`, and `dofMaxBokehPixels = 6`. The additional distance blur began within the visible mountain/LOD range, independently of the sky elevation fade.
- Tuned the active user config to f/5.6, 38,000-unit far-blur start, 0.58 far-blur intensity, 0.80 overall strength, and 4.5-pixel maximum CoC. The original config is backed up beside the preceding live deployment.
- Updated the GAMEPLAY quick preset to match the restrained distant-focus direction and expanded its distance-start slider to the supported 50,000-unit range.
- Fur geometry was being emitted outside the base surface while Skyrim can use an equality depth test against the unextruded depth prepass. Added cached depth-state variants for fur draws only: `LESS_EQUAL` with depth writes, retaining the original stencil configuration and restoring the original D3D state after each draw. Variants are keyed by D3D device and source depth state.
- Built successfully and passed the renderer audit (45 shipping modules). Deployed the DLL after backup at `build/deploy-backups/fur-depth-dof-20261011-180708`.
- Live verification still requires a new game session. Confirm the `[FurShells] Shell draws use a cached LESS_EQUAL depth variant...` log appears, then compare the same Khajiit body/tail with the toggle on and off. The updated DOF config is read at startup; verify the mountain horizon and a close-focus subject before further tuning.

## Conservative fur follow-up

- The Khajiit race fallback has been removed. It could classify broad skinned anatomy without local fur evidence and allow shells to cover skin and clothing. Automatic fallback now targets dog, wolf, fox, bear, rabbit, and hare coat/body material names; apparel and hard-surface exclusions remain ahead of positive matches. Explicitly named fur/pelt surfaces remain eligible.
- Live validation should focus on those animal coats and static pelt meshes, with Khajiit anatomy and ordinary clothing confirming that shells remain absent.

## 5 Future Visual Improvements

1. Add a modest directionally biased strand field for wind-swept coats.
2. Derive shell length from material confidence and camera distance.
3. Add a soft coverage transition at shell tips to reduce a clipped-card look.
4. Couple cloud density and cloud base to authored precipitation transitions more explicitly.
5. Evaluate DOF sky weighting against cloud depth and actual cloud opacity during sunrise and mountain shots.

## 5 Future Performance Improvements

1. Measure cold-cache `AUTO_FUR` permutation compile time and prioritize visible fur variants.
2. Reduce redundant fur variants where vertex-only flags do not alter the pixel interface.
3. Evaluate a cheaper strand coverage hash for low-end quality settings.
4. Track cloud weather-map dirty regions to reduce unnecessary updates.
5. Profile DOF silhouette reconstruction at native and upscaled output sizes.

## 5 Future Feature / Research Ideas

1. Expose a per-material shell length multiplier in Material Forge.
2. Add optional authored material tags for fur and hard anatomical parts.
3. Add a runtime debug view for fur classification confidence and selected geometry stage.
4. Provide weather-profile diagnostics showing source records and transition percentage.
5. Explore cloud-to-volumetric-fog light integration after the current weather transition baseline is verified in game.
