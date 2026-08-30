# Seasons integration implementation report

## Scope and baseline

This pass adds optional native Seasons of Skyrim context to PIXL Renderer without changing PIXL's established visual defaults, Ground Response controls, shader permutations, GPU ABI, third-party mods, or shader cache.

Pre-change checkpoint:

`build/active-dev-checkpoints/20260829-054912-seasons-integration-prepatch`

Canonical baseline commit at discovery time:

`78df570e55a3772e355327cfd1653327654e1d47` with the owner's existing active-development changes preserved.

## Files inspected

### PIXL project context and lifecycle

- `AGENTS.md`
- `PIXL_ENGINEERING_CONTEXT.md`
- `PIXL_DEVELOPMENT_WORKFLOW.md`
- `PIXL_ACTIVE_STATE.md`
- `engine/XSEPlugin.cpp`
- `engine/Globals.h`
- `engine/RenderModule.cpp`
- `cmake/AddCXXFiles.cmake`

### Runtime material resolution

- `engine/Utils/Game.h`
- `engine/Utils/Game.cpp`
- `engine/MaterialForge.cpp`
- `engine/MaterialForge/BSLightingShaderMaterialPBRLandscape.h`
- `engine/MaterialForge/BSLightingShaderMaterialPBRLandscape.cpp`
- `engine/Modules/TerrainField.h`
- `engine/Modules/TerrainField.cpp`

### Ground Response and deformation

- `engine/Modules/GroundResponse.h`
- `engine/Modules/GroundResponse.cpp`
- `pipeline/Ground Response/Kernels/GroundResponse/Runtime.hlsli`
- `pipeline/Ground Response/Kernels/GroundResponse/SurfaceDeformationUpdateCS.hlsl`
- `pipeline/Ground Response/Kernels/GroundResponse/TerrainSurface.hlsl`
- `pipeline/Ground Response/Kernels/GroundResponse/GroundResponse.hlsli`
- `pipeline/Ground Response/Kernels/GroundResponse/DeformableGround.hlsli`

### Installed Seasons of Skyrim / Turn of the Seasons

- `Data/SKSE/Plugins/po3_SeasonsOfSkyrim.dll` version 1.9.1.0
- `Data/SKSE/Plugins/po3_SeasonsOfSkyrim.ini`
- `Data/SKSE/Plugins/po3_SeasonsOfSkyrim.pdb`
- current `po3_SeasonsOfSkyrim.log`
- `Data/Turn of the Seasons.esp`
- `Data/Turn of the Seasons.bsa`
- `Data/Turn of the Seasons - Textures.bsa`
- `Data/Turn of the Seasons_SWAP.ini`
- every installed file under `Data/Seasons/`
- official Seasons of Skyrim source at read-only reference commit `89837a27849c97a9f91d01f2911d14b49bef2662`

No third-party file was modified.

## Existing compatibility discovered

PIXL was not broadly incompatible with Seasons. Several important paths were already correct:

1. `Util::GetSeasonalSwap` reads the runtime replacement texture-set FormID that Seasons stores in `BGSTextureSet::pad12C`.
2. Material Forge resolves each landscape texture set through that helper before detecting or assigning PBR maps.
3. Terrain Field resolves seasonal texture sets when it populates its six extended landscape slots.
4. Skyrim's normal terrain material setup runs before Material Forge replacement. PIXL preserves its final six `textureIsSnow` values in `SnowMetadataByMaterial`.
5. Ground Response's render pass reads the current landscape material every draw. Vanilla terrain consumes the material's final `textureIsSnow`; Material Forge terrain consumes the preserved final metadata. Both classify the diffuse resources actually rendered.

Consequently, much of the visible seasonal terrain already followed Turn of the Seasons automatically.

## Missing compatibility and bugs found

The CPU contact/gameplay side was not guaranteed to match the render side:

- `TES::GetLandTexture` returns the base land-texture record at a world position.
- Ground Response directly used that record's original physical material, editor ID, and texture-set path for snow/hard-surface/contact decisions.
- During a seasonal replacement, this could classify the pre-swap land rather than the displayed land.
- The renderer-derived snow-path cache had no explicit seasonal invalidation.
- Terrain Field cached extended texture slots by material hash with no season-generation invalidation.
- The persistent deformation texture stores normalized compaction, not the material category that produced it. Old Winter footprints could therefore be reinterpreted as Spring mud or remain visible after thaw.

## Architecture implemented

Added `SeasonIntegration`, an isolated compatibility service with two responsibilities:

1. Obtain optional authoritative season context through the public native API.
2. Resolve a base `TESLandTexture` to the currently substituted runtime texture set and, where possible, its owning replacement land-texture record.

The provider snapshot contains availability, known/unknown status, current season, and a monotonically increasing generation. Ground Response sees that snapshot but never calls the external DLL directly.

## Soft native provider

- Loaded process modules are enumerated at `PostPostLoad`.
- A provider is accepted when it exports the documented read-only `GetCurrentSeason` capability. Override behavior is already reflected by that authoritative query, so PIXL does not require or call a separate override API.
- The function is obtained with `GetProcAddress`; there is no import library, `LoadLibrary`, DLL filename dependency, or version gate.
- Discovery is retried at `DataLoaded`, game-state refresh, and no more than once every 30 seconds if a provider was genuinely loaded late.
- Active providers are polled once per second on the existing Ground Response game/main-thread update hook.
- The render thread never performs provider discovery or a provider call.
- Unknown values map to neutral context and a diagnostic warning.
- Missing provider behavior remains the material-driven PIXL baseline.

The built DLL import table contains no Seasons of Skyrim dependency or imported season function.

## Resolved land-texture behavior

At `DataLoaded`, PIXL builds a compact `BGSTextureSet FormID -> TESLandTexture` lookup from loaded forms. For a Ground Response contact:

1. Resolve the base texture set through `GetSeasonalSwap`.
2. Use the replacement diffuse path for texture-name, renderer-snow-cache, and hard/soft classification.
3. Use the replacement land record's physical material when discoverable.
4. Otherwise retain the authoritative result from `TES::GetLandMaterialType`, which Seasons itself redirects for swapped landscape materials.
5. Never use the original land record's editor ID or physical material as proof after a swap when no replacement owner can be resolved.

This applies to actor movement resistance, localized receiver probes, blended loaded-land snow coverage, hard-surface rejection, and snow/mud deformation eligibility.

## Cache and deformation lifecycle

On a published generation change, Ground Response:

- clears its rendered-texture-to-snow classification cache;
- asks Terrain Field to clear season-dependent material-hash entries under its existing shared mutex;
- schedules a one-time render-thread clear of t101 surface compaction, t102 displaced snow, and t103 elemental snow/heat history.

New contacts from the newly resolved material can be uploaded and applied in the same update after the clear. No collision field, unrelated renderer history, shader cache, or configuration is reset.

The global field clear is intentionally conservative: without a per-texel material generation, selective preservation could retain or reinterpret physically invalid history.

## Turn of the Seasons validation

Installed configuration and the current Seasons log agree on the primary land-texture variants:

| Season | Turn of the Seasons land-texture swaps |
| --- | ---: |
| Winter | 9 |
| Spring | 10 |
| Summer | 9 |
| Autumn | 1 |

The generated `MainFormSwap_WIN.ini` supplies another 64 Winter land variants, and the installed set includes Wyrmstooth worldspace additions. Trees, statics, activators, furniture, movable statics, shrubs, grass, and aspens are also swapped by the pack, but they do not need Ground Response FormID tables.

The implementation deliberately contains zero Turn of the Seasons plugin checks and zero copied swap FormIDs. Every pack using the same runtime material-substitution contract benefits from the generic resolver.

The installed Seasons log reports missing seasonal terrain/object/tree LOD and fallback to default LOD. This is an asset-installation/runtime observation, not a PIXL error and not something PIXL modifies.

## Performance assessment

### CPU

- Active provider: one trivial function-pointer query per second.
- Missing provider: no query; loaded-module rediscovery is capped at one attempt per 30 seconds and is also triggered at explicit lifecycle refresh points.
- Contact classification: one small resolved-form lookup only when a seasonal texture swap is active. Unswapped materials return immediately.
- Transition: rare classification-map and Terrain Field cache clear.

Expected steady-state cost is negligible. No runtime benchmark number is claimed.

### GPU

- No shader changes.
- No cbuffer change.
- No SRV/UAV/sampler/register change.
- No new dispatch, draw, pass, texture, or buffer.
- One existing UAV clear sequence occurs only when a season generation changes or an active provider is explicitly refreshed after game load.

### Memory

One lookup entry per loaded land texture plus a small atomic context. No seasonal asset scan or duplicate texture storage.

## Build and static validation

- CMake automatically detected and incorporated the new source files.
- Release target `PIXL-Audit` compiled and linked successfully.
- Integrated pipeline audit passed with 37 modules.
- Final canonical DLL: 19,569,152 bytes, SHA-256 `6F93F35928AA4F23AA959D40D44E9E5F56422091143064E7C388E1F14A3E9041`.
- PE dependency inspection confirmed no hard Seasons DLL dependency.
- Scoped `git diff --check` passed; inherited line-ending notices are informational.
- No HLSL was modified, so CPU/GPU ABI and shader-cache identity remain unchanged by this task.
- Existing inherited FidelityFX `MSB8028` shared-intermediate warnings remain; no new compiler warning was introduced.

## Test matrix disposition

| Case | Static result | Live status |
| --- | --- | --- |
| No Seasons provider | Fail-open material fallback is explicit; no imported DLL | Pending launch without mod |
| Native provider discovery | Installed exports and bridge ABI verified | Pending log verification |
| None/disabled | Known value supported without material assumption | Pending |
| Winter/Spring/Summer/Autumn | All known values and generation transitions supported | Pending forced overrides |
| Permanent/custom months | Official runtime query is authoritative | Pending |
| Unknown future value | Neutral context + warning | Statically verified |
| Winter -> Spring | Caches/history invalidated once; material is re-resolved | Pending visual test |
| Autumn -> Winter | Newly resolved snow becomes eligible | Pending visual test |
| Save/load/new game | Refresh request advances active-provider generation | Pending |
| Interior/exterior/worldspace | No season-to-material assumption | Pending |
| Missing/renamed DLL | Capability scan, no filename or import dependency | Statically verified |
| Turn of the Seasons | Installed configs/log audited, no hard-coded IDs | Pending four-season visual test |

## Remaining risks and limitations

1. Live Skyrim validation is still required. Build success cannot prove that a specific modded landscape appears or deforms correctly.
2. If a seasonal provider is active but deliberately does not purge/rebuild an already loaded landscape after a state override, PIXL will invalidate its own cache/history but cannot force the third-party mod to resolve new assets.
3. A replacement TXST without a discoverable owning LT falls back to the Seasons-hooked `TES::GetLandMaterialType` at sampled positions and resolved texture evidence. That is safe, but less richly attributable in diagnostics.
4. The deformation field reset is global within PIXL's player-centered field because the existing texture does not store material generation. A future per-tile generation design could preserve compatible regions, but is not release-safe or necessary for this integration.
5. The installed seasonal LOD assets are currently absent according to the Seasons log. PIXL cannot classify or deform distant LOD as full loaded terrain; default LOD fallback remains owned by Seasons/installed assets.

## Recommended in-game validation

1. Launch with Seasons absent once and confirm `[PIXL][Seasons] ... material-driven fallback`, normal Ground Response, and no load failure.
2. Launch with the installed provider and confirm detection, native API, indexed land owners, initial season, and no repeated logging.
3. Force each known season through the Seasons override. At representative Whiterun tundra, northern snow, Rift, road, cliff, and Wyrmstooth surfaces, compare what is displayed with Ground Response debug classification and deformation.
4. Winter -> Spring: verify snow tracks clear once, thawed ground stops receiving snow, and wet mud only appears when PIXL's real wetness/weather conditions allow it.
5. Autumn -> Winter: verify newly snowy resolved landscape receives snow deformation without treating exposed roads/rock/worldspaces as snow merely because the context says Winter.
6. Exercise permanent Winter/Summer, custom month mapping, override clear, save/load, new game, fast travel, teleport, interior/exterior, city worldspaces, Solstheim, Wyrmstooth, and an unsupported worldspace.
7. Exit cleanly and inspect `PIXLRenderer.log` for provider status, generation transitions, one-time invalidation, shader/resource errors, or unexpected repeated module discovery.
