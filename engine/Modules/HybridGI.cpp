#include "HybridGI.h"

#include <DirectXTex.h>

#include "../I18n/I18n.h"
#include "Deferred.h"
#include "Modules/CameraSuite.h"
#include "Modules/ImageReconstruction.h"
#include "Modules/PixelCapture.h"
#include "Modules/LinearLightCore.h"
#include "Globals.h"
#include "Menu.h"
#include "State.h"
#include "MaterialForge.h"
#include "MaterialLayers.h"
#include "Renderer/QualityProfiles.h"
#include "Util.h"
#include "../Menu/PIXLStyle.h"
#include "Utils/FileSystem.h"

#include <RE/S/SendHUDMessage.h>

#include <array>
#include <chrono>
#include <format>
#include <fstream>
#include <iomanip>

#define I18N_KEY_PREFIX "feature.hybrid_gi."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	HybridGI::Settings,
	Enabled,
	EnableGI,
	EnableExperimentalSpecularGI,
	EnableVanillaSSAO,
	NumSlices,
	NumSteps,
	ResolutionMode,
	MinScreenRadius,
	AORadius,
	GIRadius,
	Thickness,
	DepthFadeRange,
	GISaturation,
	GIDistanceCompensation,
	AOPower,
	GIStrength,
	EnableTemporalDenoiser,
	EnableBlur,
	EnableAdaptiveDenoiser,
	DepthDisocclusion,
	NormalDisocclusion,
	MaxAccumFrames,
	BlurRadius,
	DistanceNormalisation,
	EnableWorldCache,
	WorldCacheMaxAge,
	WorldCacheSampleCount,
	WorldCacheStrength,
	WorldCacheCellSizeNear,
	WorldCacheCellSizeFar,
	WorldCacheRadius,
	WorldCacheLeakReduction,
	WorldCacheTemporalResponse,
	EnableWorldCacheSecondBounce,
	WorldCacheSecondBounceStrength,
	WorldCacheInjectionStride,
	WorldCacheTraceSteps,
	EnableDirectionalOcclusion,
	DirectionalOcclusionStrength,
	EnableVoxelReflections,
	VoxelReflectionStrength,
	VoxelReflectionRoughnessCutoff,
	EnableBentNormalLighting,
	BentNormalStrength,
	EnableSpecularOcclusion,
	SpecularOcclusionStrength,
	RadianceFireflyClamp,
	UpsampleEdgeThreshold,
	ReflectionIntensity,
	ReflectionMaxRoughness,
	ReflectionMaxDistance,
	ReflectionThickness,
	ReflectionSteps,
	ReflectionTemporalResponse,
	ReflectionFireflyClamp,
	ReflectionWorldFallbackStrength,
	ReflectionRayBias,
	ReflectionRoughnessJitter,
	DebugView,
	DebugGain)

////////////////////////////////////////////////////////////////////////////////////

namespace
{
	struct PixlDiagnosticView
	{
		std::string_view name;
		uint giDebugView;
		uint materialDebugMode;
	};

	constexpr std::array PixlDiagnosticViews{
		PixlDiagnosticView{ "00-composite", 0, 0 },
		PixlDiagnosticView{ "01-ao-occlusion", 1, 0 },
		PixlDiagnosticView{ "02-diffuse-gi", 2, 0 },
		PixlDiagnosticView{ "03-specular-gi", 3, 0 },
		PixlDiagnosticView{ "04-lighting-input-radiance", 4, 0 },
		PixlDiagnosticView{ "05-voxel-confidence-coverage", 5, 0 },
		PixlDiagnosticView{ "06-voxel-cascade", 6, 0 },
		PixlDiagnosticView{ "07-cached-irradiance", 7, 0 },
		PixlDiagnosticView{ "08-directional-occlusion", 8, 0 },
		PixlDiagnosticView{ "09-pbr-coverage", 0, 1 },
		PixlDiagnosticView{ "10-pbr-base-color", 0, 2 },
		PixlDiagnosticView{ "11-pbr-roughness", 0, 3 },
		PixlDiagnosticView{ "12-pbr-metal-conductor-classification", 0, 4 },
		PixlDiagnosticView{ "13-pbr-f0", 0, 5 },
		PixlDiagnosticView{ "14-pbr-world-normal", 0, 6 },
		PixlDiagnosticView{ "15-pbr-direct-diffuse", 0, 7 },
		PixlDiagnosticView{ "16-pbr-direct-specular", 0, 8 },
		PixlDiagnosticView{ "17-pbr-physical-delta", 0, 9 },
		PixlDiagnosticView{ "18-pbr-emissive", 0, 10 },
		PixlDiagnosticView{ "19-voxel-reflection-fallback", 9, 0 },
		PixlDiagnosticView{ "20-bent-normal", 10, 0 },
		PixlDiagnosticView{ "21-directional-visibility", 11, 0 },
		PixlDiagnosticView{ "22-specular-occlusion", 12, 0 },
		PixlDiagnosticView{ "23-hybrid-reflections", 13, 0 },
		PixlDiagnosticView{ "24-pbr-raw-authored-metalness", 0, 11 },
		PixlDiagnosticView{ "25-pbr-effective-metalness", 0, 12 },
	};

	std::string PixlDiagnosticTimestamp()
	{
		const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
		std::tm local{};
		localtime_s(&local, &now);
		return std::format("{:04}{:02}{:02}-{:02}{:02}{:02}",
			local.tm_year + 1900,
			local.tm_mon + 1,
			local.tm_mday,
			local.tm_hour,
			local.tm_min,
			local.tm_sec);
	}

	std::uint64_t HashFileFNV1a(const std::filesystem::path& path)
	{
		std::ifstream stream(path, std::ios::binary);
		std::uint64_t hash = 1469598103934665603ull;
		std::array<char, 64 * 1024> bytes{};
		while (stream) {
			stream.read(bytes.data(), bytes.size());
			for (std::streamsize index = 0; index < stream.gcount(); ++index) {
				hash ^= static_cast<std::uint8_t>(bytes[static_cast<std::size_t>(index)]);
				hash *= 1099511628211ull;
			}
		}
		return hash;
	}


	enum class PixlQualityPreset
	{
		Performance,
		Balanced,
		Quality,
		Ultra,
		Custom
	};

	const char* GetPresetName(PixlQualityPreset preset)
	{
		switch (preset) {
		case PixlQualityPreset::Performance:
			return "Low";
		case PixlQualityPreset::Balanced:
			return "Medium";
		case PixlQualityPreset::Quality:
			return "High";
		case PixlQualityPreset::Ultra:
			return "Ultra";
		default:
			return "Custom";
		}
	}

	const char* GetResolutionName(int mode)
	{
		switch (mode) {
		case 0:
			return "Full";
		case 2:
			return "Quarter";
		default:
			return "Half";
		}
	}


	const char* GetPresetDescription(PixlQualityPreset preset)
	{
		switch (preset) {
		case PixlQualityPreset::Performance:
			return "Unified Low lighting workload: bounded GI rays, reduced cache cadence and lower shadow sampling.";
		case PixlQualityPreset::Balanced:
			return "Unified Medium lighting workload with balanced GI coverage, cache updates and shadow cost.";
		case PixlQualityPreset::Quality:
			return "Unified High lighting workload with strong GI/reflection density and contact-shadow sampling.";
		case PixlQualityPreset::Ultra:
			return "Unified Ultra lighting workload. This preserves the shipped PIXL known-good baseline.";
		default:
			return "Current values do not exactly match a built-in quality preset.";
		}
	}

	PixlQualityPreset DetectPreset()
	{
		const int quality = PIXLRenderer::QualityProfiles::DetectLightingTier();
		return quality >= PIXLRenderer::QualityProfiles::Low && quality <= PIXLRenderer::QualityProfiles::Ultra ?
		           static_cast<PixlQualityPreset>(quality) :
		           PixlQualityPreset::Custom;
	}

	bool DrawPixlToggleField(const char* label, bool* value)
	{
		// Use the common full-width row instead of positioning the latch relative
		// to the cursor *after* drawing the label.  The old calculation could push
		// the SSAO latch beyond a narrow/table column, leaving only a few pixels of
		// its hit box accessible at some UI scales.
		return PIXLUI::LabeledToggle(label, value);
	}

	bool BeginSettingsTable(const char* id)
	{
		if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
			return false;
		ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthStretch, 0.46f);
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.54f);
		return true;
	}

	void BeginSettingRow(const char* label, const char* tooltip = nullptr)
	{
		ImGui::TableNextRow(
			ImGuiTableRowFlags_None,
			PIXLUI::Ref(34.0f));
		ImGui::TableSetColumnIndex(0);

		ImGui::AlignTextToFramePadding();
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float cy =
			p.y + ImGui::GetTextLineHeight() * 0.5f +
			ImGui::GetStyle().FramePadding.y;

		ImGui::GetWindowDrawList()->AddCircleFilled(
			ImVec2(p.x + PIXLUI::Ref(4.0f), cy),
			PIXLUI::Ref(2.0f),
			PIXLUI::Colors::CyanSoft);

		ImGui::SetCursorPosX(
			ImGui::GetCursorPosX() +
			PIXLUI::Ref(12.0f));
		ImGui::TextColored(
			PIXLUI::ToVec4(PIXLUI::Colors::TextMuted),
			"%s",
			label);

		if (tooltip && tooltip[0] != '\0') {
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::PushTextWrapPos(
					ImGui::GetCursorPosX() +
					PIXLUI::Ref(330.0f));
				ImGui::TextUnformatted(tooltip);
				ImGui::PopTextWrapPos();
			}
		}

		ImGui::TableSetColumnIndex(1);
		ImGui::SetNextItemWidth(-1.0f);
	}

	void DrawPresetButton(const char* label, PixlQualityPreset preset, PixlQualityPreset activePreset)
	{
		const bool selected =
			activePreset == preset;

		if (PIXLUI::ActionButton(
				label,
				ImVec2(
					ImGui::GetContentRegionAvail().x,
					PIXLUI::Ref(34.0f)),
				selected)) {
			// Q1: the native Radiance page uses the same Lighting authority as the
			// main Quality workspace and Tuner quick panel. No duplicate preset table.
			PIXLRenderer::QualityProfiles::Apply(
				PIXLRenderer::QualityProfiles::Group::Lighting,
				static_cast<int>(preset));
			globals::state->Save();
		}

		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::PushTextWrapPos(
				ImGui::GetCursorPosX() +
					PIXLUI::Ref(300.0f));
			ImGui::TextUnformatted(
				GetPresetDescription(preset));
			ImGui::PopTextWrapPos();
		}
	}

}

////////////////////////////////////////////////////////////////////////////////////

void HybridGI::RestoreDefaultSettings()
{
	settings = {};
	recompileFlag = true;
}

void HybridGI::DrawSettings()
{
	// The unified module panel exposes every control regardless of legacy UI mode.
	constexpr bool showAdvanced = true;

	if (!ShadersOK()) {
		Util::Text::Error("%s", T(TKEY("shader_compile_error"), "Compute shaders failed to compile!"));
		ImGui::TextWrapped("The controls remain available, but the effect will not render until all Hybrid GI compute shaders compile successfully.");
	}

	const auto activePreset = DetectPreset();

	// -------------------------------------------------------------------------
	// QUICK SETUP
	// -------------------------------------------------------------------------
	PIXLUI::SectionBanner("QUICK SETUP");
	ImGui::TextWrapped("Choose a quality target, then adjust bounced light and contact shading. Quality changes affect GPU cost; the sections below fine-tune the result.");

	if (ImGui::BeginTable("PIXL GI Status", 3, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
		ImGui::TableNextColumn();
		DrawPixlToggleField(T(TKEY("enabled"), "Enabled"), &settings.Enabled);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Master switch for PIXL Hybrid GI. When disabled, the GI outputs are cleared and the remaining controls are ignored.");

		ImGui::TableNextColumn();
		{
			auto guard = Util::DisableGuard(!settings.Enabled);
			recompileFlag |= DrawPixlToggleField("Indirect Lighting", &settings.EnableGI);
		}
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Enables bounced indirect light. Ambient occlusion can still be used independently. Changing this automatically rebuilds the affected compute permutation.");

		ImGui::TableNextColumn();
		DrawPixlToggleField(T(TKEY("vanilla_ssao"), "Vanilla SSAO"), &settings.EnableVanillaSSAO);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Usually leave this off while PIXL AO is active to avoid double-darkening corners and contact areas.");
		ImGui::EndTable();
	}

	{
		auto guard = Util::DisableGuard(!settings.Enabled);
		ImGui::TextUnformatted("Quality Preset");
		if (ImGui::BeginTable("PIXL GI Presets", 4, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
			ImGui::TableNextColumn();
			DrawPresetButton("Low", PixlQualityPreset::Performance, activePreset);
			ImGui::TableNextColumn();
			DrawPresetButton("Medium", PixlQualityPreset::Balanced, activePreset);
			ImGui::TableNextColumn();
			DrawPresetButton("High", PixlQualityPreset::Quality, activePreset);
			ImGui::TableNextColumn();
			DrawPresetButton("Ultra", PixlQualityPreset::Ultra, activePreset);
			ImGui::EndTable();
		}

		ImGui::TextDisabled("Current: %s  |  %s resolution  |  %u x %u screen-space samples  |  %u world samples / %u cone steps",
			GetPresetName(DetectPreset()),
			GetResolutionName(settings.ResolutionMode),
			settings.NumSlices,
			settings.NumSteps,
			settings.EnableWorldCache ? settings.WorldCacheSampleCount : 0u,
			settings.EnableWorldCache ? settings.WorldCacheTraceSteps : 0u);
		ImGui::TextDisabled("Ultra is the shipped PIXL baseline. Unified presets also coordinate contact shadows and Light Volumes without changing artistic GI tuning.");

		if (Util::WarningButton("Restore Unified Ultra")) {
			PIXLRenderer::QualityProfiles::Apply(
				PIXLRenderer::QualityProfiles::Group::Lighting,
				PIXLRenderer::QualityProfiles::Ultra);
			globals::state->Save();
		}
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Restores the shipped unified Ultra lighting workload while preserving your artistic GI strength, colour and radius tuning.");

	}

	// -------------------------------------------------------------------------
	// LIGHTING BALANCE
	// -------------------------------------------------------------------------
	PIXLUI::SectionBanner("LIGHTING BALANCE");
	{
		auto guard = Util::DisableGuard(!settings.Enabled);
		if (BeginSettingsTable("PIXL GI Lighting")) {
			BeginSettingRow("Ambient Occlusion Strength", "Controls contact and crevice darkening. 1.0 is neutral; increase carefully to avoid crushed corners.");
			ImGui::SliderFloat("##ao_strength", &settings.AOPower, 0.0f, 4.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

			BeginSettingRow("Contact Depth", "Adds restrained near-field grounding by reusing PIXL GI horizon samples. It does not run a second AO pass or darken direct lights.");
			PIXLUI::Toggle("##contact_depth", &settings.EnableContactDepth);

			BeginSettingRow("Contact Depth Strength", "Controls small-scale contact and crevice depth. The contribution is bounded to prevent double-darkening with GI.");
			{
				auto contactGuard = Util::DisableGuard(!settings.EnableContactDepth);
				ImGui::SliderFloat("##contact_depth_strength", &settings.ContactDepthStrength, 0.0f, 0.75f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			}

			BeginSettingRow("Indirect Light Strength", "Brightness of bounced diffuse lighting gathered by the GI system.");
			{
				auto ilGuard = Util::DisableGuard(!settings.EnableGI);
				ImGui::SliderFloat("##gi_strength", &settings.GIStrength, 0.0f, 4.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			}

			BeginSettingRow("Indirect Light Saturation", "Colour carried by bounced lighting. Lower values make bounce light more neutral; 100% preserves the sampled colour.");
			{
				auto ilGuard = Util::DisableGuard(!settings.EnableGI);
				Util::PercentageSlider("##gi_saturation", &settings.GISaturation);
			}

			BeginSettingRow("AO Reach", "World-space radius of ambient occlusion. Smaller values create tighter contact shadows; larger values affect broader forms.");
			ImGui::SliderFloat("##ao_radius", &settings.AORadius, 10.0f, 1024.0f, "%.0f units", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);

			BeginSettingRow("GI Reach", "How far the screen-space GI searches for indirect-light contributors. Larger values cover broader lighting but increase the chance of sparse samples.");
			{
				auto ilGuard = Util::DisableGuard(!settings.EnableGI);
				ImGui::SliderFloat("##gi_radius", &settings.GIRadius, 10.0f, 1024.0f, "%.0f units", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
			}

			BeginSettingRow("Effect Fade Distance", "Near/far distance where screen-space GI and AO progressively fade to avoid unstable far-field samples.");
			if (ImGui::SliderFloat2("##depth_fade", &settings.DepthFadeRange.x, 1e4f, 5e4f, "%.0f units")) {
				settings.DepthFadeRange.y = std::clamp(settings.DepthFadeRange.y, 1.01e4f, 5e4f);
				settings.DepthFadeRange.x = std::clamp(settings.DepthFadeRange.x, 1e4f, settings.DepthFadeRange.y - 100.0f);
			}
			ImGui::EndTable();
		}
	}

	// -------------------------------------------------------------------------
	// WORLD-SPACE GI
	// -------------------------------------------------------------------------
	PIXLUI::SectionBanner("WORLD GI");
	ImGui::TextWrapped("The world cache preserves useful lighting outside the current screen view, improving continuity while you turn the camera or move through a scene.");
	{
		auto worldGuard = Util::DisableGuard(!settings.Enabled || !settings.EnableGI);
		DrawPixlToggleField("World Irradiance Cache", &settings.EnableWorldCache);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Stores recently observed scene lighting in two world-space voxel cascades. It augments screen-space GI; unseen geometry is still unknown until observed.");

		{
			auto cacheGuard = Util::DisableGuard(!settings.EnableWorldCache);
			if (BeginSettingsTable("PIXL GI World Cache")) {
				BeginSettingRow("World GI Contribution", "Strength of lighting reconstructed from the persistent world cache. Screen-space GI remains the high-detail primary source.");
				ImGui::SliderFloat("##world_strength", &settings.WorldCacheStrength, 0.0f, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

				BeginSettingRow("World GI Coverage", "Maximum useful world-space radius around the camera. Increasing this extends continuity but makes the fixed cache represent a larger region.");
				ImGui::SliderFloat("##world_radius", &settings.WorldCacheRadius, 256.0f, 4096.0f, "%.0f units", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);

				BeginSettingRow("Secondary Bounce", "Propagates a bounded amount of cached irradiance into neighbouring cells, carrying light farther around corners.");
				PIXLUI::Toggle("##second_bounce", &settings.EnableWorldCacheSecondBounce);

				BeginSettingRow("Secondary Bounce Strength", "Controls how much previous cached lighting is allowed to propagate. Keep this conservative to avoid energy growth.");
				{
					auto bounceGuard = Util::DisableGuard(!settings.EnableWorldCacheSecondBounce);
					ImGui::SliderFloat("##second_bounce_strength", &settings.WorldCacheSecondBounceStrength, 0.0f, 0.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				}

				BeginSettingRow("Directional Occlusion", "Uses voxel occupancy to reduce cached light arriving through blocked directions, improving depth and reducing light leakage.");
				PIXLUI::Toggle("##directional_occlusion", &settings.EnableDirectionalOcclusion);

				BeginSettingRow("Occlusion Strength", "How strongly world-cache occupancy suppresses blocked indirect-light directions.");
				{
					auto occGuard = Util::DisableGuard(!settings.EnableDirectionalOcclusion);
					ImGui::SliderFloat("##directional_occlusion_strength", &settings.DirectionalOcclusionStrength, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				}
				ImGui::EndTable();
			}
		}
	}

	// -------------------------------------------------------------------------
	// VISIBILITY + REFLECTIONS
	// -------------------------------------------------------------------------
	PIXLUI::SectionBanner("VISIBILITY & REFLECTIONS");
	ImGui::TextWrapped("Bent-normal visibility reuses the GI horizon data to stop ambient and rough reflection energy from arriving through blocked directions. Hybrid Reflections adds stochastic GGX screen-space transport with world-cache fallback.");
	{
		auto guard = Util::DisableGuard(!settings.Enabled);
		if (BeginSettingsTable("PIXL GI Visibility")) {
			BeginSettingRow("Bent-Normal Lighting", "Bends ambient/environment sampling toward the visible hemisphere. Most useful in deep corners, under pipes and beside large occluders.");
			PIXLUI::Toggle("##bent_normals", &settings.EnableBentNormalLighting);
			BeginSettingRow("Bent-Normal Strength", "0 keeps the geometric normal. 1 fully follows the visibility direction reconstructed by the horizon tracer.");
			{ auto g = Util::DisableGuard(!settings.EnableBentNormalLighting); ImGui::SliderFloat("##bent_strength", &settings.BentNormalStrength, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp); }
			BeginSettingRow("Physical Specular Occlusion", "Suppresses broad environment-reflection lobes when their visible hemisphere is blocked. Sharp mirror reflections remain much less affected than rough reflections.");
			PIXLUI::Toggle("##spec_occ", &settings.EnableSpecularOcclusion);
			BeginSettingRow("Specular Occlusion Strength", "Controls how strongly directional visibility affects rough AmbientProbe and reflection energy.");
			{ auto g = Util::DisableGuard(!settings.EnableSpecularOcclusion); ImGui::SliderFloat("##spec_occ_strength", &settings.SpecularOcclusionStrength, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp); }
			ImGui::EndTable();
		}

		ImGui::Spacing();
		recompileFlag |= DrawPixlToggleField("Hybrid Reflections", &settings.EnableExperimentalSpecularGI);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Stochastic GGX reflections traced through the hierarchical depth and radiance buffers. Screen hits are temporally reconstructed; misses can use the persistent world cache before the normal AmbientProbe/cubemap fallback. Changing this automatically rebuilds the reflection compute pipeline.");

		{
			auto reflectionGuard = Util::DisableGuard(!settings.EnableExperimentalSpecularGI);
			if (BeginSettingsTable("PIXL Hybrid Reflections")) {
				BeginSettingRow("Reflection Strength", "Overall contribution of scene-aware hybrid reflection transport.");
				ImGui::SliderFloat("##hybrid_reflection_strength", &settings.ReflectionIntensity, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				BeginSettingRow("Maximum Roughness", "Surfaces rougher than this use normal AmbientProbe/cubemaps only, avoiding low-value stochastic noise.");
				ImGui::SliderFloat("##hybrid_max_roughness", &settings.ReflectionMaxRoughness, 0.20f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				BeginSettingRow("Off-Screen World Fallback", "Lets the persistent world cache contribute when a reflection leaves the screen or has no reliable hit.");
				PIXLUI::Toggle("##world_reflection_fallback", &settings.EnableVoxelReflections);
				BeginSettingRow("Fallback Strength", "Strength of the coarse world-cache reflection fallback. Best on rough metal/glossy stone rather than perfect mirrors.");
				{ auto g = Util::DisableGuard(!settings.EnableVoxelReflections || !settings.EnableWorldCache); ImGui::SliderFloat("##world_reflection_strength", &settings.VoxelReflectionStrength, 0.0f, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp); }
				ImGui::EndTable();
			}
		}

		if (showAdvanced && settings.EnableExperimentalSpecularGI && ImGui::CollapsingHeader("Reflection tracing details")) {
			ImGui::TextDisabled("Advanced Reflection Tracing");
			if (BeginSettingsTable("PIXL Hybrid Reflections Advanced")) {
				BeginSettingRow("Trace Distance", "Maximum view-space distance a screen-space reflection ray can travel before falling back.");
				ImGui::SliderFloat("##reflection_distance", &settings.ReflectionMaxDistance, 256.0f, 8192.0f, "%.0f units", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
				BeginSettingRow("Trace Steps", "Maximum hierarchical depth tests per stochastic reflection ray.");
				Util::UIntSlider("##reflection_steps", &settings.ReflectionSteps, 8, 64, "%d", ImGuiSliderFlags_AlwaysClamp);
				BeginSettingRow("Surface Thickness", "Depth tolerance for accepting a screen-space intersection.");
				ImGui::SliderFloat("##reflection_thickness", &settings.ReflectionThickness, 2.0f, 96.0f, "%.1f units", ImGuiSliderFlags_AlwaysClamp);
				BeginSettingRow("Temporal Response", "How quickly stochastic history accepts new reflection samples. Lower is steadier; higher reacts faster.");
				ImGui::SliderFloat("##reflection_temporal", &settings.ReflectionTemporalResponse, 0.04f, 0.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				BeginSettingRow("Firefly Limit", "Soft luminance knee for isolated reflection outliers before temporal accumulation.");
				ImGui::SliderFloat("##reflection_firefly", &settings.ReflectionFireflyClamp, 1.0f, 32.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
				BeginSettingRow("World Fallback Blend", "Confidence multiplier for world-cache miss fill before deferred cubemap/AmbientProbe remains in charge.");
				ImGui::SliderFloat("##reflection_world_mix", &settings.ReflectionWorldFallbackStrength, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				BeginSettingRow("World Fallback Roughness Limit", "Upper roughness limit for the coarse world-cache fallback. Mirror-like surfaces are suppressed automatically because low-frequency SH cannot reproduce sharp reflections.");
				ImGui::SliderFloat("##reflection_world_roughness_cutoff", &settings.VoxelReflectionRoughnessCutoff, 0.20f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				BeginSettingRow("Ray Bias", "Offsets reflection rays away from the receiving surface to reduce self-intersections.");
				ImGui::SliderFloat("##reflection_bias", &settings.ReflectionRayBias, 1.0f, 24.0f, "%.1f units", ImGuiSliderFlags_AlwaysClamp);
				BeginSettingRow("Roughness Stochasticity", "0 approaches a stable mirror direction; 1 samples the full GGX visible-normal distribution.");
				ImGui::SliderFloat("##reflection_jitter", &settings.ReflectionRoughnessJitter, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				ImGui::EndTable();
			}
		}
	}

	// -------------------------------------------------------------------------
	// TEMPORAL STABILITY / DENOISING
	// -------------------------------------------------------------------------
	PIXLUI::SectionBanner("STABILITY & DENOISING");
	{
		auto guard = Util::DisableGuard(!settings.Enabled);
		if (ImGui::BeginTable("PIXL GI Denoiser Toggles", 3, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
			ImGui::TableNextColumn();
			const bool temporalChanged = DrawPixlToggleField("Temporal Accumulation", &settings.EnableTemporalDenoiser);
			recompileFlag |= temporalChanged;
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::TextWrapped("Reprojects stable GI/reflection history using depth, world-position and normal rejection. Changing this automatically rebuilds the affected compute permutations and clears incompatible history.");
			ImGui::TableNextColumn();
			DrawPixlToggleField("Spatial Denoiser", &settings.EnableBlur);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::TextWrapped("Edge-aware spatial reconstruction for diffuse GI and stochastic Hybrid Reflections. Mirror-like reflections stay intentionally sharp.");
			ImGui::TableNextColumn();
			{
				auto adaptiveGuard = Util::DisableGuard(!settings.EnableBlur);
				DrawPixlToggleField("Adaptive 2-Pass", &settings.EnableAdaptiveDenoiser);
			}
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::TextWrapped("Runs a second edge-aware spatial pass where geometric confidence is low. Applies immediately without a shader rebuild.");
			ImGui::EndTable();
		}

		if (BeginSettingsTable("PIXL GI Stability")) {
			BeginSettingRow("History Length", "Maximum number of frames accumulated. Higher values reduce noise but can increase persistence/ghosting after rapid changes.");
			{
				auto temporalGuard = Util::DisableGuard(!settings.EnableTemporalDenoiser);
				Util::UIntSlider("##history_frames", &settings.MaxAccumFrames, 1, 64, "%d frames", ImGuiSliderFlags_AlwaysClamp);
			}

			BeginSettingRow("Depth Rejection", "Rejects temporal history when reprojected depth changes too much. Lower values are stricter and reduce trails at geometry edges.");
			{
				auto temporalGuard = Util::DisableGuard(!settings.EnableTemporalDenoiser);
				Util::PercentageSlider("##depth_rejection", &settings.DepthDisocclusion, 0.0f, 20.0f);
			}

			BeginSettingRow("Normal Rejection", "Rejects history when surface orientation changes. This is especially useful on animated geometry, thin silhouettes and changing normals.");
			{
				auto temporalGuard = Util::DisableGuard(!settings.EnableTemporalDenoiser);
				Util::PercentageSlider("##normal_rejection", &settings.NormalDisocclusion, 0.0f, 100.0f);
			}

			BeginSettingRow("Denoise Radius", "Spatial blur radius in pixels. Larger values hide more noise but can soften small indirect-light detail.");
			{
				auto blurGuard = Util::DisableGuard(!settings.EnableBlur);
				ImGui::SliderFloat("##blur_radius", &settings.BlurRadius, 0.0f, 30.0f, "%.1f px", ImGuiSliderFlags_AlwaysClamp);
			}
			ImGui::EndTable();
		}
	}

	// -------------------------------------------------------------------------
	// ADVANCED
	// -------------------------------------------------------------------------
	if (showAdvanced && ImGui::CollapsingHeader("Sampling & cache quality")) {
		ImGui::TextWrapped("These controls trade GPU cost, cache responsiveness and reconstruction stability. Presets manage them automatically for most users.");
		{
			auto guard = Util::DisableGuard(!settings.Enabled);

			if (ImGui::CollapsingHeader("Screen-Space Sampling", ImGuiTreeNodeFlags_DefaultOpen)) {
				if (BeginSettingsTable("PIXL GI Advanced Screen")) {
					BeginSettingRow("Internal Resolution", "Full resolution is cleanest and most expensive. Half is the recommended default; quarter is intended for performance-constrained systems.");
					static constexpr const char* resolutionNames[] = { "Full", "Half", "Quarter" };
					if (ImGui::Combo("##resolution_mode", &settings.ResolutionMode, resolutionNames, static_cast<int>(std::size(resolutionNames))))
						recompileFlag = true;

					BeginSettingRow("Sample Directions", "Number of angular directions traced per pixel. More directions reduce directional noise but increase GI cost.");
					Util::UIntSlider("##num_slices", &settings.NumSlices, 1, 10, "%d", ImGuiSliderFlags_AlwaysClamp);

					BeginSettingRow("Steps Per Direction", "Number of horizon samples along each direction. More steps improve spatial accuracy and large-radius stability.");
					Util::UIntSlider("##num_steps", &settings.NumSteps, 1, 20, "%d", ImGuiSliderFlags_AlwaysClamp);

					BeginSettingRow("Experimental Adaptive Ray Tiles", "Compiles an experimental 8x8 tile classifier that reduces horizon directions and steps only in stable, low-frequency tiles. Disabled by default pending live image-quality and GPU-profiler validation.");
					recompileFlag |= DrawPixlToggleField("##adaptive_ray_tiles", &settings.EnableAdaptiveRayAllocation);

					BeginSettingRow("Adaptive Minimum Work", "Lowest fraction of configured directions and steps retained in a stable tile. This is applied independently to both loop dimensions; 50% can therefore approach one quarter of the horizon samples in qualifying tiles.");
					{
						auto adaptiveGuard = Util::DisableGuard(!settings.EnableAdaptiveRayAllocation);
						ImGui::SliderFloat("##adaptive_ray_minimum", &settings.AdaptiveRayMinimum, 0.35f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
					}

					BeginSettingRow("Minimum Screen Radius", "Prevents the far-field effect radius collapsing below this fraction of the display width.");
					ImGui::SliderFloat("##min_screen_radius", &settings.MinScreenRadius, 0.0f, 0.05f, "%.3f", ImGuiSliderFlags_AlwaysClamp);

					BeginSettingRow("AO Thickness", "Virtual occluder thickness used by the AO horizon test. Larger values can make thin geometry occlude more aggressively.");
					ImGui::SliderFloat("##thickness", &settings.Thickness, 0.0f, 128.0f, "%.1f units", ImGuiSliderFlags_AlwaysClamp);

					BeginSettingRow("GI Distance Compensation", "Biases distant radiance samples brighter or darker. Zero is neutral and recommended unless tuning a specific look.");
					ImGui::SliderFloat("##gi_distance_comp", &settings.GIDistanceCompensation, -5.0f, 5.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

					BeginSettingRow("Geometry Blur Weight", "Higher values make the spatial denoiser more sensitive to geometry differences and preserve edges more strongly.");
					ImGui::SliderFloat("##distance_normalisation", &settings.DistanceNormalisation, 0.0f, 5.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

					BeginSettingRow("Radiance Firefly Limit", "Soft-limits isolated radiance spikes before they enter the persistent world cache. Raise only if very bright emissive bounce looks unnecessarily compressed.");
					ImGui::SliderFloat("##radiance_firefly_limit", &settings.RadianceFireflyClamp, 0.0f, 32.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);

					BeginSettingRow("Upsample Edge Sensitivity", "Depth discontinuity threshold used when reconstructing half/quarter-resolution GI. Lower values preserve thin geometry more aggressively; higher values blend more smoothly.");
					ImGui::SliderFloat("##upsample_edge_threshold", &settings.UpsampleEdgeThreshold, 0.02f, 0.35f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
					ImGui::EndTable();
				}
			}

			if (ImGui::CollapsingHeader("World Cache Quality", ImGuiTreeNodeFlags_DefaultOpen)) {
				auto cacheGuard = Util::DisableGuard(!settings.EnableWorldCache);
				if (BeginSettingsTable("PIXL GI Advanced Cache")) {
					BeginSettingRow("World Samples", "Number of cache samples used during world-space reconstruction. Higher values reduce cache noise at additional cost.");
					Util::UIntSlider("##world_samples", &settings.WorldCacheSampleCount, 1, 8, "%d", ImGuiSliderFlags_AlwaysClamp);

					BeginSettingRow("Cone Trace Steps", "Maximum voxel steps used when sampling world irradiance/occlusion. Higher values see farther through the cache but cost more.");
					Util::UIntSlider("##trace_steps", &settings.WorldCacheTraceSteps, 2, 6, "%d", ImGuiSliderFlags_AlwaysClamp);

					BeginSettingRow("Cache Lifetime", "Maximum age of a voxel before it is considered stale. Longer lifetimes preserve off-screen lighting; shorter lifetimes react faster to scene changes.");
					Util::UIntSlider("##cache_age", &settings.WorldCacheMaxAge, 1, 120, "%d frames", ImGuiSliderFlags_AlwaysClamp);

					BeginSettingRow("Near Voxel Size", "World-space size of high-detail cache cells around the camera. Smaller cells preserve more detail but cover less space.");
					ImGui::SliderFloat("##near_voxel", &settings.WorldCacheCellSizeNear, 64.0f, 256.0f, "%.0f units", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);

					BeginSettingRow("Far Voxel Size", "World-space size of the lower-frequency cache cascade. It is clamped to at least the near-cell size during upload.");
					ImGui::SliderFloat("##far_voxel", &settings.WorldCacheCellSizeFar, 256.0f, 1024.0f, "%.0f units", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);

					BeginSettingRow("Leak Rejection", "Rejects cache samples that are likely to cross geometry boundaries. Higher values reduce light leaks but can discard useful indirect light.");
					ImGui::SliderFloat("##leak_reduction", &settings.WorldCacheLeakReduction, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

					BeginSettingRow("Cache Update Speed", "How quickly an existing voxel accepts newly observed lighting. Lower values are steadier; higher values respond faster to real lighting changes.");
					ImGui::SliderFloat("##cache_response", &settings.WorldCacheTemporalResponse, 0.02f, 1.0f, "%.2f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);

					BeginSettingRow("Injection Spacing", "Pixel stride used when injecting visible surfaces into the world cache. Smaller values update more pixels and cost more GPU time.");
					Util::UIntSlider("##injection_stride", &settings.WorldCacheInjectionStride, 1, 8, "%d px", ImGuiSliderFlags_AlwaysClamp);
					ImGui::EndTable();
				}
			}
		}
	}

	// -------------------------------------------------------------------------
	// DIAGNOSTICS
	// -------------------------------------------------------------------------
	if (globals::state->IsDeveloperMode()) {
		ImGui::SeparatorText("Diagnostics");
	}
	if (globals::state->IsDeveloperMode() && ImGui::CollapsingHeader("Developer & Debug Tools")) {
		static constexpr const char* debugViews[] = {
			"Composite",
			"AO Occlusion",
			"Diffuse GI",
			"Specular GI",
			"Lighting Input Radiance",
			"Voxel Confidence / Coverage",
			"Voxel Cascade",
			"Cached Irradiance",
			"Voxel Directional Occlusion",
			"Voxel Reflection Fallback",
			"Bent Normal",
			"Directional Visibility",
			"Physical Specular Occlusion",
			"Hybrid Reflections"
		};

		if (BeginSettingsTable("PIXL GI Debug")) {
			BeginSettingRow("Full-Screen View", "Visualizes a single stage of the GI pipeline. Composite returns to normal gameplay output.");
			int debugView = static_cast<int>(settings.DebugView);
			if (ImGui::Combo("##debug_view", &debugView, debugViews, static_cast<int>(std::size(debugViews))))
				settings.DebugView = static_cast<uint>(std::clamp(debugView, 0, static_cast<int>(std::size(debugViews)) - 1));

			BeginSettingRow("Debug Exposure", "Display-only exposure multiplier used by diagnostic visualizations.");
			ImGui::SliderFloat("##debug_gain", &settings.DebugGain, 0.1f, 16.0f, "%.2fx", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
			ImGui::EndTable();
		}

		if (ImGui::Button("Capture PIXL GI Diagnostic Set", { -1.0f, 0.0f }))
			RequestDiagnosticCapture();
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Temporarily closes the menu, captures the presented GI/PBR diagnostic views after a short settle period, then writes screenshots plus a manifest, settings snapshot, shader hashes, GPU timings and log snapshot under PIXL/Diagnostics/Captures. Late transparencies and HUD elements are explicitly identified as presentation overlays.");

		if (diagnosticCaptureActive || diagnosticCaptureRequested.load(std::memory_order_acquire))
			ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive), "Diagnostic capture active - keep the camera still");

		if (ImGui::TreeNode(T(TKEY("buffer_viewer"), "Buffer Viewer"))) {
			static float debugRescale = 0.3f;
			ImGui::SliderFloat(T(TKEY("view_resize"), "View Resize"), &debugRescale, 0.05f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

			BUFFER_VIEWER_NODE(texNoise, debugRescale)
			BUFFER_VIEWER_NODE(texWorkingDepth, debugRescale)
			BUFFER_VIEWER_NODE(texPrevGeo, debugRescale)
			BUFFER_VIEWER_NODE(texRadiance, debugRescale)
			BUFFER_VIEWER_NODE(texAo[0], debugRescale)
			BUFFER_VIEWER_NODE(texAo[1], debugRescale)
			BUFFER_VIEWER_NODE(texIlY[0], debugRescale)
			BUFFER_VIEWER_NODE(texIlY[1], debugRescale)
			BUFFER_VIEWER_NODE(texIlCoCg[0], debugRescale)
			BUFFER_VIEWER_NODE(texIlCoCg[1], debugRescale)
			BUFFER_VIEWER_NODE(texGiSpecular[0], debugRescale)
			BUFFER_VIEWER_NODE(texGiSpecular[1], debugRescale)
			BUFFER_VIEWER_NODE(texBentVisibility[0], debugRescale)
			BUFFER_VIEWER_NODE(texBentVisibility[1], debugRescale)
			BUFFER_VIEWER_NODE(texWorldCacheMetadata, debugRescale)
			BUFFER_VIEWER_NODE(texWorldCacheSH0, debugRescale)
			BUFFER_VIEWER_NODE(texWorldCacheSH1, debugRescale)
			BUFFER_VIEWER_NODE(texWorldCacheSH2, debugRescale)
			BUFFER_VIEWER_NODE(texWorldCacheNormal, debugRescale)

			ImGui::TreePop();
		}
	}
}

void HybridGI::RequestDiagnosticCapture()
{
	if (diagnosticCaptureActive || diagnosticCaptureRequested.exchange(true, std::memory_order_acq_rel))
		return;
	logger::info("[PIXL Hybrid GI] diagnostic capture requested");
}

void HybridGI::UpdateDiagnosticCapture()
{
	if (!diagnosticCaptureActive) {
		if (!diagnosticCaptureRequested.exchange(false, std::memory_order_acq_rel))
			return;

		diagnosticCaptureDirectory = Util::PathHelpers::GetDiagnosticsPath() /
		                             std::format("PIXL-HybridGI-{}", PixlDiagnosticTimestamp());
		std::error_code error;
		std::filesystem::create_directories(diagnosticCaptureDirectory, error);
		if (error) {
			logger::error("[PIXL Hybrid GI] failed to create diagnostic directory: {}", error.message());
			NotifyDiagnostic("PIXL GI dump failed - see PIXLRenderer.log");
			return;
		}

		diagnosticSavedMenuEnabled = Menu::GetSingleton()->IsEnabled;
		diagnosticSavedDebugView = settings.DebugView;
		diagnosticSavedMaterialDebugMode = globals::pipeline::materialForge.settings.LegacyPhysicalDebugMode;
		Menu::GetSingleton()->IsEnabled = false;
		diagnosticCaptureIndex = 0;
		diagnosticRecords.clear();
		diagnosticCaptureActive = true;
		diagnosticWaitingForScreenshot = false;
		ApplyDiagnosticView(0);
		logger::info("[PIXL Hybrid GI] diagnostic capture started at {}", diagnosticCaptureDirectory.string());
		NotifyDiagnostic("PIXL GI dump started - keep the camera still");
		return;
	}

	if (diagnosticWaitingForScreenshot) {
		++diagnosticWaitFrames;
		const bool encoderBusy = globals::pipeline::pixelCapture.GetPendingCaptureCount() != 0;
		std::error_code existsError;
		const bool screenshotExists = std::filesystem::exists(diagnosticCurrentScreenshot, existsError);
		if (encoderBusy || !screenshotExists) {
			if (diagnosticWaitFrames < 900)
				return;
			logger::error("[PIXL Hybrid GI] timed out waiting for diagnostic screenshot {}", diagnosticCurrentScreenshot.string());
			FinishDiagnosticCapture(false);
			return;
		}

		diagnosticWaitingForScreenshot = false;
		++diagnosticCaptureIndex;
		if (diagnosticCaptureIndex >= PixlDiagnosticViews.size()) {
			FinishDiagnosticCapture(true);
			return;
		}
		ApplyDiagnosticView(diagnosticCaptureIndex);
		return;
	}

	if (diagnosticSettleFrames != 0) {
		--diagnosticSettleFrames;
		return;
	}

	const auto& view = PixlDiagnosticViews.at(diagnosticCaptureIndex);
	diagnosticCurrentScreenshot = diagnosticCaptureDirectory / std::format("{}.png", view.name);
	globals::pipeline::pixelCapture.RequestCaptureToPath(diagnosticCurrentScreenshot, false);
	diagnosticRecords.push_back(DiagnosticRecord{
		std::string(view.name),
		diagnosticCurrentScreenshot.filename(),
		view.giDebugView,
		view.materialDebugMode,
		globals::state->frameCount });
	diagnosticWaitingForScreenshot = true;
	diagnosticWaitFrames = 0;
}

void HybridGI::ApplyDiagnosticView(size_t index)
{
	const auto& view = PixlDiagnosticViews.at(index);
	settings.DebugView = view.giDebugView;
	globals::pipeline::materialForge.settings.LegacyPhysicalDebugMode = view.materialDebugMode;
	diagnosticSettleFrames = 4;
	diagnosticWaitFrames = 0;
	diagnosticWaitingForScreenshot = false;
	logger::info("[PIXL Hybrid GI] diagnostic view {}/{}: {}",
		index + 1,
		PixlDiagnosticViews.size(),
		view.name);
}

void HybridGI::FinishDiagnosticCapture(bool complete)
{
	settings.DebugView = diagnosticSavedDebugView;
	globals::pipeline::materialForge.settings.LegacyPhysicalDebugMode = diagnosticSavedMaterialDebugMode;
	Menu::GetSingleton()->IsEnabled = diagnosticSavedMenuEnabled;
	diagnosticWaitingForScreenshot = false;
	diagnosticCaptureActive = false;
	// Diagnostic SH/material colours are visualization data, never reusable GI
	// history. Preserve the world cache itself and invalidate temporal sampling.
	queuedResetTemporalHistory = true;

	std::error_code copyError;
	const auto settingsPath = Util::PathHelpers::GetSettingsUserPath();
	if (!settingsPath.empty() && std::filesystem::exists(settingsPath))
		std::filesystem::copy_file(settingsPath, diagnosticCaptureDirectory / "SettingsUser.json", std::filesystem::copy_options::overwrite_existing, copyError);
	copyError.clear();
	const auto logPath = Util::PathHelpers::GetLogPath();
	if (!logPath.empty() && std::filesystem::exists(logPath))
		std::filesystem::copy_file(logPath, diagnosticCaptureDirectory / "PIXLRenderer.log", std::filesystem::copy_options::overwrite_existing, copyError);

	WriteDiagnosticManifest(complete);
	if (complete)
		logger::info("[PIXL Hybrid GI] diagnostic capture completed at {}", diagnosticCaptureDirectory.string());
	else
		logger::warn("[PIXL Hybrid GI] diagnostic capture failed at {}", diagnosticCaptureDirectory.string());
	NotifyDiagnostic(complete ?
		std::format("PIXL GI dump complete: {}", diagnosticCaptureDirectory.filename().string()) :
		"PIXL GI dump incomplete - see PIXLRenderer.log");
}

void HybridGI::WriteDiagnosticManifest(bool complete) const
{
	json root;
	root["schemaVersion"] = 3;
	root["product"] = "PIXL PBR Rendering Engine";
	root["capture"] = {
		{ "complete", complete },
		{ "frameIndex", globals::state->frameCount },
		{ "viewCount", diagnosticRecords.size() },
		{ "expectedViewCount", PixlDiagnosticViews.size() },
		{ "menuRestored", Menu::GetSingleton()->IsEnabled == diagnosticSavedMenuEnabled },
		{ "captureStage", "presented-framebuffer" },
		{ "opaqueDebugIsolation", true },
		{ "lateTransparencyIsolation", false },
		{ "hudIsolation", false },
		{ "interpretation", "Each PNG contains an opaque PIXL debug visualization followed by Skyrim's late transparency/effect and HUD presentation passes. Repeated smoke or UI is overlay contamination, not data stored in every inspected buffer." }
	};
	root["settings"]["hybridGI"] = settings;
	const auto& reconstruction = globals::pipeline::imageReconstruction;
	const auto& camera = globals::pipeline::cameraSuite;
	const auto& menuSettings = Menu::GetSingleton()->GetSettings();
	root["settings"]["quality"] = {
		{ "renderer", menuSettings.RendererQuality },
		{ "lighting", menuSettings.LightingQuality },
		{ "materials", menuSettings.MaterialsQuality },
		{ "atmosphere", menuSettings.AtmosphereQuality },
		{ "water", menuSettings.WaterQuality },
		{ "terrainVegetation", menuSettings.TerrainVegetationQuality },
		{ "characters", menuSettings.CharactersQuality },
		{ "camera", menuSettings.CameraQuality }
	};
	root["settings"]["camera"] = {
		{ "physicalCamera", camera.settings.enablePhysicalCamera },
		{ "cameraInfluence", camera.settings.cameraInfluence },
		{ "autoExposure", camera.settings.cameraAutoExposure },
		{ "exposureCompensationEV", camera.settings.cameraExposureCompensationEV },
		{ "bloom", camera.settings.enableBloom },
		{ "bloomStrength", camera.settings.bloomStrength },
		{ "bloomThreshold", camera.settings.bloomThreshold },
		{ "bloomRadius", camera.settings.bloomRadius },
		{ "stormglass", camera.settings.enableStormglass },
		{ "stormglassStrength", camera.settings.stormglassStrength },
		{ "stormglassDropScale", camera.settings.stormglassDropScale },
		{ "stormglassRefraction", camera.settings.stormglassRefraction },
		{ "stormglassTrails", camera.settings.stormglassTrails },
		{ "stormglassDryingRate", camera.settings.stormglassDryingRate },
		{ "submergedOptics", camera.settings.enableSubmergedOptics },
		{ "submergedStrength", camera.settings.submergedStrength },
		{ "submergedBlur", camera.settings.submergedBlur },
		{ "submergedRefraction", camera.settings.submergedRefraction },
		{ "submergedTransitionSpeed", camera.settings.submergedTransitionSpeed },
		{ "depthOfField", camera.settings.enableEnhancedDepthOfField },
		{ "lutPreset", camera.settings.lookPreset },
		{ "lutOpacity", camera.settings.lookOpacity }
	};
	root["settings"]["physicalPBR"] = {
		{ "featureLoaded", globals::pipeline::materialForge.loaded },
		{ "settingsBytes", sizeof(MaterialForge::Settings) },
		{ "vertexAOStrength", globals::pipeline::materialForge.settings.VertexAOStrength },
		{ "legacyPhysicalDirectLighting", globals::pipeline::materialForge.settings.EnableLegacyPhysicalDirectLighting },
		{ "legacyPhysicalSpecularScale", globals::pipeline::materialForge.settings.LegacyPhysicalSpecularScale },
		{ "legacyMetalInference", globals::pipeline::materialForge.settings.EnableLegacyMetalInference },
		{ "legacyMetalInferenceStrength", globals::pipeline::materialForge.settings.LegacyMetalInferenceStrength },
		{ "legacyMetalInferenceThreshold", globals::pipeline::materialForge.settings.LegacyMetalInferenceThreshold },
		{ "legacyMetalInferenceMaximum", globals::pipeline::materialForge.settings.LegacyMetalInferenceMaximum },
		{ "physicalLocalLightFalloff", globals::pipeline::materialForge.settings.EnablePhysicalLocalLightFalloff },
		{ "physicalLocalLightFalloffStrength", globals::pipeline::materialForge.settings.PhysicalLocalLightFalloffStrength },
		{ "localLightMinimumDistance", globals::pipeline::materialForge.settings.LocalLightMinimumDistance },
		{ "localContactShadows", globals::pipeline::materialForge.settings.EnableLocalContactShadows },
		{ "localContactShadowLightCount", globals::pipeline::materialForge.settings.LocalContactShadowLightCount },
		{ "localContactShadowLength", globals::pipeline::materialForge.settings.LocalContactShadowLength },
		{ "localContactShadowStrength", globals::pipeline::materialForge.settings.LocalContactShadowStrength },
		{ "specularAA", globals::pipeline::materialForge.settings.EnableSpecularAA },
		{ "specularAAStrength", globals::pipeline::materialForge.settings.SpecularAAStrength },
		{ "specularAAVarianceClamp", globals::pipeline::materialForge.settings.SpecularAAVarianceClamp },
		{ "ggxMultiScatter", globals::pipeline::materialForge.settings.EnableGGXMultiScatter },
		{ "ggxMultiScatterStrength", globals::pipeline::materialForge.settings.GGXMultiScatterStrength }
	};
	root["runtime"] = {
		{ "featureLoaded", loaded },
		{ "shadersOK", ShadersOK() },
		{ "diffuseGIAlwaysBoundWithHQSpecular", true },
		{ "cacheConfidenceMaturation", true },
		{ "asymmetricBrightOutlierRejection", true },
		{ "visualizationInjectionIsolated", true },
		{ "temporalHistoryResetAfterVisualization", true },
		{ "effectiveDepthFadeNear", std::clamp(settings.DepthFadeRange.x, 1e4f, std::clamp(settings.DepthFadeRange.y, 1.01e4f, 5e4f) - 100.f) },
		{ "effectiveDepthFadeFar", std::clamp(settings.DepthFadeRange.y, 1.01e4f, 5e4f) },
		{ "outputAOIndex", outputAoIdx },
		{ "outputGIIndex", outputIlIdx },
		{ "screenWidth", globals::game::graphicsState->screenWidth },
		{ "screenHeight", globals::game::graphicsState->screenHeight },
		{ "worldVoxelAtlasWidth", texWorldCacheMetadata ? texWorldCacheMetadata->desc.Width : 0 },
		{ "worldVoxelAtlasHeight", texWorldCacheMetadata ? texWorldCacheMetadata->desc.Height : 0 },
		{ "worldVoxelSHOrder", 2 },
		{ "worldVoxelSHCoefficients", 9 },
		{ "horizonMaskBins", 64 },
		{ "horizonMaskStorage", "uint2-sm5" },
		{ "worldVoxelStorageBytes", 4194304 },
		{ "worldVoxelSnapshotCopyBytesPerFrame", 2097152 },
		{ "worldCacheSecondBounce", settings.EnableWorldCacheSecondBounce },
		{ "worldCacheSecondBounceStrength", settings.WorldCacheSecondBounceStrength },
		{ "geometryAwareTemporalReprojection", true },
		{ "materialFresnelSpecularGI", true },
		{ "effectLightingScaleAppliedWithoutLinearLighting", true },
		{ "effectLightingScale", globals::pipeline::linearLightCore.settings.effectLightingMult },
		{ "linearLightingEnabled", globals::pipeline::linearLightCore.settings.enableLinearLightCore != 0u }
	};
	const float renderScaleX = std::clamp(reconstruction.resolutionScale.x, 0.0f, 1.0f);
	const float renderScaleY = std::clamp(reconstruction.resolutionScale.y, 0.0f, 1.0f);
	root["reconstruction"] = {
		{ "method", static_cast<uint32_t>(reconstruction.GetUpscaleMethod()) },
		{ "qualityMode", reconstruction.settings.qualityMode },
		{ "renderScaleX", renderScaleX },
		{ "renderScaleY", renderScaleY },
		{ "estimatedRenderWidth", static_cast<uint32_t>(std::lround(globals::game::graphicsState->screenWidth * renderScaleX)) },
		{ "estimatedRenderHeight", static_cast<uint32_t>(std::lround(globals::game::graphicsState->screenHeight * renderScaleY)) },
		{ "frameGenerationRequested", reconstruction.settings.frameGenerationMode != 0u },
		{ "frameGenerationInteropActive", reconstruction.IsFrameGenerationDx12PathActive() },
		{ "frameGenerationProducingFrames", reconstruction.IsFrameGenerationActive() },
		{ "dynamicResolutionWidthRatio", reconstruction.dynamicResolutionWidthRatio },
		{ "dynamicResolutionHeightRatio", reconstruction.dynamicResolutionHeightRatio },
		{ "jitterX", reconstruction.jitter.x },
		{ "jitterY", reconstruction.jitter.y }
	};

	auto recordBuffer = [&root](std::string_view name, const Texture2D* texture) {
		json entry{
			{ "name", name },
			{ "resident", texture && texture->resource }
		};
		if (texture && texture->resource) {
			entry["width"] = texture->desc.Width;
			entry["height"] = texture->desc.Height;
			entry["mipLevels"] = texture->desc.MipLevels;
			entry["arraySize"] = texture->desc.ArraySize;
			entry["format"] = static_cast<uint32_t>(texture->desc.Format);
			entry["bindFlags"] = texture->desc.BindFlags;
		}
		root["buffers"].push_back(std::move(entry));
	};
	recordBuffer("workingDepth", texWorkingDepth.get());
	recordBuffer("previousGeometry", texPrevGeo.get());
	recordBuffer("radiance", texRadiance.get());
	recordBuffer("radianceScratch", texRadianceTemp.get());
	for (size_t index = 0; index < 2; ++index) {
		recordBuffer(std::format("accumulationHistory{}", index), texAccumFrames[index].get());
		recordBuffer(std::format("ambientOcclusion{}", index), texAo[index].get());
		recordBuffer(std::format("diffuseLuminance{}", index), texIlY[index].get());
		recordBuffer(std::format("diffuseChroma{}", index), texIlCoCg[index].get());
		recordBuffer(std::format("specularRadiance{}", index), texGiSpecular[index].get());
		recordBuffer(std::format("bentVisibility{}", index), texBentVisibility[index].get());
	}
	recordBuffer("worldMetadata", texWorldCacheMetadata.get());
	recordBuffer("worldSH0", texWorldCacheSH0.get());
	recordBuffer("worldSH1", texWorldCacheSH1.get());
	recordBuffer("worldSH2", texWorldCacheSH2.get());
	recordBuffer("worldNormal", texWorldCacheNormal.get());
	recordBuffer("previousWorldMetadata", texWorldCachePreviousMetadata.get());
	recordBuffer("previousWorldSH0", texWorldCachePreviousSH0.get());
	recordBuffer("previousWorldSH1", texWorldCachePreviousSH1.get());
	recordBuffer("previousWorldSH2", texWorldCachePreviousSH2.get());
	recordBuffer("previousWorldNormal", texWorldCachePreviousNormal.get());
	root["shaders"] = {
		{ "prefilterDepths", prefilterDepthsCompute != nullptr },
		{ "prefilterRadiance", prefilterRadianceCompute != nullptr },
		{ "prefilterNormal", prefilterNormalCompute != nullptr },
		{ "radianceDisocclusion", radianceDisoccCompute != nullptr },
		{ "gi", giCompute != nullptr },
		{ "denoise", blurCompute != nullptr },
		{ "denoiseAtrous", blurAtrousCompute != nullptr },
		{ "upsample", upsampleCompute != nullptr },
		{ "voxelInjection", worldCacheInjectCompute != nullptr },
		{ "voxelDecay", worldCacheDecayCompute != nullptr },
		{ "hybridReflections", hybridReflectionCompute != nullptr },
		{ "hybridReflectionDenoise", hybridReflectionDenoiseCompute != nullptr }
	};

	bool allScreenshotsPresent = diagnosticRecords.size() == PixlDiagnosticViews.size();
	uint64_t screenshotBytes = 0;
	for (const auto& record : diagnosticRecords) {
		std::error_code fileError;
		const auto screenshotPath = diagnosticCaptureDirectory / record.file;
		const bool filePresent = std::filesystem::exists(screenshotPath, fileError);
		const uint64_t fileBytes = filePresent ? std::filesystem::file_size(screenshotPath, fileError) : 0u;
		allScreenshotsPresent = allScreenshotsPresent && filePresent && !fileError && fileBytes > 0u;
		screenshotBytes += fileBytes;
		root["screenshots"].push_back({
			{ "name", record.name },
			{ "file", record.file.generic_string() },
			{ "filePresent", filePresent && !fileError },
			{ "fileBytes", fileBytes },
			{ "debugView", record.debugView },
			{ "materialDebugMode", record.materialDebugMode },
			{ "frameIndex", record.frameIndex }
		});
	}
	root["captureValidation"] = {
		{ "allScreenshotsPresent", allScreenshotsPresent },
		{ "totalScreenshotBytes", screenshotBytes },
		{ "bufferInventoryCount", root["buffers"].size() },
		{ "authoritative", complete && allScreenshotsPresent }
	};

	if (globals::profiler) {
		for (const auto& timer : globals::profiler->GetResults()) {
			if (!timer.name.starts_with("HybridGI::") && timer.name != "DeferredComposite")
				continue;
			root["timings"].push_back({
				{ "name", timer.name },
				{ "valid", timer.valid },
				{ "gpuMs", timer.gpuTimeMs },
				{ "averageMs", timer.avgMs },
				{ "p95Ms", timer.p95Ms },
				{ "p99Ms", timer.p99Ms },
				{ "cpuMs", timer.cpuTimeMs }
			});
		}
	}

	constexpr std::array<std::string_view, 13> shaderFiles{
		"gi.cs.hlsl", "worldCache.hlsli", "worldCacheInject.cs.hlsl", "worldCacheDecay.cs.hlsl",
		"hybridReflection.cs.hlsl", "hybridReflectionDenoise.cs.hlsl", "blur.cs.hlsl", "radianceDisocc.cs.hlsl",
		"prefilterDepths.cs.hlsl", "prefilterRadiance.cs.hlsl", "prefilterNormal.cs.hlsl",
		"upsample.cs.hlsl", "common.hlsli"
	};
	for (const auto shaderFile : shaderFiles) {
		const auto path = Util::PathHelpers::GetShadersPath() / "HybridGI" / shaderFile;
		std::error_code sizeError;
		if (!std::filesystem::exists(path))
			continue;
		root["shaderSources"].push_back({
			{ "file", shaderFile },
			{ "bytes", std::filesystem::file_size(path, sizeError) },
			{ "fnv1a64", std::format("{:016X}", HashFileFNV1a(path)) }
		});
	}

	std::ofstream stream(diagnosticCaptureDirectory / "manifest.json");
	if (!stream) {
		logger::error("[PIXL Hybrid GI] failed to write diagnostic manifest");
		return;
	}
	stream << std::setw(2) << root;
}

void HybridGI::NotifyDiagnostic(std::string message)
{
	if (auto* taskInterface = SKSE::GetTaskInterface()) {
		taskInterface->AddTask([message = std::move(message)]() {
			RE::SendHUDMessage::ShowHUDMessage(message.c_str(), nullptr, true);
		});
	}
}

void HybridGI::LoadSettings(json& o_json)
{
	settings = o_json;
	// Append-only settings are loaded explicitly because the legacy settings
	// serializer is already at its macro field limit. Old presets receive the
	// PIXL defaults; new presets round-trip all four values.
	settings.EnableContactDepth = o_json.value("EnableContactDepth", true);
	settings.ContactDepthStrength = o_json.value("ContactDepthStrength", 0.35f);
	settings.ContactDepthRadius = o_json.value("ContactDepthRadius", 96.0f);
	settings.ContactDepthBias = o_json.value("ContactDepthBias", 0.08f);
	settings.EnableAdaptiveRayAllocation = o_json.value("EnableAdaptiveRayAllocation", false);
	settings.AdaptiveRayMinimum = o_json.value("AdaptiveRayMinimum", 0.50f);
	settings.ResolutionMode = std::clamp(settings.ResolutionMode, 0, 2);
	settings.DebugView = std::min(settings.DebugView, 13u);
	settings.WorldCacheTraceSteps = std::clamp(settings.WorldCacheTraceSteps, 2u, 6u);
	settings.WorldCacheTemporalResponse = std::clamp(settings.WorldCacheTemporalResponse, 0.02f, 1.0f);
	settings.WorldCacheSecondBounceStrength = std::clamp(settings.WorldCacheSecondBounceStrength, 0.0f, 0.5f);
	settings.VoxelReflectionStrength = std::clamp(settings.VoxelReflectionStrength, 0.0f, 1.5f);
	settings.VoxelReflectionRoughnessCutoff = std::clamp(settings.VoxelReflectionRoughnessCutoff, 0.2f, 1.0f);
	settings.BentNormalStrength = std::clamp(settings.BentNormalStrength, 0.0f, 1.0f);
	settings.SpecularOcclusionStrength = std::clamp(settings.SpecularOcclusionStrength, 0.0f, 1.0f);
	settings.RadianceFireflyClamp = std::clamp(settings.RadianceFireflyClamp, 0.0f, 64.0f);
	settings.UpsampleEdgeThreshold = std::clamp(settings.UpsampleEdgeThreshold, 0.02f, 0.35f);
	settings.ReflectionIntensity = std::clamp(settings.ReflectionIntensity, 0.0f, 2.0f);
	settings.ReflectionMaxRoughness = std::clamp(settings.ReflectionMaxRoughness, 0.05f, 1.0f);
	settings.ReflectionMaxDistance = std::clamp(settings.ReflectionMaxDistance, 256.0f, 8192.0f);
	settings.ReflectionThickness = std::clamp(settings.ReflectionThickness, 1.0f, 128.0f);
	settings.ReflectionSteps = std::clamp(settings.ReflectionSteps, 8u, 64u);
	settings.ReflectionTemporalResponse = std::clamp(settings.ReflectionTemporalResponse, 0.02f, 1.0f);
	settings.ReflectionFireflyClamp = std::clamp(settings.ReflectionFireflyClamp, 0.0f, 64.0f);
	settings.ReflectionWorldFallbackStrength = std::clamp(settings.ReflectionWorldFallbackStrength, 0.0f, 1.5f);
	settings.ReflectionRayBias = std::clamp(settings.ReflectionRayBias, 1.0f, 64.0f);
	settings.ReflectionRoughnessJitter = std::clamp(settings.ReflectionRoughnessJitter, 0.0f, 1.0f);
	settings.ContactDepthStrength = std::clamp(settings.ContactDepthStrength, 0.0f, 0.75f);
	settings.ContactDepthRadius = std::clamp(settings.ContactDepthRadius, 24.0f, 256.0f);
	settings.ContactDepthBias = std::clamp(settings.ContactDepthBias, 0.0f, 0.35f);
	settings.AdaptiveRayMinimum = std::clamp(settings.AdaptiveRayMinimum, 0.35f, 1.0f);
	settings.DepthFadeRange.y = std::clamp(settings.DepthFadeRange.y, 1.01e4f, 5e4f);
	settings.DepthFadeRange.x = std::clamp(settings.DepthFadeRange.x, 1e4f, settings.DepthFadeRange.y - 100.f);

	recompileFlag = true;
}

void HybridGI::SaveSettings(json& o_json)
{
	o_json = settings;
	o_json["EnableContactDepth"] = settings.EnableContactDepth;
	o_json["ContactDepthStrength"] = settings.ContactDepthStrength;
	o_json["ContactDepthRadius"] = settings.ContactDepthRadius;
	o_json["ContactDepthBias"] = settings.ContactDepthBias;
	o_json["EnableAdaptiveRayAllocation"] = settings.EnableAdaptiveRayAllocation;
	o_json["AdaptiveRayMinimum"] = settings.AdaptiveRayMinimum;
}

RE::BSEventNotifyControl HybridGI::MenuOpenCloseEventHandler::ProcessEvent(
	const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
	if (!a_event)
		return RE::BSEventNotifyControl::kContinue;

	if (a_event->menuName == RE::LoadingMenu::MENU_NAME && !a_event->opening)
		globals::pipeline::hybridGI.queuedResetHistory = true;
	return RE::BSEventNotifyControl::kContinue;
}

bool HybridGI::MenuOpenCloseEventHandler::Register()
{
	static MenuOpenCloseEventHandler singleton;
	auto ui = globals::game::ui;
	if (!ui) {
		logger::error("UI event source not found");
		return false;
	}
	auto* eventSource = ui->GetEventSource<RE::MenuOpenCloseEvent>();
	if (!eventSource) {
		logger::error("[PIXL Hybrid GI] Menu event source not found");
		return false;
	}
	eventSource->AddEventSink(&singleton);
	return true;
}

void HybridGI::PostPostLoad()
{
	MenuOpenCloseEventHandler::Register();
}

void HybridGI::SetupResources()
{
	auto renderer = globals::game::renderer;
	auto device = globals::d3d::device;
	auto context = globals::d3d::context;
	if (!renderer || !device || !context) {
		logger::error("[PIXL Hybrid GI] Renderer/device/context unavailable; module will remain disabled");
		return;
	}

	logger::debug("Creating buffers...");
	{
		ssgiCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<HybridGICB>(), "HybridGI::CB");
	}

	logger::debug("Creating textures...");
	{
		D3D11_TEXTURE2D_DESC texDesc{
			.Width = 64,
			.Height = 64,
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = DXGI_FORMAT_R32_UINT,
			.SampleDesc = { 1, 0 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
			.CPUAccessFlags = 0,
			.MiscFlags = 0
		};
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = texDesc.MipLevels }
		};
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		auto mainTex = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
		mainTex.texture->GetDesc(&texDesc);
		srvDesc.Format = uavDesc.Format = texDesc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		texDesc.MipLevels = srvDesc.Texture2D.MipLevels = 5;

		{
			texRadiance = eastl::make_unique<Texture2D>(texDesc, "HybridGI::Radiance");
			texRadiance->CreateSRV(srvDesc);
			// No default UAV needed: prefilterRadiance binds per-mip UAVs via uavRadiance[].

			// Create individual UAVs for each mip level for prefiltering
			for (uint i = 0; i < 5; ++i) {
				D3D11_UNORDERED_ACCESS_VIEW_DESC mipUavDesc = {
					.Format = DXGI_FORMAT_R11G11B10_FLOAT,
					.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
					.Texture2D = { .MipSlice = i }
				};
				DX::ThrowIfFailed(device->CreateUnorderedAccessView(texRadiance->resource.get(), &mipUavDesc, uavRadiance[i].put()));
				Util::SetResourceName(uavRadiance[i].get(), "HybridGI::Radiance UAV mip%u", i);
			}

			// Staging texture for mip 0 radiance. radianceDisocc writes it directly,
			// prefilterRadiance reads it as SRV and writes the mip chain back to texRadiance.
			// Avoids a full-texture CopySubresourceRegion each frame.
			D3D11_TEXTURE2D_DESC tempTexDesc = texDesc;
			tempTexDesc.MipLevels = 1;
			tempTexDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

			D3D11_SHADER_RESOURCE_VIEW_DESC tempSrvDesc = {
				.Format = DXGI_FORMAT_R11G11B10_FLOAT,
				.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
				.Texture2D = {
					.MostDetailedMip = 0,
					.MipLevels = 1 }
			};

			D3D11_UNORDERED_ACCESS_VIEW_DESC tempUavDesc = {
				.Format = DXGI_FORMAT_R11G11B10_FLOAT,
				.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
				.Texture2D = { .MipSlice = 0 }
			};

			texRadianceTemp = eastl::make_unique<Texture2D>(tempTexDesc, "HybridGI::RadianceTemp");
			texRadianceTemp->CreateSRV(tempSrvDesc);
			texRadianceTemp->CreateUAV(tempUavDesc);
		}

		texDesc.BindFlags &= ~D3D11_BIND_RENDER_TARGET;
		texDesc.MiscFlags &= ~D3D11_RESOURCE_MISC_GENERATE_MIPS;
		texDesc.Format = srvDesc.Format = uavDesc.Format = DXGI_FORMAT_R16_FLOAT;

		{
			texWorkingDepth = eastl::make_unique<Texture2D>(texDesc, "HybridGI::WorkingDepth");
			texWorkingDepth->CreateSRV(srvDesc);
			for (int i = 0; i < 5; ++i) {
				uavDesc.Texture2D.MipSlice = i;
				DX::ThrowIfFailed(device->CreateUnorderedAccessView(texWorkingDepth->resource.get(), &uavDesc, uavWorkingDepth[i].put()));
				Util::SetResourceName(uavWorkingDepth[i].get(), "HybridGI::WorkingDepth UAV mip%d", i);
			}
		}

		srvDesc.Format = uavDesc.Format = texDesc.Format = DXGI_FORMAT_R8G8_UNORM;
		{
			texNormal = eastl::make_unique<Texture2D>(texDesc, "HybridGI::Normal");
			texNormal->CreateSRV(srvDesc);
			for (uint i = 0; i < 5; ++i) {
				uavDesc.Texture2D.MipSlice = i;
				DX::ThrowIfFailed(device->CreateUnorderedAccessView(texNormal->resource.get(), &uavDesc, uavNormal[i].put()));
				Util::SetResourceName(uavNormal[i].get(), "HybridGI::Normal UAV mip%u", i);
			}
		}

		uavDesc.Texture2D.MipSlice = 0;
		texDesc.MipLevels = srvDesc.Texture2D.MipLevels = 1;
		srvDesc.Format = uavDesc.Format = texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		{
			texIlY[0] = eastl::make_unique<Texture2D>(texDesc, "HybridGI::IlY[0]");
			texIlY[0]->CreateSRV(srvDesc);
			texIlY[0]->CreateUAV(uavDesc);

			texIlY[1] = eastl::make_unique<Texture2D>(texDesc, "HybridGI::IlY[1]");
			texIlY[1]->CreateSRV(srvDesc);
			texIlY[1]->CreateUAV(uavDesc);

			texGiSpecular[0] = eastl::make_unique<Texture2D>(texDesc, "HybridGI::GiSpecular[0]");
			texGiSpecular[0]->CreateSRV(srvDesc);
			texGiSpecular[0]->CreateUAV(uavDesc);

			texGiSpecular[1] = eastl::make_unique<Texture2D>(texDesc, "HybridGI::GiSpecular[1]");
			texGiSpecular[1]->CreateSRV(srvDesc);
			texGiSpecular[1]->CreateUAV(uavDesc);

			texBentVisibility[0] = eastl::make_unique<Texture2D>(texDesc, "HybridGI::BentVisibility[0]");
			texBentVisibility[0]->CreateSRV(srvDesc);
			texBentVisibility[0]->CreateUAV(uavDesc);

			texBentVisibility[1] = eastl::make_unique<Texture2D>(texDesc, "HybridGI::BentVisibility[1]");
			texBentVisibility[1]->CreateSRV(srvDesc);
			texBentVisibility[1]->CreateUAV(uavDesc);
		}
		srvDesc.Format = uavDesc.Format = texDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
		{
			texIlCoCg[0] = eastl::make_unique<Texture2D>(texDesc, "HybridGI::IlCoCg[0]");
			texIlCoCg[0]->CreateSRV(srvDesc);
			texIlCoCg[0]->CreateUAV(uavDesc);

			texIlCoCg[1] = eastl::make_unique<Texture2D>(texDesc, "HybridGI::IlCoCg[1]");
			texIlCoCg[1]->CreateSRV(srvDesc);
			texIlCoCg[1]->CreateUAV(uavDesc);
		}

		srvDesc.Format = uavDesc.Format = texDesc.Format = DXGI_FORMAT_R8_UNORM;
		{
			texAo[0] = eastl::make_unique<Texture2D>(texDesc, "HybridGI::AO[0]");
			texAo[0]->CreateSRV(srvDesc);
			texAo[0]->CreateUAV(uavDesc);

			texAo[1] = eastl::make_unique<Texture2D>(texDesc, "HybridGI::AO[1]");
			texAo[1]->CreateSRV(srvDesc);
			texAo[1]->CreateUAV(uavDesc);

			texAccumFrames[0] = eastl::make_unique<Texture2D>(texDesc, "HybridGI::AccumFrames[0]");
			texAccumFrames[0]->CreateSRV(srvDesc);
			texAccumFrames[0]->CreateUAV(uavDesc);

			texAccumFrames[1] = eastl::make_unique<Texture2D>(texDesc, "HybridGI::AccumFrames[1]");
			texAccumFrames[1]->CreateSRV(srvDesc);
			texAccumFrames[1]->CreateUAV(uavDesc);
		}

		srvDesc.Format = uavDesc.Format = texDesc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
		{
			texPrevGeo = eastl::make_unique<Texture2D>(texDesc, "HybridGI::PrevGeo");
			texPrevGeo->CreateSRV(srvDesc);
			texPrevGeo->CreateUAV(uavDesc);
		}

		// PIXL world cache: two flattened 32^3 cascades. Metadata and surface
		// publication remain atomic R32_UINT. Three RGBA16F payloads hold nine L2
		// luminance SH coefficients plus chroma ratios; sparse injection keeps the
		// additional bandwidth bounded.
		texDesc.Width = 32u * 32u;
		texDesc.Height = 32u * 2u;
		texDesc.MipLevels = 1;
		texDesc.ArraySize = 1;
		texDesc.Format = DXGI_FORMAT_R32_UINT;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		srvDesc.Format = DXGI_FORMAT_R32_UINT;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 };
		uavDesc.Format = DXGI_FORMAT_R32_UINT;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Texture2D = { .MipSlice = 0 };

		auto createWorldCacheUintTexture = [&](const char* name) {
			auto texture = eastl::make_unique<Texture2D>(texDesc, name);
			texture->CreateSRV(srvDesc);
			texture->CreateUAV(uavDesc);
			return texture;
		};
		texWorldCacheMetadata = createWorldCacheUintTexture("PIXL HybridGI::WorldMetadata");
		texWorldCacheNormal = createWorldCacheUintTexture("PIXL HybridGI::WorldNormal");
		texWorldCachePreviousMetadata = createWorldCacheUintTexture("PIXL HybridGI::Previous WorldMetadata");
		texWorldCachePreviousNormal = createWorldCacheUintTexture("PIXL HybridGI::Previous WorldNormal");
		const UINT clearValue[4] = { 0, 0, 0, 0 };
		globals::d3d::context->ClearUnorderedAccessViewUint(texWorldCacheMetadata->uav.get(), clearValue);
		globals::d3d::context->ClearUnorderedAccessViewUint(texWorldCacheNormal->uav.get(), clearValue);
		globals::d3d::context->ClearUnorderedAccessViewUint(texWorldCachePreviousMetadata->uav.get(), clearValue);
		globals::d3d::context->ClearUnorderedAccessViewUint(texWorldCachePreviousNormal->uav.get(), clearValue);

		texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		auto createWorldCacheSHTexture = [&](const char* name) {
			auto texture = eastl::make_unique<Texture2D>(texDesc, name);
			texture->CreateSRV(srvDesc);
			texture->CreateUAV(uavDesc);
			return texture;
		};
		texWorldCacheSH0 = createWorldCacheSHTexture("PIXL HybridGI::WorldSH L0L1");
		texWorldCacheSH1 = createWorldCacheSHTexture("PIXL HybridGI::WorldSH L2A");
		texWorldCacheSH2 = createWorldCacheSHTexture("PIXL HybridGI::WorldSH L2B Chroma");
		texWorldCachePreviousSH0 = createWorldCacheSHTexture("PIXL HybridGI::Previous WorldSH L0L1");
		texWorldCachePreviousSH1 = createWorldCacheSHTexture("PIXL HybridGI::Previous WorldSH L2A");
		texWorldCachePreviousSH2 = createWorldCacheSHTexture("PIXL HybridGI::Previous WorldSH L2B Chroma");
		const FLOAT clearFloat[4] = { 0.f, 0.f, 0.f, 0.f };
		globals::d3d::context->ClearUnorderedAccessViewFloat(texWorldCacheSH0->uav.get(), clearFloat);
		globals::d3d::context->ClearUnorderedAccessViewFloat(texWorldCacheSH1->uav.get(), clearFloat);
		globals::d3d::context->ClearUnorderedAccessViewFloat(texWorldCacheSH2->uav.get(), clearFloat);
		globals::d3d::context->ClearUnorderedAccessViewFloat(texWorldCachePreviousSH0->uav.get(), clearFloat);
		globals::d3d::context->ClearUnorderedAccessViewFloat(texWorldCachePreviousSH1->uav.get(), clearFloat);
		globals::d3d::context->ClearUnorderedAccessViewFloat(texWorldCachePreviousSH2->uav.get(), clearFloat);
	}

	logger::debug("Loading noise texture...");
	{
		DirectX::ScratchImage image;
		try {
			std::filesystem::path path{ "Data\\Shaders\\HybridGI\\fast_2uges.dds" };

			DX::ThrowIfFailed(LoadFromDDSFile(path.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image));
		} catch (const DX::com_exception& e) {
			logger::error("{}", e.what());
			return;
		}

		ID3D11Resource* pResource = nullptr;
		try {
			DX::ThrowIfFailed(CreateTexture(device,
				image.GetImages(), image.GetImageCount(),
				image.GetMetadata(), &pResource));
		} catch (const DX::com_exception& e) {
			logger::error("{}", e.what());
			return;
		}

		texNoise = eastl::make_unique<Texture2D>(reinterpret_cast<ID3D11Texture2D*>(pResource), "HybridGI::Noise");

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texNoise->desc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = 1 }
		};
		texNoise->CreateSRV(srvDesc);
	}

	logger::debug("Creating samplers...");
	{
		D3D11_SAMPLER_DESC samplerDesc = {
			.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
			.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
			.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
			.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
			.MaxAnisotropy = 1,
			.MinLOD = 0,
			.MaxLOD = D3D11_FLOAT32_MAX
		};
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, linearClampSampler.put()));
		Util::SetResourceName(linearClampSampler.get(), "HybridGI::LinearClampSampler");

		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, pointClampSampler.put()));
		Util::SetResourceName(pointClampSampler.get(), "HybridGI::PointClampSampler");
	}

	CompileComputeShaders();
}

void HybridGI::ClearShaderCache()
{
	static const std::vector<winrt::com_ptr<ID3D11ComputeShader>*> shaderPtrs = {
		&prefilterDepthsCompute, &prefilterRadianceCompute, &prefilterNormalCompute, &radianceDisoccCompute, &giCompute, &blurCompute, &blurAtrousCompute, &upsampleCompute,
		&worldCacheInjectCompute, &worldCacheDecayCompute, &hybridReflectionCompute, &hybridReflectionDenoiseCompute
	};

	for (auto shader : shaderPtrs)
		*shader = nullptr;

	CompileComputeShaders();
}

void HybridGI::CompileComputeShaders()
{
	struct ShaderCompileInfo
	{
		winrt::com_ptr<ID3D11ComputeShader>* programPtr;
		std::string_view filename;
		std::vector<std::pair<const char*, const char*>> defines;
	};

	std::vector<ShaderCompileInfo>
		shaderInfos = {
			{ &prefilterDepthsCompute, "prefilterDepths.cs.hlsl", { { "LINEAR_FILTER", "" } } },
			{ &prefilterRadianceCompute, "prefilterRadiance.cs.hlsl", {} },
			{ &prefilterNormalCompute, "prefilterNormal.cs.hlsl", {} },
			{ &radianceDisoccCompute, "radianceDisocc.cs.hlsl", {} },
			{ &giCompute, "gi.cs.hlsl", {} },
			{ &blurCompute, "blur.cs.hlsl", {} },
			{ &blurAtrousCompute, "blur.cs.hlsl", { { "ATROUS_STEP_2", "" } } },
			{ &upsampleCompute, "upsample.cs.hlsl", {} },
			{ &worldCacheInjectCompute, "worldCacheInject.cs.hlsl", {} },
			{ &worldCacheDecayCompute, "worldCacheDecay.cs.hlsl", {} },
			{ &hybridReflectionCompute, "hybridReflection.cs.hlsl", {} },
			{ &hybridReflectionDenoiseCompute, "hybridReflectionDenoise.cs.hlsl", {} },
		};

	for (auto& info : shaderInfos) {
		if (settings.ResolutionMode == 1)
			info.defines.push_back({ "HALF_RES", "" });
		if (settings.ResolutionMode == 2)
			info.defines.push_back({ "QUARTER_RES", "" });
		if (settings.EnableTemporalDenoiser)
			info.defines.push_back({ "TEMPORAL_DENOISER", "" });
		if (settings.EnableGI)
			info.defines.push_back({ "GI", "" });
		if (settings.EnableExperimentalSpecularGI) {
			info.defines.push_back({ "GI_SPECULAR", "" });
			info.defines.push_back({ "HYBRID_REFLECTIONS", "" });
		}
		if (settings.EnableAdaptiveRayAllocation)
			info.defines.push_back({ "ADAPTIVE_RAY_ALLOCATION", "" });
	}

	for (auto& info : shaderInfos) {
		auto path = std::filesystem::path("Data\\Shaders\\HybridGI") / info.filename;
		if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(path.c_str(), info.defines, "cs_5_0")))
			info.programPtr->attach(rawPtr);
	}

	recompileFlag = false;
}

bool HybridGI::ShadersOK() const
{
	const bool coreResources = ssgiCB && linearClampSampler && pointClampSampler && texNoise && texWorkingDepth &&
		texPrevGeo && texRadiance && texRadianceTemp && texNormal &&
		texAccumFrames[0] && texAccumFrames[1] && texAo[0] && texAo[1] &&
		texIlY[0] && texIlY[1] && texIlCoCg[0] && texIlCoCg[1] &&
		texGiSpecular[0] && texGiSpecular[1] && texBentVisibility[0] && texBentVisibility[1] &&
		texWorldCacheMetadata && texWorldCacheSH0 && texWorldCacheSH1 && texWorldCacheSH2 && texWorldCacheNormal &&
		texWorldCachePreviousMetadata && texWorldCachePreviousSH0 && texWorldCachePreviousSH1 &&
		texWorldCachePreviousSH2 && texWorldCachePreviousNormal;

	return coreResources && prefilterDepthsCompute && prefilterRadianceCompute && prefilterNormalCompute &&
	       radianceDisoccCompute && giCompute && blurCompute && blurAtrousCompute && upsampleCompute &&
	       worldCacheInjectCompute && worldCacheDecayCompute &&
	       (!settings.EnableExperimentalSpecularGI || (hybridReflectionCompute && (!settings.EnableBlur || hybridReflectionDenoiseCompute)));
}

void HybridGI::UpdateSB()
{
	if (!texRadiance || !ssgiCB || !globals::game::shadowState || !globals::state)
		return;

	float2 res = { (float)texRadiance->desc.Width, (float)texRadiance->desc.Height };
	float2 dynres = Util::ConvertToDynamic(res);
	dynres = { std::max(floor(dynres.x), 1.0f), std::max(floor(dynres.y), 1.0f) };

	static float4x4 prevInvView = {};

	HybridGICB data{};
	{
		{
			auto eye = globals::game::shadowState->GetRuntimeData().cameraData.getEye();
			const float projectionX = std::abs(eye.projMat(0, 0)) > 1e-6f ? eye.projMat(0, 0) : 1.0f;
			const float projectionY = std::abs(eye.projMat(1, 1)) > 1e-6f ? eye.projMat(1, 1) : 1.0f;

			data.PrevInvViewMat = prevInvView;
			data.NDCToViewMul = { 2.0f / projectionX, -2.0f / projectionY, 0.0f, 0.0f };
			data.NDCToViewAdd = { -1.0f / projectionX, 1.0f / projectionY, 0.0f, 0.0f };

			prevInvView = eye.viewMat.Invert();
		}

		data.TexDim = res;
		data.RcpTexDim = float2(1.0f) / res;
		data.FrameDim = dynres;
		data.RcpFrameDim = float2(1.0f) / dynres;
		data.FrameIndex = globals::state->frameCount;

		data.NumSlices = std::clamp(settings.NumSlices, 1u, 10u);
		data.NumSteps = std::clamp(settings.NumSteps, 1u, 20u);
		data.MinScreenRadius = std::clamp(settings.MinScreenRadius, 0.0f, 1.0f) * dynres.x;

		const float aoRadius = std::clamp(settings.AORadius, 0.0f, 1024.0f);
		const float giRadius = std::clamp(settings.GIRadius, 0.0f, 1024.0f);
		const float contactRadius = settings.EnableContactDepth ? std::clamp(settings.ContactDepthRadius, 0.0f, 256.0f) : 0.0f;
		data.EffectRadius = std::max(std::max(aoRadius, giRadius), std::max(contactRadius, 1.0f));
		data.AORadius = aoRadius / data.EffectRadius;
		data.GIRadius = giRadius / data.EffectRadius;
		data.Thickness = std::clamp(settings.Thickness, 0.0f, 128.0f);
		data.DepthFadeRange.y = std::clamp(settings.DepthFadeRange.y, 1.01e4f, 5e4f);
		data.DepthFadeRange.x = std::clamp(settings.DepthFadeRange.x, 1e4f, data.DepthFadeRange.y - 100.f);
		data.DepthFadeScaleConst = 1.0f / std::max(data.DepthFadeRange.y - data.DepthFadeRange.x, 100.f);

		data.GISaturation = std::clamp(settings.GISaturation, 0.0f, 2.0f);
		data.GIDistanceCompensation = std::clamp(settings.GIDistanceCompensation, -5.0f, 5.0f);
		data.GICompensationMaxDist = aoRadius;

		data.AOPower = std::clamp(settings.AOPower, 0.0f, 4.0f);
		data.GIStrength = std::clamp(settings.GIStrength, 0.0f, 4.0f);

		data.DepthDisocclusion = std::clamp(settings.DepthDisocclusion, 0.0f, 0.2f);
		data.NormalDisocclusion = std::clamp(settings.NormalDisocclusion, 0.0f, 1.0f);
		data.MaxAccumFrames = std::clamp(settings.MaxAccumFrames, 1u, 64u);
		data.BlurRadius = std::clamp(settings.BlurRadius, 0.0f, 30.0f);
		data.DistanceNormalisation = std::clamp(settings.DistanceNormalisation, 0.0f, 5.0f);

		data.WorldCacheEnabled = settings.EnableWorldCache ? 1u : 0u;
		data.WorldCacheMaxAge = std::clamp(settings.WorldCacheMaxAge, 1u, 120u);
		data.WorldCacheSampleCount = std::clamp(settings.WorldCacheSampleCount, 1u, 8u);
		data.WorldCacheTraceSteps = std::clamp(settings.WorldCacheTraceSteps, 2u, 6u);
		data.WorldCacheStrength = std::clamp(settings.WorldCacheStrength, 0.0f, 1.5f);
		data.WorldCacheCellSizeNear = std::clamp(settings.WorldCacheCellSizeNear, 64.0f, 256.0f);
		data.WorldCacheCellSizeFar = std::max(std::clamp(settings.WorldCacheCellSizeFar, 256.0f, 1024.0f), data.WorldCacheCellSizeNear);
		data.WorldCacheRadius = std::clamp(settings.WorldCacheRadius, 256.0f, 4096.0f);
		data.WorldCacheLeakReduction = std::clamp(settings.WorldCacheLeakReduction, 0.0f, 1.0f);
		data.WorldCacheInjectionStride = std::clamp(settings.WorldCacheInjectionStride, 1u, 8u);
		data.WorldCacheDirectionalOcclusionEnabled = settings.EnableDirectionalOcclusion ? 1u : 0u;
		data.WorldCacheDirectionalOcclusionStrength = std::clamp(settings.DirectionalOcclusionStrength, 0.0f, 1.0f);
		data.DebugView = std::min(settings.DebugView, 13u);
		data.DebugGain = std::clamp(settings.DebugGain, 0.1f, 16.0f);
		data.WorldCacheTemporalResponse = std::clamp(settings.WorldCacheTemporalResponse, 0.02f, 1.0f);
		data.WorldCacheReflectionEnabled = settings.EnableVoxelReflections ? 1u : 0u;
		data.WorldCacheReflectionStrength = std::clamp(settings.VoxelReflectionStrength, 0.0f, 1.5f);
		data.WorldCacheReflectionRoughnessCutoff = std::clamp(settings.VoxelReflectionRoughnessCutoff, 0.2f, 1.0f);
		data.WorldCacheSecondBounceEnabled = settings.EnableWorldCacheSecondBounce ? 1u : 0u;
		data.WorldCacheSecondBounceStrength = std::clamp(settings.WorldCacheSecondBounceStrength, 0.0f, 0.5f);
		data.BentNormalEnabled = settings.EnableBentNormalLighting ? 1u : 0u;
		data.BentNormalStrength = std::clamp(settings.BentNormalStrength, 0.0f, 1.0f);
		data.SpecularOcclusionEnabled = settings.EnableSpecularOcclusion ? 1u : 0u;
		data.SpecularOcclusionStrength = std::clamp(settings.SpecularOcclusionStrength, 0.0f, 1.0f);
		data.RadianceFireflyClamp = std::max(settings.RadianceFireflyClamp, 0.0f);
		data.UpsampleEdgeThreshold = std::clamp(settings.UpsampleEdgeThreshold, 0.01f, 0.5f);
		data.ReflectionIntensity = std::clamp(settings.ReflectionIntensity, 0.0f, 2.0f);
		data.ReflectionMaxRoughness = std::clamp(settings.ReflectionMaxRoughness, 0.05f, 1.0f);
		data.ReflectionMaxDistance = std::clamp(settings.ReflectionMaxDistance, 256.0f, 8192.0f);
		data.ReflectionThickness = std::clamp(settings.ReflectionThickness, 2.0f, 96.0f);
		data.ReflectionSteps = std::clamp(settings.ReflectionSteps, 8u, 64u);
		data.ReflectionTemporalResponse = std::clamp(settings.ReflectionTemporalResponse, 0.04f, 0.5f);
		data.ReflectionFireflyClamp = std::clamp(settings.ReflectionFireflyClamp, 1.0f, 32.0f);
		data.ReflectionWorldFallbackStrength = std::clamp(settings.ReflectionWorldFallbackStrength, 0.0f, 1.0f);
		data.ReflectionRayBias = std::clamp(settings.ReflectionRayBias, 1.0f, 24.0f);
		data.ReflectionRoughnessJitter = std::clamp(settings.ReflectionRoughnessJitter, 0.0f, 1.0f);
		data.ContactDepthEnabled = settings.EnableContactDepth ? 1u : 0u;
		data.ContactDepthStrength = std::clamp(settings.ContactDepthStrength, 0.0f, 0.75f);
		data.ContactDepthRadius = std::clamp(settings.ContactDepthRadius, 24.0f, 256.0f);
		data.ContactDepthBias = std::clamp(settings.ContactDepthBias, 0.0f, 0.35f);
		data.AdaptiveRayMinimum = std::clamp(settings.AdaptiveRayMinimum, 0.35f, 1.0f);
	}

	ssgiCB->Update(data);
}

void HybridGI::DrawHybridGI()
{
	auto context = globals::d3d::context;
	if (!context)
		return;

	auto imageSpaceManager = globals::game::imageSpaceManager;
	if (imageSpaceManager) {
		auto& ssaoBlur = imageSpaceManager->GetRuntimeData().BSImagespaceShaderISSAOBlurH;
		// Toggle vanilla SSAO only when Skyrim has materialized the image-space
		// shader object.  The pointer can be recreated across display transitions.
		if (auto* shader = ssaoBlur.get()) {
			auto* enableSSAO = reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(shader) + 0x50LL);
			*enableSSAO = settings.EnableVanillaSSAO;
		}
	}
	UpdateDiagnosticCapture();
	const uint materialDebugMode = globals::pipeline::materialForge.settings.LegacyPhysicalDebugMode;
	if ((lastRuntimeDebugView != 0u && settings.DebugView == 0u) ||
		(lastRuntimeMaterialDebugMode != 0u && materialDebugMode == 0u))
		queuedResetTemporalHistory = true;
	lastRuntimeDebugView = settings.DebugView;
	lastRuntimeMaterialDebugMode = materialDebugMode;

	if (!(settings.Enabled && ShadersOK())) {
		FLOAT clr[4] = { 0.f, 0.f, 0.f, 0.f };
		if (texAo[outputAoIdx])
			context->ClearUnorderedAccessViewFloat(texAo[outputAoIdx]->uav.get(), clr);
		if (texIlY[outputIlIdx])
			context->ClearUnorderedAccessViewFloat(texIlY[outputIlIdx]->uav.get(), clr);
		if (texIlCoCg[outputIlIdx])
			context->ClearUnorderedAccessViewFloat(texIlCoCg[outputIlIdx]->uav.get(), clr);
		if (texGiSpecular[outputSpecIdx])
			context->ClearUnorderedAccessViewFloat(texGiSpecular[outputSpecIdx]->uav.get(), clr);
		if (texBentVisibility[outputBentIdx])
			context->ClearUnorderedAccessViewFloat(texBentVisibility[outputBentIdx]->uav.get(), clr);
		return;
	}

	if (!globals::state || !globals::state->sharedDataCB || !globals::game::renderer ||
		!globals::game::graphicsState || !globals::deferred || !globals::profiler)
		return;

	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "HybridGI");

	static uint lastFrameAoTexIdx = 0;
	static uint lastFrameGITexIdx = 0;
	static uint lastFrameAccumTexIdx = 0;
	static uint lastFrameSpecTexIdx = 0;
	static uint lastFrameBentTexIdx = 0;
	uint inputAoTexIdx = lastFrameAoTexIdx;
	uint inputGITexIdx = lastFrameGITexIdx;
	uint inputSpecTexIdx = lastFrameSpecTexIdx;
	uint inputBentTexIdx = lastFrameBentTexIdx;

	auto clearTemporalHistory = [&]() {
		const FLOAT clr[4] = { 0.f, 0.f, 0.f, 0.f };
		for (auto& tex : texAccumFrames)
			context->ClearUnorderedAccessViewFloat(tex->uav.get(), clr);
		for (auto& tex : texAo)
			context->ClearUnorderedAccessViewFloat(tex->uav.get(), clr);
		for (auto& tex : texIlY)
			context->ClearUnorderedAccessViewFloat(tex->uav.get(), clr);
		for (auto& tex : texIlCoCg)
			context->ClearUnorderedAccessViewFloat(tex->uav.get(), clr);
		for (auto& tex : texGiSpecular)
			context->ClearUnorderedAccessViewFloat(tex->uav.get(), clr);
		for (auto& tex : texBentVisibility)
			context->ClearUnorderedAccessViewFloat(tex->uav.get(), clr);
		context->ClearUnorderedAccessViewFloat(texPrevGeo->uav.get(), clr);
	};

	if (queuedResetHistory.exchange(false)) {
		clearTemporalHistory();
		const UINT clearValue[4] = { 0, 0, 0, 0 };
		context->ClearUnorderedAccessViewUint(texWorldCacheMetadata->uav.get(), clearValue);
		context->ClearUnorderedAccessViewUint(texWorldCacheNormal->uav.get(), clearValue);
	}
	if (queuedResetTemporalHistory.exchange(false))
		clearTemporalHistory();

	//////////////////////////////////////////////////////

	if (recompileFlag)
		ClearShaderCache();

	UpdateSB();

	//////////////////////////////////////////////////////

	auto renderer = globals::game::renderer;
	auto rts = renderer->GetRuntimeData().renderTargets;
	auto deferred = globals::deferred;

	float2 size = Util::ConvertToDynamic(float2{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight });
	auto resolution = std::array{
		(uint)std::max(std::isfinite(size.x) ? floor(size.x) : 1.0f, 1.0f),
		(uint)std::max(std::isfinite(size.y) ? floor(size.y) : 1.0f, 1.0f)
	};
	auto resChoices = std::array{
		resolution,
		std::array{ std::max(resolution[0] >> 1, 1u), std::max(resolution[1] >> 1, 1u) },
		std::array{ std::max(resolution[0] >> 2, 1u), std::max(resolution[1] >> 2, 1u) }
	};
	const int resolutionMode = std::clamp(settings.ResolutionMode, 0, 2);
	auto internalRes = resChoices[resolutionMode];

	std::array<ID3D11ShaderResourceView*, 17> srvs = { nullptr };
	std::array<ID3D11UnorderedAccessView*, 7> uavs = { nullptr };
	std::array<ID3D11SamplerState*, 2> samplers = { pointClampSampler.get(), linearClampSampler.get() };
	auto cb = ssgiCB->CB();

	auto resetViews = [&]() {
		srvs.fill(nullptr);
		uavs.fill(nullptr);

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
	};

	//////////////////////////////////////////////////////

	context->CSSetConstantBuffers(1, 1, &cb);
	auto* sharedDataBuf = globals::state->sharedDataCB->CB();
	context->CSSetConstantBuffers(5, 1, &sharedDataBuf);
	context->CSSetSamplers(0, (uint)samplers.size(), samplers.data());

	// prefilter depths
	{
		TracyD3D11Zone(globals::state->tracyCtx, "HybridGI - Prefilter Depths");

		srvs.at(0) = globals::pipeline::materialLayers.GetEffectsDepth(Util::GetCurrentSceneDepthSRV());
		for (int i = 0; i < 5; ++i)
			uavs.at(i) = uavWorkingDepth[i].get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(prefilterDepthsCompute.get(), nullptr, 0);
		globals::profiler->BeginPass("HybridGI::PrefilterDepths");
		context->Dispatch((resolution[0] + 15) >> 4, (resolution[1] + 15) >> 4, 1);
		globals::profiler->EndPass();
	}

	// fetch radiance and disocclusion
	{
		TracyD3D11Zone(globals::state->tracyCtx, "HybridGI - Radiance Disocc");

		resetViews();
		srvs.at(0) = rts[deferred->forwardRenderTargets[0]].SRV;
		srvs.at(1) = texWorkingDepth->srv.get();
		srvs.at(2) = rts[NORMALROUGHNESS].SRV;
		srvs.at(3) = texPrevGeo->srv.get();
		srvs.at(4) = rts[RE::RENDER_TARGET::kMOTION_VECTOR].SRV;
		srvs.at(5) = texAccumFrames[lastFrameAccumTexIdx]->srv.get();
		srvs.at(6) = texAo[inputAoTexIdx]->srv.get();
		srvs.at(7) = texIlY[inputGITexIdx]->srv.get();
		srvs.at(8) = texIlCoCg[inputGITexIdx]->srv.get();
		srvs.at(9) = texGiSpecular[inputSpecTexIdx]->srv.get();
		srvs.at(10) = texBentVisibility[inputBentTexIdx]->srv.get();

		uavs.at(0) = texRadianceTemp->uav.get();
		uavs.at(1) = texAccumFrames[!lastFrameAccumTexIdx]->uav.get();
		uavs.at(2) = texAo[!inputAoTexIdx]->uav.get();
		uavs.at(3) = texIlY[!inputGITexIdx]->uav.get();
		uavs.at(4) = texIlCoCg[!inputGITexIdx]->uav.get();
		uavs.at(5) = texGiSpecular[!inputSpecTexIdx]->uav.get();
		uavs.at(6) = texBentVisibility[!inputBentTexIdx]->uav.get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(radianceDisoccCompute.get(), nullptr, 0);
		globals::profiler->BeginPass("HybridGI::RadianceDisocc");
		context->Dispatch((internalRes[0] + 7u) >> 3, (internalRes[1] + 7u) >> 3, 1);
		globals::profiler->EndPass();

		// Prefilter radiance texture instead of using GenerateMips for proper dynamic resolution handling.
		// radianceDisocc wrote mip 0 directly to texRadianceTemp above, so we can bind it
		// as SRV input here without an intermediate CopySubresourceRegion.
		{
			TracyD3D11Zone(globals::state->tracyCtx, "HybridGI - Prefilter Radiance");

			resetViews();
			srvs.at(0) = texRadianceTemp->srv.get();
			uavs.at(0) = uavRadiance[0].get();  // Mip 0
			uavs.at(1) = uavRadiance[1].get();  // Mip 1
			uavs.at(2) = uavRadiance[2].get();  // Mip 2
			uavs.at(3) = uavRadiance[3].get();  // Mip 3
			uavs.at(4) = uavRadiance[4].get();  // Mip 4

			context->CSSetShaderResources(0, 1, srvs.data());
			context->CSSetUnorderedAccessViews(0, 5, uavs.data(), nullptr);
			context->CSSetShader(prefilterRadianceCompute.get(), nullptr, 0);
			globals::profiler->BeginPass("HybridGI::PrefilterRadiance");
			context->Dispatch((internalRes[0] + 15u) >> 4, (internalRes[1] + 15u) >> 4, 1);
			globals::profiler->EndPass();
		}

		inputAoTexIdx = !inputAoTexIdx;
		inputGITexIdx = !inputGITexIdx;
		inputSpecTexIdx = !inputSpecTexIdx;
		inputBentTexIdx = !inputBentTexIdx;
		lastFrameAccumTexIdx = !lastFrameAccumTexIdx;
	}

	// Prefilter normals
	{
		TracyD3D11Zone(globals::state->tracyCtx, "HybridGI - Prefilter Normals");

		resetViews();
		srvs.at(0) = rts[NORMALROUGHNESS].SRV;
		uavs.at(0) = uavNormal[0].get();
		uavs.at(1) = uavNormal[1].get();
		uavs.at(2) = uavNormal[2].get();
		uavs.at(3) = uavNormal[3].get();
		uavs.at(4) = uavNormal[4].get();

		context->CSSetShaderResources(0, 1, srvs.data());
		context->CSSetUnorderedAccessViews(0, 5, uavs.data(), nullptr);
		context->CSSetShader(prefilterNormalCompute.get(), nullptr, 0);
		globals::profiler->BeginPass("HybridGI::PrefilterNormals");
		context->Dispatch((internalRes[0] + 15u) >> 4, (internalRes[1] + 15u) >> 4, 1);
		globals::profiler->EndPass();
	}

	// GI
	{
		const bool visualizationActive = diagnosticCaptureActive || settings.DebugView != 0u || materialDebugMode != 0u;
		if (settings.EnableWorldCache && !visualizationActive) {
			TracyD3D11Zone(globals::state->tracyCtx, "HybridGI - PIXL World Cache Inject");
			resetViews();
			// Snapshot the complete cache before sparse injection. The shader reads
			// this immutable copy while updating the live UAVs, eliminating
			// same-dispatch second-bounce races and their one-frame brightness pops.
			context->CopyResource(texWorldCachePreviousMetadata->resource.get(), texWorldCacheMetadata->resource.get());
			context->CopyResource(texWorldCachePreviousSH0->resource.get(), texWorldCacheSH0->resource.get());
			context->CopyResource(texWorldCachePreviousSH1->resource.get(), texWorldCacheSH1->resource.get());
			context->CopyResource(texWorldCachePreviousSH2->resource.get(), texWorldCacheSH2->resource.get());
			context->CopyResource(texWorldCachePreviousNormal->resource.get(), texWorldCacheNormal->resource.get());
			srvs.at(0) = texWorkingDepth->srv.get();
			srvs.at(1) = texNormal->srv.get();
			srvs.at(2) = texRadiance->srv.get();
			srvs.at(3) = rts[ALBEDO].SRV;
			srvs.at(4) = texWorldCachePreviousMetadata->srv.get();
			srvs.at(5) = texWorldCachePreviousSH0->srv.get();
			srvs.at(6) = texWorldCachePreviousSH1->srv.get();
			srvs.at(7) = texWorldCachePreviousSH2->srv.get();
			srvs.at(8) = texWorldCachePreviousNormal->srv.get();
			uavs.at(0) = texWorldCacheMetadata->uav.get();
			uavs.at(1) = texWorldCacheSH0->uav.get();
			uavs.at(2) = texWorldCacheSH1->uav.get();
			uavs.at(3) = texWorldCacheSH2->uav.get();
			uavs.at(4) = texWorldCacheNormal->uav.get();
			context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
			context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
			context->CSSetShader(worldCacheInjectCompute.get(), nullptr, 0);
			const uint stride = std::clamp(settings.WorldCacheInjectionStride, 1u, 8u);
			const uint injectWidth = (internalRes[0] + stride - 1u) / stride;
			const uint injectHeight = (internalRes[1] + stride - 1u) / stride;
			globals::profiler->BeginPass("HybridGI::WorldCacheInject");
			context->Dispatch((injectWidth + 7u) >> 3, (injectHeight + 7u) >> 3, 1);
			globals::profiler->EndPass();

			// Expire stale toroidal cells before any GI/reflection read. The 8-bit
			// frame stamp otherwise aliases every 256 frames and can resurrect old light.
			resetViews();
			uavs.at(0) = texWorldCacheMetadata->uav.get();
			context->CSSetUnorderedAccessViews(0, 1, uavs.data(), nullptr);
			context->CSSetShader(worldCacheDecayCompute.get(), nullptr, 0);
			globals::profiler->BeginPass("HybridGI::WorldCacheDecay");
			context->Dispatch(((32u * 32u) + 7u) >> 3, ((32u * 2u) + 7u) >> 3, 1);
			globals::profiler->EndPass();
		}

		TracyD3D11Zone(globals::state->tracyCtx, "HybridGI - GI");

		resetViews();
		srvs.at(0) = texWorkingDepth->srv.get();
		srvs.at(1) = rts[NORMALROUGHNESS].SRV;
		srvs.at(2) = texRadiance->srv.get();
		srvs.at(3) = texNoise->srv.get();
		srvs.at(4) = texAccumFrames[lastFrameAccumTexIdx]->srv.get();
		srvs.at(5) = texIlY[inputGITexIdx]->srv.get();
		srvs.at(6) = texIlCoCg[inputGITexIdx]->srv.get();
		srvs.at(7) = texGiSpecular[inputSpecTexIdx]->srv.get();
		srvs.at(8) = texNormal->srv.get();
		srvs.at(9) = settings.EnableWorldCache ? texWorldCacheMetadata->srv.get() : nullptr;
		srvs.at(10) = settings.EnableWorldCache ? texWorldCacheSH0->srv.get() : nullptr;
		srvs.at(11) = settings.EnableWorldCache ? texWorldCacheSH1->srv.get() : nullptr;
		srvs.at(12) = settings.EnableWorldCache ? texWorldCacheSH2->srv.get() : nullptr;
		srvs.at(13) = settings.EnableWorldCache ? texWorldCacheNormal->srv.get() : nullptr;
			srvs.at(14) = rts[REFLECTANCE].SRV;
			srvs.at(15) = texBentVisibility[inputBentTexIdx]->srv.get();
			srvs.at(16) = texAo[inputAoTexIdx]->srv.get();

		uavs.at(0) = texAo[!inputAoTexIdx]->uav.get();
		uavs.at(1) = texIlY[!inputGITexIdx]->uav.get();
		uavs.at(2) = texIlCoCg[!inputGITexIdx]->uav.get();
		uavs.at(3) = nullptr;  // Hybrid Reflections owns the specular output.
		uavs.at(4) = texPrevGeo->uav.get();
		uavs.at(5) = texBentVisibility[!inputBentTexIdx]->uav.get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(giCompute.get(), nullptr, 0);
		globals::profiler->BeginPass("HybridGI::GI");
		context->Dispatch((internalRes[0] + 7u) >> 3, (internalRes[1] + 7u) >> 3, 1);
		globals::profiler->EndPass();

		inputAoTexIdx = !inputAoTexIdx;
		inputGITexIdx = !inputGITexIdx;
		inputBentTexIdx = !inputBentTexIdx;
		lastFrameGITexIdx = inputGITexIdx;
		lastFrameAoTexIdx = inputAoTexIdx;
		lastFrameBentTexIdx = inputBentTexIdx;
	}

	// PIXL Hybrid Reflections: stochastic GGX screen-space trace with temporal
	// history already geometry-remapped by radianceDisocc.
	if (settings.EnableExperimentalSpecularGI) {
		TracyD3D11Zone(globals::state->tracyCtx, "HybridGI - Hybrid Reflections");
		resetViews();
		srvs.at(0) = texWorkingDepth->srv.get();
		srvs.at(1) = rts[NORMALROUGHNESS].SRV;
		srvs.at(2) = texRadiance->srv.get();
		srvs.at(3) = texNoise->srv.get();
		srvs.at(4) = texGiSpecular[inputSpecTexIdx]->srv.get();
		srvs.at(5) = rts[REFLECTANCE].SRV;
		srvs.at(6) = settings.EnableWorldCache ? texWorldCacheMetadata->srv.get() : nullptr;
		srvs.at(7) = settings.EnableWorldCache ? texWorldCacheSH0->srv.get() : nullptr;
		srvs.at(8) = settings.EnableWorldCache ? texWorldCacheSH1->srv.get() : nullptr;
		srvs.at(9) = settings.EnableWorldCache ? texWorldCacheSH2->srv.get() : nullptr;
		srvs.at(10) = settings.EnableWorldCache ? texWorldCacheNormal->srv.get() : nullptr;
		uavs.at(0) = texGiSpecular[!inputSpecTexIdx]->uav.get();
		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, 1, uavs.data(), nullptr);
		context->CSSetShader(hybridReflectionCompute.get(), nullptr, 0);
		globals::profiler->BeginPass("HybridGI::HybridReflections");
		context->Dispatch((internalRes[0] + 7u) >> 3, (internalRes[1] + 7u) >> 3, 1);
		globals::profiler->EndPass();
		inputSpecTexIdx = !inputSpecTexIdx;
		lastFrameSpecTexIdx = inputSpecTexIdx;
	} else {
		lastFrameSpecTexIdx = inputSpecTexIdx;
	}

	// Bilateral spatial reconstruction for the stochastic reflection signal.
	// Reuses the existing specular ping-pong textures; no extra full-resolution
	// allocation is required. The user-facing Spatial Denoiser toggle controls
	// both diffuse GI and hybrid reflection reconstruction.
	if (settings.EnableExperimentalSpecularGI && settings.EnableBlur) {
		TracyD3D11Zone(globals::state->tracyCtx, "HybridGI - Hybrid Reflection Denoise");
		resetViews();
		srvs.at(0) = texGiSpecular[inputSpecTexIdx]->srv.get();
		srvs.at(1) = texWorkingDepth->srv.get();
		srvs.at(2) = rts[NORMALROUGHNESS].SRV;
		uavs.at(0) = texGiSpecular[!inputSpecTexIdx]->uav.get();
		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, 1, uavs.data(), nullptr);
		context->CSSetShader(hybridReflectionDenoiseCompute.get(), nullptr, 0);
		globals::profiler->BeginPass("HybridGI::HybridReflectionDenoise");
		context->Dispatch((internalRes[0] + 7u) >> 3, (internalRes[1] + 7u) >> 3, 1);
		globals::profiler->EndPass();
		inputSpecTexIdx = !inputSpecTexIdx;
		lastFrameSpecTexIdx = inputSpecTexIdx;
	}

	// blur
	if (settings.EnableBlur) {
		TracyD3D11Zone(globals::state->tracyCtx, "HybridGI - Diffuse Blur");
		const uint passCount = settings.EnableAdaptiveDenoiser ? 2u : 1u;
		for (uint passIndex = 0; passIndex < passCount; ++passIndex) {
			resetViews();
			srvs.at(0) = texWorkingDepth->srv.get();
			srvs.at(1) = rts[NORMALROUGHNESS].SRV;
			srvs.at(2) = texAccumFrames[lastFrameAccumTexIdx]->srv.get();
			srvs.at(3) = texIlY[inputGITexIdx]->srv.get();
			srvs.at(4) = texIlCoCg[inputGITexIdx]->srv.get();

			uavs.at(0) = texAccumFrames[!lastFrameAccumTexIdx]->uav.get();
			uavs.at(1) = texIlY[!inputGITexIdx]->uav.get();
			uavs.at(2) = texIlCoCg[!inputGITexIdx]->uav.get();

			context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
			context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
			context->CSSetShader(passIndex == 0 ? blurCompute.get() : blurAtrousCompute.get(), nullptr, 0);
			globals::profiler->BeginPass(passIndex == 0 ? "HybridGI::Denoise" : "HybridGI::DenoiseAtrous");
			context->Dispatch((internalRes[0] + 7u) >> 3, (internalRes[1] + 7u) >> 3, 1);
			globals::profiler->EndPass();

			inputGITexIdx = !inputGITexIdx;
			lastFrameAccumTexIdx = !lastFrameAccumTexIdx;
		}
		lastFrameGITexIdx = inputGITexIdx;
	}

	// upsample
	if (settings.ResolutionMode != 0) {
		resetViews();
		srvs.at(0) = texWorkingDepth->srv.get();
		srvs.at(1) = texAo[inputAoTexIdx]->srv.get();
		srvs.at(2) = texIlY[inputGITexIdx]->srv.get();
		srvs.at(3) = texIlCoCg[inputGITexIdx]->srv.get();
		srvs.at(4) = texGiSpecular[inputSpecTexIdx]->srv.get();
		srvs.at(5) = texBentVisibility[inputBentTexIdx]->srv.get();
		srvs.at(6) = texNormal->srv.get();

		uavs.at(0) = texAo[!inputAoTexIdx]->uav.get();
		uavs.at(1) = texIlY[!inputGITexIdx]->uav.get();
		uavs.at(2) = texIlCoCg[!inputGITexIdx]->uav.get();
		uavs.at(3) = texGiSpecular[!inputSpecTexIdx]->uav.get();
		uavs.at(4) = texBentVisibility[!inputBentTexIdx]->uav.get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(upsampleCompute.get(), nullptr, 0);
		globals::profiler->BeginPass("HybridGI::Upsample");
		context->Dispatch((resolution[0] + 7u) >> 3, (resolution[1] + 7u) >> 3, 1);
		globals::profiler->EndPass();

		inputAoTexIdx = !inputAoTexIdx;
		inputGITexIdx = !inputGITexIdx;
		inputSpecTexIdx = !inputSpecTexIdx;
		inputBentTexIdx = !inputBentTexIdx;
	}

	outputAoIdx = inputAoTexIdx;
	outputIlIdx = inputGITexIdx;
	outputSpecIdx = inputSpecTexIdx;
	outputBentIdx = inputBentTexIdx;

	// cleanup
	resetViews();

	samplers.fill(nullptr);
	cb = nullptr;

	context->CSSetConstantBuffers(1, 1, &cb);
	context->CSSetSamplers(0, (uint)samplers.size(), samplers.data());
	context->CSSetShader(nullptr, nullptr, 0);
}

#undef I18N_KEY_PREFIX
