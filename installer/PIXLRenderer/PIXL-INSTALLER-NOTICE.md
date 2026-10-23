# PIXL Renderer 1.0.8 installer notice

Neural Rendering is optional and its runtime DLL is not bundled. Select Neural
Rendering in Quick Setup for the illustrated manual-install guide, or open NR
Setup Guide in the renderer's Neural Rendering panel. Follow the packaged README,
check the file, save settings and fully restart Skyrim. Ordinary rendering does
not need NR. Existing user settings remain in place during the update.

PIXL Renderer is directed, visually evaluated and released by PIXL Studio.
Development combines hands-on implementation and testing with automated coding,
diagnostic and documentation tools. Release decisions and in-game validation
remain under PIXL Studio's direction.

This is a complex SKSE/DX11 renderer and cannot be validated against every GPU,
Skyrim runtime or mod combination. Back up important files and saves, install
through a mod manager, and allow shader compilation to finish before evaluating
performance or visuals.

## Deployment and shader ownership

PIXL is packaged as a normal **Data** mod. The archive contains PIXL-owned
shader files under PIXL-Shaders, separate from Skyrim's Shaders directory,
plus files under SKSE and Interface. Installation leaves the existing
Data\Shaders tree untouched. Keep Community Shaders and other engine-level
shader renderers disabled because their runtime hooks overlap with PIXL. Resolve
conflicts inside PIXL-Shaders only between PIXL versions or explicitly installed
PIXL add-ons.

Vortex and Mod Organizer 2 are the supported virtual-install paths. In Vortex,
deploy PIXL after any mod that supplies a file PIXL is intentionally replacing.
In MO2, put PIXL lower in the left pane so it wins those specific conflicts.
Avoid NMM's legacy virtual-install/symlink mode for PIXL: `.symlink` placeholder
files can leave shader assets unavailable to PIXL even when the mod appears
installed. Use a real extraction, Vortex, or MO2 and verify that the files exist
under `Data\PIXL-Shaders` in the active Skyrim `Data` view before launching SKSE.

After changing priority or reinstalling a conflicting shader provider, close
Skyrim and redeploy PIXL, then allow the shader cache to rebuild. Do not copy a
second PIXL `PIXL-Shaders` folder over the package or mix files from another PIXL
version.

## Optional integrations

This FOMOD installs the regular PIXL Renderer package only. Optional third-party water integrations are not included; any future compatibility package will identify the supported release and required file priority separately.
