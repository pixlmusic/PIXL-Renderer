// PIXL Renderer - curved-surface parallax runtime integration.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional permissions are described in the repository EXCEPTIONS.md.

#include "CurvedSurfaceMapping.h"

#include "Util.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	CurvedSurfaceMapping::Settings,
	Enabled, Quality, Depth, HeightBias, CurvatureStrength, SilhouetteStrength,
	FullQualityDistance, MaxDistance, MaxTexelShift, NormalStrength,
	MinSteps, MaxSteps, BinarySteps, ShadowSteps,
	CurvedSurface, SilhouetteClipping, SelfOcclusion, SelfShadow, DepthWrite,
	StaticOpaque, Trees, Terrain,
	OcclusionStrength, ShadowStrength, GrazingProtection, DebugMode)

namespace
{
	template <class T>
	void ClampSetting(T& a_value, T a_minimum, T a_maximum)
	{
		a_value = std::clamp(a_value, a_minimum, a_maximum);
	}

	void Sanitize(CurvedSurfaceMapping::Settings& a_settings)
	{
		ClampSetting(a_settings.Quality, 0u, 3u);
		ClampSetting(a_settings.Depth, 0.05f, 2.5f);
		ClampSetting(a_settings.HeightBias, -0.35f, 0.35f);
		ClampSetting(a_settings.CurvatureStrength, 0.0f, 1.5f);
		ClampSetting(a_settings.SilhouetteStrength, 0.0f, 1.0f);
		ClampSetting(a_settings.FullQualityDistance, 64.0f, 4096.0f);
		ClampSetting(a_settings.MaxDistance, a_settings.FullQualityDistance + 1.0f, 8192.0f);
		ClampSetting(a_settings.MaxTexelShift, 2.0f, 48.0f);
		ClampSetting(a_settings.NormalStrength, 0.0f, 1.5f);
		ClampSetting(a_settings.MinSteps, 4u, 20u);
		ClampSetting(a_settings.MaxSteps, a_settings.MinSteps, 32u);
		ClampSetting(a_settings.BinarySteps, 2u, 6u);
		ClampSetting(a_settings.ShadowSteps, 0u, 8u);
		ClampSetting(a_settings.OcclusionStrength, 0.0f, 1.0f);
		ClampSetting(a_settings.ShadowStrength, 0.0f, 1.0f);
		ClampSetting(a_settings.GrazingProtection, 0.0f, 1.0f);
		ClampSetting(a_settings.DebugMode, 0u, 6u);
	}
}

bool CurvedSurfaceMapping::AffectsCachedShader(
	RE::BSShader::Type a_type, std::uint32_t a_descriptor, CachedShaderStage a_stage)
{
	// CSPOM is held dormant for 1.0.4. Its define remains emitted for cache-key
	// compatibility, but no setting change is allowed to invalidate user stages.
	(void)a_type;
	(void)a_descriptor;
	(void)a_stage;
	return false;
}

std::pair<std::string, std::vector<std::string>> CurvedSurfaceMapping::GetModuleSummary()
{
	return {
		"Experimental adaptive relief mapping for eligible opaque materials with bounded curvature, edge protection and distance scaling.",
		{ "Adaptive coarse march and binary hit refinement", "Curvature-aware height traversal", "Conservative silhouette clipping", "MaterialForge and authored-height integration" }
	};
}

void CurvedSurfaceMapping::SetupResources()
{
	Sanitize(settings);
	settings.Enabled = false;
	logger::info(
		"[PIXL][CSPOM] Curved Surface Mapping initialized: enabled={} quality={} curvature={} maxDistance={} depthWrite={}",
		settings.Enabled, settings.Quality, settings.CurvedSurface, settings.MaxDistance,
		settings.DepthWrite);
}

void CurvedSurfaceMapping::LoadSettings(json& a_json)
{
	settings = a_json;
	Sanitize(settings);
	settings.Enabled = false;
}

void CurvedSurfaceMapping::SaveSettings(json& a_json)
{
	Sanitize(settings);
	settings.Enabled = false;
	a_json = settings;
}

void CurvedSurfaceMapping::DrawSettings()
{
	ImGui::TextWrapped("EXPERIMENTAL. Replaces only the displacement trace on eligible opaque materials; MaterialForge and Complex Material lighting remain active.");
	ImGui::Separator();
	ImGui::Checkbox("Enable Curved Surface Mapping", &settings.Enabled);
	if (auto tooltip = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("Adds camera-relative relief, curved-surface correction and guarded silhouette clipping. Disabled surfaces use the existing PIXL POM path.");

	ImGui::BeginDisabled(!settings.Enabled);
	static constexpr const char* qualityNames[] = { "Low", "Medium", "High", "Ultra" };
	int quality = static_cast<int>(settings.Quality);
	if (ImGui::Combo("Quality", &quality, qualityNames, static_cast<int>(std::size(qualityNames))))
		settings.Quality = static_cast<std::uint32_t>(quality);
	ImGui::SliderFloat("Depth", &settings.Depth, 0.25f, 2.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SliderFloat("Curvature", &settings.CurvatureStrength, 0.0f, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::Checkbox("Curved surface correction", &settings.CurvedSurface);
	ImGui::Checkbox("Silhouette clipping", &settings.SilhouetteClipping);
	ImGui::Checkbox("Self occlusion", &settings.SelfOcclusion);
	ImGui::Checkbox("Self shadow", &settings.SelfShadow);
	ImGui::SliderFloat("Maximum distance", &settings.MaxDistance, 256.0f, 4096.0f, "%.0f units", ImGuiSliderFlags_AlwaysClamp);

	if (ImGui::TreeNode("Material eligibility")) {
		ImGui::Checkbox("Static architecture, rocks and cliffs", &settings.StaticOpaque);
		ImGui::Checkbox("Tree trunks", &settings.Trees);
		ImGui::Checkbox("Terrain (conservative)", &settings.Terrain);
		ImGui::TextDisabled("Actors, grass, water, glass, particles and distant LOD are always excluded.");
		ImGui::TreePop();
	}

	if (ImGui::TreeNode("Advanced / diagnostics")) {
		static constexpr std::uint32_t minStepsMinimum = 4u;
		static constexpr std::uint32_t minStepsMaximum = 20u;
		static constexpr std::uint32_t maxStepsMinimum = 8u;
		static constexpr std::uint32_t maxStepsMaximum = 32u;
		static constexpr std::uint32_t refinementMinimum = 2u;
		static constexpr std::uint32_t refinementMaximum = 6u;
		static constexpr std::uint32_t shadowStepsMinimum = 0u;
		static constexpr std::uint32_t shadowStepsMaximum = 8u;
		ImGui::SliderFloat("Full quality distance", &settings.FullQualityDistance, 64.0f, settings.MaxDistance, "%.0f units", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Height bias", &settings.HeightBias, -0.35f, 0.35f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Maximum texel shift", &settings.MaxTexelShift, 2.0f, 48.0f, "%.0f texels", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Displaced normal strength", &settings.NormalStrength, 0.0f, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderScalar("Minimum steps", ImGuiDataType_U32, &settings.MinSteps, &minStepsMinimum, &minStepsMaximum);
		ImGui::SliderScalar("Maximum steps", ImGuiDataType_U32, &settings.MaxSteps, &maxStepsMinimum, &maxStepsMaximum);
		ImGui::SliderScalar("Binary refinement", ImGuiDataType_U32, &settings.BinarySteps, &refinementMinimum, &refinementMaximum);
		ImGui::SliderScalar("Self-shadow budget", ImGuiDataType_U32, &settings.ShadowSteps, &shadowStepsMinimum, &shadowStepsMaximum);
		ImGui::Checkbox("Correct effects depth", &settings.DepthWrite);
		ImGui::TextDisabled("Outward geometry expansion is withheld: the active universal Lighting path has no safe qualified hull/domain boundary.");
		static constexpr const char* debugNames[] = { "Off", "Eligibility", "Height", "Step demand", "Curvature", "Edge confidence", "LOD" };
		int debug = static_cast<int>(settings.DebugMode);
		if (ImGui::Combo("Visualisation", &debug, debugNames, static_cast<int>(std::size(debugNames))))
			settings.DebugMode = static_cast<std::uint32_t>(debug);
		ImGui::TreePop();
	}
	ImGui::EndDisabled();
	Sanitize(settings);
}
