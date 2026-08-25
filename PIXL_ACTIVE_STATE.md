# PIXL Renderer — Active Development State

> Live engineering ledger for ongoing PIXL Renderer development.
>
> Codex should update this file after meaningful development work.
> Keep it concise enough to remain useful across sessions.

## Project Status

**ACTIVE DEVELOPMENT**

The release-preparation phase has been completed separately. This file is now for normal ongoing engineering work.

## Current Active Task

- WindowLife `0.5.6` exact loose-mask integration is built, clean-Release validated, and deployed to live Data and beta for owner runtime validation as of 2026-08-25. Runtime comparison proved the external mask selected the correct pane but the mask-fit confidence gate suppressed the room, curtains, and occupants. Exact external masks now use appropriately relaxed world-size/aspect acceptance and retain a precisely pane-clipped procedural fallback if UV-to-world reconstruction is unavailable; noisy native glow maps keep the strict facade-safety gates.
- Private source-control preparation is active. Generated build/staging/runtime trees, datasets, model weights, Python environments, downloaded DXC, and local automation state are ignored; internal ledgers and the independent `tools/PixDiTEnhance` research workspace are `export-ignore`. `tools/ExportPixlPublicSource.ps1` audits a committed public GPL source archive. GitHub CLI is installed, but no GitHub account is authenticated and no remote repository has been created or pushed.
- PixDiT Photo Mode Enhance engineering-validation foundation is complete as of 2026-08-25. The reproducible pixel-space teacher/distillation/export suite, cross-vendor DirectML validation, compositor shader, native bridge contract, model card, and measured build report are under `tools/PixDiTEnhance`.
- The generated checkpoints are deliberately **not** live or release models. The original synthetic artifact proves architecture/distillation/export, while `train_real_data.py` now provides bounded restoration/geometry pretraining from the completed Sintel/TartanAir RGB-depth corpus. Neither has been copied into `Data`, connected to the Photo Mode UI, enabled at runtime, or synchronized to beta.
- Production Photo Mode integration is a subsequent development task requiring licensed representative PIXL RGB/depth captures and reviewed targets, a compiled native DirectML bridge, real-image/temporal acceptance, and physical AMD hardware validation. Preserve the original Photo Finish capture and write neural output only to a separate sibling file.
- Auto-POM, foliage, runoff, cache, and benchmark items remain runtime-validation backlog. This iteration changed/deployed only WindowLife and added isolated PixDiT training/export tooling; it did not deploy a neural model or alter the other renderer systems.
- Refine object Auto-POM for the next runtime pass after owner screenshots showed diffuse/compression noise becoming pebbled displacement on road, roof, wood and stone meshes. Preserve authored height/POM as authoritative and retain the approved terrain/GroundResponse depth behavior.
- Auto-POM acceptance criteria: synthesize height only from coherent mid-scale material structure; reject broad painted colour, baked-light gradients, chroma-only changes and unsupported fine noise; keep shifts bounded at grazing angles; preserve stable DLSS/DLAA sampling; retain useful stone/brick/board depth even when the user exposes a high maximum-depth setting.
- Smart microdetail acceptance criteria: unsupported high frequencies remain shading-only, with bounded luminance-first albedo recovery, normalized normal reconstruction and subtle cavity/ridge roughness rather than literal displacement or amplified chroma/compression artifacts.
- WindowLife `0.5.5` and Material Layers `1.3.1` are now validated and deployed to `Data\Shaders` with Skyrim closed. Runtime visual/log validation is pending the next launch; do not promote this build to beta until that pass is accepted.
- Runtime-validate Foliage Dynamics `2.1.0` natural multi-scale wind deployed on 2026-08-24 while preserving the approved WindowLife v0.5 room-atlas and repaired complex-grass material baseline.
- Wind criteria: ordinary terrain grass must move even when Skyrim supplies negligible legacy wind; TREE_ANIM trees/plants must receive coherent broad gusts plus bounded branch/tip variation; enabling motion must not change vegetation roughness/specular/metallic response or fold cards into mirror-like facets; current/previous deformation must remain coherent.
- Grass alpha/cutout criteria: the dedicated override must work with enhanced vegetation lighting and wind independently disabled, without changing the existing grass population/distance fade.
- Grass criteria: preserve first-/third-person SSS parity, move/soften the packed-normal detail transition without introducing a new seam, and produce readable normalized GGX highlights under direct light.
- WindowLife criteria: the supplied Solitude facade must never expose a proxy/interior mesh or classify a whole building; authored masks must provide a stable common window centre, size, and floor; frames remain excluded; parallax, curtains, occupant variation, and indoor passers remain convincing.
- Runoff criteria: DLSS Balanced must align with DLAA, droplets must retain depth and continuity at close range while panning/walking, and the effect must not produce the supplied red/cyan spatial break-up.
- Cache criteria: one recovery compile is expected because the previous watcher bug had already reduced the persistent library to 60 stages; after recovery, edits to direct module kernels must preserve unrelated pipeline stages.
- World Benchmark v2 remains pending runtime smoke testing after these visual systems are accepted.

## Known-Good Baselines

### Overall

- The current working PIXL build is the baseline.
- Preserve existing PIXL visual functionality unless a task explicitly changes it.
- `Data\Shaders` may contain the newest runtime-tested shader copies and must be checked when relevant.

### GroundResponse 13BE

Protected known-good behavior:

- deformable snow/mud raised hull textures correctly;
- hull is fully opaque;
- hull correctly occludes player/objects using HS/DS raster depth;
- top face lighting is correct;
- directional/world-space shadows work;
- buried-player/contact-shadow bleed-through is fixed.

Do not regress this checkpoint.

## Recent / Important Development Areas

These are context, not necessarily unfinished tasks.

### Quality profiles

- Quality settings must be wired to real runtime/rendering parameters.
- Lighting quality grouping has been an active development area.
- Radiance Weave quality previously appeared to produce little/no effect and must remain meaningfully wired.

### GroundResponse interactions

Recent work has included snow/mud deformation, shout interaction, fire/spell interaction, dragon-fire behavior, and hull shadow quality.

### Precipitation

Recent design requirements include rain world-space behavior, snow world-space behavior, convincing precipitation distance, and snow integration with PIXL deformation and vanilla behavior.

### Vegetation

Known problem area from prior development:

- advanced wind could make vegetation appear shiny;
- wind behavior needed correction;
- RunGrass compilation/integration has required debugging.

### Director / Photo mode

DOF accumulation/history behavior has been an active system and should be considered when changing temporal/camera logic.

### PixDiT neural Photo Finish validation (2026-08-25)

- The external one-click TartanAir helper under `H:\sintel training data` now uses a compact seven-environment profile (AncientTowns, Antiquity3D, OldScandinavia and four OldTown variants), explicitly excludes the approximately 23 GB Ruins pair, requests only easy/lcam_front/image+depth archives, and performs resumable Windows-native Python extraction. Per-archive completion markers prevent re-download after verified ZIP removal. The download-only import and target selection pass self-check; the owner controls when to resume the external transfer.
- Added `tools/PixDiTEnhance/TRAINING_GUIDE.md`, a beginner-oriented guide covering aligned RGB/target/depth capture requirements, manifest layout, exact teacher/distillation/export commands, RTX 3060 Ti memory guidance, result interpretation, provenance, and the production acceptance checklist. It explicitly records that the real PIXL RGB/depth dataset capture utility is still required before production training.
- Added `tools/PixDiTEnhance`, containing deterministic synthetic RGB/depth generation, a compact 7,689,731-parameter pixel-space local-window transformer, conditional flow-matching teacher training, one-forward-pass student distillation, FP32/FP16/INT8 ONNX export, CUDA/CPU/DirectML execution tests, and a master retrying test runner.
- The student consumes fixed float32 NCHW `(1,4,512,512)` RGBD and produces bounded RGB `(1,3,512,512)` in exactly one invocation. It has no VAE and no iterative runtime scheduler. Arbitrary captures use registered overlapping RGB/depth tiles after existing PIXL temporal reconstruction, detail recovery, and Photo Lens processing.
- DirectML is the required AMD/NVIDIA/Intel runtime contract; CUDA/TensorRT remains optional. FP32 and FP16 were executed through ONNX Runtime `DmlExecutionProvider` on the available NVIDIA RTX 3060 Ti. Physical AMD hardware was not available and remains required before a production claim/release.
- Non-destructive contract: disabled by default, off the render thread, original image immutable, separate `_NEURAL` output, strict model metadata/hash and finite-output checks, bounded residual compositing, and immediate original-image fallback on any failure. The validation checkpoint is SDR sRGB only; HDR bypasses it.
- Authoritative 50-epoch/50-epoch run: teacher train loss `0.123044 -> 0.032718` (best validation `0.027479`); student train loss `0.021027 -> 0.019313` (best validation `0.032130`); student validation PSNR `28.231 dB` versus synthetic raw baseline `28.153 dB`; all values finite.
- 512x512 measured inference: CUDA FP32 `59.066 ms`, CUDA FP16 `33.418 ms`, DirectML FP32 `84.660 ms`, DirectML FP16 `34.900 ms`, and developer CPU INT8 `1016.334 ms`. These are development-machine pipeline measurements, not shipping performance claims.
- Final ONNX hashes: FP32 `62DB3204AE26CE91EF075E4F3DB12EFB81B0D0B90FF34745B5128F32F1C5AFD8`; FP16 `F41C3680E504E04DD6C89489EA4A9245F47AAC7409C391DAB6CC210CF7BBAEE3`; dynamic INT8 `91110A2C193F5E061B5E968ECA26EE2688784773D97248C4C5EC068A2444F30E`.
- Validation: full master runner PASS with hard convergence/parameter/PSNR/shape/finite/one-step/DirectML precision gates; all ONNX graphs passed full checker; deployment graphs contain no Sin/Cos operators; FP16 maximum delta from FP32 `0.000742`; compositor passed FXC `ps_5_0 /Ges /WX /O3`; all Python sources passed `compileall`.
- Detailed results and limitations are in `tools/PixDiTEnhance/BUILD_SUMMARY.md`, `MODEL_CARD.md`, and `hlsl_bridge_spec.md`. Generated weights and datasets remain under ignored `build/pixdit-validation` rather than public source or release staging.
- Added `tools/PixDiTEnhance/train_real_data.py`. It indexes the real extracted TartanAir front-left RGB/metric-depth layout and reads Sintel clean/final RGB plus `.dpt` depth directly from the downloaded ZIPs. Whole trajectories/scenes remain in one split; future true PIXL aligned pairs may be weighted only on the training side.
- Full inventory at stride 8: 8,389 samples total / 6,291 train / 2,098 validation (6,261 TartanAir plus 1,064 each of Sintel clean/final). Both Dunmer screenshots are SHA-256-recorded holdout aesthetic references and are never training entries.
- A 12-sample, 64-square CUDA smoke epoch passed in 3.0 seconds with finite loss/gradients and produced a compatible 7,689,731-parameter, one-forward-pass bounded-residual checkpoint. This is a plumbing test only: one epoch over a tiny subset does not establish visual quality.
- The real-data smoke checkpoint exported successfully to opset-17 FP32/FP16/INT8 ONNX. CUDA executed FP32/FP16 with finite correct-shaped output; FP16 also executed through `DmlExecutionProvider` with finite `(1,3,64,64)` output. Export metadata now identifies real-data pretraining separately from synthetic validation. Physical AMD validation remains pending.

### Eye rendering

PIXL eye optics/rendering is an active high-fidelity subsystem.

## Current Build Status

- PixDiT work added isolated tooling/bridge specifications only and did not change compiled PIXL C++ targets, CMake inputs, live shaders, `Data`, or beta staging. The established renderer DLL baseline below remains authoritative; no redundant renderer rebuild/deployment was performed for the synthetic model-validation task.
- Build configuration: `PIXL-12C` Release baseline
- Last clean command: `cmake --build P:\build\PIXL-12C --clean-first --config Release --target PIXLRenderer -- /m /nr:false`
- Result: PASS; full clean Release compilation and link completed after correcting the bundled FidelityFX DX11 shader compiler's overlong output argument.
- DLL path: `build\PIXL-12C\Release\PIXLRenderer.dll`
- Latest incremental Release build: PASS after WindowLife `0.5.5`; deployed DLL SHA-256 `C74901A7A33003C4DE9F0328661C38A108D3A60A03099DAC1B43EE5EB8F8058F`.
- Relevant warnings/errors: only inherited FidelityFX `MSB8028` shared-intermediate warnings; no PIXL compiler/linker errors. `tools/AuditPixlRenderer.ps1` passes for all 36 modules.
- FidelityFX build finding: `FidelityFX_SC.exe` exited with `0xC0000409` after receiving the long absolute H: output path. The DX11 backend now invokes it with portable relative `../shaders/dx11` from its CMake binary working directory; generated permutation dependencies remain absolute for CMake.

## Current Shader Status

- Live shader root: `H:\The Elder Scrolls - Skyrim - Special Edition\Data\Shaders`
- Source shaders changed in this iteration: `distribution/Shaders/RunGrass.hlsl`, both Foliage Dynamics kernels, WindowLife, and all four Rain Response runoff compute shaders.
- Last permutations tested: RunGrass PS with Foliage alone and with SkyBounce/ContactShadows/AmbientProbe/WorldProbes, RunGrass VS with Foliage, WindowLife base/Deferred/Envmapped/MaterialForge/Specular Lighting PS variants, and all four runoff CS stages.
- Compile result: all targeted stages pass Windows SDK 10.0.26100 FXC `/Ges /WX /O3` using valid production permutations.
- Module versions: Foliage Dynamics `2.1.0`; WindowLife `0.5.5`; Rain Response remains `3.2.0` because its changed runoff files are direct compute kernels and a version bump would unnecessarily invalidate every family receiving its global define.
- Runtime shader-cache status before this deployment: only 60 stages / 27,240,954 bytes remain under `Data\PIXL\PipelineLibrary`. The previous direct-kernel watcher path had already deleted the full library before the repair could take effect. No cache files were manually deleted for this deployment.
- Canonical/live reconciliation: PASS, 243/243 matching; zero different, source-only, live-only, backup-like, or overlay-collision entries.
- New watcher behavior: direct module `.hlsl` changes release only that module's owned kernels; tracked/owned `.hlsli` changes invalidate only affected pipeline families; `RunGrass.hlsl` maps to Grass; only genuinely unowned shared includes can request a full persistent-library clear. Full and selective invalidations now log an explicit reason.

## Current Runtime Validation

- Last owner evidence: grass SSS works in both first and third person (PASS), but a detailed near-grass transition remains visible; normalized GGX highlights remain too weak; WindowLife `0.4.0` can place/tile occupants inconsistently and one Solitude facade still exposes invalid interior geometry; runoff has severe DLSS Balanced spatial break-up compared with DLAA.
- Pre-deployment log review: the 14:31:59 run is 28,758 bytes / 393 lines with zero error/critical, shader-failure, exception, or assertion matches. It reports a valid disk cache and saves metadata at 14:32:25. This predates the new DLL and cannot validate the new runtime paths.
- Deployment: PASS while Skyrim was closed. The live DLL SHA-256 is `334E42307CCAA71A74A76ED2479D975455323DCF485106FBCC20D607578DDE17`, exactly matching the clean Release build; all 242 assembled live shaders match canonical source. `UserGraphics.json` and the beta package were not modified.
- Audit: targeted `git diff --check` passes; `tools/AuditPixlRenderer.ps1` passes for all 36 modules.
- New rollback checkpoint: `build\active-dev-checkpoints\20260824-153811-pre-grass-window-runoff` preserves the prior live DLL, the ten replaced live shader/descriptor files, and all 60 remaining pre-run cache stages (71 files / 46,688,089 bytes total).
- Runtime validation of this build is pending the owner's next launch/cache recovery and scene tests.

## Latest Active-Development Checkpoint — 2026-08-25 WindowLife 0.5.5 Exact Pane Masks

- Owner runtime result from `0.5.4`: representative Solitude windows still reported the procedural/fallback layout, some facade geometry remained lit, and closed shutters were treated as windows.
- The renderer log proved `textures\architecture\solitude\swindow01.dds` and `swindow03.dds` were explicit candidates but had no game glow texture bound (`glow=0`). Exact optional masks already installed at `Data\Textures\masks\swindow01_mask.dds` and `swindow03_mask.dds` therefore never reached HLSL. The same log showed arbitrary Solitude roof, wall, door, trim, floor, and detail materials being accepted merely because another texture slot contained glow data.
- WindowLife now scans only the optional loose `Data\Textures\masks\*.dds` directory, canonicalizes `_mask`/`noalpha` suffixes, loads valid DDS resources, and matches them by diffuse basename. Missing or invalid optional masks preserve the existing native-glow/procedural fallback; no external mask asset is copied into PIXL source or staging.
- Lighting PS slot `t125`, previously unused, now carries the per-draw exact pane mask. Existing room atlas `t126`, per-draw structured data `t127`, 176-byte C++/HLSL payload size, and all payload offsets remain unchanged. `Class0.z` uses reserved bits to distinguish native glow from an exact external mask.
- Exact masks take precedence over native glow and bypass the old flat-normal pane guard because the authored bitmap already separates glass from frame/leadwork. Native-glow and conservative procedural paths retain their prior guards.
- Classification now requires strong diffuse-path window evidence or an exact mask match; generic architecture plus an unrelated glow slot no longer activates WindowLife. This directly targets lit shutters, roofs, walls, and facade stencils without globally excluding legitimate stained/leaded windows.
- Strict FXC `/Ges /WX /O3` passes base, vertex-colour, environment/specular, and model-space-normal alpha-test Lighting pixel permutations. Incremental Release link, the 36-module audit, and canonical/live reconciliation all pass; source/live shaders are 243/243 exact.
- Deployed with Skyrim closed. Rollback checkpoint: `build\live-checkpoints\windowlife-v055-predeploy-20260825-163846`. Existing pipeline cache was preserved so `0.5.5` should selectively rebuild only eligible WindowLife Lighting pixel stages. Beta staging remains untouched.

## Previous Active-Development Checkpoint — 2026-08-25 WindowLife 0.5.4 Authored-Fit Repair

- Owner runtime finding: `Fit Interiors to Authored Window Masks` appeared to do nothing and manual Room Cell Width/Height remained finicky across window styles. The atlas itself was healthy; the runtime log reports a 12-mip authored atlas and ready t126/t127 resources with no WindowLife shader failures.
- Root cause: failed mask confidence silently reused the same procedural room grid as the toggle-off path. Manual RoomWidth/RoomHeight also participated in strict/background acceptance bounds, so tuning the procedural fallback changed whether automatic fitting was accepted.
- Added a mask-local 3x3 coarse-guide seed recovery only for narrow panes lost by the normal one-sample fast path. Normal panes retain the prior sampling cost.
- Replaced manual-slider-dependent fit bounds with geometry-relative limits plus conservative absolute ceilings. Strict four-edge confidence remains required for people/curtains; broader background confidence remains available for arches and mullioned panes.
- When authored fitting is enabled on a glow-authored window, a rejected fit now remains honest architectural glass instead of drawing a repeated procedural interior. Procedural width/height remains available only for non-glow materials or when authored fitting is disabled.
- Added fit-state diagnostics to the existing developer overlay: red = rejected authored fit, cyan = background-safe fit, green = occupant-safe fit. Developer sliders are relabelled `Fallback Room Width/Height` with their actual scope documented.
- Reduced the accepted-room source diffuse remnant from 14% to 8%. Independent glass reflection, dirt, roughness and refraction remain intact while the flat opaque/yellow vanilla window texture contributes less over a trustworthy room.
- Four representative Lighting pixel permutations pass strict Windows SDK FXC `/Ges /WX /O3`: base, vertex-colour, environment/specular, and model-space-normal alpha-test. A manually over-composed all-feature invocation exposed unrelated Ambient Probe/Atmosphere warnings because descriptor-specific constants were absent; it was not counted as a production permutation.
- Incremental Release build and link pass with only inherited FidelityFX `MSB8028` warnings. The 36-module audit passes. Build/live DLL SHA-256 is `FD0B840B9483A455ECCC978019C491D2D32EB7F6DC8CBBB48A0A86F3C0F83D07`.
- Deployed while Skyrim was closed. Canonical/live reconciliation passes 243/243 with zero differences or collisions. The full 3,434-stage cache was preserved; WindowLife `0.5.4` should selectively invalidate only eligible static Lighting pixel stages on next launch.
- Rollback checkpoint: `build/live-checkpoints/windowlife-v054-predeploy-20260825-144648`. Beta staging and `UserGraphics.json` remain untouched.
- Runtime validation pending: inspect the large Solitude stone facade and representative small/arched/leaded windows first. The debug overlay should distinguish a genuine mask rejection from frame classification. Confirm room fitting no longer depends on Fallback Room Width/Height and that the reduced source diffuse reveals the room without losing glass character.
- Follow-up candidate after this pass: some genuinely closed/shuttered windows still appear bright at night. Add a conservative transmissive-glass discriminator from authored glow, pane-normal continuity and diffuse/roughness evidence only after runtime evidence, so shutter rejection does not suppress valid stained/leaded panes.
- A signed pane offset was requested for consideration. Do not expose negative `ParallaxDepth` directly: it would reverse layer motion rather than place a surface in front of Skyrim's depth-tested mesh. If still needed after the opacity/mask test, implement a separately named bounded optical sample/composite bias with explicit semantics.

## Previous Active-Development Checkpoint — 2026-08-25 WindowLife 0.5.3 + Auto-POM 1.3.1

- Owner runtime result for WindowLife `0.5.2`: the authored room was only readable immediately beside the pane and did not approach the supplied parallax-window reference at normal street distance. The atlas loaded successfully and the runtime log contained no WindowLife/shader errors, so the defect was in sampling/compositing rather than resource availability.
- Root-caused a concrete atlas defect: the 2048x2048 PNG was uploaded as a single-mip SRV even though the asset contract required a full mip chain. Full-resolution room detail therefore collapsed into unstable dark noise when a window occupied only a few pixels. `WindowLife.cpp` now generates a full cubic, sRGB-aware mip chain at setup, retains a safe base-level fallback, and logs the uploaded mip count.
- Added separate close and distance pane confidence. Close glass still uses the strict full-resolution glow erosion and flat-normal guard that excludes frames; minified windows can retain filtered authored glow coverage without relaxing the candidate/material classifier. Oblique street views now fade only near genuinely edge-on angles.
- Rebuilt the authored interior as a four-cue depth stack: deep mip-stable room back plane, nearer high-contrast furniture/detail plane extracted from the room atlas, shallow seeded coloured curtains, and a dark parallax side/reveal. Each layer moves at an independent depth while the real glow mask remains the final coverage authority.
- Raised room visibility to a confident reference baseline using time-of-day-aware exposure, mip-preserved local contrast, near-total suppression of the flat source diffuse in accepted pane pixels, and a nonlinear composite weight that keeps partially minified windows readable. No user configuration values were overwritten.
- Refined synthetic object Auto-POM so the configured depth is a maximum gated by coherent mid-frequency structure. The march now rejects broad paint/light gradients, chroma-only changes and unsupported fine compression noise, uses normal/luminance agreement, and leaves fine residuals to bounded luminance-first albedo/normalized-normal/signed roughness microdetail instead of treating them as geometry.
- Material Layers cache ownership is now limited to applicable Lighting pixel techniques. No cbuffer size/offset, register, SRV slot, descriptor, GPU format, or per-draw WindowLife payload changed; the payload remains 176 bytes and the atlas remains at PS `t126` beside per-draw `t127`.
- Strict Windows SDK FXC `/Ges /WX /O3` passes WindowLife base, model-space-normal, and the exact production-sized Lighting feature stack. Auto-POM base/model-space/envmap, authored POM, multilayer POM, terrain, alpha-test exclusion, and Material Forge production permutations also pass.
- Incremental Release build and link pass. Only inherited FidelityFX `MSB8028` shared-intermediate warnings remain. `tools/AuditPixlRenderer.ps1` passes all 36 modules.
- Deployment completed with Skyrim closed. Live DLL SHA-256 is `5BF43C0CF3D5E2756877E91E653284F5E2BF53E0624BD2B7670C49C234E2C13E`; canonical/live shader reconciliation passes 243/243 with zero differences or collisions. `UserGraphics.json`, the pipeline cache, and beta staging were not modified.
- Rollback checkpoint: `build/live-checkpoints/windowlife-v053-20260825-032304` preserves the prior DLL/PDB and every replaced shader/descriptor. The next launch should selectively rebuild affected Lighting pixel stages for WindowLife `0.5.3` and Material Layers `1.3.1`, not clear the whole library.

## Latest Active-Development Checkpoint — 2026-08-24 Foliage Dynamics 2.1.0

- Root-caused the terrain-grass no-motion defect: the previous PIXL enhancement was only a percentage of Skyrim's legacy displacement. Draws with negligible `WindVector.z`, or current user gust/flutter values at zero, therefore received no meaningful added movement.
- Root-caused the apparent wind/material coupling: grass alpha override was gated by `EnableEnhancedVegetation`, and Terrain/Vegetation quality presets forcibly enabled that material-lighting switch while selecting wind quality. The wind shader itself uses `EnableEnhancedWind`, but the preset made motion selection opt animated foliage into a different lighting path.
- Rebuilt grass motion as a world-stable multi-scale field: transformed absolute instance anchors, travelling eased gust cells, directional meander, a calm ambient floor, weather-dominant response, tip-weighted cross drift/flutter, exact current/previous evaluation, and a hard bound on PIXL-added displacement. `Wind Response = 0` remains exact vanilla motion.
- Rebuilt TREE_ANIM enhancement with independently sampled current/previous broad noise, branch-scale variation, tip flutter, a restrained calm floor for smaller plants with zero tree amplitude, and bounded added deformation. Tree gust anchors restore `SharedData::CameraPosAdjust`, keeping the field stable across Skyrim camera-relative origin shifts without requiring FrameBuffer b12. Static Lighting geometry without TREE_ANIM metadata is deliberately not moved because it has no safe root/tip mask and could include buildings.
- Grass alpha/cutout now has an independent runtime gate. Material alpha shaping no longer multiplies the already separately dithered population/distance coverage, preventing the cutout controls from shifting the grass fade boundary.
- Quality profiles no longer force `EnableEnhancedVegetation` while selecting Terrain/Vegetation motion quality. The UI now labels lighting and motion explicitly as separate systems.
- FeatureData b6 remains exactly 80 bytes and Foliage tuning b13 remains exactly 96 bytes; no cbuffer fields, offsets, registers, SRV slots, formats, or descriptor ABI changed. Foliage Dynamics module version is `2.1.0` so only Grass and Lighting families are expected to invalidate.
- Strict Windows SDK 10.0.26100 FXC `/Ges /WX /O3` passes RunGrass base/GroundResponse VS, RunGrass base/full/depth PS, TREE_ANIM Lighting VS, and TREE_ANIM deferred Lighting PS.
- Incremental Release build passes; only inherited FidelityFX `MSB8028` warnings remain. The 36-module audit passes. Build/live DLL SHA-256 is `FAADB16A4D11D24F0D7F3AE543AC9DEFD5F8D3E9E40B53BE3C10E0A67B8A733A`.
- Deployment completed while Skyrim was closed. Canonical/live reconciliation passes 243/243. `UserGraphics.json`, the existing 3,435-file / 2,086,188,526-byte pipeline library, and beta staging were not modified.
- The preserved cache metadata still records WindowLife `0.4.1` and Foliage Dynamics `2.0.5`, while live descriptors are `0.5.0` and `2.1.0`. The next launch must therefore selectively invalidate the 589 cached Lighting stages and eight RunGrass stages once; the other 2,837 cache files should remain. Lighting cannot be narrowed further on this launch because the pending WindowLife v0.5 room-atlas integration changed its pixel path as well as this wind update changing TREE_ANIM vertices.
- Rollback checkpoint `build/checkpoints/20260824-foliage-wind-v21-predeploy` preserves the previous live DLL and affected live shader/descriptor files. The prior `20260824-windowlife-v5-mirrorgrass-predeploy` checkpoint already preserves all 589 Lighting and eight RunGrass stages, avoiding a redundant ~986 MB cache copy.
- Runtime visual validation pending: enable `Natural Multi-Scale Wind` with the current `Wind Response 2.0`, `Gust Strength 0`, and `Leaf Flutter 0` first to prove ordinary terrain-grass coverage; then test moderate gust/flutter values on grass, shrubs, and TREE_ANIM trees. Confirm material appearance is identical in a stationary before/after wind toggle and that alpha override still changes grass coverage with both lighting and wind disabled.

## Latest Active-Development Checkpoint — 2026-08-24 18:24 +10:00

- Owner runtime result: WindowLife `0.4.1` now works well and the supplied Solitude test is materially improved. Preserve that shader as the current window baseline; no WindowLife runtime HLSL, descriptor, module version, resource binding, or cbuffer was changed in this iteration.
- Completed post-run log/cache review. The recovered persistent cache contains 3,435 files / 2,086,133,326 bytes. The latest 806-line `PIXLRenderer.log` saved metadata at 17:08:42 with zero shader/compiler failures, criticals, assertions, device removals, exceptions, or out-of-memory matches. The only warning remains the expected optional `PIXL-TerrainField.esp` absence.
- The watcher selectively invalidated only five affected stages during the prior deployment; unrelated shader families were preserved. The roughly 51-minute compile was recovery from the earlier already-deleted library, not a continuing invalidation loop.
- Root-caused the forced-complex-grass ring: `RunGrass.hlsl` faded packed-normal amplitude to zero over an `AlphaParam1`-derived world-space radius. Vanilla grass has no corresponding packed normal, explaining why it did not show the same material/lighting boundary.
- Replaced that amplitude transition with a distance-progressive `SampleBias` filter. The complex normal remains present for the complete card lifetime; distance now removes high-frequency bandwidth without changing the mean material normal or producing a lighting ring.
- Root-caused tip-only grass reflection to two compounding inputs: some complex packs author their packed alpha strongest at blade tips, and wind-deformed derivative card normals allowed individual triangles/tips to dominate the lobe. Packed alpha is now a tunable low-influence variation channel (`0.15` default), passed blade interiors use near-binary material coverage, and the coherent per-card/instance normal remains authoritative.
- Foliage tuning payload version is now 3 at the existing dedicated `b13` register and remains exactly 80 bytes on C++ and HLSL. The former c4 padding float is now `GrassComplexSpecularMapInfluence`; no descriptor slot, register, buffer size, or FeatureData b6 layout changed.
- Renamed the misleading distance UI to `Complex Normal Filter Distance` / `Complex Normal Filter Softness`, and added `Complex Specular Map Influence`. These settings now correspond to real filtering/masking behavior rather than implying a generated-grass population LOD control.
- Moved grass GGX normal-variance evaluation outside the variable-length RadiantGrid loop and evaluate it once per pixel. Sun and local lights consume the same normalized anti-aliased roughness; this repaired strict FXC X3595 derivative-in-varying-loop failures without modifying the shared FoliageDynamics include, so Lighting permutations are not invalidated by this iteration.
- Strict FXC `/Ges /WX /O3` validation passes the base complex-grass pixel shader, the full SkyBounce + ContactShadows + AmbientProbe + WorldProbes + RadiantGrid + NaturalLighting pixel permutation, and the Foliage Dynamics vertex shader.
- Incremental Release `PIXLRenderer` build passed. New build/live DLL SHA-256 is `86D1ECDA635E5B356701B55B0FD18DAA163D7853926BBD18D51E4936785EFACC`; only inherited FidelityFX `MSB8028` shared-intermediate warnings remain.
- Deployment completed with Skyrim closed. Only the DLL, `RunGrass.hlsl`, and `FoliageDynamics/FoliageTuning.hlsli` were replaced. The shared `FoliageDynamics.hlsli` remains byte-identical to the prior live file, deliberately avoiding broad Lighting invalidation.
- Canonical/live shader reconciliation passes 242/242 with zero different, source-only, live-only, backup-like, or overlay-collision entries. `tools/AuditPixlRenderer.ps1` passes all 36 integrated modules.
- Rollback checkpoint `build/checkpoints/20260824-grass-complex-v3-predeploy` contains the previous DLL, three relevant live foliage files, and all eight existing RunGrass cache stages (12 files / 23,334,629 bytes). No cache file was manually deleted.
- Added `pipeline/WindowLife/ASSET_SPEC.md` for a future authored layered-interior path: ten separate 1024x1024 lossless room sources packed into a 4x4 4096x4096 BC7 sRGB atlas; separate 512x512 straight-alpha curtain and occupant sources packed into 2048x2048 atlases; 16-pixel gutters/edge duplication; curtain, occupant, and room layers at independent depths. Missing assets must preserve the current procedural fallback.
- Generated non-runtime concept references locally: a 4x4 semi-coloured blurred Nordic occupant atlas at `C:\Users\PIXL STUDIO PC\.codex\generated_images\01a02f8d-45f1-73d2-9ae4-82449f6e00d2\exec-7d91bb0e-357f-43f9-a665-7d28b55a861b.png` and a 4x4 sixteen-room Nordic medieval interior atlas at `C:\Users\PIXL STUDIO PC\.codex\generated_images\01a02f8d-45f1-73d2-9ae4-82449f6e00d2\exec-a45d4045-2313-43a5-a10e-dd12d154ae84.png`. They are concept/source images only, not yet packaged, bound, licensed as release assets, or copied into canonical/live shaders.
- Beta staging remains intentionally untouched pending owner runtime approval.
- Next runtime expectation: only RunGrass-owned cache stages should be invalidated/recompiled. Test forced complex grass at the previous boundary, then inspect full blade length under the same sun angle while varying `Complex Specular Map Influence` between `0.0` and `0.15`. Also confirm SSS parity remains intact.

## Latest Active-Development Checkpoint — 2026-08-24 WindowLife v0.5 + mirrored grass specular

- Owner runtime result for the preceding grass build: PASS. Selective compilation was "insanely fast", the forced-complex material boundary is gone, full-blade grass response looks good, and SSS parity remains intact. Preserve this repaired grass material path as the new known-good baseline.
- Integrated the generated PIXL room concepts as `pipeline/WindowLife/Kernels/WindowLife/RoomAtlas.png`: a 2048x2048 sRGB, fully opaque PNG containing sixteen 512x512 cells. Runtime path is `Data/Shaders/WindowLife/RoomAtlas.png`; GPU allocation is a single-mip 2048 RGBA sRGB texture.
- WindowLife now loads the atlas with DirectXTex/WIC, binds it at Lighting PS `t126`, and rebinds it alongside the existing per-draw structured SRV on `t127` for every Lighting draw. Missing, failed, disabled, or incorrectly sized atlas input logs one actionable warning and retains the procedural room/reveal fallback.
- Expanded `WindowLife::PerGeometryData` and matching HLSL `PerDrawData` from 160 to 176 bytes with one aligned `Asset0` float4. It carries regional room family, atlas-ready/enabled state, authored-room blend strength, and tile count. FeatureData b6, all constant-buffer registers, existing payload offsets c0-c9, and t127 are unchanged.
- Added default-on `Authored Room Backgrounds` and `Authored Room Visibility` (`0.78`) settings with JSON persistence. WindowLife is now version `0.5.0`.
- The room atlas is sampled on a recessed `1.34x` parallax back plane beneath the existing shallow analytic curtains and independently moving occupants. Sampling flips texture V to world-up, clamps within a 2.5-texel cell inset, fades outside the physical room rectangle, remains clipped by Skyrim's full-resolution pane/glow mask, and is exterior-only; indoor passer behaviour is unchanged.
- Authored room colour is exposure-matched to the source window emission instead of replacing its brightness blindly. Legacy and Linear Light Core output paths receive the proper colour-space conversion.
- Added deterministic regional material families in native classification: general Nordic/Whiterun, Solitude/castle/noble, Riften/canal timber, Windhelm/dark stone, Markarth/Dwemer, and inn/shop/trade. Each family selects from a curated stable subset of the sixteen rooms per resolved logical window.
- Added `pipeline/WindowLife/ROOM_ATLAS_PROVENANCE.md`. The atlas was generated from an original PIXL text prompt without external images or named-franchise source assets, then mechanically cropped/resized/repacked. It remains subject to the normal release/publisher licensing review.
- Added `Mirror Complex Normal Y (Specular)`. Foliage tuning payload version is now 4 and expands its dedicated b13 block from 80 to 96 bytes with aligned c5 storage; C++ and HLSL sizes match. The option is default-off and JSON-persisted.
- Mirrored complex-grass Y is specular-only: diffuse normals and SSS remain on the already-approved primary normal. When enabled, PIXL evaluates a second tangent-Y-mirrored normalized GGX lobe for sun and RadiantGrid local lights, averages it with the primary lobe, and filters both roughness values outside variable light loops. This broadens orientation coverage without doubling energy; existing Specular Strength and Normalized GGX Response remain the intensity controls.
- Strict FXC `/Ges /WX /O3` passes WindowLife Lighting base, deferred, environment-map, Material Forge, and specular permutations. It also passes complex RunGrass base PS, full SkyBounce + ContactShadows + AmbientProbe + WorldProbes + RadiantGrid + NaturalLighting PS, and Foliage Dynamics VS.
- Release `PIXLRenderer` build passed after CMake regeneration; the 36-module audit passes. Build/live DLL SHA-256 is `907A2EB94E1201FA7B9B3805DACEBA0BB188B16E4398702865575F21D46FF4AB`.
- Deployment completed after Skyrim closed. Canonical/live shaders and assets reconcile 243/243 with zero different, source-only, live-only, backup-like, or overlay-collision entries. Beta staging remains untouched.
- Focused rollback checkpoint `build/checkpoints/20260824-windowlife-v5-mirrorgrass-predeploy` contains the prior DLL, six affected live files, all 589 prior Lighting cache stages, and all eight prior RunGrass stages (603 files / 986,443,418 bytes).
- Expected next-launch invalidation is deliberately limited to Lighting (WindowLife v0.5/source/interface change) and RunGrass (tuning v4/source change). All unrelated persistent shader families must remain present. The room PNG itself does not create shader permutations.
- Next visual test: verify the log reports the room atlas as `ready`; inspect Solitude, Whiterun/general Nordic, Riften, Windhelm, Markarth, a tavern/shop, small panes, large panes, and oblique camera movement. Then enable `Mirror Complex Normal Y (Specular)` and lower Normalized GGX Response/Specular Strength as needed while comparing distant sheen against wet ground.

## Latest Active-Development Checkpoint — 2026-08-24 Natural Skin / Tissue Diffusion 3.3

- Completed a full C++/HLSL trace across Skin Optics, Tissue Diffusion, Dialogue Focus, Lighting direct/indirect composition, deferred mask output, compute dispatch, and live runtime copies before changing the skin path.
- Root-caused a major SSS contract defect: deferred `MASKS` uses R11G11B10 and Lighting writes x=SSS amount, y=human-profile blend, z=directional ambient luminance; the texture's implicit w is coverage/1. Earlier Tissue Diffusion code misread z/w as keratin/fur, so ordinary opaque skin was treated as hardened tissue and its warm diffusion path was shortened to roughly 32%.
- Restored the real mask contract without changing its format, register, or C++ layout. Base/Human selection remains in y; z/w are no longer interpreted as species/material masks. Cross-profile samples now receive continuous rejection to prevent diffusion between unlike adjacent actors/materials.
- Burley SSS now treats mask x as contribution strength rather than multiplying mean-free path. Physical radius remains stable at antialiased face edges; the final scattered result blends by coverage, sample coordinates are safely clamped, mask strength is applied once instead of squared, and empty neighborhoods fall back to the center irradiance instead of darkening.
- Separable SSS now blends the complete Base/Human kernel continuously per tap, rejects non-skin/profile-mismatched samples, and applies SSS coverage once in the final vertical pass. It retains the existing 21-tap ABI and cbuffer layout.
- Rebuilt skin micro-detail composition from a unit reconstructed tangent-space normal. The old fixed z=0 vector plus hand-restored base z produced brittle pore slopes; the new path clamps XY safely, reconstructs z, performs normalized RNM composition, and samples AO at the same biased mip as the normal.
- Replaced resolution/distance-dependent normal-derivative curvature with geometric-normal change divided by the matching world-position footprint. Facial macro curvature is now stable across dialogue distance and resolution while pores remain excluded from curvature broadening.
- Direct skin lighting keeps both normalized GGX lobes, uses terminator/back-light-only tissue transmission instead of a constant red front-light floor, and consumes detailed/soft shadow visibility once rather than squaring local shadows. This targets natural ear/nostril transmission without a waxy diffuse fill.
- Indirect skin lighting now preserves the two roughness lobes separately, applies multi-scatter compensation per lobe, no longer deletes probe specular on curved cheeks/noses/ears, and adds a restrained energy-layered grazing peach-fuzz response.
- Dialogue Focus keeps its existing private b13 ABI and conservative mask/quality boosts. The corrected tissue amount semantics make the existing focused-NPC SSS lift operate as a real strength adjustment rather than shrinking the blur radius.
- Skin Optics is version `0.3.0`; Tissue Diffusion is version `3.3.0`. `TissueDiffusion::HasShaderDefine` now truthfully reports Lighting only. This fixes the previous whole-library invalidation behavior for Tissue profile/version changes; its direct compute kernels remain owned by `ClearShaderCache()`.
- Strict Windows SDK 10.0.26100 FXC `/Ges /WX /O3` passes five valid skin Lighting PS permutations (FaceGen and RGB-tint, model-space and tangent normals, base/full rain/SkyBounce/contact/probe lighting) plus DiffuseExtraction, Burley, separable-horizontal, and separable-vertical compute shaders. Invalid MaterialForge+skin and incomplete ENVMAP test combinations were identified as non-production permutations and were not counted as failures.
- Release build initially exposed a FidelityFX generated-header dependency race after CMake regenerated for the module versions: the generator crashed once with `0xC0000409`, then the backend observed one header before generation completed. Serial generation/retry repaired the build tree; the following Release build and incremental header rebuild both pass. Only inherited FidelityFX `MSB8028` shared-intermediate warnings remain.
- The 36-module audit passes after the dependency fix. Live deployment completed with Skyrim closed. Build/live DLL SHA-256 is `87901917872F3B4B9C02752BD6CBEAB5EDAEB5676E830018A2AE7FAB5ED5BA61`; canonical/live shader reconciliation passes 243/243 with zero different, source-only, live-only, backup-like, or overlay-collision entries.
- The 3,435-file / 2,086,188,526-byte pipeline library was preserved byte-for-byte during deployment. Its metadata still records Foliage `2.0.5`, WindowLife `0.4.1`, Skin Optics `0.2.0`, and Tissue Diffusion `3.2.0`; live versions are `2.1.0`, `0.5.0`, `0.3.0`, and `3.3.0`. The next launch should invalidate the union of 589 Lighting stages and eight RunGrass stages once, preserving the other 2,837 compiled stages rather than rebuilding the whole library.
- The latest pre-deployment runtime log contains no shader/compiler failure, exception, assertion, device-removal, or out-of-memory evidence. It predates this skin build and therefore cannot validate the new visual/runtime path. The optional Terrain Field plugin warning remains unrelated.
- Rollback checkpoint `build/checkpoints/20260824-skin-optics-v3-predeploy` preserves the previous live DLL and affected skin/tissue/Lighting shader files. The earlier WindowLife checkpoint already contains all 589 pre-change Lighting cache stages, so no redundant near-gigabyte cache copy was made.
- `UserGraphics.json` and beta staging were not modified. Existing user tuning remains authoritative; visual validation should compare the same NPC/settings before changing F0, roughness, tissue distance, or skin colour.

### Natural Skin Runtime Validation Pending

1. Let the one-time selective Lighting/RunGrass compile finish and confirm the log reports selective invalidation, not a full cache deletion.
2. Test the same human NPC in dialogue under diffuse daylight, hard sun, warm fire/local light, interior ambient light, and strong ear back-light. Check for stable cheek/nose highlights, visible but restrained pores, warm thin-feature transmission, and no red front-lit wax layer.
3. Pan and move between close and medium dialogue distances; curvature/specular width and SSS radius should remain stable rather than popping with resolution or distance.
4. Inspect face silhouettes against bright and dark backgrounds for SSS bleed, dark mask-edge halos, temporal crawl, and DLSS/DLAA differences.
5. Check at least one Argonian/Khajiit and one body-skin material. Base/Human profiles must remain distinct without the previous false opaque-skin hardening.
6. Review `PIXLRenderer.log` for Skin Optics/Tissue shader compile failures and Dialogue Focus `ACTIVE`/GPU-bind confirmation after the test.

### Permutation-Granular Cache Policy — 2026-08-24

- The first Natural Skin/Foliage/WindowLife launch proved that startup validation no longer deleted the whole library: the runtime log reported `Selectively invalidated 597 cached shader stages (966807244 bytes) for 4 changed module(s); preserved unrelated pipeline families`.
- Direct filesystem inspection during compilation confirmed 2,837 old stages remained intact. The long wait came from all 589 expensive Lighting stages plus eight RunGrass stages being removed at family granularity, while the compiler card did not distinguish disk hits from actual compilation.
- Added `RenderModule::AffectsCachedShader(type, descriptor, stage)` as a conservative cache-ownership refinement. Default behavior remains whole-family invalidation; only audited modules opt into narrower scopes.
- Audited scopes are now: Foliage Dynamics = all RunGrass plus Lighting TREE_ANIM; Tissue Diffusion = FaceGen/FaceGen-RGBTint pixel shaders; Skin Optics = FaceGen, hair, and other skinned-actor pixel shaders; WindowLife = eligible static-object Lighting pixel shaders only.
- Startup invalidation now parses the existing eight-hex-digit descriptor filenames, removes only entries matched by changed modules, and re-stamps explicitly unaffected entries in shared top-level families. This prevents `Skip Unchanged Shaders` from immediately overriding the granular decision through coarse `Lighting.hlsl` modification time.
- The compiler card now displays `DISK CACHE` and `BUILT NOW` counts separately.
- Against the preserved 597-stage checkpoint, individual expected scopes are approximately 51 Foliage stages, 64 Tissue stages, 128 Skin/actor stages, or 272 WindowLife stages. The current four-module union would be about 451 rather than 597; normal single-feature iterations receive the larger reduction.
- Release build passed. Audit passed with 36 integrated modules; canonical/live shaders remain 243/243 identical. Built DLL SHA-256: `62CAE03949D3C7C9F4BECBE0C2B971C041C4CA225E742823D7B6EC7727CF381F`.
- Skyrim was subsequently closed and the permutation-granular Release DLL was deployed to the live hard-linked Vortex/game target. Source/live DLL SHA-256 is `62CAE03949D3C7C9F4BECBE0C2B971C041C4CA225E742823D7B6EC7727CF381F`.

## Checkpoint - 2026-08-25 WindowLife 0.5.1 Optical Interior Repair

- Diagnosed the supplied arched/cathedral-window capture. The authored room atlas was loaded and bound, but the shader discarded every interior layer when the coarse glow-layout scan could not prove all four rectangular edges. Arches, curved tops, and dense mullions readily fail that strict gate. At oblique/upward views the unconstrained back-plane offset could also move a successfully resolved room completely outside its atlas cell.
- Split layout eligibility into a tolerant background confidence and the existing strict occupant confidence. Irregular windows now receive a pane-clipped room atlas, while people and curtains still require the strict boundary result that protects the known Solitude facade/helper-mesh case.
- Shallow tier-2 windows now receive the recessed background and edge layer without people. Tier-3 windows receive occupants only when their authored layout is safe.
- Added a UV-tile-bounded two-texel glow-core erosion, tightened flat-normal evidence, and added restrained diffuse support so luminous outer trim/frame halos are rejected before any glass/interior work.
- Added room-wall parallax constraints for the curtain, depth, atlas, and occupant layers. The atlas back plane now uses a deeper `1.58x` separation without disappearing at upward/grazing views.
- Changed the final optical composite so the exposed room atlas dominates pane pixels: room exposure can recover the deliberately dark atlas, flat source diffuse is attenuated, and only a faint source stained-glass/emissive tint remains. This specifically targets the reported opaque yellow-paint appearance.
- Bumped WindowLife runtime metadata from `0.5.0` to `0.5.1`. With the deployed granular-cache DLL, the next startup should invalidate only eligible static Lighting pixel permutations, not the complete pipeline library.
- Representative WindowLife pixel permutations compiled successfully with FXC: base, vertex-colour, environment-map, parallax, and Linear Light Core variants. `git diff --check` passed; the PIXL audit passed with 36 integrated modules.
- Canonical/live shader reconciliation passed at 243/243 exact matches after deployment. The desktop beta package remains untouched pending visual approval.
- Runtime validation remains required: inspect the supplied cathedral windows, the original Solitude facade, representative small/round/leaded windows, and oblique camera travel. Confirm room detail is legible, frames remain excluded, parallax reads as depth, and no proxy mesh reappears.

## Checkpoint - 2026-08-25 WindowLife 0.5.2 Contrast-Preserving Exposure

- Owner runtime capture proved the recessed room atlas is present, correctly clipped to the supplied narrow leaded window, and no longer exposes surrounding wall/frame geometry. The remaining defect was insufficient room visibility and weak interior contrast.
- Reviewed the renderer log. Hybrid GI was disabled at boot at `00:57:04`, the pipeline cache completed successfully at `01:29:41`, and Hybrid GI was manually enabled at `01:46:32`. No Hybrid GI compiler failure is present; the owner confirmed the feature is working. Hybrid GI source/runtime integration was therefore left unchanged.
- Diagnosed the low-contrast room composite: exposure was normalized independently from each sampled room texel. That pushed dark furniture and bright room lights toward the same target luminance, suppressing the very contrast that sells depth.
- WindowLife 0.5.2 now derives exposure from the known authored RoomAtlas mean instead. It preserves the atlas's native lighting/furniture contrast and uses a restrained visibility floor interpolated from the existing day/night window activity state. Linear Light Core and legacy output paths use corresponding linear/sRGB reference values.
- Source and live WindowLife metadata were bumped from `0.5.1` to `0.5.2`. Five representative Lighting pixel permutations (base, vertex colour, environment map, parallax, and Linear Light Core) compiled successfully with FXC. PIXL audit passed; canonical/live shader reconciliation remains 243/243 exact.
- Skyrim restarted while validation was finishing. File watching is disabled in the current user configuration, so the already-running process retains its prior compiled shader; WindowLife 0.5.2 takes effect on the next normal restart/selective cache validation. Do not mutate additional live shader/DLL files until Skyrim closes.

## Active Dependencies / ABI Notes

Record task-specific sensitive interfaces here.

Examples:

- C++ struct ↔ HLSL cbuffer;
- SRV/UAV register;
- descriptor slot;
- CommonLib type/API version;
- shader define dependency;
- module initialization ordering.

- `WindowLife::PerGeometryData` is shared between C++ and HLSL as a structured buffer element; this task extends both sides from 128 to 160 bytes with matching `Interior0` and `Geometry0` fields.
- WindowLife binds its per-draw structured buffer at `t127`; resource slot and descriptor lookup are unchanged.
- WindowLife `Interior0.zw` now use the previously reserved floats for authored-mask layout and interior passer enable flags. The payload remains exactly 160 bytes on both C++ and HLSL sides; no register, descriptor, resource-format, or downstream offset changed.
- SkyBounce common-buffer camera anchoring feeds `ShadowVisibilityProbeArray` sampled by enhanced vegetation SSS; first-person correctness depends on `frameBufferCached.GetCameraPosAdjust()` rather than stale `shadowState::posAdjust`.
- Grass/tree enhanced wind is compile-time present but runtime-controlled through `foliageDynamicsSettings.EnableEnhancedWind`; vanilla displacement remains authoritative.
- `FoliageTuningData` is a dedicated C++/HLSL b13 payload and is now 80 bytes on both sides. New c4 values are detail-distance scale, transition softness, normalized-GGX response, and padding; FeatureData b6 is unchanged.
- `RoofRunoffTuning` is now 32 bytes on both C++ and HLSL sides, carrying active render size and inverse size. Runoff resources are allocated in the active DLSS render domain; current depth, history, emitters, drops, and composite all use that same domain.
- WindowLife retains the existing 160-byte `PerGeometryData` payload and t127 binding. The `0.4.1` repair is shader-only and does not change its C++/HLSL ABI.
- WindowLife v0.5 supersedes that historical 0.4.1 note: its C++/HLSL per-draw payload is now 176 bytes, preserving c0-c9 and adding aligned `Asset0` at c10. RoomAtlas occupies Lighting PS t126; the structured payload remains t127. Both are rebound together by WindowLife's final per-draw Lighting hook.
- Foliage tuning v4 uses a 96-byte dedicated PS b13 payload. c0-c4 are unchanged from the approved grass build; c5 adds the mirrored-specular-Y flag plus three padding uints.
- Direct Rain Response kernels are module-owned COM resources. `RainResponse::ClearShaderCache()` releases all four kernels and resets their retry state without touching the persistent Skyrim pipeline library.
- Tissue Diffusion's deferred mask contract is x=SSS amount, y=Base/Human profile blend, z=directional ambient luminance, and w=implicit coverage/default from the R11G11B10 mask target. z/w are not keratin/fur classifications.
- Skin Optics still uses the existing seven-float4 (112-byte) shared payload, PS b7 per-geometry wetness buffer, skin detail t72, RFAOS t71, and wetness t74/t75. No cbuffer size/offset, register, resource format, descriptor slot, or ABI changed in the natural-skin pass.
- `TISSUE_DIFFUSION` is a Lighting-only global define. The module's compute shaders are compiled directly and released through `TissueDiffusion::ClearShaderCache`; they are not Skyrim pipeline-library families.
- Persistent cache layout remains `PIXL.StageShard.v1`. Permutation-granular startup validation relies on the existing cache filename contract beginning with the eight-hex-digit descriptor and conservatively deletes an affected-family entry if that descriptor cannot be parsed.
- WindowLife 0.5.1 is shader/settings-metadata only: the existing 176-byte C++/HLSL per-draw payload, t126 room atlas, t127 structured buffer, formats, and descriptor slots are unchanged.
- WindowLife 0.5.5 adds optional Lighting PS SRV `t125` for an exact per-material pane mask. `t126`, `t127`, the 176-byte structured payload, all payload offsets, and GPU formats remain unchanged. Reserved `Class0.z` bits identify native glow versus external exact mask so Lighting never samples an unbound source.
- WindowLife 0.5.6 is HLSL/settings-metadata only. It does not change t125/t126/t127, the 176-byte payload, any register, format, descriptor, or C++/HLSL ABI.

## Files Changed in Current Task

- `engine/ShaderCache.h`
- `engine/ShaderCache.cpp`
- `engine/RenderModule.h`
- `engine/Menu/OverlayRenderer.cpp`
- `engine/Utils/UI.cpp`
- `engine/Modules/FoliageDynamics.h`
- `engine/Modules/FoliageDynamics.cpp`
- `engine/Modules/RainResponse.h`
- `engine/Modules/RainResponse.cpp`
- `engine/Modules/WindowLife.h`
- `engine/Modules/WindowLife.cpp`
- `distribution/Shaders/RunGrass.hlsl`
- `pipeline/Foliage Dynamics/Module.ini`
- `pipeline/Foliage Dynamics/Kernels/FoliageDynamics/FoliageDynamics.hlsli`
- `pipeline/Foliage Dynamics/Kernels/FoliageDynamics/FoliageTuning.hlsli`
- `pipeline/WindowLife/Module.ini`
- `pipeline/WindowLife/ASSET_SPEC.md`
- `pipeline/WindowLife/Kernels/WindowLife/WindowLife.hlsli`
- `pipeline/Rain Response/Kernels/RainResponse/RoofRunoffDetectCS.hlsl`
- `pipeline/Rain Response/Kernels/RainResponse/RoofRunoffResolveCS.hlsl`
- `pipeline/Rain Response/Kernels/RainResponse/RoofRunoffGenerateCS.hlsl`
- `pipeline/Rain Response/Kernels/RainResponse/RoofRunoffCompositeCS.hlsl`
- `extern/FidelityFX-SDK/sdk/src/backends/dx11/CMakeLists.txt`
- `engine/Modules/TissueDiffusion.h`
- `distribution/Shaders/Lighting.hlsl`
- `pipeline/SkinOptics/Module.ini`
- `pipeline/SkinOptics/Kernels/SkinOptics/SkinOptics.hlsli`
- `pipeline/Tissue Diffusion/Module.ini`
- `pipeline/Tissue Diffusion/Kernels/TissueDiffusion/SSSCommon.hlsli`
- `pipeline/Tissue Diffusion/Kernels/TissueDiffusion/Burley.hlsli`
- `pipeline/Tissue Diffusion/Kernels/TissueDiffusion/SeparableSSS.hlsli`
- `tools/PixDiTEnhance/train_real_data.py`
- `tools/PixDiTEnhance/export_and_test_onnx.py`
- `tools/PixDiTEnhance/README.md`
- `tools/PixDiTEnhance/TRAINING_GUIDE.md`
- `.gitignore`
- `.gitattributes`
- `README.md`
- `PRIVATE_COMPONENTS.md`
- `SOURCE_DEPENDENCIES.md`
- `tools/ExportPixlPublicSource.ps1`
- `tools/PixDiTEnhance/PRIVATE_LICENSE.md`
- `cmake/FidelityFX-SDK.cmake`
- `cmake/patches/FidelityFX-DX11-Short-Output.patch`
- `PIXL_ACTIVE_STATE.md`

## Root Cause / Engineering Findings

- WindowLife classified any texture path containing architectural tokens as a candidate. Runtime evidence includes `sroofbluemetal01.dds`, `swoodbeam01.dds`, `smetaldoorl01.dds`, `stoneedgetrim01.dds`, `sgravestone01.dds`, and other non-window materials.
- The shader debug mask retained an 18% whole-material tint even when its pane mask was zero, exaggerating the false-positive geometry.
- Pane selection relied heavily on diffuse/gloss brightness and weak glow evidence, so raised/decorative frame areas could survive the mask.
- Window rooms used a fixed world-space extent and depth; small panes could intersect only a narrow part of an occupant, visually collapsing the silhouette into a line.
- SkyBounce centered its shadow-visibility probe field from `shadowState::posAdjust`, already documented elsewhere in PIXL as stale in first person. Enhanced grass SSS consumes this probe visibility, explaining the first-/third-person discontinuity.
- Enhanced wind multiplied the entire authored Skyrim bend by the shipped `WindStrength=2.0`, over-deforming foliage cards and exposing unstable reflective facets.
- Fifty-two installed Solitude NIFs reference Window Shadows-style helper textures under `textures\masks\swindow##_mask.dds`. Because those proxy paths contain `window`, the first revision still classified helper proxy geometry as a real pane and exposed it through the building.
- The original Solitude `_g.dds` glow maps already encode clean glass-only masks with dark frames/background. For glow-authored windows, diffuse/normal fallback thresholds were unnecessarily widening the pane selection onto frames or surrounding geometry.
- In complex grass PS permutations, SkyBounce probe visibility completely replaced detailed raster shadow visibility. A low/coarse or camera-displaced probe could therefore zero otherwise lit first-person grass transmission.
- Grass directional transmission was highly dependent on the presented side of a two-sided/card normal. Small first-/third-person view and deformation differences could flip the thin-sheet hemisphere and remove the bright backscatter term.
- The existing World Benchmark was a 20-location COC/wait/single-screenshot loop: it had no camera path, feature-focused route, benchmark quality contract, frame-time distribution, clean promo framing, settings snapshot, or resilient fallback for uncertain exterior COC names.
- Roof runoff treated output-resolution logical pixels as dynamic-resolution UVs twice: reconstruction multiplied normalized output UV by inverse render scale, while current/previous projection mapped through dynamic-resolution scale before indexing output-sized runoff buffers. Under DLSS this displaced world anchors/history and made tiny camera motion invalidate the effect.
- Roof detection used fixed 8/16-pixel eave samples and fixed 6/12-pixel roof-plane samples. Close geometry therefore represented too few world units to pass the physical size gates, explaining the missing near-camera effect.
- Runoff history sampled one nearest half-resolution texel and required a tight seed match. Sub-pixel camera movement could miss that texel, erase accumulated roof water, and leave emitters invisible until they slowly recharged.
- Real architectural glow atlases contain multiple window styles, mullions, and many disconnected bright sub-panes. Treating every connected bright island as a separate window would repeat tiny occupants in every pane; a coarser mask representation is needed to find the logical window group while the original full-resolution glow still clips every layer to the actual glass.
- WindowLife explicitly returned before evaluation whenever `SharedData::InInterior` was true, so the previous effect could never represent people passing outside an interior window.
- The old occupant was a nearly symmetric capsule figure translated rigidly across the pane. On small windows its limbs collapsed into a vertical line, and on larger windows its static outline read as a restroom/signage icon sliding sideways.
- Cold-start cache validation was all-or-nothing. A single module-version mismatch deleted all 3,434 cached stages even when that module only affected one shader family; validation also short-circuited after the first failed module.
- The live-file watcher still had a second full-clear path after cold-start invalidation was repaired: any nested `.hlsl` not present in the pipeline dependency map set `clearCache=true`. Rain runoff uses direct module compute kernels, so editing `Data\Shaders\RainResponse\RoofRunoff*.hlsl` deleted the entire persistent library even though those kernels are not library permutations.
- The watcher did not map top-level `RunGrass.hlsl` back to Skyrim's Grass shader type, and its duplicate-event predicate used AND across changed fields, which could discard distinct filesystem events.
- The apparent grass “LOD” boundary includes a shader-side packed-normal detail fade driven by `AlphaParam1`, not only Skyrim's mesh/card population distance. Moving Skyrim grass generation distance alone would not correct the lighting/material transition.
- Grass GGX was heavily attenuated twice: normal-variance filtering broadened the lobe aggressively and the final authored macro-specular response was multiplied by an additional `0.12`, making normalized highlights effectively unreadable.
- The DLSS runoff defect was a resolution-domain mismatch. Runoff UAVs were display-sized while depth and jittered scene rendering occupied the active dynamic-resolution rectangle, so Balanced mode reprojected/detected/composited different physical pixels; DLAA hid the bug because active and display dimensions matched.
- Window glow atlases split one logical window into disconnected bright islands around mullions. A one-island component fit could shrink occupants into individual subpanes, while failed fitting on a large candidate facade fell back to a repeated procedural room grid and exposed the Solitude proxy/interior mesh.
- The bundled FidelityFX shader compiler has a fixed-size path-handling defect: an absolute H: build output argument triggered `0xC0000409`, while the identical invocation succeeded with a short relative output path.
- Tissue Diffusion's documented mask z/w contract did not match the actual Lighting/G-buffer contract. Because the R11G11B10 mask target effectively returns w=1, nearly all ordinary skin was being treated as maximally hardened/fur-covered tissue.
- Burley applied the SSS amount both to mean-free-path distance and twice to neighboring sample energy, then always selected the scattered result for every positive mask value. This created inconsistent radius, edge darkening, and over-strong partial-coverage diffusion.
- Skin micro-detail decoded the shared normal texture with z=0 after remapping and restored only the base tangent z after RNM. This was not a unit-normal composition and destabilized close-up pore highlights.
- Skin indirect lighting averaged the two specular roughness lobes and then multiplied the result by `1 - Curvature`, contradicting the direct path and making curved facial landmarks unnaturally matte in probe/ambient light.
- Tissue Diffusion advertised `HasShaderDefine=true` for every Skyrim shader type even though only Lighting consumes the define, so any Tissue module-version change could invalidate the whole persistent library.
- Shader-family invalidation was still too coarse for Lighting: a changed skin, window, or tree feature removed every Lighting vertex/pixel stage even when compile-time technique guards made most permutations unable to execute that feature. Coarse `Lighting.hlsl` mtime validation would also have recompiled retained entries unless they were explicitly revalidated.
- WindowLife's authored-layout safety gate conflated a trustworthy moving-person boundary with background visibility. A missing coarse edge therefore suppressed the room atlas entirely on valid arched/mullioned windows. Independently, unconstrained view parallax could push the back plane outside `[0,1]`, reducing its weight to zero at oblique camera angles.
- The final WindowLife composite retained up to 12% flat source emission, only allowed a 2.2x exposure for the deliberately dark room atlas, and left the lit source diffuse largely intact. Even when a room sampled successfully, those choices favored the original opaque yellow texture over interior contrast.
- WindowLife 0.5.5 treated exact external pane masks as though they were noisy native glow atlases. Tall/narrow Solitude pane groups could fail geometry-radius/aspect confidence, and enabling authored-mask fitting then deliberately zeroed the room background, curtains, depth reveal, and occupants. The mask was correct; the fit trust policy was not.
- FidelityFX shader generation originally moved only the generator command onto `P:`. MSVC still opened nested permutation headers through the long canonical build path and failed at a deeply named include despite the file existing. Both generation output and the backend include directory must use the short alias.

## Changes Implemented

- Restricted native WindowLife candidates to explicit window/glass/glow-atlas evidence; generic architecture tokens alone no longer activate the effect.
- Added flat-pane normal evidence and stricter diffuse/glow gates so raised frames/decorative trim are rejected at pixel level.
- Removed whole-material debug tint outside the pane mask.
- Anchored adaptive rooms to each geometry bound, bounded depth for small panes, and added separately controlled shallow curtain and recessed-room layers.
- Added `CurtainStrength` and `RoomDepthStrength` settings/defaults while keeping descriptor/register bindings stable.
- Re-anchored SkyBounce probe data to the cached frame-buffer camera position used by other first-person-safe PIXL systems.
- Changed grass/tree enhanced wind to scale only PIXL's bounded gust/crosswind/flutter delta; zero is exact vanilla motion and the shipped value no longer doubles base deformation.
- Explicitly reject Window Shadows/helper proxy-mask material paths (`/masks/`, `_mask.dds`, `windowshadow`, `window_shadow`) before WindowLife activation, even when the filename contains `window`.
- When a real glow texture exists, use its authored luminance as the authoritative pane mask; diffuse and normal heuristics now remain fallbacks only for windows without glow maps.
- Increased the safe small-pane depth cap and added a derivative-based pane-boundary reveal so recessed layers read more clearly without extending the effect beyond glass.
- Preserve valid detailed raster illumination with `max(detailedShadow, skyProbeVisibility)` instead of allowing the coarse SkyBounce probe to replace it.
- Added a grass-only, modest normal-sign-invariant thin-sheet multiple-scatter term while retaining the existing physical backlight response, directional light color, albedo tint, SSS controls, and real shadowing.
- Bumped WindowLife to `0.3.1` and FoliageDynamics to `2.0.4` so runtime cache metadata reflects these shader changes.
- Rebuilt World Benchmark as `PIXL.WorldBenchmark.v2` with a nine-scene cinematic feature route and retained 20-scene full sweep; feature intent covers all 36 renderer modules.
- Added deterministic Catmull-Rom camera rigs, unpaused native free-camera ownership, clean HUD handling, three reference captures per scene, raw frame-time/GPU distributions, pass timing, settings snapshot, COC fallbacks, and manifest/video cue metadata.
- Added an optional temporary benchmark quality contract: DLSS/DLAA, frame generation off, and optionally frame limiter off. All modified runtime settings and HUD/camera state restore on completion/cancellation.
- Kept video recording external by design for now: in-process encoding would contaminate the benchmark workload. The deterministic clean route and manifest are ready for lossless OBS capture and promo editing.
- Corrected roof-runoff depth reconstruction/projection to keep output pixels, active-resolution depth sampling, and current/previous camera transforms in their proper coordinate spaces.
- Made runoff roof/eave tests adapt their pixel footprint to a stable world-space size near the camera, modestly increased close-drop anti-aliasing coverage, searched a 3x3 reprojection neighbourhood by stable world seed, and added a small current-frame reservoir so disocclusion/motion does not blank the effect.
- Bumped WindowLife to `0.4.0` and added `UseAuthoredMaskLayout` and `EnableInteriorPassers`, with default-on serialization and UI controls. Existing JSON files safely acquire both defaults.
- Added a two-resolution authored-mask layout: a coarse glow mip finds the logical window-group extents for scale/placement while the full-resolution glow mask remains authoritative for panes, mullions, frames, curved edges, and final compositing. Manual procedural width/height remains only as a fallback for materials without usable glow masks.
- Reconstructed WindowLife room axes from material-UV derivatives so mirrored/flipped atlas UVs do not turn occupants upside down, and applied consistent view-parallax offsets to the room, curtain, and occupant layers.
- Replaced the icon-like occupant with phase-animated segmented limbs, gait/bob, profile detail, and seeded cloak, robe, pack, and carried-object variants. Paired occupants use independent variants.
- Enabled subtle exterior passers when viewed from interiors. Indoor evaluation uses shorter/faster walking-biased events with reduced shadow strength and deliberately omits the curtain/recessed-room layers, so it reads as outside movement briefly attenuating window light rather than an interior room projected onto the glass.
- Implemented conservative per-module/per-shader-family cold-start cache invalidation. Plugin/layout changes and ambiguous mappings still force a full clear, while a known WindowLife mismatch now removes only Lighting stages; every module is validated even after an earlier mismatch.
- Moved high-frequency GroundResponse projectile, magic-event, and utility-focus telemetry from info to debug while preserving warnings, errors, and actionable diagnostics.
- Completed a Release build, strict shader compiles, source/live shader reconciliation, rollback checkpoint, and deployment while Skyrim was closed. The beta package remains intentionally untouched pending owner visual approval.
- Added reasoned persistent-cache operations and family-specific invalidation. Direct module kernels now hot-release only their owning module resources; module includes invalidate only declared dependent families; `RunGrass` maps to Grass; full clears are reserved for plugin/layout or genuinely ambiguous shared dependencies.
- Fixed watcher event de-duplication so a change in path, action, or directory is enough to retain a distinct event.
- Added Foliage Dynamics tuning payload version 2 with real-time `Grass Detail Distance` (default `1.35x`), `Detail Transition Softness` (default `1.0x`), and `Normalized GGX Response` (default `1.0`) controls. The RunGrass packed-normal transition now uses an independently scaled centre and softened half-width.
- Restored a readable normalized grass GGX response by reducing excessive specular anti-alias broadening, removing the hidden `0.12` energy loss, strengthening authored blade masks, and retaining the existing macro gloss/specular controls as authoritative user controls.
- Rebuilt runoff in active render pixels from allocation through detection, history, emitter projection, drop generation, and composite. Projection now uses the jittered camera matrix that produced current depth, and the composite queries the runoff mask dimensions rather than assuming the display target size.
- Added runoff bead depth cues from the drop-mask gradient: a restrained dark rim/core and cool lens highlight improve convexity without broad dilation or debug-colour leakage.
- Upgraded WindowLife to `0.4.1`: glow evidence must also pass flat/continuous pane-normal evidence, guide scanning tolerates narrow mullions, all subpanes derive a shared provisional centre/seed/rescan layout, and explicit size/aspect confidence gates reject facade-sized components.
- WindowLife now keeps glass optics but suppresses uncertain interior occupants when an authored glow layout fails, rather than falling back to a repeated building-wide room grid. Occupants use a lower common floor anchor, deeper seeded room placement, shallower curtains, and the existing articulated/variant/indoor-passer system.
- Changed the FidelityFX DX11 generator to a portable short relative output argument while preserving CMake's full dependency graph, allowing a genuinely clean Release build to complete.
- Corrected the Tissue Diffusion deferred-mask interpretation and rebuilt both Burley and separable sampling around stable physical radius, single-application coverage, continuous profile blending, and depth/normal/material edge rejection.
- Reconstructed unit skin micro normals, normalized their RNM composition, and made curvature distance/resolution stable from geometric derivatives.
- Preserved dual normalized-GGX skin lobes in direct and indirect light, restored curved-feature ambient reflectance, added restrained peach-fuzz IBL, and removed front-lit/double-shadowed transmission artifacts.
- Narrowed Tissue Diffusion's cache dependency to Lighting, preventing future Tissue profile/version changes from deleting unrelated shader families.
- Added stage/descriptor-aware cache ownership for the four active shader features, permutation-filtered startup deletion, explicit revalidation of unaffected shared-family entries, and visible disk-hit versus built-now compiler counters.
- Upgraded WindowLife to `0.5.1` with separate background/occupant layout confidence, shallow-tier room backgrounds, constrained room-wall parallax, stricter glass-core/frame rejection, a deeper atlas plane, and room-dominant emission/diffuse compositing.
- Upgraded WindowLife to `0.5.6`: exact external masks receive trust-appropriate world-size/aspect gates, and an exact-mask fit failure retains the existing room/silhouette system while the full-resolution authored mask clips every contribution to glass. Native glow maps retain strict fit requirements, preserving facade/helper-mesh safety.
- Added a reproducible top-level FidelityFX DX11 patch with optional `PIXL_FFX_SHORT_BINARY_ROOT`. A genuine clean Release rebuild regenerated all DX11 permutations under `P:/build/PIXL-12C/ffx-dx11-shaders` and completed successfully in 194.4 seconds. Only upstream-style MSBuild MSB8028 shared-intermediate warnings remained.
- Replaced the root GitHub README with a first-person PIXL project page covering architecture, the 36 integrated modules and renderer services, installation, build/staging, Community Shaders v1.8.3 ancestry, GPL-3.0/source obligations, third-party notices, and PIXL music links.
- Synchronized the clean Release DLL to live Data and beta. Final DLL SHA-256 is `3C615F0DADD8C5718E63C01B3CEB7722D01CA3393207B0F2A2ADABBE733C416B`; WindowLife 0.5.6 HLSL SHA-256 is `E7F6575B043C52FB65E924FD2990C54A480C2A77B306B0FCD5FFC0EAAEA04683` across source/live/beta. The audited beta contains 36 modules, the room atlas, no PixDiT/models, and no shader pipeline cache.
- Re-ran the renderer/package audit successfully and reconciled all 243 canonical shaders against live Data: 243 matches, zero differences, zero source-only/live-only files, and zero overlay collisions.

## Remaining Issues

- The primary Photo Mode Enhance aesthetic benchmark is the owner-supplied Dunmer close-up `C:\Users\PIXL STUDIO PC\Desktop\8c3566e3-263c-4349-86d0-9432c9a325ab.png`. It is a quality reference, not an assumed pixel-aligned training target. The protected comparison baseline is the current working PIXL render/build. Any neural result must remain a bounded, reversible residual blend that preserves identity, facial geometry, expression, eye direction, tattoos/markings, pose, composition, and existing PIXL lighting/material fidelity while adding restrained skin microdetail, eye/hair definition, material separation, and local contrast. Failure or low confidence must return the untouched source capture.

- WindowLife `0.5.6` requires owner visual validation across large, small, round, leaded, arched, and atlas-packed windows. With authored-mask fitting enabled, verify that the exact pane boundary remains as clean as 0.5.5 but the room, silhouettes, curtains and parallax now remain visible. Confirm the Solitude facade has no proxy/stone stencil and closed shutters/roof/wall materials remain neutral.
- Grass SSS camera parity passed the previous build but must be regression-checked. The new detail-distance control moves and softens the shader material transition; it does not change Skyrim's generated grass-card population distance. If a remaining boundary is geometric/card density rather than shader detail, adjust the relevant Skyrim grass generation/fade settings separately after an A/B isolation.
- Grass normalized GGX and the new default `1.35x` detail-distance scale require runtime evaluation under sun and overcast lighting. Tune from the new Foliage Dynamics controls rather than compensating global grass brightness until the seam source is confirmed.
- Rain runoff active-resolution and bead-depth changes require DLSS Balanced versus DLAA tests at the same camera, followed by stationary, pan, walk, and near/far checks.
- The permutation-granular clean-Release DLL is live with WindowLife metadata `0.5.6` and Material Layers `1.3.1`. The next startup is the runtime proof: its compiler card/log should show a selective WindowLife-eligible Lighting-pixel rebuild with most unrelated stages loaded from disk, not another whole-library rebuild.
- `student_1step_real.pt` from the 12-sample smoke run is validation-only and must not be integrated or judged visually. A capped 256-square pilot is now documented, but its output remains experimental until it beats the degraded-input validation baseline and passes holdout review. Production still needs co-registered PIXL raw/target/depth capture tooling, licensed targets, temporal/tiled validation, and physical AMD testing.
- World Benchmark v2 is native-build and live-deployment complete, but route framing/COCs, unpaused actors and precipitation, HUD restoration, quality restoration, captures, metrics, fallback behavior, and cancellation still need a runtime smoke test.
- The authored-mask search materially increases WindowLife shader work on candidate pane pixels. Measure Lighting/WindowLife cost during the benchmark and tune only if the observed GPU cost is disproportionate; do not discard mask correctness based on instruction count alone.
- The beta package is synchronized and audited without a shader pipeline cache. If runtime validation leads to another shader/DLL change, restage and re-audit beta before release; the owner will add the final validated cache separately.
- Natural Skin/Tissue 3.3 requires owner dialogue-scene validation. Pay particular attention to human facial warmth without waxiness, stable pore/specular response with distance, ear/nostril back-light, silhouette bleed, beast-race profiles, and DLAA versus DLSS temporal stability.

## Next Actions

1. On the next restart, record the compiler card's `DISK CACHE` and `BUILT NOW` counts plus elapsed time. WindowLife `0.5.6` should produce a selective eligible-static Lighting-pixel rebuild; stop and review the compiler log if it begins rebuilding the entire library.
2. Revisit `swindow01`/`swindow03`, the narrow leaded test, and the supplied cathedral view first. Confirm the exact loose mask is selected in the log, room structure is readable with camera-driven depth, and the flat source texture is reduced. Then inspect the original Solitude facade and closed shutters to confirm they remain neutral.
3. Repeat WindowLife validation on small, large, leaded, round, atlas-packed, and indoor-view windows; pay particular attention to atlas-cell edges and upward/grazing views.
4. A/B grass in first and third person with `Grass Detail Distance` at `1.0x` and `1.35x`, then check `Normalized GGX Response` at `0` and `1.0` under the same sun angle. Use `Detail Transition Softness` only to widen/narrow the blend after identifying its centre.
5. Compare rain runoff at the same camera under DLAA and DLSS Balanced, then test close/medium distance while stationary, panning, and walking.
6. Review the resulting PIXLRenderer/game/compiler logs and repair any demonstrated visual, cache, compilation, or resource-lifetime defect.
7. Smoke-test the nine-scene benchmark feature route and verify camera/COC framing, unpaused scene activity, captures/metrics, cancellation, and exact HUD/DLAA/frame-generation/limiter restoration.
8. After owner visual approval, keep the already synchronized beta unchanged unless a later patch is required; if it is, restage, preserve the no-cache boundary, and rerun the package audit.
9. Run the Natural Skin validation matrix above after compilation, then review the log before tuning existing user roughness/F0/MFP values or promoting the build to beta.
10. For PixDiT, run the documented capped 256-square auxiliary pilot only after reviewing the external dataset terms. Do not integrate that checkpoint; first compare validation PSNR/loss and previews, then implement aligned PIXL RGB/target/depth capture before production refinement.

## Session Recovery

If resuming after Codex restart/context compaction:

1. read `AGENTS.md`;
2. read `PIXL_ENGINEERING_CONTEXT.md`;
3. read `PIXL_DEVELOPMENT_WORKFLOW.md`;
4. read this file;
5. inspect current Git/worktree changes;
6. continue from the recorded active task.

Do not assume unfinished source modifications are disposable.
