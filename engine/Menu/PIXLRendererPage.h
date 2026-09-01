#pragma once

#include <array>
#include <optional>
#include <string_view>

/**
 * @brief Shared menu metadata and consolidated settings page for the PIXL
 * renderer override suite.
 *
 * This is deliberately UI-only metadata. RenderModule short names, shader defines,
 * persistence keys and runtime feature identities remain unchanged.
 */
namespace PIXLRendererPage
{
	inline constexpr std::string_view Category = "PIXL Renderer";

	struct Placement
	{
		std::string_view featureShortName;
		std::string_view category;
		std::string_view section;
		std::string_view runtimeGuidance;
	};

	inline constexpr std::array<std::string_view, 4> CategoryOrder{
		"LIGHTING",
		"WORLD",
		"CHARACTER",
		"CAMERA"
	};

	inline constexpr std::array<Placement, 34> Placements{ {
		{ "HybridGI", CategoryOrder[0], "INDIRECT LIGHT & REFLECTIONS", "Lighting controls are real time. Resolution and pipeline toggles automatically rebuild the affected compute shaders." },
		{ "WorldProbes", CategoryOrder[0], "INDIRECT LIGHT & REFLECTIONS", "Environment probes update automatically with the scene. Probe-authoring tools are available only in Developer Mode." },
		{ "SkyBounce", CategoryOrder[0], "INDIRECT LIGHT & REFLECTIONS", "Visibility controls are real time; zenith changes automatically queue a safe probe refresh." },
		{ "AmbientProbe", CategoryOrder[0], "INDIRECT LIGHT & REFLECTIONS", "Diffuse environment and sky-light matching controls update in real time; captured probe content refreshes with the scene." },
		{ "NaturalLighting", CategoryOrder[0], "DIRECT LIGHT", "Physical inverse-square light calibration is automatic. The current release exposes no per-light user tuning." },
		{ "LinearLightCore", CategoryOrder[0], "DIRECT LIGHT", "Linear-light accumulation is selected at startup; quality parameters remain coordinated by the shared physical lighting path." },
		{ "RadiantGrid", CategoryOrder[0], "DIRECT LIGHT", "Particle-light participation updates in real time. Cluster visualizers are Developer Mode diagnostics." },
		{ "ContactShadows", CategoryOrder[0], "DIRECT LIGHT", "Controls update in real time. Sample-count changes automatically rebuild the ray-march shader." },
		{ "SkyVeil", CategoryOrder[0], "SKY & ATMOSPHERE", "Cloud-shadow opacity follows weather in real time; enabling the shader hook requires a restart." },
		{ "InteriorDaylight", CategoryOrder[0], "SKY & ATMOSPHERE", "Interior sun matching updates with cells and weather; boot-state changes require a restart." },
		{ "SkyContinuity", CategoryOrder[0], "SKY & ATMOSPHERE", "Synchronizes sky state with the renderer's atmospheric lighting in real time." },
		{ "Atmosphere", CategoryOrder[0], "SKY & ATMOSPHERE", "Fog and scattering values are real time. Froxel resolution changes recreate volumetric resources automatically." },
		{ "LightVolumes", CategoryOrder[0], "SKY & ATMOSPHERE", "Quality changes recreate the vanilla volumetric-lighting targets and may cause a brief one-time hitch." },
		{ "VolumeOcclusion", CategoryOrder[0], "SKY & ATMOSPHERE", "The shared filtered shadow map is generated at runtime; boot-state changes require a restart." },
		{ "MaterialForge", CategoryOrder[1], "SURFACES & MATERIALS", "Material and BRDF controls update in real time through the shared feature buffer." },
		{ "MaterialLayers", CategoryOrder[1], "SURFACES & MATERIALS", "Material controls update in real time. Content still requires compatible texture assets." },
		{ "WindowLife", CategoryOrder[1], "SURFACES & MATERIALS", "Architectural glass optics and inhabited-window activity update in real time; window-class debug colours identify glass-only, shallow and full occupancy tiers." },
		{ "DistanceBlend", CategoryOrder[1], "DISTANCE & TERRAIN", "Distance transition controls update in real time and are authored as part of the world presentation rather than a laboratory system." },
		{ "TerrainField", CategoryOrder[1], "DISTANCE & TERRAIN", "Terrain field data is part of PIXL's world pipeline. Debug visualizations remain engineering-only." },
		{ "TerrainSeam", CategoryOrder[1], "DISTANCE & TERRAIN", "Terrain blending coordinates near/far landscape continuity. Boot-state changes require a restart." },
		{ "GroundResponse", CategoryOrder[1], "LAND & VEGETATION", "Grass and deformable-ground controls are real time. Initial installation adds Lighting permutations and requires a shader-cache rebuild." },
		{ "FoliageDynamics", CategoryOrder[1], "LAND & VEGETATION", "Vegetation lighting controls update in real time through the shared feature buffer." },
		{ "TerrainDetail", CategoryOrder[1], "LAND & VEGETATION", "Terrain stochastic sampling controls update in real time; enabling the hook at boot requires a restart." },
		{ "TerrainOcclusion", CategoryOrder[1], "LAND & VEGETATION", "Terrain-shadow participation updates in real time; heightfield data refreshes automatically as exterior cells change." },
		{ "Waterbody", CategoryOrder[1], "WATER & WEATHER", "Optimised mesh selection applies when water caches are rebuilt; manual cache tools are Developer Mode actions." },
		{ "WaterOptics", CategoryOrder[1], "WATER & WEATHER", "Reflection and caustic tuning is real time. Initial shader integration or module boot-state changes require a restart." },
		{ "RainResponse", CategoryOrder[1], "WATER & WEATHER", "Weather, material wetness, ripple and splash controls update in real time." },
		{ "SkinOptics", CategoryOrder[2], "SKIN", "SkinOptics optical controls update in real time; texture reload actions explicitly refresh authored detail assets." },
		{ "TissueDiffusion", CategoryOrder[2], "SKIN", "Diffusion controls update in real time. Burley sample-count changes rebuild the sampling kernel." },
		{ "StrandShading", CategoryOrder[2], "HAIR & FABRIC", "Hair scattering, transmission and self-shadow controls update in real time." },
		{ "ThinSurface", CategoryOrder[2], "HAIR & FABRIC", "Global cloth and thin-surface controls update in real time; per-mesh material tags remain authoritative." },
		{ "CameraSuite", CategoryOrder[3], "COLOUR & OUTPUT", "Camera and tone controls update in real time. Windows HDR detection or display-mode changes may require a restart." },
		{ "ImageReconstruction", CategoryOrder[3], "IMAGE & PERFORMANCE", "Image reconstruction is installed as part of the renderer but remains an explicit display choice; changing the technology can require a restart." }
	} };

	inline std::optional<Placement> GetPlacement(std::string_view featureShortName)
	{
		for (const auto& placement : Placements) {
			if (placement.featureShortName == featureShortName)
				return placement;
		}
		return std::nullopt;
	}

	inline std::string_view GetMenuCategory(std::string_view featureShortName, std::string_view nativeCategory)
	{
		return GetPlacement(featureShortName).has_value() ? Category : nativeCategory;
	}

	/** Public product language. Runtime identities remain untouched for compatibility. */
	inline std::string_view GetPublicName(std::string_view featureShortName, std::string_view fallback)
	{
		struct Name { std::string_view id; std::string_view value; };
		constexpr std::array names{
			Name{ "MaterialForge", "PBR MATERIAL" }, Name{ "MaterialLayers", "MATERIAL DEPTH" },
			Name{ "WindowLife", "WINDOW LIFE / ARCHITECTURAL GLASS" },
			Name{ "HybridGI", "RADIANCE WEAVE" }, Name{ "WorldProbes", "WORLD REFLECTIONS" },
			Name{ "SkyBounce", "SKY ILLUMINATION" }, Name{ "AmbientProbe", "AMBIENT LIGHT" },
			Name{ "NaturalLighting", "PHYSICAL LIGHTS" }, Name{ "DistanceBlend", "DISTANCE BLEND" }, Name{ "LinearLightCore", "LINEAR LIGHT" },
			Name{ "RadiantGrid", "LOCAL LIGHTS" }, Name{ "ContactShadows", "CONTACT SHADOWS" },
			Name{ "SkyVeil", "CLOUD LIGHT" }, Name{ "HorizonBlend", "DISTANT HORIZON" },
			Name{ "InteriorDaylight", "INTERIOR SUN" }, Name{ "SkyContinuity", "SKY CONTINUITY" },
			Name{ "Atmosphere", "ATMOSPHERIC FOG" }, Name{ "LightVolumes", "LIGHT SHAFTS" },
			Name{ "VolumeOcclusion", "VOLUMETRIC SHADOW" }, Name{ "SkinOptics", "SKIN DETAIL" },
			Name{ "TissueDiffusion", "SKIN DIFFUSION" }, Name{ "StrandShading", "HAIR SHADING" },
			Name{ "ThinSurface", "THIN MATERIALS" }, Name{ "GroundResponse", "GROUND INTERACTION" },
			Name{ "FoliageDynamics", "FOLIAGE LIGHT & WIND" }, Name{ "TerrainDetail", "TERRAIN DETAIL" },
			Name{ "TerrainField", "TERRAIN DATA" }, Name{ "TerrainSeam", "TERRAIN BLEND" },
			Name{ "TerrainOcclusion", "TERRAIN SHADOW" }, Name{ "Waterbody", "WATER GEOMETRY" },
			Name{ "WaterOptics", "WATER OPTICS" }, Name{ "RainResponse", "WET SURFACES" }, Name{ "PixelCapture", "PIXL DIRECTOR" },
			Name{ "CameraSuite", "DISPLAY & CAMERA" }, Name{ "ImageReconstruction", "IMAGE RECONSTRUCTION" }
		};
		for (const auto& name : names)
			if (name.id == featureShortName)
				return name.value;
		return fallback;
	}

	/** @brief Draws the complete, themed PIXL Renderer control centre. */
	void Render();
}
