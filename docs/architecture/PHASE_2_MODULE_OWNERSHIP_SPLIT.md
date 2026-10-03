# Phase 2 — Internal Module Ownership Split

## Outcome

Phase 2 introduces narrow internal components beneath four existing public owners. Public module classes, hooks, settings, shader entry points, resource registers, serialization and module short names are unchanged.

## Changes

- `GroundResponse/SurfaceClassifier` now owns reusable texture-key normalization and conservative snow-name fallback matching.
- `CameraSuite/CameraPolicy` now owns frame-delta sanitization, frame-rate-independent response math and display/menu output-state precedence.
- `HybridGI/TemporalPolicy` now owns bounded delta-time handling and the fixed-rate world-cache clock.
- `ShaderCache/DescriptorUtilities` now owns stage-profile selection, shader path construction and technique extraction.

The large public implementations delegate to these components. The extracted logic was kept functionally equivalent, except that invalid camera delta values now resolve deterministically to 1/60 second rather than propagating `NaN` through camera state.

## Compatibility

- No public class layout changed.
- No C++/HLSL ABI changed.
- No settings key or default changed.
- No hook, relocation, shader register, entry point, profile or descriptor changed.
- No shader-cache revision bump is required.
- Existing fallback behavior remains in the public owners.

## Deferred Splits

Hook-heavy rendering, GPU-resource ownership and settings UI remain in their established translation units. Moving those blocks mechanically during release polish would enlarge regression surface without runtime benefit. The new internal directories are stable seams for later extraction after focused live validation. `PixelCapture`, `ImageReconstruction`, `PulseProfiler` and the remaining Ground Response/Camera Suite render paths retain their current ownership for this reason.

## Expected Runtime Impact

The delegated helpers are small and stateless. Optimized builds should inline or reduce them to equivalent code. No new allocation, filesystem access, GPU resource or per-frame registry was introduced.

## Validation

- Release `PIXLRenderer.dll`: built successfully.
- `PIXL-Audit`: passed; 42 shipping modules retained.
- `tools/TestPhase2ModuleInternals.ps1`: passed, including standalone `/W4 /WX` C++ tests.
- Changed HLSL: none; FXC validation not applicable.
- Shader/cache ABI impact: none.
- Live Skyrim validation: pending.
