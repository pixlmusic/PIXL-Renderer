# Compiler and packaging: current-pass evidence

Scope is the paths/functions listed below, not an assertion that all renderer files are reviewed. Source remains authoritative; these tests do not launch Skyrim.

## Shader compiler/configuration investigations

Pipeline: advanced UI/config JSON -> State::SetDefines -> ShaderCache/Util::CompileShader -> D3DCompileFromFile -> D3D11 shader creation/cache. No GPU constant structure or resource register changed.

1. **Custom macro capacity (MEDIUM, fixed).** The compiler appended arbitrary custom definitions into a fixed 64-entry array before engine/module definitions. Long input could corrupt stack memory and even short input reduced capacity available to the engine callback. Engine definitions now get their own unchanged 64-entry contract; stage/developer/custom/engine macros are assembled into a reserved vector in their original order, followed by one terminator. Expected cost: one allocation per actual shader compilation, not per draw. No intended visual change. Engine-side maximum-definition and concurrent settings mutation assumptions still merit separate review.
2. **Parser value leakage (LOW, fixed).** `A=7;B` gave B the previous replacement value. Clear the replacement for each valid token. Existing invalid-token handling and saved format remain intact. Advanced configurations relying on that unintended behavior change; normal empty defaults do not. Exact-function tests cover bare/empty-valued/malformed tokens and reset.
3. **Tracked include path escape (MEDIUM, fixed).** TrackingIncludeHandler previously accepted ../ or absolute paths outside its shader root, unlike the separate module compiler. Canonicalize root/target and reject escaping relative paths. Valid nested/rooted-within-tree includes remain accepted. This deliberately rejects external includes from custom shaders; shipped include paths stay within the assembled tree. No network/file-write behavior added.
4. **Include allocation and callback robustness (LOW, fixed).** Reject null API arguments, initialize output pointers/counts before failure, and cap include reads at 16 MiB, matching the existing module compiler. Prevents oversized allocation and UINT truncation. Existing include-buffer ownership/lifetime is preserved. A pre-existing implicit tolower narrowing discovered under test /W4 /WX was made explicit without changing its byte behavior.
5. **Compile validation/caching (investigated, deferred).** Both compiler paths skip D3D bytecode validation when disk caching is enabled. A disk-cache setting does not itself prove source correctness. Changing startup compile policy requires separate compile-time/regression measurements and full cache review. No flag suppression or cache ABI change was made here. The offline FXC tests retain validation and treat warnings as errors.

Validation: Release build/audit passed after macro changes and again after include changes. Native tests extract and compile the exact changed source with MSVC /W4 /WX. Parser tests plus 30 assembly cases pass (3 stages x 2 developer modes x 5 input sizes, up to 1000 custom definitions). Include tests pass for nested/normalized paths, traversal, absolute outside paths, oversized/missing/empty files, null arguments and retained buffer lifetime. Reparse-point behavior and live concurrent settings edits remain untested.

## Shader compile evidence

Additional confirmed fix: `engine/ShaderFileWatcher.h` previously replaced an entry file's dependency edges on every permutation compilation. Guarded includes used only by an earlier permutation were lost. Registration now merges the union under the existing mutex; explicit unregister/clear removes edges. LOW-to-MEDIUM risk, no normal image change; after source edits an obsolete edge may conservatively trigger an extra recompile. `tools/TestPixlShaderDependencies.ps1` compiles the actual tracker and normalization function and passes union/empty/shared-edge/removal/clear checks plus 32 concurrent writers. Neither file adds filesystem writes or networking to the renderer. Both were read in full. Future: permutation-keyed dependency sets if pruning costs become material; disk-loaded dependency reconstruction still needs review.

`TestPixlComputeShaders.ps1` compiles 588 cases using SDK 10.0.26100 x64 FXC, cs_5_0/main, /O3 /Ges /WX, validation enabled. All passed. It follows the current HybridGI::CompileComputeShaders table: 12 programs x 3 resolution modes x 16 combinations of temporal/GI/reflection/adaptive switches = 576. Atmosphere adds 8 scattering, 2 material and 2 depth/integration cases. Logs and per-case source hashes: `build/compute-validation-64109fec2a984d5f8a4875974f6ac40f/results.csv`.

This does not cover engine-provided permutations, user-defined macros, every other module, live D3D bindings, shader math or visual quality. HLSLI dependencies are compiled through these entry points, not falsely counted as separate entry programs.

## File-by-file current review boundaries

| File | Purpose / dependencies | Current review | Findings / change | Visual / performance impact | Remaining work |
| --- | --- | --- | --- | --- | --- |
| engine/State.cpp | Configuration parsing and renderer state; pystring, UI, compiler | Partial, parser and callsites | Bare-define leak fixed | Normal defaults unchanged; trivial parse-time work | Remaining state/lifecycle/settings semantics, concurrent mutation |
| engine/ShaderCache.cpp | Engine permutations, include tracking, disk cache, compilation | Partial, changed paths and callers | Capacity, include boundary, input-size fixes | No normal image change; one compilation-only allocation; bounded include reads | Entire cache lifecycle, fixed engine macro count, races, reflection and all descriptors |
| tools/AuditPixlRenderer.ps1 | Build/package graph validation; source, descriptors, manifest helper | Full script read, changed paths reviewed | Exact shipping catalog, guarded retired exception, payload/source hashes, overlay conflicts, actual runtime registry traced through global instances to header IDs | No game cost; added offline hashing | Dedicated negative tests for every graph failure; extraction fails closed if registry syntax changes |
| tools/StagePixlRendererStandalone.ps1 | Assembles binary package, manifest/archive; source and build outputs | Full script read | Explicit channel; verification before archive; retired kernels stay excluded | No game cost | Preflight every optional asset before output replacement; reparse-ancestor safety in output cleanup |
| tools/VerifyPixlPackageManifest.ps1 | Read-only payload verification; JSON and SHA-256 | Full, new | Exact set, size/hash, duplicate/path/link rejection | Offline hashing only | Add junction/symlink fixtures and schema evolution tests; hashes do not establish trust |
| tools/TestPixlPackageManifest.ps1 | Synthetic package regression fixtures | Full, new | 14 positive/negative cases pass | Build-only, tiny retained fixtures | Add reparse-point and alternate casing cases |
| tools/TestPixlComputeShaders.ps1 | Explicit HybridGI/Atmosphere FXC matrix | Full, new | 588 passing compile cases, logs and source hashes | Offline compiler cost, no deployment | Expand to every shipping module and engine permutation; test registry drift |
| tools/TestPixlShaderDefines.ps1 | Exact-source parser/assembly test extraction; MSVC, pystring | Full, new | Regression inputs and 30 capacity/order cases pass | Build-only | Prefer integrated native test target later; test concurrent settings snapshots separately |
| tools/TestPixlShaderIncludes.ps1 | Exact-source include handler tests; MSVC/SDK | Full, new | Boundary, size, API and lifetime cases pass | Build-only; 17 MiB synthetic oversize fixture | Junction/cross-volume tests; retain failure fixtures only while diagnosing |
| cmake/FidelityFX-SDK.cmake | Pinned vendor configure patch and linkage | Full | Existing patch reapplied by configure; reverse-check passes | No runtime change | Build patch portability, do not hand-edit vendor files |
| cmake/AddCXXFiles.cmake | Recursive native/header/shader source accounting | Full | HLSL registered as IDE inputs, not FXC build tasks | No change | Ensure validation inventories distinguish compilation from file registration |
| CMakeLists.txt | Target/dependencies/generated module metadata | Retired metadata portion rechecked; broader baseline review | Exclude explicitly retired descriptor from active metadata | No shader ABI change | Continued whole-build manifest/reproducibility review |
| .gitignore | Public source hygiene | Full | Loose intermediates/logs/caches/captures/archives ignored | No runtime effect | Verify future required source archives are not unintentionally hidden |

All new tooling is offline, uses existing compiler/build dependencies, writes generated tests only beneath unique build directories, and does not modify Skyrim, load order, user configs or remotes. Synthetic outputs are intentionally retained for debugging and ignored by Git. No claim of full-repository cleanup is made.

Additional package-source gate: AuditPixlRenderer now also hashes source-backed defaults, GoldenBaseline preset, shipped theme, English localization and four notice/license files against their mapped package copies. UserGraphics is intentionally not compared to source defaults. The old RC-04 passed its internal manifest but correctly failed the expanded audit for stale DLL and English localization after newer source changes (build/final-release-records/rc04-stale-negative-audit-20260907.log). RC-05 passed all gates, including this source comparison and archive integrity. Current artifact details are tracked in CURRENT_PASS_STATUS.md; internal manifest validity alone does not prove current source provenance.

## Five future visual improvements

1. Scene-tagged shader validation captures; 2. material-permutation image tests; 3. temporal sequence comparisons; 4. numeric stress tests for shared math; 5. HDR reference probes. These are validation improvements, not new post effects.

## Five future performance improvements

1. Measure validation cost before changing compiler flags; 2. measure macro allocations/cache misses; 3. immutable shared configuration snapshots; 4. deduplicate equivalent offline compile cases; 5. cache validation evidence by complete include/config/compiler fingerprint.

## Five future feature / research ideas

1. Integrated native tests; 2. offline engine permutation manifests; 3. include-graph coverage output; 4. deterministic package provenance; 5. resize/loading/camera-cut regression automation. None justifies an unvalidated renderer rewrite.
