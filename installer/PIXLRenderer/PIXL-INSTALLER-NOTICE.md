# PIXL Renderer 1.0.3a installer notice

Neural Rendering is optional and its runtime DLL is not bundled. Select Neural
Rendering in Quick Setup for the illustrated manual-install guide, or open NR
Setup Guide in the renderer's Neural Rendering panel. Follow the packaged README,
check the file, save settings and fully restart Skyrim. Ordinary rendering does
not need NR. This silent 1.0.3a refresh preserves existing user settings.

PIXL Renderer is an owner-directed graphics project developed through hands-on
testing, experimentation and substantial AI assistance. AI tools assisted with
code, documentation, diagnostics and iteration; release decisions, visual
direction and live game testing remain under PIXL Studio's direction.

This is a complex SKSE/DX11 renderer and cannot be validated against every GPU,
Skyrim runtime or mod combination. Back up important files and saves, install
through a mod manager, and allow shader compilation to finish before evaluating
performance or visuals.

## Deployment and shader ownership

PIXL is packaged as a normal **Data** mod. The archive contains individual PIXL
files under `SKSE`, `Shaders`, and `Interface`; it does not require replacing or
merging the entire Skyrim `Data\Shaders` directory. Keep Community Shaders and
other engine-level shader renderers disabled for this release. If a mod manager
reports a conflict, resolve only the named PIXL feature files and follow the
priority guidance below rather than allowing a blanket shader-folder override.

Vortex and Mod Organizer 2 are the supported virtual-install paths. In Vortex,
deploy PIXL after any mod that supplies a file PIXL is intentionally replacing.
In MO2, put PIXL lower in the left pane so it wins those specific conflicts.
Avoid NMM's legacy virtual-install/symlink mode for PIXL: `.symlink` placeholder
files can leave shader assets unavailable to Skyrim even when the mod appears
installed. Use a real extraction, Vortex, or MO2 and verify that the files exist
under the active Skyrim `Data` view before launching SKSE.

After changing priority or reinstalling a conflicting shader provider, close
Skyrim and redeploy PIXL, then allow the shader cache to rebuild. Do not copy a
second PIXL `Shaders` folder over the package or mix files from another PIXL
version.

## SurfaceTides 1.0.2 bridge

The optional bridge is deliberately version locked. It requires the original
SurfaceTides 1.0.2 installation and replaces that version's DLL, water shader and
`SurfaceTides.ini`. The supplied PIXL preset enables `[Compatibility]
AllowPIXL=1` automatically. Back up custom SurfaceTides tuning before selecting
the integration.

The replacement is one universal SurfaceTides DLL for Skyrim SE 1.5.97, Steam
1.6.1170, GOG 1.6.1179 and 1.7.104. Use the SKSE and Address Library release that
matches the installed game. Do not install an upstream runtime-specific
SurfaceTides DLL afterward: in Vortex, set PIXL to load after SurfaceTides; in
Mod Organizer 2, place PIXL lower in the left pane. PIXL must win all three file
conflicts. If SurfaceTides is reinstalled or updated, reinstall PIXL and reselect
this integration.

The bridge option intentionally remains selectable without a FOMOD file-presence
gate. Vortex can temporarily hide or stage another mod's deployed DLL during
replacement, which made valid SurfaceTides installations appear unavailable.
Runtime capability checks still fail closed when PIXL's bridge is unavailable.

PIXL integration uses full SurfaceTides strength in wilderness water, 55% in city,
town and settlement locations, and 25% in interiors. The preset also uses stronger
forcing, longer wave retention and a modest normal/wake increase while retaining
the tested grid, tessellation ceiling and stability limits.

Do not carry this replacement DLL into a later SurfaceTides release. Future
SurfaceTides updates require a bridge rebuilt from that release unless PIXL
support has been merged upstream by the SurfaceTides author.
