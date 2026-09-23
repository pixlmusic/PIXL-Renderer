# Ground Response Phase 3 — Domain Shader Cost Map

## Baseline path

`TerrainSurface.hlsl` performed the following per generated Domain Shader vertex inside the interaction radius:

| Function | Baseline work | Phase 3 destination |
|---|---:|---|
| `GroundFilteredCompactionField` | 9 manual bilinear t101 samples, plus 5 heat taps when active | `SurfaceDerivedUpdateCS` dirty tiles |
| `GroundElementSampleAbsolute` | 1 manual bilinear t103 sample | derived compute filtering; DS still samples elemental height once for material-specific mass |
| `GroundSearchTrenchOutward` | 16 manual t101 samples | derived compute |
| `GetGroundBulkSlump` | outward search, 5 t102 bilinear samples, 8 additional t101 wall reads | derived compute |
| `GroundDepthVariationScale` / deep drift | stable world-space procedural evaluation | retained in DS because it is per-material/pristine geometry |
| Layer activation/thickness | six-layer Material Forge/Seasons-resolved draw data | retained in DS so terrain replay remains authoritative |

The worst path was therefore dozens of manual field fetches for each tessellated vertex, including repeated identical values across a patch. Phase 3 stores the filtered interaction response in dirty-tile fields and reduces the normal DS path to three bounded bilinear derived samples plus the existing single elemental sample.

## Contracts

- b13 `GroundResponseRuntimeCB`: unchanged at 176 bytes, version `0x00030100`.
- Base surface history: t101-t103 remains unchanged.
- Derived response/slump/gradient: **DS-only** t105-t107. Pixel shader t105-t107 retains the directional-shadow contract.
- Derived textures: 3 × 1024² RGBA16F = 24 MiB. They are updated only for Phase 2 dirty/active tiles.
- `Force Legacy Terrain Surface`: developer fallback that restores the existing DS reconstruction. It uses an existing debug-bit word and does not alter the runtime ABI.

## No-cull review

The replay still retains its no-cull rasterizer. The safety issue is not only scalar deformation amplitude: layered landscape source topology and grazing-angle tessellated patches can still produce degenerate/inverted generated triangles. Re-enabling culling requires a conservative per-patch foldover proof and is deferred rather than risking visible terrain holes in release testing.
