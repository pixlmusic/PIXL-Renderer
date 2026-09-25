# PIXL preset translator

## Scope

The experimental **PIXL Preset Translator** reads a detected `enbseries.ini` and, when present, its companion `enbeffect.fx.ini`. It translates a bounded list of colour, exposure, adaptation, tone and bloom values into Camera Suite through the normal PIXL settings path.

It does not load an external renderer, compile an imported `.fx` file, load a proxy DLL, or write to the detected preset.

## Discovery

Refresh performs one bounded, on-demand scan below the active Skyrim executable folder. It ignores links, game data, caches, logs, screenshots and other volatile or large roots, has depth and candidate limits, and accepts only regular configuration files up to 4 MiB. The discovered path is shown in the control tooltip.

## Supported translation

- `COLORCORRECTION`: brightness and gamma curve
- `ADAPTATION`: sensitivity
- `ENVIRONMENT`: day direct-light intensity, curve and desaturation
- `SKYLIGHTING`: day ambient minimum
- `BLOOM`: day amount
- `enbeffect.fx.ini`: established brightness, curve, contrast, saturation, Cabbage/LUX HDR and Kitsuune tone-map keys

Values are clamped to Camera Suite's normal ranges. Per-weather source values are not replayed: where an effect shader advertises weather separation, PIXL imports only the stable base/day values it can represent without overriding PIXL weather handling.

## Effect annotations

`UIName`, `UIGroup`, widgets, bindings, compile-time definitions and `Separation` are owned by the original ENB effect compiler and GUI. PIXL detects annotation presence only as useful preset metadata. It cannot execute those source shaders while PIXL owns Skyrim's DX11 renderer. The UI makes this boundary explicit so an import is predictable.

## Applying and restoring

Selecting a preset is inert. **Apply translated PIXL style** first snapshots the current PIXL Camera Suite state, then applies the translation through `CameraSuite::ApplyExternalLook`, updates HDR data and saves PIXL's own settings. **Restore before import** restores that snapshot through the same pathway. Imported files are never modified.

## Live validation

1. With ENB itself disabled, place a preset under the Skyrim installation and refresh the library.
2. Confirm its relative label, full-path tooltip and detected source metadata.
3. Apply it and verify exposure, contrast, saturation and bloom respond without loading an external module.
4. Change a PIXL camera setting, then verify restore behavior intentionally returns to the pre-import snapshot.
5. Test a preset with an `enbeffect.fx` UI annotation and weather-separated source; verify the UI calls out the base/day-only import.
