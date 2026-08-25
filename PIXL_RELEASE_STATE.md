# PIXL Renderer Release Preparation State

> Persistent engineering ledger for the autonomous release-preparation pass.
>
> Update this file after every major phase and whenever a significant blocker, build result, shader result, provenance decision, or cleanup decision occurs.

## Overall Status

**RELEASE PREPARATION COMPLETE TO THE MAXIMUM AUTOMATED EXTENT**

## Current Phase

Complete. All autonomous engineering phases and exit gates available in this source/build/runtime environment have passed. No external publication was performed. Remaining work is limited to owner-authored quality-preview images and publisher/legal review of NVIDIA redistribution obligations.

## Session Recovery Instructions

If this work is being resumed after context compaction, agent restart, or a new Codex session:

1. Read `AGENTS.md`.
2. Read `PIXL_RELEASE_PREP_TASK.md`.
3. Read this file completely.
4. Inspect current Git/worktree state.
5. Continue from `Next Actions` rather than restarting blindly.

---

# Known Working Baseline

## Source

- Canonical source: `H:\The Elder Scrolls - Skyrim - Special Edition\PBRPipeline\PIXL-Renderer-Engine`
- Branch: `pixl-pbr-skyrim`
- Commit: `dcea14b61396fd5544aa5f0ad2165c24aa0d8816`
- Initial describe: `v1.8.0-13-gdcea14b6-dirty`
- Initial Git status: heavily modified owner worktree, preserved as the functional source baseline. There were 1,675 porcelain entries: 230 unstaged deletions, 12 unstaged modifications, 966 untracked entries, 96 staged deletions, 46 staged renames, 278 staged-renamed/then-worktree-deleted paths, and 47 staged-renamed/then-worktree-modified paths. The tracked diff against HEAD covered 711 paths (6,578 insertions and 133,675 deletions); the staged diff covered 467 paths (30,321 deletions).
- Preservation rule: do not overwrite, reset, or reconstruct this tree from HEAD/upstream. The current working tree is the PIXL baseline.
- Working-tree preservation: full binary Git diff plus all untracked owner files were copied outside the repository before cleanup. The binary patch SHA-256 is `AC2AFA6173B78CF45E99784F2FECE62C13A4CEFAF8A1386369BC1C3E01A8B857`.
- P: verification: `P:\` was already mapped by `subst` to the canonical H: repository. It was not changed.

## Release DLL

- Expected build directory: `build\PIXL-12C`
- DLL filename: `build\PIXL-12C\Release\PIXLRenderer.dll`
- Baseline DLL size: 18,446,848 bytes
- Baseline DLL timestamp: `2026-08-24T02:51:41.4937628+10:00`
- Baseline DLL SHA-256: `21BC6522F48BD23C999F0FCB70D50619FBD0BEA334456FE96F8EDD1E8C4FCA2B`
- Preserved baseline copy: `H:\The Elder Scrolls - Skyrim - Special Edition\PBRPipeline\PIXL-Renderer-ReleasePrep-Baseline-20260824\artifacts\PIXLRenderer-baseline-21BC6522.dll`
- Live deployment check: `Data\SKSE\Plugins\PIXLRenderer.dll` matched the build baseline byte-for-byte and by SHA-256.
- Existing staging DLL: 18,415,616 bytes, timestamp `2026-08-24T00:07:43.5259129+10:00`, SHA-256 `9BB6A474A360F2088DD97BA3C73E95F63BE0037BEE6A8DEE1CF6A0578CA9A1CC`; it is older than the functional baseline and must not be treated as final.

## Live shaders

- Source of truth: `H:\The Elder Scrolls - Skyrim - Special Edition\Data\Shaders`
- Initial shader inventory: 346 files / 108,052,064 bytes. Active source types include 112 `.hlsl`, 73 `.hlsli`, and 36 `.ini`; the same tree also contained 32 `.bak`, 28 `.vortex_backup`, one `.13AU_backup_*`, one `.pre13M`, eight runtime DLLs, and other packaged assets. Backup-like files are cleanup candidates, not source-of-truth selections.
- Initial shader hash/inventory report: `H:\The Elder Scrolls - Skyrim - Special Edition\PBRPipeline\PIXL-Renderer-ReleasePrep-Baseline-20260824\manifests\live-shaders-sha256.csv` (346 hashed rows).
- Preserved live shader tree: `H:\The Elder Scrolls - Skyrim - Special Edition\PBRPipeline\PIXL-Renderer-ReleasePrep-Baseline-20260824\live-shaders`.

## Staging

- Staging root: `C:\Users\PIXL STUDIO PC\Desktop\PIXL RENDERING - SKYRIM - DEVELOPMENT\beta\Current\PIXL-Renderer-v1.0-CURRENT-BETA`
- Initial staging inventory: 577 files / 2,196,476,743 bytes. `_PIXL_Backups` alone contained 224 files / 2,034,830,343 bytes. The active package outside that backup tree contains the intended Data-style roots (`Shaders`, `SKSE`, `Interface` through SKSE payloads where applicable, meshes, textures, ParticleLights, ESP, manifest, README, licence/credits), but requires exact reconciliation.
- Initial staging hash/inventory report: `H:\The Elder Scrolls - Skyrim - Special Edition\PBRPipeline\PIXL-Renderer-ReleasePrep-Baseline-20260824\manifests\staging-sha256.csv` (577 hashed rows).

---

# Upstream Reference

- Project: Skyrim Community Shaders
- Baseline: v1.8.3
- Reference checkout path: `H:\The Elder Scrolls - Skyrim - Special Edition\PBRPipeline\PIXL-Renderer-ReleasePrep-Baseline-20260824\reference\community-shaders-v1.8.3`
- Reference commit/tag verification: clean detached checkout of tag `v1.8.3`, commit `2f2919a71bed6132b125e41781304c8f6f73d002`, tree `d57249647a5c6b1fc9914d771f0f373374359c75`.
- Reference acquisition note: Windows initially rejected long Terrain Shadows heightmap filenames. `core.longpaths=true` was set only in the clean reference checkout and the tag was restored successfully; the verified reference worktree is clean.
- Licence parity: local and v1.8.3 `COPYING` SHA-256 `5546AD8E1DD114ED3772A1B530D530593EF3EEA748F91837120767944E22259B`; local and v1.8.3 `EXCEPTIONS.md` SHA-256 `F836C5F1D2021EE0DAA8C43C923A1E94CEBEB25F53F8A94384C6E19BB40BA1DA`.

---

# Completed Phases

- Baseline inventory and preservation completed on 2026-08-24.
- Exact Community Shaders v1.8.3 reference acquired and verified.
- Relevant-tree byte/hash comparison against v1.8.3 completed: 508 upstream files were compared with 628 current files; 135 current files remain byte-identical to the historical baseline. Renamed/superseded module ancestry is recorded in `ATTRIBUTION.md`.
- GPL/exception and third-party provenance pass completed. `ATTRIBUTION.md` and `THIRD_PARTY_NOTICES.md` were added, repository/package credit guidance was corrected, and the existing `COPYING`/`EXCEPTIONS.md` were verified byte-identical to v1.8.3.
- Two-pass source/build/tool cleanup completed for all repository candidates whose dependency checks supported removal. No active module, live shader, or staging payload was selected merely from timestamps.
- Build/preset/staging/audit portability repair completed. Personal absolute paths were removed from public defaults and the documented `P:` mapping remains build-only convenience.
- All 36 registered render modules were reviewed for registration, descriptor identity, default-settings presence, lifecycle participation, shader/resource ownership, and important cross-module dependencies. The two demonstrated duplicate resource-initialization paths were repaired and the resulting Release build/audit passed.
- Live `Data\Shaders` and canonical public shader source were reconciled by package order, dependency path, content, and ABI rather than timestamp. The final comparison is 242/242 byte-identical files with no source-only/live-only entries or overlay collisions; active standalone changed shaders pass a strict 17-case FXC matrix.
- All seven renderer quality groups were traced from each menu entry through central application, module persistence, C++ resource/invalidation paths, and shader workload. The duplicate Radiance Weave detector was removed and real scalable workload controls were added where the profile was previously shallow.
- Release logging was audited against the preserved 602-line startup log. High-volume SDK/shader-hook traces and bounded feature diagnostics were moved to debug, while failure paths were retained or promoted to warnings/errors.
- The final post-warm-repair `--clean-first` Release build (197 seconds, exit 0), immediate verification build, settings/licensing/machine-path/source audit, all 36-module/package audit, live deployment, and warm log review passed. The final verified DLL is 19,284,992 bytes with SHA-256 `FC5EC23A7594E4D869B92FF4A4168867F18185BFB539B122324AD561127073CC`.
- The release staging package was regenerated deterministically from canonical source as a clean-cache package and audited successfully. Its entire 577-file / 2,196,476,743-byte predecessor was moved intact to a sibling preservation directory before replacement.

---

# Current Build Status

**POST-CHANGE CLEAN RELEASE BUILD PASSED**

### Last build
- Configuration: fresh CMake configure, Visual Studio 17 2022, Release, `PIXL-12C` preset, CMake 4.3.1, MSVC 19.44.35224, Windows SDK 10.0.26100.0.
- Configure result: PASS from a fully absent `build\PIXL-12C` tree.
- Command/process: `cmake -S P:\ --preset=PIXL-12C`, then `cmake --build --preset=PIXL-12C --target PIXLRenderer --parallel`.
- Result: PASS. After all native, shader, quality, and logging changes, `cmake --build --preset=PIXL-12C --target PIXLRenderer --clean-first` completed successfully; a verification build/link also passed.
- Current verified DLL: 19,284,992 bytes, timestamp `2026-08-24T07:03:52.3780872+10:00`, SHA-256 `FC5EC23A7594E4D869B92FF4A4168867F18185BFB539B122324AD561127073CC`.
- Source/package audit: PASS with 36 integrated modules, valid/aligned 42-section defaults and golden profile, exact upstream licence/exception hashes, no credential patterns, no public machine-specific absolute paths, no retired renderer/RT tokens, and the expected DLL.
- Diagnosed build-environment fault: FidelityFX's bundled `FidelityFX_SC.exe` deterministically terminated with Windows status `0xC0000409` when the generated command used the long canonical `H:` path. Reconfiguring via the already-verified `P:` SUBST shortened the tool command line and the same permutation generation proceeded normally. `H:` remains the canonical physical source; `P:` is used only for compilation.
- Meaningful warnings: none; the project compiled with its configured warning-as-error policy. `cppcheck`, `clang-tidy`, and `clang-cl` are not installed in this environment, so the module review used compiler enforcement plus targeted registration/settings/resource/ABI audits.
- Build-recovery note: a deliberately short command timeout detached the first MSBuild while it was generating the large PCH; a concurrent retry produced one locked/mismatched PCH pair. Only `cmake_pch.obj` and `cmake_pch.pch` inside the target's generated Release directory were deleted after the orphan build exited. The subsequent compile and no-work verification build passed.
- Final command: `cmake --build --preset=PIXL-12C --target PIXLRenderer --clean-first --config Release --parallel` from `P:\`; PASS with exit code 0 in 197 seconds. An immediate verification build completed in 1.8 seconds without recompilation. No C++ or HLSL source changed afterward.

---

# Current Shader Compilation Status

**COLD RUNTIME CACHE PASSED**

### Last shader-cache validation
- Cache state: the former 31-file cache was preserved externally and removed after exact path verification. A genuinely empty `Data\PIXL\PipelineLibrary` was then rebuilt by the clean DLL.
- Compilation method: `skse64_loader.exe` launched hidden against the live, hash-matched DLL/defaults and the 242-file reconciled shader tree. The full cold queue drained before metadata/cache acceptance; the process was subsequently closed and later warm runs used the completed cache.
- Result: PASS. The plugin wrote `Library.ini` with plugin version `1-0-0-0`, layout `PIXL.StageShard.v1`, and all 36 module/version entries after the queue drained. The cache contains 3,429 non-empty `.pixlbin` stages plus metadata, 1,986,002,150 bytes total; the staging completeness floor is 3,000.
- Later warm rendering legitimately added seven on-demand permutations, leaving 3,436 non-empty stages / 1,986,783,258 bytes. This does not alter the preserved cold acceptance evidence or the compile-on-device staging policy.
- Active shader compile failures: zero. The complete log has zero shader-task/compiler failures and zero errors. Its one warning is the separately diagnosed Terrain Field runtime lookup issue, not a shader failure.
- Log path: `C:\Users\PIXL STUDIO PC\Documents\My Games\Skyrim Special Edition\SKSE\PIXLRenderer.log`; the prior log is preserved as `logs\PIXLRenderer-pre-cold.log` in the external baseline snapshot.
- Source/live reconciliation: PASS, 242 canonical files and 242 live files, all SHA-256 identical; no overlay collisions.
- Static shader audit: PASS for all literal include paths and all literal C++ `Data\Shaders` runtime assets; deleted `EFFECTS11` permutations are now rejected by the audit.
- Strict Windows SDK FXC validation: PASS for 17 changed direct-entry cases spanning CameraSuite, ContactShadows, GroundResponse CS/HS/DS (both winding variants), HybridGI, ImageReconstruction, TissueDiffusion (three blur modes), and affected image-space shaders, using the same stage/global defines as `Util::CompileShader` plus `/Ges /WX /O3`.
- Post-quality Camera Suite validation: PASS for all 7 entry points affected by `PhysicalCameraCommon.hlsli` (`BloomUpsample`, `BloomPrefilter`, HDR output, physical-camera exposure/histogram/local exposure, and Stormglass) with Windows SDK 10.0.26100 FXC, runtime compute/global defines, and `/Ges /WX /O3`.
- Repaired during strict validation: GroundResponse `TerrainSurface.hlsl` reused loop variable `i` in the same FXC scope (X3078/X3129 under `/WX`); TissueDiffusion `SeparableSSSCS.hlsl` omitted `Common/GBuffer.hlsli`, leaving `GBuffer::DecodeNormal` undeclared.
- Preservation evidence: cold-cache manifest SHA-256 `18A98F8B8464547274A270B47AAB3DB0AE2189941BA33A2C6D8FBAF5D40C6771`; completed log SHA-256 `39D628B5C4DB27463C04AEAD2E2AB6E0FA434EAD6A8143FD2386C437E7C266CD` under the external baseline snapshot.

---

# Current Runtime / Smoke-Test Status

**COLD STARTUP/INIT AND FINAL CLEAN-BUILD WARM VALIDATION PASSED**

- Cold plugin initialization: PASS for the cold-tested `D8891C1AAAC71C9AAA21EB42B58C633CDA448CBDB83E9CD3A2815EC31C4ABC64` DLL through the compiler gate, DataLoaded callbacks, module/resource initialization, and responsive main menu.
- The owner cleared the former elevated Skyrim lock. Multiple repaired DLLs were then deployed byte-for-byte to the live plugin path and warm-launched against the completed cache.
- Terrain Field finding: `PIXL-TerrainField.esp` is present, listed enabled, and passes the strengthened TES4/ESL/TXST-0x800/EDID/hash package audit, but Skyrim's data handler does not load/expose it in this runtime. EDID lookup, load-order-aware plugin lookup, and the engine default relocation are all null during DataLoaded. Terrain Field now remains enabled, resolves Skyrim's active default landscape record lazily in `TESObjectLAND::SetupMaterial`, and uses it for form-ID-zero tiles. The bundled ESP still supplies extended default-tile maps when the host loads it; the module no longer disables itself when it does not.
- Streamline finding: SDK 2.10.3 deprecated global `slSetTag`. PIXL now opts into `eUseFrameBasedResourceTagging`, loads/calls `slSetTagForFrame` with the same validated frame token used by constants/evaluation, and retains one-time actionable failure logging.
- Manual-hooking finding: the FidelityFX D3D12 frame-generation path kept PIXL's compatibility proxy outermost but did not expose inner presents to Streamline. The inner frame-generation swap chain is now upgraded immediately after creation, while the D3D11 device is bound first as required by the SDK. Warm validation logged `Frame-generation present path connected` with zero deprecated-tag, device-order, `presentCommon`, shader/compiler, or critical/error matches.
- Logging finding: unconditional `GR-RESIST-CHECK` and `GR-MATERIAL` release diagnostics were identified in the warm log and are now gated behind the existing Ground Response movement-debug setting. Functional one-line initialization summaries and failures remain visible.
- Final clean-build warm evidence: build/live SHA-256 both `FC5EC23A7594E4D869B92FF4A4168867F18185BFB539B122324AD561127073CC`; 408-line / 30,315-byte PIXL log with SHA-256 `943A97961C2639ED32D444CBAD9F4777A99F5BB46AA6C5838ECE283F8FAA54C7`; zero errors/critical entries, zero shader/compiler failures, zero deprecated `slSetTag`, device-order, `presentCommon`, `GR-RESIST`, or `GR-MATERIAL` matches. Its sole warning is the explicit missing-plugin Terrain Field degradation described above.
- SKSE loader logs contained no failure/error/crash matches. No recent crash-like file and no Windows Application event referencing Skyrim/SKSE/PIXL were recorded during the final smoke window.
- Visual validation performed: NO / NOT YET

---

# Current Quality-System Status

**IMPLEMENTED AND STATICALLY/BUILD VALIDATED; RUNTIME VISUAL CHECK PENDING**

- One authoritative Lighting contract now drives Radiance Weave GI/cache/reflection workload, Contact Shadows, MaterialForge local-light contact rays, and Light Volumes. The Radiance page calls the same detector/table as the main menu; its obsolete duplicate table no longer mislabels shipped Ultra as Custom or silently changes experimental user toggles.
- Shipped Ultra is the preservation baseline. Its GI values remain slices 6, steps 12, cache samples 8, cache trace steps 4, injection stride 2, second bounce on, reflection steps 48, history 18, blur 2.0, radiance/reflection firefly clamps 8/10, contact samples 2, and one local contact-shadow light. Artistic strength/colour/radius and experimental specular/voxel toggles remain user-owned.
- Materials now scales actual object/terrain POM ray steps and detail-reconstruction modes as well as the existing material feature gates/specular sampling. Ultra preserves object 8/16/4, terrain 12/20/4, detail quality 2, and MaterialForge specular-AA strength 0.98.
- Atmosphere now scales actual volumetric froxel footprint, depth slices, and history-miss samples (Low 32/32/1 through Ultra 16/64/4), in addition to cloud detail/self-shadowing. The module's existing per-frame resource check recreates resources when grid dimensions change.
- Water, Terrain/Vegetation, and Characters were confirmed to change active SSR/caustics, wind/deformation, and skin/hair/SSS paths; their shipped Ultra values were preserved.
- Camera quality now restores from the clamped persisted menu tier at module load and changes active physical-camera histogram stride (8/6/5/4), local-exposure samples (2/4/6/8), and existing DOF quality. The new GPU tier reuses the unused `HDRDataCB` c15.w field at byte offset 252, preserving the 272-byte buffer, all registers, and all downstream offsets. Ultra preserves the prior 4-pixel histogram stride and 8 local-exposure taps.
- `ApplyGlobal` now applies all seven groups with one feature-data update and one save instead of seven intermediate updates plus duplicate menu saves. New-install code defaults now agree with the shipped Ultra JSON; all loaded quality integers are clamped to 0..3 before use.
- Validation: source/live comparison 242/242 identical; repository audit PASS; affected native target and no-work verification build PASS; all 7 Camera Suite entries affected by the shared cbuffer compile with strict FXC.

---

# Provenance / Licensing Status

**AUDIT COMPLETE; RECONCILED PACKAGE RECHECK PASSED**

- `ATTRIBUTION.md` records the exact upstream tag/commit and distinguishes retained infrastructure, PIXL-renamed/modified descendants, superseded systems, and candidate PIXL-specific additions without claiming uncertain authorship.
- `THIRD_PARTY_NOTICES.md` consolidates active third-party components and licence locations, including the Separable SSS binary acknowledgement obligation.
- `distribution/SOURCE-AND-CREDITS.md` now identifies the exact historical baseline and requires `COPYING`, `EXCEPTIONS.md`, `ATTRIBUTION.md`, and `THIRD_PARTY_NOTICES.md` in a release package.
- NVIDIA SDK redistribution/public-release notification language remains recorded as a final human publisher/legal checklist item; it is not misrepresented as completed engineering work.
- The reconciled shader/source/package payload was rechecked; no newly introduced third-party material or removed required notice was found.

---

# Cleanup Candidates

Record candidates here before deletion.

| Candidate | Classification | Pass 1 Evidence | Pass 2 Verification | Decision |
|---|---|---|---|---|
| `engine - backup\` | D | Complete 251-file duplicate-style source tree beside active `engine\`; untracked; name and content indicate a preservation copy. | Active-vs-backup hash comparison: 231 identical, 20 older/different, zero backup-only, four active-only. CMake compiles only exact `engine/`; no include, registration, runtime, config, shader, package, tool, or documentation reference points to the backup root. | REMOVE. External untracked snapshot preserves it. |
| `pipeline - Backup\` | D | Complete 171-file duplicate-style shader-module tree beside active `pipeline\`; untracked. | Active-vs-backup hash comparison: 160 identical, 11 older/different, zero backup-only, two active-only. CMake and staging enumerate only exact `pipeline/`; no runtime/config/tool/docs reference points to the backup root. | REMOVE. External untracked snapshot preserves it. |
| `_BACKUP_WINDOWLIFE_HOTFIX2_20260823-213946\` | D | Dated two-file hotfix backup at repository root. | Only contains older WindowLife C++/header revisions (active files add 261 lines and remove 89); no build/include/runtime/tool/package reference points to the directory. | REMOVE. External untracked snapshot preserves it. |
| In-tree `*.bak`, `*.pre*`, `*.13*_backup*`, `.vortex_backup` development copies | D/E | 33 `.bak` files and other dated backup variants exist in the source-like tree; live shaders separately contain 62+ backup variants. CMake compiles only `.cpp/.cxx/.h/.hpp` and HLSL/HLSLI under exact active roots, so these suffix variants are not selected by the normal source globs. | Repository source backups were enumerated individually. None is included by CMake, included by C++, loaded by runtime/string lookup, referenced by settings/UI/serialization/shaders/packaging/docs, or used by current tooling after retiring the dated apply/rollback scripts. Live shader variants remain deferred until shader reconciliation. | REMOVE verified repository copies now; defer live/staging backup removal until shader/staging phases. |
| Generated `build\` trees and old build-version DLLs/intermediates | D | 63,310 files / 20.66 GB of reproducible CMake/MSBuild output; many historical DLLs are present. | All children are ignored generated configure/build/validation output. The working DLL, hash, live copy, logs, Git diff, and untracked work are preserved externally. Presets/tooling now define regeneration; final validation requires rebuilding `build\PIXL-12C` from absent state. | REMOVE all existing generated build trees, then recreate `build\PIXL-12C` by clean configure/build. |
| Generated `dist\` archives and obsolete package outputs | D | 14 files / 107.37 MB, mostly historical ZIP output. | `dist/` is ignored output. The staging tool is the only producer/consumer and now creates a new package from canonical roots; no source/build dependency consumes an old archive. | REMOVE and regenerate only current intended artifacts. |
| Staging `_PIXL_Backups\` and unbound staging extras | D/E | 224 backup files / 2.03 GB sat inside the user-facing staging root. The active tree also had 28 shader backups, six retired NativePBR/SPIR-V experiments, `UserGraphics.json`, an unbound 37-image `Visuals\Skin` set plus `Skin.rar`, and one unbound SnowShell prototype. | The deterministic staging script, CMake, native runtime strings, HLSL graph, descriptors, settings, UI, serialization, asset loaders, documentation, and manifest were checked. Only `QualityPreviews\README.txt` had a real runtime/documentation path and was recovered into canonical distribution source. The menu loads only the PIXL brand image from `Visuals`; no Skin filename/path is consumed. | RECONCILE. Move the complete old package intact to the dated sibling preservation directory, then generate the final package from canonical source. Preserve the historical Skin artwork there; omit all verified backup/user/experimental debris from the user-facing package. |
| Empty legacy-named directories under `engine\Modules` | D | Empty directories remain for old upstream module names after PIXL module renaming. | Exact directory enumeration found only empty `InverseSquareLighting`, `ScreenSpaceShadows`, and `UnifiedWater`; CMake/build, includes, registration, strings, settings, shaders, package, tools, and docs do not depend on empty directories. | REMOVE. |
| CMake `_PIXL_RETIRED_RT_SOURCES`, physical/Vulkan RT branch, isolated smoke tests, and untracked Vulkan-Headers checkout | D | Explicitly marked retired and `HEADER_FILE_ONLY`, with runtime/UI/settings/capture callsites disabled by four `#if 0` blocks in MaterialForge. | All implementation references are internal to the retired branch or five isolated smoke tests. Generated VS project treats the `.cpp` files as headers. No enabled module/runtime path calls the branch. Vulkan-Headers is not a declared submodule or build include and only served the retired sources; AMD/CommonLib carry their own private Vulkan headers. | REMOVE the retired branch, disabled callsite blocks, isolated tests, CMake exception list, and standalone Vulkan-Headers checkout. Retain active MaterialForge physical-material/DX11 texture systems. |
| Dated 13M/13N apply/rollback scripts | D | One-off scripts patch live `Lighting.hlsl`/`Deferred.cpp`, create backup suffixes, and hard-code H:/P:. | No CMake, build, package, documentation, or active tool invokes them; only the apply/rollback scripts reference each other or `Deferred.cpp.pre13N`. Their intended changes already exist in active source and the baseline preserves the scripts. | REMOVE. |
| Retired `package\` shader fragments and `cmake\CleanupStaleEntries.cmake` | D | Two stale package shader files sit outside canonical `distribution`/`pipeline`; the CMake cleanup helper refers exclusively to the retired `package`/feature layout. | Active CMake never includes the helper, current audit explicitly rejects a `package` root, staging uses `distribution`/`pipeline`, and no runtime path consumes these two fragments. | REMOVE; update stale documentation/tool include paths to canonical roots. |
| Upstream Effects11, CS Editor, RemoteControl, RenderDoc feature modules already absent from active PIXL trees | C/E | v1.8.3 contains these features; current active `engine\Modules`/`pipeline\` do not. Existing worktree records upstream removals. | RemoteControl/RenderDoc native integrations have no active build/module path. Effects11/CSEditor still have ghost default/translation keys and dormant `EFFECTS11` shader branches; documentation also has stale developer-feature claims. | Preserve native-module removals; remove ghost UI/settings/docs now. Defer `EFFECTS11` shader-branch removal until shader-permutation/live-source reconciliation proves no active define path. |
| Four non-identical canonical shader overlay duplicates | D | The package-order model found stale base-tree copies of `Atmosphere/VolumetricFogMaterialCS.hlsl`, `RainResponse/Precipitation.hlsli`, `RainResponse/RainResponse.hlsli`, and `PIXL/Modules/RainResponse.ini`; later integrated-module content overwrites every one. | `StagePixlRendererStandalone.ps1` copies `distribution\Shaders` first and every sorted module kernel/descriptor afterward. The generated/runtime identity is the module descriptor, all shader includes resolve to the integrated module version, and the final live tree matches those later versions. Keeping the earlier non-identical copies makes source assembly ambiguous without changing the package. | REMOVE the four stale `distribution\Shaders` copies after live content is reconciled. |
| Retired `distribution\Shaders\NativePBR` diagnostic/ray-query kernels and matching live files | D | Six untracked HLSL/SPIR-V files (`HardwareRayQuerySmokeCS`, `HardwareWorldLightingCS`, `TraceDebugCS`, `TraceDenoiseCS`) remain from the physical/Vulkan ray-tracing experiment removed in the source-cleanup phase. | Exhaustive CMake/C++ runtime string, HLSL include, shader permutation, compute-dispatch, tool, packaging, and documentation searches found no consumer. CMake merely displays all shader files as project sources. The Vulkan/native implementation and isolated smoke tests were already independently proved retired and removed. | REMOVE from canonical source and live shaders; external baseline snapshot preserves every file. |
| Live-only `GroundResponse/SnowShell.hlsl` | D/E | Standalone HS/DS/PS prototype exists only in live shaders and has no canonical source counterpart. | No C++ shader compilation/load/dispatch path, HLSL include, define/permutation, package/tool/config/UI reference, or documentation requirement refers to it. Active snow deformation instead uses the integrated `TerrainSurface.hlsl` HS/DS path and b13/t101 contracts. | REMOVE from live shaders as a superseded unbound prototype; do not import to source. |
| Live shader backup and Vortex marker debris | D | The live tree contains 62 timestamped/`.bak`/`.vortex_backup` copies and 35 `__folder_managed_by_vortex` marker files, none of which is an active shader entry point/include/asset. | Canonical overlay comparison classifies them as live-only; exact runtime source references never name backup suffixes or marker files. All 346 original live files are preserved in the external baseline snapshot and its SHA-256 manifest. | REMOVE only the enumerated backup/marker files after active shader reconciliation; preserve actual runtime DLL/assets until staging analysis. |

Classifications:

- A = Required upstream infrastructure
- B = Upstream-derived and PIXL-modified
- C = Upstream feature superseded by PIXL
- D = Truly dead/obsolete/generated artifact
- E = Uncertain dependency

---

# Significant Engineering Findings

- The current codebase is not a small patch on HEAD: it is an extensive in-progress architectural/product transformation. Upstream or Git HEAD must never be used to overwrite the live worktree.
- Active source discovery is broad and automatic: `cmake/AddCXXFiles.cmake` recursively compiles `engine/*.cpp|*.cxx`, includes headers from `engine/`, and registers HLSL/HLSLI under `pipeline/**/Kernels` and `distribution/Shaders`. This makes exact root placement and suffixes important to cleanup decisions.
- The integrated pipeline has 36 registered module directories. A demonstrated defect was repaired: `pipeline\WindowLife` was active and ordered intentionally last for hook ordering, but had no `Module.ini`, so generated module versions omitted it and source/staging audit failed. A descriptor was added and temporary hard-coded version/known-module branches were removed.
- The active distribution shader tree has 92 files, while the live shader tree has 346 including runtime binaries/assets and backup debris. Content/integration comparison, not timestamp, will determine reconciliation.
- The reproducible package-order comparator (`tools/CompareLiveShaders.ps1`) assembled 240 canonical shader/descriptor files against 346 live files: 206 byte matches, 34 differences, zero canonical files absent from live, 106 live-only files, and four non-identical overlay collisions. Of the live-only set, 62 are backup variants, 35 are Vortex markers, eight are active HLSL includes paired to current C++/live entry shaders, and one is the unbound SnowShell prototype.
- Thirty changed active runtime shaders and eight missing active includes were imported from the known-working live tree after their C++/HLSL bindings and call paths were checked. Source-authoritative RainResponse/WindowLife descriptors were then mirrored back to live. Four shadowed base-tree duplicates, the six retired NativePBR diagnostics, the unbound SnowShell prototype, 62 backup variants, and 35 Vortex marker files were removed only after the recorded two-pass dependency checks; every original remains in the external baseline snapshot.
- The inactive Effects11 native module has no define producer or runtime registration. Its remaining shader-only conditional branches and `ENBSettings` symbol were therefore removed; an eight-register zeroed `ReservedPostProcessData` block remains on both C++ and HLSL sides to preserve all following FeatureData/b6 offsets.
- `COPYING` and `EXCEPTIONS.md` are exact v1.8.3 matches and must be retained.
- Initial ancestry mapping (provisional B unless later evidence shows otherwise): upstream core `Feature` infrastructure became `RenderModule`/integrated pipeline infrastructure; `TruePBR` became `MaterialForge`; IBL→AmbientProbe, ExponentialHeightFog→Atmosphere, HDRDisplay→CameraSuite, ScreenSpaceShadows→ContactShadows, LODBlending→DistanceBlend, GrassLighting→FoliageDynamics, GrassCollision→GroundResponse, HorizonFix→HorizonBlend, ScreenSpaceGI→HybridGI, Upscaling→ImageReconstruction, InteriorSun→InteriorDaylight, VolumetricLighting→LightVolumes, LinearLighting→LinearLightCore, ExtendedMaterials→MaterialLayers, InverseSquareLighting→NaturalLighting, ScreenshotFeature→PixelCapture, PerformanceOverlay→PulseProfiler, LightLimitFix→RadiantGrid, WetnessEffects→RainResponse, Skin→SkinOptics, Skylighting→SkyBounce, SkySync→SkyContinuity, CloudShadows→SkyVeil, HairSpecular→StrandShading, TerrainVariation→TerrainDetail, TerrainHelper→TerrainField, TerrainShadows→TerrainOcclusion, TerrainBlending→TerrainSeam, ExtendedTranslucency→ThinSurface, SubsurfaceScattering→TissueDiffusion, VolumetricShadows→VolumeOcclusion, WaterEffects→WaterOptics, UnifiedWater→Waterbody, and DynamicCubemaps→WorldProbes.
- Third-party notices detected in active material include Sony BEND contact-shadow code, Intel/MIT XeGTAO-derived HybridGI code, NVIDIA Streamline/reflex material, AMD FidelityFX/RCAS material, third-party BC6H code, Separable SSS code, shader math/color sources, spherical-harmonics material, and multiple OFL font families. Their source licence locations and release obligations are consolidated in `THIRD_PARTY_NOTICES.md`; none is relabelled as PIXL-original.
- The repository's long physical path is not safe for FidelityFX's bundled shader-permutation compiler. The canonical H: tree must continue to be exposed as short `P:` for builds; public build documentation now explains the repository-relative preset workflow without embedding the development-machine path.
- Registration/descriptors/settings agree for all 36 active modules. Every module display name has a default-settings section; the only additional top-level settings keys are the six intended core sections (`Advanced`, `Disable at Boot`, `General`, `Menu`, `Replace Original Shaders`, and `Version`). The live WindowLife descriptor was version `0-2-2`; source now preserves that version and its documented settings while retaining PIXL identity fields.
- The known-working runtime log proves all 36 active descriptors loaded. `HorizonBlend` correctly self-disabled because its separately optional compatibility DLL was absent; that is a handled optional dependency rather than an initialization fault.
- Two real duplicate initialization defects were traced from the baseline log and corrected. `State::Setup()` initialized MaterialForge explicitly before the central `RenderModule::ForEachLoadedModule` pass, which reloaded the complete PBR configuration twice. `ImageReconstruction::SetupResources()` initialized CameraSuite explicitly before the same central pass reached CameraSuite, causing its HDR/camera GPU resources to be created and upgraded twice. Both explicit calls were removed; central module order is now the sole owner.
- Important shared bindings were checked rather than renumbered: `FeatureData`/b6 C++ assembly remains layout-matched to HLSL; GroundResponse terrain buffers/resources and its newly reconciled compute kernels match the C++ CB/structured-buffer/register contracts; WindowLife t127 is unique; and b13 sharing occurs only across intentionally disjoint shader stages/geometry paths. DialogueFocus deliberately remains a renderer service with a private character-only PS buffer, not a registered module.

---

# Files Changed

- `ATTRIBUTION.md`, `THIRD_PARTY_NOTICES.md`, `README.md`, `distribution/SOURCE-AND-CREDITS.md` (provenance/licensing and portable build guidance).
- `CMakeLists.txt`, `CMakePresets.json`, `CMakeUserPresets.json.template`, `.clangd`, `tools/BuildRelease.ps1`, `tools/AuditPixlRenderer.ps1`, `tools/StagePixlRendererStandalone.ps1`, and shader verification scripts (valid presets/targets, safe portable defaults, audit/staging integration).
- `pipeline/WindowLife/Module.ini`, `engine/Renderer/RenderModule.cpp`, `engine/MaterialForge.cpp`, `engine/State.cpp`, `engine/Modules/ImageReconstruction.cpp`, and `engine/RenderModule.h` (descriptor repair, removal of retired hard-coded/disabled physical-ray branches, centralized module-resource lifecycle, and accurate lifecycle documentation).
- `engine/Modules/TerrainField.cpp` (runtime-discovered resilient local-form fallback for the bundled `LandscapeDefault` texture set when Skyrim's editor-ID index does not expose the TXST record).
- `distribution/SKSE/Plugins/PIXLRenderer/SettingsDefault.json` and `distribution/SKSE/Plugins/PIXLRenderer/Presets/PIXL-Renderer-Live-Tested.json` (restored the active WindowLife boot/settings section; both parse successfully and remain structurally aligned).
- `distribution/SKSE/Plugins/PIXLRenderer/QualityPreviews/README.txt`, `tools/StagePixlRendererStandalone.ps1`, and `tools/AuditPixlRenderer.ps1` (made the runtime quality-preview slot contract a canonical, automatically staged, audited release asset).
- `pipeline/Ground Response/Kernels/GroundResponse/SurfaceDeformationUpdateCS.hlsl` and `TerrainSurface.hlsl` (active runtime shader dependencies recovered byte-for-byte from the preserved live implementation after the source audit proved them missing).
- Thirty live-newer shader entry/include files across CameraSuite, common lighting/shadow data, ContactShadows, Effect, FoliageDynamics, GroundResponse, StrandShading, HybridGI, ImageReconstruction, MaterialLayers, grass, and TissueDiffusion; eight additional active ABI-paired includes for MaterialForge, DialogueFocus, eye rendering, foliage, GroundResponse runtime data, and MaterialLayers.
- `distribution/Shaders/Effect.hlsl`, `Particle.hlsl`, `Sky.hlsl`, `ISCompositeLensFlareVolumetricLighting.hlsl`, `Common/Color.hlsli`, `Common/SharedData.hlsli`, and `pipeline/AmbientProbe/.../AmbientProbe.hlsli` (removed unreachable Effects11 shader permutations while preserving the reserved 128-byte b6 offset).
- `tools/CompareLiveShaders.ps1` and `tools/AuditPixlRenderer.ps1` (reproducible package-order SHA comparison, include-closure validation, deleted-permutation guard, and literal runtime-asset validation).
- PIXL default settings, translation, architecture/calibration documents, `engine/PipelineHealth.h`, and this ledger (ghost-setting/path/claim cleanup and persistent findings).

---

# Files Removed

- Verified duplicate roots: `engine - backup\`, `pipeline - Backup\`, and `_BACKUP_WINDOWLIFE_HOTFIX2_20260823-213946\`.
- Generated output: the pre-existing 20.66 GB `build\`, old `bin\`, and old `dist\` trees. A new clean `build\PIXL-12C` is now being generated.
- Retired layout/debris: `package\`, empty legacy module directories, verified in-tree `.bak`/`.pre*` source copies, one archived `MaterialForge.rar`, and dated 13M/13N apply/rollback scripts.
- Retired disabled implementation: physical/Vulkan ray-tracing branch sources, their five isolated smoke tests, `tests\`, the undeclared standalone `extern\Vulkan-Headers` checkout, and the associated CMake header-only exception list.
- Retired/shadowed shaders: six `distribution\Shaders\NativePBR` ray-query/diagnostic files and four non-identical base-tree files that the integrated module overlay always superseded. Matching live NativePBR files and the unbound `GroundResponse/SnowShell.hlsl` prototype were also removed.
- Live shader debris: 62 `.bak`/`.vortex_backup`/dated backup variants and 35 Vortex folder-marker files. All removed live content remains recoverable from the external baseline snapshot.
- Obsolete build helper: `cmake/CleanupStaleEntries.cmake`.
- User-facing staging debris was removed by replacing the package from canonical source. The complete original package was moved intact and remains recoverable at `C:\Users\PIXL STUDIO PC\Desktop\PIXL RENDERING - SKYRIM - DEVELOPMENT\beta\ReleasePrep-Preserved-20260824\PIXL-Renderer-v1.0-CURRENT-BETA-PRE-RECONCILE` (577 files / 2,196,476,743 bytes), including historical Skin artwork and `_PIXL_Backups`.

---

# Build / Shader / Runtime Fixes

- Added the missing WindowLife module descriptor and removed hard-coded metadata exceptions, restoring consistent registration/version generation/audit behavior.
- Repaired invalid/stale CMake preset generator and target names and removed a broken target that referenced deleted `.github/configs` files.
- Added an actual `PIXL-Standalone-Beta` packaging target depending on source audit.
- Repaired audit/staging scripts so failures enumerate all errors and defaults are repository-relative and safety-bounded.
- Diagnosed long-path FidelityFX compiler termination and switched clean compilation to the intended `P:` convenience mapping without changing canonical source location.
- Repaired the audit target's PowerShell discovery (`find_program(... NAMES ...)`) and a false-positive identity scan; the direct audit now passes with all 36 modules and the Release DLL.
- Removed duplicate MaterialForge and CameraSuite initialization paths while preserving the central ordered module lifecycle; affected-source and verification Release builds pass.
- Reconciled the complete active shader namespace and repaired two strict-compile defects: GroundResponse FXC loop-variable shadowing and TissueDiffusion's missing GBuffer include. The 17-case changed-entry FXC matrix and the expanded source audit pass.
- Repaired Terrain Field's demonstrated DataLoaded failure by retaining the editor-ID lookup and adding a type-checked, load-order-aware lookup of the bundled ESL's known local form `0x800`. The final clean build/audit pass; elevated-process locking prevents the required warm-restart confirmation in this session.
- Extended the final source/package audit to parse the bundled Terrain Field plugin contract: it must be an ESL-flagged TES4 plugin containing a `TXST` record at local form `0x800` and the `LandscapeDefault` EDID, and staging must contain the byte-identical plugin. The strengthened 36-module/package audit passes.

---

# Quality-Setting Fixes

- Replaced the divergent native Radiance Weave preset detector with the central Lighting contract and calibrated Ultra to the shipped known-good values without taking ownership of artistic or experimental toggles.
- Added real POM/detail, volumetric-fog, camera-metering, and local-exposure workload scaling; verified Water, Terrain/Vegetation, and Character groups already drive active work.
- Restored Camera tier persistence, sanitized every loaded tier, aligned code defaults with shipped Ultra, and collapsed global application to one feature-data update/save transaction.

---

# Performance / Bloat Improvements

- Eliminated duplicate startup PBR configuration loading and duplicate allocation/destruction/recreation of the full CameraSuite HDR resource set. This is a deterministic startup/resource-churn reduction backed by the preserved baseline runtime log.
- Coordinated global quality application now performs one feature-data rebuild/save transaction instead of seven intermediate rebuilds and redundant menu saves.
- Lower Camera tiers now reduce physical-metering coverage and local-exposure tap count; Materials tiers reduce POM/detail work; Atmosphere tiers reduce froxel volume/sample work. Ultra preserves the shipped output/workload baseline.

---

# Logging / Debug Cleanup

- Baseline log evidence: 602 INFO lines, including 162 raw/informational Streamline SDK callbacks, 117 per-class `BSShader::LoadShaders` traces, GroundResponse diagnostic/build-tag entries, duplicated resource setup traces, and 12 individual HDR render-target upgrade lines.
- Streamline SDK `eInfo`/raw callbacks, per-shader-class/path/success traces, bounded GroundResponse `GR-DIAG` probes/build labels, DialogueFocus state/bind transitions, MaterialForge/MaterialLayers constant-buffer construction details, and individual HDR target upgrades now log at debug.
- Shader override success is summarized only when overrides exist; failed replacement counts remain warnings. Streamline SDK warnings/errors retain their severity, and a failed interposer load was promoted from info to warning.
- Render-module descriptor failures, settings JSON recovery, and override discovery/validation/read/write/delete faults were promoted from info to warning. HybridGI diagnostic-capture and Waterbody cache-generation summaries now select info versus warning from the actual success/failure result.
- All logging changes preserve compiler/shader/resource errors. The affected native target and 36-module source audit pass after the changes.
- Warm-runtime follow-up additionally gated the unconditional 48-line `GR-RESIST-CHECK` budget and per-material `GR-MATERIAL` audit behind `DebugMovementResistance`. Streamline frame-based tagging/present errors remain at error severity and Terrain Field's safe degradation remains a warning.

---

# Staging Status

**RECONCILED AND AUDITED**

- Final package: 286 files / 156,238,132 bytes; manifest records 285 payload files, `compile-on-device`, zero preloaded stages, and the `PIXL.StageShard.v1` compile pattern.
- Final DLL synchronized to staging and live Data: YES. Build/live/package SHA-256 all `FC5EC23A7594E4D869B92FF4A4168867F18185BFB539B122324AD561127073CC`.
- Final shaders synchronized: YES. Package contains all 242 canonical/live-identical shader, include, descriptor, and shader asset files plus all 36 module descriptors.
- Configuration/resources synchronized: YES. Canonical 42-section defaults/golden profile, theme, locale, fonts, PIXL brand mark, native module assets, and the quality-preview slot instructions are present.
- Required licences/notices synchronized: YES. `COPYING`, `EXCEPTIONS.md`, `ATTRIBUTION.md`, `THIRD_PARTY_NOTICES.md`, and `SOURCE-AND-CREDITS.md` are present and passed the package audit.
- Stale binaries/development debris removed: YES. No backup suffixes, archives, SPIR-V experiments, user settings, `_PIXL_Backups`, or historical Skin mockups remain in the user-facing package.
- Preservation: the entire former staging package was moved intact to the dated sibling `ReleasePrep-Preserved-20260824` directory before regeneration.
- Validation: `StagePixlRendererStandalone.ps1` and the final repeated `AuditPixlRenderer.ps1 -PackageDirectory ...` both passed; manifest/file/DLL/content checks passed. Final manifest SHA-256: `0793887195D5278088B944EB12FDF6C184FEBC985D8D453E4E635C32660F71CF`.

---

# Current Blockers

- No active engineering blocker. The previous elevated Skyrim process was closed by the owner and is no longer holding the live DLL.
- The bundled, structurally valid `PIXL-TerrainField.esp` is not loaded by the current game runtime despite being present and enabled in `plugins.txt`. PIXL now degrades safely by using Skyrim's active landscape record lazily rather than disabling Terrain Field; loading the extended-map ESP remains host/load-order behavior outside the native renderer's authority.
- The owner worktree remains unusually large and dirty by design; the external baseline snapshot and binary Git patch remain the rollback evidence for release-prep changes.

A blocker should only be listed here if it cannot reasonably be resolved from:

- local source;
- Community Shaders v1.8.3 reference;
- build system;
- live shaders;
- staging package;
- compiler/shader/runtime logs;
- available local tooling.

Non-critical blockers must not prevent unrelated phases from continuing.

---

# Next Actions

1. Supply the owner-authored quality/preset preview PNGs using the exact names in `distribution\SKSE\Plugins\PIXLRenderer\QualityPreviews\README.txt`; the intentional placeholder remains until then.
2. Before public binary distribution, complete the publisher/legal NVIDIA SDK/DLSS redistribution and notification review recorded in `THIRD_PARTY_NOTICES.md`.
3. Optionally diagnose why the host does not load the valid, enabled `PIXL-TerrainField.esp`; PIXL already remains functional through its validated lazy fallback, while extended default-tile maps require the plugin to load.

---

# Release Exit Checklist

## Source hygiene
- [x] Current PIXL source reconciled.
- [x] Obsolete build artifacts removed.
- [x] Dead files/modules removed safely.
- [x] Duplicate/backup shader/source clutter removed.
- [x] Machine-specific paths scrubbed where appropriate.
- [x] Secrets/private development data audit complete.

## Provenance/licensing
- [x] Community Shaders v1.8.3 comparison complete.
- [x] Original notices preserved.
- [x] PIXL modifications distinguished.
- [x] Applicable GPL/exception material present.
- [x] Third-party notices audited.
- [x] Remaining provenance uncertainty documented.

## Modules
- [x] Every active module reviewed.
- [x] No dangling registrations.
- [x] No ghost settings.
- [x] No deleted-feature shader permutations.
- [x] Cross-module integration reviewed.

## Quality system
- [x] Quality groups traced end-to-end.
- [x] No-op controls repaired/removed.
- [x] Radiance Weave quality investigated/fixed.
- [x] Presets produce meaningful scalable differences.
- [x] Persistence/startup/live application checked to the available runtime boundary; persisted Ultra tiers loaded, and native/shader paths were traced. Interactive visual comparison remains represented by the required owner preview-image work.

## C++ build
- [x] Clean Release build succeeds.
- [x] Expected final DLL generated.
- [x] No unresolved PIXL compile errors.

## Shaders
- [x] Live shader source reconciled into public source.
- [x] Stale shader copies removed.
- [x] Cold shader-cache rebuild completed.
- [x] Zero known active shader compilation faults.

## Runtime/logs
- [x] Available cold smoke/init validation completed.
- [x] Critical logs reviewed.
- [x] Logging cleaned/gated appropriately.
- [x] Final clean-build DLL deployed and warm-restart validated; Terrain Field remains enabled through its lazy engine-default fallback when the bundled ESP is not loaded.

## Staging
- [x] Final DLL synchronized.
- [x] Final shaders synchronized.
- [x] Final config/resources synchronized.
- [x] Required licence/attribution material present.
- [x] Stale binaries/development debris removed.

## Final verification
- [x] Final source audit completed.
- [x] Clean rebuild repeated after final changes.
- [x] Cold shader compile completed after the final shader/config changes; the later native-only form-lookup repair does not affect the cache contract.
- [x] `PIXL_RELEASE_PREP_REPORT.md` created.
- [x] Remaining manual item recorded: final quality/preset images.
