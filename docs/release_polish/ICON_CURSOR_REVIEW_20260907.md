# Icon and cursor asset review — September 7

Full source read: engine/Menu/IconLoader.cpp/h and CursorLoader.cpp/h. Shared Util::LoadTextureFromFile here is the stb UI loader, not the differently typed DDS/general loader in Utils/D3D.cpp. That other implementation remains separately pending. Related Menu initialization/deferred reload/shutdown and OverlayRenderer cursor call sites were traced, not complete containing-file reviews.

## Icon loader

Purpose/pipeline: load PNG-like decoded presentation assets into D3D11 RGBA8 SRVs with generated mips; ImGui uses them for branding. Actual definition list contains one tintable Brand/PIXL-Mark.png, not the many category/action assets described by the header's historical comments. Optional theme directory overrides the basename. Initialize/reload happens before active UI drawing; resources are released on replacement/Menu shutdown. No scene shader, GPU constant ABI, temporal history or hooks changed.

### Five investigations

1. **Optional override failure — implemented, controlled lifetime fix.** Existing code released the base icon before decoding a theme replacement. A malformed optional file lost working UI branding. Replacement now uses a temporary COM owner and size; transfers only after success. Expected result: retain correct base icon and size on failure. One extra temporary reference owner on reload, no frame cost. HIGH classification under the resource-lifetime rule, small controlled implementation with real WARP COM/upload tests.
2. **Decode boundary — implemented LOW.** stbi_info dimensions were checked but stbi_load may observe a changed file. Recheck decoded positive dimensions <=4096 before RGB masking or upload. Frees rejected data. This does not prevent allocation inside the third-party decoder itself; do not claim decompression-bomb immunity. Controlled oversized post-probe test passes without walking the undersized fake decoder allocation or issuing upload.
3. **Failure outputs — implemented LOW.** out SRV (if supplied) and size now initialize before argument validation. Missing device/path/output and rejected decode leave predictable null/zero state. Caller inventory confirms current callers use empty/replacement output slots, not borrowed live pointers. Tests cover null device/output, failed decode and oversize. No ordinary appearance change.
4. **Theme-only success — implemented LOW.** Initialize returned only base load success; a valid theme-only icon reported failure. Return now also checks resulting icon pointers after overrides. Real upload path with failed base/successful theme verifies success, both failing verifies false. Existing missing-icon text fallback remains.
5. **Mip/alpha/performance — investigated, preserved.** RGBA8 UNORM, generated mip chain and authored alpha are retained; tintable mark sets RGB white only. WARP readback verifies RGB/mask alpha in both mip levels. Removing mips or converting to sRGB changes filtering and appearance; no proof of benefit. Existing reload GPU flush/event wait may stall up to 1000 iterations, needs profiling/state tests before removal. No new render-loop work.

## Cursor loader

Purpose/pipeline: legacy Arrow migration → per-ImGui-type effective filename → theme/shared path resolution → canonical allowed-root check → shared UI texture loader → borrowed raw SRV array → foreground draw before ImGui::Render. Missing type retains default ImGui cursor behavior. Reload/Shutdown release each texture. Nine authored default slots; later ImGui cursor types may remain on built-in fallback. Custom cursors default according to theme, not forced on by this pass.

### Five investigations (no source change)

1. Migration only fills an empty Arrow role from legacy file/hotspot, preserving explicit per-type choices. No reason to rewrite schema; future round-trip fixture.
2. ResolvePath probes configured paths before IsPathAllowed; actual decode is gated by canonical Themes/Cursors roots. Exceptions during path probing are not locally caught. Do not relax this boundary or allow absolute external cursor assets.
3. Missing device logs once, deferred reload retries; missing optional images complete the reload rather than probing disk every frame. Releasing old cursors precedes device readiness, appropriate fallback needs device-loss tests before transactional change.
4. Active cursor index is bounded, raw references released by Shutdown, absent texture leaves built-in cursor. Header forward-declares Menu but uses nested ThemeSettings, depending on current PCH/include order; maintainability fix requires include-cycle check, not blind include insertion.
5. Scale/hotspot values can produce extreme draw bounds with malformed configurations; valid UI values and raster edge behavior need tests. Retained max texture bound comes from shared loader; up to nine 4096-square mip chains would be excessive. A cursor-specific budget/dedup cache needs real asset inventory, not arbitrary size restriction during release.

## File-by-file accounting

| File | Purpose/dependencies/quality | Problems/changes | Visual/performance/security/risk | Verification/future |
| --- | --- | --- | --- | --- |
| engine/Menu/IconLoader.cpp | Full; stb info/decode, native D3D11 upload/mips, fixed brand catalog, theme replacement; Menu/globals/path helpers | Four targeted fixes above; manual decode frees preserved | Better optional fallback, no scene effect/no per-frame cost; local asset loader only, controlled lifetime change | Native build and real WARP upload/COM tests; future decoder-fuzz corpus, exception RAII, precise asset memory accounting |
| engine/Menu/IconLoader.h | Full; optional initialization contract | Unmodified; description of many icons/monochrome fallback predates one-brand definition | No runtime capability change | Build; later synchronize documentation and prove unreachable monochrome branch before removal |
| engine/Menu/CursorLoader.cpp | Full; canonical path gate, migration, slot lifecycle and foreground cursor | Unmodified; findings above | No measured savings, no visual change; no network/process execution | Shared loader test applies to uploads; actual cursor UI/migration untested; future fault and scale tests |
| engine/Menu/CursorLoader.h | Full; public migration/reload/draw/shutdown contract | Unmodified; nested-type forward declaration relies on PCH | No ABI change | Production build; future self-contained header include check |
| tools/TestPixlIconLoader.ps1 | Full; exact production functions with controlled stb/Menu/path adapters and real D3D11 WARP | New test; unique ignored fixture with empty placeholder file for existence check, no game writes | No decoder security claim; actual GPU upload/mip readback and COM replacement exercised | /W4 /WX /O2 pass; initial link failed until matching project's windowsapp dependency added; no production flag changes |

Evidence: build/icon-tests-c611adf12f8d48ff98c1f1d3b22a5752/test.log, build/final-release-records/icon-fallback-build-20260907.log exit 0. WARP is software D3D11, not RTX 3060 Ti measurement. No source icons/images were edited or removed; no mod manager/load order changes.

## 5 Future Visual Improvements

1. High-DPI mark filtering; 2. cursor hotspot overlays; 3. alpha-edge reference images; 4. missing theme-asset feedback; 5. built-in cursor fallback coverage.

## 5 Future Performance Improvements

1. Measure reload GPU wait; 2. deduplicate same cursor file/scale; 3. cursor mip/resolution budget; 4. minimize unnecessary atlas/icon reload requests; 5. asset VRAM diagnostics.

## 5 Future Feature / Research Ideas

1. Real malformed-image corpus; 2. decoder/COM failure injection; 3. safe transactional asset sets; 4. self-contained UI headers; 5. package asset provenance and dimensions manifest.
