# Ground Response Phase 3 Changelog

## Implemented

- Added dirty-tile derived reconstruction for filtered compaction, settled berm, wall-collapse response, and macro height gradients.
- Moved the expensive t101/t102/t103 neighbourhood reconstruction out of the normal terrain Domain Shader path.
- Added a developer `Force Legacy Terrain Surface` fallback. It selects the previous Domain Shader implementation without changing the public runtime ABI.
- Added final macro displacement normal reconstruction and TBN orthonormalization for the derived path. Existing tangent-space normal maps remain active.
- Reworked tessellation demand to remove the fixed close 12× floor. Shared edge factors now use projected edge demand, existing drift detail, and half-step quantization.
- Kept TerrainSeam replay, G-buffer/depth/motion output, existing t101-t104 bindings, Material Forge, Seasons, and directional-shadow PS resources unchanged.

## Resource contract

| Resource | Stage/register | Format | Purpose |
|---|---|---|---|
| Derived response | DS t105 | RGBA16F | current/previous filtered compaction plus freshness |
| Derived slump | DS t106 | RGBA16F | current/previous settled berm and wall-collapse response |
| Derived gradient | DS t107 | RGBA16F | current/previous normalized height gradients |

The fields add 24 MiB at 1024². They are generated only for Phase 2 dirty/active tiles. The direct full-field fallback also regenerates them over the full field.

## Validation

- `cmake --build build\PIXL-12C --config Release --target PIXLRenderer --parallel 2` — passed.
- `fxc /WX` `cs_5_0` `SurfaceDerivedUpdateCS.hlsl` — passed.
- `fxc /WX` `hs_5_0` and `ds_5_0` `TerrainSurface.hlsl` — passed.
- Runtime b13 ABI remains 176 bytes and runtime version remains `0x00030100`.

## Live-game checks

1. Compare normal derived mode with **Force Legacy Terrain Surface** in the developer panel.
2. Verify footprints, long sprints, deep drifts, ragdolls, objects, fire, frost, and shouts across snow/mud/rock boundaries.
3. Move sideways/up/down with DLSS/DLAA enabled and verify continuous height, normal-map detail, and motion vectors.
4. Check sun shadows and forward objects at raised berms; inspect all TerrainSeam landscape layers for z-fighting.
5. Test first/third person, teleport, cell transitions, save/load, and Seasons material swaps.
