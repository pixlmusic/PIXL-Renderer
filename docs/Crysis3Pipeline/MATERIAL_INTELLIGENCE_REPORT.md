# PIXL Material Intelligence Audit

## Outcome

PIXL already has the core of the Crysis-inspired "material discipline" requested for the renderer. Authored Material Forge assets use an explicit metallic/roughness contract, while ordinary Skyrim materials are adapted into bounded base colour, perceptual GGX roughness, dielectric F0, and conservative inferred metalness. The release-safe choice is to preserve this visual baseline rather than add another classifier or globally retune it.

This pass therefore made two diagnostic/correctness changes only:

1. fixed a false-positive in the automatic hair evidence path (`chair` contains the substring `hair`); and
2. added a developer-only health view for the backend-neutral physical-material registry.

No GPU constant layout, resource binding, shader define, permutation, or rendered default was changed.

## Current Architecture

```text
Skyrim BSLightingShaderMaterialBase
    |  legacy specular colour / scale / shininess / texture-set features
    +--> PhysicalMaterial::FromLegacy (raster shader)
    |       +--> bounded base colour
    |       +--> Blinn exponent -> perceptual GGX roughness
    |       +--> bounded dielectric F0
    |       +--> optional conservative conductor inference
    |
Material Forge authored material
    |  RMAOS + explicit traits + authored scales
    +--> BSLightingShaderMaterialPBR descriptor
            +--> metallic/roughness PBR raster path
            +--> backend-neutral PhysicalMaterial registry

Both paths
    +--> energy-conserving direct-light evaluation
    +--> specular anti-aliasing
    +--> optional rough-metal multi-scatter compensation
    +--> material debug views
```

The registry is deliberately API-neutral. It assigns stable session IDs, records semantic texture bindings and revisions, and lets a future backend consume immutable snapshots without embedding Skyrim or D3D11 handles in the material contract. The current raster image is still driven directly by `Lighting.hlsl`; registry diagnostics do not affect it.

## Legacy Normalization Findings

### Base colour and colour space

- Authored PBR base colour is decoded to linear before F0/metal energy separation when Linear Light Core is disabled, then returned to the legacy path's expected transfer function.
- Legacy `FromLegacy` bounds input base colour with `saturate` and keeps the existing tuning strength as an explicit compatibility blend.
- No second implicit sRGB decode was found in the physical adapter itself.

### Roughness

- Legacy Blinn-Phong shininess is converted to perceptual GGX roughness with `sqrt(sqrt(2 / (n + 2)))`.
- Inputs are guarded against negative exponents and output remains within configurable physical bounds.
- Authored RMAOS roughness is clamped to PIXL's material limits.

### Fresnel and metalness

- Legacy specular colour/strength is interpreted as a relative dielectric control and scaled from a 4% reference instead of being treated as literal conductor reflectance.
- Environment-map colour overrides remain inside a bounded dielectric range.
- Guessed metalness uses environment response, smoothness, specular evidence, colour correlation, opacity, and tunable confidence floors. Emission never contributes.
- Authored Material Forge metallic data remains authoritative and is not capped by the legacy ceiling.

### Energy and stability

- Inferred conductor weight attenuates diffuse energy and promotes base-colour-tinted F0.
- GGX rough-metal multi-scatter compensation and derivative specular AA are already integrated.
- All user-facing physical parameters are runtime constants; the existing material debug modes do not need a shader rebuild.

## Changes Implemented

### A — Release Safe: automatic-hair lexical rejection

`ClassifyHairEvidence` intentionally accepts concatenated names containing `hair`. That also made a hierarchy named `chair` score as hair because the prior furniture rejection examined only the texture path. Furniture tokens now reject against the combined texture/mesh hierarchy, while actor-head exclusions remain texture-only so a legitimate hair mesh under a `Head` node is not rejected.

The existing final eligibility gates are unchanged: automatic hair still needs alpha geometry, skinning, a compatible lighting technique, the module loaded, and the configured confidence threshold.

### A — Release Safe: physical registry diagnostics

A new developer-only `Physical Material Registry` panel reports:

- table generation;
- total, legacy-model, and metallic/roughness-model rows;
- file-backed and runtime texture identities;
- automatic fur descriptor count; and
- invalid descriptor count.

Validation detects schema/model mismatches, invalid binding counts, and non-finite physical/glint payloads. It is read-only, appears only in developer mode, remains collapsed by default, and allocates no snapshot vectors.

### Maintainability

The legacy-metal inference comment now matches the actual implementation: an environment mask is the strongest evidence, not a mandatory input. No executable HLSL changed.

## Issues Found but Not Changed

### Automatic fur write ordering — controlled follow-up

The `AUTO_FUR` block in `Lighting.hlsl` writes bounded roughness and F0, but the later legacy physical-surface commit writes `physicalSurface.Roughness` and `physicalSurface.F0` back over those two fields. Fuzz colour/weight survives, so the feature is not completely inert. Correcting the order is visually desirable but touches the global Lighting shader and would invalidate many permutations during the owner's current live-testing cycle. It was intentionally documented rather than changed in this conservative pass.

### Registry lifetime growth — future architecture

PBR material destructors release owned material rows. Ordinary legacy rows have no equivalent destruction hook in the registry, and named/runtime texture identity maps are session-scoped. Skyrim's material set is normally bounded, but long heavily modded sessions can grow these metadata maps. Refcounted texture identities or a safe legacy-material destruction hook should be prototyped after release; adding an unproven lifetime hook now would be riskier than retaining the small CPU metadata.

### Backend table is not the raster source of truth

The registry is a strong future scene-description foundation, but no active consumer uploads its material table for current raster lighting. Debugging it must not be mistaken for proof that a raster material is shaded correctly. The existing on-screen shader debug modes remain the authoritative visual diagnostic.

## Per-File Review

| File | Role / active evidence | Assessment and decision | Validation |
| --- | --- | --- | --- |
| `engine/MaterialForge.cpp` | Module lifecycle, GUI, PBR hooks, per-draw bindings; built into `PIXLRenderer` | Added collapsed developer registry health view. Preserved existing visual settings and hooks. | Release build pass. |
| `engine/MaterialForge.h` | Settings, ABI assertions, authored material state | Five-register `Settings` ABI and dedicated b10 tuning ABI are explicitly asserted. No layout change. | Compile-time assertions retained. |
| `engine/MaterialForge/PhysicalMaterial.h` | Backend-neutral schema | Schema 3 is standard-layout/trivially-copyable and size asserted. No schema change. | Existing static assertions pass. |
| `engine/MaterialForge/PhysicalMaterialRegistry.cpp` | Legacy/PBR observation, classification, revisioning, snapshots | Fixed `chair` false positive; added lock-safe, allocation-free diagnostics. | Release build pass. |
| `engine/MaterialForge/PhysicalMaterialRegistry.h` | Registry API/ownership contract | Added CPU-only diagnostics value type/API; no GPU ABI. | Release build pass. |
| `engine/MaterialForge/DX11TextureResolver.cpp/.h` | Optional retained SRV bridge for future backend | Correctly bounded to 64 retained views and releases them when disabled. No change. | Static review. |
| `engine/MaterialForge/BSLightingShaderMaterialPBR.cpp/.h` | Authored object descriptor and texture loading | Explicit physical parameters correctly populate the shared descriptor; automatic fur semantics reach the live descriptor before raster flags. No change. | Static review. |
| `engine/MaterialForge/BSLightingShaderMaterialPBRLandscape.cpp/.h` | Six-layer landscape descriptors | Non-PBR layers fail back to legacy shading model; authored layers expose RMAOS/displacement/glint. No change. | Static review. |
| `distribution/Shaders/Common/PhysicalMaterial.hlsli` | Legacy-to-physical conversion and conductor inference | Math is bounded and energy-aware. Corrected comment only. | Live/source executable content otherwise unchanged. |
| `distribution/Shaders/Common/MaterialForgeTuning.hlsli` | Dedicated PS b10 tuning ABI | Magic/version fail-safe and per-field bounds are appropriate. No change. | Source and live shader hashes matched during audit. |
| `distribution/Shaders/Common/PBR.hlsli` / `PBRMath.hlsli` | Authored BRDF, traits, multi-scatter | Existing GGX/material response is already the appropriate PIXL implementation. No global retune. | Source and live shader hashes matched during audit. |
| `distribution/Shaders/Lighting.hlsl` | Active raster integration and debug output | Traced authored and legacy consumers plus all 13 debug modes. Deferred fur ordering fix. | Source and live shader hashes matched during audit. |
| `pipeline/MaterialForge/Module.ini` | Shipping module registration | Active integrated module, version 1-1-0. No change. | Included by build/package graph. |

## Security and Safety

- No networking, telemetry, process execution, shell invocation, credentials, or new file access was added.
- Diagnostics expose only aggregate counts, never paths, pointers, texture names, or personal data.
- The registry continues to use shared/exclusive locking; the new diagnostic query holds one shared lock and is available only in an explicitly opened developer panel.
- No third-party code was modified.

## Performance

- Normal gameplay cost: unchanged.
- Shader cost/permutations: unchanged.
- Developer panel cost: one bounded table scan only while its collapsed subsection is open; no table copy or GPU readback.
- Memory: a small stack diagnostics value only.

## Validation

- `cmake --build build\PIXL-12C --config Release --target PIXLRenderer -- /m:2 /nr:false`: **PASS**.
- Output: `build\PIXL-12C\Release\PIXLRenderer.dll`.
- `tools\AuditPixlRenderer.ps1 -BuildDirectory build\PIXL-12C\Release`: **PASS**, 38 integrated modules.
- Newly introduced compiler errors/warnings: none. The build reported existing MSB8028 shared-intermediate-directory warnings in FidelityFX projects.
- `git diff --check`: **PASS** (line-ending conversion notices only).
- Shader validation: no executable shader instruction changed, so no cache rebuild is required for this subtask.
- Runtime visual validation: pending; no deployment was performed.

## Recommended Runtime Checks

1. In developer mode, open Material Forge -> Physical Material Registry and confirm descriptor validation reports `OK` after entering a populated exterior and interior.
2. Enable Adapter Coverage, Roughness, Classification, F0, Raw Metalness, and Effective Metalness views in turn; inspect wood, stone, iron, steel, leather, skin, foliage, snow, glass, and emissives.
3. Verify alpha-tested furniture/chairs never enter Hair Reconstruction classification.
4. Verify native and modded hair still classify when attached below a head node.
5. Record a separate fur A/B test after the deferred write-order correction is scheduled.

## 5 Future Visual Improvements

1. Correct the legacy `AUTO_FUR` commit order and A/B its roughness/F0 response.
2. Add local reflection-probe identity/confidence to the future scene material query.
3. Introduce opt-in class priors for stone, wood, leather, cloth, and ice only when stable runtime evidence exists.
4. Feed thickness/transmission profiles through one shared material contract for skin, leaves, snow, ice, hair, and cloth.
5. Add an explicit debug view for conductor evidence components rather than exposing more user sliders.

## 5 Future Performance Improvements

1. Cache aggregate diagnostics by registry generation if developer tables become very large.
2. Add safe reference accounting for session texture identities.
3. Upload only changed material table rows to a future auxiliary scene buffer.
4. Share normalized physical parameters between raster, GI, reflections, and decal consumers.
5. Separate frequently updated object state from immutable material metadata.

## 5 Future Feature / Research Ideas

1. Compact PIXL auxiliary material buffer keyed by stable material ID.
2. Confidence-carrying automatic material-class inference with explicit fallback.
3. Deferred physical decals for snow, mud, blood, frost, soot, and water films.
4. Per-class transmission and mean-free-path profiles.
5. Offline developer report exporting anonymous aggregate class/confidence counts only (never asset paths by default).
