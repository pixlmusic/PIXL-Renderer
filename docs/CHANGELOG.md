# PIXL Renderer 1.0.6 Changelog

Changes since 1.0.5:

- Moved Foliage Optimizer into Experimental and disabled it by default for the release build while its Skyrim SE hook path is validated.
- Smoothed Radiance Weave voxel cascade blending to reduce visible world-cache transition edges.
- Tuned default water caustics to a restrained 1.2 intensity.
- Improved PIXL water receiver projection with moving multi-scale caustic focus and extended bridge/overhang coverage.

Changes since 1.0.4:

- Upgraded Auto-DOF with physical lens controls, unified autofocus and adaptive near/far bokeh.
- Restored reliable close-object autofocus while retaining first-person hand and weapon protection.
- Improved DOF foreground coverage, sky handling, temporal stability and DLSS/FSR/TAA integration.
- Added profile-aware Contained Liquids fill, classification, slosh, bubbles, refraction and absorption.
- Added PIXL-native Foliage Optimizer infrastructure with conservative Skyrim 1.5.97 fallback behavior.
- Fixed ordinary grass normals so Hybrid GI/SSGI reaches non-Complex Grass assets.
- Stabilized Hybrid GI's world-space cache with deterministic voxel election, fixed-rate aging and frame-rate-independent temporal response.
- Added conservative torch, candle, brazier and glow-emitter injection so transparent fire sources contribute stable indirect lighting.
- Improved particle-emitter colour, flame variation and bounded contact shadows while correcting interior directional-shadow continuity.
- Expanded fur/material recognition and restrained interior environment-specular leakage for more consistent character and room lighting.
- Expanded Low, Medium, High and Cinematic into meaningful renderer-wide workload tiers.
- Promoted the live-tested DOF, vegetation, skin and Ground Response values to release defaults.
- Stabilized Ground Response by restoring direct terrain reconstruction while retaining newer simulation features.
- Improved Ground Response persistence, material response, weather accumulation and movement-resistance cleanup.
- Improved terrain, water, atmosphere, distant-light, hair, skin and directional-SSS integration.
- Improved Image Reconstruction reactive/disocclusion handling for camera effects, foliage and deformation.
- Expanded Director, Photo Mode and Video Mode camera, focus, vegetation and capture behavior.
- Added persistent Photo/Video/Director camera profiles that restore gameplay camera settings cleanly on exit.
- Reorganized Post Processing into clearer processing, depth-of-field and preset pages with themed quick presets.
- Fixed fresh installations skipping Quick Start when no user configuration exists.
- Added source provenance, third-party attribution, branding separation and public-release hygiene documentation.
- Hardened shader validation, package manifests, cache validation, deployment rollback and interface-icon packaging.
- Promoted the final live-tested renderer configuration as the default for future release builds.

The bundled pipeline library was compiled from the 1.0.5 shader/module set; allow any hardware-specific missing permutations to finish on first launch.
