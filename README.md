# PIXL Renderer

### A single, curated DirectX 11 rendering pipeline for Skyrim Special Edition

PIXL Renderer is my attempt to make Skyrim feel properly modern without losing the atmosphere that makes Skyrim *Skyrim*. It brings lighting, materials, characters, weather, terrain, water, image reconstruction and camera finishing into one renderer that is designed, tuned and tested as a complete system.

The basic idea is simple: install one renderer, choose a quality profile and play. There is still a full tuning workspace for anyone who enjoys moving sliders for three hours and then returning them almost exactly to where they started—I have absolutely never done this, obviously lol.

> **Project status:** PIXL Renderer v1.0 is in active beta development. The current working build is the visual and functional baseline while compatibility, performance and final release packaging are validated.

## What makes PIXL different?

PIXL is not a loose bundle of unrelated effects. A native SKSE plugin coordinates an integrated set of DirectX 11 shader modules, shared frame data, material classification, runtime resources, quality profiles and diagnostics. The modules are allowed to understand the rest of the renderer, so changes to lighting, wetness, skin, foliage or reconstruction can be tuned together instead of fighting one another.

At runtime the renderer:

1. hooks the relevant Skyrim render and shader setup paths;
2. classifies the current draw, material, scene and weather state;
3. binds the shared PIXL frame/material data and module resources;
4. selects or compiles the required shader permutation;
5. evaluates the integrated HLSL pipeline; and
6. hands the finished frame to reconstruction, HDR/camera processing, profiling and capture.

Compiled pipelines are cached locally after first use. A clean install therefore compiles for the player's own hardware; the first start can take a while because Skyrim has a truly heroic number of shader permutations hiding under the floorboards lol.

## Renderer features

PIXL currently contains **37 integrated rendering modules**, plus renderer-level systems for dialogue focus, quality orchestration, benchmarking, tuning and capture.

### Lighting and atmosphere

- **Radiance Weave (Hybrid GI)** combines detailed screen-space diffuse/specular indirect lighting with a persistent two-cascade world irradiance cache, secondary bounce, directional visibility/bent normals, confidence-aware temporal accumulation and edge-aware denoising. Valid history is retained during rapid camera rotation instead of being discarded merely because the view moved quickly.
- **Radiant Grid** replaces Skyrim's four-light restriction with clustered dynamic-light handling. Particle-derived candles, torches and fires are deduplicated by emitter and retain a short bounded submission history so their illumination remains stable through turns and brief visibility changes.
- **Linear Light Core** performs lighting in a more appropriate colour space so PBR, emissive and indirect-light calculations behave consistently.
- **Natural Lighting** adds physically motivated inverse-square attenuation with controlled falloff.
- **Ambient Probe** derives ambient irradiance from environment and sky cubemaps using spherical harmonics.
- **World Probes** creates dynamic environment captures for reflections and irradiance.
- **Contact Shadows** restores fine near-surface shadow detail that conventional shadow maps miss.
- **Light Volumes** adds atmospheric scattering, volumetric depth and god rays for interiors and exteriors.
- **Interior Daylight** allows supported interiors to receive sun/moon lighting and shadows while addressing culling-related leaks.
- **Sky Bounce**, **Sky Continuity** and **Sky Veil** coordinate outdoor ambient light, celestial direction/moon state and moving cloud shadows.
- **Atmosphere** provides height-aware fog and volumetric atmospheric integration.

### Materials, characters and close-up detail

- **Material Forge** unifies legacy and authored materials under an energy-conscious PBR response with roughness, metallic, displacement, clearcoat, fuzz, glints, decals and landscape support.
- **Material Layers** handles parallax occlusion mapping, height blending, terrain heightmaps and parallax self-shadowing.
- **Window Life** upgrades architectural glass with old-glass optics, recessed room atlases, curtains, furniture depth, varied occupants, stable architectural families and adaptive window fitting. It is designed to stay inside the actual pane instead of illuminating half of Solitude—which turns out to be quite an important detail lol.
- **Skin Optics** adds layered skin response, dual specular lobes, micro detail and dynamic wetness.
- **Tissue Diffusion** provides material-aware subsurface light transport for natural skin and other translucent surfaces.
- **Strand Shading** gives hair the stable legacy PIXL directional, tangent-based specular response and controllable highlight shift. The experimental Hair Reconstruction module has been retired from the shipping pipeline in favour of this known-good path.
- **Thin Surface** supports directional transmission and multiple translucent fabric/surface models.
- **Actor Surface Effects** adds bounded, contact-driven snow, mud and wetness accumulation to the player and nearby NPCs. Effects evolve over time, remain anchored in actor/model space, and share Ground Response and weather state rather than painting a fixed biome-height band onto every character.
- **Foliage Dynamics** improves grass and vegetation lighting, GGX-style specular response, subsurface transmission, complex-grass normal handling and natural material controls.
- **Rain Response** coordinates world-stable rain, gust layers, impact splashes, wet materials, puddles, ripples, roof-edge runoff and rain mist.

### Terrain, snow, mud and water

- **Ground Response** drives actor grass interaction and a persistent, layer-classified terrain surface for raised snow, compressed tracks and wet mud, including coherent depth, G-buffer and motion-vector replay.
- **Native Seasons compatibility** optionally reads the active Seasons of Skyrim state without creating a hard dependency. Resolved runtime materials remain authoritative, while season changes invalidate stale Ground Response classifications and incompatible deformation history so Turn of the Seasons and other season packs can transition safely.
- **Terrain Detail** reduces visible tiling with stochastic variation while remaining compatible with parallax materials.
- **Terrain Field** extends terrain material texture support and automatic terrain setup.
- **Terrain Seam** blends terrain and intersecting objects more naturally.
- **Terrain Occlusion** derives terrain shadowing from height data and current sun direction.
- **Distance Blend** smooths the visual transition between full-detail objects and LOD.
- **Water Optics** adds caustics, underwater lighting and improved surface response.
- **Waterbody** unifies close and distant water geometry/lighting to reduce the familiar water-LOD mismatch.
- **Horizon Blend** cooperates with the separate HorizonBlend plugin when present and leaves vanilla far-water behaviour untouched when it is not.

### Display, performance and creation tools

- **Image Reconstruction** integrates TAA, NVIDIA DLSS/DLAA, AMD FidelityFX Super Resolution and supported frame-generation paths. The optional DX11/DX12 interop Neural Rendering path keeps depth, motion and UI resources synchronized through Present, exposes the installed runtime's legitimate quality/tuning controls, and provides a truthful DLSS/NR scene-input quality selector. NVIDIA does not expose a safe application-side INT4/FP8 switch or transformer-layer count through the validated Feature 18 contract, so PIXL does not present invented controls.
- **Camera Suite** supports HDR10 output, 16-bit intermediate rendering, histogram exposure, highlight protection, local adaptation and optional experimental lens/sensor behaviour.
- **Pixel Capture** provides asynchronous lossless screenshots, HDR PNG output and a Director Photo Finish path with locked camera/input, temporary native/DLAA reconstruction and optional offline-quality Neural Rendering before the final composite is captured. Its 8/16/24-frame neural convergence modes run complete fresh model evaluations with valid depth, motion, jitter and history, then use a robust offline resolve to reject isolated temporal outliers without recursively feeding processed RGB back into a temporal model.
- **Pulse Profiler** exposes frame timing, FPS, draw calls, VRAM, shader timing and repeatable A/B performance comparisons.
- **PIXL World Benchmark** runs repeatable scene fly-throughs, records samples/settings and captures reference frames for performance and visual-fidelity comparison.
- **Quality Profiles** apply real Low/Medium/High/Ultra changes across renderer groups; a preset that does nothing is treated as a bug, not a feature.
- **Dialogue Focus** adds a character-only presentation layer during conversations so the NPC in front of you remains the visual priority.
- **PIXL Workshop and Tuning Workspace** expose the deeper controls and diagnostics without making them mandatory for normal play.

The complete module ancestry—including every renamed Community Shaders system—is documented in [ATTRIBUTION.md](ATTRIBUTION.md). That file is the authoritative provenance map; this page is the human-readable tour.

## Installing the beta

1. Install the PIXL Renderer archive with a mod manager.
2. Enable the included `PIXL-TerrainField.esp`.
3. Launch Skyrim through SKSE.
4. Press **End** to open PIXL Renderer.
5. Choose a quality profile, make any preferred Camera adjustments, then use **SAVE LOOK**.

PIXL is intended to own the engine-level shader pipeline. Do not combine it with ENB, ReShade, Kreate, another shader-hook renderer or a second PIXL installation unless a future compatibility note explicitly says otherwise.

A clean-cache package compiles shaders on the first launch. Let that process finish before judging performance or visuals. Ordinary HLSL changes should invalidate only affected permutations; deleting the whole pipeline library is reserved for deliberate cold-cache validation.

The release baseline ships with the coherent **Enhanced** quality profile and Skyrim-native **TAA** selected. Frame generation and Neural Rendering are off by default. NVIDIA RTX 30-series and newer users can select DLSS, restart once to provision PIXL's optional DX12 sidecar, and then toggle Neural Rendering live; AMD, Intel and RTX 20-series users retain the normal TAA/FSR/DLSS paths without the NR control. Frame-generation swap-chain changes still require a restart.

The public **Camera** page contains reconstruction, DLSS/FSR, Neural Rendering, frame-generation, limiter and latency controls alongside normal camera finishing. Advanced engineering diagnostics remain in the Tuning Workspace, but ordinary setup and Photo Mode do not require entering it.

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

If the repository lives under an unusually long Windows path, map a short local drive alias and pass it only as a build-time convenience:

```powershell
cmake --preset PIXL-12C -DPIXL_FFX_SHORT_BINARY_ROOT=P:/build/PIXL-12C
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

`BuildRelease.bat` provides the normal configure/build flow. Machine-specific deployment paths belong in an untracked `CMakeUserPresets.json` or explicit script arguments.

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

The covered PIXL Renderer source is distributed under the **GNU General Public License version 3**, with the retained Community Shaders modding/linking additional permission in [EXCEPTIONS.md](EXCEPTIONS.md). The full GPL text is in [COPYING](COPYING).

If you distribute a PIXL Renderer binary or a modified build, you must also provide the corresponding covered source under the applicable GPL-3.0 terms, preserve upstream and third-party notices, identify your modifications, and comply with the licences of bundled dependencies. Please read the actual licence files rather than treating this paragraph as legal advice.

Public source packages include the renderer source, shaders, build configuration and required notices. They intentionally omit generated build output, local shader caches, credentials, machine configuration, training data and unrelated private research that is not compiled, linked, loaded or packaged with the renderer. If something is used to build a public PIXL Renderer binary, it belongs on the corresponding-source side of that line—no creative hide-and-seek with the GPL.

Skyrim and related marks belong to Bethesda Softworks LLC. PIXL Renderer branding and PIXL-authored assets belong to PIXL Studio; upstream and third-party material retains its own copyright and licence.

## Music and everything else

When I am not staring at HLSL or waiting for Skyrim to compile one more permutation, I make music as PIXL too:

[X / Twitter](https://x.com/PIXLMUSIC) · [Spotify](https://open.spotify.com/artist/210hjvKOh716ZO4ErE3x22) · [SoundCloud](https://soundcloud.com/pixl-music) · [Instagram](https://www.instagram.com/pixlmusic)

Thanks for checking out PIXL Renderer. I am building this because I still love what Skyrim can look and feel like when all of its systems finally agree with each other—and because apparently leaving a 2011 renderer alone was never really an option lol.
