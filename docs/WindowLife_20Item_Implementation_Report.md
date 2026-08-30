# WindowLife cross-window consistency and fidelity pass

Date: 2026-08-30

Baseline: the owner's live `UserGraphics.json` was hashed before work and was not edited. The accepted manual `150 x 150` room calibration and `AutomaticRoomSizing=false` therefore remain the live baseline; the automatic path receives the new family and native-layout work when enabled.

## Phase A — consistency and depth

### A1 — Stable per-instance room identity

**Diagnosis.** `Evaluate()` seeded fallback rooms with `Hash21(absoluteRoomCell + GetRuntime0().yy * 41.0f)` and native rooms with a quantized `centerPlane`. Evenly spaced instances could therefore collide despite being separate geometry.

**Plan.** Cache a material identity, combine it with the geometry name and quantized world-bound centre, upload a 24-bit stable salt in `Layout0.x`, and salt both native and fallback seeds.

**Patch.** `Classification::materialIdentity`, `StableHash32`, `HashCombine32`, `UpdateAndBindActive()`, and both HLSL `roomSeed` expressions were changed.

**Consistency/regression.** This is upstream of A5/A6/B10: rooms, curtains, occupants and variation share the same new identity. It does not move an aperture or alter the live manual calibration.

**Acceptance.** Walk a facade containing at least five evenly spaced windows. Adjacent instances should no longer repeat the complete room/curtain/occupant sequence because their grid cells happened to alias.

### A2 — Classification-time native-layout policy

**Diagnosis.** `ResolveAuthoredPaneLayout()` requires the current draw's UV and world-plane derivatives; those values do not exist during the CPU material classification. Caching its final `centerPlane/roomSize` there would require GPU readback or a new prepass and would be incorrect for instanced geometry.

**Plan.** Cache the material-owned portion once (`explicit native guide`, `generic native guide`, or `fallback`) in `Layout0.y`. Retain draw-local geometric reconstruction at a fixed texture-dimension-derived guide mip.

**Patch.** `ClassifyMaterial()`/`UpdateAndBindActive()` now upload the cached policy; `Evaluate()` requires it before attempting native reconstruction.

**Consistency/regression.** This is the safe single-pass DX11 interpretation of A2 and feeds A3. Exact per-instance aperture bounds remain correctly draw-local.

**Acceptance.** Identical material classes always attempt the same layout strategy. The debug overlay must not show one copy attempting native layout and another skipping it solely because of per-pixel lighting.

### A3 — Confidence dead zone and continuous promotion

**Diagnosis.** Native background/layers previously switched at the single tests `confidence > 0.5` and `backgroundConfidence > 0.5`.

**Plan.** Map confidence through `smoothstep(0.35, 0.65, confidence)`, blend fallback and native aperture centre/size continuously, and admit foreground layers through the same weighted confidence.

**Patch.** `nativeBackgroundWeight` and `nativeLayerWeight` replace the binary 0.5 switch. Debug states use the same confidence bands.

**Consistency/regression.** This is a stateless dead-zone blend, not a cross-frame CPU history cache. It avoids new per-window state/lifetime hazards and works with A2's fixed guide mip. A visually unstable edge case remains a live-test item rather than being hidden with excessive temporal smoothing.

**Acceptance.** Approach and retreat from a window while watching the room edge and debug tier. There should be no one-frame hard resize at the old 0.5 boundary.

### A4 — Family-calibrated fallback rooms

**Diagnosis.** `GetAdaptiveRoomSize()` used one `const float2 referenceRoom = float2(110, 140)` for every architecture family.

**Plan.** Use a six-entry calibration: Nordic `110x140`, noble `132x174`, Riften `118x138`, Windhelm `104x154`, Dwemer `128x128`, trade `126x146`.

**Patch.** Added `GetFamilyFallbackRoomSize()` in HLSL and the matching CPU radius-reference table used by A7.

**Consistency/regression.** Only automatic sizing changes. Manual sizing stays exactly user-controlled.

**Acceptance.** Force fallback on Dwemer and Solitude windows: their boxes should be square/compact versus tall/noble rather than identical.

### A5 — Family-aware curtain selection

**Diagnosis.** Curtain selection was `floor(Hash11(roomSeed * 47.91 + 19.7) * 15.999)` and ignored `roomFamily`.

**Plan.** Curate five curtain candidates per existing family, parallel to `SelectRoomTile()`.

**Patch.** Added `SelectCurtainTile(roomSeed, family, warmthBand)` and replaced uniform atlas selection.

**Consistency/regression.** Uses A1's seed and A6's warmth band. The atlas layout and bindings are unchanged.

**Acceptance.** Sample noble and farmhouse districts: curtain distributions should visibly differ across several windows.

### A6 — Correlated curtain/room warmth

**Diagnosis.** `curtainPalette = Hash11(roomSeed * 29.17 + 2.3)` was independent of room illumination and time of day.

**Plan.** Derive a shared `roomWarmthBand` from stable room identity and `Layout0.z` night blend, then use it for tile choice and tint.

**Patch.** Curtain tile and fallback/atlas tint now share the room warmth band.

**Consistency/regression.** B2 consumes the same night state, so room and curtain color cannot drift into contradictory warm/cold choices.

**Acceptance.** Check warm night rooms and cool daytime rooms for obviously clashing curtains; none should stand out as independently randomized.

### A7 — Geometry-first aperture/facade classification

**Diagnosis.** `Asset0.w` was selected primarily from NIF-name substrings such as `window`, `glass`, `facade`, and `wall`.

**Plan.** Compare geometry radius with the family reference radius. Ratios at/below `2.75` are apertures, at/above `4.25` are facades; names only break ties inside the ambiguous band.

**Patch.** Reworked `UpdateAndBindActive()` and made the CPU family calibration match HLSL.

**Consistency/regression.** Preserves name hints for genuinely ambiguous assets while improving texture/mesh replacer compatibility. It feeds the same `Asset0.w` consumed by automatic sizing.

**Acceptance.** Compare structurally equivalent vanilla and replacer meshes with different names; they should choose the same treatment.

### A8 — Grazing/mip-safe atlas insets

**Diagnosis.** Room, curtain and occupant atlas inset caps were fixed at `0.12`, independent of view angle.

**Plan.** Add a small grazing-angle margin and raise the hard cap to `0.18` while retaining mip-dependent growth.

**Patch.** Updated all three atlas inset formulas.

**Consistency/regression.** Insets only affect tile-border safety. B9 independently improves alpha edges inside the safe tile.

**Acceptance.** Inspect distant and near-edge-on windows throughout the configured fade range; no adjacent atlas tile should bleed into the pane.

### A9 — Final-aperture reveal anchoring

**Diagnosis.** The current reveal already used `baseRoomLocal`, but this dependency was implicit. Native/fallback blending could otherwise have reintroduced a stale coordinate if reveal were calculated earlier.

**Plan.** Keep reveal construction after final aperture resolution and use that same coordinate for directional reveal shading.

**Patch.** `depthEdgeCoord`, `roomEdge`, and `directionalRoomEdge` are calculated only after the final `roomSize/centerPlane/baseRoomLocal` have been resolved.

**Consistency/regression.** Depends on A3 ordering and supports B6. No second shifted room sample was introduced.

**Acceptance.** On native and fallback windows, the dark reveal must stay on the actual glass edge while orbiting and approaching.

### A10 — Three-state debug overlay

**Diagnosis.** `authoredLayoutState` was already composed in `Lighting.hlsl` as red/cyan/green, but its developer tooltip did not explain the tiers clearly.

**Plan.** Preserve the implementation, make state production follow A3, and clarify the GUI description.

**Patch.** Debug-state thresholds now use the confidence bands; the tooltip explicitly documents red rejection, cyan background-only, and green occupant-safe layout.

**Consistency/regression.** No release cost while disabled. This is the principal live diagnostic for A2–A7.

**Acceptance.** Enable `Show Window Class Overlay`; the three native-layout states must be visually distinguishable in addition to blue/amber material tiers.

## Phase B — visual fidelity

### B1 — Environment reflection

**Diagnosis.** WindowLife altered `material.F0`/roughness, so normal PBR could reach World Probes, but it had no explicit grazing glass layer and read mostly as a scalar response.

**Plan.** Sample the existing dynamic cubemap using the final glass normal and roughness, weight with Schlick Fresnel, and add a restrained glass-only lobe.

**Patch.** Added a `WORLD_PROBES`-guarded WindowLife probe contribution in `Lighting.hlsl`; `EnvironmentReflectionStrength` defaults to `0.42`, range `0–1.5`, normal GUI.

**Consistency/regression.** Reuses existing probes and adds no pass/resource. The coefficient is intentionally small to avoid double energy with the material's normal indirect lobe.

**Acceptance.** At sunset/grazing angles, panes should show directional sky/environment color rather than a flat brightening.

### B2 — Time-of-day color temperature

**Diagnosis.** Night changed shadow/activity strength but did not tint `layeredRoomColor`.

**Plan.** Upload the existing day/night blend and interpolate cool-neutral day to candle-warm night.

**Patch.** `Layout0.z` and `timeTint`; governed with B3 by `InteriorLightingResponse` default `0.34`, range `0–1`, normal GUI.

**Consistency/regression.** Shares A6's warmth state. Brightness presentation controls remain independent.

**Acceptance.** The same room should be neutral/cool by day and visibly warmer at night.

### B3 — Live exterior lighting/weather response

**Diagnosis.** Atlas color was independent of `SharedData::GetAmbient`, directional light color, and rain.

**Plan.** Apply a restrained normalized ambient/sun tint and a small overcast/rain dimming.

**Patch.** `layeredRoomColor` now responds to live ambient, directional color, and rain through `InteriorLightingResponse`.

**Consistency/regression.** Season is not treated as weather; only real shared light/rain state contributes.

**Acceptance.** Compare the same window at noon, sunset and heavy overcast; hue/readability should track the scene without erasing room art.

### B4 — Exterior light-spill receiver

**Diagnosis.** WindowLife currently runs inside the window material's Lighting pixel shader. It cannot shade unrelated ground/wall receiver pixels or project geometry outside the window.

**Plan.** Deferred intentionally. A correct implementation needs a bounded window-emitter list plus a receiver/decal or deferred-light pass, visibility rejection, lifetime management, and reconstruction testing. Faking it in the current shader would only repaint the pane.

**Patch.** None in this pass.

**Consistency/regression.** Deferral prevents a new draw/pass, wall leaks, and a late-release pipeline/ABI change. The current bright-window output remains suitable input for future emitter extraction.

**Acceptance.** Future implementation must cast a soft night-only pool onto nearby receivers with no through-wall leakage. This criterion is not claimed complete now.

### B5 — Mip-filtered grime texture

**Diagnosis.** Glass grime combined multiple procedural noise frequencies and a sharp analytic streak per pixel, making oblique/distant stability dependent on TAA.

**Plan.** Use a neutral 1024-square, mip-filtered texture in world-plane coordinates, retaining a low-frequency analytic fallback if missing.

**Patch.** Added `GlassGrime_1k.png`, PS `t122`, one-time WIC load/mipmap generation, filtered `SampleGrad`, and provenance. `WeatherGlassResponse` defaults to `0.36`, range `0–1`, normal GUI.

**Consistency/regression.** t122–t127 is a contiguous audited range; no other active binding uses t122. Missing asset disables only the texture path.

**Acceptance.** Move the camera past oblique/distant panes; grime should remain stable without changing the accepted close-up character.

### B6 — Sun-directional reveal

**Diagnosis.** Room-edge shading was a uniform `roomEdge * 0.18` vignette.

**Plan.** Project the live directional-light vector into the resolved aperture plane and bias shadowing to the opposing reveal side.

**Patch.** Added `directionalRoomEdge`; `DirectionalRevealStrength` defaults `0.22`, range `0–0.60`, developer GUI.

**Consistency/regression.** Uses A9's final aperture coordinates and does not shift atlas UVs.

**Acceptance.** Rotate around a sunlit window; its recess should read asymmetrically relative to the sun rather than as a static vignette.

### B7 — Rain/cold glass response

**Diagnosis.** Dirt/distortion ignored live precipitation and cold state.

**Plan.** Add filtered vertical rain streak modulation from real rain intensity and subtle frost/condensation from engine snow-weather state.

**Patch.** `Layout0.w` carries `Sky::IsSnowing()`. `EvaluateSurface()` adds rain flow and frost haze through `WeatherGlassResponse`.

**Consistency/regression.** Does not infer meteorology from season. It changes only detected glass and gracefully falls back when the texture is absent.

**Acceptance.** Compare dry, rainy, and snow-weather panes: rain should produce subtle vertical response and cold weather should reduce clarity slightly.

### B8 — Solar glass glint

**Diagnosis.** General roughness/F0 response lacked a distinct tight flat-glass glint.

**Plan.** Add an exterior-only `pow(R·V, 192)` lobe using real directional light color.

**Patch.** Added the glint after indirect reflection; `SunGlintStrength` defaults `0.24`, range `0–1`, developer GUI.

**Consistency/regression.** Exterior-only prevents the directional-light-through-walls regression previously fixed in PIXL interiors.

**Acceptance.** A tight highlight should sweep across suitable exterior panes at the matching sun/view angle.

### B9 — Close cutout edge filtering

**Diagnosis.** Authored curtain/occupant alpha was sampled directly; fixed atlas inset protected tiles but did not anti-alias close silhouette edges.

**Plan.** Use derivative-scaled alpha feathering at low mip only, leaving distant mip filtering unchanged.

**Patch.** Added curtain/occupant `fwidth` feathering; `CloseLayerFeather` defaults `0.65`, range `0–1.5`, developer GUI.

**Consistency/regression.** Works inside A8's tile-safe UVs and does not soften room art.

**Acceptance.** Close occupants/curtains should have clean, softly transmitted edges; distant windows must not regain atlas bleed.

### B10 — Per-room inhabitation variation

**Diagnosis.** Neighboring rooms shared the same flat emission treatment and had no internal height response.

**Plan.** Add stable seed-based brightness/color jitter and a subtle ceiling-to-floor falloff.

**Patch.** `RoomVariationStrength` defaults `0.12`, range `0–0.35`, developer GUI; applied before user contrast/emission.

**Consistency/regression.** Uses A1's identity, so variation is stable and cannot swim with camera motion. Existing presentation sliders remain authoritative.

**Acceptance.** A tall facade should read as separate inhabited rooms with subtle stable variation, not one repeated lightbox.

## Summary

| Item | One-line result | Files touched | New setting |
|---|---|---|---|
| A1 | Material + geometry + transform instance salt | `WindowLife.h/.cpp/.hlsli` | None |
| A2 | Cached material layout policy | same | None |
| A3 | 0.35–0.65 confidence transition | `.hlsli` | None |
| A4 | Six family fallback calibrations | `.cpp/.hlsli` | None |
| A5 | Family-aware curtain atlas choice | `.hlsli` | None |
| A6 | Shared room/curtain warmth band | `.hlsli` | None |
| A7 | Geometry-first aperture/facade decision | `.cpp` | None |
| A8 | Mip/grazing-safe atlas margins | `.hlsli` | None |
| A9 | Final-bound directional reveal | `.hlsli` | `DirectionalRevealStrength` |
| A10 | Stable three-tier debug semantics | `.cpp/.hlsli`, `Lighting.hlsl` retained | None |
| B1 | Explicit Schlick-weighted World Probe layer | `Lighting.hlsl`, WindowLife settings | `EnvironmentReflectionStrength` |
| B2 | Day/night room temperature | WindowLife C++/HLSL | `InteriorLightingResponse` |
| B3 | Ambient/sun/rain room response | `.hlsli` | shared above |
| B4 | Correct receiver pass deferred | report only | None |
| B5 | Mip-filtered glass grime | C++/HLSL/asset/provenance | `WeatherGlassResponse` |
| B6 | Sun-aware reveal shading | `.hlsli` | `DirectionalRevealStrength` |
| B7 | Rain streak/frost response | C++/HLSL | `WeatherGlassResponse` |
| B8 | Tight exterior solar glint | `Lighting.hlsl` | `SunGlintStrength` |
| B9 | Derivative close-cutout feather | `.hlsli` | `CloseLayerFeather` |
| B10 | Stable room color/height variation | `.hlsli` | `RoomVariationStrength` |

## Deferred or constrained items

- **B4** is deliberately deferred because the existing pass has no receiver pixels. Implementing it correctly is a new renderer pass, not a WindowLife shader edit.
- **A2/A3** are implemented without GPU readback or a persistent per-window CPU state map. Final aperture bounds remain draw-local; cached policy + fixed guide mip + continuous confidence gives the requested stability without introducing a stateful DX11 synchronization hazard.

## ABI and ordering risks

- WindowLife's private structured payload grew from 192 to **240 bytes** on both C++ and HLSL sides. `static_assert` protects the CPU size; the HLSL field order is identical.
- A new private PS SRV occupies **t122**. WindowLife now binds one contiguous t122–t127 range on every Lighting draw, including neutral draws. Repository binding audit found no competing active t122 declaration.
- World Probe sampling is compile-guarded. Builds without World Probes keep the prior material response.
- The live user configuration file is not modified. The shipped default and live-tested preset explicitly carry the seven new conservative defaults; older user files remain compatible through the existing defaulting serializer.

## Ordered live-test checklist

1. Verify PIXL loads, the GUI opens, and the log reports the 240-byte t127 payload plus grime at t122.
2. Confirm the current saved appearance/manual room dimensions are unchanged with Automatic Room Sizing off.
3. Enable automatic sizing and the debug overlay; inspect red/cyan/green layout states across Solitude, Whiterun, Riften, Windhelm and Markarth.
4. Walk toward/away from the same windows and orbit at grazing angles; look for layout pops, seams, room swimming or duplicate layers.
5. Inspect five or more neighboring windows for repeated room/curtain/occupant sequences and stable per-window variation.
6. Compare family proportions and curtain styles between noble, farmhouse/trade and Dwemer architecture.
7. Inspect close occupant/curtain edges, then the same atlas layers near `DistanceFadeEnd`.
8. Compare the same exterior windows at noon, sunset, night and overcast/rain/snow weather.
9. At sunset, look for directional probe color and a tight sun glint without metallic glass or interior sun leakage.
10. Recheck known problem assets: Solitude facade/helper mesh, paneled Whiterun windows, closed shutters, tiny panes and large multi-window facades.
11. Review `PIXLRenderer.log` for WindowLife resource failures, shader compilation errors, register conflicts or classification spam.

Static validation completed: Release C++ target/Audit passed; representative base, World Probes + SkyBounce, and Material Forge + World Probes Lighting pixel permutations compiled with FXC. Visual acceptance remains pending live Skyrim testing.
