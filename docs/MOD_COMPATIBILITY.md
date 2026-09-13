# PIXL mod compatibility and troubleshooting

## Runtime test coverage

The owner reports successful testing on GOG. The executable in the supplied GOG
installation was inspected on September 14, 2026 and reports **1.6.1179.0**.
Steam 1.6.1170 is a separate target; this test does not reproduce that user's setup.
Skyrim 1.5.97 also has owner test coverage. Use SKSE and dependencies matching the
actual executable version, not the SE/AE product name.

## What should I keep?

PIXL changes rendering; it does not replace the world's textures, meshes, grass
placements, weather records or placed lights.

| Mod category | Guidance |
|---|---|
| Mountain, landscape, architecture and character textures/meshes | Normally keep. Authored material data determines which material effects are available; individual packs still need testing. |
| Grass and tree replacers | Normally keep. PIXL supplies shading and interaction, not replacement vegetation assets. Distant LOD and static shrubs are different from terrain grass. |
| Weather plugins | Normally keep one coherent weather setup and its required patches. PIXL reacts to game weather; it does not replace every weather plugin. |
| Light placement and interior overhauls | Keep if desired, with their dependencies/patches. PIXL lighting does not reproduce their edits to placed lights or architecture. |
| Embers XD | Confirmed startup with BEES 1.2 on 1.5.97; see below. |
| Community Shaders / ENB renderer | Cannot run alongside this PIXL release. |
| ReShade | Experimental Camera-page bridge for an installed API 20 compatible add-on runtime; live rendering compatibility remains unvalidated. |
| Duplicate shader/upscaler helpers | Follow PIXL's startup diagnostics. Existing conflicts include SkyrimUpscaler, EVLaS/AELAS, SSEReShadeHelper, TAASharpen and NVIDIA_Reflex. |

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
