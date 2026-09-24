# PIXL GUI framework 1.0.4

The PIXL theme, tuner, navigation, Photo Mode, Video Mode, and capture controls remain authoritative. Existing in-progress GUI and Director changes were preserved on the release branch.

## Extension presentation

`ExtensionRegistry.h` now carries an internal status enum without changing the extension callback ABI. The Extensions pillar renders a compact state label. SurfaceTides uses a small code-drawn wave mark and reports `NOT INSTALLED` or `DETECTED`; the latter deliberately does not imply a successful water-draw handshake. The SurfaceTides panel explains the exact 1.0.2 bridge and where to verify live activation. No third-party DLL is loaded by the UI.

## Current conventions

- Keep controls in the existing PIXL pages and use `PIXLStyle.h` palette/scaling helpers.
- Use `%s` or `TextUnformatted` for external strings, and wrap explanatory text inside available width.
- Show only confirmed runtime state. A detected optional DLL is not automatically compatible or active.
- Preserve Photo/Video state restoration and do not take camera ownership from an extension panel.

## Pending live checks

Inspect 720p through 4K, scaling, panel clipping, icon alignment, SurfaceTides absent/present, input ownership, Photo/Video exit, and UI state after a shader reload. The code-level GUI test does not render Skyrim.
