// Community Shaders HDR Display-derived file.
// Modified for PIXL Renderer, 2026: physical camera, capture/Director integration,
// exposure, stormglass, bloom and native Auto-DOF orchestration.
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional permissions are described in the repository EXCEPTIONS.md.

#include "CameraSuite.h"
#include "CameraSuite/CameraPolicy.h"

#include "PCH.h"

#include "Buffer.h"
#include "Globals.h"
#include "I18n/I18n.h"
#include "LinearLightCore.h"
#include "Menu.h"
#include "Menu/PIXLStyle.h"
#include "Menu/TuningWorkspaceRenderer.h"
#include "RainResponse.h"
#include "ShaderCache.h"
#include "SkyBounce.h"
#include "State.h"
#include "WeatherManager.h"
#include "VolumetricClouds.h"
#include "ImageReconstruction.h"
#include "Renderer/ExternalPostProcessing.h"
#include "Renderer/TemporalContext.h"
#include "Util.h"
#include <algorithm>
#include <cmath>
#include <dxgi1_4.h>
#include <dxgi1_6.h>
#include <imgui.h>
#include <WICTextureLoader.h>

#define I18N_KEY_PREFIX "feature.camera_suite."

namespace
{
	void DrawSettingsTooltip(const char* text)
	{
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", text);
	}

	void ApplyCameraLookPreset(CameraSuite::Settings& s, bool cinematic)
	{
		s.cameraAutoExposure = true;
		s.cameraExposureCompensationEV = 0.0f;
		s.cameraHighlightProtection = cinematic ? 0.78f : 0.65f;
		s.cameraShadowDetail = cinematic ? 0.18f : 0.14f;
		s.cameraContrast = cinematic ? 1.04f : 1.0f;
		s.cameraLocalExposure = cinematic ? 0.18f : 0.12f;
		s.cameraSaturation = cinematic ? 0.98f : 1.0f;
		s.cameraToe = cinematic ? 0.16f : 0.12f;
		s.cameraShoulder = cinematic ? 0.82f : 0.72f;
		s.cameraInfluence = cinematic ? 0.65f : 0.35f;
		s.experimentalBodycam = false;
		s.enableLensEffects = false;
	}
}
#define PIXL_CAMERA_SETTINGS_JSON_FIELDS(X) \
	X(enableHDR) \
	X(hdrPaperWhite) \
	X(hdrPeakNits) \
	X(hdrUIBrightness) \
	X(dontShowHDRWarning) \
	X(hdrAutoDetected) \
	X(enablePhysicalCamera) \
	X(cameraAutoExposure) \
	X(cameraExposureCompensationEV) \
	X(cameraMinExposureEV) \
	X(cameraMaxExposureEV) \
	X(cameraLowPercentile) \
	X(cameraHighPercentile) \
	X(cameraHighlightProtection) \
	X(cameraShadowDetail) \
	X(cameraContrast) \
	X(cameraLocalExposure) \
	X(cameraAdaptBrightToDark) \
	X(cameraAdaptDarkToBright) \
	X(cameraSaturation) \
	X(cameraToe) \
	X(cameraShoulder) \
	X(cameraInfluence) \
	X(menuSceneBrightness) \
	X(lookPreset) \
	X(lookOpacity) \
	X(enableBloom) \
	X(bloomStrength) \
	X(bloomThreshold) \
	X(bloomRadius) \
	X(enableStormglass) \
	X(stormglassStrength) \
	X(stormglassDropScale) \
	X(stormglassRefraction) \
	X(stormglassTrails) \
	X(stormglassDryingRate) \
	X(enableSubmergedOptics) \
	X(submergedStrength) \
	X(submergedBlur) \
	X(submergedRefraction) \
	X(submergedTransitionSpeed) \
	X(enableColdLens) \
	X(coldLensStrength) \
	X(coldAltitudeStart) \
	X(coldAltitudeFull) \
	X(enableElementalDamageLens) \
	X(elementalLensStrength) \
	X(enableSkyrimDepthOfField) \
	X(preferCinematicDoF) \
	X(enableEnhancedDepthOfField) \
	X(dofAutoFocus) \
	X(dofActorTracking) \
	X(dofStrength) \
	X(dofFocusDistance) \
	X(dofFocusRange) \
	X(dofBokehRadius) \
	X(dofHighlightResponse) \
	X(dofFocusEdgeProtection) \
	X(dofForegroundCoverage) \
	X(dofNearBlurIntensity) \
	X(dofFarBlurIntensity) \
	X(dofFarBlurDistance) \
	X(dofCatEye) \
	X(dofAnamorphicRatio) \
	X(dofPhysicalLens) \
	X(dofFocalLengthMm) \
	X(dofFStop) \
	X(dofSensorHeightMm) \
	X(dofFocusSpeed) \
	X(dofFocusDeadband) \
	X(dofMaxBokehPixels) \
	X(dofApertureBlades) \
	X(dofBladeCurvature) \
	X(dofApertureRotation) \
	X(enableModernMotionBlur) \
	X(motionBlurStrength) \
	X(motionBlurShutter) \
	X(motionBlurMaxPixels) \
	X(experimentalBodycam) \
	X(enableLensEffects) \
	X(bodycamStrength) \
	X(bodycamDistortion) \
	X(bodycamNoise) \
	X(bodycamVignette) \
	X(bodycamChromaticAberration) \
	X(bodycamSharpen) \
	X(bodycamExposureAggressiveness) \
	X(bodycamHighlightBloom) \
	X(bodycamWhiteBalance)

void to_json(nlohmann::json& json, const CameraSuite::Settings& settings)
{
	json = nlohmann::json::object();
#define PIXL_CAMERA_WRITE_JSON(field) json[#field] = settings.field;
	PIXL_CAMERA_SETTINGS_JSON_FIELDS(PIXL_CAMERA_WRITE_JSON)
#undef PIXL_CAMERA_WRITE_JSON
}

void from_json(const nlohmann::json& json, CameraSuite::Settings& settings)
{
	// WITH_DEFAULT semantics: missing fields retain the in-class defaults. This
	// keeps old user profiles forward compatible as CameraSuite grows.
#define PIXL_CAMERA_READ_JSON(field) \
	if (const auto it = json.find(#field); it != json.end() && !it->is_null()) \
		it->get_to(settings.field);
	PIXL_CAMERA_SETTINGS_JSON_FIELDS(PIXL_CAMERA_READ_JSON)
#undef PIXL_CAMERA_READ_JSON
}

#undef PIXL_CAMERA_SETTINGS_JSON_FIELDS

void CameraSuite::ApplyDepthOfFieldPreset(std::uint32_t preset)
{
	// Ordered as Gameplay, Balanced, Portrait and Cinema; the original values
	// are retained exactly, including the intentionally very distant gameplay haze.
	struct LensPreset
	{
		float strength, focalLength, fStop, maxBokeh;
		float nearBlur, farBlur, farDistance, focusRange;
	};
	static constexpr LensPreset presets[] = {
		{ 0.80f, 28.0f, 5.6f, 4.5f, 0.0f, 0.58f, 38000.0f, 15000.0f },
		{ 1.0f, 30.0f, 3.4f, 6.0f, 0.12f, 0.90f, 26000.0f, 12000.0f },
		{ 1.0f, 50.0f, 2.2f, 12.0f, 0.55f, 1.05f, 18000.0f, 7000.0f },
		{ 1.0f, 40.0f, 1.8f, 18.0f, 0.75f, 1.20f, 22000.0f, 9000.0f }
	};
	const auto& lens = presets[std::min(preset, 3u)];
	auto& s = settings;
	s.enableEnhancedDepthOfField = true;
	s.preferCinematicDoF = false;
	s.enableSkyrimDepthOfField = false;
	s.dofPhysicalLens = true;
	s.dofAutoFocus = true;
	s.dofActorTracking = true;
	s.dofStrength = lens.strength;
	s.dofFocalLengthMm = lens.focalLength;
	s.dofFStop = lens.fStop;
	s.dofMaxBokehPixels = lens.maxBokeh;
	s.dofNearBlurIntensity = lens.nearBlur;
	s.dofFarBlurIntensity = lens.farBlur;
	s.dofFarBlurDistance = lens.farDistance;
	s.dofFocusRange = lens.focusRange;
}

void CameraSuite::DrawSettings()
{
	ExternalPostProcessing::DrawSettings();
	auto hdrWarningPopupTitle = std::format("{}##CameraSuite", T(TKEY("warning_popup_title"), "HDR Warning"));

	if (isHDRMonitor) {
		Util::Text::Success(T(TKEY("display_detected"), "Camera Suite Detected"));
	} else if (isHDRCapableMonitor) {
		Util::Text::Warning(T(TKEY("capable_display_windows_hdr_off"), "HDR Capable Display (Windows HDR is off)"));
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T(TKEY("capable_display_windows_hdr_off_tooltip_0"), "Your monitor supports HDR, but Windows HDR is currently disabled."));
			ImGui::TextUnformatted(T(TKEY("capable_display_windows_hdr_off_tooltip_1"), "Enable HDR in Windows Display Settings to allow auto-detection."));
		}
	} else {
		Util::Text::Warning(T(TKEY("sdr_display_not_detected"), "SDR Display (HDR not detected)"));
	}

	const bool isExclusiveFullscreen = globals::pipeline::imageReconstruction.loaded ? !globals::pipeline::imageReconstruction.isWindowed : wasExclusiveFullscreen;

	if (isExclusiveFullscreen) {
		ImGui::Spacing();
		Util::Text::WrappedWarning(T(TKEY("exclusive_fullscreen_warning"), "WARNING: Exclusive Fullscreen detected."));
		Util::Text::WrappedWarning(T(TKEY("exclusive_fullscreen_warning_detail"), "HDR is not compatible with Exclusive Fullscreen and may not work correctly. Switch to Borderless Windowed mode for proper HDR support."));
		ImGui::Spacing();
	}

	ImGui::Spacing();

	bool oldEnableHDR;
	bool currentEnableHDR;
	{
		std::lock_guard<std::mutex> lock(settingsMutex);
		oldEnableHDR = settings.enableHDR;
		currentEnableHDR = settings.enableHDR;
	}

	// Disable the checkbox only when no HDR monitor is detected AND HDR is not already on
	// (allow disabling HDR even on SDR if it was enabled from saved settings).
	if (!isHDRMonitor && !currentEnableHDR) {
		ImGui::BeginDisabled();
	}

	if (ImGui::Checkbox(T(TKEY("enable_hdr"), "Enable HDR"), &currentEnableHDR)) {
		{
			std::lock_guard<std::mutex> lock(settingsMutex);
			settings.enableHDR = currentEnableHDR;
			if (settings.enableHDR != oldEnableHDR) {
				logger::info("HDR: enableHDR changed to: {}", settings.enableHDR);
				UpdateHDRData();
				UpdateSwapChainColorSpace();
			}
		}
	}

	if (!isHDRMonitor && !oldEnableHDR) {
		ImGui::EndDisabled();
	}

	if (auto _tt = Util::HoverTooltipWrapper()) {
		if (isHDRMonitor) {
			ImGui::TextUnformatted(T(TKEY("enable_hdr_tooltip"), "Enable HDR output. Matches vanilla visuals with extended dynamic range."));
		} else if (isHDRCapableMonitor) {
			ImGui::TextUnformatted(T(TKEY("enable_hdr_tooltip_windows_off"), "Monitor supports HDR but Windows HDR is off. Enable HDR in Windows Display Settings, then restart the game."));
		} else {
			ImGui::TextUnformatted(T(TKEY("enable_hdr_tooltip_not_detected"), "HDR display not detected. Use Advanced button to override."));
		}
	}

	// Advanced override button â€” shown when HDR is neither active nor auto-detected
	if (!isHDRMonitor && !oldEnableHDR) {
		ImGui::SameLine();
		if (ImGui::Button(T(TKEY("advanced"), "Advanced"))) {
			bool dontShowWarning;
			{
				std::lock_guard<std::mutex> lock(settingsMutex);
				dontShowWarning = settings.dontShowHDRWarning;
			}
			if (!dontShowWarning) {
				pendingHDREnable = true;
				showHDRWarningPopup = true;
				ImGui::OpenPopup(hdrWarningPopupTitle.c_str());
			} else {
				// User previously dismissed warnings, enable directly
				{
					std::lock_guard<std::mutex> lock(settingsMutex);
					settings.enableHDR = true;
					logger::info("HDR: enableHDR changed to: true (advanced override, warning suppressed)");
					UpdateHDRData();
					UpdateSwapChainColorSpace();
				}
			}
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			if (isHDRCapableMonitor) {
				ImGui::TextUnformatted(T(TKEY("advanced_tooltip_enable_windows_hdr"), "Enable Windows HDR instead of forcing it here."));
			} else {
				ImGui::TextUnformatted(T(TKEY("advanced_tooltip_force_enable"), "Force enable HDR even without detection (not recommended)."));
			}
		}
	}

	{
		std::lock_guard<std::mutex> lock(settingsMutex);
		if (!isHDRMonitor && settings.enableHDR) {
			ImGui::Spacing();
			Util::Text::WrappedWarning(T(TKEY("enabled_without_detected_display"), "HDR is enabled but no HDR display was detected."));
		}
	}

	if (auto popup = Util::CenteredPopupModal(hdrWarningPopupTitle.c_str(), &showHDRWarningPopup, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
		// Prevent background dimming by pushing lower modal dimming
		ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);

		Util::Text::Warning(T(TKEY("force_enable_hdr_warning"), "WARNING: Force Enable HDR"));
		ImGui::Separator();
		ImGui::Spacing();
		Util::Text::WrappedWarning(T(TKEY("force_enable_hdr_detected_warning"), "HDR was not detected on your monitor."));
		Util::Text::WrappedWarning(T(TKEY("force_enable_hdr_sdr_warning"), "The game will look VERY WRONG on an SDR (standard) display."));
		ImGui::Spacing();
		ImGui::TextWrapped("%s", T(TKEY("force_enable_hdr_confirm"), "Only proceed if you have an HDR-capable display that was not detected correctly."));
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		const auto buttonWidthForLabel = [](const char* label) {
			return ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
		};
		const char* forceEnableLabel = T(TKEY("force_enable_hdr"), "Force Enable HDR");
		const char* cancelLabel = T(TKEY("cancel"), "Cancel");
		const float buttonWidth = std::max({
			ThemeManager::Constants::POPUP_BUTTON_WIDTH * Util::GetUIScale(),
			buttonWidthForLabel(forceEnableLabel),
			buttonWidthForLabel(cancelLabel)
		});

		if (ImGui::Button(forceEnableLabel, ImVec2(buttonWidth, 0))) {
			{
				std::lock_guard<std::mutex> lock(settingsMutex);
				settings.enableHDR = true;
				logger::info("HDR: enableHDR changed to: true (forced override)");
				UpdateHDRData();
				UpdateSwapChainColorSpace();
			}
			showHDRWarningPopup = false;
			pendingHDREnable = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button(cancelLabel, ImVec2(buttonWidth, 0))) {
			{
				std::lock_guard<std::mutex> lock(settingsMutex);
				settings.enableHDR = false;
			}
			showHDRWarningPopup = false;
			pendingHDREnable = false;
			ImGui::CloseCurrentPopup();
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		bool dontShowWarning;
		{
			std::lock_guard<std::mutex> lock(settingsMutex);
			dontShowWarning = settings.dontShowHDRWarning;
		}
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, ImGui::GetStyle().FramePadding.y * 0.5f));
		ImGui::SetWindowFontScale(0.9f);
		if (ImGui::Checkbox(T(TKEY("dont_show_again"), "Don't show me this again"), &dontShowWarning)) {
			std::lock_guard<std::mutex> lock(settingsMutex);
			settings.dontShowHDRWarning = dontShowWarning;
		}
		ImGui::SetWindowFontScale(1.0f);
		ImGui::PopStyleVar();

		ImGui::PopStyleVar();
	}

	bool isHDREnabled;
	{
		std::lock_guard<std::mutex> lock(settingsMutex);
		isHDREnabled = settings.enableHDR;
	}

	if (isHDREnabled) {
		ImGui::Spacing();

		uint oldPaperWhite;
		int currentPaperWhite;
		uint oldPeakNits;
		int currentPeakNits;
		{
			std::lock_guard<std::mutex> lock(settingsMutex);
			oldPaperWhite = settings.hdrPaperWhite;
			currentPaperWhite = static_cast<int>(settings.hdrPaperWhite);
			oldPeakNits = settings.hdrPeakNits;
			currentPeakNits = static_cast<int>(settings.hdrPeakNits);
		}

		ImGui::SliderInt(T(TKEY("paper_white_nits"), "Paper White (nits)"), &currentPaperWhite, 80, 500, "%d", ImGuiSliderFlags_AlwaysClamp);
		{
			std::lock_guard<std::mutex> lock(settingsMutex);
			if (currentPaperWhite >= static_cast<int>(settings.hdrPeakNits)) {
				currentPaperWhite = static_cast<int>(settings.hdrPeakNits) - 1;
			}
			settings.hdrPaperWhite = static_cast<uint>(std::max(currentPaperWhite, 1));
			if (oldPaperWhite != settings.hdrPaperWhite) {
				UpdateHDRData();
			}
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T(TKEY("paper_white_tooltip_0"), "How bright SDR white appears on your HDR display."));
			ImGui::TextUnformatted(T(TKEY("paper_white_tooltip_1"), "203 nits is the ITU BT.2408 reference. Increase for a brighter image."));
		}

		ImGui::SliderInt(T(TKEY("peak_brightness_nits"), "Peak Brightness (nits)"), &currentPeakNits, 400, 10000, "%d", ImGuiSliderFlags_AlwaysClamp);
		{
			std::lock_guard<std::mutex> lock(settingsMutex);
			if (currentPeakNits <= static_cast<int>(settings.hdrPaperWhite)) {
				currentPeakNits = static_cast<int>(settings.hdrPaperWhite) + 1;
			}
			settings.hdrPeakNits = static_cast<uint>(currentPeakNits);
			if (oldPeakNits != settings.hdrPeakNits) {
				UpdateHDRData();
			}
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T(TKEY("peak_brightness_tooltip_0"), "Maximum brightness your display can produce."));
			ImGui::TextUnformatted(T(TKEY("peak_brightness_tooltip_1"), "Set to match your display's actual peak brightness."));
		}

		ImGui::TextDisabled(T(TKEY("display_reports_max_nits"), "Display reports: %.0f nits max"), cachedDisplayMaxLuminance);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T(TKEY("display_reports_max_nits_tooltip_0"), "Reported by OS/driver (DXGI MaxLuminance), not a direct meter reading."));
			ImGui::TextUnformatted(T(TKEY("display_reports_max_nits_tooltip_1"), "It may be EDID metadata and can differ from real highlight peak output."));
			ImGui::TextUnformatted(T(TKEY("display_reports_max_nits_tooltip_2"), "Treat this as a starting point and tune Peak Brightness as needed."));
		}
	}

	// UI brightness slider - only shown when HDR is enabled
	ImGui::Spacing();
	{
		std::lock_guard<std::mutex> lock(settingsMutex);
		if (settings.enableHDR) {
			float oldUIBrightness = settings.hdrUIBrightness;
			float currentUIBrightness = settings.hdrUIBrightness;

			ImGui::SliderFloat(T(TKEY("ui_brightness_multiplier"), "UI Brightness Multiplier"), &currentUIBrightness, 0.5f, 5.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
			if (oldUIBrightness != currentUIBrightness) {
				settings.hdrUIBrightness = currentUIBrightness;
				UpdateHDRData();
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted(T(TKEY("ui_brightness_multiplier_tooltip_0"), "UI brightness = Paper White x this multiplier in HDR mode."));
				ImGui::TextUnformatted(T(TKEY("ui_brightness_multiplier_tooltip_1"), "1.00x = UI renders at Paper White brightness. Higher values make UI brighter relative to scene content."));
				ImGui::TextUnformatted(T(TKEY("ui_brightness_multiplier_tooltip_2"), "Note: Main menu and loading screens always render at Paper White brightness."));
			}
		}
	}

	// ---------------------------------------------------------------------
	// PIXL PHYSICAL CAMERA
	// ---------------------------------------------------------------------
	ImGui::Spacing();
	ImGui::SeparatorText("Physical Camera");
	ImGui::TextWrapped("Scene-aware exposure and a luminance-preserving camera response keep bright sources intense without throwing away useful shadow detail. Works in SDR and HDR; Linear Light Core provides the cleanest scene-referred input.");
	{
		std::lock_guard<std::mutex> lock(settingsMutex);
		bool changed = false;
		changed |= Util::FeatureToggle("Enable Physical Camera", &settings.enablePhysicalCamera);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Runs the PIXL camera mapper before display output. Disable to return to the normal PIXL Renderer display path.");

		if (settings.enablePhysicalCamera) {
			ImGui::TextDisabled("Recommended: Auto Exposure ON, 0.0 EV, neutral preset");
			changed |= ImGui::SliderFloat("Camera Influence", &settings.cameraInfluence, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			DrawSettingsTooltip("Controls the SDR blend between Skyrim's authored image and PIXL's photographic response. 25-40% retains the original art direction; HDR always uses the full display mapper.");
			if (ImGui::Button("Neutral")) {
				ApplyCameraLookPreset(settings, false);
				changed = true;
			}
			DrawSettingsTooltip("Restores a neutral, palette-preserving physical-camera response. Applies immediately.");
			ImGui::SameLine();
			if (ImGui::Button("Cinematic")) {
				ApplyCameraLookPreset(settings, true);
				changed = true;
			}
			DrawSettingsTooltip("Uses stronger highlight protection and local adaptation with a restrained cinematic contrast curve. Applies immediately.");

			if (ImGui::CollapsingHeader("LUT / Tonemap", ImGuiTreeNodeFlags_DefaultOpen)) {
				const char* looks[] = {
					"Original", "Nordic Neutral", "Saga", "Dramatic", "Hearthfire", "Bleak",
					"Bleach", "Winter", "Sunset", "Fantasy Green", "Nightfall", "Cinematic"
				};
				const int previousLook = std::clamp(static_cast<int>(settings.lookPreset), 0, static_cast<int>(std::size(looks)) - 1);
				int selectedLook = previousLook;
				if (ImGui::Combo("Colour Grade", &selectedLook, looks, static_cast<int>(std::size(looks)))) {
					settings.lookPreset = static_cast<uint>(std::clamp(selectedLook, 0, static_cast<int>(std::size(looks)) - 1));
					if (selectedLook == 11 && previousLook != 11)
						settings.lookOpacity = 0.07f;  // Cinematic remains a subtle, user-adjustable starting point.
					LoadLookTexture();
					changed = true;
				}
				DrawSettingsTooltip("Selects a PIXL-authored 32-point LUT after the physical camera and before UI composition. Original is a neutral bypass.");
				ImGui::BeginDisabled(settings.lookPreset == 0);
				int lutOpacityPercent = std::clamp(static_cast<int>(std::lround(settings.lookOpacity * 100.0f)), 0, 100);
				if (ImGui::SliderInt("LUT Opacity", &lutOpacityPercent, 0, 100, "%d%%", ImGuiSliderFlags_AlwaysClamp)) {
					settings.lookOpacity = static_cast<float>(lutOpacityPercent) * 0.01f;
					changed = true;
				}
				ImGui::EndDisabled();
				DrawSettingsTooltip("Blends the selected colour grade from 0 to 100%. Start low to preserve weather and texture colours; Cinematic starts at a restrained 7%.");
			}
			if (ImGui::Button("Bodycam Preset")) {
				settings.cameraAutoExposure = true;
				settings.cameraExposureCompensationEV = 0.0f;
				settings.cameraAdaptBrightToDark = 0.65f;
				settings.cameraAdaptDarkToBright = 0.65f;
				settings.experimentalBodycam = true;
				settings.enableLensEffects = true;
				settings.bodycamStrength = 0.82f;
				settings.bodycamDistortion = 0.13f;
				settings.bodycamNoise = 0.20f;
				settings.bodycamVignette = 0.09f;
				settings.bodycamChromaticAberration = 0.022f;
				settings.bodycamSharpen = 0.12f;
				settings.bodycamExposureAggressiveness = 0.82f;
				settings.bodycamHighlightBloom = 0.18f;
				settings.bodycamWhiteBalance = 0.14f;
				changed = true;
			}
			DrawSettingsTooltip("Enables the experimental body-worn digital-camera profile. This is a stylized preset and is not the recommended neutral rendering mode.");

			changed |= ImGui::Checkbox("Automatic Exposure", &settings.cameraAutoExposure);
			DrawSettingsTooltip("Uses broad, equally weighted frame metering with bright/dark outlier rejection, so a small centred candle does not dominate the room. Disable for a fixed exposure offset.");
			changed |= ImGui::SliderFloat("Exposure Compensation", &settings.cameraExposureCompensationEV, -4.0f, 4.0f, "%+.2f EV", ImGuiSliderFlags_AlwaysClamp);
			DrawSettingsTooltip("Offsets the physical-camera exposure in stops. Positive values brighten the scene; negative values preserve more highlight headroom.");
			changed |= ImGui::SliderFloat("Highlight Protection", &settings.cameraHighlightProtection, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			DrawSettingsTooltip("Compresses the brightest scene values before display mapping. Higher values retain more bright-source detail with a softer roll-off.");
			changed |= ImGui::SliderFloat("Shadow Detail", &settings.cameraShadowDetail, 0.0f, 0.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			DrawSettingsTooltip("Lifts recoverable low-luminance detail without changing the metered exposure. Excessive values can flatten scene depth.");
			changed |= ImGui::SliderFloat("Local Exposure", &settings.cameraLocalExposure, 0.0f, 0.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			DrawSettingsTooltip("Blends a small bilateral neighbourhood exposure correction to balance bright windows and dark interiors while respecting edges.");

			if (ImGui::CollapsingHeader("Camera Advanced")) {
				changed |= ImGui::SliderFloat("Minimum Exposure", &settings.cameraMinExposureEV, -10.0f, 0.0f, "%+.1f EV", ImGuiSliderFlags_AlwaysClamp);
				DrawSettingsTooltip("Lower exposure limit. Gameplay auto exposure stays within 1 EV below your compensation to keep dark scenes readable; manual exposure can still use the full range.");
				changed |= ImGui::SliderFloat("Maximum Exposure", &settings.cameraMaxExposureEV, 0.0f, 10.0f, "%+.1f EV", ImGuiSliderFlags_AlwaysClamp);
				DrawSettingsTooltip("Brightest exposure the automatic meter may select. This prevents extreme amplification in very dark scenes.");
				changed |= ImGui::SliderFloat("Metering Low Percentile", &settings.cameraLowPercentile, 0.0f, 0.20f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
				DrawSettingsTooltip("Fraction of the darkest histogram samples ignored by exposure metering. Raise it to prevent tiny black regions from biasing exposure.");
				changed |= ImGui::SliderFloat("Metering High Percentile", &settings.cameraHighPercentile, 0.80f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
				DrawSettingsTooltip("Upper histogram percentile used by exposure metering. Lower it to reject small, extremely bright outliers.");
				changed |= ImGui::SliderFloat("Bright to Dark Adaptation", &settings.cameraAdaptBrightToDark, 0.65f, 4.0f, "%.2f s", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
				DrawSettingsTooltip("Approximate adaptation time when entering a darker environment. A 0.65 s minimum prevents noticeable exposure pumping during camera turns.");
				changed |= ImGui::SliderFloat("Dark to Bright Adaptation", &settings.cameraAdaptDarkToBright, 0.65f, 2.0f, "%.2f s", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
				DrawSettingsTooltip("Approximate adaptation time when entering a brighter environment. A 0.65 s minimum prevents rapid gameplay exposure flashes.");
				changed |= ImGui::SliderFloat("Contrast", &settings.cameraContrast, 0.75f, 1.30f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				DrawSettingsTooltip("Mid-tone contrast applied by the display response. 1.0 is neutral.");
				changed |= ImGui::SliderFloat("Saturation", &settings.cameraSaturation, 0.70f, 1.25f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				DrawSettingsTooltip("Output colour saturation after exposure and highlight handling. 1.0 preserves the source saturation.");
				changed |= ImGui::SliderFloat("Shadow Toe", &settings.cameraToe, 0.0f, 0.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				DrawSettingsTooltip("Shapes the darkest part of the response curve. Higher values produce a firmer black toe.");
				changed |= ImGui::SliderFloat("Highlight Shoulder", &settings.cameraShoulder, 0.2f, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				DrawSettingsTooltip("Shapes the highlight roll-off. Higher values create a broader, softer shoulder before the display peak.");
			}

			if (ImGui::CollapsingHeader("Lens Finishing")) {
				changed |= ImGui::Checkbox("Enable Lens Finishing", &settings.enableLensEffects);
				DrawSettingsTooltip("Enables the three independent lens controls below without requiring Bodycam. Off by default for a clean image.");
				ImGui::BeginDisabled(!settings.enableLensEffects);
				changed |= ImGui::SliderFloat("Low-Light Sensor Noise", &settings.bodycamNoise, 0.0f, 0.60f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				DrawSettingsTooltip("Subtle gain-dependent grain in dark regions. Increases shader work only when enabled.");
				changed |= ImGui::SliderFloat("Lens Vignette", &settings.bodycamVignette, 0.0f, 0.30f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				DrawSettingsTooltip("Gently darkens the outer image. Keep low for natural gameplay visibility.");
				changed |= ImGui::SliderFloat("Edge Aberration", &settings.bodycamChromaticAberration, 0.0f, 0.08f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
				DrawSettingsTooltip("Subtle edge-only colour separation. Requires two additional scene samples where active.");
				ImGui::EndDisabled();
			}

			if (ImGui::CollapsingHeader("Experimental - Bodycam Emulation")) {
				Util::Text::WrappedWarning("Experimental. Emulates a modern body-worn digital camera with wide-lens distortion, highlight bloom and edge processing. Lens Finishing controls are separate. Exposure adaptation is bounded for gameplay stability. UI is intentionally left undistorted.");
				changed |= ImGui::Checkbox("Enable Bodycam Emulation", &settings.experimentalBodycam);
				DrawSettingsTooltip("Master switch for the stylized lens/sensor pass. Updates immediately and leaves UI rendering undistorted.");
				if (settings.experimentalBodycam) {
					changed |= ImGui::SliderFloat("Emulation Strength", &settings.bodycamStrength, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
					DrawSettingsTooltip("Overall blend between the neutral camera output and the bodycam emulation.");
					changed |= ImGui::SliderFloat("Wide Lens Distortion", &settings.bodycamDistortion, 0.0f, 0.30f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
					DrawSettingsTooltip("Radial wide-angle lens distortion applied near the image edges.");
					changed |= ImGui::SliderFloat("Digital Sharpness", &settings.bodycamSharpen, 0.0f, 0.35f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
					DrawSettingsTooltip("Adds restrained edge enhancement after lens and exposure processing.");
					changed |= ImGui::SliderFloat("Exposure Aggressiveness", &settings.bodycamExposureAggressiveness, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
					DrawSettingsTooltip("Scales the bodycam profile's faster, more visible automatic-exposure response.");
					changed |= ImGui::SliderFloat("Highlight Bloom", &settings.bodycamHighlightBloom, 0.0f, 0.50f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
					DrawSettingsTooltip("Adds a compact bright-source glow inside the bodycam emulation. This is separate from Skyrim's image-space bloom.");
					changed |= ImGui::SliderFloat("Auto White Balance", &settings.bodycamWhiteBalance, 0.0f, 0.40f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
					DrawSettingsTooltip("Strength of the bodycam profile's neutralising white-balance estimate.");
				}
			}
		}

		if (ImGui::CollapsingHeader("Bloom", ImGuiTreeNodeFlags_DefaultOpen)) {
			changed |= ImGui::Checkbox("Enable PIXL Bloom", &settings.enableBloom);
			DrawSettingsTooltip("Scene-linear soft-knee bloom integrated into PIXL Physical Camera. Bright-source colour is retained and isolated fireflies are bounded.");
			ImGui::BeginDisabled(!settings.enableBloom);
			changed |= ImGui::SliderFloat("Bloom Strength", &settings.bloomStrength, 0.0f, 3.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::SliderFloat("Bloom Threshold", &settings.bloomThreshold, 0.0f, 5.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::SliderFloat("Bloom Radius", &settings.bloomRadius, 0.1f, 5.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
			ImGui::EndDisabled();
		}

		if (ImGui::CollapsingHeader("Stormglass Weather Lens", ImGuiTreeNodeFlags_DefaultOpen)) {
			changed |= ImGui::Checkbox("Enable Stormglass", &settings.enableStormglass);
			DrawSettingsTooltip("Adds procedural rain beads and draining trails using Skyrim's live precipitation intensity. The scene refracts through the wet optical surface while HUD and menus remain sharp.");
			ImGui::BeginDisabled(!settings.enableStormglass);
			changed |= ImGui::SliderFloat("Stormglass Presence", &settings.stormglassStrength, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::SliderFloat("Drop Scale", &settings.stormglassDropScale, 0.50f, 2.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::SliderFloat("Optical Refraction", &settings.stormglassRefraction, 0.0f, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::SliderFloat("Trail Formation", &settings.stormglassTrails, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::SliderFloat("Drying Rate", &settings.stormglassDryingRate, 0.01f, 0.50f, "%.3f/s", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
			DrawSettingsTooltip("How quickly residual beads clear once rainfall stops. Surface-break water begins fully wet and uses the same natural drying state.");
			ImGui::EndDisabled();
		}

		if (ImGui::CollapsingHeader("Submerged Optics", ImGuiTreeNodeFlags_DefaultOpen)) {
			changed |= ImGui::Checkbox("Enable Submerged Optics", &settings.enableSubmergedOptics);
			DrawSettingsTooltip("Refines Skyrim's authored underwater image with restrained water-type colour absorption, optical softening and a smooth surface-break transition.");
			ImGui::BeginDisabled(!settings.enableSubmergedOptics);
			changed |= ImGui::SliderFloat("Underwater Presence", &settings.submergedStrength, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::SliderFloat("Optical Softening", &settings.submergedBlur, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::SliderFloat("Water Refraction", &settings.submergedRefraction, 0.0f, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::SliderFloat("Surface Transition", &settings.submergedTransitionSpeed, 0.5f, 8.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
			DrawSettingsTooltip("Controls how quickly the optical response settles when entering or leaving water. Breaking the surface transfers a draining film into Stormglass.");
			ImGui::EndDisabled();
		}

		if (ImGui::CollapsingHeader("Cold & Elemental Lens", ImGuiTreeNodeFlags_DefaultOpen)) {
			changed |= ImGui::Checkbox("Environmental Frost Edges", &settings.enableColdLens);
			DrawSettingsTooltip("Builds a restrained crystalline edge response from live snowfall and high exterior altitude. It never affects HUD/menu composition and fades naturally in warmer clear areas.");
			ImGui::BeginDisabled(!settings.enableColdLens);
			changed |= ImGui::SliderFloat("Frost Edge Strength", &settings.coldLensStrength, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::SliderFloat("Cold Altitude Start", &settings.coldAltitudeStart, 0.0f, 70000.0f, "%.0f units", ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::SliderFloat("Cold Altitude Full", &settings.coldAltitudeFull, 1000.0f, 90000.0f, "%.0f units", ImGuiSliderFlags_AlwaysClamp);
			settings.coldAltitudeFull = std::max(settings.coldAltitudeFull, settings.coldAltitudeStart + 1000.0f);
			ImGui::EndDisabled();
			changed |= ImGui::Checkbox("Elemental Hit Optics", &settings.enableElementalDamageLens);
			DrawSettingsTooltip("Confirmed fire and frost projectile hits on the player produce short heat-distortion or ice-edge pulses. This includes compatible dragon breath projectiles without guessing from nearby spell visuals.");
			ImGui::BeginDisabled(!settings.enableElementalDamageLens);
			changed |= ImGui::SliderFloat("Elemental Presence", &settings.elementalLensStrength, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::EndDisabled();
		}

		if (ImGui::CollapsingHeader("Skyrim Depth of Field", ImGuiTreeNodeFlags_DefaultOpen)) {
			if (IsCinematicDoFLoaded()) {
				changed |= ImGui::Checkbox("Let Cinematic DoF own depth of field", &settings.preferCinematicDoF);
				DrawSettingsTooltip("Leaves depth of field to the installed standalone effect. Turn this off to use PIXL's native buffer-integrated cinematic focus instead.");
				ImGui::TextWrapped("Cinematic DoF Standalone detected. PIXL can either leave it in control or use its own native DOF path.");
			}
			ImGui::BeginDisabled(UsesCinematicDoF());
			changed |= ImGui::Checkbox("Enable PIXL Auto-DOF", &settings.enableEnhancedDepthOfField);
			DrawSettingsTooltip("Uses PIXL's native scene color and depth buffers with a restrained bokeh response. It avoids the heavy constant blur of the legacy path and remains disabled while Cinematic DoF owns the effect.");
			if (settings.enableEnhancedDepthOfField) {
				ImGui::TextDisabled("QUICK LENS PRESETS");
				static constexpr const char* dofPresetNames[] = { "GAMEPLAY", "BALANCED", "PORTRAIT", "CINEMA" };
				static constexpr const char* dofPresetTips[] = {
					"Restrained separation for normal movement.",
					"A versatile exploration and dialogue baseline.",
					"Stronger subject separation for portraits.",
					"A more expressive Director-style lens."
				};
				const float presetGap = PIXLUI::Ref(5.0f);
				const float availablePresetWidth = ImGui::GetContentRegionAvail().x;
				const std::uint32_t presetColumns = availablePresetWidth < PIXLUI::Ref(360.0f) ? 2u : 4u;
				const float presetWidth = std::max(1.0f,
					(availablePresetWidth - presetGap * static_cast<float>(presetColumns - 1u)) /
					static_cast<float>(presetColumns));
				for (std::uint32_t preset = 0; preset < std::size(dofPresetNames); ++preset) {
					if (preset % presetColumns != 0)
						ImGui::SameLine(0.0f, presetGap);
					ImGui::PushID(static_cast<int>(preset));
					if (ImGui::Button(dofPresetNames[preset], ImVec2(presetWidth, PIXLUI::Ref(30.0f)))) {
						ApplyDepthOfFieldPreset(preset);
						changed = true;
					}
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("%s", dofPresetTips[preset]);
					ImGui::PopID();
				}
				ImGui::Dummy(ImVec2(0.0f, PIXLUI::Ref(3.0f)));
				changed |= ImGui::Checkbox("Track Dialogue Actors", &settings.dofActorTracking);
				DrawSettingsTooltip("During dialogue, smoothly follows the visible speaker's head instead of jumping between screen-depth samples. Outside dialogue, PIXL uses stable screen autofocus.");
				changed |= ImGui::Checkbox("Autofocus from Scene Depth", &settings.dofAutoFocus);
				DrawSettingsTooltip("Samples the scene depth around the screen centre and eases focus changes over time to prevent pumping during camera movement.");
				changed |= ImGui::SliderFloat("DOF Strength", &settings.dofStrength, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				DrawSettingsTooltip("Controls the visible separation. Lower values keep normal gameplay readable; higher values produce stronger cinematic focus.");
				changed |= ImGui::SliderFloat("Near Blur Intensity", &settings.dofNearBlurIntensity, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				DrawSettingsTooltip("Controls foreground/near-field blur independently from autofocus. Higher values expand nearby bokeh more strongly over the background.");
				changed |= ImGui::SliderFloat("Far / Distance Blur", &settings.dofFarBlurIntensity, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				DrawSettingsTooltip("Adds controllable distance blur over autofocus to soften distant LOD transitions and background detail. Higher values increase the far-field haze.");
				changed |= ImGui::SliderFloat("Distance Blur Start", &settings.dofFarBlurDistance, 500.0f, 50000.0f, "%.0f game units", ImGuiSliderFlags_AlwaysClamp);
				DrawSettingsTooltip("The distance where the additional far-field haze begins. Autofocus remains physical-camera based and independent of this threshold.");
				changed |= ImGui::SliderFloat("Focus / Distance Fade", &settings.dofFocusRange, 240.0f, 20000.0f, "%.0f game units", ImGuiSliderFlags_AlwaysClamp);
				DrawSettingsTooltip("Controls the soft focus transition and how gradually the distance blur reaches full strength. Long values such as 10000-15000 keep gameplay readable while hiding distant LOD changes.");
				if (ImGui::TreeNode("Advanced Lens")) {
					changed |= ImGui::Checkbox("Physical Thin-Lens Model", &settings.dofPhysicalLens);
					DrawSettingsTooltip("Uses focal length, sensor size and f-stop to derive a signed, resolution-aware circle of confusion. Director and Photo modes use this automatically.");
					ImGui::BeginDisabled(!settings.dofPhysicalLens && !TuningWorkspaceRenderer::IsDirectorPhotoModeActive() && !TuningWorkspaceRenderer::IsDirectorVideoModeActive());
					changed |= ImGui::SliderFloat("Focal Length", &settings.dofFocalLengthMm, 18.0f, 200.0f, "%.0f mm", ImGuiSliderFlags_AlwaysClamp);
					changed |= ImGui::SliderFloat("F-Stop", &settings.dofFStop,
						static_cast<float>(PIXL::Metadata::Settings::CameraDofFStop.minimum),
						static_cast<float>(PIXL::Metadata::Settings::CameraDofFStop.maximum), "f/%.1f", ImGuiSliderFlags_AlwaysClamp);
					changed |= ImGui::SliderFloat("Sensor Height", &settings.dofSensorHeightMm, 10.0f, 40.0f, "%.1f mm", ImGuiSliderFlags_AlwaysClamp);
					changed |= ImGui::SliderFloat("Focus Speed", &settings.dofFocusSpeed, 0.25f, 20.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
					changed |= ImGui::SliderFloat("Maximum Bokeh Radius", &settings.dofMaxBokehPixels, 4.0f, 96.0f, "%.0f px", ImGuiSliderFlags_AlwaysClamp);
					int apertureBlades = static_cast<int>(settings.dofApertureBlades);
					if (ImGui::SliderInt("Aperture Blades", &apertureBlades, 3, 12)) {
						settings.dofApertureBlades = static_cast<uint>(apertureBlades);
						changed = true;
					}
					changed |= ImGui::SliderFloat("Blade Curvature", &settings.dofBladeCurvature, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
					changed |= ImGui::SliderAngle("Aperture Rotation", &settings.dofApertureRotation, -180.0f, 180.0f);
					changed |= ImGui::SliderFloat("Cat-Eye", &settings.dofCatEye, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
					changed |= ImGui::SliderFloat("Anamorphic Ratio", &settings.dofAnamorphicRatio, 0.5f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
					ImGui::EndDisabled();
					ImGui::TreePop();
				}
			}
			ImGui::EndDisabled();
			ImGui::BeginDisabled(settings.enableEnhancedDepthOfField || UsesCinematicDoF());
			changed |= ImGui::Checkbox("Enable Skyrim Depth of Field", &settings.enableSkyrimDepthOfField);
			DrawSettingsTooltip("Enables Skyrim's authored image-space depth of field when PIXL Auto-DOF is not active.");
			ImGui::EndDisabled();
		}

		if (ImGui::CollapsingHeader("Modern Motion Blur", ImGuiTreeNodeFlags_DefaultOpen)) {
			changed |= ImGui::Checkbox("Enable Camera Motion Blur", &settings.enableModernMotionBlur);
			DrawSettingsTooltip("Depth-aware, shutter-based camera motion blur. UI remains sharp, depth edges reject background bleeding, and tiny temporal jitter is ignored.");
			ImGui::BeginDisabled(!settings.enableModernMotionBlur);
			changed |= ImGui::SliderFloat("Motion Blur Strength", &settings.motionBlurStrength, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::SliderFloat("Shutter Angle", &settings.motionBlurShutter, 0.10f, 1.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
			DrawSettingsTooltip("Scales camera travel captured during the frame. 0.5 approximates a cinematic 180-degree shutter.");
			changed |= ImGui::SliderFloat("Maximum Motion", &settings.motionBlurMaxPixels, 4.0f, 48.0f, "%.0f px", ImGuiSliderFlags_AlwaysClamp);
			DrawSettingsTooltip("Bounds the blur footprint during rapid turns so the image remains readable and stable.");
			ImGui::EndDisabled();
		}

		if (ImGui::CollapsingHeader("Menus and Loading Screens")) {
			changed |= ImGui::SliderFloat("Scene Brightness", &settings.menuSceneBrightness, 0.75f, 3.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
			DrawSettingsTooltip("Brightness applied only to the display-referred main-menu and loading-screen scene layer. Gameplay exposure and UI text are unchanged. Applies immediately.");
		}

		if (changed) {
			settings.cameraLowPercentile = std::clamp(settings.cameraLowPercentile, 0.0f, 0.20f);
			settings.cameraHighPercentile = std::clamp(settings.cameraHighPercentile,
				std::max(0.80f, settings.cameraLowPercentile + 0.05f), 1.0f);
			UpdateHDRData();
		}
	}
}

void CameraSuite::ApplyExternalLook(float exposureEV, float contrast, float saturation, float adaptationSeconds, float highlightProtection, float shadowDetail, float toe, float shoulder, bool bloomEnabled, float bloomStrength, uint lookPreset, float lookOpacity, float influence)
{
	std::lock_guard<std::mutex> lock(settingsMutex);
	const auto finiteOr = [](float value, float fallback) { return std::isfinite(value) ? value : fallback; };
	settings.enablePhysicalCamera = true;
	settings.cameraAutoExposure = true;
	settings.cameraExposureCompensationEV = std::clamp(finiteOr(exposureEV, 0.0f), -4.0f, 4.0f);
	settings.cameraContrast = std::clamp(contrast, 0.75f, 1.30f);
	settings.cameraSaturation = std::clamp(saturation, 0.70f, 1.25f);
	settings.cameraAdaptBrightToDark = std::clamp(finiteOr(adaptationSeconds, 1.20f), 0.65f, 4.0f);
	settings.cameraHighlightProtection = std::clamp(finiteOr(highlightProtection, 0.65f), 0.0f, 1.0f);
	settings.cameraShadowDetail = std::clamp(shadowDetail, 0.0f, 0.5f);
	settings.cameraToe = std::clamp(toe, 0.0f, 0.5f);
	settings.cameraShoulder = std::clamp(shoulder, 0.2f, 1.5f);
	settings.enableBloom = bloomEnabled;
	settings.bloomStrength = std::clamp(bloomStrength, 0.0f, 3.0f);
	settings.lookPreset = std::min(lookPreset, 11u);
	settings.lookOpacity = std::clamp(lookOpacity, 0.0f, 1.0f);
	settings.cameraInfluence = std::clamp(influence, 0.0f, 1.0f);
}

#undef I18N_KEY_PREFIX

void CameraSuite::SaveSettings(json& o_json)
{
	std::lock_guard<std::mutex> lock(settingsMutex);
	const auto& settingsToSave = persistentSettingsOverride ? *persistentSettingsOverride : settings;
	o_json = settingsToSave;
}

void CameraSuite::BeginTransientSettings(const Settings& persistentBaseline)
{
	std::lock_guard<std::mutex> lock(settingsMutex);
	persistentSettingsOverride = persistentBaseline;
}

void CameraSuite::EndTransientSettings()
{
	std::lock_guard<std::mutex> lock(settingsMutex);
	persistentSettingsOverride.reset();
}

void CameraSuite::LoadSettings(json& o_json)
{
	std::lock_guard<std::mutex> lock(settingsMutex);

	bool oldEnableHDR = settings.enableHDR;
	const auto clampFinite = [](float value, float fallback, float minimum, float maximum) {
		return std::clamp(std::isfinite(value) ? value : fallback, minimum, maximum);
	};

	settings = o_json.is_object() ? o_json.get<Settings>() : Settings{};
	// Older profiles had these optics implicitly tied to Bodycam. Preserve their
	// appearance once, while new neutral profiles keep the independent pass off.
	const json& savedCameraSettings = o_json.is_object() ? o_json : json::object();
	settings.enableLensEffects = savedCameraSettings.value("enableLensEffects", settings.experimentalBodycam);
	// Camera quality belongs to the coordinated menu contract rather than the
	// authored camera-look JSON. Restore it on startup instead of reverting to
	// the member default after a saved Low/Medium/High selection.
	if (globals::menu)
		cameraQuality = static_cast<std::uint32_t>(std::clamp(globals::menu->GetSettings().CameraQuality, 0, 3));

	// Sanitize PIXL Physical Camera settings loaded from older or hand-edited JSON.
	settings.cameraExposureCompensationEV = clampFinite(settings.cameraExposureCompensationEV, 0.0f, -4.0f, 4.0f);
	settings.cameraMinExposureEV = clampFinite(settings.cameraMinExposureEV, -6.0f, -10.0f, 0.0f);
	settings.cameraMaxExposureEV = clampFinite(settings.cameraMaxExposureEV, 6.0f, 0.0f, 10.0f);
	settings.cameraLowPercentile = clampFinite(settings.cameraLowPercentile, 0.02f, 0.0f, 0.20f);
	settings.cameraHighPercentile = clampFinite(settings.cameraHighPercentile, 0.98f, 0.80f, 1.0f);
	if (settings.cameraHighPercentile <= settings.cameraLowPercentile + 0.05f)
		settings.cameraHighPercentile = std::min(1.0f, settings.cameraLowPercentile + 0.05f);
	settings.cameraHighlightProtection = clampFinite(settings.cameraHighlightProtection, 0.65f, 0.0f, 1.0f);
	settings.cameraShadowDetail = std::clamp(settings.cameraShadowDetail, 0.0f, 0.5f);
	settings.cameraContrast = std::clamp(settings.cameraContrast, 0.75f, 1.30f);
	settings.cameraLocalExposure = clampFinite(settings.cameraLocalExposure, 0.12f, 0.0f, 0.5f);
	settings.cameraAdaptBrightToDark = clampFinite(settings.cameraAdaptBrightToDark, 1.20f, 0.65f, 4.0f);
	settings.cameraAdaptDarkToBright = clampFinite(settings.cameraAdaptDarkToBright, 0.65f, 0.65f, 2.0f);
	settings.cameraSaturation = std::clamp(settings.cameraSaturation, 0.70f, 1.25f);
	settings.cameraToe = std::clamp(settings.cameraToe, 0.0f, 0.5f);
	settings.cameraShoulder = std::clamp(settings.cameraShoulder, 0.2f, 1.5f);
	settings.cameraInfluence = std::clamp(settings.cameraInfluence, 0.0f, 1.0f);
	settings.menuSceneBrightness = std::clamp(settings.menuSceneBrightness, 0.75f, 3.0f);
	settings.lookPreset = std::min(settings.lookPreset, 11u);
	settings.lookOpacity = std::clamp(settings.lookOpacity, 0.0f, 1.0f);
	settings.bloomStrength = std::clamp(settings.bloomStrength, 0.0f, 3.0f);
	settings.bloomThreshold = std::clamp(settings.bloomThreshold, 0.0f, 5.0f);
	settings.bloomRadius = std::clamp(settings.bloomRadius, 0.0f, 5.0f);
	settings.stormglassStrength = std::clamp(settings.stormglassStrength, 0.0f, 1.0f);
	settings.stormglassDropScale = std::clamp(settings.stormglassDropScale, 0.50f, 2.0f);
	settings.stormglassRefraction = std::clamp(settings.stormglassRefraction, 0.0f, 1.5f);
	settings.stormglassTrails = std::clamp(settings.stormglassTrails, 0.0f, 1.0f);
	settings.stormglassDryingRate = std::clamp(settings.stormglassDryingRate, 0.01f, 0.50f);
	settings.submergedStrength = std::clamp(settings.submergedStrength, 0.0f, 1.0f);
	settings.submergedBlur = std::clamp(settings.submergedBlur, 0.0f, 1.0f);
	settings.submergedRefraction = std::clamp(settings.submergedRefraction, 0.0f, 1.5f);
	settings.submergedTransitionSpeed = std::clamp(settings.submergedTransitionSpeed, 0.5f, 8.0f);
	settings.coldLensStrength = std::clamp(settings.coldLensStrength, 0.0f, 1.0f);
	settings.coldAltitudeStart = std::clamp(settings.coldAltitudeStart, 0.0f, 70000.0f);
	settings.coldAltitudeFull = std::clamp(
		settings.coldAltitudeFull,
		settings.coldAltitudeStart + 1000.0f,
		90000.0f);
	settings.elementalLensStrength = std::clamp(settings.elementalLensStrength, 0.0f, 1.0f);
	settings.dofStrength = std::clamp(settings.dofStrength, 0.0f, 1.0f);
	settings.dofFocusDistance = std::clamp(settings.dofFocusDistance, 100.0f, 20000.0f);
	settings.dofFocusRange = std::clamp(settings.dofFocusRange, 100.0f, 20000.0f);
	settings.dofBokehRadius = std::clamp(settings.dofBokehRadius, 0.5f, 2.0f);
	settings.dofHighlightResponse = std::clamp(settings.dofHighlightResponse, 0.0f, 1.0f);
	settings.dofFocusEdgeProtection = std::clamp(settings.dofFocusEdgeProtection, 0.0f, 2.0f);
	settings.dofForegroundCoverage = std::clamp(settings.dofForegroundCoverage, 0.0f, 1.5f);
	settings.dofNearBlurIntensity = std::clamp(settings.dofNearBlurIntensity, 0.0f, 2.0f);
	settings.dofFarBlurIntensity = std::clamp(settings.dofFarBlurIntensity, 0.0f, 2.0f);
	settings.dofFarBlurDistance = std::clamp(settings.dofFarBlurDistance, 500.0f, 50000.0f);
	settings.dofCatEye = std::clamp(settings.dofCatEye, 0.0f, 1.0f);
	settings.dofAnamorphicRatio = std::clamp(settings.dofAnamorphicRatio, 0.5f, 2.0f);
	settings.dofFocalLengthMm = std::clamp(settings.dofFocalLengthMm, 18.0f, 200.0f);
	settings.dofFStop = std::clamp(settings.dofFStop,
		static_cast<float>(PIXL::Metadata::Settings::CameraDofFStop.minimum),
		static_cast<float>(PIXL::Metadata::Settings::CameraDofFStop.maximum));
	settings.dofSensorHeightMm = std::clamp(settings.dofSensorHeightMm, 10.0f, 40.0f);
	settings.dofFocusSpeed = std::clamp(settings.dofFocusSpeed, 0.25f, 20.0f);
	settings.dofFocusDeadband = std::clamp(settings.dofFocusDeadband, 0.0f, 0.25f);
	settings.dofMaxBokehPixels = std::clamp(settings.dofMaxBokehPixels, 4.0f, 96.0f);
	settings.dofApertureBlades = std::clamp(settings.dofApertureBlades, 3u, 12u);
	settings.dofBladeCurvature = std::clamp(settings.dofBladeCurvature, 0.0f, 1.0f);
	settings.dofApertureRotation = std::clamp(settings.dofApertureRotation, -3.14159f, 3.14159f);
	settings.motionBlurStrength = std::clamp(settings.motionBlurStrength, 0.0f, 1.0f);
	settings.motionBlurShutter = std::clamp(settings.motionBlurShutter, 0.10f, 1.0f);
	settings.motionBlurMaxPixels = std::clamp(settings.motionBlurMaxPixels, 4.0f, 48.0f);
	settings.bodycamStrength = std::clamp(settings.bodycamStrength, 0.0f, 1.0f);
	settings.bodycamDistortion = std::clamp(settings.bodycamDistortion, 0.0f, 0.30f);
	settings.bodycamNoise = std::clamp(settings.bodycamNoise, 0.0f, 0.60f);
	settings.bodycamVignette = std::clamp(settings.bodycamVignette, 0.0f, 0.30f);
	settings.bodycamChromaticAberration = std::clamp(settings.bodycamChromaticAberration, 0.0f, 0.08f);
	settings.bodycamSharpen = std::clamp(settings.bodycamSharpen, 0.0f, 0.35f);
	settings.bodycamExposureAggressiveness = std::clamp(settings.bodycamExposureAggressiveness, 0.0f, 1.0f);
	settings.bodycamHighlightBloom = std::clamp(settings.bodycamHighlightBloom, 0.0f, 0.50f);
	settings.bodycamWhiteBalance = std::clamp(settings.bodycamWhiteBalance, 0.0f, 0.40f);
	environmentStateValid = false;

	// Defer auto-detection to SetupResources where the swap chain is available.
	// DetectHDR() needs globals::d3d::swapChain which isn't valid during early plugin init.
	// hdrAutoDetected starts false in defaults and is only set true after auto-detect
	// completes in SetupResources, so this correctly triggers on first launch even
	// when the default config was auto-generated with enableHDR: false.
	if (!settings.hdrAutoDetected) {
		pendingAutoDetect = true;
		logger::info("[HDR] Auto-detection not yet run - deferring to SetupResources");
	}

	if (settings.enableHDR != oldEnableHDR) {
		UpdateHDRData();
		UpdateSwapChainColorSpace();
	}
}

void CameraSuite::RestoreDefaultSettings()
{
	bool hdrMonitor = DetectHDR();
	settings.enableHDR = hdrMonitor;
	settings.hdrPaperWhite = 203;
	settings.hdrPeakNits = 1000;
	settings.hdrUIBrightness = 1.0f;
	settings.dontShowHDRWarning = false;
	settings.enablePhysicalCamera = true;
	settings.cameraAutoExposure = true;
	settings.cameraExposureCompensationEV = 0.0f;
	settings.cameraMinExposureEV = -6.0f;
	settings.cameraMaxExposureEV = 6.0f;
	settings.cameraLowPercentile = 0.02f;
	settings.cameraHighPercentile = 0.98f;
	settings.cameraHighlightProtection = 0.65f;
	settings.cameraShadowDetail = 0.14f;
	settings.cameraContrast = 1.0f;
	settings.cameraLocalExposure = 0.12f;
	settings.cameraAdaptBrightToDark = 1.20f;
	settings.cameraAdaptDarkToBright = 0.65f;
	settings.cameraSaturation = 1.0f;
	settings.cameraToe = 0.12f;
	settings.cameraShoulder = 0.72f;
	settings.cameraInfluence = 1.0f;
	settings.menuSceneBrightness = 1.0f;
	settings.lookPreset = 0;
	settings.lookOpacity = 0.35f;
	settings.enableBloom = false;
	settings.bloomStrength = 0.80f;
	settings.bloomThreshold = 1.20f;
	settings.bloomRadius = 1.00f;
	settings.enableStormglass = true;
	settings.stormglassStrength = 0.58f;
	settings.stormglassDropScale = 1.00f;
	settings.stormglassRefraction = 0.65f;
	settings.stormglassTrails = 0.55f;
	settings.stormglassDryingRate = 0.055f;
	settings.enableSubmergedOptics = true;
	settings.submergedStrength = 0.32f;
	settings.submergedBlur = 0.22f;
	settings.submergedRefraction = 0.42f;
	settings.submergedTransitionSpeed = 3.20f;
	settings.enableColdLens = true;
	settings.coldLensStrength = 0.28f;
	settings.coldAltitudeStart = 28000.0f;
	settings.coldAltitudeFull = 60000.0f;
	settings.enableElementalDamageLens = true;
	settings.elementalLensStrength = 0.45f;
	settings.enableSkyrimDepthOfField = false;
	settings.preferCinematicDoF = false;
	settings.enableEnhancedDepthOfField = true;
	settings.dofAutoFocus = true;
	settings.dofActorTracking = true;
	settings.dofStrength = 1.0f;
	settings.dofFocusDistance = 2200.0f;
	settings.dofFocusRange = 11605.0f;
	settings.dofBokehRadius = 1.0f;
	settings.dofHighlightResponse = 0.30f;
	settings.dofFocusEdgeProtection = 0.95f;
	settings.dofForegroundCoverage = 0.55f;
	settings.dofNearBlurIntensity = 0.0f;
	settings.dofFarBlurIntensity = 0.5f;
	settings.dofFarBlurDistance = 50000.0f;
	settings.dofCatEye = 0.12f;
	settings.dofAnamorphicRatio = 1.0f;
	settings.dofPhysicalLens = true;
	settings.dofFocalLengthMm = 18.0f;
	settings.dofFStop = static_cast<float>(PIXL::Metadata::Settings::CameraDofFStop.defaultValue);
	settings.dofSensorHeightMm = 14.9f;
	settings.dofFocusSpeed = 8.81f;
	settings.dofFocusDeadband = 0.055f;
	settings.dofMaxBokehPixels = 5.0f;
	settings.dofApertureBlades = 6;
	settings.dofBladeCurvature = 0.63f;
	settings.dofApertureRotation = 0.06981317f;
	settings.enableModernMotionBlur = false;
	settings.motionBlurStrength = 0.45f;
	settings.motionBlurShutter = 0.50f;
	settings.motionBlurMaxPixels = 24.0f;
	settings.experimentalBodycam = false;
	settings.enableLensEffects = false;
	settings.bodycamStrength = 0.75f;
	settings.bodycamDistortion = 0.12f;
	settings.bodycamNoise = 0.18f;
	settings.bodycamVignette = 0.08f;
	settings.bodycamChromaticAberration = 0.025f;
	settings.bodycamSharpen = 0.10f;
	settings.bodycamExposureAggressiveness = 0.75f;
	settings.bodycamHighlightBloom = 0.15f;
	settings.bodycamWhiteBalance = 0.12f;
	environmentStateValid = false;
	environmentTime = 0.0f;
	stormglassRainIntensityState = 0.0f;
	coldLensState = 0.0f;
	fireLensState = 0.0f;
	frostImpactLensState = 0.0f;
	stormglassWetnessState = 0.0f;
	surfaceBreakFilmState = 0.0f;
	submergedBlendState = 0.0f;
	stormglassLateralInertiaState = 0.0f;
	stormglassPreviousCameraYaw = 0.0f;
	stormglassCameraYawValid = false;
	wasPlayerUnderwater = false;
}
