# PIXL Renderer 1.0.2 installer notice

PIXL Renderer is an owner-directed graphics project developed through hands-on
testing, experimentation and substantial AI assistance. AI tools assisted with
code, documentation, diagnostics and iteration; release decisions, visual
direction and live game testing remain under PIXL Studio's direction.

This is a complex SKSE/DX11 renderer and cannot be validated against every GPU,
Skyrim runtime or mod combination. Back up important files and saves, install
through a mod manager, and allow shader compilation to finish before evaluating
performance or visuals.

## SurfaceTides 1.0.2 bridge

The optional bridge is deliberately version locked. It requires the original
SurfaceTides 1.0.2 installation and replaces that version's DLL, water shader and
`SurfaceTides.ini`. The supplied PIXL preset enables `[Compatibility]
AllowPIXL=1` automatically. Back up custom SurfaceTides tuning before selecting
the integration.

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
