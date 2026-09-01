#include "HairReconstruction.h"

#include <algorithm>
#include <cmath>

#include "Globals.h"
#include "I18n/I18n.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	HairReconstruction::Settings,
	Enabled,
	Quality,
	DetectionThreshold,
	ReconstructionThreshold,
	AnisotropicLighting,
	DirectionBlend,
	StrandDetail,
	Transmission,
	SecondaryMotion,
	WindResponse,
	MotionStrength,
	Damping,
	WetHair,
	WetDarkening,
	WetRoughness,
	WetWeight,
	SnowResponse,
	ProceduralStrands,
	StrandDensity,
	SilhouetteDetail,
	SimulationDistance,
	DebugMode)

namespace
{
	void ClampSettings(HairReconstruction::Settings& a_settings)
	{
		a_settings.Quality = std::min(a_settings.Quality, 3u);
		a_settings.DetectionThreshold = std::clamp(a_settings.DetectionThreshold, 0.65f, 0.98f);
		a_settings.ReconstructionThreshold = std::clamp(
			std::max(a_settings.ReconstructionThreshold, a_settings.DetectionThreshold), 0.70f, 1.0f);
		a_settings.DirectionBlend = std::clamp(a_settings.DirectionBlend, 0.0f, 1.0f);
		a_settings.StrandDetail = std::clamp(a_settings.StrandDetail, 0.0f, 1.0f);
		a_settings.Transmission = std::clamp(a_settings.Transmission, 0.0f, 1.5f);
		a_settings.WindResponse = std::clamp(a_settings.WindResponse, 0.0f, 1.5f);
		a_settings.MotionStrength = std::clamp(a_settings.MotionStrength, 0.0f, 0.50f);
		a_settings.Damping = std::clamp(a_settings.Damping, 0.0f, 1.0f);
		a_settings.WetDarkening = std::clamp(a_settings.WetDarkening, 0.0f, 0.45f);
		a_settings.WetRoughness = std::clamp(a_settings.WetRoughness, 0.04f, 0.70f);
		a_settings.WetWeight = std::clamp(a_settings.WetWeight, 0.0f, 1.0f);
		a_settings.StrandDensity = std::clamp(a_settings.StrandDensity, 0.0f, 1.0f);
		a_settings.SilhouetteDetail = std::clamp(a_settings.SilhouetteDetail, 0.0f, 0.25f);
		a_settings.SimulationDistance = std::clamp(a_settings.SimulationDistance, 512.0f, 8192.0f);
		a_settings.DebugMode = std::min(a_settings.DebugMode, 7u);
	}
}

HairReconstruction::Settings HairReconstruction::GetCommonBufferData() const
{
	Settings result = settings;
	float frameDelta = globals::game::deltaTime ? *globals::game::deltaTime : RE::GetSecondsSinceLastFrame();
	if (!std::isfinite(frameDelta) || frameDelta < 0.0f)
		frameDelta = 0.0f;
	if (globals::game::ui && globals::game::ui->GameIsPaused())
		frameDelta = 0.0f;
	result.FrameDelta = std::clamp(frameDelta, 0.0f, 0.10f);
	return result;
}

void HairReconstruction::ApplyQualityTier(std::uint32_t a_quality)
{
	settings.Quality = std::min(a_quality, 3u);
	switch (settings.Quality) {
	case 0:
		settings.SecondaryMotion = false;
		settings.ProceduralStrands = false;
		settings.StrandDetail = 0.20f;
		settings.SilhouetteDetail = 0.0f;
		settings.SimulationDistance = 1600.0f;
		break;
	case 1:
		settings.SecondaryMotion = true;
		settings.ProceduralStrands = false;
		settings.StrandDetail = 0.35f;
		settings.MotionStrength = 0.14f;
		settings.SilhouetteDetail = 0.03f;
		settings.SimulationDistance = 3000.0f;
		break;
	case 2:
		settings.SecondaryMotion = true;
		settings.ProceduralStrands = true;
		settings.StrandDetail = 0.55f;
		settings.StrandDensity = 0.22f;
		settings.MotionStrength = 0.18f;
		settings.SilhouetteDetail = 0.06f;
		settings.SimulationDistance = 4600.0f;
		break;
	default:
		settings.SecondaryMotion = true;
		settings.ProceduralStrands = true;
		settings.StrandDetail = 0.75f;
		settings.StrandDensity = 0.38f;
		settings.MotionStrength = 0.22f;
		settings.SilhouetteDetail = 0.09f;
		settings.SimulationDistance = 6500.0f;
		break;
	}
	ClampSettings(settings);
}

void HairReconstruction::DrawSettings()
{
	auto tooltip = [](const char* a_text) {
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", a_text);
	};

	Util::UIntCheckbox("Enable Hair Reconstruction", &settings.Enabled);
	int quality = static_cast<int>(settings.Quality);
	if (ImGui::Combo("Effect Quality", &quality, "Low\0Medium\0High\0Ultra\0"))
		ApplyQualityTier(static_cast<std::uint32_t>(std::clamp(quality, 0, 3)));
	tooltip("Scales virtual fibre detail, motion distance and simulation complexity. Low retains automatic detection and improved lighting.");

	if (ImGui::CollapsingHeader("Appearance", ImGuiTreeNodeFlags_DefaultOpen)) {
		Util::UIntCheckbox("Anisotropic Direction Reconstruction", &settings.AnisotropicLighting);
		ImGui::SliderFloat("Direction Inference", &settings.DirectionBlend, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		tooltip("Blends authored tangents with a stable UV/card direction reconstructed from the rendered geometry.");
		ImGui::SliderFloat("Virtual Strand Detail", &settings.StrandDetail, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Transmission Response", &settings.Transmission, 0.0f, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Silhouette Breakup", &settings.SilhouetteDetail, 0.0f, 0.25f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		tooltip("Subtly filters authored alpha-card edges. High values can expose poor source alpha and are intentionally bounded.");
	}

	if (ImGui::CollapsingHeader("Dynamics", ImGuiTreeNodeFlags_DefaultOpen)) {
		Util::UIntCheckbox("Secondary Motion", &settings.SecondaryMotion);
		ImGui::SliderFloat("Wind Response", &settings.WindResponse, 0.0f, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Motion Strength", &settings.MotionStrength, 0.0f, 0.50f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Damping", &settings.Damping, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		tooltip("Motion is deterministic in current and previous frames so reconstruction receives matching hair velocity rather than actor-only motion.");
		ImGui::SliderFloat("Simulation Distance", &settings.SimulationDistance, 512.0f, 8192.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
	}

	if (ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_DefaultOpen)) {
		Util::UIntCheckbox("Wet Hair", &settings.WetHair);
		ImGui::SliderFloat("Wet Darkening", &settings.WetDarkening, 0.0f, 0.45f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Wet Roughness", &settings.WetRoughness, 0.04f, 0.70f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Wet Motion Weight", &settings.WetWeight, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		Util::UIntCheckbox("Snow Accumulation Compatibility", &settings.SnowResponse);
		tooltip("Snow remains owned by Actor Surface Effects; this switch only allows detected hair to participate in that existing actor-local material path.");
	}

	if (ImGui::CollapsingHeader("Advanced")) {
		Util::UIntCheckbox("Procedural Micro-Fibres", &settings.ProceduralStrands);
		ImGui::SliderFloat("Micro-Fibre Density", &settings.StrandDensity, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Hair Detection Threshold", &settings.DetectionThreshold, 0.65f, 0.98f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Motion Reconstruction Threshold", &settings.ReconstructionThreshold, settings.DetectionThreshold, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		tooltip("Higher thresholds reduce false positives. Native Skyrim Hair technique geometry is authoritative and is never rejected by these heuristic thresholds.");
	}

	if (ImGui::CollapsingHeader("Debug")) {
		int debugMode = static_cast<int>(settings.DebugMode);
		if (ImGui::Combo("Hair Debug View", &debugMode,
				"Off\0Detection\0Direction\0Root / Tip\0Flexibility / Region\0Motion\0Temporal Confidence\0Micro-Fibres\0"))
			settings.DebugMode = static_cast<std::uint32_t>(std::clamp(debugMode, 0, 7));
	}

	ClampSettings(settings);
}

void HairReconstruction::LoadSettings(json& a_json)
{
	settings = a_json;
	ClampSettings(settings);
}

void HairReconstruction::SaveSettings(json& a_json)
{
	a_json = settings;
}

void HairReconstruction::RestoreDefaultSettings()
{
	settings = {};
}
