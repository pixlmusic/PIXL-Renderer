# Lighting integration notes

This is the working map for the Lighting-category pass. It records the current
runtime ownership before any larger slider consolidation is attempted.

## Current module flow

| Group | Modules | Shared responsibility |
| --- | --- | --- |
| Indirect light | HybridGI, WorldProbes, SkyBounce, AmbientProbe | Screen/world GI, probe radiance, sky ambient and rough environment response |
| Direct light | NaturalLighting, LinearLightCore, RadiantGrid, ContactShadows | Physical light conversion, linear accumulation, clustered local emitters and contact grounding |
| Sky and atmosphere | SkyVeil, InteriorDaylight, SkyContinuity, Atmosphere, LightVolumes, VolumeOcclusion | Weather/sky state, interior sun, analytical and volumetric fog, shafts and volumetric occlusion |

The modules already share renderer resources and quality profiles, but their UI
currently exposes mostly module-local controls. The first release-safe change is
therefore UI-only: `Menu::Settings::AdvancedControls` is a persisted global mode,
owned by the Tuner rail. It does not alter render values by itself.

## Global simple/advanced contract

- `AdvancedMode` remains the Tuner-open/full-navigator state and is intentionally
  separate.
- `AdvancedControls = false` is the public default and should show controls that
  are useful for normal visual tuning.
- `AdvancedControls = true` exposes engineering controls in every category.
- Module settings keep their existing values, serialization keys and runtime ABI.
- Existing module-local advanced sections are being migrated to the shared mode;
  no control is silently reset when the mode changes.

The first Lighting migrations are HybridGI engineer controls, Linear Light's
advanced tab, and Atmosphere's advanced volumetric-quality group. Further
Lighting modules should follow the same query rather than adding independent
static toggles.

## Proposed unified Lighting controls

These are UI orchestration controls, not replacement shader parameters:

1. **Indirect lighting balance** — coordinated GI contribution, sky/probe fill,
   and rough-reflection fallback, with per-module values retained in Advanced.
2. **Direct-light response** — directional/ambient calibration and local-light
   participation, avoiding duplicated brightness multipliers.
3. **Grounding and occlusion** — contact shadows, GI AO/contact depth and
   volumetric occlusion, with safeguards against double-darkening.
4. **Sky and atmosphere** — weather response, sky protection, fog scattering and
   volumetric light strength, preserving separate interior handling.
5. **Lighting quality** — continue routing workload changes through the existing
   `QualityProfiles::Group::Lighting` path rather than duplicating dispatch logic.

Before implementing these groups, each candidate control must be traced through
its module settings, constant buffers/resources and shader consumers. Artistic
controls remain available in Advanced so the simple mode cannot destroy authored
looks.

## Validation checklist

- Toggle Advanced while each Lighting module is selected; no value changes.
- Switch categories and confirm the same mode remains active.
- Load an older settings file without `AdvancedControls`; it defaults to false.
- Save/reload and confirm the mode persists.
- Verify quality presets still update the existing unified Lighting group.
- Confirm weather/scene constraints continue to win over UI edits.
