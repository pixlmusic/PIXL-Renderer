# Ground Response Phase 2

## Surface scheduling

- The 1024x1024 snow/mud field now uses 32x32 logical tiles (four 8x8 compute
  groups each) and dispatches only tiles intersecting new stamps, recovery
  regions, or newly exposed toroidal slabs.
- CPU preparation builds tile headers, compact per-tile stamp indices, and a
  compact tile dispatch list. Tiled shader groups consume the local stamp list;
  recovery-only tiles avoid stamp traversal entirely.
- Active recovery regions are conservative, merge repeated nearby contacts, and
  expire only after the longest current visible settling/recovery window.
- A stationary field with no recovery region issues no surface compute dispatch.

## Safety and compatibility

- `ForceFullSurfaceSimulation` retains the established full 1024x1024 dispatch
  as a developer fallback. Failed tile resources or capacity overflow use it
  automatically.
- Texture formats and t101/t102/t103 rendering contracts are unchanged.
- Elemental snow now unbinds t103 as a compute UAV while disabled; the shader
  has an explicit branch and leaves the cleared resource untouched.
- Grass collision remains independent and unchanged in this phase.

## Diagnostics

Developer mode exposes tile dispatches, stamp references, maximum references
per tile, slab tiles, and fallback count. Toggle `Force Full Surface Simulation`
for a direct old/new visual comparison with identical gameplay input.

## Validation

- Release `PIXLRenderer.dll` built successfully.
- `SurfaceDeformationUpdateCS` and `CollisionUpdateCS` compiled with warnings
  treated as errors.
