# Shared Shader Cache ABI Incident — 2026-09-02

## Symptom

The live game rendered most opaque geometry black while emissive surfaces, snow/water-like passes and the sky remained partially visible.

## Log Evidence

`PIXLRenderer.log` contained no shader compiler, DLSS, Neural Rendering, D3D11 or D3D12 runtime error. It reported:

- all module versions matched their cached versions;
- `Using disk cache`;
- Neural Rendering live evaluation was disabled;
- only selected Water, RunGrass and reflection stages were rebuilt during the affected launch.

The persistent pipeline library contained 3,562 stages. Lighting, Effect, DistantTree and Sky families still included binaries compiled before the current shared-buffer layout.

## Root Cause

PIXL cache validation used only:

- the public plugin version;
- pipeline path-layout version;
- per-module version/load state.

The current polish work appended fields to shared shader constant-buffer structures. `FeatureData b6` is a single packed sequence of module structures, so enlarging an earlier structure changes the offsets of every later structure. Per-module invalidation is insufficient because a shader can read a shifted later structure without carrying the define of the module whose earlier structure grew.

The old cached shader therefore read valid GPU memory at incorrect offsets. This is a cache ABI mismatch, not an HLSL compilation failure, and explains why the log looked healthy while opaque lighting was invalid.

## Correction

`ShaderCache.cpp` now writes and validates an explicit shared-shader ABI identifier:

```text
ShaderABI = PIXL.SharedBuffers.20260902.1
```

The identifier must be incremented whenever a globally shared b5/b6 layout, register contract, common structure or permutation-binary interpretation changes without a public plugin-version change. A missing or mismatched identifier forces full pipeline-library invalidation before any old stage can run.

The standalone packaging script also refuses to package a preloaded pipeline library with an incompatible or missing shared-shader ABI stamp.

## Recovery Performed

The affected 3,562-file, 227,848,810-byte library was moved—not deleted—to:

```text
Data/PIXL/PipelineLibrary.quarantine-20260902-2000
```

The live `PipelineLibrary` path is absent so the next launch performs one clean compile. The quarantine is recoverable for binary comparison and can be removed after the new library is visually validated.

## Validation

- Release plugin rebuild: PASS.
- Integrated 38-module repository audit: PASS.
- Live DLL deployment hash: PASS.
- Source diff whitespace/error check: PASS.
- Runtime shader compilation and visual validation: pending next launch.

