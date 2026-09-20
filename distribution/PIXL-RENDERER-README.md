# PIXL Renderer 1.0.3

An integrated DirectX 11 renderer for Skyrim Special Edition.

## Installation

### Requirements

- Skyrim Special Edition on Windows, with SKSE matching your game runtime.
- Engine Fixes and its prerequisites, matching that same runtime. PIXL checks for `Data/SKSE/Plugins/EngineFixes.dll`; it is not bundled.
- A DirectX 11-capable GPU. DLSS/DLAA require supported NVIDIA hardware and an available runtime.
- Borderless/windowed mode for frame generation and the optional Neural Rendering sidecar.

Owner testing covers Skyrim SE 1.5.97, GOG 1.6.1179.0, and Steam 1.6.1170. The
SurfaceTides bridge uses one CommonLibSSE-NG universal DLL for 1.5.97, Steam
1.6.1170, GOG 1.6.1179 and 1.7.104. Its automated tests cover the shared binary;
visual confirmation currently covers Steam 1.6.1170, so the other runtime
combinations still require live bridge testing.

### Optional SurfaceTides compatibility

PIXL 1.0.3 includes the renderer side of the SurfaceTides bridge. The FOMOD offers
an optional bridge for the exact SurfaceTides 1.0.2 release. Select it only when
that original release is already installed. The choice replaces SurfaceTides' DLL,
water shader and INI with the PIXL-tuned integration preset. It enables
`AllowPIXL=1` automatically. Back up custom SurfaceTides tuning before selecting
the integration, then restart through SKSE.

Install SurfaceTides first and PIXL second. In Vortex, make PIXL load after
SurfaceTides; in Mod Organizer 2, place PIXL lower in the left pane. PIXL must
win the `SurfaceTides.dll`, `Water.hlsl` and `SurfaceTides.ini` conflicts. Do not
install a separate runtime-specific SurfaceTides DLL afterward—the PIXL DLL
already supports 1.5.97, Steam 1.6.1170, GOG 1.6.1179 and 1.7.104. Reinstall PIXL
and reselect the integration after any SurfaceTides reinstall or update.

SurfaceTides supplies tessellation/displacement; PIXL retains water shading and
optics. The preset strengthens and lengthens wilderness waves while the bridge
scales displacement to 55% in city locations and 25% in interiors. Without SurfaceTides,
PIXL uses its regular water. The compatibility patch
is unnecessary once a supported upstream SurfaceTides build implements the bridge.
Do not carry the bundled replacement DLL into a later SurfaceTides release.

### Install and first launch

1. Close Skyrim. Install the release ZIP with Vortex or Mod Organizer 2 as a normal **Data** mod. For manual installation, extract its contents into Skyrim's `Data` directory, not beside `SkyrimSE.exe`. PIXL installs its own files beneath `SKSE`, `Shaders` and `Interface`; do not replace the complete `Data\Shaders` directory.
2. Enable the included `PIXL-TerrainField.esp`. Install required dependencies separately.
3. Disable other engine-level shader renderers and duplicate PIXL installations. Do not combine this release with Community Shaders, ENB or Kreate. ReShade is unvalidated; use a ReShade-free setup for the supported baseline. `SSEReShadeHelper.dll` is explicitly incompatible. Weather plugins and texture/mesh mods are separate from these renderers.
4. Launch using your normal SKSE/mod-manager shortcut.
5. Complete Quick Setup: choose Off, TAA, FSR Quality, DLSS Quality or **DLAA**. DLAA uses native-resolution DLSS anti-aliasing rather than upscaling. NVIDIA options are disabled when unavailable.
6. Optionally check **Enable frame generation**. If setup requests a restart, save your game and relaunch normally. The confirmed exit option saves PIXL settings, **not game progress**, and does not automatically relaunch.
7. Use **Page Down** for PIXL Renderer, **Home** for Photo Mode, and **SAVE LOOK** to keep adjustments. Reopen **QUICK SETUP** whenever needed.

NMM's legacy virtual-install/symlink mode is not supported for PIXL shader
assets. It can leave `.symlink` placeholders where Skyrim expects real HLSL
files, which commonly presents as missing modules or repeated compilation. Use
Vortex, MO2, or a real manual extraction instead. If Vortex or MO2 reports a
file conflict, let PIXL win only the explicitly listed feature files and keep
the documented load order; do not enable a blanket `Data\Shaders` override.

The release ships owner-tuned fog and POM defaults, with reconstruction set to **Off/None**, frame generation off and real-time Neural Rendering off. Accepting the existing quality selection preserves the tuned look. The advanced tuner remains optional.

### Startup Neural Rendering

Quick Setup also offers **Enable Neural Rendering (experimental)** on supported
NVIDIA hardware. It requires DLSS/DLAA, the packaged NR runtime, SDR and
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

When supplied, `PIXL/PipelineLibrary` is a snapshot of a live-tested cache. Missing or invalidated permutations compile as needed, and no bundled cache can cover every mod combination. Let compilation finish before evaluating performance. Do not routinely delete the cache: module-scoped updates preserve unaffected stages, while shared ABI/layout changes may require wider recompilation.

Update through your mod manager with Skyrim closed. Existing `SKSE/Plugins/PIXL/Config/UserGraphics.json` settings take precedence over new defaults. For a fresh default test, back up that file outside Data and remove the live copy while Skyrim is closed. The ZIP never ships a user config.

Required runtime folders retain their existing paths; moving them breaks shader/resource loading. Package documentation and licence notices are grouped under `SKSE/Plugins/PIXL/Documentation`. The package manifest records exact source revision and payload hashes. Logs, developer reports, build tools and debug symbols are not part of the plugin payload.


Source: [PIXL Renderer on GitHub](https://github.com/pixlmusic/PIXL-Renderer).
Licences, attribution and source notices are included in
`SKSE/Plugins/PIXL/Documentation`. This release has owner live-test coverage,
not an exhaustive hardware, security or mod-compatibility certification.

PIXL Renderer is owner-directed and has been developed through hands-on testing,
experimentation and substantial AI assistance. AI tools assisted with code,
documentation, diagnostics and iteration; visual direction, release decisions
and live game testing remain under PIXL Studio's direction.
