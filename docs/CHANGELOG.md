# PIXL Renderer 1.0.5 Changelog

Changes since 1.0.4:

- Upgraded Cinematic DOF with physical lens controls, unified autofocus and adaptive near/far bokeh.
- Restored reliable close-object autofocus while retaining first-person hand and weapon protection.
- Improved DOF foreground coverage, sky handling, temporal stability and DLSS/FSR/TAA integration.
- Added profile-aware Contained Liquids fill, classification, slosh, bubbles, refraction and absorption.
- Added PIXL-native Foliage Optimizer infrastructure with conservative Skyrim 1.5.97 fallback behavior.
- Fixed ordinary grass normals so Hybrid GI/SSGI reaches non-Complex Grass assets.
- Expanded Low, Medium, High and Cinematic into meaningful renderer-wide workload tiers.
- Promoted the live-tested DOF, vegetation, skin and Ground Response values to release defaults.
- Stabilized Ground Response by restoring direct terrain reconstruction while retaining newer simulation features.
- Improved Ground Response persistence, material response, weather accumulation and movement-resistance cleanup.
- Improved terrain, water, atmosphere, distant-light, hair, skin and directional-SSS integration.
- Improved Image Reconstruction reactive/disocclusion handling for camera effects, foliage and deformation.
- Expanded Director, Photo Mode and Video Mode camera, focus, vegetation and capture behavior.
- Added persistent Photo/Video/Director camera profiles that restore gameplay camera settings cleanly on exit.
- Reorganized Post Processing into clearer processing, depth-of-field and preset pages with themed quick presets.
- Added source provenance, third-party attribution, branding separation and public-release hygiene documentation.
- Hardened shader validation, package manifests, cache validation, deployment rollback and interface-icon packaging.

The bundled pipeline library was compiled from the 1.0.5 shader/module set; allow any hardware-specific missing permutations to finish on first launch.
