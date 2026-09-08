# Diagnostic and filesystem review — September 7

Full source: engine/PipelineHealth.cpp/h and engine/Utils/FileSystem.cpp/h. Related override error producer, theme delete UI, DLL folder UI and Workshop call sites were traced, not their complete containing files. Existing issue/Workshop functionality is active; WorkshopToolsRenderer invokes testing UI behind IsDeveloperMode. No live diagnostic create/delete/restore action was executed during this audit.

## Pipeline Health architecture

RenderModule load failure / orphan catalog scan / SettingsOverrideManager error → issue vector + file metadata → grouped overlay rows and confirmation popup → explicit user cleanup → deferred issue removal. Workshop separately creates test descriptors, persists original versions and restores them. Inputs are registered modules, descriptor/version data, override errors and local files; outputs are diagnostics, user-requested filesystem actions and UI navigation. No scene pass, HLSL, history, frame generation or CPU/GPU ABI. Cached module singleton pointers assume fixed registry lifetime. UI/D3D ownership remains Menu's responsibility.

## Five major investigations

1. **Cleanup boundary — LOW correction, HIGH consequence if absent.** An orphan `..ini` has stem `.` and previously resolved its module directory to the entire shader root. Cached fileInfo paths also went directly to SafeDelete. Module lookup now rejects non-identifier names; deletion validates all targets before either action, reconstructs the exact descriptor/kernel target and rejects reparse redirects/errors/root paths. Override errors are a distinct legitimate contract: hasINI means `<Overrides>/<mod>_<feature>.json`. This path is independently checked, permits mod names with spaces/dots and never permits a shader directory. Expected result: prevent accidental broad or redirected cleanup; negligible button-time work, no image impact. Modified PipelineHealth.cpp; exact-source tests use recorded deletion calls, not real payload removal.
2. **Issue-list lifetime — HIGH (lifetime), controlled fix.** DrawPipelineHealthUI collects pointers into the issue vector. The popup erased that vector immediately, invalidating later rows; its remove_if predicate also captured an element that could move. Popup now copies the successful key and removal occurs after all categorized rows have finished. No per-frame snapshot of every issue; only a string for an actual deletion. Full native build and source-contract/deferred-erase regression pass. Real ImGui popup navigation still requires testing.
3. **Workshop restoration correctness — LOW.** Original absent version was restored by deleting `[Info] Version` although test creation changed `[PIXL Module] Version`. Corrected section. Normal version values unchanged. Exact restore function with recording INI implementation verifies both absent and explicit versions; actual INI parser/file encoding remains an integration test.
4. **Persistent recovery boundary — LOW correction.** Test state contains filenames; restore now validates identifier and exact non-redirected catalog path before reading/writing/removing. Failure no longer discards recovery metadata: all records remain available for retry until complete success. No external path action in tests; simulated locked/load failure and outside target retain state. Not a transactional journal: creation still writes files before SavePersistentTestState, and concurrent file substitution remains possible. Avoid expanding Workshop operations.
5. **Diagnostic overhead/legacy cleanup — investigated, retained.** Group vectors allocate while issue UI is drawn, metadata scans recurse per issue, Workshop refresh can parse tracking state multiple times per frame. No measured cost or gameplay-loop proof justifies cache redesign. Legacy obsolete-feature map remains used by scanner and issue guidance; not deleted based on historical names. Public wording says install separate integrated features in places: needs localization/UI follow-up, not a rendering rewrite.

## Filesystem utility investigations

1. SafeDelete existence check was outside its filesystem_error handler; moved inside so inaccessible/malformed paths produce a failure result. It remains a generic helper, not a universal ownership policy. Pipeline cleanup supplies its new boundary; theme cleanup uses a discovered theme file and needs broader race/symlink policy review.
2. SanitizeFileName handled `CON` but not `CON.txt`. It now checks the base before the first extension and inserts `_` before that extension; old no-extension behavior (`CON_`) is preserved. Native tests cover CON/NUL/COM/LPT, ordinary mod filenames, invalid characters and empty whitespace. No new path or process access.
3. PathHelpers uses game executable location rather than cwd by default, preserves MO2 physical-root helpers and current PIXL runtime layout. GetRealPathFromDataRelative is not a security validator (absolute/parent components accepted); current inspected caller opens a configured DLL folder. Do not reuse it as a destructive boundary. Long-path fallback and failed physical-root cache remain future work.
4. Legacy migration copies only fixed known state mappings and skips already existing destinations; no source deletion. Entire destination-folder existence can skip missing individual legacy children, and interrupted partial copies are not transactional. Avoid changing migration precedence during release without fixtures/user-state comparison.
5. JSON diff catches parse/per-change exceptions and filters small numeric changes; error/empty-diff ambiguity, large-file bounds and sequential array JSON-patch indexes deserve regression cases. No measured performance claim; no serializer behavior changed here.

## File-by-file record

| Path | Purpose/dependencies/quality | Problems and changes | Implications/risk | Validation and future |
| --- | --- | --- | --- | --- |
| engine/PipelineHealth.cpp | Full; diagnostic registry, filesystem metadata/UI/Workshop; RenderModule, Menu, I18n, SimpleIni, Windows/filesystem | Exact-target gates, invalid identifier guard, deferred erase, correct restore section and retained failure metadata | No scene effect; extra work only on cleanup; HIGH pointer-lifetime fix, otherwise LOW corrections. Existing user-button ShellExecute opens directories/logs/support links, not an updater | Native builds and exact-source tests pass. UI, MO2 deletion behavior and failure-injection remain; unknown folder ownership is still inferred from its name and explicit user confirmation, not a signed ownership manifest |
| engine/PipelineHealth.h | Full; issue/file/test records, public queries and developer tooling API | Unmodified; obsolete field/comment names and unchecked external API calls retained | No new security behavior/ABI; caller lifetime contract important | Build passes; future initialize latestTimestamp in default records and document returned vector invalidation |
| engine/Utils/FileSystem.cpp | Full; path layout/migration, generic deletion, filename sanitation, JSON comparison | Guard filesystem probe exceptions; extension-aware device-name sanitation | LOW, no visual behavior; no per-frame allocation added. Existing migration/file writes retained | Native tests cover sanitizer, build covers helper change; future transactional migration/save and UTF-8-safe filename truncation |
| engine/Utils/FileSystem.h | Full; utility/path declarations, inline DLL version enumeration | Unmodified; declared paths partly reflect older layout | No extra capability; existing local DLL metadata reads | Implementation now traced; dynamic callback/format helpers outside this file still need full audit |
| tools/TestPixlDiagnosticSafety.ps1 | Full; exact-source functions/records with recording delete/INI adapters, real Win32 path attributes and isolated junction | New regression coverage; no live game operations, no network | Unique ignored build fixture retained, including junction to its own sibling fixture; no cleanup/deletion of payloads | /W4 /WX /O2 passed. First extraction regex and missing test namespace brace failed before running; fixed harness, no production suppression |

Latest tests: build/diagnostic-tests-690ce9d8d77d4c7fb5c52b8e687aaefa/test.log. Production diagnostic/override changes: build/final-release-records/diagnostic-override-build-20260907.log, exit 0. Subsequent filename change is recorded by filename-safety-build-20260907.log; final result belongs in CURRENT_PASS_STATUS.md. The test executes the actual restore function with mock INI operations; successful cleanup attempts removal of a deliberately nonexistent fixture state file only. It does not simulate arbitrary ImGui interaction or execute production SafeDelete.

Remaining security limitations: path checks are not handle-based/atomic against malicious concurrent replacement; generic theme deletion has no new root policy; name-derived unknown shader-directory ownership should move to package manifests or mod-manager cleanup in future. No claim of global filesystem/security safety is made.

## 5 Future Visual Improvements

1. Accurate integrated-module guidance; 2. accessible issue grouping; 3. clear invalid-target UI feedback; 4. locale-complete cleanup labels; 5. stale/missing asset status visualization.

## 5 Future Performance Improvements

1. Profile issue grouping allocations; 2. cache metadata by file change; 3. Workshop state refresh throttling; 4. bound recursive diagnostics; 5. coherent immutable issue snapshots if worker producers are introduced.

## 5 Future Feature / Research Ideas

1. Manifest-owned cleanup; 2. recoverable quarantine instead of deletion; 3. transactional Workshop journal; 4. injected disk/parser failures; 5. MO2/Vortex fixture coverage.
