# PIXL Renderer Active File Review Matrix

Generated from the canonical CMake source globs and the release staging copy graph. `Reviewed = NO` is the initial anti-skipping state and may only become `YES` after per-file inspection.

| File | Module | Build Active | Reviewed | Modified | Security | Fidelity | Perf | Future Ideas |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `engine/Renderer/WorldBenchmark.cpp` | World Benchmark | YES | NO | NO | NO | NO | NO | NO |
| `engine/Renderer/WorldBenchmark.h` | World Benchmark | YES | NO | NO | NO | NO | NO | NO |
| `tools/AuditPixlRenderer.ps1` | Build / Release | YES | NO | NO | NO | NO | NO | NO |
| `tools/CompareLiveShaders.ps1` | Build / Release | YES | NO | NO | NO | NO | NO | NO |
| `tools/ExportPixlPublicSource.ps1` | Build / Release | YES | NO | NO | NO | NO | NO | NO |
| `tools/StagePixlRendererStandalone.ps1` | Build / Release | YES | NO | NO | NO | NO | NO | NO |
| `.gitattributes` | Build / Release | YES | NO | NO | NO | NO | NO | NO |
| `.gitignore` | Build / Release | YES | NO | NO | NO | NO | NO | NO |
| `.gitmodules` | Build / Release | YES | NO | NO | NO | NO | NO | NO |
| `BuildDebug.bat` | Build / Release | YES | NO | NO | NO | NO | NO | NO |
| `BuildDev.bat` | Build / Release | YES | NO | NO | NO | NO | NO | NO |
| `BuildDevFast.bat` | Build / Release | YES | NO | NO | NO | NO | NO | NO |
| `BuildPR.bat` | Build / Release | YES | NO | NO | NO | NO | NO | NO |
| `BuildRelease.bat` | Build / Release | YES | NO | NO | NO | NO | NO | NO |
| `cmake/AddCXXFiles.cmake` | Build / Release | YES | NO | NO | NO | NO | NO | NO |
| `cmake/FidelityFX-SDK.cmake` | Build / Release | YES | NO | NO | NO | NO | NO | NO |
| `cmake/ModuleVersions.h.in` | Build / Release | YES | NO | NO | NO | NO | NO | NO |
| `cmake/patches/FidelityFX-DX11-Short-Output.patch` | Build / Release | YES | NO | NO | NO | NO | NO | NO |
| `cmake/Plugin.h.in` | Build / Release | YES | NO | NO | NO | NO | NO | NO |
| `cmake/ports/clib-util/portfile.cmake` | Build / Release | YES | NO | NO | NO | NO | NO | NO |
| `cmake/ports/clib-util/vcpkg.json` | Build / Release | YES | NO | NO | NO | NO | NO | NO |
| `cmake/ports/tracy/build-tools.patch` | Build / Release | YES | NO | NO | NO | NO | NO | NO |
| `cmake/ports/tracy/portfile.cmake` | Build / Release | YES | NO | NO | NO | NO | NO | NO |
| `cmake/ports/tracy/vcpkg.json` | Build / Release | YES | NO | NO | NO | NO | NO | NO |
| `cmake/shadertoolsconfig.json.in` | Build / Release | YES | NO | NO | NO | NO | NO | NO |
| `cmake/Streamline/CMakeLists.txt` | Build / Release | YES | NO | NO | NO | NO | NO | NO |
| `cmake/ThemePresets.h.in` | Build / Release | YES | NO | NO | NO | NO | NO | NO |
| `cmake/triplets/x64-windows-static-md-release.cmake` | Build / Release | YES | NO | NO | NO | NO | NO | NO |
| `cmake/Version.rc.in` | Build / Release | YES | NO | NO | NO | NO | NO | NO |
| `cmake/XSEPlugin.cmake` | Build / Release | YES | NO | NO | NO | NO | NO | NO |
| `CMakeLists.txt` | Build / Release | YES | NO | NO | NO | NO | NO | NO |
| `CMakePresets.json` | Build / Release | YES | NO | NO | NO | NO | NO | NO |
| `README.md` | Build / Release | YES | NO | NO | NO | NO | NO | NO |
| `SOURCE_DEPENDENCIES.md` | Build / Release | YES | NO | NO | NO | NO | NO | NO |
| `vcpkg.json` | Build / Release | YES | NO | NO | NO | NO | NO | NO |
| `build/PIXL-12C/cmake/ModuleVersions.h` | Generated Build Inputs | YES | NO | NO | NO | NO | NO | NO |
| `build/PIXL-12C/cmake/Plugin.h` | Generated Build Inputs | YES | NO | NO | NO | NO | NO | NO |
| `build/PIXL-12C/cmake/ThemePresets.h` | Generated Build Inputs | YES | NO | NO | NO | NO | NO | NO |
| `build/PIXL-12C/cmake/version.rc` | Generated Build Inputs | YES | NO | NO | NO | NO | NO | NO |
| `build/PIXL-12C/CMakeFiles/PIXLRenderer.dir/cmake_pch.cxx` | Generated Build Inputs | YES | NO | NO | NO | NO | NO | NO |
| `build/PIXL-12C/CMakeFiles/PIXLRenderer.dir/Release/cmake_pch.hxx` | Generated Build Inputs | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/AmbientProbe/Module.ini` | AmbientProbe | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Atmosphere/Module.ini` | Atmosphere | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Camera Suite/Module.ini` | CameraSuite | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Contact Shadows/Module.ini` | ContactShadows | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Distance Blend/Module.ini` | DistanceBlend | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Foliage Dynamics/Module.ini` | FoliageDynamics | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Ground Response/Module.ini` | GroundResponse | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/HorizonBlend/Module.ini` | HorizonBlend | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Hybrid GI/Module.ini` | HybridGI | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/ImageReconstruction/Module.ini` | ImageReconstruction | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Interior Daylight/Module.ini` | InteriorDaylight | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Light Volumes/Module.ini` | LightVolumes | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Linear Light Core/Module.ini` | LinearLightCore | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/MaterialForge/Module.ini` | MaterialForge | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Material Layers/Module.ini` | MaterialLayers | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Natural Lighting/Module.ini` | NaturalLighting | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Pixel Capture/Module.ini` | PixelCapture | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Pulse Profiler/Module.ini` | PulseProfiler | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Radiant Grid/Module.ini` | RadiantGrid | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Rain Response/Module.ini` | RainResponse | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/SkinOptics/Module.ini` | SkinOptics | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/SkyBounce/Module.ini` | SkyBounce | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Sky Continuity/Module.ini` | SkyContinuity | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Sky Veil/Module.ini` | SkyVeil | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Strand Shading/Module.ini` | StrandShading | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Terrain Detail/Module.ini` | TerrainDetail | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Terrain Field/Module.ini` | TerrainField | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Terrain Occlusion/Module.ini` | TerrainOcclusion | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Terrain Seam/Module.ini` | TerrainSeam | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Thin Surface/Module.ini` | ThinSurface | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Tissue Diffusion/Module.ini` | TissueDiffusion | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Volume Occlusion/Module.ini` | VolumeOcclusion | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Waterbody/Module.ini` | Waterbody | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Water Optics/Module.ini` | WaterOptics | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/WindowLife/Module.ini` | WindowLife | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/World Probes/Module.ini` | WorldProbes | YES | NO | NO | NO | NO | NO | NO |
| `engine/Buffer.h` | Core Renderer | YES | NO | NO | NO | NO | NO | NO |
| `engine/FrameAnnotations.cpp` | Core Renderer | YES | NO | NO | NO | NO | NO | NO |
| `engine/Globals.cpp` | Core Renderer | YES | NO | NO | NO | NO | NO | NO |
| `engine/Globals.h` | Core Renderer | YES | NO | NO | NO | NO | NO | NO |
| `engine/ModuleGroups.h` | Core Renderer | YES | NO | NO | NO | NO | NO | NO |
| `engine/ModuleRules.cpp` | Core Renderer | YES | NO | NO | NO | NO | NO | NO |
| `engine/ModuleRules.h` | Core Renderer | YES | NO | NO | NO | NO | NO | NO |
| `engine/PCH.cpp` | Core Renderer | YES | NO | NO | NO | NO | NO | NO |
| `engine/Profiler.cpp` | Core Renderer | YES | NO | NO | NO | NO | NO | NO |
| `engine/Profiler.h` | Core Renderer | YES | NO | NO | NO | NO | NO | NO |
| `engine/RenderModule.cpp` | Core Renderer | YES | NO | NO | NO | NO | NO | NO |
| `engine/RenderModule.h` | Core Renderer | YES | NO | NO | NO | NO | NO | NO |
| `engine/SceneSettingsManager.cpp` | Core Renderer | YES | NO | NO | NO | NO | NO | NO |
| `engine/SceneSettingsManager.h` | Core Renderer | YES | NO | NO | NO | NO | NO | NO |
| `engine/SettingsOverrideManager.cpp` | Core Renderer | YES | NO | NO | NO | NO | NO | NO |
| `engine/SettingsOverrideManager.h` | Core Renderer | YES | NO | NO | NO | NO | NO | NO |
| `engine/State.cpp` | Core Renderer | YES | NO | NO | NO | NO | NO | NO |
| `engine/State.h` | Core Renderer | YES | NO | NO | NO | NO | NO | NO |
| `engine/Util.h` | Core Renderer | YES | NO | NO | NO | NO | NO | NO |
| `engine/WeatherManager.cpp` | Core Renderer | YES | NO | NO | NO | NO | NO | NO |
| `engine/WeatherManager.h` | Core Renderer | YES | NO | NO | NO | NO | NO | NO |
| `engine/WeatherVariableRegistry.h` | Core Renderer | YES | NO | NO | NO | NO | NO | NO |
| `engine/XSEPlugin.cpp` | Core Renderer | YES | NO | NO | NO | NO | NO | NO |
| `engine/Utils/ActorUtils.cpp` | Core Utilities | YES | NO | NO | NO | NO | NO | NO |
| `engine/Utils/ActorUtils.h` | Core Utilities | YES | NO | NO | NO | NO | NO | NO |
| `engine/Utils/D3D.cpp` | Core Utilities | YES | NO | NO | NO | NO | NO | NO |
| `engine/Utils/D3D.h` | Core Utilities | YES | NO | NO | NO | NO | NO | NO |
| `engine/Utils/ExternalEmittance.cpp` | Core Utilities | YES | NO | NO | NO | NO | NO | NO |
| `engine/Utils/ExternalEmittance.h` | Core Utilities | YES | NO | NO | NO | NO | NO | NO |
| `engine/Utils/FileSystem.cpp` | Core Utilities | YES | NO | NO | NO | NO | NO | NO |
| `engine/Utils/FileSystem.h` | Core Utilities | YES | NO | NO | NO | NO | NO | NO |
| `engine/Utils/Form.cpp` | Core Utilities | YES | NO | NO | NO | NO | NO | NO |
| `engine/Utils/Form.h` | Core Utilities | YES | NO | NO | NO | NO | NO | NO |
| `engine/Utils/Format.cpp` | Core Utilities | YES | NO | NO | NO | NO | NO | NO |
| `engine/Utils/Format.h` | Core Utilities | YES | NO | NO | NO | NO | NO | NO |
| `engine/Utils/Game.cpp` | Core Utilities | YES | NO | NO | NO | NO | NO | NO |
| `engine/Utils/Game.h` | Core Utilities | YES | NO | NO | NO | NO | NO | NO |
| `engine/Utils/GameSetting.cpp` | Core Utilities | YES | NO | NO | NO | NO | NO | NO |
| `engine/Utils/GameSetting.h` | Core Utilities | YES | NO | NO | NO | NO | NO | NO |
| `engine/Utils/Input.h` | Core Utilities | YES | NO | NO | NO | NO | NO | NO |
| `engine/Utils/LegitProfiler.h` | Core Utilities | YES | NO | NO | NO | NO | NO | NO |
| `engine/Utils/Moon.h` | Core Utilities | YES | NO | NO | NO | NO | NO | NO |
| `engine/Utils/PerfUtils.h` | Core Utilities | YES | NO | NO | NO | NO | NO | NO |
| `engine/Utils/RestartSettings.h` | Core Utilities | YES | NO | NO | NO | NO | NO | NO |
| `engine/Utils/Serialize.cpp` | Core Utilities | YES | NO | NO | NO | NO | NO | NO |
| `engine/Utils/Serialize.h` | Core Utilities | YES | NO | NO | NO | NO | NO | NO |
| `engine/Utils/SphericalHarmonics.cpp` | Core Utilities | YES | NO | NO | NO | NO | NO | NO |
| `engine/Utils/SphericalHarmonics.h` | Core Utilities | YES | NO | NO | NO | NO | NO | NO |
| `engine/Utils/Subrect.cpp` | Core Utilities | YES | NO | NO | NO | NO | NO | NO |
| `engine/Utils/Subrect.h` | Core Utilities | YES | NO | NO | NO | NO | NO | NO |
| `engine/Utils/UI.cpp` | Core Utilities | YES | NO | NO | NO | NO | NO | NO |
| `engine/Utils/UI.h` | Core Utilities | YES | NO | NO | NO | NO | NO | NO |
| `engine/Utils/WinApi.cpp` | Core Utilities | YES | NO | NO | NO | NO | NO | NO |
| `engine/Utils/WinApi.h` | Core Utilities | YES | NO | NO | NO | NO | NO | NO |
| `ATTRIBUTION.md` | Release Package | YES | NO | NO | NO | NO | NO | NO |
| `COPYING` | Release Package | YES | NO | NO | NO | NO | NO | NO |
| `distribution/PIXL-RENDERER-README.md` | Release Package | YES | NO | NO | NO | NO | NO | NO |
| `distribution/SKSE/Plugins/PIXLRenderer/Presets/PIXL-Renderer-Live-Tested.json` | Release Package | YES | NO | NO | NO | NO | NO | NO |
| `distribution/SKSE/Plugins/PIXLRenderer/SettingsDefault.json` | Release Package | YES | NO | NO | NO | NO | NO | NO |
| `distribution/SKSE/Plugins/PIXLRenderer/Themes/PIXL.json` | Release Package | YES | NO | NO | NO | NO | NO | NO |
| `distribution/SKSE/Plugins/PIXLRenderer/Translations/en.json` | Release Package | YES | NO | NO | NO | NO | NO | NO |
| `distribution/SOURCE-AND-CREDITS.md` | Release Package | YES | NO | NO | NO | NO | NO | NO |
| `EXCEPTIONS.md` | Release Package | YES | NO | NO | NO | NO | NO | NO |
| `THIRD_PARTY_NOTICES.md` | Release Package | YES | NO | NO | NO | NO | NO | NO |
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
| `pipeline/AmbientProbe/Kernels/AmbientProbe/AmbientProbe.hlsli` | AmbientProbe | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/AmbientProbe/Kernels/AmbientProbe/DiffuseAmbientProbe.dds` | AmbientProbe | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/AmbientProbe/Kernels/AmbientProbe/DiffuseAmbientProbeCS.hlsl` | AmbientProbe | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/AmbientProbe/Kernels/AmbientProbe/SpecAmbientProbe.dds` | AmbientProbe | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Atmosphere/Kernels/Atmosphere/Atmosphere.hlsli` | Atmosphere | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Atmosphere/Kernels/Atmosphere/VolumetricFogCommon.hlsli` | Atmosphere | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Atmosphere/Kernels/Atmosphere/VolumetricFogConservativeDepthCS.hlsl` | Atmosphere | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Atmosphere/Kernels/Atmosphere/VolumetricFogCSCommon.hlsli` | Atmosphere | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Atmosphere/Kernels/Atmosphere/VolumetricFogIntegrationCS.hlsl` | Atmosphere | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Atmosphere/Kernels/Atmosphere/VolumetricFogLightScatteringCS.hlsl` | Atmosphere | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Atmosphere/Kernels/Atmosphere/VolumetricFogMaterialCS.hlsl` | Atmosphere | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Camera Suite/Kernels/CameraSuite/BloomDownsampleCS.hlsl` | CameraSuite | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Camera Suite/Kernels/CameraSuite/BloomPrefilterCS.hlsl` | CameraSuite | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Camera Suite/Kernels/CameraSuite/BloomUpsampleCS.hlsl` | CameraSuite | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Camera Suite/Kernels/CameraSuite/HDROutputCS.hlsl` | CameraSuite | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Camera Suite/Kernels/CameraSuite/HDRSun.hlsli` | CameraSuite | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Camera Suite/Kernels/CameraSuite/Lens/FireMask.png` | CameraSuite | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Camera Suite/Kernels/CameraSuite/Lens/FrostMask.png` | CameraSuite | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Camera Suite/Kernels/CameraSuite/Lens/PROVENANCE.md` | CameraSuite | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Camera Suite/Kernels/CameraSuite/Looks/Bleach.png` | CameraSuite | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Camera Suite/Kernels/CameraSuite/Looks/Bleak.png` | CameraSuite | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Camera Suite/Kernels/CameraSuite/Looks/Cinematic.png` | CameraSuite | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Camera Suite/Kernels/CameraSuite/Looks/Dramatic.png` | CameraSuite | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Camera Suite/Kernels/CameraSuite/Looks/FantasyGreen.png` | CameraSuite | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Camera Suite/Kernels/CameraSuite/Looks/Hearthfire.png` | CameraSuite | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Camera Suite/Kernels/CameraSuite/Looks/Nightfall.png` | CameraSuite | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Camera Suite/Kernels/CameraSuite/Looks/NordicNeutral.png` | CameraSuite | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Camera Suite/Kernels/CameraSuite/Looks/PROVENANCE.md` | CameraSuite | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Camera Suite/Kernels/CameraSuite/Looks/Saga.png` | CameraSuite | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Camera Suite/Kernels/CameraSuite/Looks/Sunset.png` | CameraSuite | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Camera Suite/Kernels/CameraSuite/Looks/Winter.png` | CameraSuite | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Camera Suite/Kernels/CameraSuite/PhysicalCameraCommon.hlsli` | CameraSuite | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Camera Suite/Kernels/CameraSuite/PhysicalCameraExposureCS.hlsl` | CameraSuite | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Camera Suite/Kernels/CameraSuite/PhysicalCameraHistogramCS.hlsl` | CameraSuite | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Camera Suite/Kernels/CameraSuite/PhysicalCameraLocalExposureCS.hlsl` | CameraSuite | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Camera Suite/Kernels/CameraSuite/Stormglass.hlsli` | CameraSuite | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Camera Suite/Kernels/CameraSuite/StormglassFieldCS.hlsl` | CameraSuite | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Camera Suite/Kernels/CameraSuite/UIBrightnessCS.hlsl` | CameraSuite | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Contact Shadows/Kernels/ContactShadows/bend_sss_gpu.hlsli` | ContactShadows | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Contact Shadows/Kernels/ContactShadows/ContactShadows.hlsli` | ContactShadows | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Contact Shadows/Kernels/ContactShadows/RaymarchCS.hlsl` | ContactShadows | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Foliage Dynamics/Kernels/FoliageDynamics/FoliageDynamics.hlsli` | FoliageDynamics | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Foliage Dynamics/Kernels/FoliageDynamics/FoliageTuning.hlsli` | FoliageDynamics | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Foliage Dynamics/Kernels/FoliageDynamics/FoliageWind.hlsli` | FoliageDynamics | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Ground Response/Kernels/GroundResponse/CollisionUpdateCS.hlsl` | GroundResponse | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Ground Response/Kernels/GroundResponse/DeformableGround.hlsli` | GroundResponse | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Ground Response/Kernels/GroundResponse/GroundResponse.hlsli` | GroundResponse | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Ground Response/Kernels/GroundResponse/Runtime.hlsli` | GroundResponse | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Ground Response/Kernels/GroundResponse/SNOW_MICRO_PROVENANCE.md` | GroundResponse | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Ground Response/Kernels/GroundResponse/SnowMicro.png` | GroundResponse | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Ground Response/Kernels/GroundResponse/SurfaceDeformationUpdateCS.hlsl` | GroundResponse | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Ground Response/Kernels/GroundResponse/TerrainSurface.hlsl` | GroundResponse | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Hybrid GI/Kernels/HybridGI/blur.cs.hlsl` | HybridGI | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Hybrid GI/Kernels/HybridGI/common.hlsli` | HybridGI | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Hybrid GI/Kernels/HybridGI/fast_2uges.dds` | HybridGI | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Hybrid GI/Kernels/HybridGI/gi.cs.hlsl` | HybridGI | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Hybrid GI/Kernels/HybridGI/hybridReflection.cs.hlsl` | HybridGI | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Hybrid GI/Kernels/HybridGI/hybridReflectionDenoise.cs.hlsl` | HybridGI | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Hybrid GI/Kernels/HybridGI/prefilterDepths.cs.hlsl` | HybridGI | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Hybrid GI/Kernels/HybridGI/prefilterNormal.cs.hlsl` | HybridGI | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Hybrid GI/Kernels/HybridGI/prefilterRadiance.cs.hlsl` | HybridGI | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Hybrid GI/Kernels/HybridGI/radianceDisocc.cs.hlsl` | HybridGI | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Hybrid GI/Kernels/HybridGI/upsample.cs.hlsl` | HybridGI | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Hybrid GI/Kernels/HybridGI/worldCache.hlsli` | HybridGI | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Hybrid GI/Kernels/HybridGI/worldCacheDecay.cs.hlsl` | HybridGI | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Hybrid GI/Kernels/HybridGI/worldCacheInject.cs.hlsl` | HybridGI | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/CopyDepthToSharedBufferPS.hlsl` | ImageReconstruction | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/DepthRefractionUpscalePS.hlsl` | ImageReconstruction | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/EncodeTexturesCS.hlsl` | ImageReconstruction | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/FidelityFX/amd_fidelityfx_framegeneration_dx12.dll` | ImageReconstruction | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/FidelityFX/amd_fidelityfx_loader_dx12.dll` | ImageReconstruction | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/FidelityFX/license.md` | ImageReconstruction | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/RCAS/RCAS.hlsl` | ImageReconstruction | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/Streamline/license.txt` | ImageReconstruction | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/Streamline/nvngx_dlss.dll` | ImageReconstruction | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/Streamline/nvngx_dlss.license.txt` | ImageReconstruction | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/Streamline/reflex.license.txt` | ImageReconstruction | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/Streamline/sl.common.dll` | ImageReconstruction | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/Streamline/sl.dlss.dll` | ImageReconstruction | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/Streamline/sl.interposer.dll` | ImageReconstruction | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/Streamline/sl.pcl.dll` | ImageReconstruction | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/Streamline/sl.reflex.dll` | ImageReconstruction | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/UnderwaterMaskUpscalePS.hlsl` | ImageReconstruction | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/ImageReconstruction/Kernels/ImageReconstruction/UpscaleVS.hlsl` | ImageReconstruction | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Material Layers/Kernels/MaterialLayers/MaterialDetail.hlsli` | MaterialLayers | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Material Layers/Kernels/MaterialLayers/MaterialLayers.hlsli` | MaterialLayers | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Material Layers/Kernels/MaterialLayers/MaterialLayersParallaxCore.hlsli` | MaterialLayers | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Material Layers/Kernels/MaterialLayers/MaterialLayersTerrain.hlsli` | MaterialLayers | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Material Layers/Kernels/MaterialLayers/MaterialLayersTuning.hlsli` | MaterialLayers | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Natural Lighting/Kernels/NaturalLighting/NaturalLighting.hlsli` | NaturalLighting | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Radiant Grid/Kernels/RadiantGrid/ClusterBuildingCS.hlsl` | RadiantGrid | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Radiant Grid/Kernels/RadiantGrid/ClusterCullingCS.hlsl` | RadiantGrid | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Radiant Grid/Kernels/RadiantGrid/Common.hlsli` | RadiantGrid | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Radiant Grid/Kernels/RadiantGrid/RadiantGrid.hlsli` | RadiantGrid | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/Rain Response/Kernels/RainResponse/optimized-ggx.hlsli` | RainResponse | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Rain Response/Kernels/RainResponse/Precipitation.hlsli` | RainResponse | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Rain Response/Kernels/RainResponse/RainResponse.hlsli` | RainResponse | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Rain Response/Kernels/RainResponse/RoofRunoffCompositeCS.hlsl` | RainResponse | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Rain Response/Kernels/RainResponse/RoofRunoffDetectCS.hlsl` | RainResponse | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Rain Response/Kernels/RainResponse/RoofRunoffGenerateCS.hlsl` | RainResponse | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Rain Response/Kernels/RainResponse/RoofRunoffResolveCS.hlsl` | RainResponse | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Rain Response/Kernels/RainResponse/WorldPrecipitation.hlsli` | RainResponse | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/ISApplyReflections.hlsl` | Screen-Space / Camera | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/ISDepthOfField.hlsl` | Screen-Space / Camera | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/ISHDR.hlsl` | Screen-Space / Camera | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/ISReflectionsRayTracing.hlsl` | Screen-Space / Camera | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/Common/BRDF.hlsli` | Shared Shader Math | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/Common/Color.hlsli` | Shared Shader Math | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/Common/DisplayMapping.hlsli` | Shared Shader Math | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/Common/DummyVS.hlsl` | Shared Shader Math | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/Common/DummyVSTexCoord.hlsl` | Shared Shader Math | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/Common/FastMath.hlsli` | Shared Shader Math | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/Common/FrameBuffer.hlsli` | Shared Shader Math | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/Common/Game.hlsli` | Shared Shader Math | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/Common/GBuffer.hlsli` | Shared Shader Math | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/Common/Glints/Glints2023.hlsli` | Shared Shader Math | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/Common/Glints/noisegen.cs.hlsl` | Shared Shader Math | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/Common/LightingCommon.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/Common/LightingEval.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/Common/LightingLandscape.hlsli` | Shared Shader Math | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/Common/LodLandscape.hlsli` | Shared Shader Math | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/Common/MaterialForgeTuning.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/Common/Math.hlsli` | Shared Shader Math | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/Common/MotionBlur.hlsli` | Shared Shader Math | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/Common/PBR.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/Common/PBRMath.hlsli` | Shared Shader Math | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/Common/Permutation.hlsli` | Shared Shader Math | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/Common/PhysicalMaterial.hlsli` | Shared Shader Math | YES | YES | YES | YES | YES | YES | YES |
| `distribution/Shaders/Common/Random.hlsli` | Shared Shader Math | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/Common/Shading.hlsli` | Shared Shader Math | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/Common/ShadowSampling.hlsli` | Shared Shader Math | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/Common/SharedData.hlsli` | Shared Shader Math | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/Common/Skinned.hlsli` | Shared Shader Math | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/Common/Spherical Harmonics/LICENSE` | Shared Shader Math | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/Common/Spherical Harmonics/SphericalHarmonics.hlsli` | Shared Shader Math | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/Common/Triplanar.hlsli` | Shared Shader Math | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/SkinOptics/Kernels/SkinOptics/skin_detail_n.dds` | SkinOptics | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/SkinOptics/Kernels/SkinOptics/SkinOptics.hlsli` | SkinOptics | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/SkyBounce/Kernels/SkyBounce/SkyBounce.hlsli` | SkyBounce | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/SkyBounce/Kernels/SkyBounce/UpdateProbesCS.hlsl` | SkyBounce | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/BloodSplatter.hlsl` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/DeferredCompositeCS.hlsl` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/DialogueFocus/DialogueFocus.hlsli` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/Effect.hlsl` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/EyeRendering/EyeRendering.hlsli` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/ISAlphaBlend.hlsl` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/ISApplyVolumetricLighting.hlsl` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/ISBasicCopy.hlsl` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/ISBlur.hlsl` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/ISCompositeLensFlareVolumetricLighting.hlsl` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/ISCopy.hlsl` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/ISDebugSnow.hlsl` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/ISDoubleVision.hlsl` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/ISDownsample.hlsl` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/ISExp.hlsl` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/ISIBLensFlare.hlsl` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/ISLightingComposite.hlsl` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/ISLocalMap.hlsl` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/ISMap.hlsl` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/ISNoise.hlsl` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/ISRadialBlur.hlsl` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/ISRefraction.hlsl` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/ISSAOBlur.hlsl` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/ISSAOCameraZ.hlsl` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/ISSAOComposite.hlsl` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/ISSAOMinify.hlsl` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/ISSILComposite.hlsl` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/ISSimpleColor.hlsl` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/ISSnowSSS.hlsl` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/ISTemporalAA.hlsl` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/ISUpsampleDynamicResolution.hlsl` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/ISVolumetricLighting.hlsl` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/ISVolumetricLightingBlurHCS.hlsl` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/ISVolumetricLightingBlurVCS.hlsl` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/ISVolumetricLightingGenerateCS.hlsl` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/ISVolumetricLightingRaymarchCS.hlsl` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/ISWaterBlend.hlsl` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/ISWaterDisplacement.hlsl` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/ISWaterFlow.hlsl` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/ISWorldMap.hlsl` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/LICENSE` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/Lighting.hlsl` | Skyrim Shader Entry Points | YES | YES | NO | YES | YES | YES | YES |
| `distribution/Shaders/Menu/BackgroundBlurComposite.hlsl` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/Menu/BackgroundBlurHorizontal.hlsl` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/Menu/BackgroundBlurVertical.hlsl` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/Particle.hlsl` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/Sky.hlsl` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/Utility.hlsl` | Skyrim Shader Entry Points | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Sky Veil/Kernels/SkyVeil/SkyVeil.hlsli` | SkyVeil | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Strand Shading/Kernels/Hair/Hair.hlsli` | StrandShading | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Strand Shading/Kernels/Hair/TangentShift.dds` | StrandShading | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Terrain Detail/Kernels/TerrainDetail/TerrainDetail.hlsli` | TerrainDetail | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Terrain Occlusion/Kernels/TerrainOcclusion/ShadowUpdate.cs.hlsl` | TerrainOcclusion | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Terrain Occlusion/Kernels/TerrainOcclusion/TerrainOcclusion.hlsli` | TerrainOcclusion | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Terrain Seam/Kernels/TerrainSeam/DepthBlend.hlsl` | TerrainSeam | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Terrain Seam/Kernels/TerrainSeam/TerrainSeam.hlsli` | TerrainSeam | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Thin Surface/Kernels/ThinSurface/ThinSurface.hlsli` | ThinSurface | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Tissue Diffusion/Kernels/TissueDiffusion/Burley.hlsli` | TissueDiffusion | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Tissue Diffusion/Kernels/TissueDiffusion/DiffuseExtractionCS.hlsl` | TissueDiffusion | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Tissue Diffusion/Kernels/TissueDiffusion/SeparableSSS.hlsli` | TissueDiffusion | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Tissue Diffusion/Kernels/TissueDiffusion/SeparableSSSCS.hlsl` | TissueDiffusion | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Tissue Diffusion/Kernels/TissueDiffusion/SSSCommon.hlsli` | TissueDiffusion | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/DistantTree.hlsl` | Vegetation | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/RunGrass.hlsl` | Vegetation | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Volume Occlusion/Kernels/VolumeOcclusion/BlurShadowCS.hlsl` | VolumeOcclusion | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Volume Occlusion/Kernels/VolumeOcclusion/DownsampleShadowCS.hlsl` | VolumeOcclusion | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Volume Occlusion/Kernels/VolumeOcclusion/VolumeOcclusion.hlsli` | VolumeOcclusion | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Shaders/Water.hlsl` | Water | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Water Optics/Kernels/WaterOptics/watercaustics.dds` | WaterOptics | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Water Optics/Kernels/WaterOptics/WaterCaustics.hlsli` | WaterOptics | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Water Optics/Kernels/WaterOptics/WaterParallax.hlsli` | WaterOptics | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/WindowLife/Kernels/WindowLife/CurtainAtlas_high-fidelity-2k.dds` | WindowLife | YES | YES | YES | YES | YES | YES | YES |
| `pipeline/WindowLife/Kernels/WindowLife/RoomAtlas_2k.dds` | WindowLife | YES | YES | YES | YES | YES | YES | YES |
| `pipeline/WindowLife/Kernels/WindowLife/CurtainAtlas.png` | WindowLife | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/WindowLife/Kernels/WindowLife/OccupantAtlas.png` | WindowLife | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/WindowLife/Kernels/WindowLife/RoomAtlas.png` | WindowLife | YES | YES | NO | YES | YES | YES | YES |
| `pipeline/WindowLife/Kernels/WindowLife/WindowLife.hlsli` | WindowLife | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/World Probes/Kernels/WorldProbes/BC6HEncodeCS.hlsl` | WorldProbes | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/World Probes/Kernels/WorldProbes/defaultcubemap.dds` | WorldProbes | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/World Probes/Kernels/WorldProbes/InferCubemapCS.hlsl` | WorldProbes | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/World Probes/Kernels/WorldProbes/SpecularIrradianceCS.hlsl` | WorldProbes | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/World Probes/Kernels/WorldProbes/UpdateCubemapCS.hlsl` | WorldProbes | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/World Probes/Kernels/WorldProbes/WorldProbes.hlsli` | WorldProbes | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/DialogueFocus.h` | Camera Suite | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/ContactShadows/bend_sss_cpu.h` | Contact Shadows | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/ImageReconstruction/DX12SwapChain.cpp` | Image Reconstruction | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/ImageReconstruction/DX12SwapChain.h` | Image Reconstruction | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/ImageReconstruction/FidelityFX.cpp` | Image Reconstruction | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/ImageReconstruction/FidelityFX.h` | Image Reconstruction | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/ImageReconstruction/RCAS/RCAS.cpp` | Image Reconstruction | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/ImageReconstruction/RCAS/RCAS.h` | Image Reconstruction | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/ImageReconstruction/Streamline.cpp` | Image Reconstruction | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/ImageReconstruction/Streamline.h` | Image Reconstruction | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/AmbientProbe.cpp` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/AmbientProbe.h` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/Atmosphere.cpp` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/Atmosphere.h` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/CameraSuite.cpp` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/CameraSuite.h` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/ContactShadows.cpp` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/ContactShadows.h` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/DistanceBlend.cpp` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/DistanceBlend.h` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/FoliageDynamics.cpp` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/FoliageDynamics.h` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/GroundResponse.cpp` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/GroundResponse.h` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/HorizonBlend.cpp` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/HorizonBlend.h` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/HybridGI.cpp` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/HybridGI.h` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/ImageReconstruction.cpp` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/ImageReconstruction.h` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/InteriorDaylight.cpp` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/InteriorDaylight.h` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/LightVolumes.cpp` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/LightVolumes.h` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/LinearLightCore.cpp` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/LinearLightCore.h` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/MaterialLayers.cpp` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/MaterialLayers.h` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/NaturalLighting.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/NaturalLighting.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/OverlayFeature.h` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/PixelCapture.cpp` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/PixelCapture.h` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/PulseProfiler.cpp` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/PulseProfiler.h` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/RadiantGrid.cpp` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/RadiantGrid.h` | Module Infrastructure | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/RainResponse.cpp` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/RainResponse.h` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/SkinOptics.cpp` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/SkinOptics.h` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/SkyBounce.cpp` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/SkyBounce.h` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/SkyContinuity.cpp` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/SkyContinuity.h` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/SkyVeil.cpp` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/SkyVeil.h` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/StrandShading.cpp` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/StrandShading.h` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/TerrainDetail.cpp` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/TerrainDetail.h` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/TerrainField.cpp` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/TerrainField.h` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/TerrainOcclusion.cpp` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/TerrainOcclusion.h` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/TerrainSeam.cpp` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/TerrainSeam.h` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/ThinSurface.cpp` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/ThinSurface.h` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/TissueDiffusion.cpp` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/TissueDiffusion.h` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/VolumeOcclusion.cpp` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/VolumeOcclusion.h` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/Waterbody.cpp` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/Waterbody.h` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/WaterOptics.cpp` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/WaterOptics.h` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/WindowLife.cpp` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/WindowLife.h` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/WorldProbes.cpp` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/WorldProbes.h` | Module Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/NaturalLighting/Common.h` | Natural Lighting | YES | YES | NO | YES | YES | YES | YES |
| `engine/Modules/PulseProfiler/ABTesting/ABTestAggregator.cpp` | Pulse Profiler | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/PulseProfiler/ABTesting/ABTestAggregator.h` | Pulse Profiler | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/PulseProfiler/ABTesting/ABTesting.cpp` | Pulse Profiler | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/PulseProfiler/ABTesting/ABTesting.h` | Pulse Profiler | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/Waterbody/Flowmap.cpp` | Waterbody | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/Waterbody/Flowmap.h` | Waterbody | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/Waterbody/WaterCache.cpp` | Waterbody | YES | NO | NO | NO | NO | NO | NO |
| `engine/Modules/Waterbody/WaterCache.h` | Waterbody | YES | NO | NO | NO | NO | NO | NO |
| `engine/EngineFix.cpp` | Runtime Hooks | YES | NO | NO | NO | NO | NO | NO |
| `engine/EngineFix.h` | Runtime Hooks | YES | NO | NO | NO | NO | NO | NO |
| `engine/EngineFixes/EffectShaderNoDecalsFix.cpp` | Runtime Hooks | YES | NO | NO | NO | NO | NO | NO |
| `engine/EngineFixes/EffectShaderNoDecalsFix.h` | Runtime Hooks | YES | NO | NO | NO | NO | NO | NO |
| `engine/EngineFixes/ShadowmapCascadeCullingFix.cpp` | Runtime Hooks | YES | NO | NO | NO | NO | NO | NO |
| `engine/EngineFixes/ShadowmapCascadeCullingFix.h` | Runtime Hooks | YES | NO | NO | NO | NO | NO | NO |
| `engine/EngineFixes/ShadowmapCascadeRasterizerFix.cpp` | Runtime Hooks | YES | NO | NO | NO | NO | NO | NO |
| `engine/EngineFixes/ShadowmapCascadeRasterizerFix.h` | Runtime Hooks | YES | NO | NO | NO | NO | NO | NO |
| `engine/Hooks.cpp` | Runtime Hooks | YES | NO | NO | NO | NO | NO | NO |
| `engine/Hooks.h` | Runtime Hooks | YES | NO | NO | NO | NO | NO | NO |
| `include/DynamicWetness_PublicAPI.h` | Public/Interop Headers | YES | NO | NO | NO | NO | NO | NO |
| `include/ENB/AntTweakBar.h` | Public/Interop Headers | YES | NO | NO | NO | NO | NO | NO |
| `include/ENB/ENBSeriesAPI.h` | Public/Interop Headers | YES | NO | NO | NO | NO | NO | NO |
| `include/ENB/ENBSeriesSDK.h` | Public/Interop Headers | YES | NO | NO | NO | NO | NO | NO |
| `include/FidelityFX/api/include/dx12/ffx_api_dx12.h` | Public/Interop Headers | YES | NO | NO | NO | NO | NO | NO |
| `include/FidelityFX/api/include/dx12/ffx_api_dx12.hpp` | Public/Interop Headers | YES | NO | NO | NO | NO | NO | NO |
| `include/FidelityFX/api/include/ffx_api.h` | Public/Interop Headers | YES | NO | NO | NO | NO | NO | NO |
| `include/FidelityFX/api/include/ffx_api.hpp` | Public/Interop Headers | YES | NO | NO | NO | NO | NO | NO |
| `include/FidelityFX/api/include/ffx_api_loader.h` | Public/Interop Headers | YES | NO | NO | NO | NO | NO | NO |
| `include/FidelityFX/api/include/ffx_api_types.h` | Public/Interop Headers | YES | NO | NO | NO | NO | NO | NO |
| `include/FidelityFX/api/include/ffx_api-helper.h` | Public/Interop Headers | YES | NO | NO | NO | NO | NO | NO |
| `include/FidelityFX/framegeneration/include/dx12/ffx_api_framegeneration_dx12.h` | Public/Interop Headers | YES | NO | NO | NO | NO | NO | NO |
| `include/FidelityFX/framegeneration/include/dx12/ffx_api_framegeneration_dx12.hpp` | Public/Interop Headers | YES | NO | NO | NO | NO | NO | NO |
| `include/FidelityFX/framegeneration/include/ffx_framegeneration.h` | Public/Interop Headers | YES | NO | NO | NO | NO | NO | NO |
| `include/FidelityFX/framegeneration/include/ffx_framegeneration.hpp` | Public/Interop Headers | YES | NO | NO | NO | NO | NO | NO |
| `include/FidelityFX/framegeneration/include/ffx_framegeneration_api_types.h` | Public/Interop Headers | YES | NO | NO | NO | NO | NO | NO |
| `include/FrameAnnotations.h` | Public/Interop Headers | YES | NO | NO | NO | NO | NO | NO |
| `include/PCH.h` | Public/Interop Headers | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Radiant Grid/Assets/LightProfiles/black.ini` | Radiant Grid | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Radiant Grid/Assets/LightProfiles/enblightglow.ini` | Radiant Grid | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Radiant Grid/Assets/LightProfiles/fxglowenb.ini` | Radiant Grid | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Radiant Grid/Assets/LightProfiles/glowsoft01_enbl.ini` | Radiant Grid | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Radiant Grid/Assets/LightProfiles/po3_fullalpha.ini` | Radiant Grid | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Terrain Field/Assets/Plugin/PIXL-TerrainField.esp` | Terrain Field | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/Blackreach.Terrain.HeightMap.-7.-8.7.12.-284.861.dds` | Terrain Occlusion | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/ccBGSSSE067DeadlandsWorld.Terrain.HeightMap.-6.-4.4.7.-140.353.dds` | Terrain Occlusion | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/ccKRTSSE001QNWorld.Terrain.HeightMap.-3.-4.2.2.-972.265.dds` | Terrain Occlusion | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/DeepwoodRedoubtWorld.Terrain.HeightMap.-39.15.-27.24.-618.472.dds` | Terrain Occlusion | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/DLC01FalmerValley.Terrain.HeightMap.-16.-13.10.13.-530.2212.dds` | Terrain Occlusion | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/DLC01SoulCairn.Terrain.HeightMap.-52.-43.30.44.-188.245.dds` | Terrain Occlusion | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/DLC1HunterHQWorld.Terrain.HeightMap.-31.-8.3.5.-662.308.dds` | Terrain Occlusion | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/DLC2ApocryphaWorld.Terrain.HeightMap.-6.-1.7.9.-250.-206.dds` | Terrain Occlusion | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/DLC2SolstheimWorld.Terrain.HeightMap.-64.-64.127.127.-1024.6342.dds` | Terrain Occlusion | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/FORMAT.md` | Terrain Occlusion | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/JaphetsFollyWorld.Terrain.HeightMap.-7.-6.6.6.-210.163.dds` | Terrain Occlusion | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/MarkarthWorld.Terrain.HeightMap.-48.-3.-31.5.-3375.2609.dds` | Terrain Occlusion | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/SkuldafnWorld.Terrain.HeightMap.40.-21.62.-1.-285.1876.dds` | Terrain Occlusion | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/Sovngarde.Terrain.HeightMap.-23.-32.34.40.-1331.4715.dds` | Terrain Occlusion | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Terrain Occlusion/Assets/HeightMaps/Tamriel.Terrain.HeightMap.-57.-43.61.50.-4629.4924.dds` | Terrain Occlusion | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Waterbody/Assets/Meshes/Water/OptimisedWaterMesh.nif` | Waterbody | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Waterbody/Assets/Meshes/Water/WaterMesh.nif` | Waterbody | YES | NO | NO | NO | NO | NO | NO |
| `pipeline/Waterbody/Assets/WorldWater/Tamriel_precache.wpc` | Waterbody | YES | NO | NO | NO | NO | NO | NO |
| `engine/Deferred.cpp` | Shader Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/Deferred.h` | Shader Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/PipelineBuffer.cpp` | Shader Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/PipelineBuffer.h` | Shader Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/PipelineHealth.cpp` | Shader Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/PipelineHealth.h` | Shader Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/ShaderCache.cpp` | Shader Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/ShaderCache.h` | Shader Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/ShaderFileWatcher.h` | Shader Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/ShaderTools/BSShader.h` | Shader Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/ShaderTools/BSShaderHooks.cpp` | Shader Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/ShaderTools/BSShaderHooks.h` | Shader Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/ShaderTools/ShaderCompiler.cpp` | Shader Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `engine/ShaderTools/ShaderCompiler.h` | Shader Infrastructure | YES | NO | NO | NO | NO | NO | NO |
| `extern/CommonLibSSE-NG@8f4205da56f01cbe98557422c008a5719c180fb9` | CommonLibSSE-NG | YES | NO | NO | NO | NO | NO | NO |
| `extern/FidelityFX-SDK@054f0ade7bd443710644ecb2564936281821f532` | FidelityFX SDK | YES | NO | NO | NO | NO | NO | NO |
| `extern/Streamline-DX12@a9ed1f58436864891f68b0458300464dc53d9a69` | NVIDIA Streamline | YES | NO | NO | NO | NO | NO | NO |
| `engine/Menu.cpp` | UI | YES | NO | NO | NO | NO | NO | NO |
| `engine/Menu.h` | UI | YES | NO | NO | NO | NO | NO | NO |
| `engine/Menu/BackgroundBlur.cpp` | UI | YES | NO | NO | NO | NO | NO | NO |
| `engine/Menu/BackgroundBlur.h` | UI | YES | NO | NO | NO | NO | NO | NO |
| `engine/Menu/CursorLoader.cpp` | UI | YES | NO | NO | NO | NO | NO | NO |
| `engine/Menu/CursorLoader.h` | UI | YES | NO | NO | NO | NO | NO | NO |
| `engine/Menu/Fonts.cpp` | UI | YES | NO | NO | NO | NO | NO | NO |
| `engine/Menu/Fonts.h` | UI | YES | NO | NO | NO | NO | NO | NO |
| `engine/Menu/FontSelector.cpp` | UI | YES | NO | NO | NO | NO | NO | NO |
| `engine/Menu/FontSelector.h` | UI | YES | NO | NO | NO | NO | NO | NO |
| `engine/Menu/IconLoader.cpp` | UI | YES | NO | NO | NO | NO | NO | NO |
| `engine/Menu/IconLoader.h` | UI | YES | NO | NO | NO | NO | NO | NO |
| `engine/Menu/LaunchExperienceRenderer.cpp` | UI | YES | NO | NO | NO | NO | NO | NO |
| `engine/Menu/LaunchExperienceRenderer.h` | UI | YES | NO | NO | NO | NO | NO | NO |
| `engine/Menu/OverlayRenderer.cpp` | UI | YES | NO | NO | NO | NO | NO | NO |
| `engine/Menu/OverlayRenderer.h` | UI | YES | NO | NO | NO | NO | NO | NO |
| `engine/Menu/PIXLRendererPage.cpp` | UI | YES | NO | NO | NO | NO | NO | NO |
| `engine/Menu/PIXLRendererPage.h` | UI | YES | NO | NO | NO | NO | NO | NO |
| `engine/Menu/PIXLStyle.h` | UI | YES | NO | NO | NO | NO | NO | NO |
| `engine/Menu/PulsePanelRenderer.cpp` | UI | YES | NO | NO | NO | NO | NO | NO |
| `engine/Menu/PulsePanelRenderer.h` | UI | YES | NO | NO | NO | NO | NO | NO |
| `engine/Menu/RuntimeSettingsRenderer.cpp` | UI | YES | NO | NO | NO | NO | NO | NO |
| `engine/Menu/RuntimeSettingsRenderer.h` | UI | YES | NO | NO | NO | NO | NO | NO |
| `engine/Menu/ThemeManager.cpp` | UI | YES | NO | NO | NO | NO | NO | NO |
| `engine/Menu/ThemeManager.h` | UI | YES | NO | NO | NO | NO | NO | NO |
| `engine/Menu/TuningWorkspaceRenderer.cpp` | UI | YES | NO | NO | NO | NO | NO | NO |
| `engine/Menu/TuningWorkspaceRenderer.h` | UI | YES | NO | NO | NO | NO | NO | NO |
| `engine/Menu/WorkshopToolsRenderer.cpp` | UI | YES | NO | NO | NO | NO | NO | NO |
| `engine/Menu/WorkshopToolsRenderer.h` | UI | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Interface/PIXLRenderer/Fonts/Jost/Jost-Light.ttf` | UI Assets | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Interface/PIXLRenderer/Fonts/Jost/Jost-Regular.ttf` | UI Assets | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Interface/PIXLRenderer/Fonts/Jost/Jost-SemiBold.ttf` | UI Assets | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Interface/PIXLRenderer/Fonts/Jost/OFL.txt` | UI Assets | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Interface/PIXLRenderer/Fonts/Sanguis/Sanguis OFL License.txt` | UI Assets | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Interface/PIXLRenderer/Fonts/Sanguis/Sanguis.ttf` | UI Assets | YES | NO | NO | NO | NO | NO | NO |
| `distribution/Interface/PIXLRenderer/Visuals/Brand/PIXL-Mark.png` | UI Assets | YES | NO | NO | NO | NO | NO | NO |
| `distribution/SKSE/Plugins/PIXLRenderer/QualityPreviews/README.txt` | UI Assets | YES | NO | NO | NO | NO | NO | NO |
| `engine/I18n/I18n.cpp` | Localization | YES | NO | NO | NO | NO | NO | NO |
| `engine/I18n/I18n.h` | Localization | YES | NO | NO | NO | NO | NO | NO |
| `engine/Renderer/QualityProfiles.cpp` | Quality Profiles | YES | NO | NO | NO | NO | NO | NO |
| `engine/Renderer/QualityProfiles.h` | Quality Profiles | YES | NO | NO | NO | NO | NO | NO |
