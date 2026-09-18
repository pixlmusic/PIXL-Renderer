# PIXL Renderer 1.0.2 / SurfaceTides upstream handoff

## Current result

Owner confirmed visible SurfaceTides displacement with PIXL enabled on Steam
Skyrim 1.6.1170 on 2026-09-17. The runtime reported
`PIXL water draw handshake V1 matched live replacement shaders`, tessellated
draws with `failed=0`, and sampled `changeMask=00`. This is a successful local
integration test, not exhaustive certification of every water type or runtime.

## Files supplied

The developer package contains the modified SurfaceTides source archive, the
tested SurfaceTides compatibility patch, PIXL's Hooks.cpp and Water.hlsl as
integration references, and this document. The main PIXL 1.0.2 release supplies
the matching host DLL. Source is a working-tree snapshot; no new commit exists.
No clean original SurfaceTides checkout was available for a trustworthy complete
unified diff. Compare the supplied files against your own 1.0.2 baseline.

## Why the earlier bridge failed

PIXL binds its own cached shader objects, outside Skyrim's original shader maps.
Indexing/tagging only the original objects leaves every PIXL water draw unmatched.
The simulation can be healthy while its displacement never reaches a water draw.
Later successful binds during a PIXL-off comparison do not establish coexistence.

## Host API contract

Resolve `PIXL_QueryWaterDrawV1` from the already loaded `PIXLRenderer.dll` using
GetModuleHandleW/GetProcAddress; do not load another renderer instance.

Treat the export as an optional capability rather than checking a PIXL product
version. If PIXL is loaded but the V1 export is absent, disable only SurfaceTides
and retain PIXL's normal water. Validate every successful response against the
currently bound VS/PS objects and the known V1 descriptor range before borrowing
its records. Reject an unknown future layout per draw and leave that draw on
PIXL's path. PIXL 1.0.2 and later keep V1 as the backward-compatible contract;
an incompatible future ABI must use a new export name instead of changing V1.

```cpp
using QueryWaterDrawV1 = bool (*)(
    ID3D11VertexShader* boundVS,
    ID3D11PixelShader* boundPS,
    RE::BSGraphics::VertexShader** outVS,
    RE::BSGraphics::PixelShader** outPS,
    bool* waterbody);
```

Windows x64 MSVC/CommonLibSSE ABI, synchronous render-thread call. Query the
actual immediate-context shaders immediately before drawing. A true result
means PIXL is enabled, water is enabled, its current shader class is Water, and
both actual shader objects match the active PIXL permutation. The output records
are borrowed for this draw only: never release or retain their pointers. Use their
IDs and constant tables immediately. `waterbody` indicates PIXL Waterbody's active
shader contract. Outputs are only usable on true. The host resets the two record
outputs on failure; the flag need not be read on failure. This API uses CommonLib
records, so coordinate ABI changes with PIXL and introduce a new export version
if this contract changes. A future plain-data ABI would reduce that dependency.

## SurfaceTides changes to merge

- `src/plugin/Bridge.hpp`: AllowPIXL configuration, pixlActive, coexistence checks.
- `src/plugin/Game.cpp`: parse AllowPIXL.
- `src/plugin/Main.cpp`: detect PIXL and require opt-in; select coexistence mode.
- `src/plugin/Renderer.hpp` and `.cpp`: resolve the host API; validate actual
  bound water; compile against returned constant tables; separate native/PIXL
  and Waterbody cache keys. Bind SurfaceTides VS/HS/DS and patch topology while
  preserving PIXL PS, PS constants, textures and samplers. Restore touched state.
  Use the original SurfaceTides path for positively identified native water.
- `Data/Shaders/SurfaceTides/Water.hlsl`: ST_PIXL_GEOMETRY and ST_PIXL_WATERBODY
  variants match PIXL output semantics and Waterbody scaled/wading flow UVs.
  Keep original-source PS reflection separate from the geometry-only variant.
- `Data/SKSE/Plugins/SurfaceTides.ini`: document AllowPIXL; source default is 0.
  The 1.0.2 PIXL bridge also accepts optional
  `PIXLCityDisplacementScale=0.55` and
  `PIXLInteriorDisplacementScale=0.25` keys in `[Compatibility]`. Missing keys
  use those compiled defaults, so the compatibility overlay does not need to
  replace a user's INI.

## Contextual displacement

The bridge keeps the configured SurfaceTides displacement and normal strength at
100% in wilderness water. While PIXL coexistence is active it scales both to 55%
for locations carrying `LocTypeCity`, `LocTypeTown` or `LocTypeSettlement`, and
to 25% in interior cells. Location keywords are used instead of a fixed city
worldspace list so city overhauls and custom settlements can participate.

Only rendering and floe-follow strength are scaled; the simulation continues and
tessellation density is unchanged. Missing or unclassified location metadata
fails to full wilderness strength rather than unexpectedly flattening water.
- `tests/ShaderTests.cpp`: geometry stages tested with and without Waterbody.

The post-draw probe now captures the expected PS t96 binding instead of assuming
SurfaceTides owns it. A 0x20 result from the older probe was a false mismatch.
PIXL's Water.hlsl composes its normal with the rasterized geometric normal,
so Fresnel/specular/refraction orientation follows displacement. No SurfaceTides
pixel textures are imposed on PIXL's resource slots. ST uses b11, t96/t97 and s15
on its geometry stages; native VS constants are copied to HS/DS.

## Build and validation

From SurfaceTides source: `cmake --preset windows-release`, then
`cmake --build --preset windows-release --parallel`, then
`ctest --preset windows-release --output-on-failure`.
Both DLLs built; PIXL integrated audit passed; all four SurfaceTides tests passed,
including expanded SM5 shader permutations. PIXL baseline and Waterbody river
pixel shaders compiled with FXC. Existing signed/unsigned warnings remain.

SurfaceTides now builds against CommonLibSSE-NG 8.1.0 with both SE and AE layouts
enabled. One DLL exports `SKSEPlugin_Query`, `SKSEPlugin_Version` and
`SKSEPlugin_Load`, and explicitly accepts Skyrim 1.5.97, Steam 1.6.1170, GOG
1.6.1179 and 1.7.104. Each game still needs its matching SKSE and Address Library;
1.7.104 requires the format-5-capable Address Library data. Automated build and
shader tests validate the shared artifact but do not replace a live test on each
runtime.

Before an upstream release, check missing/disabled PIXL and missing export,
Waterbody off/on, native SurfaceTides fallback, rivers/lakes/interiors, shoreline
depth/refraction, underwater, additive lights, cell changes, and TAA/DLSS/FG motion.
Build tests alone do not prove correct images, temporal behavior or performance.
Keep diagnostics optional. Consider disabling repetitive LogStats for public use.

## Immediate user distribution

1. Install the full supported SurfaceTides 1.0.2 mod first.
2. Install or reinstall PIXL and select **SurfaceTides 1.0.2 Integration**. The bundled
   option replaces `SKSE/Plugins/SurfaceTides.dll`,
   `Shaders/SurfaceTides/Water.hlsl` and `SKSE/Plugins/SurfaceTides.ini`.
3. Back up custom SurfaceTides tuning first. The PIXL preset enables
   `[Compatibility] AllowPIXL=1`, strengthens wilderness wave excitation and
   retention, and supplies the contextual city/interior scales.
4. Let PIXL win all three conflicts, close Skyrim, and restart via SKSE.

Do not install a runtime-specific upstream SurfaceTides DLL afterward. The PIXL
replacement is already universal across the four supported runtimes. In Vortex,
set PIXL after SurfaceTides; in MO2, put PIXL lower in the left pane. Reinstall
the PIXL integration after any SurfaceTides reinstall or update.

The integration is not a standalone SurfaceTides installation. Without the
original mod, PIXL keeps its regular water. PIXL does not install SurfaceTides.

## Recommended permanent upstream integration

The SurfaceTides owner should merge the supplied source into their current branch,
not redistribute the supplied 1.0.2 DLL in future releases. Keep
`PIXL_QueryWaterDrawV1` as an optional capability resolved from the already loaded
PIXL module. Do not hard-code a PIXL product version and do not load PIXL manually.

Add a `PIXL Renderer` installer choice that writes `AllowPIXL=1` while preserving
the rest of the selected SurfaceTides configuration. The official SurfaceTides DLL
and `Shaders/SurfaceTides/Water.hlsl` should then contain the bridge, eliminating
the separate compatibility patch. Build that DLL from each new SurfaceTides release
so users receive all upstream fixes. If the V1 capability is absent or a returned
draw fails validation, SurfaceTides must leave the draw on PIXL's normal path.

For each future release, run the existing solver, floe, water-selection and SM5
shader tests, then test one river, one lake, underwater, a cell transition and
PIXL Waterbody both enabled and disabled. An ABI-breaking PIXL integration must
use a new export name; never silently change the V1 contract.
Publish the modified SurfaceTides source alongside the binary patch and retain its
LICENSE/THIRD_PARTY notices. The source archive and patch in this handoff correspond.
Once upstream implements this contract, users can replace the compatibility patch
with that supported upstream release. Do not install both competing DLLs.

This package is prepared for sharing; it has not been sent to the developer.
