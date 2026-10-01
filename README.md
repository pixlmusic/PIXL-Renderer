# PIXL Renderer

### A single, curated DirectX 11 rendering pipeline for Skyrim Special Edition

PIXL Renderer modernizes Skyrim's lighting, materials, characters, weather, terrain, water and image reconstruction while preserving the atmosphere of the original game. Its systems are designed and tuned as one integrated renderer.

Install one renderer, complete Quick Setup and play. An optional advanced tuning workspace provides deeper control over the image.

> **Release candidate:** PIXL Renderer 1.0.5. See installation requirements and validation limits below before installing. Optional DLSSG proxy, Neural Rendering and Curved Surface Mapping paths remain experimental.

> **Release status:** 1.0.5 is a release candidate for advanced Skyrim setups. It has extensive owner testing, but GPU, runtime and mod combinations vary; keep a recoverable mod-manager profile and report reproducible issues with the PIXL log attached.

## 1.0.5 renderer upgrade

This source revision builds on the 1.0.4 baseline with final renderer integration, stability and quality-control work while retaining the established Skyrim SE DirectX 11 and shader-cache architecture:

- **Auto-DOF** with one authoritative autofocus path, physical-lens controls, half-resolution adaptive bokeh, separate near/far treatment, foreground coverage, edge-aware reconstruction, sky protection and Director/Photo/Video integration;
- **Contained Liquids** with effect-first potion classification, cached bottle-profile fitting, profile-aware volume fill, viscosity-aware slosh and bounded glass/liquid optics while preserving Skyrim's original bottle draw;
- **Distant Life** for restrained, depth-aware distant settlement light activity that remains compatible with atmosphere, HDR and camera bokeh;
- **Ground Response and water refinements** covering stable snow/mud deformation edges, material-specific response, flow, refraction, foam and temporal behavior;
- **Window Life, Material Forge, directional SSS and character rendering updates** with safer classification, more stable projection/material response and live-tested skin-lighting defaults;
- **PIXL Render Origin groundwork** for explicit large-world coordinate handling and validation without replacing Skyrim's camera-relative DX11 pipeline;
- **Hybrid GI and emitter lighting refinements** with deterministic world-cache updates, fixed-rate cache aging, transparent fire/emissive injection, warmer particle-light classification and bounded contact shadows;
- **reconstruction-aware camera and post processing** for TAA, DLSS/DLAA and FSR paths, with sharper UI separation and explicit history invalidation; and
- **public-source provenance and release tooling** with preserved upstream notices, module-level attribution, third-party notices, reproducible audits and package hygiene checks.

The concise changes since 1.0.4 are documented in [docs/CHANGELOG.md](docs/CHANGELOG.md); the earlier baseline audit remains in [docs/RELEASE_1.0.4_AUDIT.md](docs/RELEASE_1.0.4_AUDIT.md).

## What makes PIXL different?

PIXL is not a loose bundle of unrelated effects. A native SKSE plugin coordinates an integrated set of DirectX 11 shader modules, shared frame data, material classification, runtime resources, quality profiles and diagnostics. The modules are allowed to understand the rest of the renderer, so changes to lighting, wetness, skin, foliage or reconstruction can be tuned together instead of fighting one another.

At runtime the renderer:

1. hooks the relevant Skyrim render and shader setup paths;
2. classifies the current draw, material, scene and weather state;
3. binds the shared PIXL frame/material data and module resources;
4. selects or compiles the required shader permutation;
5. evaluates the integrated HLSL pipeline; and
6. hands the finished frame to reconstruction, HDR/camera processing, profiling and capture.

The release includes a preloaded pipeline library. Additional or invalidated shader permutations compile locally and are retained for subsequent sessions.

## Renderer features

PIXL currently contains **41 integrated rendering modules**, plus renderer-level systems for dialogue focus, Director/Photo/Video workflows, quality orchestration, benchmarking, tuning and capture. Individual experimental modules can remain disabled by default. One retired Hair Reconstruction source record remains solely for shared-shader ABI auditability and is not a runtime module.

### Lighting and atmosphere

- **Radiance Weave (Hybrid GI)** combines detailed screen-space diffuse/specular indirect lighting with a persistent two-cascade world irradiance cache, secondary bounce, directional visibility/bent normals, confidence-aware temporal accumulation and edge-aware denoising. Deterministic voxel selection, fixed-rate aging and frame-rate-independent response keep world-space lighting stable through camera movement and reconstruction changes.
- **Radiant Grid** replaces Skyrim's four-light restriction with clustered dynamic-light handling. Particle-derived candles, torches and fires are deduplicated by emitter, retain source-appropriate colour and flame variation, and provide conservative indirect-light injection and contact shadows where Skyrim has no shadow-map slice.
- **Linear Light Core** performs lighting in a more appropriate colour space so PBR, emissive and indirect-light calculations behave consistently.
- **Natural Lighting** adds physically motivated inverse-square attenuation with controlled falloff.
- **Ambient Probe** derives ambient irradiance from environment and sky cubemaps using spherical harmonics.
- **World Probes** creates dynamic environment captures for reflections and irradiance.
- **Contact Shadows** restores fine near-surface shadow detail that conventional shadow maps miss.
- **Light Volumes** adds atmospheric scattering, volumetric depth and god rays for interiors and exteriors.
- **Interior Daylight** allows supported interiors to receive sun/moon lighting and shadows while addressing culling-related leaks.
- **Sky Bounce**, **Sky Continuity** and **Sky Veil** coordinate outdoor ambient light, celestial direction/moon state and moving cloud shadows.
- **Atmosphere** provides weather-driven height fog and volumetric atmospheric integration with world-scale visibility protection, Mie-style directional response and stable temporal history during camera motion.

### Materials, characters and close-up detail

- **Material Forge** unifies legacy and authored materials under an energy-conscious PBR response with roughness, metallic, displacement, clearcoat, fuzz, glints, decals and landscape support.
- **Material Layers** handles parallax occlusion mapping, height blending, terrain heightmaps and parallax self-shadowing.
- **Window Life** upgrades architectural glass with old-glass optics, recessed room atlases, curtains, furniture depth, varied occupants, stable architectural families and adaptive window fitting. Projection and emission remain bounded to the physical pane to avoid light leaking across nearby architecture.
- **Skin Optics** adds layered skin response, dual specular lobes, micro detail and dynamic wetness. The eye path separately preserves sclera readability at grazing lid edges while keeping corneal reflections dielectric and stable at distance.
- **Tissue Diffusion** provides material-aware subsurface light transport for natural skin and other translucent surfaces.
- **Strand Shading** gives hair the stable legacy PIXL directional, tangent-based specular response and controllable highlight shift. The experimental Hair Reconstruction module has been retired from the shipping pipeline in favour of this known-good path.
- **Thin Surface** supports directional transmission and multiple translucent fabric/surface models.
- **Actor Surface Effects** adds bounded, contact-driven snow, mud and wetness accumulation to the player and nearby NPCs. Effects evolve over time, remain anchored in actor/model space, and share Ground Response and weather state rather than painting a fixed biome-height band onto every character.
- **Foliage Dynamics** improves grass and vegetation lighting, Hybrid GI reception through stable two-sided macro normals, directional screen-space shadow reception, GGX-style specular response, subsurface transmission, UV-safe complex-grass normal handling and natural material controls.
- **Rain Response** coordinates world-stable rain, gust layers, impact splashes, wet materials, puddles, ripples, roof-edge runoff and rain mist.
- **Contained Liquids** classifies supported potions by their dominant effects, fits and caches bottle profiles, resolves volume-correct fill levels and renders damped family-specific slosh, absorption, scattering, refraction, Fresnel response, meniscus and bubbles. Ambiguous containers retain normal Skyrim rendering.

### Terrain, snow, mud and water

- **Grass Collision** remains a dedicated actor-driven grass interaction field inside the Ground Response module.
- **PIXL Ground Response terrain deformation** is a separate persistent, layer-classified snow/mud surface with raised geometry, compressed tracks, and coherent depth, G-buffer, and motion-vector replay.
- **Native Seasons compatibility** optionally reads the active Seasons of Skyrim state without creating a hard dependency. Resolved runtime materials remain authoritative, while season changes invalidate stale Ground Response classifications and incompatible deformation history so Turn of the Seasons and other season packs can transition safely.
- **Terrain Detail** reduces visible tiling with stochastic variation while remaining compatible with parallax materials.
- **Terrain Field** extends terrain material texture support and automatic terrain setup.
- **Terrain Seam** blends terrain and intersecting objects more naturally.
- **Terrain Occlusion** derives terrain shadowing from height data and current sun direction.
- **Distance Blend** smooths the visual transition between full-detail objects and LOD.
- **Distant Life** adds restrained settlement and structure activity into distant scenery, with distance-scaled softness and intensity designed to read through atmosphere and Auto-DOF without becoming a second bloom pass.
- **Water Optics** adds surface-derived caustic bounce, underwater lighting, multi-scale flow response and high-resolution world-space contact/whitewater foam without a camera-following player decal.
- **Waterbody** unifies close and distant water geometry/lighting to reduce the familiar water-LOD mismatch.
- **Horizon Blend** cooperates with the separate HorizonBlend plugin when present and leaves vanilla far-water behaviour untouched when it is not.

### Display, performance and creation tools

- **Image Reconstruction** integrates TAA, NVIDIA DLSS/DLAA, AMD FidelityFX Super Resolution and supported frame-generation paths. The optional DX11/DX12 interop Neural Rendering path keeps depth, motion and UI resources synchronized through Present and exposes only quality controls supported by the installed runtime. NVIDIA does not expose application-side INT4/FP8 selection or transformer-layer counts through the validated Feature 18 contract, so those controls are intentionally omitted.
- **Camera Suite** supports HDR10 output, 16-bit intermediate rendering, histogram exposure, highlight protection, local adaptation and Auto-DOF. The native Auto-DOF path uses physical lens parameters, unified actor/depth autofocus, adaptive bokeh, foreground coverage and reconstruction-aware temporal handling across gameplay, Photo, Video and Director modes.
- **Pixel Capture** provides asynchronous lossless screenshots, HDR PNG output and a Director Photo Finish path with locked camera/input, temporary native/DLAA reconstruction and optional offline-quality Neural Rendering before the final composite is captured. Its 8/16/24-frame neural convergence modes run complete fresh model evaluations with valid depth, motion, jitter and history, then use a robust offline resolve to reject isolated temporal outliers without recursively feeding processed RGB back into a temporal model.
- **Pulse Profiler** exposes frame timing, FPS, draw calls, VRAM, shader timing and repeatable A/B performance comparisons.
- **PIXL World Benchmark** runs repeatable scene fly-throughs, records samples/settings and captures reference frames for performance and visual-fidelity comparison.
- **Quality Profiles** apply real Low/Medium/High/Cinematic workload contracts across renderer groups. High preserves the former Cinematic presentation; the new Cinematic tier uses genuinely larger ray, froxel, bokeh and surface budgets for exceptional GPUs and capture work.
- **Dialogue Focus** adds a character-only presentation layer during conversations so the NPC in front of you remains the visual priority.
- **PIXL Workshop and Tuning Workspace** expose the deeper controls and diagnostics without making them mandatory for normal play.

The complete module ancestry—including every renamed Community Shaders system—is documented in [ATTRIBUTION.md](ATTRIBUTION.md). That file is the authoritative provenance map; this page is the human-readable tour.

## Installation

### Optional Neural Rendering: manual installation

Neural Rendering (NR) is experimental and optional. **The NR runtime `nvngx_dlssnr.dll` is not included in PIXL or its FOMOD.** Normal rendering does not require it. PIXL does not download or install it automatically.

1. In **Quick Setup**, select **Enable Neural Rendering (experimental)** to open the four-card guide. You can also open **PIXL Renderer → Neural Rendering → NR Setup Guide**.
2. Open the [RenoDX Discord](https://discord.gg/renodx), find **dlss5-forum**, and open the **Patched DLSS-NR** discussion.
3. Use **Pinned Messages only**, and **ShortFuse's pinned version only** for the illustrated setup. Do not use unverified chat attachments or third-party mirrors. If that post is unavailable, leave NR disabled.
4. Obtain the runtime only if you have permission to use it. Extract it if supplied in an archive, then place the DLL at:
   ```text
   Skyrim Special Edition/Data/Shaders/ImageReconstruction/Streamline/nvngx_dlssnr.dll
   ```
5. Return to PIXL and select **I've copied it — Check File**. Green means found; red means missing; cyan means ready to confirm. This verifies file presence, **not authenticity, compatibility or licensing**.
6. Confirm and return to Quick Setup, then **Save & Continue**. Use supported NVIDIA hardware, DLSS/DLAA, SDR and borderless/windowed mode. Save your game, fully exit Skyrim, and relaunch through SKSE. A save reload is not enough: the DX11/DX12 sidecar is initialized at startup.

This is a third-party experimental runtime, not a NVIDIA-approved PIXL download. Availability and upstream instructions may change. Installing this silent 1.0.3 update does not require deleting your shader cache or resetting your settings.

### Requirements

- Skyrim Special Edition on Windows, with SKSE matching your game runtime.
- Engine Fixes and its prerequisites, matching that same runtime. PIXL checks for `Data/SKSE/Plugins/EngineFixes.dll`; it is not bundled.
- A DirectX 11-capable GPU. DLSS/DLAA require supported NVIDIA hardware and an available runtime.
- Borderless/windowed mode for frame generation and the optional Neural Rendering sidecar.

Owner testing covers Skyrim SE 1.5.97 and a reported successful GOG test whose executable was verified as 1.6.1179.0. Steam 1.6.1170 remains a separate validation target. See [mod compatibility and troubleshooting](docs/MOD_COMPATIBILITY.md) for assets, weather, grass interaction, WindowLife and ENB/ReShade distinctions.

### Install and first launch

1. Close Skyrim. Install the release ZIP with Vortex or Mod Organizer 2 as a normal **Data** mod. For manual installation, extract its contents into Skyrim's `Data` directory, not beside `SkyrimSE.exe`.
2. Enable the included `PIXL-TerrainField.esp`. Install required dependencies separately.
3. Disable other engine-level shader renderers and duplicate PIXL installations. Do not combine this release with Community Shaders, ENB or Kreate. ReShade is unvalidated; use a ReShade-free setup for the supported baseline. `SSEReShadeHelper.dll` is explicitly incompatible. Weather plugins and texture/mesh mods are separate from these renderers.
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

## Building from source

### Requirements

- Windows and Visual Studio 2022 with the x64 C++ toolchain
- CMake 4.2 or newer
- Git with submodule support
- vcpkg exposed through `VCPKG_ROOT`

Clone/update all pinned dependencies, configure the Release preset and build the renderer:

```powershell
git submodule update --init --recursive
cmake --preset PIXL-12C
cmake --build build/PIXL-12C --config Release --target PIXLRenderer --parallel
```

If the repository lives under an unusually long Windows path, expose it through
a short local build root and pass that root only as a build-time convenience:

```powershell
cmake --preset PIXL-12C -DPIXL_FFX_SHORT_BINARY_ROOT=<short-local-build-root>/PIXL-12C
```

The alias is not embedded into public source or release packages. See [SOURCE_DEPENDENCIES.md](SOURCE_DEPENDENCIES.md) for the exact submodule pins and build-patch details.

Run the source/package audit after building:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\tools\AuditPixlRenderer.ps1 `
  -BuildDirectory .\build\PIXL-12C\Release
```

Create a clean-cache test package with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\tools\StagePixlRendererStandalone.ps1 `
  -BuildDirectory .\build\PIXL-12C\Release `
  -SkipPipelineLibrary
```

`BuildRelease.bat` provides the normal configure/build flow. Local deployment
paths belong in an untracked `CMakeUserPresets.json` or explicit script arguments.

## Repository layout

| Path | Purpose |
|---|---|
| `engine/` | Native SKSE renderer, hooks, module orchestration, UI, capture and diagnostics |
| `pipeline/` | Integrated module descriptors, HLSL kernels and module-owned runtime assets |
| `distribution/` | Authored files copied into the Skyrim `Data` package |
| `cmake/` | Reproducible build integration, triplets, ports and pinned dependency patches |
| `tools/` | Packaging, auditing and public-source export utilities |
| `docs/` | Architecture, calibration and engineering references intended for publication |

`build/`, `bin/`, staged archives, shader pipeline libraries and machine-local configuration are generated data and are intentionally excluded from Git. A source release does **not** need a multi-gigabyte build directory; it needs the actual source and scripts required to reproduce the distributed binary.

## Community Shaders ancestry and credit

PIXL Renderer is **not** a clean-room renderer. It is a substantially modified work derived in large part from [Community Shaders](https://github.com/doodlum/skyrim-community-shaders), with [Community Shaders v1.8.3](https://github.com/community-shaders/skyrim-community-shaders/releases/tag/v1.8.3) used as the historical comparison baseline.

Community Shaders and its contributors retain full credit for upstream code, assets, infrastructure and ideas. PIXL Studio is credited only for the PIXL modifications and independently authored PIXL work. Renaming or extending an upstream module does not make its history disappear, and I have tried to make that distinction clear throughout the repository.

My genuine thanks go to the Community Shaders team and to everyone whose work made this branch of Skyrim graphics development possible. PIXL would not exist in this form without that foundation. The project is independent and is not endorsed by or affiliated with the Community Shaders team or Bethesda Game Studios.

For the detailed upstream-to-PIXL module map, modification notice and attribution rules, read [ATTRIBUTION.md](ATTRIBUTION.md). Third-party code, SDKs, shader fragments and fonts are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Licence

Unless a component says otherwise, the covered PIXL Renderer source is distributed under **GPL-3.0-or-later**, with the retained Community Shaders Modding Exception and GPL-3.0 Linking Exception (with Corresponding Source) in [EXCEPTIONS.md](EXCEPTIONS.md). The full GPL text is in [COPYING](COPYING).

If you distribute a PIXL Renderer binary or a modified build, you must also provide the corresponding covered source under the applicable GPL-3.0 terms, preserve upstream and third-party notices, identify your modifications, and comply with the licences of bundled dependencies. Please read the actual licence files rather than treating this paragraph as legal advice.

Public source packages include the renderer source, shaders, build configuration and required notices. They intentionally omit generated build output, local shader caches, credentials, machine configuration, training data and unrelated private research that is not compiled, linked, loaded or packaged with the renderer. Any material required to build a distributed PIXL Renderer binary must be included with the corresponding source as required by the applicable licence.

Skyrim and related marks belong to their respective owners. Source-code licensing is separate from project identity; see [TRADEMARKS.md](TRADEMARKS.md). Upstream and third-party material retains its own copyright and licence.

## Music and everything else

When I am not staring at HLSL or waiting for Skyrim to compile one more permutation, I make music as PIXL too:

[X / Twitter](https://x.com/PIXLMUSIC) · [Spotify](https://open.spotify.com/artist/210hjvKOh716ZO4ErE3x22) · [SoundCloud](https://soundcloud.com/pixl-music) · [Instagram](https://www.instagram.com/pixlmusic)

PIXL Renderer is built around a simple goal: preserve Skyrim's identity while making its rendering systems feel coherent, responsive and physically credible.
