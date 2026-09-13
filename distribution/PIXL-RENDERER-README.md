# PIXL Renderer 1.0

An integrated DirectX 11 renderer for Skyrim Special Edition.

## Installation

### Requirements

- Skyrim Special Edition on Windows, with SKSE matching your game runtime.
- Engine Fixes and its prerequisites, matching that same runtime. PIXL checks for `Data/SKSE/Plugins/EngineFixes.dll`; it is not bundled.
- A DirectX 11-capable GPU. DLSS/DLAA require supported NVIDIA hardware and an available runtime.
- Borderless/windowed mode for frame generation and the optional Neural Rendering sidecar.

The owner-tested baseline is Skyrim SE 1.5.97. Other runtimes and mod combinations require validation; build support alone is not a compatibility guarantee.

### Install and first launch

1. Close Skyrim. Install the release ZIP with Vortex or Mod Organizer 2 as a normal **Data** mod. For manual installation, extract its contents into Skyrim's `Data` directory, not beside `SkyrimSE.exe`.
2. Enable the included `PIXL-TerrainField.esp`. Install required dependencies separately.
3. Disable other engine-level shader renderers and duplicate PIXL installations. Do not combine this release with Community Shaders, ENB, ReShade or Kreate.
4. Launch using your normal SKSE/mod-manager shortcut.
5. Complete Quick Setup: choose Off, TAA, FSR Quality, DLSS Quality or **DLAA**. DLAA uses native-resolution DLSS anti-aliasing rather than upscaling. NVIDIA options are disabled when unavailable.
6. Optionally check **Enable frame generation**. If setup requests a restart, save your game and relaunch normally. The confirmed exit option saves PIXL settings, **not game progress**, and does not automatically relaunch.
7. Use **Page Down** for PIXL Renderer, **Home** for Photo Mode, and **SAVE LOOK** to keep adjustments. Reopen **QUICK SETUP** whenever needed.

The release ships owner-tuned fog and POM defaults, with reconstruction set to **Off/None**, frame generation off and real-time Neural Rendering off. Accepting the existing quality selection preserves the tuned look. The advanced tuner remains optional.

### Optional DLSS frame-generation proxy

The third-party [DLSSG proxy project](https://github.com/sdli1995/dlssg_for_sm86) is **not bundled** and is not required for ordinary DLSS/DLAA or FSR frame generation. Consult its [English installation guide](https://github.com/sdli1995/dlssg_for_sm86/blob/main/README.en.md) for current requirements and supported configurations.

For a compatible proxy setup, close Skyrim and install the upstream proxy DLL and `dlssg_sm86.ini` **beside `SkyrimSE.exe`, not inside Data**. Do not overwrite another mod's proxy DLL; use only an upstream-supported alternative entry point if appropriate. Install only one proxy from that project. Relaunch, select the DLSSG backend in PIXL's Camera controls and enable frame generation; another restart may be required to provision the sidecar.

PIXL greys out DLSSG when neither native support nor the expected proxy files are detected. File detection is not proof of compatibility. This optional path remains experimental and needs hardware-specific testing. Use FSR frame generation if the DLSSG path is unavailable.

### Cache and updating

The release includes a snapshot of the live-tested `PIXL/PipelineLibrary`. Missing or invalidated permutations compile as needed; the bundled cache cannot cover every mod combination. Let compilation finish before evaluating performance. Do not routinely delete the cache: module-scoped updates preserve unaffected stages, while shared ABI/layout changes may require wider recompilation.

Update through your mod manager with Skyrim closed. Existing `SKSE/Plugins/PIXL/Config/UserGraphics.json` settings take precedence over new defaults. For a fresh default test, back up that file outside Data and remove the live copy while Skyrim is closed. The ZIP never ships a user config.

Required runtime folders retain their existing paths; moving them breaks shader/resource loading. Package documentation and licence notices are grouped under `SKSE/Plugins/PIXL/Documentation`. The package manifest records exact source revision and payload hashes. Logs, developer reports, build tools and debug symbols are not part of the plugin payload.


Source: [PIXL Renderer on GitHub](https://github.com/pixlmusic/PIXL-Renderer).
Licences, attribution and source notices are included in
`SKSE/Plugins/PIXL/Documentation`. This release has owner live-test coverage,
not an exhaustive hardware, security or mod-compatibility certification.
