# PIXL Renderer attribution and ancestry

PIXL Renderer is a modified work derived in substantial part from the Skyrim
Community Shaders project. The historical comparison baseline for this release
is Community Shaders v1.8.3, commit
`2f2919a71bed6132b125e41781304c8f6f73d002`:

- https://github.com/community-shaders/skyrim-community-shaders
- https://github.com/community-shaders/skyrim-community-shaders/releases/tag/v1.8.3

Community Shaders and its contributors retain credit for upstream code and
assets. PIXL Studio is credited only for PIXL Renderer modifications and
independently authored PIXL material. PIXL Renderer is not endorsed by or
affiliated with the Community Shaders team or Bethesda Game Studios.

## Licence and modification notice

The covered renderer source is distributed under the same GPL-3.0 convention
used by the v1.8.3 upstream project. The complete licence is in `COPYING`. The
unchanged Community Shaders modding/linking additional permission is in
`EXCEPTIONS.md`.

PIXL Studio substantially modified the upstream work for PIXL Renderer through
August 2026. Changes include an integrated renderer/module architecture,
renamed and extended rendering systems, PIXL settings and quality profiles,
new runtime integrations, shader changes, user interface work, packaging, and
product branding. File history and this notice must be kept with redistributed
source so upstream work is not represented as PIXL-original.

## Required upstream infrastructure retained

### SmoothCam compatibility interface

`engine/Menu/SmoothCamAPI.h` is the unmodified modder API header from mwilsnd's
SmoothCam, file revision `bf62523b24a1de45c2b4b18c35730246d2f5b48c`:
https://github.com/mwilsnd/SkyrimSE-SmoothCam/blob/bf62523b24a1de45c2b4b18c35730246d2f5b48c/SmoothCam/include/SmoothCamAPI.h
The header expressly permits modders to copy it into their projects for API use.
Credit for the interface belongs to its upstream authors. PIXL's camera lease
and restoration integration is PIXL code; SmoothCam itself is not bundled.

Core plugin loading, SKSE/CommonLib integration, Direct3D hooks, shader-cache
and shader-compiler infrastructure, settings/serialization, weather support,
menu infrastructure, engine fixes, and many shared shaders remain derived from
or byte-identical to Community Shaders v1.8.3. Renaming `src` to `engine` does
not change that provenance.

## Renamed or extended upstream systems

The following table records the principal module ancestry. Classification as
"derived" includes both lightly renamed modules and modules that PIXL has
substantially extended; it does not assign individual authors where the source
history does not establish one reliably.

| Community Shaders v1.8.3 system | PIXL Renderer system |
|---|---|
| Feature/module infrastructure | RenderModule and integrated pipeline infrastructure |
| TruePBR | MaterialForge |
| IBL | AmbientProbe |
| Exponential Height Fog | Atmosphere |
| HDR Display | CameraSuite |
| Screen-Space Shadows | ContactShadows |
| LOD Blending | DistanceBlend |
| Grass Lighting | FoliageDynamics |
| Grass Collision | GroundResponse grass-collision field only |
| Horizon Fix | HorizonBlend |
| Screen-Space GI | HybridGI / Radiance Weave |
| Upscaling | ImageReconstruction |
| Interior Sun | InteriorDaylight |
| Volumetric Lighting | LightVolumes |
| Linear Lighting | LinearLightCore |
| Extended Materials | MaterialLayers |
| Inverse Square Lighting | NaturalLighting |
| Screenshot Feature | PixelCapture |
| Performance Overlay | PulseProfiler |
| Light Limit Fix | RadiantGrid |
| Wetness Effects | RainResponse |
| Skin | SkinOptics |
| Skylighting | SkyBounce |
| Sky Sync | SkyContinuity |
| Cloud Shadows | SkyVeil |
| Hair Specular | StrandShading |
| Terrain Variation | TerrainDetail |
| Terrain Helper | TerrainField |
| Terrain Shadows | TerrainOcclusion |
| Terrain Blending | TerrainSeam |
| Extended Translucency | ThinSurface |
| Subsurface Scattering | TissueDiffusion |
| Volumetric Shadows | VolumeOcclusion |
| Water Effects | WaterOptics |
| Unified Water | Waterbody |
| Dynamic Cubemaps | WorldProbes |

Effects11, CS Editor, Remote Control, and RenderDoc feature integrations from
the upstream baseline are not active PIXL rendering modules. Narrow legacy-name
references may remain only where required to detect, migrate, or report an
incompatible old installation.

## Ground Response provenance clarification

`GroundResponse` contains two separate rendering paths that are intentionally
described separately:

1. **Grass Collision** retains the actor-driven grass interaction field. It is
   the path corresponding to the upstream Grass Collision entry above. The
   active implementation uses `CollisionUpdateCS.hlsl`, a dedicated collision
   texture, and the Skyrim grass-shader draw hook.
2. **PIXL Ground Response terrain deformation** is the PIXL-authored snow/mud
   system. It uses its own material classification, persistent absolute-world
   deformation fields, and DirectX 11 hull/domain stages to build a raised,
   compressible terrain surface. It is not a copied Community Shaders
   snow/mud-deformation implementation.

The snow/mud path was developed from observable rendered behaviour, controlled
runtime testing, and public DirectX 11 tessellation/hull-mesh research. This is
an engineering-provenance statement, not a legal authorship determination; the
upstream code history and third-party notices remain authoritative wherever
they apply.

## PIXL-specific additions

WindowLife, DialogueFocus, renderer quality-profile orchestration, PIXL tuning
and benchmark interfaces, physical-material extensions, and other files with
no v1.8.3 counterpart are tracked as PIXL-specific additions for engineering
provenance. Absence from v1.8.3 alone is not a legal determination of original
authorship; embedded notices and repository history remain authoritative.

Third-party components and shader fragments retain their own notices. See
`THIRD_PARTY_NOTICES.md` and the licence files beside those components.
