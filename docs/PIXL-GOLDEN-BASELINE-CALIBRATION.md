# PIXL Golden Baseline Calibration

The installer baseline is a renderer-authored look, not a copy of a player's
mutable `UserGraphics.json`. Start each pass from `PIXL-Golden-Baseline.json`,
change only the three shipped pages, and promote the final settings into both
`RendererDefaults.json` and `PIXL-Golden-Baseline.json` after the matrix passes.

Linear Light Core and Atmosphere remain disabled for this baseline.

## Scene matrix

| Condition | Location / scene | What to protect |
|---|---|---|
| Clear exterior, midday | Whiterun plains looking toward the city | Grass/material colour, sunlit stone, distant terrain stability |
| Clear exterior, sunrise/sunset | The Guardian Stones overlooking Lake Ilinalta | Sky gradient, silhouette separation, water reflections |
| Snow, daylight | Winterhold bridge and college courtyard | Snow highlight detail, blue ambient light, fog layering |
| Snowstorm / flat light | Road south of Dawnstar | White-on-white separation, temporal stability, particle brightness |
| Dense forest | Falkreath forest near Pinewatch | Canopy bounce, deep shadow readability, foliage shimmer |
| Waterfall / mist | Riverwood falls | Smoke/mist integration, water caustics, bright foam clipping |
| Large city night | Solitude market and archway | Warm lamps against cool ambient, metal response, shadow detail |
| Small settlement night | Riften docks | Wet wood, water reflection continuity, local-light falloff |
| Dark stone interior | Bleak Falls Barrow entrance halls | Torch smoke, contact shadows, readable black level |
| Warm fire interior | Bannered Mare main room | SkinOptics, fire particles, wood roughness, exposure recovery |
| Mixed magic lighting | College of Winterhold Hall of the Elements | Saturated effects, emissive restraint, specular colour |
| Dwemer interior | Blackreach or Alftand | Bronze metal classification, cyan emissives, large-scale darkness |
| Cave with bioluminescence | Blackreach fungal areas | Coloured bounce, emissive detail, fog without washout |
| Character close-up | RaceMenu or dialogue under neutral daylight | SkinOptics diffusion, hair highlights, eye specular, stable exposure |
| Weather transition | Whiterun exterior through clear-to-rain transition | Adaptation speed, no sudden GI/exposure discontinuity |

## Capture discipline

- Use the same save, camera position, time, weather and display mode for each pass.
- Capture Composite plus the PIXL diagnostic set before and after adjustments.
- Change one page/category at a time; do not tune hidden feature internals.
- Reject a change if it improves one scene by breaking another scene in the same condition class.
- Record average and p95 GPU time with the visual captures.
- The final baseline must pass the entire matrix twice: once from a cold shader library and once warm.

## Promotion

1. Save the validated renderer state.
2. Copy the feature/menu settings into
   `distribution/SKSE/Plugins/PIXLRenderer/SettingsDefault.json`.
3. Copy the identical JSON into
   `distribution/SKSE/Plugins/PIXLRenderer/Presets/PIXL-Renderer-Live-Tested.json`.
4. Stage a clean package; these become `RendererDefaults.json` and
   `Profiles/PIXL-Golden-Baseline.json` in the player build.
5. Run `tools/AuditPixlRenderer.ps1` and archive the matrix captures with the build hash.
