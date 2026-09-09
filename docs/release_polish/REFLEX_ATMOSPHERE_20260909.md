# Reflex controls and atmosphere-defaults follow-up

## Scope and evidence

The owner confirmed the preceding DLSS-G/HUD build works, then requested usable
Reflex controls, their saved atmosphere look as default, packaging and source
publication. This is a focused follow-up, not a completed whole-engine audit.

The UI previously checked only D3D11 Reflex and disabled controls for every
sidecar, although DLSS-G already requested D3D12 low-latency mode while active.
Both settings views now select the actual DLSS-G runtime and query Reflex's
`lowLatencyAvailable`. Boost/limiter controls work even if the optional
always-on switch is off, because generation itself requires Reflex. FSR3 still
owns its pacing. No new scheduling hooks, queue waits or shader edits were made.
Successful option changes are logged once per change, not each frame.

All 54 saved Atmosphere properties were copied to the shipped defaults and C++
initializers. Existing user configs and historical named profiles are preserved.
The 272-byte shared structure, field order and bindings are unchanged. Defaults
alter the authored look and volume quality (36-pixel grid, 40 depth slices);
performance relative to the previous default is not measured. Classification B:
owner-selected visual change, requiring wider scene coverage before release.

## Selected-file review

| File | Purpose / change | Validation and remaining work |
| --- | --- | --- |
| engine/Modules/ImageReconstruction/Streamline.h, .cpp | Reflex availability from SDK; change-only options logging; preserve DX11 suppression | Release build; SDK smoke normal/boost/off, limiter and sleep accepted; actual latency still unmeasured |
| engine/Menu/PIXLRendererPage.cpp | Public Camera latency controls route to active runtime | Release build; in-game control/latency retest pending |
| engine/Modules/ImageReconstruction.cpp | Legacy controls use same availability and automatic-mode policy | Release build; serialized preferences unchanged |
| engine/Modules/Atmosphere.h | Owner look also used by Restore Defaults and missing fields | Existing ABI static assertion; 54-field parity test |
| distribution/SKSE/Plugins/PIXLRenderer/SettingsDefault.json | Ship owner Atmosphere block only; all other modules preserved | JSON parse and float32 parity test |
| tools/SidecarSmoke.cpp | Test actual Reflex runtime capabilities/options/sleep | Rebuilt and passed RTX 3060 Ti with SM86 and dual instances; no game input-latency claim |
| tools/TestPixlAtmosphereDefaults.ps1 | Read-only JSON/C++/serialization parity test | Passed 54 fields; rejects mismatches and nonfinite values |
| tools/StagePixlRendererStandalone.ps1 | Package coherent runtime and integration instructions | Use existing manifest/hash verification; keep user config and root proxy out of package |
| distribution/PIXL-RENDERER-README.md, docs/ImageReconstruction/DLSSG_SM86_INTEGRATION.md | Explain Reflex, multiplier, proxy install and default migration | Reviewed against code; release-candidate status explicit |

Security scope: no networking, shell execution or DLL-load behavior was added
to the renderer in this follow-up. Tests use existing trusted local runtime
paths. Earlier vendor NGX privacy/offline caveats remain unresolved. No measured
performance improvement is claimed. Do not turn on higher multipliers or Boost
by default for all users; power, base frame time and refresh rate differ.

Future: measure input-to-display latency at 2x/3x/4x, verify limiter semantics
against actual output timing, and validate atmosphere across interiors, maps,
weather transitions and low-memory hardware. Keep algorithmic changes separate
from this owner-selected baseline update.

## Artifacts and deployment

- Release DLL SHA256: `B9A0F5385C29D19721D98C1A97E36D46F16464DE734FF0D78472C45ADC3A028B`.
- Package: `dist/PIXL-Renderer-v1.0-RC-20260909-Reflex.zip`.
- ZIP SHA256: `A5A2DD0215DB32B6F5217559DC95BC0CEBF0DFBF0AEBC932885EC8E77B93721D`.
- Manifest verification passed for 326 payloads; no user graphics config or
  shader-cache payload was included. Existing live cache was not touched.
- Live DLL/PDB were hash-verified after backup to
  `checkpoints/reflex-atmosphere-before-20260909-231254`. Live defaults received
  only the atmosphere block; the saved always-on Reflex switch was enabled.
  Boost, limiter, multiplier and neural preferences were left as authored.
- Source diff whitespace checks exclude unchanged upstream license contents,
  which contain vendor trailing whitespace. Vendor notices were preserved.
