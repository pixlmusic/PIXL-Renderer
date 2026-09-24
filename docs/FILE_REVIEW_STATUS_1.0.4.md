# PIXL Renderer 1.0.4 source review status

## What the existing matrix proves

`docs/release_polish/FILE_REVIEW_MATRIX.md` enumerates 735 PIXL-owned build/runtime candidates, including one retired module file. `tools/WriteReleasePolishInventory.ps1` loads each text file and searches for include statements and risk patterns. This is an **automated inventory**, not the requested per-file engineering review. Its `Semantic review` column therefore says `Not verified in this matrix` for every row.

The `engine`, `pipeline`, and `distribution` roots currently contain 107 `.cpp`, 126 `.h`, 110 `.hlsl`, and 78 `.hlsli` files: 421 code files in total. Counting them does not establish that their callers, lifecycle, shader interfaces, visual behavior, or performance have been examined. Prior scoped reports contain some genuine module investigations, but no complete, current evidence-backed sign-off exists for all 421 code files.

## Validation that has actually run

- The 1.0.4 Release DLL builds and the integrated PIXL audit passes with 37 shipping modules and one retired source-only module.
- Strict FXC tests pass 733 **selected cases** across material, grass, landscape/water, WindowLife, Hybrid GI, and atmosphere paths. A case is a permutation, not a distinct shader file. This is not full compilation of every shipped shader/permutation.
- The package audit validates module registration, stage-source overlays, literal shader includes, required assets, and staged source hashes. It does not establish numerical or resource-binding correctness at runtime.
- This pass traced `ShaderCache.cpp` to the staging script and found that preloaded packages checked layout and shared ABI but omitted the new shader revision. Staging now checks all three against the runtime constants; the older installed library without `ShaderRevision` was rejected in a packaging test.
- WindowLife's 256-byte CPU/HLSL per-draw payload was checked against its static assertion and sixteen `float4` fields; stale 240-byte diagnostic text was corrected. Live binding remains untested.
- A targeted Director lifecycle trace found that Video-to-Photo mode switching left the Video path in `Playing` state. The switch now stops playback while retaining the path and scrub position; the changed C++ built successfully, but the transition still needs live camera/input testing.

## Open release gate

The requested exhaustive C++/HLSL review is **IN PROGRESS**. To close it, each active file needs evidence of its build/runtime role, direct dependencies and consumers, correctness and lifecycle analysis, shader register/constant parity where applicable, actual findings or an explicit retain decision, and validation. A generated pattern scan must not be used as a substitute. The 1.0.4 packages are live-test candidates, not a certified final public release.

The next review order is: shader compiler/cache and package graph; MaterialForge and lighting; Ground Response with grass collision; WindowLife; Photo/Video camera ownership and capture; then the remaining modules and shared utilities. Prior scoped reports may be linked as evidence only after their source revision and coverage have been checked against the current tree.
