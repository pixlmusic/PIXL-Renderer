#include "DistanceBlend.h"

#include "../I18n/I18n.h"

#define I18N_KEY_PREFIX "feature.distance_blend."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	DistanceBlend::Settings,
	LODTerrainBrightness,
	LODObjectBrightness,
	LODObjectSnowBrightness,
	DisableTerrainVertexColors,
	LODTerrainGamma,
	LODObjectGamma,
	LODObjectSnowGamma)

void DistanceBlend::DrawSettings()
{
	ImGui::SliderFloat(T(TKEY("lod_terrain_brightness"), "LOD Terrain Brightness"), &settings.LODTerrainBrightness, 0.01f, 5.f, "%.2f");
	ImGui::SliderFloat(T(TKEY("lod_object_brightness"), "LOD Object Brightness"), &settings.LODObjectBrightness, 0.01f, 5.f, "%.2f");
	ImGui::SliderFloat(T(TKEY("lod_object_snow_brightness"), "LOD Object Snow Brightness"), &settings.LODObjectSnowBrightness, 0.01f, 5.f, "%.2f");
	ImGui::SliderFloat(T(TKEY("lod_terrain_gamma"), "LOD Terrain Gamma"), &settings.LODTerrainGamma, 0.1f, 3.f, "%.2f");
	ImGui::SliderFloat(T(TKEY("lod_object_gamma"), "LOD Object Gamma"), &settings.LODObjectGamma, 0.1f, 3.f, "%.2f");
	ImGui::SliderFloat(T(TKEY("lod_object_snow_gamma"), "LOD Object Snow Gamma"), &settings.LODObjectSnowGamma, 0.1f, 3.f, "%.2f");
	ImGui::Checkbox(T(TKEY("disable_terrain_vertex_colors"), "Disable Terrain Vertex Colors"), (bool*)&settings.DisableTerrainVertexColors);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("disable_terrain_vertex_colors_tooltip"),
							  "Disables vertex coloring on nearby terrain. Best combined with terrain LOD generated in xLODGen with Vertex Color Intensity set to 0."));
	}
}

#undef I18N_KEY_PREFIX

void DistanceBlend::LoadSettings(json& o_json)
{
	settings = o_json;
}

void DistanceBlend::SaveSettings(json& o_json)
{
	o_json = settings;
}

void DistanceBlend::RestoreDefaultSettings()
{
	settings = {};
}
