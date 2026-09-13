# PIXL 1.0 compatibility and startup update

## Delivered behavior

- Foreground shader preparation ends after startup or game entry. Subsequent
  shader failures stay in logs/settings instead of showing over gameplay.
- Quick Setup includes Neural Rendering, selects DLAA when enabling NR from a
  non-DLSS path, and uses the existing save/restart-later/exit notice if the NR
  resources were not prepared at boot. Default NR remains off.
- Camera > External post-processing offers a ReShade preset dropdown and effects
  toggle using an optional, already loaded API 20 compatible add-on runtime.
  Requests are processed on ReShade's DX11 callback; no injection, preset rewriting,
  runtime download or background filesystem polling is added.
- Startup ENB conflicts explain why PIXL's effects/menu cannot initialize.
- Grass/window requirements, BEES 1.2 on 1.5.97, and owner GOG testing on verified
  1.6.1179.0 are documented in MOD_COMPATIBILITY.md and the package guide.

## Explicit limitations

**ENB coexistence and an installed-ENB preset selector are not implemented.** The
included ENB SDK exposes parameters/callbacks, not a portable ENB preset renderer
that can be inserted into PIXL. Its parameter writes require ENB callbacks and can
alter saved presets. Removing the early conflict check would expose overlapping
renderer hooks, resource assumptions and presentation ownership without resolving
them. This was rejected for the stable DX11 release. Future integration would need
an independently tested renderer ownership mode and an ENB preset test corpus.

**ReShade support is experimental control integration, not live certification.**
The API 20 host must already be loaded when PIXL initializes and allow add-ons.
Other API versions fail registration gracefully. The bridge deliberately ignores
DX12 runtimes. Its absence does not disable PIXL. SSEReShadeHelper remains an
existing startup conflict. ReShade's own compilation and overlay are not controlled
by PIXL's shader UI policy.

No game was launched for this update. Test SDR with NR/FG off first, then depth
effects, DLSS/FSR, HDR and alternative presenters separately. Confirm preset change,
effect toggle, runtime destruction/recreation and overlay ordering. Test NR first
activation, Continue for now, restart, and unavailable hardware/runtime. Test cached
and cold startup, save loading, new games and interior/exterior transitions.

## Validation

Release compilation passed after adding the new translation unit. The first
regeneration build failed linking it; the next build compiled the newly discovered
source and linked successfully. The final camera-page addition was also rebuilt.
No compiler timing or FPS claims are made.

All 14 package-manifest tests passed. Deployment fixtures verified DLL replacement,
Vortex hardlink isolation, preserving UserGraphics and cache, and repeat deployment.
A preflight RELEASE package verified 3,809 payloads, including the existing cache
and matching Streamline 2.10.3 sidecar components. Source shader ABI and module
versions are unchanged. Live runtime/GPU validation remains outstanding.

## Scope and review accounting

| Files | Purpose/dependencies | Correctness, security and changes | Fidelity/performance, rejected work and future validation |
|---|---|---|---|
| engine/Renderer/ExternalPostProcessing.cpp, .h | Optional ReShade registration, camera controls and callback mailbox | New active files; reviewed. Bounded local preset discovery on explicit refresh, no DLL loading. API acceptance required. Runtime pointers used only in callbacks; mutex mailbox separates UI. | No added pass or shader math. Reject force-loading/ENB preset overwrites. Test DX11 runtime lifetime and UI ordering. |
| extern/ReShade/include/*.hpp | Six unmodified upstream API declaration headers at pinned commit | Reviewed declarations consumed by bridge; license retained and bundled. No upstream implementation executes in PIXL. | API 20 only; no ImGui table sharing. Future: validate released host versions and maintain explicit version compatibility. |
| engine/XSEPlugin.cpp | SKSE initialization, ENB conflict, compiler handoff | Reviewed modified paths; early conflict fail and one-way handoff. Register optional bridge only after successful conflict-free Load. | No shader ABI changes. Future ENB ownership requires isolated architecture/test coverage. |
| engine/Menu/LaunchExperienceRenderer.cpp | Startup choices and restart notice | Reviewed UI -> serialized NR flag -> existing boot provisioner. Hardware/image-path gates and normal fallback. | No forced HDR changes or process spawning. Test NR/FG combinations and small displays. |
| engine/Menu/OverlayRenderer.cpp | Compiler progress/error presentation | Reviewed all background-flag consumers; post-startup failures stay in settings/log. | No compilation disabled; no per-frame logging added. Test travel without popups. |
| engine/Menu/PIXLRendererPage.cpp, engine/Modules/CameraSuite.cpp | Normal Camera page and advanced Camera controls | Reviewed integration calls; both expose same optional bridge. | No rendering changes. Verify preset selection on active camera page. |
| engine/Modules/GroundResponse.cpp, WindowLife.cpp | Feature settings | Reviewed scoped receiver flow; explanatory UI only. | Detection/math unchanged. Future asset-specific AE reports needed. |
| tools/StagePixlRendererStandalone.ps1 | Existing package creation/manifest | Adds compatibility guide and ReShade license. Existing cache/ABI validation retained. | No downloaded shader replacement. Verify archive integrity. |
| tools/DeployPixlRelease.ps1 | New manifest-driven deployment | Paths checked within Data, reparse paths rejected, old hardlinks moved to backups. User config/cache skipped. | Hash-verified selective replacement; no unrelated deletion. Backup manifest retained. |
| tools/TestPixlReleaseDeployment.ps1 | Disposable deployment fixtures | Tests hardlink and protected-state semantics, repeat run. | Fixture files never executed. Future: interrupted-deployment restore tooling. |
| README.md, distribution/PIXL-RENDERER-README.md, docs/MOD_COMPATIBILITY.md, THIRD_PARTY_NOTICES.md, extern/ReShade/README.md, LICENSE.md | User guidance, evidence and license | Updated/reviewed; unvalidated claims explicitly labeled. | No changes to intended visual defaults. Future live results should amend coverage. |

This is scoped per-file review of the update, not a new whole-repository audit.
The prior FidelityFX local build patch is retained unchanged.
