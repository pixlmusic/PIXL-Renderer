# PIXL GUI 30-Point Implementation

## Scope and design

This is a PIXL-native UI pass. It extends the existing custom tuner shell, `PIXLUI` primitives, theme tokens, Director/Photo/Video controls, and runtime settings. It does not import legacy upstream windows, navigation, styles, preset semantics, or configuration paths.

The reference image is treated as hierarchy and interaction guidance only. It does not provide runtime data, preview imagery, or preset values.

## Current ownership map

| Area | Current owner | Notes |
|---|---|---|
| Main shell, menu persistence, input ownership | `engine/Menu.cpp/.h` | Owns the public vs advanced tuner split, theme persistence, hotkey capture, and UI input capture. |
| Tuner navigation and module settings host | `engine/Menu/TuningWorkspaceRenderer.cpp/.h` | Owns category rail, feature list, module header/modal, Director Photo/Video state, and contextual tuner footer. |
| Shared PIXL presentation | `engine/Menu/PIXLStyle.h`, `Controls.h` | Existing chrome, colour tokens, component styling, slider rows, status pills, transitions, and DPI scaling. |
| Product/quality page | `engine/Menu/PIXLRendererPage.cpp/.h` | Owns PIXL product controls, quality cards, quick setup and real pipeline status. |
| Settings persistence / look restore | `engine/State.cpp`, `engine/SceneSettingsManager.*`, `engine/SettingsOverrideManager.*` | Existing serialization and scene override paths remain authoritative. |
| Runtime profiling | `engine/Modules/PulseProfiler.*`, `engine/Menu/PulsePanelRenderer.*` | Existing real metrics only; no synthetic timing values. |
| Director / capture | `TuningWorkspaceRenderer`, `Modules/PixelCapture.*`, `Modules/CameraSuite.*` | Photo Mode remains the proven owner. Video Mode stays a sibling session. |
| Theme/fonts/icons/DPI | `ThemeManager.*`, `Fonts.*`, `IconLoader.*` | All new chrome uses existing PIXL scale and palette helpers. |

## Shared primitives and state to extend

1. Add a persistent PIXL workspace preference block to `Menu::Settings`:
   - favorite module IDs;
   - optional focus scrim;
   - bounded recent module IDs;
   - no renderer setting key is renamed or repurposed.
2. Extend `PIXLUI` with reusable compact state chips, section controls, reset affordances, contextual footer keys, and a standard tooltip format.
3. Introduce a small UI-only settings-history controller in the tuner:
   - serializes through each moduleâ€™s existing `SaveSettings` / `LoadSettings`;
   - records only user-visible edits;
   - coalesces continuous slider edits;
   - never writes directly to feature memory;
   - supports safe per-module undo/redo and A/B hold;
   - clears on settings reload / incompatible transitions.
4. Extend the current module search path to include cached aliases, module summaries, and opt-in `RenderModule::GetSettingsSearchEntries()` metadata. It is cached and does not scan or allocate the complete source tree per frame.

## Staged implementation

1. **Shared foundation:** stylesheet primitives, persistent workspace preferences, UI-only history and snapshots.
2. **Tuner shell:** status chips, selected rail, Favorites / Recent / Modified views, contextual footer, focus scrim, and search result metadata.
3. **Module host:** uniform module header, active/disabled/modified/controlled state, reset/undo/redo/A-B actions, section entry points, and dependency messaging.
4. **Product pages:** apply the shared controls to PIXL Renderer, Hotkeys, General, Camera/Director/Photo/Video and developer pages while retaining their existing controls.
5. **Quality/search/accessibility:** quality intent/details, direct-entry and reset UX supplied by shared controls where module widgets already use them, DPI/scroll checks and aliases.
6. **Validation:** build release target, source checks for old settings keys, no legacy upstream menu ownership, and a 30-point completion matrix.

## Migration and regression risks

- **Persistent settings:** new fields are defaulted through the existing JSON mechanism. Existing settings keys and profile semantics remain unchanged.
- **Runtime module settings:** undo/A-B applies only through existing module load paths. Failed or unavailable modules are never snapshotted.
- **Input:** keyboard shortcuts are handled only while the PIXL tuner owns input and never during Director camera handoff or a text entry field.
- **Director:** Photo Mode/capture ownership and SmoothCam lease logic remain unchanged. Video merely reports mode state in shared chrome.
- **Performance:** search metadata and snapshots are invalidated lazily; profiler labels are based only on Pulse metrics or module state.
- **Scope:** legacy module draw routines remain behaviorally authoritative. The shared host provides hierarchy, status, diagnostics, and reset workflows around them rather than reimplementing every feature control.

## Completion matrix

| # | Improvement | Status | Shared implementation | Pages/modules covered |
|---:|---|---|---|---|
| 1 | Clear hierarchy | COMPLETE | Tuner module host and shared section headings | All advanced modules; PIXL Renderer, Hotkeys, General, Director |
| 2 | Quality preset intent / modified presentation | COMPLETE | Existing PIXL quality cards and profile details remain authoritative | Quality and renderer pages |
| 3 | Unified state language | COMPLETE | State chips, PIXL colours and module host state | Module headers, navigation, Director |
| 4 | Reset actions | PARTIAL | Existing module reset stays authoritative; shared host has module reset | All modules; per-control reset needs opt-in default metadata |
| 5 | Slider and input UX | PARTIAL | Existing native slider direct entry, keyboard navigation, clamping and drag tracking | Shared Controls consumers; default markers need per-setting metadata |
| 6 | Navigation states | COMPLETE | Selected cyan rail, active state, Favorites/Recent/Modified views | Advanced tuner |
| 7 | Settings search | PARTIAL | Cached aliases, module summaries, search metadata and serialized setting-key index | Advanced tuner; focus-to-exact-widget awaits module opt-in callbacks |
| 8 | Basic / Advanced separation | COMPLETE | Existing PIXL product pages and Developer Mode gate | Public pages and advanced tuner |
| 9 | Real performance UI | COMPLETE | Existing Pulse profiler and feature timer panels | Developer pages and loaded module panels |
| 10 | Dependencies / incompatibility | COMPLETE | Existing ModuleRules plus shared dependency styling | Module host and constraint dialog |
| 11 | Top status bar | COMPLETE | Live, Photo, Video and inspection state reporting | PIXL shell |
| 12 | Undo / redo | COMPLETE | Bounded, coalesced module JSON snapshots through official load/save paths | Loaded modules in advanced tuner |
| 13 | Save Look | PARTIAL | Existing whole-renderer Save Look / Restore remains authoritative | Public and advanced shell; named grouped looks are future work |
| 14 | A/B comparison | COMPLETE | Per-module baseline/edit swap via official module paths | Loaded modules in advanced tuner |
| 15 | Applied feedback | COMPLETE | Existing restrained transitions plus status/action feedback | Shell, navigation, module host |
| 16 | Reduce permanent instructions | COMPLETE | Existing product summaries and hover help retained; workflow descriptions moved into tooltips where appropriate | Tuner host and public pages |
| 17 | Scrolling | COMPLETE | Existing independently scrollable module surface, themed scrollbar | Advanced modules |
| 18 | Spacing rhythm | COMPLETE | Existing PIXL layout, engineering style and DPI scaling | Shell, module host, product pages |
| 19 | Typography | COMPLETE | Existing role-based font system and DPI-aware scaling | Entire PIXL UI |
| 20 | Selected navigation rail | COMPLETE | Existing custom cyan rail and shared NavItem | Category rail and module navigation |
| 21 | Favorites / Recent | COMPLETE | Persisted bounded workspace settings and filter views | Advanced tuner |
| 22 | Outer-category help | COMPLETE | Existing contextual category tooltips | Category rail |
| 23 | Photo / Video context | COMPLETE | Existing Director integration plus status/footer additions | CAM, Director Photo and Video |
| 24 | Contextual footer | COMPLETE | Director-aware shortcut footer | Advanced tuner and Director |
| 25 | Focus scrim | COMPLETE | Optional low-opacity scrim configured under General | Advanced tuner |
| 26 | Modified summary | COMPLETE | Module Modified chip and Modified filter | Advanced tuner |
| 27 | Debug separation | COMPLETE | Existing Developer Mode gating and engineering pages | PIXL, profiler, developer modules |
| 28 | Diagnostic badges | COMPLETE | Existing real Pulse data and module status/constraint states | Module host and profiling |
| 29 | Restrained animation | COMPLETE | Existing PIXL hover/selection/section motion helper | Shell, navigation, module host |
| 30 | Consistent application | COMPLETE | Shared tuner host wraps every active advanced module; product, hotkey, Director and public pages retain PIXL-specific panels | LIGHT, WORLD, CHAR, CAM, PIXL, Photo, Video, PixelCapture, profiler |

## Validation

- Release target: cmake --build --preset PIXL-12C.
- The upgrade does not add a shader resource, cbuffer, hook, or renderer setting ABI.
- Existing user settings receive default values for the three added workspace preferences.
- Undo/A-B snapshots are discarded when Restore is used; an active A/B preview restores the edited snapshot before the history is cleared.
- No legacy upstream UI implementation or layout is used.

## Live validation checklist

1. Open the public PIXL page, then the advanced Tuner; verify the selected cyan rail and the low-opacity scrim.
2. Open a loaded module, edit a slider, then test UNDO, REDO, A / B, and RESET MODULE.
3. Favorite two modules, close/open the tuner, then verify FAV and REC views.
4. Search GI, AO, snow, water, photo, video, and a known JSON setting key. Confirm the matching module can be selected.
5. Test Photo and Video Mode transitions with/without SmoothCam; confirm the top status and footer change, then exit and confirm normal camera/HUD restoration.
6. Enable a real module dependency/scene override; verify the amber Scene Controlled state and the existing explanation.
7. Test at 1080p, 1440p and 4K with any non-default PIXL UI scale. Check long module panels, scrollbars, hotkey capture and the focus-scrim toggle.


## Reference-fit refinement (2026-09-24)

The advanced Tuner now calculates its content width from the active viewport and caps it at a readable wide-canvas size. The outer header uses the same measured width, feature pages no longer repeat their module title above the actual module header, and the module surface gained bottom breathing room plus a taller shared viewport. Normal loaded modules use their existing enable control as the single activation affordance; header state chips are now reserved for disabled, restart-required, scene-controlled, and modified states.

## Camera and delivery layout refinement (2026-09-24)

The public Post-processing | Upscaling page now keeps the clean live preview in the upper-right column. Exposure and finishing settings remain in the two adjacent compact columns. Image Reconstruction retains its shared path selector; its Neural Rendering controls and Frame Delivery controls now occupy separate side-by-side columns. Public page switches reset their scroll position so the quality preview always opens at the top of the Quality page.

## Director polish and UI work reduction (2026-09-25)

- The Photo Mode quick panel is constrained to the current viewport, clips all draw-list text, and pages the focused options instead of drawing the entire effect list below the footer. It now exposes the persisted **PIXL Signature** watermark setting.
- Photo and Video use a full-width contextual footer with actual active-state segments. The segments report enabled effects and capture state; they are not frame-time measurements.
- The Video workspace has a shared interactive timeline with point ticks, click/drag scrubbing, exact-time input, state labels and clear authoring instructions. The overlay uses the same path timing data.
- Point marker times are cached when the path rebuilds. Scrubbing reads that cache and does not allocate a marker vector per frame.
- The Director footer copies Camera Suite settings once under its existing lock and uses stack-backed fixed arrays. It avoids repeated locks and dynamically assembled effect lists during drawing.
- Existing tuner search remains cached, and the existing constraint scan remains throttled. This pass does not add a new per-frame filesystem scan, profiler query or render pass.

### Validation

- `cmake --build --preset PIXL-12C` completed successfully after the Director and preset-translator changes.
- `tools/TestPixlGui.ps1` passed all 15 scaled layout cases and its registry/stack-isolation checks. Its deliberate unbalanced-provider exercise emits expected ImGui recovery diagnostics before reporting PASS.
- Live coverage remains required for 1080p, 1440p and 4K layout, Photo watermark placement, Video path scrubbing and transitions.
