# Markarth depth follow-up

Owner confirmed the main parallax toggle removes the wall artifact. This
narrows it to parallax, not necessarily synthetic auto-POM.

Read-only Skyrim.esm inspection resolves reference 000B793E to base 000B7939,
MrkSilverfishInnPart05, Architecture/Markarth/MrkSilverfishInnPart05.nif.
No loose mesh at that path was present. Read-only Meshes0.bsa decompression
found the mesh's oldrock2.dds / oldrock2_n.dds / oldrock2_p.dds texture set,
among its other stone/window/decal sets. A plugin material override or exact
affected triangle was not captured; this is not proof of the final cause.

## Controlled test change

Disable the experimental authored-POM hardware depth resolve by default,
retaining authored UV parallax, auto-POM and their shading. Add an explicit
PIXL_PARALLAX_DEPTH_AUTHORED compile-time opt-in for developer comparisons.
Auto, Material Forge and terrain generic depth were already opt-in/off.
This is a general depth-path change, not an exact-asset exclusion. It avoids
reconstructed depth interacting with overlapping architecture; live testing
must establish whether it resolves this particular artifact. No height-map,
texture, mesh, window classifier or user setting was changed.

MaterialLayers version 1-3-4 invokes existing selective Lighting pixel-stage
invalidation. Do not clear the whole shader library. No CPU/GPU buffer layout
or resource slot changed.

## Terrain virtual-depth controls: diagnosis only

MaterialLayers.cpp UI edits the serialized TerrainVirtualDepth fields.
Prepass uploads the 304-byte tuning buffer to PS b9; matching HLSL getters
read it. However Lighting.hlsl defaults PIXL_PARALLAX_DEPTH_LANDSCAPE to 0,
so the terrain resolve and its TerrainVirtualDepthEnabled check are compiled
out. These controls therefore have no rendering effect in the shipped path.
The shader comments identify stability/UV reconstruction concerns and a
separate ground-deformation path. Do not simply flip this gate: restore only
after depth/prepass, overlapping geometry and temporal consumers are tested.
The controls have not been reconnected or removed in this diagnostic scope.

## File accounting

All below are active and reviewed for correctness, security, fidelity,
performance and future opportunities in this scope (not a whole-file audit).

| File | Modified | Purpose/dependencies, findings, validation and future |
| --- | --- | --- |
| distribution/Shaders/Lighting.hlsl | Yes | Shared lighting VS/PS; authored depth output now opt-in. Ordinary POM stays. Ten strict FXC cases cover authored deferred depth on/off, alpha, forward, ordinary auto-compatible VS/PS. Disassembly confirms SV_Depth removed only in the disabled comparison. Future: depth/prepass-consistent displacement; live Markarth validation pending. |
| pipeline/Material Layers/Module.ini | Yes | Cache provenance/version; existing module predicate invalidates its affected Lighting PS stages. Future: persisted per-stage include signatures. |
| engine/Modules/MaterialLayers.cpp | No | UI/serialization/b9 upload verified for virtual-depth fields; controls are currently ineffective. Future: mark unsupported controls unavailable, or implement validated depth path. |
| engine/Modules/MaterialLayers.h | No | 304-byte CPU tuning contract retained. Future: represent runtime availability separately from stored preference. |
| pipeline/Material Layers/Kernels/MaterialLayers/MaterialLayersTuning.hlsli | No | Matching fields/getters retained; no ABI modification. Future: test CPU/GPU offsets mechanically. |

No networking, third-party modification, per-frame CPU work, or measured
performance claim. The expected benefit is depth stability, with loss of the
experimental authored relief in depth-based effects. Actual GPU savings and
visual outcome are unmeasured. Broad POM disable and asset-name blacklists
were rejected; a matched screenshot test remains necessary.

Deployed Lighting.hlsl and MaterialLayers.ini with SHA256 verification while
Skyrim was closed. Previous files preserved under
build/deployment-backups/Markarth-Depth-20260913-084136-da9ce8a16956475cb0bb73d801c3a9f5.
No DLL/config/cache files were changed; release ZIP is unchanged. The runtime
will handle affected-stage invalidation on next launch.
