# PIXL Renderer season compatibility

## Architecture

PIXL treats season and surface identity as separate kinds of information:

- Seasons of Skyrim is an optional context provider. When its public native API is present, PIXL reads the authoritative current season, including custom month maps, permanent seasons, and overrides.
- Skyrim's resolved runtime landscape material remains the surface authority. Winter does not automatically mean snow, and no other season automatically implies mud, rain, or dryness.
- Ground Response consumes the resolved seasonal texture set and, where available, the land-texture record that owns it. This keeps classification compatible with Turn of the Seasons, other season packs, landscape replacers, and modded worldspaces without hard-coded plugin FormIDs.

The integration is intentionally isolated in `SeasonIntegration`. Ground Response does not call third-party DLLs directly, and the render thread never queries the provider.

## Provider detection

At SKSE `PostPostLoad`, PIXL enumerates modules that are already loaded in the Skyrim process and looks for the documented read-only Seasons of Skyrim export `GetCurrentSeason`. It never loads a Seasons DLL, links an import library, or calls an override mutation API.

Detection is capability-based rather than version-based. The provider DLL may be renamed without breaking the bridge. Discovery is retried at `DataLoaded` and on game-state refreshes so unusual plugin load order remains fail-safe.

Known API values are mapped as follows:

| Native value | PIXL value |
| ---: | --- |
| 0 | None |
| 1 | Winter |
| 2 | Spring |
| 3 | Summer |
| 4 | Autumn |

Unknown future values are treated as `None/Unknown` for diagnostics. They never crash PIXL or force a material classification.

## Fallback behavior

If Seasons of Skyrim is absent, its exports cannot be found, or the provider returns an unknown value:

1. PIXL remains independently loadable.
2. Ground Response continues using its established material-driven classification.
3. No shader define, constant-buffer field, resource, pass, or user setting changes.
4. No season-based snow or weather assumption is introduced.

Seasonal assets can therefore still work through resolved-material evidence even if the native context API is unavailable.

## Resolved material classification

Seasons of Skyrim applies landscape substitutions before Skyrim creates the final landscape shader material. It also records the replacement texture-set FormID in the original texture set's runtime `pad12C` field. PIXL already used that post-swap texture set in Material Forge and Terrain Field, and captured the final landscape material's authored `textureIsSnow` flags.

The compatibility layer extends that same resolution to Ground Response's CPU contact path:

1. Start with the land texture returned by Skyrim at the contact point.
2. Resolve its current texture set through PIXL's existing `GetSeasonalSwap` helper.
3. Find the loaded land-texture record that owns the resolved texture set when one exists.
4. Classify the resolved diffuse path, resolved physical material, and renderer-observed snow flag.
5. Use original land material data only when no active seasonal swap exists.

This distinction prevents both stale directions of error: original dirt remaining dirt after a Winter snow substitution, and original snow remaining snow after a Spring/Summer thaw substitution.

## Cache and history invalidation

`SeasonIntegration` publishes a tiny atomic snapshot containing provider availability, season, status, and a monotonically increasing generation. Provider polling occurs at low frequency on PIXL's existing game/main-thread Ground Response update hook.

When the generation changes, PIXL:

- clears Ground Response's renderer-derived texture classification cache;
- invalidates Terrain Field's material-hash-to-seasonal-parallax cache;
- schedules a one-time render-thread clear of the snow/mud deformation, displaced-snow, and elemental history fields.

The history clear is deliberate. PIXL's shared deformation field stores compaction rather than the material category that created it, so retaining it across an incompatible snow-to-soil or soil-to-snow transition could reinterpret old tracks as a different material. The clear occurs only on a real season generation or explicit game-state refresh, then newly classified contacts can accumulate normally.

No per-frame material-cache flush, filesystem scan, full-screen pass, or GPU season branch is added.

## Lifecycle behavior

- Initialization: provider discovery and initial context publication.
- Data loaded: land-texture lookup index creation and one provider refresh.
- New game / post-load game: authoritative state refresh and a generation advance when the provider is active.
- Exterior gameplay: low-frequency season query on the game thread.
- Interior gameplay: season state remains available, but materials still determine Ground Response eligibility.
- Transition or override change: generation advance, dependent cache invalidation, and one controlled deformation-history clear.

## Turn of the Seasons audit

The installed Turn of the Seasons configuration was inspected under `Data/Seasons`. It supplies seasonal land-texture swaps for Winter, Spring, Summer, and Autumn, plus object/tree/grass swaps. PIXL does not encode those FormIDs. The generic resolved-texture path handles them and remains applicable to future packs.

The installed pack currently declares:

- Winter: 9 land-texture substitutions in its primary configuration.
- Spring: 10 land-texture substitutions.
- Summer: 9 land-texture substitutions.
- Autumn: 1 land-texture substitution.

Additional generated Winter configuration is also present. These counts are diagnostic findings, not runtime assumptions.

## Performance

- GPU overhead: none beyond the existing Ground Response work; no new shaders, passes, draw calls, or buffers.
- CPU steady state: one inexpensive function-pointer call at a low polling frequency while a provider is active.
- CPU transition cost: rare cache invalidation and a lazy material rebuild as affected landscape cells initialize.
- Memory: one small texture-set-to-land-texture lookup table and a few atomic context fields.
- Filesystem: no runtime seasonal-config scanning.

## Known limitations

- PIXL can resolve a seasonal texture set even when no unique owning land-texture record is available. In that case texture/material evidence remains usable, but PIXL deliberately refuses to trust the pre-swap physical material.
- The deformation history field has no per-texel material generation. A rare global field clear is therefore safer than attempting to preserve potentially invalid tracks across a season transition.
- Live validation still requires forcing all four Seasons states in Skyrim and checking representative Turn of the Seasons landscapes, transitions, saves, and worldspaces.

## Upstream API references

- Seasons of Skyrim public exports: <https://github.com/powerof3/SeasonsOfSkyrim/blob/master/src/main.cpp>
- Season enum and settings behavior: <https://github.com/powerof3/SeasonsOfSkyrim/blob/master/include/Seasons.h>
- Runtime season manager: <https://github.com/powerof3/SeasonsOfSkyrim/blob/master/src/SeasonManager.cpp>
