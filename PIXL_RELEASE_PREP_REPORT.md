# PIXL Renderer Release-Preparation Report

Date: 2026-08-24  
Scope: local release engineering only; no push, tag, upload, deployment, or external release was performed.

## Outcome

PIXL Renderer was preserved, reconciled, repaired, clean-built, deployed locally, warm-validated, and staged from its canonical source. The public shader namespace and live Skyrim shader namespace contain the same 242 files byte-for-byte. A complete cold compile produced 3,429 shader stages with zero shader/compiler failures. The deterministic clean-cache staging package passes the integrated 36-module/source/package audit and contains the exact final Release DLL. All automatable release-preparation exit gates available in this environment passed; only owner-authored preview images and publisher/legal review remain.

## Baseline and preservation

- Canonical source: `H:\The Elder Scrolls - Skyrim - Special Edition\PBRPipeline\PIXL-Renderer-Engine`; `P:\` was used only as the already-established short-path build mapping.
- Initial branch/commit: `pixl-pbr-skyrim` at `dcea14b61396fd5544aa5f0ad2165c24aa0d8816`, with a large pre-existing owner worktree intentionally preserved rather than reset.
- Original working/live DLL: 18,446,848 bytes, SHA-256 `21BC6522F48BD23C999F0FCB70D50619FBD0BEA334456FE96F8EDD1E8C4FCA2B`.
- Final verified/live/staged DLL: 19,284,992 bytes, SHA-256 `FC5EC23A7594E4D869B92FF4A4168867F18185BFB539B122324AD561127073CC`.
- External recovery snapshot: `H:\The Elder Scrolls - Skyrim - Special Edition\PBRPipeline\PIXL-Renderer-ReleasePrep-Baseline-20260824`, including the original DLL, live shaders, inventories/hashes, logs, untracked files, and a binary Git patch (SHA-256 `AC2AFA6173B78CF45E99784F2FECE62C13A4CEFAF8A1386369BC1C3E01A8B857`).
- The 577-file / 2,196,476,743-byte pre-reconciliation staging package was moved intact to `C:\Users\PIXL STUDIO PC\Desktop\PIXL RENDERING - SKYRIM - DEVELOPMENT\beta\ReleasePrep-Preserved-20260824\PIXL-Renderer-v1.0-CURRENT-BETA-PRE-RECONCILE`.

## Cleanup

The two-pass dependency rule was applied before permanent repository/live-tree removal. Major removals were:

- duplicate `engine - backup`, `pipeline - Backup`, and dated WindowLife hotfix trees;
- 20.66 GB of old generated build output plus old `bin`, `dist`, obsolete package output, and historical build-version artifacts;
- in-tree and live shader `.bak`, `.pre*`, `.vortex_backup`, and Vortex marker debris;
- the retired physical/Vulkan ray-tracing branch, its isolated smoke tests, undeclared standalone Vulkan-Headers checkout, disabled integration blocks, and dormant NativePBR HLSL/SPIR-V diagnostics;
- the unbound GroundResponse SnowShell prototype and four stale shader copies always overwritten by active module overlays;
- empty legacy module directories, old one-off 13M/13N apply/rollback scripts, an archived MaterialForge copy, and the unreferenced `cmake/CleanupStaleEntries.cmake` helper;
- user-specific settings, old DLLs, archives, backup shaders, experimental binaries, and `_PIXL_Backups` from the final staging payload.

Useful infrastructure was retained and repaired: repository-relative CMake presets, Release build helpers, deterministic standalone staging, source/package audit, live-shader comparator, shader compiler/cache infrastructure, module descriptors, and development documentation that still describes active workflows. No module or shader was removed on timestamp or missing-text-reference evidence alone.

## Community Shaders v1.8.3 and provenance

The exact historical baseline was checked out cleanly at tag `v1.8.3`, commit `2f2919a71bed6132b125e41781304c8f6f73d002`, tree `d57249647a5c6b1fc9914d771f0f373374359c75`. Comparison covered 508 relevant upstream files and 628 current files; 135 current files remain byte-identical to that baseline.

Required upstream-derived infrastructure remains credited to Community Shaders and its contributors: plugin/SKSE/CommonLib integration, D3D hooks, shader compilation/cache, settings/serialization, weather and menu infrastructure, engine fixes, and shared shaders. The ancestry table in `ATTRIBUTION.md` maps the renamed or substantially extended systems, including TruePBR to MaterialForge, Screen-Space GI to HybridGI/Radiance Weave, HDR Display to CameraSuite, and the other active renderer modules. Effects11, CS Editor, Remote Control, and RenderDoc integrations are not active PIXL modules; their dead settings/shader remnants were removed where verified.

WindowLife, DialogueFocus, PIXL quality orchestration, PIXL tuning/benchmark interfaces, and physical-material extensions are recorded as PIXL-specific additions where no v1.8.3 counterpart exists. Absence from the tag is explicitly not treated as conclusive legal authorship evidence.

`COPYING` and `EXCEPTIONS.md` are byte-identical to v1.8.3. `ATTRIBUTION.md` and `THIRD_PARTY_NOTICES.md` were added, and the release package contains those four files plus `SOURCE-AND-CREDITS.md`. Embedded Sony/Bend, Intel/XeGTAO, AMD FidelityFX, NVIDIA Streamline/DLSS, BC6H, Separable SSS, shader, font, and other third-party notices were retained. A human publisher/legal review must still confirm NVIDIA SDK/DLSS redistribution and any pre-release notification obligations before public distribution.

## Engineering fixes

- Added the missing active WindowLife descriptor, retained its live `0-2-2` identity, and removed hard-coded metadata exceptions.
- Removed duplicate MaterialForge setup from `State::Setup` and duplicate CameraSuite HDR-resource setup from ImageReconstruction. The central ordered 36-module lifecycle is now the sole owner.
- Corrected invalid/stale CMake generator/target names and made the audit and standalone staging targets real, repository-relative, and safety-bounded.
- Diagnosed FidelityFX compiler termination `0xC0000409` on the very long canonical path; builds use the existing short `P:` mapping without embedding that machine path in public configuration.
- Reconciled missing active GroundResponse dependencies and live-newer shader entries/includes. Strict FXC then exposed and repaired GroundResponse loop-variable scope errors and TissueDiffusion's missing GBuffer include.
- Removed unreachable Effects11 branches while retaining the reserved 128-byte b6 block so the C++/HLSL feature-buffer ABI and all following offsets remain unchanged.
- Preserved shared registers, descriptor slots, resource formats, and C++/HLSL layouts. Camera quality uses the formerly unused `HDRDataCB` c15.w field at byte 252; the buffer remains 272 bytes.
- Reduced global-quality application from seven feature-data rebuilds and redundant saves to one coherent update/save.
- Moved high-volume informational Streamline callbacks, per-shader hook traces, Ground Response diagnostics, DialogueFocus transitions, resource-construction details, and individual HDR-target upgrades to debug. Warm validation found two remaining unconditional Ground Response families (`GR-RESIST-CHECK` and `GR-MATERIAL`); both are now gated behind the existing movement-debug setting. Failure paths remain visible or were promoted to warning/error.
- The cold DataLoaded pass exposed Terrain Field disabling itself because Skyrim's editor-ID index did not expose the bundled `LandscapeDefault` TXST. The asset is the exact v1.8.3 194-byte light plugin and contains type TXST/local form `0x800`. Warm testing proved the host does not load/expose that plugin even though it is present and enabled, so EDID, load-order plugin lookup, and the engine default relocation are null during DataLoaded. Terrain Field now stays enabled and resolves Skyrim's active default landscape record lazily in `TESObjectLAND::SetupMaterial`; form-ID-zero tiles use that record, while extended default-tile maps remain available when the host loads the bundled ESP.
- Migrated the bundled Streamline 2.10.3 integration from deprecated `slSetTag` to frame-based resource tagging with `eUseFrameBasedResourceTagging`, `slSetTagForFrame`, and the existing validated frame token. The FidelityFX D3D12 frame-generation path now upgrades its internal swap chain so Streamline observes every present while PIXL's D3D11 compatibility proxy remains outermost. The D3D11 device is bound before activating that hook, matching the SDK's manual-hooking order.
- The release audit now parses that plugin contract directly: TES4/ESL flag, `TXST` local form `0x800`, `LandscapeDefault` EDID, and source/package hash equality are all enforced and pass.

## Quality settings

All seven groups were traced from menu state through persistence, native settings, invalidation/resource paths, and active shader work:

- **Lighting:** controls the complete HybridGI/Radiance Weave slice/step/cache/reflection/history/denoiser workload, Contact Shadows samples, MaterialForge local-contact-light count, and Light Volumes tier. The former separate Radiance detector was inconsistent and could label shipped Ultra as Custom; one central contract now applies and detects the tiers. Ultra preserves the live-tested 6 slices, 12 steps, 8 cache samples, 4 cache trace steps, stride 2, second bounce, 48 reflection steps, 18 history frames, radius 2.0, and 8/10 firefly clamps. Artistic and experimental user toggles are not commandeered by the preset.
- **Materials:** now changes actual object/terrain POM loop counts, refinement/detail reconstruction, parallax/height/shadow gates, specular AA, and GGX multiscatter. Previously it changed several gates/strengths but left the main ray-loop workload shallow. Ultra preserves object 8/16/4, terrain 12/20/4, detail quality 2, and specular-AA 0.98.
- **Atmosphere:** controls SkyVeil detail/self-shadowing plus real Atmosphere froxel footprint, Z depth, and history-miss samples. The tiers now scale 32/32/1 through 16/64/4, and the module recreates resources on grid-size change.
- **Water:** changes active enhanced SSR/caustic gates, SSR distance/edge fade, caustic dispersion, and invalidates HybridGI history where needed.
- **Terrain/Vegetation:** changes enhanced wind, vegetation specular/flutter work, deformable ground, snow/mud deformation, and the active terrain-tiling path.
- **Characters:** changes skin detail/SSS, TissueDiffusion Burley sample count (8/12/16/21), kernel refresh, hair mode, and self-shadowing.
- **Camera:** the persisted clamped tier now reaches the GPU and changes physical-camera histogram stride (8/6/5/4), local-exposure taps (2/4/6/8), and existing DOF sampling. Ultra preserves the prior 4-pixel histogram stride and 8-tap exposure baseline; exposure/grade/effect enablement remains authored by the user.

The shipped Ultra default remains the visual baseline. Runtime startup loaded the live persisted menu with all seven group tiers at Ultra; defaults, golden profile, code defaults, and tier clamping are aligned.

## Shader validation

- Canonical source/live comparison: 242 versus 242 files, 242 SHA matches, zero differences, zero source-only/live-only paths, and zero overlay collisions.
- Static audit: complete literal include closure and C++ runtime shader-asset closure passed; deleted Effects11 permutations are guarded.
- Strict Windows SDK 10.0.26100 FXC validation: 17 affected direct-entry cases passed with runtime-equivalent defines plus `/Ges /WX /O3`; all seven CameraSuite entries affected by the shared cbuffer also passed.
- Cold-cache method: the previous cache was preserved and removed, the clean DLL/defaults and reconciled shaders were deployed, and Skyrim was launched through `skse64_loader.exe` so the plugin rebuilt `Data\PIXL\PipelineLibrary` from an empty state. Completion is accepted only when the queue drains and writes `Library.ini` with the `PIXL.StageShard.v1` contract.
- Final cold-cache result: PASS. `Library.ini` records plugin `1-0-0-0`, layout `PIXL.StageShard.v1`, and all 36 module versions. The cache contains 3,429 non-empty `.pixlbin` files plus metadata, totalling 1,986,002,150 bytes. The complete log contains zero shader-task/compiler failures and zero errors. Manifest SHA-256 is `18A98F8B8464547274A270B47AAB3DB0AE2189941BA33A2C6D8FBAF5D40C6771`; cold log SHA-256 is `39D628B5C4DB27463C04AEAD2E2AB6E0FA434EAD6A8143FD2386C437E7C266CD` in the external snapshot.
- The cold runtime warning was Terrain Field's failed EDID lookup after the shader gate. That native-only lifecycle defect was repaired as described above and does not invalidate the completed HLSL/cache result. Later warm runs added seven normal on-demand permutations, leaving 3,436 stages; staging intentionally remains compile-on-device with no bundled cache.

## Build validation

- Configuration: CMake 4.3.1, Visual Studio 17 2022, MSVC 19.44.35224, Windows SDK 10.0.26100.0, Release, `PIXL-12C` preset.
- Clean process: configure from an absent `build\PIXL-12C` tree with `cmake -S P:\ --preset=PIXL-12C`, then build `PIXLRenderer`; the final post-runtime-repair gate used `cmake --build --preset=PIXL-12C --target PIXLRenderer --clean-first --config Release --parallel` followed by a verification build/link.
- Result: PASS with exit code 0 in 197 seconds. The warning-as-error build produced the final DLL identified above; the immediate verification build completed in 1.8 seconds without recompilation. No meaningful compiler warnings remain. `cppcheck`, `clang-tidy`, and `clang-cl` were unavailable, so the audit used MSVC enforcement plus targeted registration/settings/resource/ABI checks.

## Staging

The current beta staging directory was regenerated from canonical source as the intended clean-cache/compile-on-device package. It contains 286 files / 156,238,132 bytes, including:

- the exact verified DLL;
- all 242 reconciled shader/include/descriptor/asset files and 36 module descriptors;
- canonical defaults, golden profile, theme, locale, fonts, PIXL brand mark, native runtime assets, and the source-controlled quality-preview instructions;
- `COPYING`, `EXCEPTIONS.md`, `ATTRIBUTION.md`, `THIRD_PARTY_NOTICES.md`, release README/credits, and a regenerated hash manifest.

`AuditPixlRenderer.ps1 -PackageDirectory ...` passed twice after final reconciliation. The manifest records 285 payload entries, `compile-on-device`, zero preloaded stages, and `PIXL.StageShard.v1`; its SHA-256 is `0793887195D5278088B944EB12FDF6C184FEBC985D8D453E4E635C32660F71CF`. Package/build DLL hashes match. No backup suffixes, archives, user settings, SPIR-V experiments, `_PIXL_Backups`, historical mockup Skin assets, or stale binaries remain in the user-facing package; those historical files remain recoverable from the preserved former package.

The canonical build, live `Data\SKSE\Plugins\PIXLRenderer.dll`, and staging package DLL are byte-identical with SHA-256 `FC5EC23A7594E4D869B92FF4A4168867F18185BFB539B122324AD561127073CC`.

## Remaining manual items

- Supply the final authored quality/preset preview PNGs using the exact names documented in `SKSE\Plugins\PIXL\Interface\QualityPreviews\README.txt`. Missing images currently use the intentional UI placeholder.
- Before any public binary release, the publisher/legal owner must confirm NVIDIA SDK/DLSS redistribution and notification obligations documented in `THIRD_PARTY_NOTICES.md`.

## Final runtime and audit evidence

The former elevated-process lock was cleared by the owner. The exact final clean DLL was deployed live and warm-launched against the completed cache. Its 408-line / 30,315-byte PIXL log has SHA-256 `943A97961C2639ED32D444CBAD9F4777A99F5BB46AA6C5838ECE283F8FAA54C7` and contains zero errors/critical entries, zero shader/compiler failures, and zero deprecated-tag, device-order, `presentCommon`, `GR-RESIST`, or `GR-MATERIAL` matches. Streamline logged `Frame-generation present path connected`; the only warning is Terrain Field's explicit notice that the host did not load its ESP and the safe lazy fallback is active.

SKSE loader logs contained no error/failure/crash matches. No recent crash-like file and no Windows Application event referencing Skyrim, SKSE, or PIXL was recorded during the final smoke window. The final package audit passed with 36 integrated modules, and the independent live-shader comparison passed 242/242 with zero differences, source-only/live-only files, or overlay collisions.

The bundled Terrain Field plugin loading anomaly remains an optional host/load-order investigation, not a release-preparation blocker: the plugin contract and staging hash pass, PIXL no longer disables the module, and the validated fallback preserves normal terrain operation. Extended maps from that ESP naturally require the host to load it.

No external release operation was performed.
