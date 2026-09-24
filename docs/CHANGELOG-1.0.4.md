# PIXL Renderer 1.0.4 candidate

- Added a SurfaceTides status page in PIXL Extensions. It distinguishes a loaded DLL from confirmed water-draw compatibility and explains the existing optional 1.0.2 bridge.
- Updated executable, interface, installer, and package version metadata to 1.0.4.
- Changed pipeline-library metadata so old 1.0.3a shader stages are rebuilt for the updated shared lighting/material shader source.
- Preserved the current Director Video Mode, material/lighting, WindowLife, and GUI improvements in the source tree; these remain subject to live-game validation before a public release claim.
- Retained existing settings keys and the SurfaceTides version-locked FOMOD choice.

This candidate uses compile-on-device shader cache mode. First launch must finish shader compilation. See `RELEASE_CHECKLIST_1.0.4.md` for tests still required.
