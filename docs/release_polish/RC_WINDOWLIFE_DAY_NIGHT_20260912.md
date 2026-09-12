# September 12 release-candidate follow-up

## Subsequent live deployment (owner requested)

Correction after owner objection to full-cache removal: the full invalidation
was not requested. On the owner's request, restored 4,123 original cache files
and merged 31 current-run files, hash-verifying every copied file. Excluded only
719 old pixel-stage entries in Lighting, Sky, ISSAOCompositeFog and
ISSAOCompositeSAOFog. These contain the actual changed ambient, WindowLife and
sky-protection logic. Vertex bytecode is retained; only reviewed unchanged vertex
entries in those families were timestamp-revalidated against the coarse source
mtime check. Other shader families and compute caches remain intact. Current
Library.ini differs only in WindowLife version 0-7-2, verified before reuse.
Newly generated cache remains backed up at
`build/cache-restores/20260912-124029-8ab3738e15464cdc88560303dc8b8b4e/NewlyCompiledCacheBackup`;
the original deployment backup is untouched. No game process was running.

`tools/RestorePixlRcCache.ps1`: active restoration-only helper, added and reviewed
for path containment, process checks, cache identity, hash verification and
preservation of both prior/new cache copies. No runtime rendering cost; fidelity
review limits exclusions to changed pixel code. Future improvement: transactional
rollback if final directory replacement fails. This supersedes the full-recompile
instructions below. No full-cache clearing is needed for this update.

After the initial package-only handoff, deployed the 12 changed runtime payloads
to the canonical Skyrim Data directory. All live SHA256 values match the RC
manifest. UserGraphics.json remained unchanged. Existing Vortex hardlinks were
moved aside before copying replacement files, preserving Vortex's stored sources.
The old generated PipelineLibrary was moved into the same recoverable backup so
the next launch compiles the updated shaders. No game process was running.

Backup: `build/deployment-backups/RC-DayNight-20260912-122615-9db15f481ba94e1f8f9ee399e34f60ef`.
Its deployment.csv records exact paths and whether each file previously existed.
Vortex redeployment of the older installed mod can restore older files; install
the new RC archive in Vortex for a persistent managed update.

`tools/DeployPixlRcFollowup.ps1` is an active, reviewed, added deployment-only tool.
It verifies the package manifest, rejects unexpected live reparse paths, refuses
running-game deployment, limits writes to 12 explicit runtime paths, verifies
copied hashes, preserves user settings and backs up replaced files/cache. It has
no renderer/GPU workload. Future improvement: automatic rollback on an interrupted
deployment; current backups support manual restoration. Scoped security,
correctness, fidelity-preservation and performance review completed.

## Scope and decisions

This is a focused follow-up to the owner's six release observations, not a new claim
that every renderer source has been exhaustively re-reviewed. Existing unrelated
README, staging-script, release-readiness and FidelityFX edits were preserved.

Authoritative shader inputs: distribution/Shaders plus active pipeline/*/Kernels,
assembled by tools/StagePixlRendererStandalone.ps1. The ignored repository Data
shader tree is stale; an earlier ambient adjustment there was removed and applied
to the shipping Lighting source instead. No live game files were deployed.

## Implemented

- Setup: modal ordering without per-frame focus theft; wrapped explanations;
  viewport-limited taller card; Save follows content instead of overlapping it;
  current settings initialize on appearance; combo keyboard focus cannot dismiss setup.
- Camera: DOF moved from beneath the centre preview to the right-hand controls.
  Existing compact spacing and reconstruction directly below the workspace retained.
- Interior ambient: 8% reduction of the enclosed directional ambient component,
  excluding reflections; not a global exposure or direct-light change.
- Casting lights: exact player ActorMagicCaster::light identity, left/right hands.
  RadiantGrid fade multiplied by 0.65, radius extended 1.5x with a 180-unit floor;
  inverse-square source size at least 2, with a 35%-radius smooth retirement zone.
  Inverse-square CPU luminance uses matching source size, intensity and retirement.
  Ordinary lights retain original response. This is not a tune of all NPC or
  third-party particle emitters; verify those separately if they remain too bright.
- Sky Protection: independent 0..1 setting, default 0.35. Reduces atmosphere alpha
  only for non-geometry depth in ISSAOComposite and reflection sky in Sky.hlsl.
  Minimum World Visibility and scene fog remain independent. Does not remove
  weather-authored cloud opacity or vanilla fog when separately enabled.
- WindowLife: exterior saved depth 50.2 retained (new default 50); separate interior
  depth 80, with a wider interior-only safe cap. Outdoor atlas enabled by default,
  strength 0.78, behind existing refracted glass and passers.

## WindowLife architecture and ABI

UI -> serialized WindowLife settings -> per-frame interior selection -> Optics0.x,
Asset0.y/z and reserved Fidelity1.w -> pixel shader atlas projection -> Lighting
emission composite. PerGeometryData remains 240 bytes, 15 float4 fields.
No new shader slots: t126 is room art outside/daytime outdoor art inside; t124 is
curtains outside/nighttime outdoor art inside. Curtains are not evaluated inside.
t122, t123, t125 and t127 retain their existing roles. Both atlases remain resident;
no disk loads at dawn/dusk. Missing day art retains original glass/passers; missing
night art uses a darkened day fallback. Optional resource failures are logged.

The existing calendar blend is reused: night before 05:00, smooth dawn 05:00–08:00,
day 08:00–17:00, smooth dusk 17:00–20:00, night thereafter. Same tile and coordinates
sample both sheets in linear color. Outdoor art bypasses the exterior lit-room
exposure boost. These are authored regional scenery impressions, not actual views
of the exterior cell, and not volumetric geometry.

Atmosphere replaces two padding floats with skyProtection + padding at the same
position in CPU Settings and SharedData HLSL. Size remains 272 bytes (existing
compile-time assertion). No downstream field offsets or resource slots change.

## Generated artwork / provenance

Used the built-in image-generation tool via the imagegen skill, not the API CLI.
Night was generated as a lighting edit of the day atlas. The tool returned 1254x1254
images; offline DirectXTex conversion resamples to the loader's required 2048x2048.
Conversion is reproducible with tools/ConvertWindowLifeAtlas.cpp using the existing
DirectXTex dependency. DDS is BC1 sRGB with eight mips; shader clamps mip <=7 and
insets each tile. PNG compatibility fallbacks are also shipped. Four-by-four grid:
pine/meadow, autumn/birch, snowy foothills, alpine/tundra rows. No new runtime
dependency, network requirement or telemetry.

Assets under pipeline/WindowLife/Kernels/WindowLife:

- OutdoorAtlas_2k.dds and OutdoorAtlas.png
- OutdoorAtlasNight_2k.dds and OutdoorAtlasNight.png

Day prompt:

> Create a production game texture atlas, square 2048x2048, exactly 4 columns by
> 4 rows of equally sized square outdoor landscape images, edge to edge, no gutters
> no frames no text. Use case historical-scene. Asset for PIXL Renderer: fake outdoor
> views through medieval Nordic windows seen from indoors, later refracted and
> parallax projected by shader. Each tile a realistic restrained northern European
> wilderness scene, eye level looking out into nature, horizon at 45% from top
> consistently. Row 1 four temperate pine woodland / meadow scenes with distant
> hills. Row 2 four birch and autumn woodland riverbank scenes. Row 3 four snowy pine
> and rocky mountain foothill scenes. Row 4 four rocky alpine and sparse tundra
> scenes. Different compositions per tile. Neutral overcast daylight, soft lighting,
> believable muted natural color, no sun disc, no baked fog, no dramatic contrast.
> Clear middle-distance trunks and bushes with distant hills and sky for depth
> cues. No people no buildings no interior rooms no window frames no glass no
> curtains no labels no borders no watermark. Texture atlas not a displayed mockup;
> each tile fills exactly one quarter of each dimension.

Night prompt:

> Edit this exact 4x4 outdoor landscape texture atlas to its matching NIGHTTIME
> version. Use case lighting-weather. Preserve the exact tile grid dimensions,
> every tree trunk, shoreline, rock, mountain silhouette, camera position, horizon
> and framing in each of all sixteen tiles. Change ONLY illumination and sky to
> dark natural moonlit night with muted blue-grey ambient light, softly luminous
> cloudy night sky and a few dim stars. No moon disc, no added objects or lights.
> Snow subtly reflects moonlight. Forest foreground should be dark but readable.
> No daylight white clouds, no teal saturation, no aurora. Must align spatially with
> original daytime atlas for shader crossfading, same 4 columns 4 rows edge to edge
> no gutters no text no frames. Request output 2048x2048 if possible.

## Per-file review findings

All rows below are active, reviewed, modified/added, with security, fidelity,
performance and future-work review scoped to this change. No third-party sources
were changed. Visual changes are B (controlled improvements needing game validation);
UI focus/layout fixes are A (correctness fixes).

| File | Purpose / dependencies / review and validation |
| --- | --- |
| engine/Menu/LaunchExperienceRenderer.cpp | ImGui setup, QualityProfiles/ImageReconstruction. Fix focus/overlap and one-time initialization. Compiled; game interaction pending. |
| engine/Menu/PIXLRendererPage.cpp | Camera layout and persisted controls. Existing field wiring retained; DOF right. Compiled; resolution/DPI check pending. |
| engine/Modules/WindowLife.h | Settings, resource ownership and 240-byte payload. Independent depths and owning night/day SRVs. ABI static assertions compile. |
| engine/Modules/WindowLife.cpp | Serialization, loading, calendar blend and slot bindings. Reuses reserved semantic and interior-unused slot. Compiled; fallback tests in game pending. |
| pipeline/WindowLife/Module.ini | Runtime/cache module identity bumped 0-7-2. Staging dependency audited. |
| pipeline/WindowLife/Kernels/WindowLife/WindowLife.hlsli | Atlas projection, refraction, mips, blending. No slot growth; bounded UVs/depth. FXC passes. |
| pipeline/WindowLife/Kernels/WindowLife/OutdoorAtlas_2k.dds | Day sRGB compressed runtime image. Inspected generated art; converted successfully; shader binding traced. |
| pipeline/WindowLife/Kernels/WindowLife/OutdoorAtlasNight_2k.dds | Night matching runtime image. Same format and layout; runtime dissolve needs motion review. |
| pipeline/WindowLife/Kernels/WindowLife/OutdoorAtlas.png | Resized daytime WIC fallback, existing loader path; offline format conversion checked. |
| pipeline/WindowLife/Kernels/WindowLife/OutdoorAtlasNight.png | Night WIC fallback; same loader validation and fallback behavior. |
| distribution/Shaders/Lighting.hlsl | Shipping compositor and ambient response. Outdoor exposure branch preserves night; modest enclosed ambient trim. FXC passes. |
| distribution/SKSE/Plugins/PIXLRenderer/SettingsDefault.json | Shipping initial defaults. Separate interior settings; exterior saved value unchanged. JSON parse checked by staging/audit. |
| engine/Modules/Atmosphere.h | Shared settings ABI. Padding reuse only; size assertion remains 272. Compiled. |
| engine/Modules/Atmosphere.cpp | Sky setting UI, finite/clamped loading and serialization. Does not misuse world transmittance. Compiled. |
| distribution/Shaders/Common/SharedData.hlsli | GPU ABI mirror; matching padding reuse without offset growth. Lighting/fog/sky compile. |
| distribution/Shaders/ISSAOComposite.hlsl | Depth-classified gameplay fog composite; only sky alpha reduced. FXC APPLY_FOG + ATMOSPHERE_PIPELINE passes. |
| distribution/Shaders/Sky.hlsl | Reflection sky fog. Same protection alpha; valid TEX permutation compiles. |
| engine/Modules/NaturalLighting.cpp | Player casting-source identity and softer/wider radiance. Existing CommonLib/runtime ownership retained; no extra hooks. Compiled; visual and CPU profiling pending. |
| tools/ConvertWindowLifeAtlas.cpp | Build-only WIC/DDS converter using installed DirectXTex; explicit input/output arguments. Conversion HRESULT success, no runtime execution path. |
| tools/TestWindowLifeShaders.ps1 | Local FXC checks, writes only build outputs; no external actions. Six permutations pass (including SKINNED exclusion). |

No unrelated cleanup/deletion was performed. Global all-light intensity/radius
changes were rejected; broad renderer redesigns were not attempted.

## Validation and release limitations

Built DLL: build/PIXL-12C/Release/PIXLRenderer.dll, 19,712,512 bytes.
SHA256: `7CF3F70C1DB2BE26716BFC30915FFB1B9169DA000FC4556FFEC5BAE9B71128B5`.
New compile-on-device archive: dist/PIXL-Renderer-RC-20260912-WindowLife-DayNight.zip,
240,950,717 bytes, SHA256
`18D1B6113E2577D3F48748FCB25C90850A12F6A57A865A1ABD520EF7B8206009`.
The staging manifest verified 330 payloads. This is a new standalone RC, not an
overwrite of the owner's existing zipped release candidate. Install the whole
package: replacing only the DLL would omit required shaders/artwork.

Release build: actual VS2022/CMake PIXLRenderer target succeeds. The duplicate
PATH/Path child environment was normalized without modifying system settings;
the existing vcpkg cache required approved external access. No dependencies were
newly requested. Automatic game deployment remains OFF.

FXC: six Lighting permutations pass with PIXL_WINDOW_LIFE; SKINNED confirms its
intentional exclusion. Gameplay fog and textured sky permutations pass. Initial
sky commands omitted TEX and failed (invalid TexCoord0); corrected defines pass.
Repository shipping audit passes: 37 active modules, one retired source-only module.
Initial asset audit correctly required PNG fallback files; they were generated and
the audit rerun successfully. This is not exhaustive shader permutation coverage.

Final checks also passed: combined MATERIAL_FORGE + RADIANT_GRID + NATURAL_LIGHTING
+ ATMOSPHERE_PIPELINE + PIXL_WINDOW_LIFE Lighting compilation, staged-package audit,
330-payload manifest verification and 7-Zip archive integrity test (331 files).

No in-game testing or GPU timings are claimed. Before upload, test first-run combo
selection, Escape/Enter, DPI/short windows, camera-page scrolling, noon/midnight
and transitions at 05/08/17/20, interior-to-exterior transitions, oblique windows,
missing-art fallback, hand flames versus torches, clear/cloudy skies and reflection
skies. Modded particle casting emitters may require a separate tune. The generated
night edit may contain small art alignment changes; check slow dawn/dusk dissolve.

## 5 Future Visual Improvements

1. Weather-aware outdoor atlas exposure.
2. Region/cell-specific art selection without hard dependencies.
3. Authored foreground/background depth planes.
4. Matched art alignment review across every dusk tile.
5. In-game hand-light photometric comparisons against torches.

## 5 Future Performance Improvements

1. Profile dual-atlas texture bandwidth on window-heavy scenes.
2. Skip night sampling at full daylight if measurements justify branching.
3. Consider BC7 offline quality/size tradeoffs.
4. Profile casting-light identity checks before adding pointer caches.
5. Measure UI vertical fit and rendering on lower-resolution displays.

## 5 Future Feature / Research Ideas

1. Optional location-authored window overrides.
2. Snow/rain seasonal outdoor variants.
3. Lightweight depth-authored landscapes.
4. Automated ImGui first-run interaction tests.
5. Particle-emitter-specific casting light controls.
