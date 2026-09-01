# PIXL Transmission and Screen-Space SSS Audit

Date: 2026-08-31
Status: existing production paths verified; no duplicate generic SSS pass added

## Outcome

PIXL already contains both halves of the Crysis-inspired target:

1. a material-local direct/indirect **transmission contract** used by PBR materials, skin, foliage and hair; and
2. a full-resolution **screen-space Tissue Diffusion** stage for skin-family pixels with material/profile rejection.

The renderer therefore does not need another global SSS blur. The valuable future work is to extend explicit, conservative material profiles only where PIXL has reliable thickness/classification evidence.

## Existing material transmission

`DirectLightingOutput` already contains a dedicated `transmission` lobe. The main Lighting path accumulates that lobe for the directional light and every eligible local/RadiantGrid light, then adds it once after direct diffuse/specular composition.

- Material Forge PBR subsurface materials read authored `SubsurfaceColor` and `Thickness`; direct evaluation supplies forward/back-scatter transmission and indirect diffuse weighting.
- Skin Optics owns its guarded tissue transmittance, thickness calibration, shadow visibility and energy caps instead of treating ears/nostrils as emissive fill.
- Foliage Dynamics supplies thin-surface tint, leaf/grass backlighting and diffuse-energy compensation.
- Strand Shading supplies Scheuermann/Marschner hair transmission, shadowed incident light and Hair Reconstruction response.
- Ground/snow has a separate thickness/scattering response appropriate to a particulate surface rather than borrowing the skin profile.

This already implements the correct architectural lesson: one lighting interface with specialized material profiles, not one identical blur applied to skin, leaves, snow and hair.

## Existing screen-space diffusion

Tissue Diffusion extracts diffuse irradiance, performs Burley or separable diffusion before tone mapping, and reapplies albedo according to the configured scatter mode. Its mask contract contains:

```text
x: scattering amount
y: human/beast profile selection
z/w: existing deferred material/coverage data
```

The filter uses center depth, FOV-scaled physical radius, per-sample depth rejection, material/profile agreement, finite normalization and a center fallback. This prevents skin energy from bleeding indiscriminately into armour/backgrounds. The 21-sample kernels and horizontal/vertical compute passes are already production infrastructure.

## Why a universal Crysis-style blur was rejected

- Foliage and hair often render through alpha-tested/forward paths and require directional thin-surface transmission, not broad skin diffusion.
- Snow and ice need thickness, absorption and internal scattering information that the current skin mask does not encode.
- Reusing the human/beast mask for unrelated materials would cause cross-material halos and increase full-screen bandwidth.
- A new generic profile ID requires a deliberate auxiliary-buffer channel and all producer/consumer ABI work; it must not be smuggled into an occupied mask channel.

## Safe future extension

1. Inventory real spare bits/channels in the existing deferred scene buffers.
2. Add a small semantic transmission-profile ID only if it survives all current mask consumers.
3. Keep skin as the only broad screen-space diffusion profile initially.
4. Add leaf/cloth/snow/ice profiles as directional local terms unless a captured visual case proves screen diffusion is required.
5. Require depth, normal and profile agreement plus reconstruction-reactive tagging for every new screen-space profile.

## Runtime validation queue

- skin/ears in sun, candlelight and strong backlight;
- skin beside armour, hair and background silhouettes for bleed;
- foliage against sky and local firelight;
- snow in thin and deep coverage;
- TAA/DLSS during dialogue, fast orbit and disocclusion;
- Tissue Diffusion off/on while preserving material-local transmission.

## Classification

- Existing generic transmission interface: **A — essential and active.**
- Existing Tissue Diffusion: **A — production screen-space SSS.**
- Additional material profiles: **B — useful only with reliable classification/thickness.**
- New universal multi-material SSS buffer/pass: **C — unjustified before an auxiliary-channel and temporal audit.**
