# Phase 8 — Declarative Settings and Shader ABI Metadata

## Outcome

PIXL now has a small dependency-free metadata layer for representative renderer settings and critical C++/HLSL buffer contracts. It validates shared facts without replacing the established JSON settings framework or generating renderer logic.

## Settings Schema

`RendererMetadata.h` records stable IDs, owning modules, friendly labels, categories, types, defaults, ranges, live/restart behavior and shader visibility for representative controls in:

- MaterialForge;
- CameraSuite Auto-DOF;
- HybridGI;
- GroundResponse;
- RainResponse;
- WaterOptics.

Each corresponding settings structure now takes its default from the schema. The selected UI sliders and runtime clamps take their min/max from the same descriptor, reducing three-way drift between defaults, controls and shader upload.

This exposed one real inconsistency: `CameraSuite::Settings` and release JSON used Auto-DOF f/3.4, while `RestoreDefaultSettings` silently changed it to f/1.8. Restore now uses the authoritative f/3.4 release default. Existing user configurations are not migrated or overwritten.

## ABI Metadata

Versioned records describe owner, buffer, register slot, size and alignment for:

- MaterialForge FeatureData: b6 / 80 bytes;
- CameraSuite HDR: b0 / 352 bytes;
- CameraSuite Auto-DOF: b1 / 128 bytes;
- HybridGI: b1 / 416 bytes;
- GroundResponse runtime: b13 / 208 bytes;
- RainResponse FeatureData: b6 / 256 bytes;
- WaterOptics FeatureData: b6 / 64 bytes.

Existing module `static_assert` size checks now consume those records. Existing detailed field-offset assertions remain in place. A deterministic FNV-1a metadata hash is exposed for diagnostics; this is not yet a shader-cache key and therefore does not change cache compatibility.

## Developer Diagnostics

The Developer panel displays schema defaults/ranges, shader visibility, ABI slots/sizes/versions and the aggregate metadata hash. It is read-only and incurs no work while the panel is closed.

## Shader and Runtime Impact

- No cbuffer field, offset, size, slot or shader source changed.
- No setting key changed.
- Existing JSON remains backwards compatible.
- No new build or runtime dependency was introduced.
- The only visible default correction occurs after an explicit CameraSuite reset and aligns it with shipped defaults.

## Validation

- Portable metadata test validates unique IDs/buffers, finite defaults/ranges, alignment, slots and deterministic hash generation.
- Contract validation confirms module size assertions reference metadata and HLSL still declares the expected b1, b6 and b13 owners.
- All 12 portable CTest checks passed.
- Release `PIXLRenderer.dll`: built successfully.
- `PIXL-Audit`: passed; all 42 shipping modules retained.
- All 12 portable CTest checks passed from the full build tree.
- `git diff --check`: no whitespace errors (repository line-ending conversion notices remain).
- Shader reflection expansion beyond existing RenderOrigin coverage is deferred; static size/offset/register validation is active now.

## Deferred Work

- Expand metadata only as controls are touched, avoiding a risky mass conversion.
- Add reflection-backed layout comparison for compiled critical cbuffers.
- Generate documentation/tooltips only after localization ownership is defined.
- Consider the ABI hash as a cache input in a deliberate cache-version phase, not silently.
