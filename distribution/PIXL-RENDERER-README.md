# PIXL Renderer 1.0

An integrated DirectX 11 renderer for Skyrim Special Edition.

## Installation

### Requirements

- Skyrim Special Edition on Windows, with SKSE matching your game runtime.
- Engine Fixes and its prerequisites, matching that same runtime. PIXL checks for `Data/SKSE/Plugins/EngineFixes.dll`; it is not bundled.
- A DirectX 11-capable GPU. DLSS/DLAA require supported NVIDIA hardware and an available runtime.
- Borderless/windowed mode for frame generation and the optional Neural Rendering sidecar.

Owner testing covers Skyrim SE 1.5.97 and a reported successful GOG test whose executable was verified as 1.6.1179.0. Steam 1.6.1170 remains a separate validation target. Build support alone is not a compatibility guarantee.

### Install and first launch

1. Close Skyrim. Install the release ZIP with Vortex or Mod Organizer 2 as a normal **Data** mod. For manual installation, extract its contents into Skyrim's `Data` directory, not beside `SkyrimSE.exe`.
2. Enable the included `PIXL-TerrainField.esp`. Install required dependencies separately.
3. Disable other engine-level shader renderers and duplicate PIXL installations. Do not combine this release with Community Shaders, ENB or Kreate. ReShade is unvalidated; use a ReShade-free setup for the supported baseline. `SSEReShadeHelper.dll` is explicitly incompatible. Weather plugins and texture/mesh mods are separate from these renderers.
4. Launch using your normal SKSE/mod-manager shortcut.
5. Complete Quick Setup: choose Off, TAA, FSR Quality, DLSS Quality or **DLAA**. DLAA uses native-resolution DLSS anti-aliasing rather than upscaling. NVIDIA options are disabled when unavailable.
6. Optionally check **Enable frame generation**. If setup requests a restart, save your game and relaunch normally. The confirmed exit option saves PIXL settings, **not game progress**, and does not automatically relaunch.
7. Use **Page Down** for PIXL Renderer, **Home** for Photo Mode, and **SAVE LOOK** to keep adjustments. Reopen **QUICK SETUP** whenever needed.

The release ships owner-tuned fog and POM defaults, with reconstruction set to **Off/None**, frame generation off and real-time Neural Rendering off. Accepting the existing quality selection preserves the tuned look. The advanced tuner remains optional.

### Startup Neural Rendering

Quick Setup also offers **Enable Neural Rendering (experimental)** on supported
NVIDIA hardware. It requires DLSS/DLAA, the installed NR runtime, SDR and
borderless/windowed mode. Enabling it selects DLAA if needed. Save & Continue
advises a restart when NR or frame-generation presentation resources were not
prepared at launch. Continue for now is available; exiting does not save gameplay.

### Experimental ReShade camera controls

Camera > External post-processing can control an already installed ReShade build
with add-on API 20 support. Refresh finds INI presets beside SkyrimSE.exe and in
ReShadePresets. Select a preset or toggle ReShade effects there; changes run in
ReShade's callback. Its own effect compilation can still occur when switching.
Start validation with SDR, NR and frame generation off. Depth effects and alternate
presenters remain unvalidated. PIXL does not bundle ReShade, ENB or their presets.
ENB presets cannot run inside PIXL's camera pipeline; ENB remains incompatible.

### Optional DLSS frame-generation proxy installation

The third-party [DLSSG proxy project](https://github.com/sdli1995/dlssg_for_sm86) is **not bundled** and is not required for ordinary DLSS/DLAA or FSR frame generation. Consult its [English installation guide](https://github.com/sdli1995/dlssg_for_sm86/blob/main/README.en.md) for current requirements and supported configurations.

For a compatible proxy setup, close Skyrim and install the upstream proxy DLL and `dlssg_sm86.ini` **beside `SkyrimSE.exe`, not inside Data**. Do not overwrite another mod's proxy DLL; use only an upstream-supported alternative entry point if appropriate. Install only one proxy from that project. Relaunch, select the DLSSG backend in PIXL's Camera controls and enable frame generation; another restart may be required to provision the sidecar.

PIXL greys out DLSSG when neither native support nor the expected proxy files are detected. File detection is not proof of compatibility. This optional path remains experimental and needs hardware-specific testing. Use FSR frame generation if the DLSSG path is unavailable.

### Cache and updating

The updated renderer releases the full-screen compilation panel after startup. Newly encountered combinations compile in the background instead of reopening it during travel. This does not eliminate compilation cost or guarantee every possible combination is prepared before play.

### Common mod questions

Keep desired texture, mountain, grass and tree replacers: PIXL supplies rendering, not replacement assets. Weather plugins and light-placement overhauls also perform different jobs; retain their required patches and avoid overlapping weather setups. NAT's weather plugin is separate from ENB, but NAT.ENB III was designed around its ENB preset, so its appearance under PIXL needs testing.

Grass interaction is built in under Ground Response and needs no separate interaction mod. It affects nearby terrain grass, not every static shrub or distant LOD. WindowLife includes its room artwork and needs no separate parallax-window mod, but depends on recognized architectural glass materials. Unrecognized replacement texture names may need compatibility work. Use its window classification diagnostic and provide the affected asset/location when reporting a missed window.

Embers XD startup was confirmed by the owner on Skyrim 1.5.97 with **BEES 1.2**. The tested Embers ESP uses header 1.71 and crashed without that compatibility support. Enable compatibility patches only with their required masters.

For detailed limits and troubleshooting, see the [compatibility guide](https://github.com/pixlmusic/PIXL-Renderer/blob/main/docs/MOD_COMPATIBILITY.md).

### Updating and cache preservation

The release includes a snapshot of the live-tested `PIXL/PipelineLibrary`. Missing or invalidated permutations compile as needed; the bundled cache cannot cover every mod combination. Let compilation finish before evaluating performance. Do not routinely delete the cache: module-scoped updates preserve unaffected stages, while shared ABI/layout changes may require wider recompilation.

Update through your mod manager with Skyrim closed. Existing `SKSE/Plugins/PIXL/Config/UserGraphics.json` settings take precedence over new defaults. For a fresh default test, back up that file outside Data and remove the live copy while Skyrim is closed. The ZIP never ships a user config.

Required runtime folders retain their existing paths; moving them breaks shader/resource loading. Package documentation and licence notices are grouped under `SKSE/Plugins/PIXL/Documentation`. The package manifest records exact source revision and payload hashes. Logs, developer reports, build tools and debug symbols are not part of the plugin payload.


Source: [PIXL Renderer on GitHub](https://github.com/pixlmusic/PIXL-Renderer).
Licences, attribution and source notices are included in
`SKSE/Plugins/PIXL/Documentation`. This release has owner live-test coverage,
not an exhaustive hardware, security or mod-compatibility certification.
