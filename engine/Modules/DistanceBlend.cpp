#include "DistanceBlend.h"

#include "../I18n/I18n.h"
#include <algorithm>
#include <cmath>

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
	ImGui::SliderFloat(T(TKEY("lod_terrain_brightness"), "Distant Terrain Brightness"), &settings.LODTerrainBrightness, 0.01f, 5.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SliderFloat(T(TKEY("lod_object_brightness"), "Distant Object Brightness"), &settings.LODObjectBrightness, 0.01f, 5.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SliderFloat(T(TKEY("lod_object_snow_brightness"), "Distant Snow Brightness"), &settings.LODObjectSnowBrightness, 0.01f, 5.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SliderFloat(T(TKEY("lod_terrain_gamma"), "Distant Terrain Tone"), &settings.LODTerrainGamma, 0.1f, 3.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SliderFloat(T(TKEY("lod_object_gamma"), "Distant Object Tone"), &settings.LODObjectGamma, 0.1f, 3.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SliderFloat(T(TKEY("lod_object_snow_gamma"), "Distant Snow Tone"), &settings.LODObjectSnowGamma, 0.1f, 3.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	Util::UIntCheckbox(T(TKEY("disable_terrain_vertex_colors"), "Disable Terrain Vertex Colors"), &settings.DisableTerrainVertexColors);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("disable_terrain_vertex_colors_tooltip"),
							  "Disables vertex coloring on nearby terrain. Best combined with terrain LOD generated in xLODGen with Vertex Color Intensity set to 0."));
	}
}

#undef I18N_KEY_PREFIX

void DistanceBlend::LoadSettings(json& o_json)
{
	settings = o_json;
	auto bounded = [](float value, float minimum, float maximum) {
		return std::isfinite(value) ? std::clamp(value, minimum, maximum) : 1.0f;
	};
	settings.LODTerrainBrightness = bounded(settings.LODTerrainBrightness, 0.01f, 5.0f);
	settings.LODObjectBrightness = bounded(settings.LODObjectBrightness, 0.01f, 5.0f);
	settings.LODObjectSnowBrightness = bounded(settings.LODObjectSnowBrightness, 0.01f, 5.0f);
	settings.LODTerrainGamma = bounded(settings.LODTerrainGamma, 0.1f, 3.0f);
	settings.LODObjectGamma = bounded(settings.LODObjectGamma, 0.1f, 3.0f);
	settings.LODObjectSnowGamma = bounded(settings.LODObjectSnowGamma, 0.1f, 3.0f);
	settings.DisableTerrainVertexColors = settings.DisableTerrainVertexColors != 0 ? 1u : 0u;
}

void DistanceBlend::SaveSettings(json& o_json)
{
	o_json = settings;
}

void DistanceBlend::RestoreDefaultSettings()
{
	settings = {};
}
