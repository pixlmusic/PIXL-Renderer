# Build input review — September 7

This records current-pass reads and verification. It is not a full release completion report.

## Metadata duplication fixed

`cmake/ModuleVersions.h.in` previously defined a static map and four static unordered_sets in a header included through RenderModule/PCH throughout the renderer. These are now inline const variables: one shared immutable set, rather than internal-linkage copies per translation unit. Values, types, lookup keys, serialized configuration and GPU ABI are unchanged.

LOW risk; generated output was regenerated through the existing CMake flow, not edited directly. The resulting PCH/native rebuild and PIXL-Audit passed under the canonical Release preset. DLL size decreased from 19,702,272 to 19,670,016 bytes (32,256 bytes) with this change. Startup allocation reduction is expected from container sharing; no elapsed-time or runtime-memory measurement is claimed.

Latest build log: `build/final-release-records/metadata-sharing-build-20260907.log`. DLL SHA-256: `6B30B0FCB8A02E33D4FEAEBE820EEBD7ECB9F50578C2257E8445F9FB2D70AB50`.

## File-specific findings

| File | Review / role | Findings, decisions and risk | Validation / future possibilities |
| --- | --- | --- | --- |
| CMakeLists.txt | Full source read; dependencies, metadata generation and audit/stage targets | Explicit retired descriptor excluded; CMake permits only source Id characters used by current modules. Version matching warns rather than fails on malformed versions; audit later verifies runtime mapping but could be stricter. Theme-name embedding assumes trusted filenames. | Successful configure/build/audit. Future descriptor parser tests and malformed-version fail-fast check. No broad dependency upgrades. |
| CMakePresets.json | Full; canonical VS2022 Release and separate dev/CI paths | PIXL-12C inherits all-runtime x64 static-MD setup; AUTO_PLUGIN_DEPLOYMENT OFF. Dev-Fast is explicitly nonshipping. Code-analysis vendor settings do not establish a command-line /analyze run. | Current preset used successfully. Do not report static analysis as run merely because editor settings enable it. |
| cmake/XSEPlugin.cmake | Full; native target/flags/CommonLib linkage | C++23, /W4 /WX, /sdl /GS, optimized LTCG, ASLR/DEP. CFG explicitly disabled for dynamic trampoline compatibility. /wd4200 is existing; no warning suppression added. Uses prebuilt CommonLib and Release mapping for Debug. | Built. Future independently verify prebuilt library provenance and alternate runtime variants; no runtime compatibility claim from compile flags alone. |
| cmake/ModuleVersions.h.in | Full; generated metadata containers | Changed static const to inline const to remove per-TU instances. Header depends on existing PCH for map/string_view/REL types. | Full native rebuild; expected startup/memory improvement, measured DLL reduction only. Future constexpr/frozen structures require separate measurements and no rewrite now. |
| cmake/Plugin.h.in | Full; version/name/git description | Trusted git describe embedded into C++ string; future escaping if arbitrary tag naming is supported. No new paths/secrets. | Generated and compiled. Public version remains 1.0.0; git describe may retain historical tag lineage. |
| cmake/ThemePresets.h.in | Full; static theme-name array | Inline constexpr storage appropriate. | Generated/compiled. Test unusual theme filenames before supporting them. |
| cmake/version.rc.in | Full; Windows version resource | FILETYPE is currently application (0x1), although output is a DLL; metadata-only inconsistency, not startup failure. Preserve license notice. | Resource compiled; future set VFT_DLL with release metadata tests. |
| cmake/Streamline/CMakeLists.txt | Full; vendor include interface | Dynamic NGX entry points, not a mandatory NGX import library. Existing /wd5103 applies through interface, not added here. | DLL import list has no NGX import. Vendor redistribution review remains separate. |
| BuildRelease.bat | Full; intended release wrapper | Shipping configure/build error levels checked. Caller working directory assumed to be source root; Dev-Fast auto-discovers VS. Environment variables are not scoped by setlocal. No game deployment with current preset. | Used successfully. Future arbitrary-CWD invocation and environment-isolation tests. |
| vcpkg.json | Full; pinned ports/features | Baseline pinned; overrides preserve current library versions. No standalone vcpkg-configuration.json exists in this root. | Successful cached dependency resolution; no upgrades performed. |
| .gitattributes | Full; source export exclusions | Instructions, internal review ledger and generated trees export-ignored. This does not ignore required source assets from the working build. | Read only. Public source export is deferred until an explicitly approved commit. |
| tools/ExportPixlPublicSource.ps1 | Full; public source archive | Refuses tracked dirty source except the verified FidelityFX patch. Uses committed git archive, not a working-tree snapshot. That refusal is appropriate now; do not commit merely to enable export. | Not executed. Future reparse-ancestor/overwrite safety and archive provenance tests. |
| tools/GeneratePixlReleaseDefaults.ps1 | Full; explicit-baseline preset generator | Does not run during the current release build/stage. It rereads baseline and writes outputs before final integrity checks; supplying an output path as baseline could overwrite it before throwing. Preset constants must be checked against current QualityProfiles before use. | Not executed; no live/default config changed. Future fail-fast overlap guard and transactional writes in isolated fixtures. |
| tools/CompareLiveShaders.ps1 | Full; canonical/live hash reconciliation | Includes retired descriptors/kernels in comparison intentionally as source accounting, unlike staging. Reports overlay collisions and leaves all live files untouched. Source-only Hair entries are not a packaging omission. | Earlier reconciliation: 267 matches, zero differences, two source-only records, nine live-only artifacts. Future explicit shipping/source-only column; keep newer-shader protection. |

## Runtime import boundary

`dumpbin /dependents` confirms D3D11/D3D12, D3DCOMPILER_47 and Windows API sets plus MSVCP140, MSVCP140_ATOMIC_WAIT, VCOMP140, VCRUNTIME140/140_1. The existing D3D12 sidecar is not a conversion of the Skyrim DX11 renderer. Windows/Visual C++ runtime prerequisites must be documented; do not copy arbitrary system DLLs into the player package. Streamline/NGX dependencies are dynamically loaded and require separate package inspection.

## Five release-tool improvements assessed

1. Retired metadata distinction (implemented); 2. shared metadata containers (implemented); 3. explicit RC channel (implemented); 4. manifest integrity/path checks (implemented); 5. package-to-authoritative-source comparison and overlay conflict detection (implemented). All act offline or during startup; no GPU visual equation is changed.

## 5 Future Visual Improvements

1. Shader permutation manifests; 2. captured scene validation; 3. ABI reflection checks; 4. visual preset-drift tests; 5. color/texture format validation.

## 5 Future Performance Improvements

1. Measure startup allocation sharing; 2. incremental offline validation fingerprints; 3. compile matrix equivalence deduplication; 4. inspect dependency build costs; 5. measure packaged asset decode/upload cost.

## 5 Future Feature / Research Ideas

1. Reproducible source exports after approval; 2. transactional preset generation; 3. isolated package negative tests; 4. consistent descriptor schema validation; 5. signed/provenance-verified vendor payload ingestion where publisher terms permit.
