# PGLite compatibility with PIXL Renderer

## Source contract

The supplied PGLite runtime patcher detects `CommunityShaders.dll` or ENB to decide whether it can run. It also disables TruePBR and the hair flow-map patcher unless `CommunityShaders.dll` is loaded. With PIXL alone, PGLite therefore disables itself or removes its TruePBR option before any mesh reaches MaterialForge.

PGLite's TruePBR output uses `SLSF2_UNUSED01` (second shader-flag word, bit 23). MaterialForge tests the corresponding combined `kMenuScreen` bit 55 during `BSLightingShaderProperty::LoadBinary` and constructs its PBR material. PGLite writes `_rmaos` to texture slot 5, emissive to 2, height to 3, and feature maps to 6/7. These are the slots MaterialForge's `BSLightingShaderMaterialPBR` consumes. PGLite's hair flow map goes to backlight slot 7 with the backlighting flag; PIXL Lighting samples that slot for hair when Strand Shading is active. Complex Material and parallax patchers are independent of the TruePBR framework gate.

MaterialForge already consumes the mesh flag and texture layout PGLite writes. No MaterialForge shader, CPU/GPU ABI, or texture binding change is needed for that contract. The required adapter is PGLite's framework detection.

## Local adapter

The source under `_REFERENCE/PGPatcher-main/PGPatcher-main/PGLite` has been adapted to recognize `PIXLRenderer.dll` as a PBR renderer, keep TruePBR and hair flow-map options available with PIXL alone, display PIXL in its status, and log the detection. ENB-only Fix Mesh Lighting remains ENB-gated. The reference tree is ignored by PIXL's Git rules, so [`compatibility/PGLite-PIXLRenderer.patch`](../../compatibility/PGLite-PIXLRenderer.patch) is the portable change. Apply it to an unmodified copy of the supplied PGPatcher source from the PIXL repository root with `git apply compatibility/PGLite-PIXLRenderer.patch`. From a standalone PGPatcher source root, use `git apply -p4 <path-to-PIXL-repository>/compatibility/PGLite-PIXLRenderer.patch`.

The patch passes `git apply --check` against a reconstructed pristine copy and `git apply --reverse --check` against the edited reference source. This verifies patch structure and exact source integration.

## Built compatibility option

The original PGLite runtime DLL does not recognize a PIXL-only installation.
The optional **PIXL-aware PGLite runtime** FOMOD choice installs a locally built
PGLite DLL that detects `PIXLRenderer.dll` directly. It installs the cubemap
PGLite expects and leaves an existing `PGLite.ini` untouched. A sample INI and
the corresponding patched source, dependency source, license, build notes, and
patch are retained in the FOMOD archive. If an original PGLite mod is installed,
the PIXL-aware DLL must win the DLL file conflict in the mod manager. The guide
choice remains available for users who want the patch without the DLL.

The DLL was compiled in Release mode against PGPatcher's pinned nifly and
CommonLibSSE-NG revisions with the PGLite vcpkg dependencies. Its SKSE exports
and Windows DLL dependencies were checked. The FOMOD manifest, install choices,
and archive integrity passed automated checks. This build is a local
compatibility variant, not an official upstream PGLite release. Direct PIXL
detection is preferable to exposing PIXL globally as Community Shaders, which
could lead unrelated mods to assume APIs and behavior PIXL does not provide.

## In-game validation

The patched DLL was placed in the active Steam Skyrim Special Edition
installation for a live test; the previous PGLite DLL was backed up. The
November 11 live log reports `Community Shaders no, PIXL Renderer yes, ENB no`
and `PGLite is ready`. It also enters the TruePBR config loading path, which is
guarded by the TruePBR enable setting. PIXL reaches its DataLoaded finalization
in the same run. This confirms runtime detection and startup together in this
Steam setup. The log found **zero TruePBR configs and entries**, so it does not
demonstrate the appearance of a patched TruePBR mesh. The owner confirmed that
the F8 menu opened and worked. A representative mesh with RMAOS, height, and
optional hair flow map still needs visual validation before claiming complete
compatibility across game versions and mod setups.
