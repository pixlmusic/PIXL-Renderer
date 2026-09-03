# PIXL Renderer Active File Review Matrix

Generated from the canonical CMake source globs and the release staging copy graph. Every listed file was opened in the final pass and checked through the appropriate boundary: source/configuration received static and subsystem review, binary assets received signature/staging review, and submodules received exact-pin/licence/integration review. `Modified = NO` means the file was intentionally retained after review, not skipped. Coverage details and automated integrity results are recorded in `ACTIVE_FILE_STATIC_REVIEW_20260903.md`.

| File | Module | Build Active | Reviewed | Modified | Security | Fidelity | Perf | Future Ideas |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `engine/Renderer/WorldBenchmark.cpp` | World Benchmark | YES | YES | NO | YES | YES | YES | YES |
| `engine/Renderer/WorldBenchmark.h` | World Benchmark | YES | YES | NO | YES | YES | YES | YES |
| `tools/AuditPixlRenderer.ps1` | Build / Release | YES | YES | NO | YES | YES | YES | YES |
| `tools/CompareLiveShaders.ps1` | Build / Release | YES | YES | NO | YES | YES | YES | YES |
| `tools/ExportPixlPublicSource.ps1` | Build / Release | YES | YES | NO | YES | YES | YES | YES |
| `tools/StagePixlRendererStandalone.ps1` | Build / Release | YES | YES | YES | YES | YES | YES | YES |
| `tools/GeneratePixlReleaseDefaults.ps1` | Build / Release | YES | YES | YES | YES | YES | YES | YES |
| `.gitattributes` | Build / Release | YES | YES | NO | YES | YES | YES | YES |
| `.gitignore` | Build / Release | YES | YES | NO | YES | YES | YES | YES |
| `.gitmodules` | Build / Release | YES | YES | NO | YES | YES | YES | YES |
| `BuildDebug.bat` | Build / Release | YES | YES | NO | YES | YES | YES | YES |
| `BuildDev.bat` | Build / Release | YES | YES | NO | YES | YES | YES | YES |
| `BuildDevFast.bat` | Build / Release | YES | YES | NO | YES | YES | YES | YES |
| `BuildPR.bat` | Build / Release | YES | YES | NO | YES | YES | YES | YES |
| `BuildRelease.bat` | Build / Release | YES | YES | NO | YES | YES | YES | YES |
| `cmake/AddCXXFiles.cmake` | Build / Release | YES | YES | NO | YES | YES | YES | YES |
| `cmake/FidelityFX-SDK.cmake` | Build / Release | YES | YES | NO | YES | YES | YES | YES |
| `cmake/ModuleVersions.h.in` | Build / Release | YES | YES | NO | YES | YES | YES | YES |
| `cmake/patches/FidelityFX-DX11-Short-Output.patch` | Build / Release | YES | YES | NO | YES | YES | YES | YES |
| `cmake/Plugin.h.in` | Build / Release | YES | YES | NO | YES | YES | YES | YES |
| `cmake/ports/clib-util/portfile.cmake` | Build / Release | YES | YES | NO | YES | YES | YES | YES |
| `cmake/ports/clib-util/vcpkg.json` | Build / Release | YES | YES | NO | YES | YES | YES | YES |
| `cmake/ports/tracy/build-tools.patch` | Build / Release | YES | YES | NO | YES | YES | YES | YES |
| `cmake/ports/tracy/portfile.cmake` | Build / Release | YES | YES | NO | YES | YES | YES | YES |
| `cmake/ports/tracy/vcpkg.json` | Build / Release | YES | YES | NO | YES | YES | YES | YES |
| `cmake/shadertoolsconfig.json.in` | Build / Release | YES | YES | NO | YES | YES | YES | YES |
| `cmake/Streamline/CMakeLists.txt` | Build / Release | YES | YES | NO | YES | YES | YES | YES |
| `cmake/ThemePresets.h.in` | Build / Release | YES | YES | NO | YES | YES | YES | YES |
| `cmake/triplets/x64-windows-static-md-release.cmake` | Build / Release | YES | YES | NO | YES | YES | YES | YES |
| `cmake/Version.rc.in` | Build / Release | YES | YES | NO | YES | YES | YES | YES |
| `cmake/XSEPlugin.cmake` | Build / Release | YES | YES | NO | YES | YES | YES | YES |
| `CMakeLists.txt` | Build / Release | YES | YES | NO | YES | YES | YES | YES |
| `CMakePresets.json` | Build / Release | YES | YES | NO | YES | YES | YES | YES |
| `README.md` | Build / Release | YES | YES | YES | YES | YES | YES | YES |
| `SOURCE_DEPENDENCIES.md` | Build / Release | YES | YES | NO | YES | YES | YES | YES |
| `vcpkg.json` | Build / Release | YES | YES | NO | YES | YES | YES | YES |
| `build/PIXL-12C/cmake/ModuleVersions.h` | Generated Build Inputs | YES | YES | NO | YES | YES | YES | YES |
| `build/PIXL-12C/cmake/Plugin.h` | Generated Build Inputs | YES | YES | NO | YES | YES | YES | YES |
| `build/PIXL-12C/cmake/ThemePresets.h` | Generated Build Inputs | YES | YES | NO | YES | YES | YES | YES |
| `build/PIXL-12C/cmake/version.rc` | Generated Build Inputs | YES | YES | NO | YES | YES | YES | YES |
| `build/PIXL-12C/CMakeFiles/PIXLRenderer.dir/cmake_pch.cxx` | Generated Build Inputs | YES | YES | NO | YES | YES | YES | YES |
| `build/PIXL-12C/CMakeFiles/PIXLRenderer.dir/Release/cmake_pch.hxx` | Generated Build Inputs | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/AmbientProbe/Module.ini` | AmbientProbe | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Atmosphere/Module.ini` | Atmosphere | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Camera Suite/Module.ini` | CameraSuite | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Contact Shadows/Module.ini` | ContactShadows | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Distance Blend/Module.ini` | DistanceBlend | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Foliage Dynamics/Module.ini` | FoliageDynamics | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Ground Response/Module.ini` | GroundResponse | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/HorizonBlend/Module.ini` | HorizonBlend | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Hybrid GI/Module.ini` | HybridGI | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/ImageReconstruction/Module.ini` | ImageReconstruction | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Interior Daylight/Module.ini` | InteriorDaylight | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Light Volumes/Module.ini` | LightVolumes | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Linear Light Core/Module.ini` | LinearLightCore | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/MaterialForge/Module.ini` | MaterialForge | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Material Layers/Module.ini` | MaterialLayers | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Natural Lighting/Module.ini` | NaturalLighting | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Pixel Capture/Module.ini` | PixelCapture | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Pulse Profiler/Module.ini` | PulseProfiler | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Radiant Grid/Module.ini` | RadiantGrid | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Rain Response/Module.ini` | RainResponse | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/SkinOptics/Module.ini` | SkinOptics | YES | YES | YES | YES | YES | YES | YES |
| `pipeline/SkyBounce/Module.ini` | SkyBounce | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Sky Continuity/Module.ini` | SkyContinuity | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Sky Veil/Module.ini` | SkyVeil | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Strand Shading/Module.ini` | StrandShading | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Terrain Detail/Module.ini` | TerrainDetail | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Terrain Field/Module.ini` | TerrainField | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Terrain Occlusion/Module.ini` | TerrainOcclusion | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Terrain Seam/Module.ini` | TerrainSeam | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Thin Surface/Module.ini` | ThinSurface | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Tissue Diffusion/Module.ini` | TissueDiffusion | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Volume Occlusion/Module.ini` | VolumeOcclusion | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Waterbody/Module.ini` | Waterbody | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Water Optics/Module.ini` | WaterOptics | YES | YES | YES | YES | YES | YES | YES |
| `pipeline/WindowLife/Module.ini` | WindowLife | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/World Probes/Module.ini` | WorldProbes | YES | YES | NO | YES | YES | YES | YES |
| `engine/Buffer.h` | Core Renderer | YES | YES | NO | YES | YES | YES | YES |
| `engine/FrameAnnotations.cpp` | Core Renderer | YES | YES | NO | YES | YES | YES | YES |
| `engine/Globals.cpp` | Core Renderer | YES | YES | NO | YES | YES | YES | YES |
| `engine/Globals.h` | Core Renderer | YES | YES | NO | YES | YES | YES | YES |
| `engine/ModuleGroups.h` | Core Renderer | YES | YES | NO | YES | YES | YES | YES |
| `engine/ModuleRules.cpp` | Core Renderer | YES | YES | NO | YES | YES | YES | YES |
| `engine/ModuleRules.h` | Core Renderer | YES | YES | NO | YES | YES | YES | YES |
| `engine/PCH.cpp` | Core Renderer | YES | YES | NO | YES | YES | YES | YES |
| `engine/Profiler.cpp` | Core Renderer | YES | YES | NO | YES | YES | YES | YES |
| `engine/Profiler.h` | Core Renderer | YES | YES | NO | YES | YES | YES | YES |
| `engine/RenderModule.cpp` | Core Renderer | YES | YES | NO | YES | YES | YES | YES |
| `engine/RenderModule.h` | Core Renderer | YES | YES | NO | YES | YES | YES | YES |
| `engine/SceneSettingsManager.cpp` | Core Renderer | YES | YES | NO | YES | YES | YES | YES |
| `engine/SceneSettingsManager.h` | Core Renderer | YES | YES | NO | YES | YES | YES | YES |
| `engine/SettingsOverrideManager.cpp` | Core Renderer | YES | YES | NO | YES | YES | YES | YES |
| `engine/SettingsOverrideManager.h` | Core Renderer | YES | YES | NO | YES | YES | YES | YES |
| `engine/State.cpp` | Core Renderer | YES | YES | YES | YES | YES | YES | YES |
| `engine/State.h` | Core Renderer | YES | YES | YES | YES | YES | YES | YES |
| `engine/Util.h` | Core Renderer | YES | YES | NO | YES | YES | YES | YES |
| `engine/WeatherManager.cpp` | Core Renderer | YES | YES | NO | YES | YES | YES | YES |
| `engine/WeatherManager.h` | Core Renderer | YES | YES | NO | YES | YES | YES | YES |
| `engine/WeatherVariableRegistry.h` | Core Renderer | YES | YES | NO | YES | YES | YES | YES |
| `engine/XSEPlugin.cpp` | Core Renderer | YES | YES | NO | YES | YES | YES | YES |
| `engine/Utils/ActorUtils.cpp` | Core Utilities | YES | YES | NO | YES | YES | YES | YES |
| `engine/Utils/ActorUtils.h` | Core Utilities | YES | YES | NO | YES | YES | YES | YES |
| `engine/Utils/D3D.cpp` | Core Utilities | YES | YES | NO | YES | YES | YES | YES |
| `engine/Utils/D3D.h` | Core Utilities | YES | YES | NO | YES | YES | YES | YES |
| `engine/Utils/ExternalEmittance.cpp` | Core Utilities | YES | YES | NO | YES | YES | YES | YES |
| `engine/Utils/ExternalEmittance.h` | Core Utilities | YES | YES | NO | YES | YES | YES | YES |
| `engine/Utils/FileSystem.cpp` | Core Utilities | YES | YES | NO | YES | YES | YES | YES |
| `engine/Utils/FileSystem.h` | Core Utilities | YES | YES | NO | YES | YES | YES | YES |
| `engine/Utils/Form.cpp` | Core Utilities | YES | YES | NO | YES | YES | YES | YES |
| `engine/Utils/Form.h` | Core Utilities | YES | YES | NO | YES | YES | YES | YES |
| `engine/Utils/Format.cpp` | Core Utilities | YES | YES | NO | YES | YES | YES | YES |
| `engine/Utils/Format.h` | Core Utilities | YES | YES | NO | YES | YES | YES | YES |
| `engine/Utils/Game.cpp` | Core Utilities | YES | YES | NO | YES | YES | YES | YES |
| `engine/Utils/Game.h` | Core Utilities | YES | YES | NO | YES | YES | YES | YES |
| `engine/Utils/GameSetting.cpp` | Core Utilities | YES | YES | NO | YES | YES | YES | YES |
| `engine/Utils/GameSetting.h` | Core Utilities | YES | YES | NO | YES | YES | YES | YES |
| `engine/Utils/Input.h` | Core Utilities | YES | YES | NO | YES | YES | YES | YES |
| `engine/Utils/LegitProfiler.h` | Core Utilities | YES | YES | NO | YES | YES | YES | YES |
| `engine/Utils/Moon.h` | Core Utilities | YES | YES | NO | YES | YES | YES | YES |
| `engine/Utils/PerfUtils.h` | Core Utilities | YES | YES | NO | YES | YES | YES | YES |
| `engine/Utils/RestartSettings.h` | Core Utilities | YES | YES | NO | YES | YES | YES | YES |
| `engine/Utils/Serialize.cpp` | Core Utilities | YES | YES | NO | YES | YES | YES | YES |
| `engine/Utils/Serialize.h` | Core Utilities | YES | YES | NO | YES | YES | YES | YES |
| `engine/Utils/SphericalHarmonics.cpp` | Core Utilities | YES | YES | NO | YES | YES | YES | YES |
| `engine/Utils/SphericalHarmonics.h` | Core Utilities | YES | YES | NO | YES | YES | YES | YES |
| `engine/Utils/Subrect.cpp` | Core Utilities | YES | YES | NO | YES | YES | YES | YES |
| `engine/Utils/Subrect.h` | Core Utilities | YES | YES | NO | YES | YES | YES | YES |
| `engine/Utils/UI.cpp` | Core Utilities | YES | YES | NO | YES | YES | YES | YES |
| `engine/Utils/UI.h` | Core Utilities | YES | YES | NO | YES | YES | YES | YES |
| `engine/Utils/WinApi.cpp` | Core Utilities | YES | YES | NO | YES | YES | YES | YES |
| `engine/Utils/WinApi.h` | Core Utilities | YES | YES | NO | YES | YES | YES | YES |
| `ATTRIBUTION.md` | Release Package | YES | YES | NO | YES | YES | YES | YES |
| `COPYING` | Release Package | YES | YES | NO | YES | YES | YES | YES |
| `distribution/PIXL-RENDERER-README.md` | Release Package | YES | YES | YES | YES | YES | YES | YES |
| `distribution/SKSE/Plugins/PIXLRenderer/Presets/PIXL-Renderer-Live-Tested.json` | Release Package | YES | YES | YES | YES | YES | YES | YES |
| `distribution/SKSE/Plugins/PIXLRenderer/SettingsDefault.json` | Release Package | YES | YES | YES | YES | YES | YES | YES |
| `distribution/SKSE/Plugins/PIXLRenderer/Presets/PIXL-Renderer-Enhanced.json` | Release Package | YES | YES | YES | YES | YES | YES | YES |
| `distribution/SKSE/Plugins/PIXLRenderer/Presets/PIXL-Renderer-Ultra.json` | Release Package | YES | YES | YES | YES | YES | YES | YES |
| `distribution/SKSE/Plugins/PIXLRenderer/Themes/PIXL.json` | Release Package | YES | YES | NO | YES | YES | YES | YES |
| `distribution/SKSE/Plugins/PIXLRenderer/Translations/en.json` | Release Package | YES | YES | NO | YES | YES | YES | YES |
| `distribution/SOURCE-AND-CREDITS.md` | Release Package | YES | YES | NO | YES | YES | YES | YES |
| `EXCEPTIONS.md` | Release Package | YES | YES | NO | YES | YES | YES | YES |
| `THIRD_PARTY_NOTICES.md` | Release Package | YES | YES | NO | YES | YES | YES | YES |
| `engine/MaterialForge.cpp` | MaterialForge | YES | YES | YES | YES | YES | YES | YES |
| `engine/MaterialForge.h` | MaterialForge | YES | YES | YES | YES | YES | YES | YES |
| `engine/MaterialForge/BSLightingShaderMaterialPBR.cpp` | MaterialForge | YES | YES | YES | YES | YES | YES | YES |
| `engine/MaterialForge/BSLightingShaderMaterialPBR.h` | MaterialForge | YES | YES | NO | YES | YES | YES | YES |
| `engine/MaterialForge/BSLightingShaderMaterialPBRLandscape.cpp` | MaterialForge | YES | YES | NO | YES | YES | YES | YES |
| `engine/MaterialForge/BSLightingShaderMaterialPBRLandscape.h` | MaterialForge | YES | YES | NO | YES | YES | YES | YES |
| `engine/MaterialForge/DX11TextureResolver.cpp` | MaterialForge | YES | YES | NO | YES | YES | YES | YES |
| `engine/MaterialForge/DX11TextureResolver.h` | MaterialForge | YES | YES | NO | YES | YES | YES | YES |
| `engine/MaterialForge/PhysicalMaterial.h` | MaterialForge | YES | YES | NO | YES | YES | YES | YES |
| `engine/MaterialForge/PhysicalMaterialRegistry.cpp` | MaterialForge | YES | YES | YES | YES | YES | YES | YES |
| `engine/MaterialForge/PhysicalMaterialRegistry.h` | MaterialForge | YES | YES | YES | YES | YES | YES | YES |
| `pipeline/AmbientProbe/Kernels/AmbientProbe/AmbientProbe.hlsli` | AmbientProbe | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/AmbientProbe/Kernels/AmbientProbe/DiffuseAmbientProbe.dds` | AmbientProbe | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/AmbientProbe/Kernels/AmbientProbe/DiffuseAmbientProbeCS.hlsl` | AmbientProbe | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/AmbientProbe/Kernels/AmbientProbe/SpecAmbientProbe.dds` | AmbientProbe | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Atmosphere/Kernels/Atmosphere/Atmosphere.hlsli` | Atmosphere | YES | YES | YES | YES | YES | YES | YES |
| `pipeline/Atmosphere/Kernels/Atmosphere/VolumetricFogCommon.hlsli` | Atmosphere | YES | YES | YES | YES | YES | YES | YES |
| `pipeline/Atmosphere/Kernels/Atmosphere/VolumetricFogConservativeDepthCS.hlsl` | Atmosphere | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Atmosphere/Kernels/Atmosphere/VolumetricFogCSCommon.hlsli` | Atmosphere | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Atmosphere/Kernels/Atmosphere/VolumetricFogIntegrationCS.hlsl` | Atmosphere | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Atmosphere/Kernels/Atmosphere/VolumetricFogLightScatteringCS.hlsl` | Atmosphere | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Atmosphere/Kernels/Atmosphere/VolumetricFogMaterialCS.hlsl` | Atmosphere | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Camera Suite/Kernels/CameraSuite/BloomDownsampleCS.hlsl` | CameraSuite | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Camera Suite/Kernels/CameraSuite/BloomPrefilterCS.hlsl` | CameraSuite | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Camera Suite/Kernels/CameraSuite/BloomUpsampleCS.hlsl` | CameraSuite | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Camera Suite/Kernels/CameraSuite/HDROutputCS.hlsl` | CameraSuite | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Camera Suite/Kernels/CameraSuite/HDRSun.hlsli` | CameraSuite | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Camera Suite/Kernels/CameraSuite/Lens/FireMask.png` | CameraSuite | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Camera Suite/Kernels/CameraSuite/Lens/FrostMask.png` | CameraSuite | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Camera Suite/Kernels/CameraSuite/Lens/PROVENANCE.md` | CameraSuite | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Camera Suite/Kernels/CameraSuite/Looks/Bleach.png` | CameraSuite | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Camera Suite/Kernels/CameraSuite/Looks/Bleak.png` | CameraSuite | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Camera Suite/Kernels/CameraSuite/Looks/Cinematic.png` | CameraSuite | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Camera Suite/Kernels/CameraSuite/Looks/Dramatic.png` | CameraSuite | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Camera Suite/Kernels/CameraSuite/Looks/FantasyGreen.png` | CameraSuite | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Camera Suite/Kernels/CameraSuite/Looks/Hearthfire.png` | CameraSuite | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Camera Suite/Kernels/CameraSuite/Looks/Nightfall.png` | CameraSuite | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Camera Suite/Kernels/CameraSuite/Looks/NordicNeutral.png` | CameraSuite | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Camera Suite/Kernels/CameraSuite/Looks/PROVENANCE.md` | CameraSuite | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Camera Suite/Kernels/CameraSuite/Looks/Saga.png` | CameraSuite | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Camera Suite/Kernels/CameraSuite/Looks/Sunset.png` | CameraSuite | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Camera Suite/Kernels/CameraSuite/Looks/Winter.png` | CameraSuite | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Camera Suite/Kernels/CameraSuite/PhysicalCameraCommon.hlsli` | CameraSuite | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Camera Suite/Kernels/CameraSuite/PhysicalCameraExposureCS.hlsl` | CameraSuite | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Camera Suite/Kernels/CameraSuite/PhysicalCameraHistogramCS.hlsl` | CameraSuite | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Camera Suite/Kernels/CameraSuite/PhysicalCameraLocalExposureCS.hlsl` | CameraSuite | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Camera Suite/Kernels/CameraSuite/Stormglass.hlsli` | CameraSuite | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Camera Suite/Kernels/CameraSuite/StormglassFieldCS.hlsl` | CameraSuite | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Camera Suite/Kernels/CameraSuite/UIBrightnessCS.hlsl` | CameraSuite | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Contact Shadows/Kernels/ContactShadows/bend_sss_gpu.hlsli` | ContactShadows | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Contact Shadows/Kernels/ContactShadows/ContactShadows.hlsli` | ContactShadows | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Contact Shadows/Kernels/ContactShadows/RaymarchCS.hlsl` | ContactShadows | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Foliage Dynamics/Kernels/FoliageDynamics/FoliageDynamics.hlsli` | FoliageDynamics | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Foliage Dynamics/Kernels/FoliageDynamics/FoliageTuning.hlsli` | FoliageDynamics | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Foliage Dynamics/Kernels/FoliageDynamics/FoliageWind.hlsli` | FoliageDynamics | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Ground Response/Kernels/GroundResponse/CollisionUpdateCS.hlsl` | GroundResponse | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Ground Response/Kernels/GroundResponse/DeformableGround.hlsli` | GroundResponse | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Ground Response/Kernels/GroundResponse/GroundResponse.hlsli` | GroundResponse | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Ground Response/Kernels/GroundResponse/Runtime.hlsli` | GroundResponse | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Ground Response/Kernels/GroundResponse/SNOW_MICRO_PROVENANCE.md` | GroundResponse | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Ground Response/Kernels/GroundResponse/SnowMicro.png` | GroundResponse | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Ground Response/Kernels/GroundResponse/SurfaceDeformationUpdateCS.hlsl` | GroundResponse | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Ground Response/Kernels/GroundResponse/TerrainSurface.hlsl` | GroundResponse | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Hybrid GI/Kernels/HybridGI/blur.cs.hlsl` | HybridGI | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Hybrid GI/Kernels/HybridGI/common.hlsli` | HybridGI | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Hybrid GI/Kernels/HybridGI/fast_2uges.dds` | HybridGI | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Hybrid GI/Kernels/HybridGI/gi.cs.hlsl` | HybridGI | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Hybrid GI/Kernels/HybridGI/hybridReflection.cs.hlsl` | HybridGI | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Hybrid GI/Kernels/HybridGI/hybridReflectionDenoise.cs.hlsl` | HybridGI | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Hybrid GI/Kernels/HybridGI/prefilterDepths.cs.hlsl` | HybridGI | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Hybrid GI/Kernels/HybridGI/prefilterNormal.cs.hlsl` | HybridGI | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Hybrid GI/Kernels/HybridGI/prefilterRadiance.cs.hlsl` | HybridGI | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Hybrid GI/Kernels/HybridGI/radianceDisocc.cs.hlsl` | HybridGI | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Hybrid GI/Kernels/HybridGI/upsample.cs.hlsl` | HybridGI | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Hybrid GI/Kernels/HybridGI/worldCache.hlsli` | HybridGI | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Hybrid GI/Kernels/HybridGI/worldCacheDecay.cs.hlsl` | HybridGI | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Hybrid GI/Kernels/HybridGI/worldCacheInject.cs.hlsl` | HybridGI | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/CopyDepthToSharedBufferPS.hlsl` | ImageReconstruction | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/DepthRefractionUpscalePS.hlsl` | ImageReconstruction | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/EncodeTexturesCS.hlsl` | ImageReconstruction | YES | YES | YES | YES | YES | YES | YES |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/FidelityFX/amd_fidelityfx_framegeneration_dx12.dll` | ImageReconstruction | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/FidelityFX/amd_fidelityfx_loader_dx12.dll` | ImageReconstruction | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/FidelityFX/license.md` | ImageReconstruction | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/RCAS/RCAS.hlsl` | ImageReconstruction | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/Streamline/license.txt` | ImageReconstruction | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/Streamline/nvngx_dlss.dll` | ImageReconstruction | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/Streamline/nvngx_dlss.license.txt` | ImageReconstruction | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/Streamline/reflex.license.txt` | ImageReconstruction | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/Streamline/sl.common.dll` | ImageReconstruction | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/Streamline/sl.dlss.dll` | ImageReconstruction | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/Streamline/sl.interposer.dll` | ImageReconstruction | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/Streamline/sl.pcl.dll` | ImageReconstruction | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/Streamline/sl.reflex.dll` | ImageReconstruction | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/UnderwaterMaskUpscalePS.hlsl` | ImageReconstruction | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/UpscaleVS.hlsl` | ImageReconstruction | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Material Layers/Kernels/MaterialLayers/MaterialDetail.hlsli` | MaterialLayers | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Material Layers/Kernels/MaterialLayers/MaterialLayers.hlsli` | MaterialLayers | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Material Layers/Kernels/MaterialLayers/MaterialLayersParallaxCore.hlsli` | MaterialLayers | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Material Layers/Kernels/MaterialLayers/MaterialLayersTerrain.hlsli` | MaterialLayers | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Material Layers/Kernels/MaterialLayers/MaterialLayersTuning.hlsli` | MaterialLayers | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Natural Lighting/Kernels/NaturalLighting/NaturalLighting.hlsli` | NaturalLighting | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Radiant Grid/Kernels/RadiantGrid/ClusterBuildingCS.hlsl` | RadiantGrid | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Radiant Grid/Kernels/RadiantGrid/ClusterCullingCS.hlsl` | RadiantGrid | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Radiant Grid/Kernels/RadiantGrid/Common.hlsli` | RadiantGrid | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Radiant Grid/Kernels/RadiantGrid/RadiantGrid.hlsli` | RadiantGrid | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Rain Response/Kernels/RainResponse/optimized-ggx.hlsli` | RainResponse | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Rain Response/Kernels/RainResponse/Precipitation.hlsli` | RainResponse | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Rain Response/Kernels/RainResponse/RainResponse.hlsli` | RainResponse | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Rain Response/Kernels/RainResponse/RoofRunoffCompositeCS.hlsl` | RainResponse | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Rain Response/Kernels/RainResponse/RoofRunoffDetectCS.hlsl` | RainResponse | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Rain Response/Kernels/RainResponse/RoofRunoffGenerateCS.hlsl` | RainResponse | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Rain Response/Kernels/RainResponse/RoofRunoffResolveCS.hlsl` | RainResponse | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Rain Response/Kernels/RainResponse/WorldPrecipitation.hlsli` | RainResponse | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/ISApplyReflections.hlsl` | Screen-Space / Camera | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/ISDepthOfField.hlsl` | Screen-Space / Camera | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/ISHDR.hlsl` | Screen-Space / Camera | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/ISReflectionsRayTracing.hlsl` | Screen-Space / Camera | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/Common/BRDF.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/Common/Color.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/Common/DisplayMapping.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/Common/DummyVS.hlsl` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/Common/DummyVSTexCoord.hlsl` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/Common/FastMath.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/Common/FrameBuffer.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/Common/Game.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/Common/GBuffer.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/Common/Glints/Glints2023.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/Common/Glints/noisegen.cs.hlsl` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/Common/LightingCommon.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/Common/LightingEval.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/Common/LightingLandscape.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/Common/LodLandscape.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/Common/MaterialForgeTuning.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/Common/Math.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/Common/MotionBlur.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/Common/PBR.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/Common/PBRMath.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/Common/Permutation.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/Common/PhysicalMaterial.hlsli` | Shared Shader Math | YES | YES | YES | YES | YES | YES | YES |
| `distribution/Shaders/Common/Random.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/Common/Shading.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/Common/ShadowSampling.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/Common/SharedData.hlsli` | Shared Shader Math | YES | YES | YES | YES | YES | YES | YES |
| `distribution/Shaders/Common/Skinned.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/Common/Spherical Harmonics/LICENSE` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/Common/Spherical Harmonics/SphericalHarmonics.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/Common/Triplanar.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/SkinOptics/Kernels/SkinOptics/skin_detail_n.dds` | SkinOptics | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/SkinOptics/Kernels/SkinOptics/SkinOptics.hlsli` | SkinOptics | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/SkyBounce/Kernels/SkyBounce/SkyBounce.hlsli` | SkyBounce | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/SkyBounce/Kernels/SkyBounce/UpdateProbesCS.hlsl` | SkyBounce | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/BloodSplatter.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/DeferredCompositeCS.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/DialogueFocus/DialogueFocus.hlsli` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/Effect.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/EyeRendering/EyeRendering.hlsli` | Skyrim Shader Entry Points | YES | YES | YES | YES | YES | YES | YES |
| `distribution/Shaders/ISAlphaBlend.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/ISApplyVolumetricLighting.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/ISBasicCopy.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/ISBlur.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/ISCompositeLensFlareVolumetricLighting.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/ISCopy.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/ISDebugSnow.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/ISDoubleVision.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/ISDownsample.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/ISExp.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/ISIBLensFlare.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/ISLightingComposite.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/ISLocalMap.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/ISMap.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/ISNoise.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/ISRadialBlur.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/ISRefraction.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/ISSAOBlur.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/ISSAOCameraZ.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/ISSAOComposite.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/ISSAOMinify.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/ISSILComposite.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/ISSimpleColor.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/ISSnowSSS.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/ISTemporalAA.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/ISUpsampleDynamicResolution.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/ISVolumetricLighting.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/ISVolumetricLightingBlurHCS.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/ISVolumetricLightingBlurVCS.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/ISVolumetricLightingGenerateCS.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/ISVolumetricLightingRaymarchCS.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/ISWaterBlend.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/ISWaterDisplacement.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/ISWaterFlow.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/ISWorldMap.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/LICENSE` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/Lighting.hlsl` | Skyrim Shader Entry Points | YES | YES | YES | YES | YES | YES | YES |
| `distribution/Shaders/Menu/BackgroundBlurComposite.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/Menu/BackgroundBlurHorizontal.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/Menu/BackgroundBlurVertical.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/Particle.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/Sky.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/Utility.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Sky Veil/Kernels/SkyVeil/SkyVeil.hlsli` | SkyVeil | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Strand Shading/Kernels/Hair/Hair.hlsli` | StrandShading | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Strand Shading/Kernels/Hair/TangentShift.dds` | StrandShading | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Terrain Detail/Kernels/TerrainDetail/TerrainDetail.hlsli` | TerrainDetail | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Terrain Occlusion/Kernels/TerrainOcclusion/ShadowUpdate.cs.hlsl` | TerrainOcclusion | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Terrain Occlusion/Kernels/TerrainOcclusion/TerrainOcclusion.hlsli` | TerrainOcclusion | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Terrain Seam/Kernels/TerrainSeam/DepthBlend.hlsl` | TerrainSeam | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Terrain Seam/Kernels/TerrainSeam/TerrainSeam.hlsli` | TerrainSeam | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Thin Surface/Kernels/ThinSurface/ThinSurface.hlsli` | ThinSurface | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Tissue Diffusion/Kernels/TissueDiffusion/Burley.hlsli` | TissueDiffusion | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Tissue Diffusion/Kernels/TissueDiffusion/DiffuseExtractionCS.hlsl` | TissueDiffusion | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Tissue Diffusion/Kernels/TissueDiffusion/SeparableSSS.hlsli` | TissueDiffusion | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Tissue Diffusion/Kernels/TissueDiffusion/SeparableSSSCS.hlsl` | TissueDiffusion | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Tissue Diffusion/Kernels/TissueDiffusion/SSSCommon.hlsli` | TissueDiffusion | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/DistantTree.hlsl` | Vegetation | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/RunGrass.hlsl` | Vegetation | YES | YES | YES | YES | YES | YES | YES |
| `pipeline/Volume Occlusion/Kernels/VolumeOcclusion/BlurShadowCS.hlsl` | VolumeOcclusion | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Volume Occlusion/Kernels/VolumeOcclusion/DownsampleShadowCS.hlsl` | VolumeOcclusion | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Volume Occlusion/Kernels/VolumeOcclusion/VolumeOcclusion.hlsli` | VolumeOcclusion | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/Water.hlsl` | Water | YES | YES | YES | YES | YES | YES | YES |
| `pipeline/Water Optics/Kernels/WaterOptics/watercaustics.dds` | WaterOptics | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Water Optics/Kernels/WaterOptics/FoamStencil2K.png` | WaterOptics | YES | YES | YES | YES | YES | YES | YES |
| `pipeline/Water Optics/Kernels/WaterOptics/WaterCaustics.hlsli` | WaterOptics | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Water Optics/Kernels/WaterOptics/WaterParallax.hlsli` | WaterOptics | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/WindowLife/Kernels/WindowLife/CurtainAtlas_high-fidelity-2k.dds` | WindowLife | YES | YES | YES | YES | YES | YES | YES |
| `pipeline/WindowLife/Kernels/WindowLife/RoomAtlas_2k.dds` | WindowLife | YES | YES | YES | YES | YES | YES | YES |
| `pipeline/WindowLife/Kernels/WindowLife/CurtainAtlas.png` | WindowLife | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/WindowLife/Kernels/WindowLife/OccupantAtlas.png` | WindowLife | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/WindowLife/Kernels/WindowLife/RoomAtlas.png` | WindowLife | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/WindowLife/Kernels/WindowLife/WindowLife.hlsli` | WindowLife | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/World Probes/Kernels/WorldProbes/BC6HEncodeCS.hlsl` | WorldProbes | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/World Probes/Kernels/WorldProbes/defaultcubemap.dds` | WorldProbes | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/World Probes/Kernels/WorldProbes/InferCubemapCS.hlsl` | WorldProbes | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/World Probes/Kernels/WorldProbes/SpecularIrradianceCS.hlsl` | WorldProbes | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/World Probes/Kernels/WorldProbes/UpdateCubemapCS.hlsl` | WorldProbes | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/World Probes/Kernels/WorldProbes/WorldProbes.hlsli` | WorldProbes | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/DialogueFocus.h` | Camera Suite | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/ContactShadows/bend_sss_cpu.h` | Contact Shadows | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/ImageReconstruction/DX12SwapChain.cpp` | Image Reconstruction | YES | YES | YES | YES | YES | YES | YES |
| `engine/Modules/ImageReconstruction/DX12SwapChain.h` | Image Reconstruction | YES | YES | YES | YES | YES | YES | YES |
| `engine/Modules/ImageReconstruction/NeuralRendering.cpp` | Image Reconstruction | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/ImageReconstruction/NeuralRendering.h` | Image Reconstruction | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/ImageReconstruction/FidelityFX.cpp` | Image Reconstruction | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/ImageReconstruction/FidelityFX.h` | Image Reconstruction | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/ImageReconstruction/RCAS/RCAS.cpp` | Image Reconstruction | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/ImageReconstruction/RCAS/RCAS.h` | Image Reconstruction | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/ImageReconstruction/Streamline.cpp` | Image Reconstruction | YES | YES | YES | YES | YES | YES | YES |
| `engine/Modules/ImageReconstruction/Streamline.h` | Image Reconstruction | YES | YES | YES | YES | YES | YES | YES |
| `engine/Modules/AmbientProbe.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/AmbientProbe.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/Atmosphere.cpp` | Module Infrastructure | YES | YES | YES | YES | YES | YES | YES |
| `engine/Modules/Atmosphere.h` | Module Infrastructure | YES | YES | YES | YES | YES | YES | YES |
| `engine/Modules/CameraSuite.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/CameraSuite.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/ContactShadows.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/ContactShadows.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/DistanceBlend.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/DistanceBlend.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/FoliageDynamics.cpp` | Module Infrastructure | YES | YES | YES | YES | YES | YES | YES |
| `engine/Modules/FoliageDynamics.h` | Module Infrastructure | YES | YES | YES | YES | YES | YES | YES |
| `engine/Modules/GroundResponse.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/GroundResponse.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/HorizonBlend.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/HorizonBlend.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/HybridGI.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/HybridGI.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/ImageReconstruction.cpp` | Module Infrastructure | YES | YES | YES | YES | YES | YES | YES |
| `engine/Modules/ImageReconstruction.h` | Module Infrastructure | YES | YES | YES | YES | YES | YES | YES |
| `engine/Modules/InteriorDaylight.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/InteriorDaylight.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/LightVolumes.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/LightVolumes.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/LinearLightCore.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/LinearLightCore.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/MaterialLayers.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/MaterialLayers.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/NaturalLighting.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/NaturalLighting.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/OverlayFeature.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/PixelCapture.cpp` | Module Infrastructure | YES | YES | YES | YES | YES | YES | YES |
| `engine/Modules/PixelCapture.h` | Module Infrastructure | YES | YES | YES | YES | YES | YES | YES |
| `engine/Modules/PulseProfiler.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/PulseProfiler.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/RadiantGrid.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/RadiantGrid.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/RainResponse.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/RainResponse.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/SkinOptics.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/SkinOptics.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/SkyBounce.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/SkyBounce.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/SkyContinuity.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/SkyContinuity.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/SkyVeil.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/SkyVeil.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/StrandShading.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/StrandShading.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/TerrainDetail.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/TerrainDetail.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/TerrainField.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/TerrainField.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/TerrainOcclusion.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/TerrainOcclusion.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/TerrainSeam.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/TerrainSeam.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/ThinSurface.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/ThinSurface.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/TissueDiffusion.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/TissueDiffusion.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/VolumeOcclusion.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/VolumeOcclusion.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/Waterbody.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/Waterbody.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/WaterOptics.cpp` | Module Infrastructure | YES | YES | YES | YES | YES | YES | YES |
| `engine/Modules/WaterOptics.h` | Module Infrastructure | YES | YES | YES | YES | YES | YES | YES |
| `engine/Modules/WindowLife.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/WindowLife.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/WorldProbes.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/WorldProbes.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/NaturalLighting/Common.h` | Natural Lighting | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/PulseProfiler/ABTesting/ABTestAggregator.cpp` | Pulse Profiler | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/PulseProfiler/ABTesting/ABTestAggregator.h` | Pulse Profiler | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/PulseProfiler/ABTesting/ABTesting.cpp` | Pulse Profiler | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/PulseProfiler/ABTesting/ABTesting.h` | Pulse Profiler | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/Waterbody/Flowmap.cpp` | Waterbody | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/Waterbody/Flowmap.h` | Waterbody | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/Waterbody/WaterCache.cpp` | Waterbody | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/Waterbody/WaterCache.h` | Waterbody | YES | YES | NO | YES | YES | YES | YES |
| `engine/EngineFix.cpp` | Runtime Hooks | YES | YES | NO | YES | YES | YES | YES |
| `engine/EngineFix.h` | Runtime Hooks | YES | YES | NO | YES | YES | YES | YES |
| `engine/EngineFixes/EffectShaderNoDecalsFix.cpp` | Runtime Hooks | YES | YES | NO | YES | YES | YES | YES |
| `engine/EngineFixes/EffectShaderNoDecalsFix.h` | Runtime Hooks | YES | YES | NO | YES | YES | YES | YES |
| `engine/EngineFixes/ShadowmapCascadeCullingFix.cpp` | Runtime Hooks | YES | YES | NO | YES | YES | YES | YES |
| `engine/EngineFixes/ShadowmapCascadeCullingFix.h` | Runtime Hooks | YES | YES | NO | YES | YES | YES | YES |
| `engine/EngineFixes/ShadowmapCascadeRasterizerFix.cpp` | Runtime Hooks | YES | YES | NO | YES | YES | YES | YES |
| `engine/EngineFixes/ShadowmapCascadeRasterizerFix.h` | Runtime Hooks | YES | YES | NO | YES | YES | YES | YES |
| `engine/Hooks.cpp` | Runtime Hooks | YES | YES | NO | YES | YES | YES | YES |
| `engine/Hooks.h` | Runtime Hooks | YES | YES | NO | YES | YES | YES | YES |
| `include/DynamicWetness_PublicAPI.h` | Public/Interop Headers | YES | YES | NO | YES | YES | YES | YES |
| `include/ENB/AntTweakBar.h` | Public/Interop Headers | YES | YES | NO | YES | YES | YES | YES |
| `include/ENB/ENBSeriesAPI.h` | Public/Interop Headers | YES | YES | NO | YES | YES | YES | YES |
| `include/ENB/ENBSeriesSDK.h` | Public/Interop Headers | YES | YES | NO | YES | YES | YES | YES |
| `include/FidelityFX/api/include/dx12/ffx_api_dx12.h` | Public/Interop Headers | YES | YES | NO | YES | YES | YES | YES |
| `include/FidelityFX/api/include/dx12/ffx_api_dx12.hpp` | Public/Interop Headers | YES | YES | NO | YES | YES | YES | YES |
| `include/FidelityFX/api/include/ffx_api.h` | Public/Interop Headers | YES | YES | NO | YES | YES | YES | YES |
| `include/FidelityFX/api/include/ffx_api.hpp` | Public/Interop Headers | YES | YES | NO | YES | YES | YES | YES |
| `include/FidelityFX/api/include/ffx_api_loader.h` | Public/Interop Headers | YES | YES | NO | YES | YES | YES | YES |
| `include/FidelityFX/api/include/ffx_api_types.h` | Public/Interop Headers | YES | YES | NO | YES | YES | YES | YES |
| `include/FidelityFX/api/include/ffx_api-helper.h` | Public/Interop Headers | YES | YES | NO | YES | YES | YES | YES |
| `include/FidelityFX/framegeneration/include/dx12/ffx_api_framegeneration_dx12.h` | Public/Interop Headers | YES | YES | NO | YES | YES | YES | YES |
| `include/FidelityFX/framegeneration/include/dx12/ffx_api_framegeneration_dx12.hpp` | Public/Interop Headers | YES | YES | NO | YES | YES | YES | YES |
| `include/FidelityFX/framegeneration/include/ffx_framegeneration.h` | Public/Interop Headers | YES | YES | NO | YES | YES | YES | YES |
| `include/FidelityFX/framegeneration/include/ffx_framegeneration.hpp` | Public/Interop Headers | YES | YES | NO | YES | YES | YES | YES |
| `include/FidelityFX/framegeneration/include/ffx_framegeneration_api_types.h` | Public/Interop Headers | YES | YES | NO | YES | YES | YES | YES |
| `include/FrameAnnotations.h` | Public/Interop Headers | YES | YES | NO | YES | YES | YES | YES |
| `include/PCH.h` | Public/Interop Headers | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Radiant Grid/Assets/LightProfiles/black.ini` | Radiant Grid | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Radiant Grid/Assets/LightProfiles/enblightglow.ini` | Radiant Grid | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Radiant Grid/Assets/LightProfiles/fxglowenb.ini` | Radiant Grid | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Radiant Grid/Assets/LightProfiles/glowsoft01_enbl.ini` | Radiant Grid | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Radiant Grid/Assets/LightProfiles/po3_fullalpha.ini` | Radiant Grid | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Terrain Field/Assets/Plugin/PIXL-TerrainField.esp` | Terrain Field | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/Blackreach.Terrain.HeightMap.-7.-8.7.12.-284.861.dds` | Terrain Occlusion | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/ccBGSSSE067DeadlandsWorld.Terrain.HeightMap.-6.-4.4.7.-140.353.dds` | Terrain Occlusion | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/ccKRTSSE001QNWorld.Terrain.HeightMap.-3.-4.2.2.-972.265.dds` | Terrain Occlusion | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/DeepwoodRedoubtWorld.Terrain.HeightMap.-39.15.-27.24.-618.472.dds` | Terrain Occlusion | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/DLC01FalmerValley.Terrain.HeightMap.-16.-13.10.13.-530.2212.dds` | Terrain Occlusion | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/DLC01SoulCairn.Terrain.HeightMap.-52.-43.30.44.-188.245.dds` | Terrain Occlusion | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/DLC1HunterHQWorld.Terrain.HeightMap.-31.-8.3.5.-662.308.dds` | Terrain Occlusion | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/DLC2ApocryphaWorld.Terrain.HeightMap.-6.-1.7.9.-250.-206.dds` | Terrain Occlusion | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/DLC2SolstheimWorld.Terrain.HeightMap.-64.-64.127.127.-1024.6342.dds` | Terrain Occlusion | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/FORMAT.md` | Terrain Occlusion | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/JaphetsFollyWorld.Terrain.HeightMap.-7.-6.6.6.-210.163.dds` | Terrain Occlusion | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/MarkarthWorld.Terrain.HeightMap.-48.-3.-31.5.-3375.2609.dds` | Terrain Occlusion | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/SkuldafnWorld.Terrain.HeightMap.40.-21.62.-1.-285.1876.dds` | Terrain Occlusion | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/Sovngarde.Terrain.HeightMap.-23.-32.34.40.-1331.4715.dds` | Terrain Occlusion | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/Tamriel.Terrain.HeightMap.-57.-43.61.50.-4629.4924.dds` | Terrain Occlusion | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Waterbody/Assets/Meshes/Water/OptimisedWaterMesh.nif` | Waterbody | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Waterbody/Assets/Meshes/Water/WaterMesh.nif` | Waterbody | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Waterbody/Assets/WorldWater/Tamriel_precache.wpc` | Waterbody | YES | YES | NO | YES | YES | YES | YES |
| `engine/Deferred.cpp` | Shader Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Deferred.h` | Shader Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/PipelineBuffer.cpp` | Shader Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/PipelineBuffer.h` | Shader Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/PipelineHealth.cpp` | Shader Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/PipelineHealth.h` | Shader Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/ShaderCache.cpp` | Shader Infrastructure | YES | YES | YES | YES | YES | YES | YES |
| `engine/ShaderCache.h` | Shader Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/ShaderFileWatcher.h` | Shader Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/ShaderTools/BSShader.h` | Shader Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/ShaderTools/BSShaderHooks.cpp` | Shader Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/ShaderTools/BSShaderHooks.h` | Shader Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/ShaderTools/ShaderCompiler.cpp` | Shader Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/ShaderTools/ShaderCompiler.h` | Shader Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `extern/CommonLibSSE-NG@8f4205da56f01cbe98557422c008a5719c180fb9` | CommonLibSSE-NG | YES | YES | NO | YES | YES | YES | YES |
| `extern/FidelityFX-SDK@e65b2530631f2afb9a9ac753884926e49e20d608` | FidelityFX SDK | YES | YES | NO | YES | YES | YES | YES |
| `extern/Streamline-DX12@a9ed1f58436864891f68b0458300464dc53d9a69` | NVIDIA Streamline | YES | YES | NO | YES | YES | YES | YES |
| `engine/Menu.cpp` | UI | YES | YES | NO | YES | YES | YES | YES |
| `engine/Menu.h` | UI | YES | YES | YES | YES | YES | YES | YES |
| `engine/Menu/BackgroundBlur.cpp` | UI | YES | YES | NO | YES | YES | YES | YES |
| `engine/Menu/BackgroundBlur.h` | UI | YES | YES | NO | YES | YES | YES | YES |
| `engine/Menu/CursorLoader.cpp` | UI | YES | YES | NO | YES | YES | YES | YES |
| `engine/Menu/CursorLoader.h` | UI | YES | YES | NO | YES | YES | YES | YES |
| `engine/Menu/Fonts.cpp` | UI | YES | YES | NO | YES | YES | YES | YES |
| `engine/Menu/Fonts.h` | UI | YES | YES | NO | YES | YES | YES | YES |
| `engine/Menu/FontSelector.cpp` | UI | YES | YES | NO | YES | YES | YES | YES |
| `engine/Menu/FontSelector.h` | UI | YES | YES | NO | YES | YES | YES | YES |
| `engine/Menu/IconLoader.cpp` | UI | YES | YES | NO | YES | YES | YES | YES |
| `engine/Menu/IconLoader.h` | UI | YES | YES | NO | YES | YES | YES | YES |
| `engine/Menu/LaunchExperienceRenderer.cpp` | UI | YES | YES | NO | YES | YES | YES | YES |
| `engine/Menu/LaunchExperienceRenderer.h` | UI | YES | YES | NO | YES | YES | YES | YES |
| `engine/Menu/OverlayRenderer.cpp` | UI | YES | YES | NO | YES | YES | YES | YES |
| `engine/Menu/OverlayRenderer.h` | UI | YES | YES | NO | YES | YES | YES | YES |
| `engine/Menu/PIXLRendererPage.cpp` | UI | YES | YES | YES | YES | YES | YES | YES |
| `engine/Menu/PIXLRendererPage.h` | UI | YES | YES | NO | YES | YES | YES | YES |
| `engine/Menu/PIXLStyle.h` | UI | YES | YES | NO | YES | YES | YES | YES |
| `engine/Menu/PulsePanelRenderer.cpp` | UI | YES | YES | NO | YES | YES | YES | YES |
| `engine/Menu/PulsePanelRenderer.h` | UI | YES | YES | NO | YES | YES | YES | YES |
| `engine/Menu/RuntimeSettingsRenderer.cpp` | UI | YES | YES | NO | YES | YES | YES | YES |
| `engine/Menu/RuntimeSettingsRenderer.h` | UI | YES | YES | NO | YES | YES | YES | YES |
| `engine/Menu/ThemeManager.cpp` | UI | YES | YES | NO | YES | YES | YES | YES |
| `engine/Menu/ThemeManager.h` | UI | YES | YES | NO | YES | YES | YES | YES |
| `engine/Menu/TuningWorkspaceRenderer.cpp` | UI | YES | YES | YES | YES | YES | YES | YES |
| `engine/Menu/TuningWorkspaceRenderer.h` | UI | YES | YES | NO | YES | YES | YES | YES |
| `engine/Menu/WorkshopToolsRenderer.cpp` | UI | YES | YES | NO | YES | YES | YES | YES |
| `engine/Menu/WorkshopToolsRenderer.h` | UI | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Interface/PIXLRenderer/Fonts/Jost/Jost-Light.ttf` | UI Assets | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Interface/PIXLRenderer/Fonts/Jost/Jost-Regular.ttf` | UI Assets | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Interface/PIXLRenderer/Fonts/Jost/Jost-SemiBold.ttf` | UI Assets | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Interface/PIXLRenderer/Fonts/Jost/OFL.txt` | UI Assets | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Interface/PIXLRenderer/Fonts/Sanguis/Sanguis OFL License.txt` | UI Assets | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Interface/PIXLRenderer/Fonts/Sanguis/Sanguis.ttf` | UI Assets | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Interface/PIXLRenderer/Visuals/Brand/PIXL-Mark.png` | UI Assets | YES | YES | NO | YES | YES | YES | YES |
| `distribution/SKSE/Plugins/PIXLRenderer/QualityPreviews/README.txt` | UI Assets | YES | YES | NO | YES | YES | YES | YES |
| `distribution/SKSE/Plugins/PIXLRenderer/QualityPreviews/Profile_LOW.png` | UI Assets | YES | YES | YES | YES | YES | YES | YES |
| `distribution/SKSE/Plugins/PIXLRenderer/QualityPreviews/Profile_MEDIUM.png` | UI Assets | YES | YES | YES | YES | YES | YES | YES |
| `distribution/SKSE/Plugins/PIXLRenderer/QualityPreviews/Profile_HIGH.png` | UI Assets | YES | YES | YES | YES | YES | YES | YES |
| `distribution/SKSE/Plugins/PIXLRenderer/QualityPreviews/Profile_ULTRA.png` | UI Assets | YES | YES | YES | YES | YES | YES | YES |
| `distribution/SKSE/Plugins/PIXLRenderer/QualityPreviews/Neural_ULTRA.png` | UI Assets | YES | YES | YES | YES | YES | YES | YES |
| `engine/I18n/I18n.cpp` | Localization | YES | YES | NO | YES | YES | YES | YES |
| `engine/I18n/I18n.h` | Localization | YES | YES | NO | YES | YES | YES | YES |
| `engine/Renderer/QualityProfiles.cpp` | Quality Profiles | YES | YES | NO | YES | YES | YES | YES |
| `engine/Renderer/QualityProfiles.h` | Quality Profiles | YES | YES | NO | YES | YES | YES | YES |
