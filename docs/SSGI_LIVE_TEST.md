# PIXL SSGI live test

This live-test build replaces the active HybridGI diffuse pass with a PIXL DX11 port of the adaptive view-space ray march in UnitySSGIURP. It consumes PIXL's linear view depth, decoded view normals, and opaque radiance pyramid; accepted screen hits provide diffuse bounce, while missed rays provide the matching screen-space visibility term used as AO. Geometry-reprojected temporal history, depth/normal-guided spatial filtering for both diffuse GI and AO, and edge-aware full-resolution reconstruction are connected in the deferred pipeline. The existing hierarchical screen-space reflection pass remains optional and no longer receives world-cache or voxel fallback data.

The old world irradiance cache and scene voxel capture are disabled for this test. Older settings and presets that request the cache are ignored at load, its legacy UI controls are hidden behind the test-build gate, and neither its voxel volume nor its cache textures are allocated. This removes the blocky voxel contribution and its allocations from the active path. Screen-space transport cannot illuminate surfaces outside the current depth/radiance buffers; off-screen bounce requires a separate tested fallback.

Photo Finish already implements the Camera Accumulation reference's offline sample averaging in PIXL's capture pipeline: it freezes the Director Photo camera, jitters the projection, collects completed frames, aligns the jittered samples, and performs robust temporal averaging before the existing image-processing and save stages. That path is retained rather than adding a second accumulation buffer. It rejects some isolated frame outliers, but animated particles and materials can still produce residual trails.

Local Contact Shadows and inverse-distance light falloff now appear in Radiant Grid's Local Light Shading section. The controls still write the same MaterialForge settings and preserve their existing serialized keys and shader bindings.

The adaptive-ray control is connected to the validated temporal accumulation count: pixels with fewer than two accepted frames retain the configured ray count, while converged pixels progressively reduce work toward `AdaptiveRayMinimum`. Lighting-reactive history is capped by the preceding reprojection pass, so stale pixels return to the high-work path. Adaptive allocation is disabled in the shader when temporal denoising is off, avoiding stale accumulation data. The compiled shader revision is bumped so existing cached bytecode cannot mask this change.

The shader port is derived from UnitySSGIURP, Copyright (c) 2024 jiaozi158, under the MIT License. The accompanying `SSGI_REFERENCE_LICENSE.md` is included beside the shader in both source and runtime payload.

## Live-test limits

- SSGI and AO are screen-space effects; camera edges, thin geometry, and missing off-screen surfaces can cause incomplete transport.
- Indoor local point-light shadowing remains handled by Radiant Grid and Skyrim's local shadow maps; this SSGI pass does not generate point-light shadow maps.
- Confirm temporal stability, GI strength, AO polarity, SSR composition, and photo convergence in motion and in a fixed-camera capture before treating these defaults as release quality.

## Build validation record

The 2026-11-05 live-test build compiled successfully for Skyrim SE 1.7.104. The SSGI WARP test passed its no-self-lighting, finite-bounce, AO occluder-response, adaptive-ray convergence, native local-shadow tap, and screen-space point-shadow blocker checks. The RadiantGrid cluster and scene-voxelization GPU tests also passed. Strict FXC validation passed 693 compute-shader variants and 36 targeted runtime module permutations with warnings treated as errors. The architecture inventory validator passed with no unresolved includes, include cycles, or unresolved literal runtime assets.

The staged DLL and all 323 assembled shader payloads were deployed to the Steam installation. Post-deployment SHA-256 comparison found all 323 shader files matched the canonical assembled source; the deployed DLL matched the build output (SHA-256 `381F0FD61C8161C9BC1A0623F1A0D326D850A657D3BEE4F0AA33F890573F790E`). Skyrim was closed during deployment. Image quality and temporal behaviour still require in-game validation; these offline checks do not establish that shadows, GI, AO, and reflections look correct in motion.

## 2026-11-05 closed-game verification

The renderer was rebuilt from the current `PIXL-12C` Release tree. The live-test package manifest verifies 393 package entries; the deployment selector confirms all 324 selected DLL/shader entries are already identical to the package. The DLL SHA-256 is `381F0FD61C8161C9BC1A0623F1A0D326D850A657D3BEE4F0AA33F890573F790E`; all 323 live shader files match the canonical assembled source, with no missing or extra files.

The dated Steam pipeline-cache backup is already present in `Data/PIXL/PipelineLibrary`: all 7,758 backup files are byte-for-byte present, alongside 210 additional live cache files (7,968 total). The cache layout (`PIXL.StageShard.v1`) and shared ABI (`PIXL.SharedBuffers.20260926.RenderOrigin1`) match this build. Its older shader-revision label is retained as provenance; per-artifact source-closure identity determines which compiled stages can be reused, and the renderer recompiles changed artifacts.

The current Steam settings enable GI, the world cache, SSR, native local shadows, and screen-space local shadows. Skyrim remained closed for build/deploy validation; no new game session or image-quality claim is made by this check.

## 2026-11-05 closed-game follow-up

The Release target rebuilt successfully from `build/PIXL-12C`. The produced DLL SHA-256 is `FE6AA88D73CFFDB33799AC65D414A3BAEA736415846C11ECB27FAB530B702FEC`; the Steam-installed DLL has the same hash. Shader reconciliation again found 323/323 payloads identical, with no source-only or live-only files. No recopy was needed.

Offline verification passed: 7 focused SSGI, RadiantGrid, voxelization, reflection, math, and reflection-context tests; 40 targeted module shader permutations compiled under `/WX /Ges`; 3 RadiantGrid compute variants compiled; and the architecture inventory validated 935 files and 133 owners with no unresolved includes, cycles, or literal runtime asset paths. Skyrim was closed throughout. These checks establish source/build/deployment parity and compile-time contracts, not in-game visual correctness; a current gameplay log or frame could not be collected while the game was closed.

## 2026-11-05 point-shadow default correction

The screen-space point-light shadow implementation was connected but defaulted off in both `RadiantGrid::Settings` and `SettingsDefault.json`. Changed the default to on and updated the tooltip to describe its actual coverage limit: it traces visible screen-space blockers for up to four influential local lights and cannot see off-screen occluders. Existing saved user settings still take precedence over defaults.

Release rebuilt and deployed to Steam while Skyrim was closed. Build and installed DLL SHA-256: `8ECCF82A4F92FEF6737FCA4EAE769AB7896CA629241C2D21E57E17A1A1056FE1`. Source and installed `SettingsDefault.json` SHA-256: `17D7098DF6A589E2E15A8F5E78C7CD4F82882557717234B85152149D385189E3`; JSON parsing succeeded. The 323 shader payloads remained identical. Five focused SSGI, RadiantGrid, SSR, and reflection-context tests passed. Visual behaviour and frame cost remain unverified until a live scene is available.

## 2026-11-05 final closed-game verification

With Skyrim still closed, the Release target rebuilt successfully. Its DLL and the Steam DLL match at SHA-256 `8ECCF82A4F92FEF6737FCA4EAE769AB7896CA629241C2D21E57E17A1A1056FE1`; source and Steam settings defaults also match. Shader reconciliation found 323/323 files identical, with no missing, extra, or differing payloads. No recopy was needed.

All 693 compute-shader variants compiled with zero failures. All 40 targeted runtime module permutations compiled with `/WX /Ges`. Six focused scene voxelizer, SSGI, RadiantGrid, reflection Hi-Z, and reflection-context tests passed; architecture validation found no unresolved includes, cycles, or runtime asset paths. The current Steam PIXL log was absent while the game was closed, so runtime readiness messages and visual behaviour still require a live session.

## 2026-11-05 screen-space lighting preset correction

The unified Lighting quality tiers previously enabled world-cache flags and described cache sampling even though this live-test build disables world-cache/scene-voxel execution. Updated the tier contract to keep those dormant switches off, removed cache-only workload fields from tier matching/application, and corrected the GI panel descriptions to report only active screen-space rays/steps. Settings loading also clamps the old directional-cache and second-bounce switches off, so stale saved presets cannot make the disabled path appear selected. Artistic controls and the active screen-space SSGI/SSR/local-shadow shader paths are unchanged.

Release build completed successfully. The staged LIVE-TEST manifest verified 393 entries, and the deployment tool updated only the Steam `SKSE/Plugins/PIXLRenderer.dll`; user settings and the existing pipeline cache were preserved. Build and Steam DLL SHA-256 match: `A5F5A0BBD84D0E33A550CE12A2273AFD6C2C0DCCD4D3E12BF29186FC9FBADFFD`. Skyrim was closed during deployment. Shader payloads were unchanged from the previously verified 323/323 source-to-Steam match. In-game visuals and temporal stability remain unverified until the user runs the scene.

## 2026-11-05 dormant-cache preset cleanup

A focused preset test exposed a mismatch between six legacy presets and their portable equivalents: the legacy files requested the world cache even though this test build disables it. Aligned the paired presets and renderer defaults so world-cache, cache-directional-occlusion, second-bounce, cache-intelligence, budgeted cache updates, and emitter-injection toggles default off where present. The active screen-space SSGI/AO/SSR and local-shadow behavior is unchanged. The `PIXLTestRenderPresetLibrary` check now passes.

Deployed the six portable presets and `RendererDefaults.json` through the verified Steam deployment tool. The user settings file and pipeline shader cache were preserved. Source and installed defaults match at SHA-256 `680B022325209481FBBEECA1E8020C885BE8A3AF9DF42FB6ADCA4815FE084F16`; the installed DLL remains identical to Release at `A5F5A0BBD84D0E33A550CE12A2273AFD6C2C0DCCD4D3E12BF29186FC9FBADFFD`. Seven focused SSGI, AO, local-shadow, SSR, preset, and reflection-context tests pass under WARP. Skyrim remains closed; visual stability still requires an in-game run.

## 2026-11-05 full-suite readiness check

The seven focused SSGI, RadiantGrid, SSR, reflection, and preset tests passed. The first full 48-test run found one existing packaging mismatch: `SettingsDefault.json` set volumetric fog history weight to `0.65`, while the runtime default and contract specify `0.82`. Updated the packaged default to `0.82`; all 48 configured tests then passed. The architecture validator passed for 935 files and 133 owners. Shader reconciliation found 323/323 Steam payloads identical to the canonical assembled source. The Release DLL and Steam DLL match at SHA-256 `A5F5A0BBD84D0E33A550CE12A2273AFD6C2C0DCCD4D3E12BF29186FC9FBADFFD`; updated settings defaults were deployed with a backup and match at SHA-256 `7C36993CF7638A7D828C5BA62603BFEDA03E328B87013D9F5BE89C1D44A64303`. Skyrim was closed throughout deployment. This verifies the offline package/build contracts, not visual quality; in-game lighting, GI/reflection strength, noise, temporal stability, and frame cost remain to be checked in a live scene.

## 2026-11-05 menu-render crash follow-up

The 10:00:58 crash executes a null function pointer from `SKSEMenuFramework.dll`'s `HudManager::Render`, called by its `RenderUIHook`. PIXL appears only in CrashLogger's stack scan, and source search confirms PIXL does not reference SKSE Menu Framework or register HUD callbacks. The report therefore does not establish that PIXL caused the Framework's null callback. Source review did identify duplicate PIXL detours at the shared `MenuManager::DrawInterfaceStart` entry: a core detour plus an ImageReconstruction detour (or a CameraSuite detour when ImageReconstruction was absent). Consolidated PIXL's temporal suppression, ImageReconstruction `PostDisplay`, and CameraSuite UI-buffer routing under the single core detour, preserving the previous active-path conditions. This reduces hook-order interaction at the shared UI entry; it is a hardening change, not a proven root-cause fix for the Framework callback.

The rebuilt Release DLL passed all 48 configured tests and was deployed to Steam through `DeployPixlRelease.ps1`, selecting only `SKSE/Plugins/PIXLRenderer.dll`. The shader tree, user settings, and live shader cache were left unchanged. Build and Steam DLL SHA-256 match: `8B9A5C160F9DDAF47E0A66E31BB4948A08550FA2BC73F11A40FDC48D7CDB2DB3`. Skyrim was closed during deployment. A live run is required to determine whether the crash reproduces with the unified PIXL hook.

## 2026-11-07 normal-mode GI diagnostic capture

The Steam PIXL log reports HybridGI core and reflection shaders ready, AO/diffuse/bent SRVs bound to the deferred composite, and successful render dispatch. Those checks establish pipeline connectivity, not nonzero AO, GI, or reflection output. The normal Radiance Weave page now exposes the existing automatic diagnostic capture under a collapsed Diagnostics header, without requiring Developer Mode. The capture includes AO, diffuse GI, input radiance, hybrid reflections, a settings snapshot, shader hashes, and the log. It must be run in game to distinguish empty source buffers, tracing failures, and weak composition. The deferred World Probes view vector was also corrected to use the camera position instead of the world origin; this fixes an identified specular-direction error but does not establish that SSR, AO, or SSGI are visually working.

The `PIXL-12C` Release build and audit passed. The staged LIVE-TEST manifest verified 393 payloads; all 40 targeted module shader permutations passed with `/WX /Ges`. The verified Steam deployment changed the DLL and the composite shader, preserving user settings and the live shader cache. Build and installed DLL SHA-256 match: `D36A25583BA150205F5D09475BB4799BF5EBBEABB43E0B9F654CEB5002A4FE36`. Architecture validation found no missing includes, cycles, or runtime paths but flagged the generated index as stale from the broader active worktree; a full regeneration is deferred to that repository-wide pass. The in-game diagnostic set is required before treating AO, SSGI, and SSR as fixed.

## 2026-11-11 cache diagnostic repair

The 23:03 Steam capture exposed a diagnostic mismatch: the active SSGI kernel produced diffuse GI for cache debug selections 5–7 and 14–19, so the composite labeled repeated GI images as cache coverage, age, and state. The live SSGI kernel now reads the resident surface cell and its bound intelligence atlas for those views. Coverage uses occupancy, confidence, age, and intelligence reliability; the cascade view distinguishes near and far cells; cached irradiance displays the stored SH response independent of the artistic GI strength; intelligence modes show stored variance, age, reliability, state, light-change evidence, and receiver-plane compatibility. Empty or unmatched cells show black. Normal rendering modes do not enter this diagnostic branch.

The former directional-occlusion and voxel-reflection fallback views are not implemented by this active SSGI kernel. The capture skips them with an explicit reason, and the UI marks their reserved selections unavailable instead of presenting a mislabeled diffuse-GI frame. FXC compiled the active full-resolution feature combination, reduced-resolution variants, and the no-intelligence variant with `/WX /Ges`. The Release DLL built, architecture validation passed, and the DLL plus SSGI shader were backed up and deployed to the closed Steam game. An in-game recapture is required to validate the colors, cache occupancy, and any remaining presented-frame overlay contamination.
