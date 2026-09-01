# PIXL preset differences

Only settings that differ from the untouched live baseline are listed.

## PIXL_Complete

| Setting | Baseline | New Value | Module | Reason |
| --- | --- | --- | --- | --- |
| `Actor Surface Effects.EffectDistance` | `3200.0` | `4000` | Actor Surface Effects | Scales character material/SSS/surface-effect workload. |
| `Actor Surface Effects.EffectQuality` | `1` | `2` | Actor Surface Effects | Scales character material/SSS/surface-effect workload. |
| `Actor Surface Effects.MaximumAffectedNPCs` | `24` | `32` | Actor Surface Effects | Scales character material/SSS/surface-effect workload. |
| `Atmosphere.volumetricGridPixelSize` | `24` | `20` | Atmosphere | Scales volumetric froxel quality or disables optional volumetrics for Minimum. |
| `Atmosphere.volumetricGridSizeZ` | `40` | `52` | Atmosphere | Scales volumetric froxel quality or disables optional volumetrics for Minimum. |
| `Atmosphere.volumetricHistoryMissSampleCount` | `2` | `3` | Atmosphere | Scales volumetric froxel quality or disables optional volumetrics for Minimum. |
| `Camera Suite.bloomRadius` | `1.0` | `0.9` | Camera Suite | Keeps post-processing coherent with the preset goal rather than maximizing intensity. |
| `Camera Suite.bloomStrength` | `0.38777589797973633` | `0.3` | Camera Suite | Keeps post-processing coherent with the preset goal rather than maximizing intensity. |
| `Camera Suite.bloomThreshold` | `0.46825048327445984` | `0.62` | Camera Suite | Keeps post-processing coherent with the preset goal rather than maximizing intensity. |
| `Contact Shadows.SampleCount` | `1` | `2` | Contact Shadows | Applies the renderer-owned Lighting quality/stability contract. |
| `Ground Response.GeometryFadeStart` | `2150.0` | `2850` | Ground Response | Scales or safely gates geometric ground simulation and tessellation. |
| `Ground Response.GeometryRenderDistance` | `2600.0` | `3400` | Ground Response | Scales or safely gates geometric ground simulation and tessellation. |
| `Ground Response.GeometryTessellationFar` | `2.0` | `2.5` | Ground Response | Scales or safely gates geometric ground simulation and tessellation. |
| `Ground Response.GeometryTessellationFarDistance` | `1250.0` | `1600` | Ground Response | Scales or safely gates geometric ground simulation and tessellation. |
| `Ground Response.GeometryTessellationNear` | `7.0` | `10` | Ground Response | Scales or safely gates geometric ground simulation and tessellation. |
| `Ground Response.GeometryTessellationNearDistance` | `500.0` | `600` | Ground Response | Scales or safely gates geometric ground simulation and tessellation. |
| `Hybrid GI.BlurRadius` | `2.5` | `2.2` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.NumSlices` | `4` | `5` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.NumSteps` | `8` | `10` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.RadianceFireflyClamp` | `5.0` | `6` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.ReflectionFireflyClamp` | `6.0` | `8` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.ReflectionSteps` | `24` | `32` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.WorldCacheInjectionStride` | `4` | `3` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.WorldCacheSampleCount` | `4` | `6` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.WorldCacheTemporalResponse` | `0.10000000149011612` | `0.12` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.WorldCacheTraceSteps` | `3` | `4` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `ImageReconstruction.presetDLSS` | `0` | `5` | Image Reconstruction | Selects the profile reconstruction strategy without enabling frame generation. |
| `ImageReconstruction.qualityMode` | `1` | `0` | Image Reconstruction | Selects the profile reconstruction strategy without enabling frame generation. |
| `ImageReconstruction.sharpnessEnabledDLSS` | `true` | `false` | Image Reconstruction | Selects the profile reconstruction strategy without enabling frame generation. |
| `ImageReconstruction.sharpnessFSR` | `0.20000000298023224` | `0` | Image Reconstruction | Selects the profile reconstruction strategy without enabling frame generation. |
| `ImageReconstruction.upscaleMethod` | `1` | `3` | Image Reconstruction | Selects the profile reconstruction strategy without enabling frame generation. |
| `Light Volumes.ExteriorQuality` | `1` | `2` | Light Volumes | Applies the renderer-owned Lighting quality/stability contract. |
| `Light Volumes.InteriorQuality` | `1` | `2` | Light Volumes | Applies the renderer-owned Lighting quality/stability contract. |
| `Material Forge.GGXMultiScatterStrength` | `0.75` | `1` | Material Forge | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Forge.SpecularAAStrength` | `0.75` | `1` | Material Forge | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Layers.EnableHeightBlending` | `0` | `1` | Material Layers | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Layers.PIXL Tuning.DetailQuality` | `1` | `2` | Material Layers | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Layers.PIXL Tuning.ObjectMaxSteps` | `12` | `18` | Material Layers | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Layers.PIXL Tuning.ObjectNearSteps` | `6` | `9` | Material Layers | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Layers.PIXL Tuning.ObjectRefinementSteps` | `4` | `6` | Material Layers | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Layers.PIXL Tuning.TerrainMaxSteps` | `14` | `22` | Material Layers | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Layers.PIXL Tuning.TerrainNearSteps` | `6` | `8` | Material Layers | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Layers.PIXL Tuning.TerrainRefinementSteps` | `4` | `6` | Material Layers | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Menu.AtmosphereQuality` | `1` | `2` | Quality/UI | Keeps the visible quality contract synchronized with the module values. |
| `Menu.CameraQuality` | `1` | `2` | Quality/UI | Keeps the visible quality contract synchronized with the module values. |
| `Menu.CharactersQuality` | `1` | `2` | Quality/UI | Keeps the visible quality contract synchronized with the module values. |
| `Menu.LightingQuality` | `1` | `2` | Quality/UI | Keeps the visible quality contract synchronized with the module values. |
| `Menu.MaterialsQuality` | `1` | `2` | Quality/UI | Keeps the visible quality contract synchronized with the module values. |
| `Menu.RendererQuality` | `1` | `2` | Quality/UI | Keeps the visible quality contract synchronized with the module values. |
| `Menu.TerrainVegetationQuality` | `1` | `2` | Quality/UI | Keeps the visible quality contract synchronized with the module values. |
| `Menu.WaterQuality` | `1` | `2` | Quality/UI | Keeps the visible quality contract synchronized with the module values. |
| `Strand Shading.HairMode` | `0` | `1` | Strand Shading | Scales character material/SSS/surface-effect workload. |
| `Tissue Diffusion.BurleySamples` | `12` | `16` | Tissue Diffusion | Scales character material/SSS/surface-effect workload. |
| `Water Optics.CausticsDispersion` | `0.44999998807907104` | `0.65` | Water Optics | Scales the real SSR/caustic workload and stability controls. |
| `Water Optics.SSRDistanceScale` | `0.8600000143051147` | `1.06` | Water Optics | Scales the real SSR/caustic workload and stability controls. |
| `Water Optics.SSREdgeFade` | `1.149999976158142` | `0.95` | Water Optics | Scales the real SSR/caustic workload and stability controls. |

## PIXL_Extreme_Fidelity

| Setting | Baseline | New Value | Module | Reason |
| --- | --- | --- | --- | --- |
| `Actor Surface Effects.EffectDistance` | `3200.0` | `4800` | Actor Surface Effects | Scales character material/SSS/surface-effect workload. |
| `Actor Surface Effects.EffectQuality` | `1` | `3` | Actor Surface Effects | Scales character material/SSS/surface-effect workload. |
| `Actor Surface Effects.MaximumAffectedNPCs` | `24` | `48` | Actor Surface Effects | Scales character material/SSS/surface-effect workload. |
| `Atmosphere.volumetricGridPixelSize` | `24` | `16` | Atmosphere | Scales volumetric froxel quality or disables optional volumetrics for Minimum. |
| `Atmosphere.volumetricGridSizeZ` | `40` | `64` | Atmosphere | Scales volumetric froxel quality or disables optional volumetrics for Minimum. |
| `Atmosphere.volumetricHistoryMissSampleCount` | `2` | `4` | Atmosphere | Scales volumetric froxel quality or disables optional volumetrics for Minimum. |
| `Camera Suite.bloomRadius` | `1.0` | `0.85` | Camera Suite | Keeps post-processing coherent with the preset goal rather than maximizing intensity. |
| `Camera Suite.bloomStrength` | `0.38777589797973633` | `0.28` | Camera Suite | Keeps post-processing coherent with the preset goal rather than maximizing intensity. |
| `Camera Suite.bloomThreshold` | `0.46825048327445984` | `0.68` | Camera Suite | Keeps post-processing coherent with the preset goal rather than maximizing intensity. |
| `Contact Shadows.SampleCount` | `1` | `2` | Contact Shadows | Applies the renderer-owned Lighting quality/stability contract. |
| `Ground Response.GeometryFadeStart` | `2150.0` | `3400` | Ground Response | Scales or safely gates geometric ground simulation and tessellation. |
| `Ground Response.GeometryRenderDistance` | `2600.0` | `4000` | Ground Response | Scales or safely gates geometric ground simulation and tessellation. |
| `Ground Response.GeometryTessellationFar` | `2.0` | `3` | Ground Response | Scales or safely gates geometric ground simulation and tessellation. |
| `Ground Response.GeometryTessellationFarDistance` | `1250.0` | `1895` | Ground Response | Scales or safely gates geometric ground simulation and tessellation. |
| `Ground Response.GeometryTessellationNear` | `7.0` | `14` | Ground Response | Scales or safely gates geometric ground simulation and tessellation. |
| `Ground Response.GeometryTessellationNearDistance` | `500.0` | `699` | Ground Response | Scales or safely gates geometric ground simulation and tessellation. |
| `Hybrid GI.BlurRadius` | `2.5` | `2` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.MaxAccumFrames` | `20` | `18` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.NumSlices` | `4` | `6` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.NumSteps` | `8` | `12` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.RadianceFireflyClamp` | `5.0` | `8` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.ReflectionFireflyClamp` | `6.0` | `10` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.ReflectionSteps` | `24` | `48` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.WorldCacheInjectionStride` | `4` | `2` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.WorldCacheSampleCount` | `4` | `8` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.WorldCacheTemporalResponse` | `0.10000000149011612` | `0.14` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.WorldCacheTraceSteps` | `3` | `4` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `ImageReconstruction.presetDLSS` | `0` | `5` | Image Reconstruction | Selects the profile reconstruction strategy without enabling frame generation. |
| `ImageReconstruction.qualityMode` | `1` | `0` | Image Reconstruction | Selects the profile reconstruction strategy without enabling frame generation. |
| `ImageReconstruction.sharpnessEnabledDLSS` | `true` | `false` | Image Reconstruction | Selects the profile reconstruction strategy without enabling frame generation. |
| `ImageReconstruction.sharpnessFSR` | `0.20000000298023224` | `0` | Image Reconstruction | Selects the profile reconstruction strategy without enabling frame generation. |
| `ImageReconstruction.upscaleMethod` | `1` | `3` | Image Reconstruction | Selects the profile reconstruction strategy without enabling frame generation. |
| `Light Volumes.ExteriorQuality` | `1` | `2` | Light Volumes | Applies the renderer-owned Lighting quality/stability contract. |
| `Light Volumes.InteriorQuality` | `1` | `2` | Light Volumes | Applies the renderer-owned Lighting quality/stability contract. |
| `Material Forge.GGXMultiScatterStrength` | `0.75` | `1` | Material Forge | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Forge.LocalContactShadowLightCount` | `1` | `2` | Material Forge | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Forge.SpecularAAStrength` | `0.75` | `1.34` | Material Forge | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Layers.EnableHeightBlending` | `0` | `1` | Material Layers | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Layers.PIXL Tuning.DetailQuality` | `1` | `2` | Material Layers | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Layers.PIXL Tuning.ObjectMaxSteps` | `12` | `24` | Material Layers | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Layers.PIXL Tuning.ObjectNearSteps` | `6` | `12` | Material Layers | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Layers.PIXL Tuning.ObjectRefinementSteps` | `4` | `8` | Material Layers | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Layers.PIXL Tuning.TerrainMaxSteps` | `14` | `30` | Material Layers | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Layers.PIXL Tuning.TerrainNearSteps` | `6` | `10` | Material Layers | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Layers.PIXL Tuning.TerrainRefinementSteps` | `4` | `8` | Material Layers | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Menu.AtmosphereQuality` | `1` | `3` | Quality/UI | Keeps the visible quality contract synchronized with the module values. |
| `Menu.CameraQuality` | `1` | `3` | Quality/UI | Keeps the visible quality contract synchronized with the module values. |
| `Menu.CharactersQuality` | `1` | `3` | Quality/UI | Keeps the visible quality contract synchronized with the module values. |
| `Menu.LightingQuality` | `1` | `3` | Quality/UI | Keeps the visible quality contract synchronized with the module values. |
| `Menu.MaterialsQuality` | `1` | `3` | Quality/UI | Keeps the visible quality contract synchronized with the module values. |
| `Menu.RendererQuality` | `1` | `3` | Quality/UI | Keeps the visible quality contract synchronized with the module values. |
| `Menu.TerrainVegetationQuality` | `1` | `3` | Quality/UI | Keeps the visible quality contract synchronized with the module values. |
| `Menu.WaterQuality` | `1` | `3` | Quality/UI | Keeps the visible quality contract synchronized with the module values. |
| `Pixel Capture.PhotoFinishDetailStrength` | `0.6499999761581421` | `0.72` | Pixel Capture | Raises offline screenshot reconstruction only; normal gameplay cost is unchanged. |
| `Pixel Capture.PhotoFinishQualityPreset` | `2` | `3` | Pixel Capture | Raises offline screenshot reconstruction only; normal gameplay cost is unchanged. |
| `Pixel Capture.PhotoFinishScale` | `2` | `4` | Pixel Capture | Raises offline screenshot reconstruction only; normal gameplay cost is unchanged. |
| `Pixel Capture.PhotoFinishTemporalSamples` | `16` | `24` | Pixel Capture | Raises offline screenshot reconstruction only; normal gameplay cost is unchanged. |
| `Strand Shading.HairMode` | `0` | `1` | Strand Shading | Scales character material/SSS/surface-effect workload. |
| `Tissue Diffusion.BurleySamples` | `12` | `21` | Tissue Diffusion | Scales character material/SSS/surface-effect workload. |
| `Water Optics.CausticsDispersion` | `0.44999998807907104` | `0.88` | Water Optics | Scales the real SSR/caustic workload and stability controls. |
| `Water Optics.SSRDistanceScale` | `0.8600000143051147` | `1.29` | Water Optics | Scales the real SSR/caustic workload and stability controls. |
| `Water Optics.SSREdgeFade` | `1.149999976158142` | `0.8` | Water Optics | Scales the real SSR/caustic workload and stability controls. |

## PIXL_Minimum

| Setting | Baseline | New Value | Module | Reason |
| --- | --- | --- | --- | --- |
| `Actor Surface Effects.EffectQuality` | `1` | `0` | Actor Surface Effects | Scales character material/SSS/surface-effect workload. |
| `Actor Surface Effects.Enable` | `true` | `false` | Actor Surface Effects | Scales character material/SSS/surface-effect workload. |
| `Atmosphere.useWorldProbes` | `1` | `0` | Atmosphere | Scales volumetric froxel quality or disables optional volumetrics for Minimum. |
| `Atmosphere.volumetricFogEnabled` | `1` | `0` | Atmosphere | Scales volumetric froxel quality or disables optional volumetrics for Minimum. |
| `Atmosphere.volumetricGridPixelSize` | `24` | `32` | Atmosphere | Scales volumetric froxel quality or disables optional volumetrics for Minimum. |
| `Atmosphere.volumetricGridSizeZ` | `40` | `32` | Atmosphere | Scales volumetric froxel quality or disables optional volumetrics for Minimum. |
| `Atmosphere.volumetricHistoryMissSampleCount` | `2` | `1` | Atmosphere | Scales volumetric froxel quality or disables optional volumetrics for Minimum. |
| `Camera Suite.enableBloom` | `true` | `false` | Camera Suite | Keeps post-processing coherent with the preset goal rather than maximizing intensity. |
| `Camera Suite.enableColdLens` | `true` | `false` | Camera Suite | Keeps post-processing coherent with the preset goal rather than maximizing intensity. |
| `Camera Suite.enableElementalDamageLens` | `true` | `false` | Camera Suite | Keeps post-processing coherent with the preset goal rather than maximizing intensity. |
| `Camera Suite.enableModernMotionBlur` | `true` | `false` | Camera Suite | Keeps post-processing coherent with the preset goal rather than maximizing intensity. |
| `Camera Suite.enablePhysicalCamera` | `true` | `false` | Camera Suite | Keeps post-processing coherent with the preset goal rather than maximizing intensity. |
| `Camera Suite.enableStormglass` | `true` | `false` | Camera Suite | Keeps post-processing coherent with the preset goal rather than maximizing intensity. |
| `Camera Suite.enableSubmergedOptics` | `true` | `false` | Camera Suite | Keeps post-processing coherent with the preset goal rather than maximizing intensity. |
| `Contact Shadows.Enable` | `1` | `0` | Contact Shadows | Applies the renderer-owned Lighting quality/stability contract. |
| `Foliage Dynamics.EnableEnhancedVegetation` | `1` | `0` | Foliage Dynamics | Enables or safely gates an optional high-cost visual module for this profile. |
| `Foliage Dynamics.EnableEnhancedWind` | `1` | `0` | Foliage Dynamics | Enables or safely gates an optional high-cost visual module for this profile. |
| `Foliage Dynamics.OverrideComplexGrassSettings` | `1` | `0` | Foliage Dynamics | Enables or safely gates an optional high-cost visual module for this profile. |
| `Ground Response.EnableDeformableGround` | `true` | `false` | Ground Response | Scales or safely gates geometric ground simulation and tessellation. |
| `Ground Response.EnableGeometricSnow` | `true` | `false` | Ground Response | Scales or safely gates geometric ground simulation and tessellation. |
| `Ground Response.EnableGroundResponse` | `true` | `false` | Ground Response | Scales or safely gates geometric ground simulation and tessellation. |
| `Ground Response.EnableMudDeformation` | `true` | `false` | Ground Response | Scales or safely gates geometric ground simulation and tessellation. |
| `Ground Response.EnableSnowDeformation` | `true` | `false` | Ground Response | Scales or safely gates geometric ground simulation and tessellation. |
| `Ground Response.EnableWeatherSnowAccumulation` | `true` | `false` | Ground Response | Scales or safely gates geometric ground simulation and tessellation. |
| `Ground Response.GeometryFadeStart` | `2150.0` | `1450` | Ground Response | Scales or safely gates geometric ground simulation and tessellation. |
| `Ground Response.GeometryRenderDistance` | `2600.0` | `1800` | Ground Response | Scales or safely gates geometric ground simulation and tessellation. |
| `Ground Response.GeometryTessellationFar` | `2.0` | `1.5` | Ground Response | Scales or safely gates geometric ground simulation and tessellation. |
| `Ground Response.GeometryTessellationFarDistance` | `1250.0` | `900` | Ground Response | Scales or safely gates geometric ground simulation and tessellation. |
| `Ground Response.GeometryTessellationNear` | `7.0` | `4` | Ground Response | Scales or safely gates geometric ground simulation and tessellation. |
| `Ground Response.GeometryTessellationNearDistance` | `500.0` | `400` | Ground Response | Scales or safely gates geometric ground simulation and tessellation. |
| `Hybrid GI.BlurRadius` | `2.5` | `2.8` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.Enabled` | `true` | `false` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.EnableExperimentalSpecularGI` | `true` | `false` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.EnableGI` | `true` | `false` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.EnableWorldCache` | `true` | `false` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.EnableWorldCacheSecondBounce` | `true` | `false` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.NumSlices` | `4` | `3` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.NumSteps` | `8` | `6` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.RadianceFireflyClamp` | `5.0` | `4` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.ReflectionFireflyClamp` | `6.0` | `4` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.ReflectionSteps` | `24` | `16` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.WorldCacheInjectionStride` | `4` | `6` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.WorldCacheSampleCount` | `4` | `3` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.WorldCacheTemporalResponse` | `0.10000000149011612` | `0.08` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.WorldCacheTraceSteps` | `3` | `2` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `ImageReconstruction.qualityMode` | `1` | `4` | Image Reconstruction | Selects the profile reconstruction strategy without enabling frame generation. |
| `ImageReconstruction.sharpnessEnabledDLSS` | `true` | `false` | Image Reconstruction | Selects the profile reconstruction strategy without enabling frame generation. |
| `ImageReconstruction.sharpnessFSR` | `0.20000000298023224` | `0.2` | Image Reconstruction | Selects the profile reconstruction strategy without enabling frame generation. |
| `ImageReconstruction.upscaleMethod` | `1` | `2` | Image Reconstruction | Selects the profile reconstruction strategy without enabling frame generation. |
| `Light Volumes.ExteriorEnabled` | `true` | `false` | Light Volumes | Applies the renderer-owned Lighting quality/stability contract. |
| `Light Volumes.ExteriorQuality` | `1` | `0` | Light Volumes | Applies the renderer-owned Lighting quality/stability contract. |
| `Light Volumes.InteriorEnabled` | `true` | `false` | Light Volumes | Applies the renderer-owned Lighting quality/stability contract. |
| `Light Volumes.InteriorQuality` | `1` | `0` | Light Volumes | Applies the renderer-owned Lighting quality/stability contract. |
| `Material Forge.EnableGGXMultiScatter` | `1` | `0` | Material Forge | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Forge.EnableLocalContactShadows` | `1` | `0` | Material Forge | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Forge.GGXMultiScatterStrength` | `0.75` | `0.55` | Material Forge | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Forge.SpecularAAStrength` | `0.75` | `0.5` | Material Forge | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Layers.EnableParallax` | `1` | `0` | Material Layers | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Layers.EnableShadows` | `1` | `0` | Material Layers | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Layers.PIXL Tuning.DetailQuality` | `1` | `0` | Material Layers | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Layers.PIXL Tuning.EnableAutoPOMSelfShadows` | `1` | `0` | Material Layers | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Layers.PIXL Tuning.EnableDetailReconstruction` | `1` | `0` | Material Layers | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Layers.PIXL Tuning.EnableObjectAutoPOM` | `1` | `0` | Material Layers | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Layers.PIXL Tuning.EnableTerrainAutoPOM` | `1` | `0` | Material Layers | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Layers.PIXL Tuning.EnableTerrainSelfShadows` | `1` | `0` | Material Layers | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Layers.PIXL Tuning.EnableTerrainVirtualDepth` | `1` | `0` | Material Layers | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Layers.PIXL Tuning.ObjectMaxSteps` | `12` | `8` | Material Layers | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Layers.PIXL Tuning.ObjectNearSteps` | `6` | `4` | Material Layers | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Layers.PIXL Tuning.ObjectRefinementSteps` | `4` | `2` | Material Layers | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Layers.PIXL Tuning.TerrainMaxSteps` | `14` | `8` | Material Layers | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Layers.PIXL Tuning.TerrainNearSteps` | `6` | `4` | Material Layers | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Layers.PIXL Tuning.TerrainRefinementSteps` | `4` | `2` | Material Layers | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Menu.AtmosphereQuality` | `1` | `0` | Quality/UI | Keeps the visible quality contract synchronized with the module values. |
| `Menu.CameraQuality` | `1` | `0` | Quality/UI | Keeps the visible quality contract synchronized with the module values. |
| `Menu.CharactersQuality` | `1` | `0` | Quality/UI | Keeps the visible quality contract synchronized with the module values. |
| `Menu.LightingQuality` | `1` | `0` | Quality/UI | Keeps the visible quality contract synchronized with the module values. |
| `Menu.MaterialsQuality` | `1` | `0` | Quality/UI | Keeps the visible quality contract synchronized with the module values. |
| `Menu.RendererQuality` | `1` | `0` | Quality/UI | Keeps the visible quality contract synchronized with the module values. |
| `Menu.TerrainVegetationQuality` | `1` | `0` | Quality/UI | Keeps the visible quality contract synchronized with the module values. |
| `Menu.WaterQuality` | `1` | `0` | Quality/UI | Keeps the visible quality contract synchronized with the module values. |
| `Rain Response.EnableRaindropFx` | `1` | `0` | Rain Response | Enables or safely gates an optional high-cost visual module for this profile. |
| `Rain Response.EnableRainParticleEnhancement` | `1` | `0` | Rain Response | Enables or safely gates an optional high-cost visual module for this profile. |
| `Rain Response.EnableRainResponse` | `1` | `0` | Rain Response | Enables or safely gates an optional high-cost visual module for this profile. |
| `Rain Response.EnableRipples` | `1` | `0` | Rain Response | Enables or safely gates an optional high-cost visual module for this profile. |
| `Rain Response.EnableSplashes` | `1` | `0` | Rain Response | Enables or safely gates an optional high-cost visual module for this profile. |
| `Rain Response.SnowPrecipitation.Enable` | `true` | `false` | Rain Response | Enables or safely gates an optional high-cost visual module for this profile. |
| `Skin Optics.EnableSkin` | `true` | `false` | Skin Optics | Scales character material/SSS/surface-effect workload. |
| `Skin Optics.EnableSkinDetail` | `true` | `false` | Skin Optics | Scales character material/SSS/surface-effect workload. |
| `Skin Optics.UseSSS` | `true` | `false` | Skin Optics | Scales character material/SSS/surface-effect workload. |
| `Sky Veil.EnableVolumetricClouds` | `1` | `0` | Sky Veil | Enables or safely gates an optional high-cost visual module for this profile. |
| `Strand Shading.Enabled` | `1` | `0` | Strand Shading | Scales character material/SSS/surface-effect workload. |
| `Strand Shading.EnableSelfShadow` | `1` | `0` | Strand Shading | Scales character material/SSS/surface-effect workload. |
| `Tissue Diffusion.BurleySamples` | `12` | `8` | Tissue Diffusion | Scales character material/SSS/surface-effect workload. |
| `Tissue Diffusion.EnableCharacterLighting` | `1` | `0` | Tissue Diffusion | Scales character material/SSS/surface-effect workload. |
| `Water Optics.CausticsDispersion` | `0.44999998807907104` | `0.25` | Water Optics | Scales the real SSR/caustic workload and stability controls. |
| `Water Optics.EnableEnhancedCaustics` | `1` | `0` | Water Optics | Scales the real SSR/caustic workload and stability controls. |
| `Water Optics.EnableEnhancedSSR` | `1` | `0` | Water Optics | Scales the real SSR/caustic workload and stability controls. |
| `Water Optics.SSRDistanceScale` | `0.8600000143051147` | `0.65` | Water Optics | Scales the real SSR/caustic workload and stability controls. |
| `Water Optics.SSREdgeFade` | `1.149999976158142` | `1.35` | Water Optics | Scales the real SSR/caustic workload and stability controls. |
| `Window Life.EnableWindowLife` | `true` | `false` | WindowLife | Enables or safely gates an optional high-cost visual module for this profile. |
| `World Probes.EnabledSSR` | `1` | `0` | World Probes | Intentional PIXL_Minimum profile adjustment. |

## PIXL_Linear_Diffuse

| Setting | Baseline | New Value | Module | Reason |
| --- | --- | --- | --- | --- |
| `Actor Surface Effects.EffectQuality` | `1` | `2` | Actor Surface Effects | Scales character material/SSS/surface-effect workload. |
| `Ambient Probe.EnvironmentProbeScale` | `1.2200000286102295` | `1.05` | Ambient Probe | Intentional PIXL_Linear_Diffuse profile adjustment. |
| `Ambient Probe.SkyProbeScale` | `1.3700000047683716` | `1.15` | Ambient Probe | Intentional PIXL_Linear_Diffuse profile adjustment. |
| `Atmosphere.volumetricGridPixelSize` | `24` | `20` | Atmosphere | Scales volumetric froxel quality or disables optional volumetrics for Minimum. |
| `Atmosphere.volumetricGridSizeZ` | `40` | `52` | Atmosphere | Scales volumetric froxel quality or disables optional volumetrics for Minimum. |
| `Atmosphere.volumetricHistoryMissSampleCount` | `2` | `3` | Atmosphere | Scales volumetric froxel quality or disables optional volumetrics for Minimum. |
| `Camera Suite.bloomRadius` | `1.0` | `0.85` | Camera Suite | Keeps post-processing coherent with the preset goal rather than maximizing intensity. |
| `Camera Suite.bloomStrength` | `0.38777589797973633` | `0.25` | Camera Suite | Keeps post-processing coherent with the preset goal rather than maximizing intensity. |
| `Camera Suite.bloomThreshold` | `0.46825048327445984` | `0.7` | Camera Suite | Keeps post-processing coherent with the preset goal rather than maximizing intensity. |
| `Camera Suite.cameraExposureCompensationEV` | `-4.0` | `-2.6` | Camera Suite | Keeps post-processing coherent with the preset goal rather than maximizing intensity. |
| `Camera Suite.cameraHighlightProtection` | `0.8030648827552795` | `0.9` | Camera Suite | Keeps post-processing coherent with the preset goal rather than maximizing intensity. |
| `Camera Suite.cameraLocalExposure` | `0.10000000149011612` | `0.15` | Camera Suite | Keeps post-processing coherent with the preset goal rather than maximizing intensity. |
| `Camera Suite.cameraMaxExposureEV` | `2.0` | `3` | Camera Suite | Keeps post-processing coherent with the preset goal rather than maximizing intensity. |
| `Camera Suite.cameraMinExposureEV` | `-7.0` | `-6` | Camera Suite | Keeps post-processing coherent with the preset goal rather than maximizing intensity. |
| `Camera Suite.cameraShadowDetail` | `0.14781944453716278` | `0.25` | Camera Suite | Keeps post-processing coherent with the preset goal rather than maximizing intensity. |
| `Contact Shadows.SampleCount` | `1` | `2` | Contact Shadows | Applies the renderer-owned Lighting quality/stability contract. |
| `Contact Shadows.ShadowContrast` | `4.0` | `2.5` | Contact Shadows | Applies the renderer-owned Lighting quality/stability contract. |
| `Ground Response.GeometryFadeStart` | `2150.0` | `2850` | Ground Response | Scales or safely gates geometric ground simulation and tessellation. |
| `Ground Response.GeometryRenderDistance` | `2600.0` | `3400` | Ground Response | Scales or safely gates geometric ground simulation and tessellation. |
| `Ground Response.GeometryTessellationFar` | `2.0` | `2.5` | Ground Response | Scales or safely gates geometric ground simulation and tessellation. |
| `Ground Response.GeometryTessellationFarDistance` | `1250.0` | `1600` | Ground Response | Scales or safely gates geometric ground simulation and tessellation. |
| `Ground Response.GeometryTessellationNear` | `7.0` | `10` | Ground Response | Scales or safely gates geometric ground simulation and tessellation. |
| `Ground Response.GeometryTessellationNearDistance` | `500.0` | `600` | Ground Response | Scales or safely gates geometric ground simulation and tessellation. |
| `Hybrid GI.AOPower` | `1.2999999523162842` | `1.1` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.BlurRadius` | `2.5` | `2.2` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.GISaturation` | `0.703000009059906` | `0.75` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.GIStrength` | `1.309999942779541` | `1.05` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.NumSlices` | `4` | `5` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.NumSteps` | `8` | `10` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.RadianceFireflyClamp` | `5.0` | `6` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.ReflectionFireflyClamp` | `6.0` | `8` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.ReflectionSteps` | `24` | `32` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.WorldCacheInjectionStride` | `4` | `3` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.WorldCacheSampleCount` | `4` | `6` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.WorldCacheTemporalResponse` | `0.10000000149011612` | `0.12` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Hybrid GI.WorldCacheTraceSteps` | `3` | `4` | Hybrid GI / Radiance Weave | Applies the renderer-owned Lighting quality/stability contract. |
| `Light Volumes.ExteriorQuality` | `1` | `2` | Light Volumes | Applies the renderer-owned Lighting quality/stability contract. |
| `Light Volumes.InteriorQuality` | `1` | `2` | Light Volumes | Applies the renderer-owned Lighting quality/stability contract. |
| `Linear Light Core.ambientGamma` | `0.9100000262260437` | `1.1` | Linear Light Core | Calibrates the global gamma/energy conversion as one coherent linear-lighting stack. |
| `Linear Light Core.ambientMult` | `1.7000000476837158` | `1.35` | Linear Light Core | Calibrates the global gamma/energy conversion as one coherent linear-lighting stack. |
| `Linear Light Core.bloodEffectMult` | `1.5800000429153442` | `1` | Linear Light Core | Calibrates the global gamma/energy conversion as one coherent linear-lighting stack. |
| `Linear Light Core.colorGamma` | `0.949999988079071` | `1.35` | Linear Light Core | Calibrates the global gamma/energy conversion as one coherent linear-lighting stack. |
| `Linear Light Core.deferredEffectMult` | `1.5800000429153442` | `1` | Linear Light Core | Calibrates the global gamma/energy conversion as one coherent linear-lighting stack. |
| `Linear Light Core.directionalLightMult` | `1.7799999713897705` | `1.1` | Linear Light Core | Calibrates the global gamma/energy conversion as one coherent linear-lighting stack. |
| `Linear Light Core.effectAlphaGamma` | `0.9100000262260437` | `1.1` | Linear Light Core | Calibrates the global gamma/energy conversion as one coherent linear-lighting stack. |
| `Linear Light Core.effectGamma` | `0.8999999761581421` | `1.1` | Linear Light Core | Calibrates the global gamma/energy conversion as one coherent linear-lighting stack. |
| `Linear Light Core.effectLightingMult` | `1.1399999856948853` | `1` | Linear Light Core | Calibrates the global gamma/energy conversion as one coherent linear-lighting stack. |
| `Linear Light Core.emitColorGamma` | `0.9200000166893005` | `1.1` | Linear Light Core | Calibrates the global gamma/energy conversion as one coherent linear-lighting stack. |
| `Linear Light Core.emitColorMult` | `0.9900000095367432` | `1` | Linear Light Core | Calibrates the global gamma/energy conversion as one coherent linear-lighting stack. |
| `Linear Light Core.enableLinearLightCore` | `0` | `1` | Linear Light Core | Calibrates the global gamma/energy conversion as one coherent linear-lighting stack. |
| `Linear Light Core.fogAlphaGamma` | `1.3300000429153442` | `1.2` | Linear Light Core | Calibrates the global gamma/energy conversion as one coherent linear-lighting stack. |
| `Linear Light Core.fogGamma` | `1.100000023841858` | `1.1` | Linear Light Core | Calibrates the global gamma/energy conversion as one coherent linear-lighting stack. |
| `Linear Light Core.glowmapGamma` | `0.9100000262260437` | `1.1` | Linear Light Core | Calibrates the global gamma/energy conversion as one coherent linear-lighting stack. |
| `Linear Light Core.glowmapMult` | `1.2899999618530273` | `0.85` | Linear Light Core | Calibrates the global gamma/energy conversion as one coherent linear-lighting stack. |
| `Linear Light Core.lightGamma` | `1.0299999713897705` | `1.15` | Linear Light Core | Calibrates the global gamma/energy conversion as one coherent linear-lighting stack. |
| `Linear Light Core.membraneEffectMult` | `1.4700000286102295` | `1` | Linear Light Core | Calibrates the global gamma/energy conversion as one coherent linear-lighting stack. |
| `Linear Light Core.otherEffectMult` | `1.600000023841858` | `1` | Linear Light Core | Calibrates the global gamma/energy conversion as one coherent linear-lighting stack. |
| `Linear Light Core.pointLightMult` | `1.100000023841858` | `1.1` | Linear Light Core | Calibrates the global gamma/energy conversion as one coherent linear-lighting stack. |
| `Linear Light Core.projectedEffectMult` | `1.5099999904632568` | `1` | Linear Light Core | Calibrates the global gamma/energy conversion as one coherent linear-lighting stack. |
| `Linear Light Core.skyGamma` | `1.2000000476837158` | `1.1` | Linear Light Core | Calibrates the global gamma/energy conversion as one coherent linear-lighting stack. |
| `Linear Light Core.vanillaDiffuseColorMult` | `1.0099999904632568` | `1.05` | Linear Light Core | Calibrates the global gamma/energy conversion as one coherent linear-lighting stack. |
| `Linear Light Core.vlGamma` | `1.190000057220459` | `1.1` | Linear Light Core | Calibrates the global gamma/energy conversion as one coherent linear-lighting stack. |
| `Linear Light Core.waterGamma` | `1.149999976158142` | `1.1` | Linear Light Core | Calibrates the global gamma/energy conversion as one coherent linear-lighting stack. |
| `Material Forge.GGXMultiScatterStrength` | `0.75` | `1` | Material Forge | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Forge.SpecularAAStrength` | `0.75` | `1` | Material Forge | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Layers.EnableHeightBlending` | `0` | `1` | Material Layers | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Layers.PIXL Tuning.DetailQuality` | `1` | `2` | Material Layers | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Layers.PIXL Tuning.ObjectMaxSteps` | `12` | `18` | Material Layers | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Layers.PIXL Tuning.ObjectNearSteps` | `6` | `9` | Material Layers | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Layers.PIXL Tuning.ObjectRefinementSteps` | `4` | `6` | Material Layers | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Layers.PIXL Tuning.TerrainMaxSteps` | `14` | `22` | Material Layers | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Layers.PIXL Tuning.TerrainNearSteps` | `6` | `8` | Material Layers | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Material Layers.PIXL Tuning.TerrainRefinementSteps` | `4` | `6` | Material Layers | Applies the renderer-owned material/POM/BRDF quality contract. |
| `Menu.AtmosphereQuality` | `1` | `2` | Quality/UI | Keeps the visible quality contract synchronized with the module values. |
| `Menu.CameraQuality` | `1` | `2` | Quality/UI | Keeps the visible quality contract synchronized with the module values. |
| `Menu.CharactersQuality` | `1` | `2` | Quality/UI | Keeps the visible quality contract synchronized with the module values. |
| `Menu.LightingQuality` | `1` | `2` | Quality/UI | Keeps the visible quality contract synchronized with the module values. |
| `Menu.MaterialsQuality` | `1` | `2` | Quality/UI | Keeps the visible quality contract synchronized with the module values. |
| `Menu.RendererQuality` | `1` | `2` | Quality/UI | Keeps the visible quality contract synchronized with the module values. |
| `Menu.TerrainVegetationQuality` | `1` | `2` | Quality/UI | Keeps the visible quality contract synchronized with the module values. |
| `Menu.WaterQuality` | `1` | `2` | Quality/UI | Keeps the visible quality contract synchronized with the module values. |
| `Strand Shading.HairMode` | `0` | `1` | Strand Shading | Scales character material/SSS/surface-effect workload. |
| `Tissue Diffusion.BurleySamples` | `12` | `16` | Tissue Diffusion | Scales character material/SSS/surface-effect workload. |
| `Water Optics.CausticsDispersion` | `0.44999998807907104` | `0.65` | Water Optics | Scales the real SSR/caustic workload and stability controls. |
| `Water Optics.SSRDistanceScale` | `0.8600000143051147` | `1.06` | Water Optics | Scales the real SSR/caustic workload and stability controls. |
| `Water Optics.SSREdgeFade` | `1.149999976158142` | `0.95` | Water Optics | Scales the real SSR/caustic workload and stability controls. |
