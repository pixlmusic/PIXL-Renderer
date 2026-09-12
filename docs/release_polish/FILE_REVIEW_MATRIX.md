# PIXL Renderer Active File Review Matrix

Final September 12 packaging/weather/tips pass: [FINAL_RELEASE_20260912.md](FINAL_RELEASE_20260912.md)
records the additional active-file review, native weather tests, cache provenance,
public release guards and remaining validation limitations.

September 12 startup/fog follow-up: [scoped file accounting](STARTUP_FOG_WINDOW_EMISSION_20260912.md)
records each changed active file, config provenance, cache-preserving deployment,
validation and future work for DLSS/Off setup, Photo Mode labels, current defaults,
fog tuning and interior background emission.

September 12 scoped RC follow-up: [per-file review and validation](RC_WINDOWLIFE_DAY_NIGHT_20260912.md)
contains the active/modified file accounting for setup, camera layout, WindowLife
day/night assets, ambient lighting, casting lights and sky protection. Every listed
file received scoped correctness, security, fidelity, performance and future-work
review; this does not claim a new exhaustive review of unrelated active files.

September 10 scoped Hybrid GI blur review: `HYBRID_GI_BLUR_20260910.md` records
the shader changes, unchanged binding contract, tests and deferred cache work.
`tools/TestPixlHybridGIBlur.ps1` is active, reviewed, added, security-reviewed
(explicit local paths; build-only outputs), fidelity-reviewed (numeric contract),
performance-reviewed (no runtime workload change), with future GPU tests recorded.

September 9 Reflex/defaults follow-up: see `REFLEX_ATMOSPHERE_20260909.md` for
the selected-path review and validation limitations. New validation file:

September 11 consumer-release pass: `CONSUMER_RELEASE_READINESS_20260911.md`
records first-run setup, public defaults, release cache staging, package
cleanliness, and current validation limits.

| Path | Subsystem | Active | Reviewed | Modified | Security reviewed | Fidelity reviewed | Performance reviewed | Future suggestions | Scope |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `tools/TestPixlAtmosphereDefaults.ps1` | Atmosphere default parity | YES | YES | Added | YES | Values only | N/A | YES | Read-only 54-field schema/float32 test |

Historical review ledger inherited from the September 3 baseline. The YES flags below record that earlier ledger's assertions, NOT proof of a fresh September 7 semantic review of every file. Some Modified flags also refer to earlier work. The September 7 pass has checked build/staging boundaries and selected source paths; its exhaustive per-file review is still incomplete. A module summary or automated content scan cannot establish a per-file semantic review. See `CURRENT_PASS_STATUS.md` for current evidence and outstanding release gates. Do not treat this matrix alone as a completion certificate.

September 9 reconstruction work is recorded in `IMAGE_RECONSTRUCTION_20260909.md`.
That report distinguishes startup tests from pending gameplay and full-file review;
it does not supersede the incomplete September 7 audit.

| File | Module | Build Active | Reviewed | Modified | Security | Fidelity | Perf | Future Ideas | September 7 scope |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `tools/SidecarSmoke.cpp` | Reconstruction diagnostic | Tool | YES | YES | Partial: vendor internals excluded | Pending GPU visuals | No benchmark | YES | Added September 9; full source |
| `tools/BuildSidecarSmoke.cmd` | Reconstruction diagnostic | Tool | YES | YES | YES | N/A | N/A | YES | Added September 9; full source |
| `tools/TestPixlSidecarRuntime.ps1` | Runtime validation | Tool | YES | YES | Metadata only | N/A | N/A | YES | Added September 9; full source |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/StreamlineDX12/sl.interposer.dll` | DLSS-G runtime | YES | Provenance only | Added | NOT audited internally | Pending | Pending | YES | September 9 official 2.10.3 |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/StreamlineDX12/sl.common.dll` | DLSS-G runtime | YES | Provenance only | Added | NOT audited internally | Pending | Pending | YES | September 9 official 2.10.3 |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/StreamlineDX12/sl.dlss_g.dll` | DLSS-G runtime | YES | Provenance only | Added | NOT audited internally | Pending | Pending | YES | September 9 official 2.10.3 |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/StreamlineDX12/sl.reflex.dll` | DLSS-G runtime | YES | Provenance only | Added | NOT audited internally | N/A | Pending | YES | September 9 official 2.10.3 |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/StreamlineDX12/sl.pcl.dll` | DLSS-G runtime | YES | Provenance only | Added | NOT audited internally | N/A | Pending | YES | September 9 official 2.10.3 |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/StreamlineDX12/nvngx_dlssg.dll` | NGX runtime | YES | Provenance only | Added | NOT audited internally | Pending | Pending | YES | September 9 official SDK bundle |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/StreamlineDX12/license.txt` | Vendor notices | YES | Retained | Added | N/A | N/A | N/A | N/A | September 9 official SDK bundle |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/StreamlineDX12/3rd-party-licenses.md` | Vendor notices | YES | Retained | Added | N/A | N/A | N/A | N/A | September 9 official SDK bundle |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/StreamlineDX12/nvngx_dlss.license.txt` | Vendor notices | YES | Retained | Added | N/A | N/A | N/A | N/A | September 9 official SDK bundle |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/StreamlineDX12/reflex.license.txt` | Vendor notices | YES | Retained | Added | N/A | N/A | N/A | N/A | September 9 official SDK bundle |
| `engine/Renderer/WorldBenchmark.cpp` | World Benchmark | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Renderer/WorldBenchmark.h` | World Benchmark | YES | YES | NO | YES | YES | YES | YES | Pending |
| `tools/AuditPixlRenderer.ps1` | Build / Release | YES | YES | YES | YES | YES | YES | YES | Full source |
| `tools/VerifyPixlPackageManifest.ps1` | Build / Release validation (new September 7) | YES | YES | YES | YES | N/A | YES | YES | Full source |
| `tools/TestPixlPackageManifest.ps1` | Build / Release tests (new September 7) | YES | YES | YES | YES | N/A | YES | YES | Full source |
| `tools/TestPixlComputeShaders.ps1` | Shader validation (new September 7) | YES | YES | YES | YES | N/A | YES | YES | Full source |
| `tools/TestPixlShaderDefines.ps1` | Compiler regression tests (new September 7) | YES | YES | YES | YES | N/A | YES | YES | Full source |
| `tools/TestPixlShaderIncludes.ps1` | Include regression tests (new September 7) | YES | YES | YES | YES | N/A | YES | YES | Full source |
| `tools/TestPixlShaderDependencies.ps1` | Hot-reload regression tests (new September 7) | YES | YES | YES | YES | N/A | YES | YES | Full source |
| `tools/TestPixlRCAS.ps1` | Reconstruction fallback tests (new September 7) | YES | YES | YES | YES | N/A | YES | YES | Full source |
| `tools/TestPixlRCASShader.ps1` | Reconstruction WARP/ABI tests (new September 7) | YES | YES | YES | YES | YES | YES | YES | Full source |
| `tools/CompareLiveShaders.ps1` | Build / Release | YES | YES | NO | YES | YES | YES | YES | Full source |
| `tools/ExportPixlPublicSource.ps1` | Build / Release | YES | YES | NO | YES | YES | YES | YES | Full source |
| `tools/StagePixlRendererStandalone.ps1` | Build / Release | YES | YES | YES | YES | YES | YES | YES | Full source |
| `tools/GeneratePixlReleaseDefaults.ps1` | Build / Release | YES | YES | YES | YES | YES | YES | YES | Full source |
| `.gitattributes` | Build / Release | YES | YES | NO | YES | YES | YES | YES | Full source |
| `.gitignore` | Build / Release | YES | YES | YES | YES | YES | YES | YES | Full source |
| `.gitmodules` | Build / Release | YES | YES | NO | YES | YES | YES | YES | Pending |
| `BuildDebug.bat` | Build / Release | YES | YES | NO | YES | YES | YES | YES | Pending |
| `BuildDev.bat` | Build / Release | YES | YES | NO | YES | YES | YES | YES | Pending |
| `BuildDevFast.bat` | Build / Release | YES | YES | NO | YES | YES | YES | YES | Pending |
| `BuildPR.bat` | Build / Release | YES | YES | NO | YES | YES | YES | YES | Pending |
| `BuildRelease.bat` | Build / Release | YES | YES | NO | YES | YES | YES | YES | Full source |
| `cmake/AddCXXFiles.cmake` | Build / Release | YES | YES | NO | YES | YES | YES | YES | Full source |
| `cmake/FidelityFX-SDK.cmake` | Build / Release | YES | YES | NO | YES | YES | YES | YES | Full source |
| `cmake/ModuleVersions.h.in` | Build / Release | YES | YES | YES | YES | YES | YES | YES | Full source |
| `cmake/patches/FidelityFX-DX11-Short-Output.patch` | Build / Release | YES | YES | NO | YES | YES | YES | YES | Pending |
| `cmake/Plugin.h.in` | Build / Release | YES | YES | NO | YES | YES | YES | YES | Full source |
| `cmake/ports/clib-util/portfile.cmake` | Build / Release | YES | YES | NO | YES | YES | YES | YES | Pending |
| `cmake/ports/clib-util/vcpkg.json` | Build / Release | YES | YES | NO | YES | YES | YES | YES | Pending |
| `cmake/ports/tracy/build-tools.patch` | Build / Release | YES | YES | NO | YES | YES | YES | YES | Pending |
| `cmake/ports/tracy/portfile.cmake` | Build / Release | YES | YES | NO | YES | YES | YES | YES | Pending |
| `cmake/ports/tracy/vcpkg.json` | Build / Release | YES | YES | NO | YES | YES | YES | YES | Pending |
| `cmake/shadertoolsconfig.json.in` | Build / Release | YES | YES | NO | YES | YES | YES | YES | Pending |
| `cmake/Streamline/CMakeLists.txt` | Build / Release | YES | YES | NO | YES | YES | YES | YES | Full source |
| `cmake/ThemePresets.h.in` | Build / Release | YES | YES | NO | YES | YES | YES | YES | Full source |
| `cmake/triplets/x64-windows-static-md-release.cmake` | Build / Release | YES | YES | NO | YES | YES | YES | YES | Pending |
| `cmake/Version.rc.in` | Build / Release | YES | YES | NO | YES | YES | YES | YES | Full source |
| `cmake/XSEPlugin.cmake` | Build / Release | YES | YES | NO | YES | YES | YES | YES | Full source |
| `CMakeLists.txt` | Build / Release | YES | YES | YES | YES | YES | YES | YES | Full source |
| `CMakePresets.json` | Build / Release | YES | YES | NO | YES | YES | YES | YES | Full source |
| `README.md` | Build / Release | YES | YES | YES | YES | YES | YES | YES | Pending |
| `SOURCE_DEPENDENCIES.md` | Build / Release | YES | YES | NO | YES | YES | YES | YES | Pending |
| `vcpkg.json` | Build / Release | YES | YES | NO | YES | YES | YES | YES | Full source |
| `build/PIXL-12C/cmake/ModuleVersions.h` | Generated Build Inputs | YES | YES | NO | YES | YES | YES | YES | Pending |
| `build/PIXL-12C/cmake/Plugin.h` | Generated Build Inputs | YES | YES | NO | YES | YES | YES | YES | Pending |
| `build/PIXL-12C/cmake/ThemePresets.h` | Generated Build Inputs | YES | YES | NO | YES | YES | YES | YES | Pending |
| `build/PIXL-12C/cmake/version.rc` | Generated Build Inputs | YES | YES | NO | YES | YES | YES | YES | Pending |
| `build/PIXL-12C/CMakeFiles/PIXLRenderer.dir/cmake_pch.cxx` | Generated Build Inputs | YES | YES | NO | YES | YES | YES | YES | Pending |
| `build/PIXL-12C/CMakeFiles/PIXLRenderer.dir/Release/cmake_pch.hxx` | Generated Build Inputs | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/AmbientProbe/Module.ini` | AmbientProbe | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Atmosphere/Module.ini` | Atmosphere | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Camera Suite/Module.ini` | CameraSuite | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Contact Shadows/Module.ini` | ContactShadows | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Distance Blend/Module.ini` | DistanceBlend | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Foliage Dynamics/Module.ini` | FoliageDynamics | YES | YES | NO | YES | YES | YES | YES | Full source |
| `pipeline/Ground Response/Module.ini` | GroundResponse | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/HorizonBlend/Module.ini` | HorizonBlend | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Hybrid GI/Module.ini` | HybridGI | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/ImageReconstruction/Module.ini` | ImageReconstruction | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Interior Daylight/Module.ini` | InteriorDaylight | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Light Volumes/Module.ini` | LightVolumes | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Linear Light Core/Module.ini` | LinearLightCore | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/MaterialForge/Module.ini` | MaterialForge | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Material Layers/Module.ini` | MaterialLayers | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Natural Lighting/Module.ini` | NaturalLighting | YES | YES | NO | YES | YES | YES | YES | Full source |
| `pipeline/Pixel Capture/Module.ini` | PixelCapture | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Pulse Profiler/Module.ini` | PulseProfiler | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Radiant Grid/Module.ini` | RadiantGrid | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Rain Response/Module.ini` | RainResponse | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/SkinOptics/Module.ini` | SkinOptics | YES | YES | YES | YES | YES | YES | YES | Pending |
| `pipeline/SkyBounce/Module.ini` | SkyBounce | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Sky Continuity/Module.ini` | SkyContinuity | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Sky Veil/Module.ini` | SkyVeil | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Strand Shading/Module.ini` | StrandShading | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Terrain Detail/Module.ini` | TerrainDetail | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Terrain Field/Module.ini` | TerrainField | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Terrain Occlusion/Module.ini` | TerrainOcclusion | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Terrain Seam/Module.ini` | TerrainSeam | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Thin Surface/Module.ini` | ThinSurface | YES | YES | NO | YES | YES | YES | YES | Full source |
| `pipeline/Tissue Diffusion/Module.ini` | TissueDiffusion | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Volume Occlusion/Module.ini` | VolumeOcclusion | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Waterbody/Module.ini` | Waterbody | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Water Optics/Module.ini` | WaterOptics | YES | YES | YES | YES | YES | YES | YES | Full source |
| `pipeline/WindowLife/Module.ini` | WindowLife | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/World Probes/Module.ini` | WorldProbes | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Buffer.h` | Core Renderer | YES | YES | YES | YES | YES | YES | YES | Full source |
| `engine/FrameAnnotations.cpp` | Core Renderer | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Globals.cpp` | Core Renderer | YES | YES | NO | YES | YES | YES | YES | Full source |
| `engine/Globals.h` | Core Renderer | YES | YES | NO | YES | YES | YES | YES | Full source |
| `engine/ModuleGroups.h` | Core Renderer | YES | YES | NO | YES | YES | YES | YES | Full source |
| `engine/ModuleRules.cpp` | Core Renderer | YES | YES | NO | YES | YES | YES | YES | Full source |
| `engine/ModuleRules.h` | Core Renderer | YES | YES | NO | YES | YES | YES | YES | Full source |
| `engine/PCH.cpp` | Core Renderer | YES | YES | NO | YES | YES | YES | YES | Full source |
| `engine/Profiler.cpp` | Core Renderer | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Profiler.h` | Core Renderer | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/RenderModule.cpp` | Core Renderer | YES | YES | NO | YES | YES | YES | YES | Full source |
| `engine/RenderModule.h` | Core Renderer | YES | YES | NO | YES | YES | YES | YES | Full source |
| `engine/SceneSettingsManager.cpp` | Core Renderer | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/SceneSettingsManager.h` | Core Renderer | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/SettingsOverrideManager.cpp` | Core Renderer | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/SettingsOverrideManager.h` | Core Renderer | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/State.cpp` | Core Renderer | YES | YES | YES | YES | YES | YES | YES | Partial |
| `engine/State.h` | Core Renderer | YES | YES | YES | YES | YES | YES | YES | Pending |
| `engine/Util.h` | Core Renderer | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/WeatherManager.cpp` | Core Renderer | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/WeatherManager.h` | Core Renderer | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/WeatherVariableRegistry.h` | Core Renderer | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/XSEPlugin.cpp` | Core Renderer | YES | YES | NO | YES | YES | YES | YES | Full source |
| `engine/Utils/ActorUtils.cpp` | Core Utilities | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Utils/ActorUtils.h` | Core Utilities | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Utils/D3D.cpp` | Core Utilities | YES | YES | NO | YES | YES | YES | YES | Partial |
| `engine/Utils/D3D.h` | Core Utilities | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Utils/ExternalEmittance.cpp` | Core Utilities | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Utils/ExternalEmittance.h` | Core Utilities | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Utils/FileSystem.cpp` | Core Utilities | YES | YES | YES | YES | YES | YES | YES | Full source |
| `engine/Utils/FileSystem.h` | Core Utilities | YES | YES | NO | YES | YES | YES | YES | Full source |
| `engine/Utils/Form.cpp` | Core Utilities | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Utils/Form.h` | Core Utilities | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Utils/Format.cpp` | Core Utilities | YES | YES | NO | YES | YES | YES | YES | Partial |
| `engine/Utils/Format.h` | Core Utilities | YES | YES | NO | YES | YES | YES | YES | Partial |
| `engine/Utils/Game.cpp` | Core Utilities | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Utils/Game.h` | Core Utilities | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Utils/GameSetting.cpp` | Core Utilities | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Utils/GameSetting.h` | Core Utilities | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Utils/Input.h` | Core Utilities | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Utils/LegitProfiler.h` | Core Utilities | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Utils/Moon.h` | Core Utilities | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Utils/PerfUtils.h` | Core Utilities | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Utils/RestartSettings.h` | Core Utilities | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Utils/Serialize.cpp` | Core Utilities | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Utils/Serialize.h` | Core Utilities | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Utils/SphericalHarmonics.cpp` | Core Utilities | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Utils/SphericalHarmonics.h` | Core Utilities | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Utils/Subrect.cpp` | Core Utilities | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Utils/Subrect.h` | Core Utilities | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Utils/UI.cpp` | Core Utilities | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Utils/UI.h` | Core Utilities | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Utils/WinApi.cpp` | Core Utilities | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Utils/WinApi.h` | Core Utilities | YES | YES | NO | YES | YES | YES | YES | Pending |
| `ATTRIBUTION.md` | Release Package | YES | YES | NO | YES | YES | YES | YES | Pending |
| `COPYING` | Release Package | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/PIXL-RENDERER-README.md` | Release Package | YES | YES | YES | YES | YES | YES | YES | Full source |
| `distribution/SKSE/Plugins/PIXLRenderer/Presets/PIXL-Renderer-Live-Tested.json` | Release Package | YES | YES | YES | YES | YES | YES | YES | Pending |
| `distribution/SKSE/Plugins/PIXLRenderer/SettingsDefault.json` | Release Package | YES | YES | YES | YES | YES | YES | YES | Full source |
| `distribution/SKSE/Plugins/PIXLRenderer/Presets/PIXL-Renderer-Enhanced.json` | Release Package | YES | YES | YES | YES | YES | YES | YES | Pending |
| `distribution/SKSE/Plugins/PIXLRenderer/Presets/PIXL-Renderer-Ultra.json` | Release Package | YES | YES | YES | YES | YES | YES | YES | Pending |
| `distribution/SKSE/Plugins/PIXLRenderer/Themes/PIXL.json` | Release Package | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/SKSE/Plugins/PIXLRenderer/Translations/en.json` | Release Package | YES | YES | YES | YES | YES | YES | YES | Partial |
| `distribution/SOURCE-AND-CREDITS.md` | Release Package | YES | YES | YES | YES | YES | YES | YES | Full source |
| `EXCEPTIONS.md` | Release Package | YES | YES | NO | YES | YES | YES | YES | Pending |
| `THIRD_PARTY_NOTICES.md` | Release Package | YES | YES | NO | YES | YES | YES | YES | Full source |
| `engine/MaterialForge.cpp` | MaterialForge | YES | YES | YES | YES | YES | YES | YES | Pending |
| `engine/MaterialForge.h` | MaterialForge | YES | YES | YES | YES | YES | YES | YES | Pending |
| `engine/MaterialForge/BSLightingShaderMaterialPBR.cpp` | MaterialForge | YES | YES | YES | YES | YES | YES | YES | Pending |
| `engine/MaterialForge/BSLightingShaderMaterialPBR.h` | MaterialForge | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/MaterialForge/BSLightingShaderMaterialPBRLandscape.cpp` | MaterialForge | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/MaterialForge/BSLightingShaderMaterialPBRLandscape.h` | MaterialForge | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/MaterialForge/DX11TextureResolver.cpp` | MaterialForge | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/MaterialForge/DX11TextureResolver.h` | MaterialForge | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/MaterialForge/PhysicalMaterial.h` | MaterialForge | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/MaterialForge/PhysicalMaterialRegistry.cpp` | MaterialForge | YES | YES | YES | YES | YES | YES | YES | Pending |
| `engine/MaterialForge/PhysicalMaterialRegistry.h` | MaterialForge | YES | YES | YES | YES | YES | YES | YES | Pending |
| `pipeline/AmbientProbe/Kernels/AmbientProbe/AmbientProbe.hlsli` | AmbientProbe | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/AmbientProbe/Kernels/AmbientProbe/DiffuseAmbientProbe.dds` | AmbientProbe | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/AmbientProbe/Kernels/AmbientProbe/DiffuseAmbientProbeCS.hlsl` | AmbientProbe | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/AmbientProbe/Kernels/AmbientProbe/SpecAmbientProbe.dds` | AmbientProbe | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Atmosphere/Kernels/Atmosphere/Atmosphere.hlsli` | Atmosphere | YES | YES | YES | YES | YES | YES | YES | Pending |
| `pipeline/Atmosphere/Kernels/Atmosphere/VolumetricFogCommon.hlsli` | Atmosphere | YES | YES | YES | YES | YES | YES | YES | Pending |
| `pipeline/Atmosphere/Kernels/Atmosphere/VolumetricFogConservativeDepthCS.hlsl` | Atmosphere | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Atmosphere/Kernels/Atmosphere/VolumetricFogCSCommon.hlsli` | Atmosphere | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Atmosphere/Kernels/Atmosphere/VolumetricFogIntegrationCS.hlsl` | Atmosphere | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Atmosphere/Kernels/Atmosphere/VolumetricFogLightScatteringCS.hlsl` | Atmosphere | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Atmosphere/Kernels/Atmosphere/VolumetricFogMaterialCS.hlsl` | Atmosphere | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Camera Suite/Kernels/CameraSuite/BloomDownsampleCS.hlsl` | CameraSuite | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Camera Suite/Kernels/CameraSuite/BloomPrefilterCS.hlsl` | CameraSuite | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Camera Suite/Kernels/CameraSuite/BloomUpsampleCS.hlsl` | CameraSuite | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Camera Suite/Kernels/CameraSuite/HDROutputCS.hlsl` | CameraSuite | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Camera Suite/Kernels/CameraSuite/HDRSun.hlsli` | CameraSuite | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Camera Suite/Kernels/CameraSuite/Lens/FireMask.png` | CameraSuite | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Camera Suite/Kernels/CameraSuite/Lens/FrostMask.png` | CameraSuite | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Camera Suite/Kernels/CameraSuite/Lens/PROVENANCE.md` | CameraSuite | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Camera Suite/Kernels/CameraSuite/Looks/Bleach.png` | CameraSuite | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Camera Suite/Kernels/CameraSuite/Looks/Bleak.png` | CameraSuite | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Camera Suite/Kernels/CameraSuite/Looks/Cinematic.png` | CameraSuite | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Camera Suite/Kernels/CameraSuite/Looks/Dramatic.png` | CameraSuite | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Camera Suite/Kernels/CameraSuite/Looks/FantasyGreen.png` | CameraSuite | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Camera Suite/Kernels/CameraSuite/Looks/Hearthfire.png` | CameraSuite | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Camera Suite/Kernels/CameraSuite/Looks/Nightfall.png` | CameraSuite | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Camera Suite/Kernels/CameraSuite/Looks/NordicNeutral.png` | CameraSuite | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Camera Suite/Kernels/CameraSuite/Looks/PROVENANCE.md` | CameraSuite | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Camera Suite/Kernels/CameraSuite/Looks/Saga.png` | CameraSuite | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Camera Suite/Kernels/CameraSuite/Looks/Sunset.png` | CameraSuite | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Camera Suite/Kernels/CameraSuite/Looks/Winter.png` | CameraSuite | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Camera Suite/Kernels/CameraSuite/PhysicalCameraCommon.hlsli` | CameraSuite | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Camera Suite/Kernels/CameraSuite/PhysicalCameraExposureCS.hlsl` | CameraSuite | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Camera Suite/Kernels/CameraSuite/PhysicalCameraHistogramCS.hlsl` | CameraSuite | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Camera Suite/Kernels/CameraSuite/PhysicalCameraLocalExposureCS.hlsl` | CameraSuite | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Camera Suite/Kernels/CameraSuite/Stormglass.hlsli` | CameraSuite | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Camera Suite/Kernels/CameraSuite/StormglassFieldCS.hlsl` | CameraSuite | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Camera Suite/Kernels/CameraSuite/UIBrightnessCS.hlsl` | CameraSuite | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Contact Shadows/Kernels/ContactShadows/bend_sss_gpu.hlsli` | ContactShadows | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Contact Shadows/Kernels/ContactShadows/ContactShadows.hlsli` | ContactShadows | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Contact Shadows/Kernels/ContactShadows/RaymarchCS.hlsl` | ContactShadows | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Foliage Dynamics/Kernels/FoliageDynamics/FoliageDynamics.hlsli` | FoliageDynamics | YES | YES | NO | YES | YES | YES | YES | Full source |
| `pipeline/Foliage Dynamics/Kernels/FoliageDynamics/FoliageTuning.hlsli` | FoliageDynamics | YES | YES | NO | YES | YES | YES | YES | Full source |
| `pipeline/Foliage Dynamics/Kernels/FoliageDynamics/FoliageWind.hlsli` | FoliageDynamics | YES | YES | NO | YES | YES | YES | YES | Full source |
| `pipeline/Ground Response/Kernels/GroundResponse/CollisionUpdateCS.hlsl` | GroundResponse | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Ground Response/Kernels/GroundResponse/DeformableGround.hlsli` | GroundResponse | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Ground Response/Kernels/GroundResponse/GroundResponse.hlsli` | GroundResponse | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Ground Response/Kernels/GroundResponse/Runtime.hlsli` | GroundResponse | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Ground Response/Kernels/GroundResponse/SNOW_MICRO_PROVENANCE.md` | GroundResponse | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Ground Response/Kernels/GroundResponse/SnowMicro.png` | GroundResponse | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Ground Response/Kernels/GroundResponse/SurfaceDeformationUpdateCS.hlsl` | GroundResponse | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Ground Response/Kernels/GroundResponse/TerrainSurface.hlsl` | GroundResponse | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Hybrid GI/Kernels/HybridGI/blur.cs.hlsl` | HybridGI | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Hybrid GI/Kernels/HybridGI/common.hlsli` | HybridGI | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Hybrid GI/Kernels/HybridGI/fast_2uges.dds` | HybridGI | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Hybrid GI/Kernels/HybridGI/gi.cs.hlsl` | HybridGI | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Hybrid GI/Kernels/HybridGI/hybridReflection.cs.hlsl` | HybridGI | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Hybrid GI/Kernels/HybridGI/hybridReflectionDenoise.cs.hlsl` | HybridGI | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Hybrid GI/Kernels/HybridGI/prefilterDepths.cs.hlsl` | HybridGI | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Hybrid GI/Kernels/HybridGI/prefilterNormal.cs.hlsl` | HybridGI | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Hybrid GI/Kernels/HybridGI/prefilterRadiance.cs.hlsl` | HybridGI | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Hybrid GI/Kernels/HybridGI/radianceDisocc.cs.hlsl` | HybridGI | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Hybrid GI/Kernels/HybridGI/upsample.cs.hlsl` | HybridGI | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Hybrid GI/Kernels/HybridGI/worldCache.hlsli` | HybridGI | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Hybrid GI/Kernels/HybridGI/worldCacheDecay.cs.hlsl` | HybridGI | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Hybrid GI/Kernels/HybridGI/worldCacheInject.cs.hlsl` | HybridGI | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/CopyDepthToSharedBufferPS.hlsl` | ImageReconstruction | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/DepthRefractionUpscalePS.hlsl` | ImageReconstruction | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/EncodeTexturesCS.hlsl` | ImageReconstruction | YES | YES | YES | YES | YES | YES | YES | Pending |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/FidelityFX/amd_fidelityfx_framegeneration_dx12.dll` | ImageReconstruction | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/FidelityFX/amd_fidelityfx_loader_dx12.dll` | ImageReconstruction | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/FidelityFX/license.md` | ImageReconstruction | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/RCAS/RCAS.hlsl` | ImageReconstruction | YES | YES | NO | YES | YES | YES | YES | Full source |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/Streamline/license.txt` | ImageReconstruction | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/Streamline/nvngx_dlss.dll` | ImageReconstruction | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/Streamline/nvngx_dlss.license.txt` | ImageReconstruction | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/Streamline/reflex.license.txt` | ImageReconstruction | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/Streamline/sl.common.dll` | ImageReconstruction | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/Streamline/sl.dlss.dll` | ImageReconstruction | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/Streamline/sl.interposer.dll` | ImageReconstruction | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/Streamline/sl.pcl.dll` | ImageReconstruction | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/Streamline/sl.reflex.dll` | ImageReconstruction | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/UnderwaterMaskUpscalePS.hlsl` | ImageReconstruction | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/UpscaleVS.hlsl` | ImageReconstruction | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Material Layers/Kernels/MaterialLayers/MaterialDetail.hlsli` | MaterialLayers | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Material Layers/Kernels/MaterialLayers/MaterialLayers.hlsli` | MaterialLayers | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Material Layers/Kernels/MaterialLayers/MaterialLayersParallaxCore.hlsli` | MaterialLayers | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Material Layers/Kernels/MaterialLayers/MaterialLayersTerrain.hlsli` | MaterialLayers | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Material Layers/Kernels/MaterialLayers/MaterialLayersTuning.hlsli` | MaterialLayers | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Natural Lighting/Kernels/NaturalLighting/NaturalLighting.hlsli` | NaturalLighting | YES | YES | NO | YES | YES | YES | YES | Full source |
| `pipeline/Radiant Grid/Kernels/RadiantGrid/ClusterBuildingCS.hlsl` | RadiantGrid | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Radiant Grid/Kernels/RadiantGrid/ClusterCullingCS.hlsl` | RadiantGrid | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Radiant Grid/Kernels/RadiantGrid/Common.hlsli` | RadiantGrid | YES | YES | NO | YES | YES | YES | YES | Full source |
| `pipeline/Radiant Grid/Kernels/RadiantGrid/RadiantGrid.hlsli` | RadiantGrid | YES | YES | NO | YES | YES | YES | YES | Full source |
| `pipeline/Rain Response/Kernels/RainResponse/optimized-ggx.hlsli` | RainResponse | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Rain Response/Kernels/RainResponse/Precipitation.hlsli` | RainResponse | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Rain Response/Kernels/RainResponse/RainResponse.hlsli` | RainResponse | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Rain Response/Kernels/RainResponse/RoofRunoffCompositeCS.hlsl` | RainResponse | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Rain Response/Kernels/RainResponse/RoofRunoffDetectCS.hlsl` | RainResponse | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Rain Response/Kernels/RainResponse/RoofRunoffGenerateCS.hlsl` | RainResponse | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Rain Response/Kernels/RainResponse/RoofRunoffResolveCS.hlsl` | RainResponse | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Rain Response/Kernels/RainResponse/WorldPrecipitation.hlsli` | RainResponse | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/ISApplyReflections.hlsl` | Screen-Space / Camera | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/ISDepthOfField.hlsl` | Screen-Space / Camera | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/ISHDR.hlsl` | Screen-Space / Camera | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/ISReflectionsRayTracing.hlsl` | Screen-Space / Camera | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/Common/BRDF.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES | Partial |
| `distribution/Shaders/Common/Color.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES | Partial |
| `distribution/Shaders/Common/DisplayMapping.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/Common/DummyVS.hlsl` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/Common/DummyVSTexCoord.hlsl` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/Common/FastMath.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/Common/FrameBuffer.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/Common/Game.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/Common/GBuffer.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/Common/Glints/Glints2023.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/Common/Glints/noisegen.cs.hlsl` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/Common/LightingCommon.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/Common/LightingEval.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/Common/LightingLandscape.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/Common/LodLandscape.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/Common/MaterialForgeTuning.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/Common/Math.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/Common/MotionBlur.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/Common/PBR.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/Common/PBRMath.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/Common/Permutation.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/Common/PhysicalMaterial.hlsli` | Shared Shader Math | YES | YES | YES | YES | YES | YES | YES | Pending |
| `distribution/Shaders/Common/Random.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/Common/Shading.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/Common/ShadowSampling.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/Common/SharedData.hlsli` | Shared Shader Math | YES | YES | YES | YES | YES | YES | YES | Partial |
| `distribution/Shaders/Common/Skinned.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/Common/Spherical Harmonics/LICENSE` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/Common/Spherical Harmonics/SphericalHarmonics.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/Common/Triplanar.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/SkinOptics/Kernels/SkinOptics/skin_detail_n.dds` | SkinOptics | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/SkinOptics/Kernels/SkinOptics/SkinOptics.hlsli` | SkinOptics | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/SkyBounce/Kernels/SkyBounce/SkyBounce.hlsli` | SkyBounce | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/SkyBounce/Kernels/SkyBounce/UpdateProbesCS.hlsl` | SkyBounce | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/BloodSplatter.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/DeferredCompositeCS.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/DialogueFocus/DialogueFocus.hlsli` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/Effect.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/EyeRendering/EyeRendering.hlsli` | Skyrim Shader Entry Points | YES | YES | YES | YES | YES | YES | YES | Pending |
| `distribution/Shaders/ISAlphaBlend.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/ISApplyVolumetricLighting.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/ISBasicCopy.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/ISBlur.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/ISCompositeLensFlareVolumetricLighting.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/ISCopy.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/ISDebugSnow.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/ISDoubleVision.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/ISDownsample.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/ISExp.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/ISIBLensFlare.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/ISLightingComposite.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/ISLocalMap.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/ISMap.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/ISNoise.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/ISRadialBlur.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/ISRefraction.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/ISSAOBlur.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/ISSAOCameraZ.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/ISSAOComposite.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/ISSAOMinify.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/ISSILComposite.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/ISSimpleColor.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/ISSnowSSS.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/ISTemporalAA.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/ISUpsampleDynamicResolution.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/ISVolumetricLighting.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/ISVolumetricLightingBlurHCS.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/ISVolumetricLightingBlurVCS.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/ISVolumetricLightingGenerateCS.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/ISVolumetricLightingRaymarchCS.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/ISWaterBlend.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/ISWaterDisplacement.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/ISWaterFlow.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/ISWorldMap.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/LICENSE` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/Lighting.hlsl` | Skyrim Shader Entry Points | YES | YES | YES | YES | YES | YES | YES | Partial |
| `distribution/Shaders/Menu/BackgroundBlurComposite.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Full source |
| `distribution/Shaders/Menu/BackgroundBlurHorizontal.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Full source |
| `distribution/Shaders/Menu/BackgroundBlurVertical.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Full source |
| `distribution/Shaders/Particle.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/Sky.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/Utility.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Sky Veil/Kernels/SkyVeil/SkyVeil.hlsli` | SkyVeil | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Strand Shading/Kernels/Hair/Hair.hlsli` | StrandShading | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Strand Shading/Kernels/Hair/TangentShift.dds` | StrandShading | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Terrain Detail/Kernels/TerrainDetail/TerrainDetail.hlsli` | TerrainDetail | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Terrain Occlusion/Kernels/TerrainOcclusion/ShadowUpdate.cs.hlsl` | TerrainOcclusion | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Terrain Occlusion/Kernels/TerrainOcclusion/TerrainOcclusion.hlsli` | TerrainOcclusion | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Terrain Seam/Kernels/TerrainSeam/DepthBlend.hlsl` | TerrainSeam | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Terrain Seam/Kernels/TerrainSeam/TerrainSeam.hlsli` | TerrainSeam | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Thin Surface/Kernels/ThinSurface/ThinSurface.hlsli` | ThinSurface | YES | YES | NO | YES | YES | YES | YES | Full source |
| `pipeline/Tissue Diffusion/Kernels/TissueDiffusion/Burley.hlsli` | TissueDiffusion | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Tissue Diffusion/Kernels/TissueDiffusion/DiffuseExtractionCS.hlsl` | TissueDiffusion | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Tissue Diffusion/Kernels/TissueDiffusion/SeparableSSS.hlsli` | TissueDiffusion | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Tissue Diffusion/Kernels/TissueDiffusion/SeparableSSSCS.hlsl` | TissueDiffusion | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Tissue Diffusion/Kernels/TissueDiffusion/SSSCommon.hlsli` | TissueDiffusion | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/DistantTree.hlsl` | Vegetation | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/RunGrass.hlsl` | Vegetation | YES | YES | YES | YES | YES | YES | YES | Full source |
| `pipeline/Volume Occlusion/Kernels/VolumeOcclusion/BlurShadowCS.hlsl` | VolumeOcclusion | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Volume Occlusion/Kernels/VolumeOcclusion/DownsampleShadowCS.hlsl` | VolumeOcclusion | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Volume Occlusion/Kernels/VolumeOcclusion/VolumeOcclusion.hlsli` | VolumeOcclusion | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Shaders/Water.hlsl` | Water | YES | YES | YES | YES | YES | YES | YES | Partial |
| `pipeline/Water Optics/Kernels/WaterOptics/watercaustics.dds` | WaterOptics | YES | YES | NO | YES | YES | YES | YES | Asset boundary |
| `pipeline/Water Optics/Kernels/WaterOptics/FoamStencil2K.png` | WaterOptics | YES | YES | YES | YES | YES | YES | YES | Asset boundary |
| `pipeline/Water Optics/Kernels/WaterOptics/WaterCaustics.hlsli` | WaterOptics | YES | YES | NO | YES | YES | YES | YES | Full source |
| `pipeline/Water Optics/Kernels/WaterOptics/WaterParallax.hlsli` | WaterOptics | YES | YES | NO | YES | YES | YES | YES | Full source |
| `pipeline/WindowLife/Kernels/WindowLife/CurtainAtlas_high-fidelity-2k.dds` | WindowLife | YES | YES | YES | YES | YES | YES | YES | Pending |
| `pipeline/WindowLife/Kernels/WindowLife/RoomAtlas_2k.dds` | WindowLife | YES | YES | YES | YES | YES | YES | YES | Pending |
| `pipeline/WindowLife/Kernels/WindowLife/CurtainAtlas.png` | WindowLife | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/WindowLife/Kernels/WindowLife/OccupantAtlas.png` | WindowLife | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/WindowLife/Kernels/WindowLife/RoomAtlas.png` | WindowLife | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/WindowLife/Kernels/WindowLife/WindowLife.hlsli` | WindowLife | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/World Probes/Kernels/WorldProbes/BC6HEncodeCS.hlsl` | WorldProbes | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/World Probes/Kernels/WorldProbes/defaultcubemap.dds` | WorldProbes | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/World Probes/Kernels/WorldProbes/InferCubemapCS.hlsl` | WorldProbes | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/World Probes/Kernels/WorldProbes/SpecularIrradianceCS.hlsl` | WorldProbes | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/World Probes/Kernels/WorldProbes/UpdateCubemapCS.hlsl` | WorldProbes | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/World Probes/Kernels/WorldProbes/WorldProbes.hlsli` | WorldProbes | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/DialogueFocus.h` | Camera Suite | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/ContactShadows/bend_sss_cpu.h` | Contact Shadows | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/ImageReconstruction/DX12SwapChain.cpp` | Image Reconstruction | YES | YES | YES | YES | YES | YES | YES | Partial |
| `engine/Modules/ImageReconstruction/DX12SwapChain.h` | Image Reconstruction | YES | YES | YES | YES | YES | YES | YES | Pending |
| `engine/Modules/ImageReconstruction/NeuralRendering.cpp` | Image Reconstruction | YES | YES | NO | YES | YES | YES | YES | Full source |
| `engine/Modules/ImageReconstruction/NeuralRendering.h` | Image Reconstruction | YES | YES | NO | YES | YES | YES | YES | Full source |
| `engine/Modules/ImageReconstruction/FidelityFX.cpp` | Image Reconstruction | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/ImageReconstruction/FidelityFX.h` | Image Reconstruction | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/ImageReconstruction/RCAS/RCAS.cpp` | Image Reconstruction | YES | YES | YES | YES | YES | YES | YES | Full source |
| `engine/Modules/ImageReconstruction/RCAS/RCAS.h` | Image Reconstruction | YES | YES | YES | YES | YES | YES | YES | Full source |
| `engine/Modules/ImageReconstruction/Streamline.cpp` | Image Reconstruction | YES | YES | YES | YES | YES | YES | YES | Partial |
| `engine/Modules/ImageReconstruction/Streamline.h` | Image Reconstruction | YES | YES | YES | YES | YES | YES | YES | Pending |
| `engine/Modules/AmbientProbe.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/AmbientProbe.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/Atmosphere.cpp` | Module Infrastructure | YES | YES | YES | YES | YES | YES | YES | Partial |
| `engine/Modules/Atmosphere.h` | Module Infrastructure | YES | YES | YES | YES | YES | YES | YES | Pending |
| `engine/Modules/CameraSuite.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/CameraSuite.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/ContactShadows.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/ContactShadows.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/DistanceBlend.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/DistanceBlend.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/FoliageDynamics.cpp` | Module Infrastructure | YES | YES | YES | YES | YES | YES | YES | Full source |
| `engine/Modules/FoliageDynamics.h` | Module Infrastructure | YES | YES | YES | YES | YES | YES | YES | Full source |
| `engine/Modules/GroundResponse.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Partial |
| `engine/Modules/GroundResponse.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/HorizonBlend.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/HorizonBlend.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/HybridGI.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Partial |
| `engine/Modules/HybridGI.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/ImageReconstruction.cpp` | Module Infrastructure | YES | YES | YES | YES | YES | YES | YES | Partial |
| `engine/Modules/ImageReconstruction.h` | Module Infrastructure | YES | YES | YES | YES | YES | YES | YES | Full source |
| `engine/Modules/InteriorDaylight.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/InteriorDaylight.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/LightVolumes.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/LightVolumes.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/LinearLightCore.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/LinearLightCore.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/MaterialLayers.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/MaterialLayers.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/NaturalLighting.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Full source |
| `engine/Modules/NaturalLighting.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Full source |
| `engine/Modules/OverlayFeature.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/PixelCapture.cpp` | Module Infrastructure | YES | YES | YES | YES | YES | YES | YES | Pending |
| `engine/Modules/PixelCapture.h` | Module Infrastructure | YES | YES | YES | YES | YES | YES | YES | Pending |
| `engine/Modules/PulseProfiler.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/PulseProfiler.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/RadiantGrid.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Partial |
| `engine/Modules/RadiantGrid.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/RainResponse.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/RainResponse.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/SkinOptics.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/SkinOptics.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/SkyBounce.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/SkyBounce.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/SkyContinuity.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/SkyContinuity.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/SkyVeil.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/SkyVeil.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/StrandShading.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/StrandShading.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/TerrainDetail.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/TerrainDetail.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/TerrainField.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/TerrainField.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/TerrainOcclusion.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/TerrainOcclusion.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/TerrainSeam.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/TerrainSeam.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/ThinSurface.cpp` | Module Infrastructure | YES | YES | YES | YES | YES | YES | YES | Full source |
| `engine/Modules/ThinSurface.h` | Module Infrastructure | YES | YES | YES | YES | YES | YES | YES | Full source |
| `engine/Modules/TissueDiffusion.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/TissueDiffusion.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/VolumeOcclusion.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/VolumeOcclusion.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/Waterbody.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/Waterbody.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/WaterOptics.cpp` | Module Infrastructure | YES | YES | YES | YES | YES | YES | YES | Full source |
| `engine/Modules/WaterOptics.h` | Module Infrastructure | YES | YES | YES | YES | YES | YES | YES | Full source |
| `engine/Modules/WindowLife.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/WindowLife.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/WorldProbes.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/WorldProbes.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/NaturalLighting/Common.h` | Natural Lighting | YES | YES | YES | YES | YES | YES | YES | Full source |
| `engine/Modules/PulseProfiler/ABTesting/ABTestAggregator.cpp` | Pulse Profiler | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/PulseProfiler/ABTesting/ABTestAggregator.h` | Pulse Profiler | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/PulseProfiler/ABTesting/ABTesting.cpp` | Pulse Profiler | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/PulseProfiler/ABTesting/ABTesting.h` | Pulse Profiler | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/Waterbody/Flowmap.cpp` | Waterbody | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/Waterbody/Flowmap.h` | Waterbody | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/Waterbody/WaterCache.cpp` | Waterbody | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Modules/Waterbody/WaterCache.h` | Waterbody | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/EngineFix.cpp` | Runtime Hooks | YES | YES | NO | YES | YES | YES | YES | Full source |
| `engine/EngineFix.h` | Runtime Hooks | YES | YES | NO | YES | YES | YES | YES | Full source |
| `engine/EngineFixes/EffectShaderNoDecalsFix.cpp` | Runtime Hooks | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/EngineFixes/EffectShaderNoDecalsFix.h` | Runtime Hooks | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/EngineFixes/ShadowmapCascadeCullingFix.cpp` | Runtime Hooks | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/EngineFixes/ShadowmapCascadeCullingFix.h` | Runtime Hooks | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/EngineFixes/ShadowmapCascadeRasterizerFix.cpp` | Runtime Hooks | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/EngineFixes/ShadowmapCascadeRasterizerFix.h` | Runtime Hooks | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Hooks.cpp` | Runtime Hooks | YES | YES | NO | YES | YES | YES | YES | Partial |
| `engine/Hooks.h` | Runtime Hooks | YES | YES | NO | YES | YES | YES | YES | Full source |
| `include/DynamicWetness_PublicAPI.h` | Public/Interop Headers | YES | YES | NO | YES | YES | YES | YES | Pending |
| `include/ENB/AntTweakBar.h` | Public/Interop Headers | YES | YES | NO | YES | YES | YES | YES | Pending |
| `include/ENB/ENBSeriesAPI.h` | Public/Interop Headers | YES | YES | NO | YES | YES | YES | YES | Pending |
| `include/ENB/ENBSeriesSDK.h` | Public/Interop Headers | YES | YES | NO | YES | YES | YES | YES | Pending |
| `include/FidelityFX/api/include/dx12/ffx_api_dx12.h` | Public/Interop Headers | YES | YES | NO | YES | YES | YES | YES | Pending |
| `include/FidelityFX/api/include/dx12/ffx_api_dx12.hpp` | Public/Interop Headers | YES | YES | NO | YES | YES | YES | YES | Pending |
| `include/FidelityFX/api/include/ffx_api.h` | Public/Interop Headers | YES | YES | NO | YES | YES | YES | YES | Pending |
| `include/FidelityFX/api/include/ffx_api.hpp` | Public/Interop Headers | YES | YES | NO | YES | YES | YES | YES | Pending |
| `include/FidelityFX/api/include/ffx_api_loader.h` | Public/Interop Headers | YES | YES | NO | YES | YES | YES | YES | Pending |
| `include/FidelityFX/api/include/ffx_api_types.h` | Public/Interop Headers | YES | YES | NO | YES | YES | YES | YES | Pending |
| `include/FidelityFX/api/include/ffx_api-helper.h` | Public/Interop Headers | YES | YES | NO | YES | YES | YES | YES | Pending |
| `include/FidelityFX/framegeneration/include/dx12/ffx_api_framegeneration_dx12.h` | Public/Interop Headers | YES | YES | NO | YES | YES | YES | YES | Pending |
| `include/FidelityFX/framegeneration/include/dx12/ffx_api_framegeneration_dx12.hpp` | Public/Interop Headers | YES | YES | NO | YES | YES | YES | YES | Pending |
| `include/FidelityFX/framegeneration/include/ffx_framegeneration.h` | Public/Interop Headers | YES | YES | NO | YES | YES | YES | YES | Pending |
| `include/FidelityFX/framegeneration/include/ffx_framegeneration.hpp` | Public/Interop Headers | YES | YES | NO | YES | YES | YES | YES | Pending |
| `include/FidelityFX/framegeneration/include/ffx_framegeneration_api_types.h` | Public/Interop Headers | YES | YES | NO | YES | YES | YES | YES | Pending |
| `include/FrameAnnotations.h` | Public/Interop Headers | YES | YES | NO | YES | YES | YES | YES | Pending |
| `include/PCH.h` | Public/Interop Headers | YES | YES | NO | YES | YES | YES | YES | Full source |
| `pipeline/Radiant Grid/Assets/LightProfiles/black.ini` | Radiant Grid | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Radiant Grid/Assets/LightProfiles/enblightglow.ini` | Radiant Grid | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Radiant Grid/Assets/LightProfiles/fxglowenb.ini` | Radiant Grid | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Radiant Grid/Assets/LightProfiles/glowsoft01_enbl.ini` | Radiant Grid | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Radiant Grid/Assets/LightProfiles/po3_fullalpha.ini` | Radiant Grid | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Terrain Field/Assets/Plugin/PIXL-TerrainField.esp` | Terrain Field | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/Blackreach.Terrain.HeightMap.-7.-8.7.12.-284.861.dds` | Terrain Occlusion | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/ccBGSSSE067DeadlandsWorld.Terrain.HeightMap.-6.-4.4.7.-140.353.dds` | Terrain Occlusion | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/ccKRTSSE001QNWorld.Terrain.HeightMap.-3.-4.2.2.-972.265.dds` | Terrain Occlusion | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/DeepwoodRedoubtWorld.Terrain.HeightMap.-39.15.-27.24.-618.472.dds` | Terrain Occlusion | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/DLC01FalmerValley.Terrain.HeightMap.-16.-13.10.13.-530.2212.dds` | Terrain Occlusion | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/DLC01SoulCairn.Terrain.HeightMap.-52.-43.30.44.-188.245.dds` | Terrain Occlusion | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/DLC1HunterHQWorld.Terrain.HeightMap.-31.-8.3.5.-662.308.dds` | Terrain Occlusion | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/DLC2ApocryphaWorld.Terrain.HeightMap.-6.-1.7.9.-250.-206.dds` | Terrain Occlusion | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/DLC2SolstheimWorld.Terrain.HeightMap.-64.-64.127.127.-1024.6342.dds` | Terrain Occlusion | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/FORMAT.md` | Terrain Occlusion | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/JaphetsFollyWorld.Terrain.HeightMap.-7.-6.6.6.-210.163.dds` | Terrain Occlusion | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/MarkarthWorld.Terrain.HeightMap.-48.-3.-31.5.-3375.2609.dds` | Terrain Occlusion | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/SkuldafnWorld.Terrain.HeightMap.40.-21.62.-1.-285.1876.dds` | Terrain Occlusion | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/Sovngarde.Terrain.HeightMap.-23.-32.34.40.-1331.4715.dds` | Terrain Occlusion | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/Tamriel.Terrain.HeightMap.-57.-43.61.50.-4629.4924.dds` | Terrain Occlusion | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Waterbody/Assets/Meshes/Water/OptimisedWaterMesh.nif` | Waterbody | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Waterbody/Assets/Meshes/Water/WaterMesh.nif` | Waterbody | YES | YES | NO | YES | YES | YES | YES | Pending |
| `pipeline/Waterbody/Assets/WorldWater/Tamriel_precache.wpc` | Waterbody | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Deferred.cpp` | Shader Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Deferred.h` | Shader Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/PipelineBuffer.cpp` | Shader Infrastructure | YES | YES | NO | YES | YES | YES | YES | Full source |
| `engine/PipelineBuffer.h` | Shader Infrastructure | YES | YES | NO | YES | YES | YES | YES | Full source |
| `engine/PipelineHealth.cpp` | Shader Infrastructure | YES | YES | YES | YES | YES | YES | YES | Full source |
| `engine/PipelineHealth.h` | Shader Infrastructure | YES | YES | NO | YES | YES | YES | YES | Full source |
| `engine/ShaderCache.cpp` | Shader Infrastructure | YES | YES | YES | YES | YES | YES | YES | Partial |
| `engine/ShaderCache.h` | Shader Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/ShaderFileWatcher.h` | Shader Infrastructure | YES | YES | YES | YES | YES | YES | YES | Full source |
| `engine/ShaderTools/BSShader.h` | Shader Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/ShaderTools/BSShaderHooks.cpp` | Shader Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/ShaderTools/BSShaderHooks.h` | Shader Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/ShaderTools/ShaderCompiler.cpp` | Shader Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/ShaderTools/ShaderCompiler.h` | Shader Infrastructure | YES | YES | NO | YES | YES | YES | YES | Pending |
| `extern/CommonLibSSE-NG@8f4205da56f01cbe98557422c008a5719c180fb9` | CommonLibSSE-NG | YES | YES | NO | YES | YES | YES | YES | Pending |
| `extern/FidelityFX-SDK@e65b2530631f2afb9a9ac753884926e49e20d608` | FidelityFX SDK | YES | YES | NO | YES | YES | YES | YES | Pending |
| `extern/Streamline-DX12@a9ed1f58436864891f68b0458300464dc53d9a69` | NVIDIA Streamline | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Menu.cpp` | UI | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Menu.h` | UI | YES | YES | YES | YES | YES | YES | YES | Pending |
| `engine/Menu/BackgroundBlur.cpp` | UI | YES | YES | YES | YES | YES | YES | YES | Full source |
| `engine/Menu/BackgroundBlur.h` | UI | YES | YES | NO | YES | YES | YES | YES | Full source |
| `engine/Menu/CursorLoader.cpp` | UI | YES | YES | NO | YES | YES | YES | YES | Full source |
| `engine/Menu/CursorLoader.h` | UI | YES | YES | NO | YES | YES | YES | YES | Full source |
| `engine/Menu/Fonts.cpp` | UI | YES | YES | YES | YES | YES | YES | YES | Full source |
| `engine/Menu/Fonts.h` | UI | YES | YES | NO | YES | YES | YES | YES | Full source |
| `engine/Menu/FontSelector.cpp` | UI | YES | YES | NO | YES | YES | YES | YES | Full source |
| `engine/Menu/FontSelector.h` | UI | YES | YES | NO | YES | YES | YES | YES | Full source |
| `engine/Menu/IconLoader.cpp` | UI | YES | YES | YES | YES | YES | YES | YES | Full source |
| `engine/Menu/IconLoader.h` | UI | YES | YES | NO | YES | YES | YES | YES | Full source |
| `engine/Menu/LaunchExperienceRenderer.cpp` | UI | YES | YES | YES | YES | YES | YES | YES | Full source |
| `engine/Menu/LaunchExperienceRenderer.h` | UI | YES | YES | YES | YES | YES | YES | YES | Full source |
| `engine/Menu/OverlayRenderer.cpp` | UI | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Menu/OverlayRenderer.h` | UI | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Menu/PIXLRendererPage.cpp` | UI | YES | YES | YES | YES | YES | YES | YES | Full source |
| `engine/Menu/PIXLRendererPage.h` | UI | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Menu/PIXLStyle.h` | UI | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Menu/PulsePanelRenderer.cpp` | UI | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Menu/PulsePanelRenderer.h` | UI | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Menu/RuntimeSettingsRenderer.cpp` | UI | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Menu/RuntimeSettingsRenderer.h` | UI | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Menu/ThemeManager.cpp` | UI | YES | YES | YES | YES | YES | YES | YES | Full source |
| `engine/Menu/ThemeManager.h` | UI | YES | YES | NO | YES | YES | YES | YES | Full source |
| `engine/Menu/TuningWorkspaceRenderer.cpp` | UI | YES | YES | YES | YES | YES | YES | YES | Pending |
| `engine/Menu/TuningWorkspaceRenderer.h` | UI | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Menu/WorkshopToolsRenderer.cpp` | UI | YES | YES | NO | YES | YES | YES | YES | Partial |
| `engine/Menu/WorkshopToolsRenderer.h` | UI | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/Jost/Jost-Light.ttf` | UI Assets | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/Jost/Jost-Regular.ttf` | UI Assets | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/Jost/Jost-SemiBold.ttf` | UI Assets | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/Jost/OFL.txt` | UI Assets | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/Sanguis/Sanguis OFL License.txt` | UI Assets | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/Sanguis/Sanguis.ttf` | UI Assets | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/Interface/PIXLRenderer/Visuals/Brand/PIXL-Mark.png` | UI Assets | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/SKSE/Plugins/PIXLRenderer/QualityPreviews/README.txt` | UI Assets | YES | YES | NO | YES | YES | YES | YES | Pending |
| `distribution/SKSE/Plugins/PIXLRenderer/QualityPreviews/Profile_LOW.png` | UI Assets | YES | YES | YES | YES | YES | YES | YES | Pending |
| `distribution/SKSE/Plugins/PIXLRenderer/QualityPreviews/Profile_MEDIUM.png` | UI Assets | YES | YES | YES | YES | YES | YES | YES | Pending |
| `distribution/SKSE/Plugins/PIXLRenderer/QualityPreviews/Profile_HIGH.png` | UI Assets | YES | YES | YES | YES | YES | YES | YES | Pending |
| `distribution/SKSE/Plugins/PIXLRenderer/QualityPreviews/Profile_ULTRA.png` | UI Assets | YES | YES | YES | YES | YES | YES | YES | Pending |
| `distribution/SKSE/Plugins/PIXLRenderer/QualityPreviews/Neural_ULTRA.png` | UI Assets | YES | YES | YES | YES | YES | YES | YES | Pending |
| `engine/I18n/I18n.cpp` | Localization | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/I18n/I18n.h` | Localization | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Renderer/QualityProfiles.cpp` | Quality Profiles | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/Renderer/QualityProfiles.h` | Quality Profiles | YES | YES | NO | YES | YES | YES | YES | Pending |
| `engine/SeasonIntegration.cpp` | Season integration; see runtime-foundations report | YES | YES | NO | YES | YES | YES | YES | Full source |
| `engine/SeasonIntegration.h` | Season integration; see runtime-foundations report | YES | YES | NO | YES | YES | YES | YES | Full source |
| `engine/Modules/ActorSurfaceEffects.cpp` | Actor environmental response | YES | NO | NO | NO | NO | NO | NO | Pending |
| `engine/Modules/ActorSurfaceEffects.h` | Actor environmental response | YES | NO | NO | NO | NO | NO | NO | Pending |
| `engine/Modules/HairReconstruction.cpp` | Retired runtime module; still compiled / ABI reservation | YES | NO | NO | NO | NO | NO | NO | Pending |
| `engine/Modules/HairReconstruction.h` | Retired runtime module; still compiled / ABI reservation | YES | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/SKSE/Plugins/PIXLRenderer/Presets/PIXL-Renderer-Medium.json` | Quality presets; stage usage to trace | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Shaders/Common/PIXLAdvancedSnowMaterial.hlsli` | Shared snow materials | YES | NO | NO | NO | NO | NO | NO | Pending |
| `pipeline/Actor Surface Effects/Kernels/ActorSurfaceEffects/ActorSurfaceEffects.hlsli` | Actor environmental response | YES | NO | NO | NO | NO | NO | NO | Pending |
| `pipeline/Actor Surface Effects/Kernels/ActorSurfaceEffects/CharacterRuntime.hlsli` | Actor environmental response | YES | NO | NO | NO | NO | NO | NO | Pending |
| `pipeline/Actor Surface Effects/Module.ini` | Actor environmental response | YES | NO | NO | NO | NO | NO | NO | Pending |
| `pipeline/Foliage Dynamics/Kernels/FoliageDynamics/WorldWind.hlsli` | Shared world wind | YES | YES | NO | YES | YES | YES | YES | Full source |
| `pipeline/Hair Reconstruction/Kernels/HairReconstruction/HairReconstruction.hlsli` | Retired guarded shader; excluded from player package | NO | NO | NO | NO | NO | NO | NO | Pending |
| `pipeline/Hair Reconstruction/Module.ini` | Retired descriptor; audit exclusion input | YES | NO | NO | NO | NO | NO | NO | Pending |
| `tools/GeneratePixlPresetExperiments.ps1` | Preset experiment helper; active use to trace | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `tools/install-worktree-alias.ps1` | Workspace helper; active use to trace | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `tools/new-worktree.ps1` | Workspace helper; active use to trace | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `tools/update-cmake-version.ps1` | Build metadata helper; active use to trace | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `tools/verify-shader-refactor.ps1` | Shader validation helper; active use to trace | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/CrimsonPro/CrimsonPro-Light.ttf` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/CrimsonPro/CrimsonPro-Regular.ttf` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/CrimsonPro/CrimsonPro-SemiBold.ttf` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/IBMPlexMono/IBMPlexMono-Light.ttf` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/IBMPlexMono/IBMPlexMono-Regular.ttf` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/IBMPlexMono/IBMPlexMono-SemiBold.ttf` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/IBMPlexMono/OFL.txt` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/IBMPlexSans/IBMPlexSans-Light.ttf` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/IBMPlexSans/IBMPlexSans-Regular.ttf` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/IBMPlexSans/IBMPlexSans-SemiBold.ttf` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/IBMPlexSans/IBMPlexSans_Condensed-Light.ttf` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/IBMPlexSans/IBMPlexSans_Condensed-Regular.ttf` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/IBMPlexSans/IBMPlexSans_Condensed-SemiBold.ttf` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/IBMPlexSans/OFL.txt` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/IBMPlexSerif/IBMPlexSerif-Light.ttf` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/IBMPlexSerif/IBMPlexSerif-Regular.ttf` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/IBMPlexSerif/IBMPlexSerif-SemiBold.ttf` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/IBMPlexSerif/OFL.txt` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/Inter/Inter_24pt-Light.ttf` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/Inter/Inter_24pt-Regular.ttf` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/Inter/Inter_24pt-SemiBold.ttf` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/Inter/OFL.txt` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/OpenDyslexic/OFL.txt` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/OpenDyslexic/OpenDyslexic3-Bold.ttf` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/OpenDyslexic/OpenDyslexic3-Regular.ttf` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/Roboto/OFL.txt` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/Roboto/Roboto-Bold.ttf` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/Roboto/Roboto-Regular.ttf` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/Roboto/Roboto-SemiBold.ttf` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/Roboto/Roboto-Thin.ttf` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/Roboto/Roboto_Condensed-Light.ttf` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/Roboto/Roboto_Condensed-Regular.ttf` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/Roboto/Roboto_Condensed-SemiBold.ttf` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/RobotoSlab/RobotoSlab-Light.ttf` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/RobotoSlab/RobotoSlab-Regular.ttf` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/RobotoSlab/RobotoSlab-SemiBold.ttf` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/Rubik/OFL.txt` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/Rubik/Rubik-Light.ttf` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/Rubik/Rubik-Regular.ttf` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/Rubik/Rubik-SemiBold.ttf` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/Sovngarde/Sovngarde OFL License.txt` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/Sovngarde/SovngardeBold.ttf` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Fonts/Sovngarde/SovngardeLight.ttf` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Visuals/Controls/Monochrome/clear-cache.png` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Visuals/Controls/Monochrome/load-settings.png` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Visuals/Controls/Monochrome/restore-settings.png` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Visuals/Controls/Monochrome/save-settings.png` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Visuals/Controls/clear-cache.png` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Visuals/Controls/load-settings.png` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Visuals/Controls/restore-settings.png` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Visuals/Controls/save-settings.png` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Visuals/Sections/characters.png` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Visuals/Sections/debug.png` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Visuals/Sections/display.png` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Visuals/Sections/grass.png` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Visuals/Sections/landscape.png` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Visuals/Sections/lighting.png` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Visuals/Sections/materials.png` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Visuals/Sections/post-processing.png` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Visuals/Sections/sky.png` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `distribution/Interface/PIXLRenderer/Visuals/Sections/water.png` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `pipeline/WindowLife/ASSET_SPEC.md` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `pipeline/WindowLife/Kernels/WindowLife/GlassGrime_1k.png` | Inventory gap reconciled; usage/classification pending | YES | NO | NO | NO | NO | NO | NO | Pending |
| `pipeline/WindowLife/ROOM_ATLAS_PROVENANCE.md` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `pipeline/WindowLife/WINDOW_LAYER_ATLAS_PROVENANCE.md` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `tools/verify-shader-refactor.sh` | Inventory gap reconciled; usage/classification pending | UNDETERMINED | NO | NO | NO | NO | NO | NO | Pending |
| `tools/TestPixlFoliageSettings.ps1` | Foliage validation (new September 7) | YES | YES | YES | YES | YES | YES | YES | Full source |
| `tools/TestPixlGrassShaders.ps1` | Foliage validation (new September 7) | YES | YES | YES | YES | YES | YES | YES | Full source |
| `tools/TestPixlThinSurfaceSettings.ps1` | Material validation (new September 7) | YES | YES | YES | YES | YES | YES | YES | Full source |
| `tools/TestPixlMaterialShaders.ps1` | Material validation (new September 7) | YES | YES | YES | YES | YES | YES | YES | Full source |
| `tools/TestPixlFonts.ps1` | Font/theme validation (new September 7) | YES | YES | YES | YES | YES | YES | YES | Full source |
| `tools/TestPixlDiagnosticSafety.ps1` | Diagnostic safety validation (new September 7) | YES | YES | YES | YES | YES | YES | YES | Full source |
| `tools/TestPixlIconLoader.ps1` | UI texture validation (new September 7) | YES | YES | YES | YES | YES | YES | YES | Full source |
| `tools/TestPixlLandscapeSettings.ps1` | Landscape settings validation (new September 7) | YES | YES | YES | YES | YES | YES | YES | Full source |
| `tools/TestPixlLandscapeShaders.ps1` | Landscape/water permutation validation (new September 7) | YES | YES | YES | YES | YES | YES | YES | Full source |
| `distribution/Shaders/Lighting.hlsl` | Lighting and terrain shader entry points | YES | YES | YES | YES | YES | YES | YES | Full source; independent Terrain Detail guard fixed |
| `engine/Modules/DistanceBlend.cpp` | Distance-based terrain blending settings/runtime | YES | YES | YES | YES | YES | YES | YES | Full source |
| `engine/Modules/DistanceBlend.h` | Distance blend ABI/settings | YES | YES | YES | YES | YES | YES | YES | Full source |
| `engine/Modules/TerrainDetail.cpp` | Terrain detail settings/runtime | YES | YES | YES | YES | YES | YES | YES | Full source |
| `engine/Modules/TerrainDetail.h` | Terrain detail ABI/settings | YES | YES | YES | YES | YES | YES | YES | Full source |
| `engine/Modules/HorizonBlend.cpp` | Horizon depth blending runtime | YES | YES | YES | YES | YES | YES | YES | Full source |
| `engine/Modules/HorizonBlend.h` | Horizon blend interface | YES | YES | YES | YES | YES | YES | YES | Full source |
| `pipeline/Terrain Detail/Module.ini` | Terrain detail module metadata | YES | YES | YES | YES | YES | YES | YES | Full source |
| `pipeline/Distance Blend/Module.ini` | Distance blend module metadata | YES | YES | YES | YES | YES | YES | YES | Full source |
| `pipeline/HorizonBlend/Module.ini` | Horizon blend module metadata | YES | YES | YES | YES | YES | YES | YES | Full source |
