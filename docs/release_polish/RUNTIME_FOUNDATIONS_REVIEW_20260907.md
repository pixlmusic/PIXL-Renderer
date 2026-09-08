# Runtime foundation review — continuation evidence

This is a growing file-specific record, not an assertion that all foundation files or hooks have been reviewed. No startup/hook/seasonal ABI changes were made in this batch.

## Startup sequence traced

```text
SKSEPlugin_Load
  -> release logging / conflicting-host check
  -> SKSE::Init + trampoline allocation
  -> Load: ENB gate / compatibility checks / listener registration
     -> legacy state migration -> globals OnInit/ReInit -> locale/settings/theme
     -> required EngineFixes check -> early D3D hooks -> loaded-module Load
SKSE PostPostLoad
  -> Seasons discovery -> Deferred/renderer/engine-fix hooks
  -> frame annotations -> module PostPostLoad -> scene-event registration
  -> disk-cache validation -> optional file watcher
Engine render-target creation hook
  -> original target creation -> globals ReInit -> State::Setup
     -> core resources -> DialogueFocus -> typed-UAV capability probe
     -> module SetupResources -> actor late hooks -> Deferred resources
     -> weather and scene settings
SKSE DataLoaded
  -> late game globals -> seasons material index -> engine fixes/annotations
  -> nonblocking compilation gate -> module DataLoaded, once, on main task queue
Window close
  -> quit flag -> stop shader compilation
```

This establishes initialization/event dependencies, not the complete per-frame pass order. That remains open.

## Current file-specific findings

| File | Review / purpose | Correctness, integration and security findings | Performance / fidelity / future possibilities | Changes / validation |
| --- | --- | --- | --- | --- |
| engine/XSEPlugin.cpp | Full source read; exported SKSE entry points and startup events | Listener registration return/interface is unchecked. Detached compilation waiter relies on singleton lifetime/quit coordination. ENB and conflicting-module gates are intentional, not dead code. LoadLibrary of EngineFixes is an explicit requirement. | Foreground compile gate preserves message/frame pumping. Retain one-shot DataLoaded guard; test quit during compilation and task-interface failure. Address Library declarations do not prove all runtime offsets compatible. | None. Local exe is 1.5.97; matching SKSE DLL and version-1-5-97-0.bin exist; EngineFixes exists. No game launch performed. |
| engine/Globals.cpp | Full source read; module storage, borrowed game/D3D pointers, Map/Unmap hooks | HIGH follow-up: mappedFrameBuffer stores the caller's D3D11_MAPPED_SUBRESOURCE pointer between Map and Unmap. Its lifetime depends on caller stack lifetime; an owned copy would remove that assumption. ReInit dereferences engine singletons directly. Do not patch hook contracts without targeted runtime validation. | FrameBuffer snapshot avoids repeated engine queries; confirm per-context/thread assumptions and resize handling. Empty llf namespace is harmless, not worth a rename pass. | None; referenced setup/close callsites inspected, no new hook installed. |
| engine/Globals.h | Full: module/global declarations and camera-buffer structure, including remaining accessors | Frame data contains ten matrices and five float4 values. Borrowed pointers and mapped-buffer storage share the lifetime concerns recorded for Globals.cpp. | Verify layout against actual engine buffer and runtime variant, not merely header comments. | None; source reviewed, runtime layout not independently captured. |
| engine/PCH.cpp | Full; EASTL allocation shims | Alignment-taking overload ignores requested alignment/offset. Current explicit alignas scan found 16-byte types, not proof no over-aligned external container instantiation exists. Do not mix aligned allocation/free conventions casually. | Future allocator contract test for >16-byte alignment; no visual change intended. | None. Built with current toolchain. |
| engine/PipelineBuffer.cpp | Full; packed module settings serialization | Trivially-copyable pack assertion and thread-local non-owning buffer are retained. Hair 128-byte reservation is intentionally preserved. Every module field still needs separate CPU/HLSL offset validation. | No per-update allocation in this packer. Future per-block offset/size assertions without reordering. | None; built; WaterOptics 64-byte field order checked separately. |
| engine/PipelineBuffer.h | Full; non-owning buffer interface | Comment names a_early while implementation names a_inWorld; stale semantic naming, no ABI difference. Caller must consume before next pack on same thread. | Clarify documentation with complete callers; do not introduce an owning allocation. | None; two State callsites found. |
| engine/Hooks.h | Full declaration surface only | Identifies shader selection, dirty states, immediate pass and early D3D hooks; declarations alone do not validate hook sites. | Trace full Hooks.cpp before any address change. | None; implementation review incomplete. |
| engine/EngineFix.cpp | Full registry implementation | Three post-post-load fix objects, empty DataLoaded list, intentional retained extension surface. No removal justified. Individual patch implementations still require review. | One-time registration; no meaningful steady-state allocation issue here. | None; compiled. |
| engine/EngineFix.h | Full virtual registry interface | Virtual destructor and static-list ownership are appropriate; Install default is inert. | Keep common interface; no cosmetic rewrite. | None; compiled. |
| engine/ModuleRules.cpp | Full; UI constraints queries/tooltips | Conflict branch says log once but has no rate limit. No GetActiveConstraints override found in current module sources, so conflict production is not established. First-wins semantics and type-sensitive variant comparison are retained. | Potential repeated full-module scans/allocations in UI; measure before caching constraints with invalidation risks. No proven active producer does not justify deleting consumers. | None; compiled, reference search completed. |
| engine/ModuleRules.h | Full; constraint DTOs | Explicit false default for isConstrained, variant value-initialization and bounded source loops. English tooltip strings need future localization; no credential/process/network behavior. | Prefer typed settings contracts if producers are added. | None. |
| engine/ModuleGroups.h | Full; UI category constants | Stable string_view literals with static lifetime. | Localize presentation separately from stable grouping keys; renaming would risk UI grouping for no rendering benefit. | None. |
| engine/SeasonIntegration.cpp | Full; optional provider/index implementation | HIGH follow-up: GetContext loads generation then separate relaxed fields; this provides visibility of earlier writes but not an indivisible snapshot during a new publication. Provider discovery accepts any loaded GetCurrentSeason export and borrows module lifetime. Poll schedule fields rely on game-thread call discipline. | Existing 1-second poll/30-second rediscovery and index swap are bounded. Preserve runtime-resolved TXST classification; no hard dependency on season mods. Consider one packed atomic publication after concurrency tests. | None; source/callsite review only, season transitions untested. |
| engine/SeasonIntegration.h | Full; snapshot/provider/index interfaces | Mutex/shared-mutex ownership clear, refresh flag atomic. See publication concern above. No automatic loading/unloading or persistence. | Per-frame readers should remain lock-free only with coherent snapshot semantics. | None. |

## Five priority investigations for continued foundation work

1. Main-thread finalization and quit lifetime; 2. Map/Unmap caller-storage lifetime; 3. coherent season snapshots; 4. repeated core SetupResources ownership; 5. listener/hook failure reporting. These are findings and future verification tasks, not claims of five completed fixes.

## 5 Future Visual Improvements

1. Camera-buffer layout regression capture; 2. seasonal material transition captures; 3. lighting initialization tests; 4. loader/resize history checks; 5. inspect neutral resource failure output.

## 5 Future Performance Improvements

1. Measure UI constraint traversal; 2. preserve nonallocating FeatureData packing; 3. coherent compact season publication; 4. profile resource recreation frequency; 5. measure startup compiler gate responsiveness.

## 5 Future Feature / Research Ideas

1. Automated startup dependency preflight; 2. runtime relocation diagnostics; 3. owned frame-map snapshot tests; 4. lifetime-aware game shutdown harness; 5. per-module capability/fallback reporting.
