# PIXL 1.0.4 shader and CPU parity

## Binding and shader ownership

`CMakeLists.txt` and `tools/StagePixlRendererStandalone.ps1` assemble the active `distribution/Shaders` entry points with each active `pipeline/*/Kernels` tree. Module descriptors are copied to `Shaders/PIXL/Modules`; the retired Hair Reconstruction descriptor is excluded. `PIXL-Audit` checks the integrated stage. The optional SurfaceTides bridge calls `PIXL_QueryWaterDrawV1` with bound VS/PS objects and borrowed shader records; the V1 export layout was not changed.

## Current changes

No CPU/HLSL structure or shader register was changed by the SurfaceTides UI/version work. Existing uncommitted WindowLife and material edits predate this pass and must be tested as part of the release candidate. WindowLife's current module descriptor and shader source travel together through the stage script.

## Validation limits

The strict FXC scripts passed 32 material, 32 grass, 72 landscape/water, 9 WindowLife, and 588 compute cases. Include-path, dependency-union, and shader-define tests passed. They do not compile every possible Skyrim shader descriptor or prove live resource binding. A full cache rebuild and D3D debug-layer/live-game test are still needed before production release.
