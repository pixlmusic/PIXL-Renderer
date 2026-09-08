# RC in-game test checklist

Not yet executed. Do not use an older staging checkpoint as proof that the current source is tested. Run after the final verified PIXL-only deployment and preserve its backup. Leave load order and unrelated mods unchanged.

Record exact DLL hash, Skyrim/SKSE versions, GPU/driver, resolution, preset, reconstruction/FG mode, scene/save, symptoms and matching log timestamp. Keep before/after camera position and weather comparable; separate shader-compilation stutter from steady-state frame times. No timing targets below are claimed measurements.

## Startup and persistence

- [ ] Launch through the normal SKSE/mod-manager path; confirm the PIXL DLL/version and expected 37 modules in the log.
- [ ] Confirm no missing assets, shader failures, invalid resource warnings or repeated exception logs.
- [ ] Check the foreground compiler panel remains responsive on first launch. Do not call a long clean-cache compile a hang solely because it is busy.
- [ ] Restart with the resulting cache; confirm expected reuse and unchanged image.
- [ ] Save/reload graphics settings, reload the same save, and restart Skyrim. Verify selections persist.
- [ ] Quit during/after a normal load and verify clean process exit. Test forced termination only if deliberately diagnosing a hang.

## Scene and motion coverage

| Scene/action | Check |
| --- | --- |
| Exterior daylight, dawn/dusk and night | Plausible indirect/direct balance, stable specular detail, coherent exposure, no extreme clipping |
| Interiors and dungeons | No exterior sky/fog leakage; local lights and dark corners remain readable |
| Dense vegetation and grass | World-space gusts, stable attachment points, no camera-relative wind or sparkling foliage |
| Rivers, lakes, shores and underwater | Flow seams, wave repetition, normal continuity, reflection fallback, refraction edges, caustics and contact foam |
| Rain, snow, fog and volumetrics | World-space precipitation, roofs/runoff, weather transitions, no flashing history or white-fog flattening |
| Snow and mud/ground response | Material-specific imprints, exclusions, persistence and recovery; no footprints on unsuitable geometry |
| Seasonal terrain swaps if already installed | Snow/mud classification follows active materials; no stale seasonal response after reload |
| Combat and magic | Moving characters, particles/transparency, blood effects if present, no reconstruction trails |
| Dialogue | Stable subject focus, skin/eyes, foreground occlusion, smooth return to gameplay camera |
| First-/third-person switches | Correct arms/equipment/actor masks and motion vectors; no persistent camera offset |
| Fast camera rotation and rapid movement | No excessive ghosting, shimmer, trails, disocclusion halos or displaced fine detail |
| Loading screens, fast travel and worldspace/interior transitions | Histories reset where needed; no stale water/GI/weather data or resource-growth trend |
| Resolution/window changes using existing supported controls | Resources recover; input/UI scale stays usable; no frozen frame or persistent black output |
| Photo Mode/Photo Finish if used | Capture completes; no recursive sharpening/ghosting; returning to gameplay restores camera/input/settings |

## Reconstruction and presentation matrix

- [ ] Native/DLAA where supported: steady edges, foliage and moving silhouettes.
- [ ] DLSS Quality, Balanced, Performance and any exposed extra mode: correct resolution/aspect and input alignment; compare motion and disocclusion.
- [ ] FSR modes exposed by the current build: equivalent output extent, stable exposure and reasonable edge quality.
- [ ] Frame generation off/on: stable UI/cursor, no doubled input handling, sensible frame pacing and clean transitions. Capture base-rendered frame time separately from displayed FPS.
- [ ] Unsupported-hardware states: disabled controls explain limitations and preserve a working reconstruction fallback.
- [ ] Sharpening minimum/default/high: no ringing, vegetation noise amplification or double sharpening.
- [ ] Exposure transitions: interior/exterior and bright/dark pans; no pumping, stuck exposure or FG brightness mismatch.
- [ ] Bloom: controlled highlights and no halos on dark silhouettes.
- [ ] DOF/dialogue focus: stable depth edges, no permanent gameplay blur, clean enable/disable.
- [ ] Reflex/latency controls: state persists, available modes match hardware/path, no repeated warning spam.
- [ ] Optional neural reconstruction: test only after its documented HIGH-risk compatibility disposition; verify ordinary DLSS remains intact when unavailable/failing.

## UI, presets and changes in this continuation

- [ ] With enhanced water SSR OFF, Reflection Balance and Water Tint remain editable and visibly affect their respective water lobes; SSR-specific sliders remain disabled.
- [ ] Toggle SSR back ON; no setting reset or broken slider bounds.
- [ ] Test every exposed quality preset at the same scene; confirm coherent underlying settings, persistence and computational scaling.
- [ ] Navigate keyboard/mouse/controller paths already supported; tooltips, focus, scrolling and scale remain usable.
- [ ] Optional developer-only hot reload: a guarded include used by one permutation must invalidate that entry file even after another permutation compiles. Preserve original source and do not ship a debug shader.
- [ ] Do not modify custom compile definitions on a live test installation merely to stress capacity; isolated native tests cover this safely outside Skyrim.

## Report failures

Provide the scene/save, reproduction steps, mode/preset, DLL hash, screenshot or short motion recording, and relevant PIXL log section. Black output, startup failure, corrupted settings, persistent ghosting or a major frame-time/VRAM regression blocks release. Restore only the PIXL-owned deployment backup if necessary; do not remove unrelated mods or change load order to conceal a regression.

After testing, wait for the owner's explicit approval before any commit, push, tag or publication.
