# PIXL mod compatibility and troubleshooting

## Runtime test coverage

The current 1.0.9 live-test target is **Steam 1.6.1170**. Earlier owner testing
also covered Skyrim 1.5.97 and a GOG executable verified as **1.6.1179.0**.
Coverage on one executable does not certify every hook or feature on the others.
Use SKSE and dependencies matching the actual executable version, not the SE/AE
product name.

## What should I keep?

PIXL changes rendering; it does not replace the world's textures, meshes, grass
placements, weather records or placed lights.

| Mod category | Guidance |
|---|---|
| Mountain, landscape, architecture and character textures/meshes | Normally keep. Authored material data determines which material effects are available; individual packs still need testing. |
| Grass and tree replacers | Normally keep. PIXL supplies shading and interaction, not replacement vegetation assets. Distant LOD and static shrubs are different from terrain grass. |
| Weather plugins | Normally keep one coherent weather setup and its required patches. PIXL reacts to game weather; it does not replace every weather plugin. |
| Light placement and interior overhauls | Keep if desired, with their dependencies/patches. PIXL lighting does not reproduce their edits to placed lights or architecture. |
| PGLite | The original runtime DLL does not recognize a PIXL-only installation. The FOMOD offers an optional PIXL-aware PGLite DLL, cubemap, and corresponding source that enable PIXL detection without Community Shaders. The Release build and package checks passed, and the Steam live log confirms PIXL detection and PGLite startup. No TruePBR config was installed in that run, so mesh appearance and hair-flow validation remain pending. See [PGLite compatibility](compatibility/PGLITE.md). |
| Embers XD | Confirmed startup with BEES 1.2 on 1.5.97; see below. |
| Community Shaders / ENB renderer | Cannot run alongside this PIXL release. |
| ReShade | Experimental Camera-page bridge for an installed API 20 compatible add-on runtime; live rendering compatibility remains unvalidated. |
| Duplicate shader/upscaler helpers | Follow PIXL's startup diagnostics. Existing conflicts include SkyrimUpscaler, EVLaS/AELAS, SSEReShadeHelper, TAASharpen and NVIDIA_Reflex. |

### Skyrim Performance Solution (SPS)

PIXL and SPS loaded together on the Steam **1.6.1170** test installation on
2026-10-30, using PIXL **1.0.9.0** and SPS **1.0.70**. SPS installed its
`GetRenderPasses_Occlusion` vfunc 0x2D and `Main::RenderDepth` detour after PIXL's
SkyBounce hook; PIXL logged its SPS detection notice and completed renderer
DataLoaded initialization. No plugin error entries appeared in either startup
log. This verifies plugin co-load and hook installation through initialization;
it does not certify visual quality in gameplay scenes.

The bundled SPS reference source is v1.0.63, older than the installed build. The
current [SPS 1.0.70 source release](https://github.com/dnai92/skyrimPerformanceSolution/tree/5f1de84)
was checked for the shared occlusion hook; its wrapper calls the previously
installed function before applying its filter. The installed runtime log
corroborates that hook path. The SPS project supports Skyrim 1.5.97 and 1.6.317
through 1.7.x, and reports testing on Steam 1.6.1170.

There is one intentional visual interaction in the precipitation/skylighting
occlusion map. PIXL SkyBounce submits eligible occluders larger than radius 32.
SPS's `[SkylightingCulling]` option is enabled by default and filters that shared
pass list below radius 128. This reduces SPS draw work but also removes smaller
objects from PIXL SkyBounce's sky-visibility data. If SkyBounce occlusion detail
looks weaker, set `bEnabled=false` under `[SkylightingCulling]` in `SPS.ini`;
other SPS features can remain enabled. PIXL logs this interaction when it detects
`SPS.dll`.

The current SPS config has `[SkylightingCulling]` enabled at radius 128. This is
an intentional quality/performance trade-off, not a plugin conflict. Other SPS
culling options can also change shadow or geometry detail; compare them
individually if a visual difference appears. Manual visual testing of interiors,
exteriors, moving lights and weather remains outstanding.

### NAT

Do not equate a weather ESP with the ENB renderer. Original NAT also changes
lighting, effects and tonemapping. NAT.ENB III supplies a weather plugin and an
ENB preset designed together. Keeping its weather plugin does not reproduce the
ENB look under PIXL and is not a validated NAT/PIXL pairing. Test exposure, fog,
rain and day/night transitions; avoid stacking overlapping weather systems.
Sources: [original NAT](https://www.nexusmods.com/skyrimspecialedition/mods/12842),
[NAT.ENB III](https://www.nexusmods.com/skyrimspecialedition/mods/27141).

### ENB and ReShade are separate cases

ENB detection previously returned before PIXL initialized, leaving no PIXL menu
or effects and only a log entry. The updated source gives a clear startup conflict
message and reports plugin load failure. It retains the protection against running
two renderers together; ENB coexistence needs a separate architecture project.

The Camera page now has **External post-processing (experimental)**. It registers
with an already loaded ReShade runtime only if that runtime accepts API 20. A
compatible ReShade build with add-on support is required; PIXL does not install or
bundle it. Use Refresh installed ReShade presets to discover INI presets beside
SkyrimSE.exe or in its ReShadePresets subfolder. The dropdown and effects switch
submit changes to ReShade's DX11 callback. Other preset locations remain available
through ReShade's own UI. Switching a preset may compile ReShade effects.

The bridge does not bypass, duplicate or manually schedule ReShade's render pass.
It ignores DX12 runtimes and never writes ENB files. It is a control integration,
not proof that every effect, depth buffer or presenter works together. No runtime
pointer is invoked from the PIXL menu thread. The vendor headers are pinned and
an unsupported API fails registration without disabling PIXL.

Core ReShade is not explicitly rejected by name in the current startup code.
`SSEReShadeHelper.dll` is explicitly rejected. This distinction is not proof that
ReShade works: swap-chain ownership, HDR, depth access, UI ordering, reconstruction
and frame generation need live validation. Do not remove the helper conflict check
or overwrite another tool's proxy DLL merely to force a launch.

A live ReShade validation pass should begin with SDR, frame generation and NR off and
simple colour effects, then test depth effects, DLSS/FSR, HDR and frame generation
individually. Record ReShade version, proxy arrangement and GPU. No ReShade/ENB
compatibility result is claimed by this source review.

### Experimental ENB preset translator

The same external-post workspace can read a disabled ENB preset as a source for a
bounded PIXL Camera Suite translation. Refresh performs an on-demand, bounded
scan under the active Skyrim install, ignores links and large/volatile game roots,
and only accepts ordinary `enbseries.ini` files. Selecting a result changes
nothing. Applying it records a PIXL-only camera restore point and maps recognised
colour, exposure, adaptation, tone and bloom controls through the normal Camera
Suite setter. It does not write an ENB file, load an ENB DLL or compile an
imported effect shader.

ENB Helper annotations such as `UIName`, `UIGroup`, `UIBinding` and `Separation`
describe controls for ENB's own effect runtime. PIXL may report annotated source
and weather-separated parameters as preset metadata, but keeps PIXL's weather
model authoritative and imports only stable supported base/day values. Use
**Restore before import** to return to the snapshot made before the first import.

## Neural Rendering startup choice

Quick Setup includes Enable Neural Rendering (experimental). Enabling it selects
DLAA when a non-DLSS image path was selected. NR requires supported NVIDIA hardware,
the installed NR runtime, DLSS/DLAA, SDR and a windowed/borderless presentation.
Save & Continue uses the existing restart notice when the NR presentation resources
were not prepared at boot. Continue for now retains the available fallback; the
exit action does not save game progress. NR remains off by default.

## Shader preparation without gameplay popups

Startup preparation keeps its progress screen. Finishing it or entering a game
switches subsequent work to the existing background compiler budget. Compilation
failure notices stay in the PIXL settings/log after that handoff rather than
appearing over gameplay. Default asynchronous compilation and disk caching remain
enabled. No GPU bytecode or shared shader ABI changed in this update.

The provided cache is validated against the package metadata; it cannot promise
all possible mod permutations are already compiled. Background CPU work and a
temporary existing shader fallback can still be visible. This policy controls
PIXL's UI, not ReShade's own effect compilation screen.

## Effects that appear inactive

- **Grass interaction:** built in, enabled by Ground Response. No separate
  interaction mod or Complex Grass texture pack is required. Test nearby ordinary
  terrain grass in third person. The CPU updates the collision field and binds VS
  t100; RunGrass invokes the displacement path. Static plants and distant LOD are
  not guaranteed receivers. A failing grass shader can prevent the effect.
- **WindowLife:** room artwork is included. No separate parallax-window mod is
  required. Material classification uses texture-path evidence for architectural
  glass; replacement textures with unrecognized names can be missed. Boarded
  windows and window-shadow mask proxies are intentionally excluded. Enable Window
  Life and use Show Window Class Overlay under its diagnostics to identify missing
  classification. Supply the affected location, mesh/texture pack and log before
  broadening detection, which can otherwise paint rooms on walls and roofs.
- **Authored material effects:** a renderer cannot recover missing authored height,
  roughness or normal data perfectly. Specialized material paths depend on suitable
  assets; this is distinct from installing another shader renderer.
- **No menu and no effects:** check PIXLRenderer.log, SKSE version/dependencies and
  conflict messages. Page Down opens settings; Home opens Photo Mode.
- **Repeated compiler screen:** fixed in the updated source by releasing foreground
  mode after startup and when entering a game. New permutations still compile in
  the background; a bundled cache cannot cover every mod combination. Existing
  public archives need rebuilding to receive this fix.

## Embers XD on Skyrim 1.5.97

The project owner confirmed successful startup with Embers XD and **Backported
Extended ESL Support (BEES) 1.2** on **Skyrim Special Edition 1.5.97**, September 14,
2026. This confirms the reported startup issue is resolved; it is not an exhaustive
visual or gameplay compatibility test.

The installed `Embers XD.esp` has a 1.71 plugin header. BEES was absent during the
failed launches. Installing BEES resolved the crash. This is a plugin-format
compatibility requirement for this tested installation, not a PIXL shader fix.
See the [BEES author's explanation](https://github.com/Nukem9/skyrimse-backported-esl-support#purpose).

Enable Embers compatibility patches only when their required masters are installed
and enabled. This installation also had ELFX/Lux Via patches and generated LOD
plugins referencing absent lighting masters; those were disabled during diagnosis.

The Embers/BEES result applies to the tested 1.5.97 installation.
