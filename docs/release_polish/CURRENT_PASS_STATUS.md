# September 7 release-polish continuation

September 12 superseding package status: the owner has requested final RELEASE
packaging and source push. See `FINAL_RELEASE_20260912.md` for the current verified
build, scoped tests and compatible 3475-stage cache snapshot. The public package
excludes UserGraphics. Historical no-cache/no-publication statements below describe
September 7, not the current artifact. Outstanding exhaustive-review and runtime
limitations remain explicitly documented rather than represented as completed.

Status: **release candidate built and deployed for owner testing; no commit/push/tag/publication**.

This file supersedes unsupported completion claims in earlier continuation notes. The inherited September 3 matrix has 586 entries; reading hashes, scanning patterns, or summarizing a module is not a semantic review of every file. The required exhaustive file review, five substantiated investigations per module, UI control tracing and runtime shader permutation coverage remain open.

The matrix has an explicit September 7 scope column and now includes the landscape settings/shader tools plus their source rows. Current row count is 689; pending rows remain and are not represented as completed runtime review. Full source review does not mean runtime/visual validation passed.

## Verified build and authority evidence

- Canonical workspace remains the Skyrim SKSE/DX11 source, not the Fallout port or a historical reference checkout.
- `BuildRelease.bat PIXL-12C PIXL-12C` completed successfully after CMake's clean target. Visual Studio 2022/MSVC 14.44, Release/LTCG; FidelityFX shader generation and native link succeeded. Log: `build/final-release-records/final-build-20260907.log`.
- Clean-build checkpoint output was 19,703,808 bytes. Subsequent targeted fixes were rebuilt successfully; latest packaged build log: `build/final-release-records/blur-guard-build-20260907.log`, exit 0. Packaged DLL: `build/PIXL-12C/Release/PIXLRenderer.dll`, 19,678,720 bytes, SHA-256 `31A743002192C9882A6415D6066C2578B73D69921B08A0264E0C4685583FDA85`. CMake stderr is wrapped as `NativeCommandError` by PowerShell; that wrapper is not a compiler error.
- This build compiles FidelityFX permutations, NOT every dynamically compiled PIXL HLSL permutation. PIXL shader validation is a separate pending gate.
- Before shader edits: 267 shared files byte-identical, zero differences; 2 source-only retired Hair records, 9 live-only vendor/manager/archive artifacts. After the documented grass linear-light fix: 266 matches and one intentional RunGrass difference, same source/live-only counts, no overlay collisions. Evidence: `build/release-polish-current/shader-reconciliation-rc04`. No newer authored live shader was overwritten.
- Runtime registration excludes Hair Reconstruction while retaining its ABI reservation. Generated module metadata now excludes the retired descriptor: 37 shipping modules, one retained source record. Retired kernels remain excluded from the dated RC.

## Changes in this pass

1. CMake excludes retired descriptors from generated active metadata.
2. Package audit checks the exact shipping descriptor set and packaged literal include availability, with one narrowly guarded retired Hair exception. This is not general preprocessor analysis.
3. Staging accepts an explicit release channel, without changing its default LIVE-TEST behavior.
4. New manifest verification checks hashes, sizes, exact payload coverage, canonical paths and no reparse points. Both stage and audit invoke it. Fourteen fixture tests pass, including corruption and traversal failures.
5. `.gitignore` now covers loose compiler intermediates, shader caches, captures, logs, dumps and temporary archives; required vendor DLLs/libraries are not blanket-ignored.
6. Report provenance corrected: inherited reviews and old game logs do not establish new validation.
7. Fixed custom shader macro capacity overflow and bare-definition value leakage. Exact-source native regression tests pass, including 1000 custom definitions.
8. Bounded tracked shader include reads, rejected shader-root escapes, initialized failure outputs and checked callback arguments. Exact-source native tests pass.
9. Fixed hot-reload dependency loss across permutations by retaining their combined include edges. Tests pass with 32 concurrent writers.
10. Fixed Water Optics UI scope: Reflection Balance and Water Tint remain independently editable with enhanced SSR off. No shader equations changed.
11. Shared generated immutable module containers across translation units with C++ inline variables. The rebuilt DLL is 32,256 bytes smaller than the preceding checkpoint; startup time and runtime memory have not been measured.
12. Package audit now traces the actual runtime registry through global instances to module IDs and rejects registration/catalog mismatches; header existence alone is not treated as registration.
13. RCAS now reports missing resources and its caller copies reconstructed output unchanged on failure; warning moved out of the frame path, numeric inputs bounded. Exact-source control-flow and 20 real D3D11 WARP shader tests pass, including reflected ABI/bindings.
14. Foliage legacy reload resets missing tuning extensions; tree normal-Y control stays independent of enhanced lighting. Grass now honors linear point-light flags. Exact-source settings tests and 32 strict grass VS/PS compile cases pass.
15. NaturalLighting engine light overlay now has size/alignment/offset assertions. Thin Surface bounds invalid settings, defaults unknown modes to disabled, asserts its 16-byte ABI and labels its inverse opacity blend correctly. Settings tests and 32 selected material VS/PS cases pass.
16. Removed proven uninstantiated StructuredBuffer wrapper and its two descriptor helpers, preserving active Buffer/ConstantBuffer/Texture wrappers and all HLSL StructuredBuffer types. Full affected rebuild passed. See BUFFER_REVIEW_20260907.md; no runtime speedup claimed for dead-code removal.
17. Package audit compares source-backed defaults, baseline preset, theme, English text and notices. RC-04 correctly failed after its DLL/localization became stale; RC-05 and RC-06 passed source checks.
18. Font/theme fixes: retain locale glyph ranges for atlas lifetime, guard ImGui context before GetIO, skip missing optional Windows fonts, correct legacy role labels/ExtraBold ranking/byte-safe extension parsing and detect failed theme flush. Exact-source tests plus 20 real ImGui atlas builds pass. See FONT_THEME_REVIEW_20260907.md; full UI/runtime fault testing remains open.
19. Diagnostic cleanup now rejects invalid module IDs, mismatched/root/redirected paths and validates separate JSON override ownership. Issue erasure is deferred until row pointers are finished. Workshop restores the correct version section and retains failed recovery metadata. Generic deletion catches probe errors; filename sanitation handles reserved device basenames with extensions. Exact-source recorded-action tests pass; no live cleanup executed. See DIAGNOSTIC_FILESYSTEM_REVIEW_20260907.md.
20. Background blur skips fully off-screen/degenerate windows and asserts both 32-byte buffer layouts. Seven strict shader entry checks and exact clipped-area tests pass. Unresolved state/lifetime concerns are documented in BACKGROUND_BLUR_REVIEW_20260907.md; no unvalidated color-space/filter redesign.
21. Landscape settings reload now applies finite/range validation and ABI assertions; Terrain Detail and Horizon Blend lifecycle state is canonicalized.
22. Lighting now declares Terrain Detail stochastic offsets when Terrain Detail runs without Material Layers. Selected landscape/water coverage passes 72 strict FXC VS/PS cases, including Material Layers combinations. See LANDSCAPE_REVIEW_20260907.md.

FXC /WX /Ges /O3 passed all 588 explicit Hybrid GI/Atmosphere cases. This is not full renderer shader-permutation coverage; see `COMPILER_AND_PACKAGING_REVIEW_20260907.md`.

No motion-vector generation, GPU ABI, hook addresses or live user configurations have changed. The grass light-conversion flag is a controlled rendering correction; the rest of the new visual equations remain unchanged. Expected visual benefits are documented, not claimed as measured/captured. No GPU timing improvement is claimed.

## Staged artifact, not a completion claim

- Latest package integrity/source/catalog checkpoint: `dist/PIXL-Renderer-v1.0-RC-20260907-10`. Includes the landscape fixes above. Full audit passed after the final build. This is NOT certification of complete engineering review.
- Archive checkpoint: `dist/PIXL-Renderer-v1.0-RC-20260907-08.zip`, 217,775,374 bytes; separate 7-Zip readback test passed (`build/final-release-records/rc08-archive-test-20260907.log`). Uncompressed payload plus manifest: 361,744,043 bytes.
- Archive SHA-256: `A760147B949744DC65F40920E1BF78DAAEC7C9BE405B1D26B43D036623AE34BB`.
- The RC-10 archive is 217,758,748 bytes; the packaged/deployed DLL is 19,679,232 bytes, SHA-256 `E277E4E86C2DC09F93D47D137B38062877B4C5751439216B9B7251D4AFDF7290`. Previous live DLL backup: `Data/SKSE/Plugins/PIXLRenderer.dll.preRC10_20260907_194112`.
- 315 payload files plus manifest, 37 module INIs, compile-on-device cache mode. Full payload manifest passed before archiving.
- The earlier undated RC is superseded for testing; retained for recoverability, not deployed. Known-good desktop beta and live Skyrim DLL/configuration remain untouched.
- RC-07 archive readback passed, but its later audit overlapped the blur rebuild and could not read the locked DLL; it then reported the DLL mismatch gate. RC-08 was staged/audited after build completion. Do not treat that RC-07 failed audit as a successful provenance check.
- Local prerequisite check: SkyrimSE.exe 1.5.97.0, skse64_1_5_97.dll, version-1-5-97-0.bin and EngineFixes.dll present; no conflicting DLL from XSEPlugin.cpp's named list was found. No Skyrim process was running when checked. This is not a successful startup test of the rebuilt DLL.

## Outstanding gates

- Complete current-pass file-by-file and module review with actual source evidence, including newly added tools.
- Complete runtime HLSL compile coverage, UI wiring trace, resource/pipeline review and measured performance testing where available.
- Resolve or explicitly disposition the HIGH-risk private neural-runtime identity workaround described in `SECURITY_AUDIT.md`. Do not expand it or silently disable a working feature.
- Finish package/source provenance checks and public redistribution review for vendor binaries. A self-consistent manifest is not an authenticity check.
- Build/audit after final source changes, archive verification, recoverable PIXL-only deployment, then in-game owner testing. No load-order changes.
