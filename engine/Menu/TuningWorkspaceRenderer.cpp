#include "TuningWorkspaceRenderer.h"

#include <RE/C/CrosshairPickData.h>
#include <RE/F/FreeCameraState.h>
#include <SKSE/InputMap.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <format>
#include <imgui.h>
#include <ranges>
#include <unordered_map>
#include <unordered_set>

#include "RenderModule.h"
#include "Modules/HybridGI.h"
#include "Modules/CameraSuite.h"
#include "Modules/PixelCapture.h"
#include "Modules/PulseProfiler.h"
#include "ModuleRules.h"
#include "PipelineHealth.h"
#include "Profiler.h"
#include "Fonts.h"
#include "Globals.h"
#include "I18n/I18n.h"
#include "Menu.h"
#include "Menu/LaunchExperienceRenderer.h"
#include "Menu/PIXLRendererPage.h"
#include "Menu/PIXLStyle.h"
#include "Menu/PulsePanelRenderer.h"
#include "Menu/ThemeManager.h"
#include "SceneSettingsManager.h"
#include "SettingsOverrideManager.h"
#include "State.h"
#include "Renderer/QualityProfiles.h"
#include "Util.h"
#include "Utils/UI.h"
#include "WeatherVariableRegistry.h"

namespace
{
	// Core built-in menu names that always appear first in the menu list
	// These are canonical identifiers used for logic — NOT translated
	// Color for the [ALPHA]/[BETA] stage marker. Alpha (less stable) reads as an error,
	// Beta as a warning.
	ImVec4 StageTagColor(RenderModule::ReleaseStage stage)
	{
		const auto& statusPalette = globals::menu->GetTheme().StatusPalette;
		return stage == RenderModule::ReleaseStage::Alpha ? statusPalette.Error : statusPalette.Warning;
	}



	void SeparatorTextWithFont(const char* text, Menu::FontRole role)
	{
		MenuFonts::FontRoleGuard guard(role);
		ImGui::SeparatorText(text);
	}

	void SeparatorTextWithFont(const std::string& text, Menu::FontRole role)
	{
		SeparatorTextWithFont(text.c_str(), role);
	}

	bool BeginTabItemWithFont(const char* label, Menu::FontRole role, ImGuiTabItemFlags flags = ImGuiTabItemFlags_None)
	{
		return MenuFonts::BeginTabItemWithFont(label, role, flags);
	}

	enum class DirectorWeatherPreset : int
	{
		Original = 0,
		Clear = 1,
		Cloudy = 2,
		Rain = 3,
		Snow = 4
	};

	struct DirectorPhotoModeState
	{
		bool active = false;
		bool snapshotValid = false;
		bool restoreWorldOnExit = true;
		bool restoreLookOnExit = true;

		// Live FH-style photo-mode presentation.
		bool hudVisible = true;
		bool quickPanelVisible = false;
		bool focusTargetMode = false;
		bool focusTargetValid = false;
		float focusTargetDistance = 0.0f;
		int selectedQuickOption = 0;

		// FH-style free-camera leash. 8192 Skyrim units is intentionally generous
		// enough for wide composition while keeping the camera local to the subject.
		bool cameraAnchorValid = false;
		RE::NiPoint3 cameraAnchor{};
		float cameraBoundaryRadius = 8192.0f;
		float cameraBoundaryUsage = 0.0f;
		bool cameraBoundaryHit = false;
		bool cameraMotionValid = false;
		RE::NiPoint3 cameraMotionPosition{};
		RE::NiPoint3 cameraMotionVelocity{};

		// UI-safe delayed capture. The HUD disappears before the request and
		// stays suppressed through the temporal sample window.
		int captureDelayFrames = 0;
		int captureHideFrames = 0;

		float originalHour = 12.0f;
		float photoHour = 12.0f;
		RE::TESWeather* originalWeather = nullptr;
		DirectorWeatherPreset weatherPreset =
			DirectorWeatherPreset::Original;

		CameraSuite::Settings originalCamera{};

		// Preserve the actor's incoming alpha so Photo Mode never inherits Skyrim's
		// close-camera fade and stealth/fade state is restored exactly on exit.
		bool playerAlphaSnapshotValid = false;
		float originalPlayerAlpha = 1.0f;
	};

	DirectorPhotoModeState
		g_directorPhotoMode{};

	bool EvaluateDirectorPhotoModeEligibility(
		std::string* reason)
	{
		auto reject = [reason](std::string_view message) {
			if (reason)
				*reason = message;
			return false;
		};

		auto* player =
			RE::PlayerCharacter::GetSingleton();
		auto* camera =
			RE::PlayerCamera::GetSingleton();

		if (!player ||
			!player->Is3DLoaded() ||
			!player->GetParentCell() ||
			!camera) {
			return reject(
				"Photo Mode is available after the playable world has finished loading.");
		}

		if (player->IsDead()) {
			return reject(
				"Photo Mode is unavailable while the player is dead or reloading.");
		}

		auto* ui =
			RE::UI::GetSingleton();

		if (!ui) {
			return reject(
				"Photo Mode is waiting for Skyrim's interface state.");
		}

		if (ui->closingAllMenus) {
			return reject(
				"Photo Mode is unavailable during a menu or world transition.");
		}

		// These aggregate flags cover inventory/container/barter/crafting menus,
		// pause/journal screens and modal confirmation boxes.  Explicit checks
		// below cover non-pausing menus that still own camera or gameplay input.
		if (ui->GameIsPaused() ||
			ui->IsItemMenuOpen() ||
			ui->IsApplicationMenuOpen() ||
			ui->IsModalMenuOpen()) {
			return reject(
				"Close Skyrim's current menu before entering Photo Mode.");
		}

		const bool incompatibleMenuOpen =
			ui->IsMenuOpen(RE::MapMenu::MENU_NAME) ||
			ui->IsMenuOpen(RE::InventoryMenu::MENU_NAME) ||
			ui->IsMenuOpen(RE::MagicMenu::MENU_NAME) ||
			ui->IsMenuOpen(RE::FavoritesMenu::MENU_NAME) ||
			ui->IsMenuOpen(RE::ContainerMenu::MENU_NAME) ||
			ui->IsMenuOpen(RE::BarterMenu::MENU_NAME) ||
			ui->IsMenuOpen(RE::CraftingMenu::MENU_NAME) ||
			ui->IsMenuOpen(RE::JournalMenu::MENU_NAME) ||
			ui->IsMenuOpen(RE::TweenMenu::MENU_NAME) ||
			ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME) ||
			ui->IsMenuOpen(RE::MainMenu::MENU_NAME) ||
			ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME) ||
			ui->IsMenuOpen(RE::Console::MENU_NAME) ||
			ui->IsMenuOpen(RE::BookMenu::MENU_NAME) ||
			ui->IsMenuOpen(RE::SleepWaitMenu::MENU_NAME) ||
			ui->IsMenuOpen(RE::LockpickingMenu::MENU_NAME) ||
			ui->IsMenuOpen(RE::MessageBoxMenu::MENU_NAME);

		if (incompatibleMenuOpen) {
			return reject(
				"Close Skyrim's current menu before entering Photo Mode.");
		}

		if (auto* state = globals::state;
			state &&
			(state->isMainMenuOpen ||
			 state->isLoadingMenuOpen ||
			 state->isMapMenuOpen)) {
			return reject(
				"Photo Mode is unavailable during map, loading and main-menu states.");
		}

		if (reason)
			reason->clear();
		return true;
	}

	void UpdateDirectorPlayerBodyFade(
		RE::PlayerCamera* playerCamera)
	{
		if (!g_directorPhotoMode.active ||
			!g_directorPhotoMode.playerAlphaSnapshotValid ||
			!playerCamera ||
			!playerCamera->cameraRoot)
			return;

		auto* player =
			RE::PlayerCharacter::GetSingleton();
		if (!player)
			return;

		// Character photography is a primary Director use case. The earlier
		// capsule fade made close portraits impossible, so keep the exact incoming
		// alpha even if the free camera approaches or intersects the player.
		player->SetAlpha(g_directorPhotoMode.originalPlayerAlpha);
	}

	std::string DirectorLower(
		std::string_view value)
	{
		std::string result(value);

		std::ranges::transform(
			result,
			result.begin(),
			[](unsigned char c) {
				return static_cast<char>(
					std::tolower(c));
			});

		return result;
	}

	const char* DirectorWeatherName(
		RE::TESWeather* weather)
	{
		if (!weather)
			return "ORIGINAL";

		if (const char* name =
				weather->GetName();
			name &&
			name[0] != '\0') {
			return name;
		}

		if (const char* editorID =
				weather->GetFormEditorID();
			editorID &&
			editorID[0] != '\0') {
			return editorID;
		}

		return "WEATHER";
	}

	bool DirectorWeatherMatches(
		std::string_view text,
		DirectorWeatherPreset preset)
	{
		const std::string lower =
			DirectorLower(text);

		switch (preset) {
		case DirectorWeatherPreset::Clear:
			return
				lower.find("clear") !=
					std::string::npos ||
				lower.find("sunny") !=
					std::string::npos ||
				lower.find("pleasant") !=
					std::string::npos;

		case DirectorWeatherPreset::Cloudy:
			return
				lower.find("cloud") !=
					std::string::npos ||
				lower.find("overcast") !=
					std::string::npos ||
				lower.find("fog") !=
					std::string::npos;

		case DirectorWeatherPreset::Rain:
			return
				lower.find("rain") !=
					std::string::npos ||
				lower.find("storm") !=
					std::string::npos ||
				lower.find("thunder") !=
					std::string::npos;

		case DirectorWeatherPreset::Snow:
			return
				lower.find("snow") !=
					std::string::npos ||
				lower.find("blizzard") !=
					std::string::npos;

		default:
			return false;
		}
	}

	RE::TESWeather* FindDirectorWeather(
		DirectorWeatherPreset preset)
	{
		if (preset ==
			DirectorWeatherPreset::Original) {
			return
				g_directorPhotoMode
					.originalWeather;
		}

		auto* dataHandler =
			RE::TESDataHandler::GetSingleton();

		if (!dataHandler)
			return nullptr;

		for (auto* weather :
			 dataHandler->
				 GetFormArray<RE::TESWeather>()) {
			if (!weather)
				continue;

			const char* displayName =
				DirectorWeatherName(
					weather);

			if (DirectorWeatherMatches(
					displayName,
					preset)) {
				return weather;
			}

			if (const char* editorID =
					weather->GetFormEditorID();
				editorID &&
				DirectorWeatherMatches(
					editorID,
					preset)) {
				return weather;
			}
		}

		return nullptr;
	}

	void ApplyDirectorWeather(
		DirectorWeatherPreset preset)
	{
		auto* sky =
			RE::Sky::GetSingleton();

		if (!sky)
			return;

		if (preset ==
			DirectorWeatherPreset::Original) {
			sky->ReleaseWeatherOverride();

			if (g_directorPhotoMode
					.originalWeather) {
				sky->SetWeather(
					g_directorPhotoMode
						.originalWeather,
					false,
					true);
			}

			return;
		}

		if (auto* weather =
				FindDirectorWeather(
					preset)) {
			sky->ForceWeather(
				weather,
				true);
		}
	}

	void ApplyDirectorHour(float hour)
	{
		auto* calendar =
			RE::Calendar::GetSingleton();

		if (!calendar ||
			!calendar->gameHour)
			return;

		calendar->gameHour->value =
			std::clamp(
				hour,
				0.0f,
				23.99f);
	}

	void ExitDirectorPhotoMode();

	bool EnterDirectorPhotoMode()
	{
		auto* player =
			RE::PlayerCharacter::GetSingleton();
		auto* camera =
			RE::PlayerCamera::GetSingleton();

		std::string unavailableReason;
		if (!EvaluateDirectorPhotoModeEligibility(
				&unavailableReason)) {
			logger::info(
				"[PIXL Director] Photo Mode activation rejected: {}",
				unavailableReason);
			return false;
		}

		if (camera->IsInFreeCameraMode() &&
			!g_directorPhotoMode.active) {
			// Never hijack a free-camera session that PIXL did not create.
			return false;
		}

		if (g_directorPhotoMode.active)
			return true;

		auto* calendar =
			RE::Calendar::GetSingleton();
		auto* sky =
			RE::Sky::GetSingleton();
		auto& cameraSuite =
			globals::pipeline::cameraSuite;

		g_directorPhotoMode.originalHour =
			calendar
				? calendar->GetHour()
				: 12.0f;

		g_directorPhotoMode.photoHour =
			g_directorPhotoMode
				.originalHour;

		g_directorPhotoMode.originalWeather =
			sky
				? sky->currentWeather
				: nullptr;

		g_directorPhotoMode.weatherPreset =
			DirectorWeatherPreset::Original;

		{
			std::lock_guard<std::mutex>
				lock(
					cameraSuite
						.settingsMutex);

			g_directorPhotoMode
				.originalCamera =
					cameraSuite.settings;
		}

		// Photo mode captures an optically clean scene. Both CameraSuite's PIXL
		// enhancement and Skyrim's native image-space DOF are suppressed until
		// Director exits; focus/aperture controls now feed only Photo Lens DOF.
		cameraSuite.SetPhotoModeDofIsolation(true);

		g_directorPhotoMode.snapshotValid =
			true;
		g_directorPhotoMode.originalPlayerAlpha =
			player->GetAlpha();
		g_directorPhotoMode.playerAlphaSnapshotValid =
			true;

		g_directorPhotoMode.hudVisible = true;
		g_directorPhotoMode.quickPanelVisible = false;
		g_directorPhotoMode.focusTargetMode = false;
		g_directorPhotoMode.focusTargetValid = false;
		g_directorPhotoMode.focusTargetDistance = 0.0f;
		g_directorPhotoMode.selectedQuickOption = 0;
		g_directorPhotoMode.cameraAnchorValid = false;
		g_directorPhotoMode.cameraAnchor = {};
		g_directorPhotoMode.cameraBoundaryUsage = 0.0f;
		g_directorPhotoMode.cameraBoundaryHit = false;
		g_directorPhotoMode.cameraMotionValid = false;
		g_directorPhotoMode.cameraMotionPosition = {};
		g_directorPhotoMode.cameraMotionVelocity = {};
		g_directorPhotoMode.captureDelayFrames = 0;
		g_directorPhotoMode.captureHideFrames = 0;

		// CommonLib exposes Skyrim's native free-camera path directly. Passing
		// true asks the engine to freeze world simulation while free camera is
		// active, giving Director its photo-mode pause without a custom timescale
		// hook or gameplay patch.
		camera->ToggleFreeCameraMode(
			true);

		g_directorPhotoMode.active =
			camera->IsInFreeCameraMode();

		if (!g_directorPhotoMode.active) {
			// Undo the camera/DOF/player snapshots immediately when native TFC
			// refuses ownership.  This leaves no partially entered Photo Mode.
			ExitDirectorPhotoMode();
			return false;
		}

		if (g_directorPhotoMode.active) {
			auto* freeCameraState =
				static_cast<RE::FreeCameraState*>(
					camera->
						currentState
						.get());

			if (freeCameraState) {
				g_directorPhotoMode.cameraAnchor =
					freeCameraState->translation;
				g_directorPhotoMode.cameraAnchorValid =
					true;
				g_directorPhotoMode.cameraMotionPosition =
					freeCameraState->translation;
				g_directorPhotoMode.cameraMotionVelocity = {};
				g_directorPhotoMode.cameraMotionValid = true;
			}
		}

		if (g_directorPhotoMode.active &&
			globals::menu) {
			// Return keyboard/mouse to Skyrim's free camera immediately.
			globals::menu->IsEnabled =
				false;
		}

		return
			g_directorPhotoMode.active;
	}

	void ExitDirectorPhotoMode()
	{
		if (!g_directorPhotoMode.active &&
			!g_directorPhotoMode.snapshotValid &&
			!g_directorPhotoMode.playerAlphaSnapshotValid) {
			return;
		}

		auto* camera =
			RE::PlayerCamera::GetSingleton();

		if (camera &&
			camera->IsInFreeCameraMode()) {
			camera->ToggleFreeCameraMode(
				false);
		}

		auto& cameraSuite =
			globals::pipeline::cameraSuite;

		if (g_directorPhotoMode
				.snapshotValid) {
			if (g_directorPhotoMode
					.restoreWorldOnExit) {
				ApplyDirectorHour(
					g_directorPhotoMode
						.originalHour);

				ApplyDirectorWeather(
					DirectorWeatherPreset::
						Original);
			}

			if (g_directorPhotoMode
					.restoreLookOnExit) {
				{
					std::lock_guard<std::mutex>
						lock(
							cameraSuite
								.settingsMutex);

					cameraSuite.settings =
						g_directorPhotoMode
							.originalCamera;
				}

				cameraSuite.LoadLookTexture();
				cameraSuite.UpdateHDRData();
			}
		}

		cameraSuite.SetPhotoModeDofIsolation(false);

		if (g_directorPhotoMode
				.playerAlphaSnapshotValid) {
			if (auto* player =
					RE::PlayerCharacter::GetSingleton()) {
				player->SetAlpha(
					g_directorPhotoMode
						.originalPlayerAlpha);
			}
		}

		g_directorPhotoMode.active =
			false;
		g_directorPhotoMode.snapshotValid =
			false;
		g_directorPhotoMode.weatherPreset =
			DirectorWeatherPreset::Original;
		g_directorPhotoMode.hudVisible = true;
		g_directorPhotoMode.quickPanelVisible = false;
		g_directorPhotoMode.focusTargetMode = false;
		g_directorPhotoMode.focusTargetValid = false;
		g_directorPhotoMode.focusTargetDistance = 0.0f;
		g_directorPhotoMode.selectedQuickOption = 0;
		g_directorPhotoMode.cameraAnchorValid = false;
		g_directorPhotoMode.cameraAnchor = {};
		g_directorPhotoMode.cameraBoundaryUsage = 0.0f;
		g_directorPhotoMode.cameraBoundaryHit = false;
		g_directorPhotoMode.cameraMotionValid = false;
		g_directorPhotoMode.cameraMotionPosition = {};
		g_directorPhotoMode.cameraMotionVelocity = {};
		g_directorPhotoMode.captureDelayFrames = 0;
		g_directorPhotoMode.captureHideFrames = 0;
		g_directorPhotoMode.playerAlphaSnapshotValid = false;
		g_directorPhotoMode.originalPlayerAlpha = 1.0f;
	}

	void DrawDirectorQuickLook()
	{
		auto& camera =
			globals::pipeline::cameraSuite;

		float exposure = 0.0f;
		float contrast = 1.0f;
		float saturation = 1.0f;
		float highlightProtection = 0.0f;
		float shadowDetail = 0.0f;

		bool bloomEnabled = false;
		float bloomStrength = 0.0f;

		bool dofEnabled = false;
		float dofStrength = 0.0f;
		float focusDistance = 2200.0f;
		float focusRange = 1600.0f;
		float bokehRadius = 1.0f;

		int lookPreset = 0;

		{
			std::lock_guard<std::mutex>
				lock(
					camera.settingsMutex);

			exposure =
				camera.settings
					.cameraExposureCompensationEV;
			contrast =
				camera.settings
					.cameraContrast;
			saturation =
				camera.settings
					.cameraSaturation;
			highlightProtection =
				camera.settings
					.cameraHighlightProtection;
			shadowDetail =
				camera.settings
					.cameraShadowDetail;

			bloomEnabled =
				camera.settings
					.enableBloom;
			bloomStrength =
				camera.settings
					.bloomStrength;

			dofEnabled =
				camera.settings
					.enableEnhancedDepthOfField;
			dofStrength =
				camera.settings
					.dofStrength;
			focusDistance =
				camera.settings
					.dofFocusDistance;
			focusRange =
				camera.settings
					.dofFocusRange;
			bokehRadius =
				camera.settings
					.dofBokehRadius;

			lookPreset =
				static_cast<int>(
					camera.settings
						.lookPreset);
		}

		bool changed = false;

		const char* looks[] = {
			"Original",
			"Nordic Neutral",
			"Saga",
			"Dramatic",
			"Hearthfire",
			"Bleak"
		};

		changed |=
			PIXLUI::CycleSelector(
				"Colour grade",
				&lookPreset,
				looks,
				static_cast<int>(
					std::size(
						looks)));

		changed |=
			PIXLUI::SliderFloatField(
				"Exposure",
				&exposure,
				-2.0f,
				2.0f,
				"%+.2f EV");

		changed |=
			PIXLUI::SliderFloatField(
				"Contrast",
				&contrast,
				0.75f,
				1.25f,
				"%.2f");

		changed |=
			PIXLUI::SliderFloatField(
				"Colour",
				&saturation,
				0.75f,
				1.25f,
				"%.2f");

		changed |=
			PIXLUI::SliderFloatField(
				"Highlight protection",
				&highlightProtection,
				0.0f,
				1.0f,
				"%.2f");

		changed |=
			PIXLUI::SliderFloatField(
				"Shadow detail",
				&shadowDetail,
				0.0f,
				0.4f,
				"%.2f");

		changed |=
			PIXLUI::LabeledToggle(
				"Bloom",
				&bloomEnabled);

		ImGui::BeginDisabled(
			!bloomEnabled);

		changed |=
			PIXLUI::SliderFloatField(
				"Bloom strength",
				&bloomStrength,
				0.0f,
				2.0f,
				"%.2f");

		ImGui::EndDisabled();

		changed |=
			PIXLUI::LabeledToggle(
				"Depth of field",
				&dofEnabled);

		ImGui::BeginDisabled(
			!dofEnabled);

		changed |=
			PIXLUI::SliderFloatField(
				"DOF strength",
				&dofStrength,
				0.0f,
				1.0f,
				"%.2f");

		changed |=
			PIXLUI::SliderFloatField(
				"Focus distance",
				&focusDistance,
				100.0f,
				20000.0f,
				"%.0f units",
				true);

		changed |=
			PIXLUI::SliderFloatField(
				"Focus range",
				&focusRange,
				100.0f,
				20000.0f,
				"%.0f units",
				true);

		changed |=
			PIXLUI::SliderFloatField(
				"Bokeh size",
				&bokehRadius,
				0.5f,
				2.0f,
				"%.2fx");

		ImGui::EndDisabled();

		if (changed) {
			{
				std::lock_guard<std::mutex>
					lock(
						camera.settingsMutex);

				camera.settings
					.cameraExposureCompensationEV =
						exposure;
				camera.settings.cameraContrast =
					contrast;
				camera.settings.cameraSaturation =
					saturation;
				camera.settings
					.cameraHighlightProtection =
						highlightProtection;
				camera.settings.cameraShadowDetail =
					shadowDetail;

				camera.settings.enableBloom =
					bloomEnabled;
				camera.settings.bloomStrength =
					bloomStrength;

				camera.settings
					.enableEnhancedDepthOfField =
						dofEnabled;
				camera.settings.dofStrength =
					dofStrength;
				camera.settings.dofFocusDistance =
					focusDistance;
				camera.settings.dofFocusRange =
					focusRange;
				camera.settings.dofBokehRadius =
					bokehRadius;

				camera.settings.lookPreset =
					static_cast<uint>(
						lookPreset);
			}

			camera.LoadLookTexture();
			camera.UpdateHDRData();
		}
	}

	void DrawDirectorEnvironment()
	{
		auto* calendar =
			RE::Calendar::GetSingleton();

		if (calendar &&
			calendar->gameHour) {
			if (PIXLUI::SliderFloatField(
					"Time of day",
					&g_directorPhotoMode
						 .photoHour,
					0.0f,
					23.99f,
					"%.2f h")) {
				ApplyDirectorHour(
					g_directorPhotoMode
						.photoHour);
			}
		} else {
			ImGui::TextDisabled(
				"Time control unavailable.");
		}

		const char* weatherLabels[] = {
			"Original",
			"Clear",
			"Cloudy",
			"Rain",
			"Snow"
		};

		int weatherPreset =
			static_cast<int>(
				g_directorPhotoMode
					.weatherPreset);

		if (PIXLUI::CycleSelector(
				"Weather",
				&weatherPreset,
				weatherLabels,
				static_cast<int>(
					std::size(
						weatherLabels)))) {
			g_directorPhotoMode
				.weatherPreset =
					static_cast<
						DirectorWeatherPreset>(
							weatherPreset);

			ApplyDirectorWeather(
				g_directorPhotoMode
					.weatherPreset);
		}

		PIXLUI::LabeledToggle(
			"Restore world on exit",
			&g_directorPhotoMode
				 .restoreWorldOnExit);

		PIXLUI::LabeledToggle(
			"Restore look on exit",
			&g_directorPhotoMode
				 .restoreLookOnExit);
	}

	enum class DirectorQuickOption : int
	{
		Weather = 0,
		TimeOfDay,
		Exposure,
		DepthOfField,
		FocusMode,
		FocusDistance,
		ApertureBokeh,
		Bloom,
		Contrast,
		Colour,
		CaptureQuality,
		OutputResolution,
		TemporalSampling,
		MotionFinish,
		ShutterStrength,
		Count
	};

	constexpr int kDirectorQuickOptionCount =
		static_cast<int>(
			DirectorQuickOption::Count);

	struct DirectorQuickReadout
	{
		const char* label = "";
		std::string value;
		float normalized = -1.0f;
	};

	PixelCapture* GetDirectorCapture()
	{
		if (!globals::pipeline::pixelCapture.loaded)
			return nullptr;

		return
			&globals::pipeline::pixelCapture;
	}

	const char* DirectorWeatherPresetLabel(
		DirectorWeatherPreset preset)
	{
		switch (preset) {
		case DirectorWeatherPreset::Clear:
			return "CLEAR";
		case DirectorWeatherPreset::Cloudy:
			return "CLOUDY";
		case DirectorWeatherPreset::Rain:
			return "RAIN";
		case DirectorWeatherPreset::Snow:
			return "SNOW";
		default:
			return "ORIGINAL";
		}
	}

	void SetDirectorFocusTargetMode(bool enabled)
	{
		g_directorPhotoMode.focusTargetMode =
			enabled;
		g_directorPhotoMode.focusTargetValid =
			false;

		if (!enabled)
			return;

		// Focus targeting only records the desired offline focal plane. Director
		// intentionally keeps realtime PIXL + vanilla DOF disabled while framing.
	}

	bool UpdateDirectorFocusTarget()
	{
		g_directorPhotoMode.focusTargetValid =
			false;

		if (!g_directorPhotoMode.active ||
			!g_directorPhotoMode.focusTargetMode) {
			return false;
		}

#if defined(EXCLUSIVE_SKYRIM_FLAT)
		auto* pick =
			RE::CrosshairPickData::GetSingleton();
		auto* playerCamera =
			RE::PlayerCamera::GetSingleton();

		if (!pick ||
			!playerCamera ||
			!playerCamera->cameraRoot) {
			return false;
		}

		const RE::NiPoint3& hit =
			pick->collisionPoint;

		const RE::NiPoint3& cameraPos =
			playerCamera->
				cameraRoot->
				world.translate;

		const float dx =
			hit.x -
			cameraPos.x;
		const float dy =
			hit.y -
			cameraPos.y;
		const float dz =
			hit.z -
			cameraPos.z;

		const float distance =
			std::sqrt(
				dx * dx +
				dy * dy +
				dz * dz);

		if (!std::isfinite(distance) ||
			distance < 25.0f ||
			distance > 20000.0f) {
			return false;
		}

		g_directorPhotoMode.focusTargetValid =
			true;
		g_directorPhotoMode.focusTargetDistance =
			distance;

		auto& camera =
			globals::pipeline::cameraSuite;

		bool changed = false;

		{
			std::lock_guard<std::mutex>
				lock(
					camera.settingsMutex);

			if (std::abs(
					camera.settings
						.dofFocusDistance -
					distance) >
				4.0f) {
				camera.settings
					.dofFocusDistance =
						distance;
				changed = true;
			}

		}

		if (changed)
			camera.UpdateHDRData();

		return true;
#else
		return false;
#endif
	}

	void ArmDirectorPhotoCapture()
	{
		if (!g_directorPhotoMode.active)
			return;

		auto* capture =
			GetDirectorCapture();

		if (!capture ||
			capture->IsPhotoFinishSampling())
			return;

		g_directorPhotoMode.captureDelayFrames =
			2;
		g_directorPhotoMode.captureHideFrames =
			0;
	}

	void DispatchDirectorPhotoCapture()
	{
		auto* capture =
			GetDirectorCapture();

		if (!capture)
			return;

		if (capture->photoFinishEnabled) {
			capture->RequestPhotoFinishCapture();

			g_directorPhotoMode.captureHideFrames =
				std::max(
					5,
					static_cast<int>(
						capture->
							photoFinishTemporalSamples) +
						4);
		} else {
			capture->captureRequested =
				true;

			g_directorPhotoMode.captureHideFrames =
				4;
		}
	}

	void AdjustDirectorQuickOption(int direction)
	{
		if (direction == 0)
			return;

		direction =
			direction > 0
				? 1
				: -1;

		const auto option =
			static_cast<DirectorQuickOption>(
				std::clamp(
					g_directorPhotoMode
						.selectedQuickOption,
					0,
					kDirectorQuickOptionCount -
						1));

		auto& camera =
			globals::pipeline::cameraSuite;
		auto* capture =
			GetDirectorCapture();
		bool cameraChanged = false;

		switch (option) {
		case DirectorQuickOption::Weather:
		{
			const int current =
				static_cast<int>(
					g_directorPhotoMode
						.weatherPreset);
			constexpr int count = 5;

			g_directorPhotoMode.weatherPreset =
				static_cast<DirectorWeatherPreset>(
					(current +
					 direction +
					 count) %
					count);

			ApplyDirectorWeather(
				g_directorPhotoMode
					.weatherPreset);
			break;
		}

		case DirectorQuickOption::TimeOfDay:
			g_directorPhotoMode.photoHour +=
				0.25f *
				static_cast<float>(
					direction);

			if (g_directorPhotoMode.photoHour <
				0.0f)
				g_directorPhotoMode.photoHour +=
					24.0f;

			if (g_directorPhotoMode.photoHour >=
				24.0f)
				g_directorPhotoMode.photoHour -=
					24.0f;

			ApplyDirectorHour(
				g_directorPhotoMode
					.photoHour);
			break;

		case DirectorQuickOption::Exposure:
		{
			std::lock_guard<std::mutex>
				lock(
					camera.settingsMutex);

			camera.settings
				.cameraExposureCompensationEV =
					std::clamp(
						camera.settings
								.cameraExposureCompensationEV +
							0.10f *
								static_cast<float>(
									direction),
						-2.0f,
						2.0f);

			cameraChanged = true;
			break;
		}

		case DirectorQuickOption::DepthOfField:
		{
			if (capture)
				capture->photoLensDofEnabled =
					!capture->photoLensDofEnabled;

			// Live DOF remains isolated; this switch controls only the final
			// high-quality Photo Lens resolve.
			break;
		}

		case DirectorQuickOption::FocusMode:
			SetDirectorFocusTargetMode(
				!g_directorPhotoMode
					 .focusTargetMode);
			return;

		case DirectorQuickOption::FocusDistance:
		{
			SetDirectorFocusTargetMode(
				false);

			std::lock_guard<std::mutex>
				lock(
					camera.settingsMutex);

			const float step =
				std::max(
					50.0f,
					camera.settings
							.dofFocusDistance *
						0.075f);

			camera.settings
				.dofFocusDistance =
					std::clamp(
						camera.settings
								.dofFocusDistance +
							step *
								static_cast<float>(
									direction),
						100.0f,
						20000.0f);

			cameraChanged = true;
			break;
		}

		case DirectorQuickOption::ApertureBokeh:
		{
			std::lock_guard<std::mutex>
				lock(
					camera.settingsMutex);

			camera.settings.dofBokehRadius =
				std::clamp(
					camera.settings.dofBokehRadius +
						0.10f *
							static_cast<float>(
								direction),
					0.5f,
					2.0f);

			if (capture) {
				capture->photoLensDofStrength =
					std::clamp(
						0.18f +
							(camera.settings.dofBokehRadius -
							 0.5f) /
								1.5f *
								0.78f,
						0.18f,
						0.96f);
			}

			cameraChanged = true;
			break;
		}

		case DirectorQuickOption::Bloom:
		{
			std::lock_guard<std::mutex>
				lock(
					camera.settingsMutex);

			if (!camera.settings.enableBloom &&
				direction > 0) {
				camera.settings.enableBloom =
					true;
				camera.settings.bloomStrength =
					std::max(
						camera.settings
							.bloomStrength,
						0.25f);
			} else {
				camera.settings.bloomStrength =
					std::clamp(
						camera.settings
								.bloomStrength +
							0.10f *
								static_cast<float>(
									direction),
						0.0f,
						2.0f);

				if (camera.settings
						.bloomStrength <=
					0.001f)
					camera.settings.enableBloom =
						false;
			}

			cameraChanged = true;
			break;
		}

		case DirectorQuickOption::Contrast:
		{
			std::lock_guard<std::mutex>
				lock(
					camera.settingsMutex);

			camera.settings.cameraContrast =
				std::clamp(
					camera.settings.cameraContrast +
						0.02f *
							static_cast<float>(
								direction),
					0.75f,
					1.25f);

			cameraChanged = true;
			break;
		}

		case DirectorQuickOption::Colour:
		{
			std::lock_guard<std::mutex>
				lock(
					camera.settingsMutex);

			camera.settings.cameraSaturation =
				std::clamp(
					camera.settings.cameraSaturation +
						0.02f *
							static_cast<float>(
								direction),
					0.75f,
					1.25f);

			cameraChanged = true;
			break;
		}

		case DirectorQuickOption::CaptureQuality:
			if (capture) {
				int preset =
					static_cast<int>(
						capture->
							photoFinishQualityPreset);

				preset =
					(preset +
					 direction +
					 4) %
					4;

				capture->photoFinishQualityPreset =
					static_cast<unsigned int>(
						preset);

				switch (preset) {
				case 0:
					capture->photoFinishTemporalSamples = 4u;
					capture->photoFinishScale = 1u;
					capture->photoFinishDetailStrength = 0.28f;
					capture->photoLensDofQuality = 0u;
					break;
				case 1:
					capture->photoFinishTemporalSamples = 8u;
					capture->photoFinishScale = 2u;
					capture->photoFinishDetailStrength = 0.45f;
					capture->photoLensDofQuality = 1u;
					break;
				case 2:
					capture->photoFinishTemporalSamples = 16u;
					capture->photoFinishScale = 2u;
					capture->photoFinishDetailStrength = 0.65f;
					capture->photoLensDofQuality = 2u;
					break;
				default:
					capture->photoFinishTemporalSamples = 24u;
					capture->photoFinishScale = 4u;
					capture->photoFinishDetailStrength = 0.82f;
					capture->photoLensDofQuality = 3u;
					break;
				}
			}
			break;

		case DirectorQuickOption::OutputResolution:
			if (capture) {
				if (direction > 0) {
					capture->photoFinishScale =
						capture->photoFinishScale <
								2u
							? 2u
							: capture->
									  photoFinishScale <
								  4u
								? 4u
								: 1u;
				} else {
					capture->photoFinishScale =
						capture->photoFinishScale >=
								4u
							? 2u
							: capture->
									  photoFinishScale >=
								  2u
								? 1u
								: 4u;
				}
			}
			break;

		case DirectorQuickOption::TemporalSampling:
			if (capture) {
				if (direction > 0) {
					capture->
						photoFinishTemporalSamples =
							capture->
									photoFinishTemporalSamples <
								4u
								? 4u
								: capture->
										  photoFinishTemporalSamples <
									  8u
									? 8u
									: 1u;
				} else {
					capture->
						photoFinishTemporalSamples =
							capture->
									photoFinishTemporalSamples >=
								8u
								? 4u
								: capture->
										  photoFinishTemporalSamples >=
									  4u
									? 1u
									: 8u;
				}
			}
			break;

		case DirectorQuickOption::MotionFinish:
			if (capture) {
				capture->
					photoFinishMotionEnabled =
						!capture->
							 photoFinishMotionEnabled;
			}
			break;

		case DirectorQuickOption::ShutterStrength:
			if (capture) {
				capture->photoFinishMotionEnabled =
					true;

				capture->photoFinishMotionStrength =
					std::clamp(
						capture->
								photoFinishMotionStrength +
							0.05f *
								static_cast<float>(
									direction),
						0.0f,
						1.0f);
			}
			break;

		default:
			break;
		}

		if (cameraChanged)
			camera.UpdateHDRData();
	}

	DirectorQuickReadout GetDirectorQuickReadout(
		DirectorQuickOption option)
	{
		DirectorQuickReadout result;
		auto& camera =
			globals::pipeline::cameraSuite;
		auto* capture =
			GetDirectorCapture();

		CameraSuite::Settings settingsCopy{};

		{
			std::lock_guard<std::mutex>
				lock(
					camera.settingsMutex);
			settingsCopy =
				camera.settings;
		}

		switch (option) {
		case DirectorQuickOption::Weather:
			result.label = "Weather";
			result.value =
				DirectorWeatherPresetLabel(
					g_directorPhotoMode
						.weatherPreset);
			break;

		case DirectorQuickOption::TimeOfDay:
		{
			result.label = "Time of Day";
			const int hour =
				static_cast<int>(
					std::floor(
						g_directorPhotoMode
							.photoHour));
			const int minute =
				static_cast<int>(
					std::floor(
						std::fmod(
							g_directorPhotoMode
								.photoHour,
							1.0f) *
						60.0f));

			result.value =
				std::format(
					"{:02d}:{:02d}",
					hour,
					minute);
			result.normalized =
				g_directorPhotoMode
					.photoHour /
				24.0f;
			break;
		}

		case DirectorQuickOption::Exposure:
			result.label = "Exposure";
			result.value =
				std::format(
					"{:+.2f} EV",
					settingsCopy
						.cameraExposureCompensationEV);
			result.normalized =
				(settingsCopy
					 .cameraExposureCompensationEV +
				 2.0f) /
				4.0f;
			break;

		case DirectorQuickOption::DepthOfField:
			result.label = "Photo Lens DOF";
			result.value =
				capture &&
						capture->photoLensDofEnabled
					? "ON / PHOTO HQ"
					: "OFF";
			break;

		case DirectorQuickOption::FocusMode:
			result.label = "Focus Mode";
			if (g_directorPhotoMode
					.focusTargetMode) {
				result.value =
					g_directorPhotoMode
							.focusTargetValid
						? "TRACKING / LOCK"
						: "TRACKING / SEARCH";
			} else {
				result.value = "MANUAL";
			}
			break;

		case DirectorQuickOption::FocusDistance:
			result.label = "Focus";
			result.value =
				std::format(
					"{:.0f} u",
					settingsCopy
						.dofFocusDistance);
			result.normalized =
				std::clamp(
					settingsCopy
							.dofFocusDistance /
						10000.0f,
					0.0f,
					1.0f);
			break;

		case DirectorQuickOption::ApertureBokeh:
			result.label = "Aperture / Bokeh";
			result.value =
				std::format(
					"{:.2f}x",
					settingsCopy
						.dofBokehRadius);
			result.normalized =
				(settingsCopy.dofBokehRadius -
				 0.5f) /
				1.5f;
			break;

		case DirectorQuickOption::Bloom:
			result.label = "Bloom";
			if (settingsCopy.enableBloom) {
				result.value =
					std::format(
						"{:.2f}",
						settingsCopy
							.bloomStrength);
				result.normalized =
					settingsCopy
						.bloomStrength /
					2.0f;
			} else {
				result.value = "OFF";
				result.normalized = 0.0f;
			}
			break;

		case DirectorQuickOption::Contrast:
			result.label = "Contrast";
			result.value =
				std::format(
					"{:.2f}",
					settingsCopy
						.cameraContrast);
			result.normalized =
				(settingsCopy.cameraContrast -
				 0.75f) /
				0.50f;
			break;

		case DirectorQuickOption::Colour:
			result.label = "Colour";
			result.value =
				std::format(
					"{:.2f}",
					settingsCopy
						.cameraSaturation);
			result.normalized =
				(settingsCopy.cameraSaturation -
				 0.75f) /
				0.50f;
			break;

		case DirectorQuickOption::CaptureQuality:
			result.label = "Capture Quality";
			if (!capture) {
				result.value = "N/A";
			} else {
				switch (capture->
					photoFinishQualityPreset) {
				case 0:
					result.value = "FAST";
					break;
				case 1:
					result.value = "ENHANCED";
					break;
				case 2:
					result.value = "CINEMATIC";
					break;
				default:
					result.value = "ULTRA";
					break;
				}
			}
			break;

		case DirectorQuickOption::OutputResolution:
			result.label = "Photo Resolution";
			if (!capture) {
				result.value = "N/A";
			} else if (capture->
						   photoFinishScale >=
					   4u) {
				result.value = "4X ULTRA";
			} else if (capture->
						   photoFinishScale >=
					   2u) {
				result.value = "2X SUPER";
			} else {
				result.value = "NATIVE";
			}
			break;

		case DirectorQuickOption::TemporalSampling:
			result.label = "Sampling";
			result.value =
				capture
					? std::format(
						  "{} FRAME",
						  capture->
							  photoFinishTemporalSamples)
					: "N/A";
			break;

		case DirectorQuickOption::MotionFinish:
			result.label = "Motion Finish";
			result.value =
				capture &&
						capture->
							photoFinishMotionEnabled
					? "ON"
					: "OFF";
			break;

		case DirectorQuickOption::ShutterStrength:
			result.label = "Shutter";
			if (capture) {
				result.value =
					std::format(
						"{:.2f}",
						capture->
							photoFinishMotionStrength);
				result.normalized =
					capture->
						photoFinishMotionStrength;
			} else {
				result.value = "N/A";
			}
			break;

		default:
			break;
		}

		return result;
	}


	void SmoothDirectorCameraMotion(RE::FreeCameraState* freeCameraState)
	{
		if (!freeCameraState || !g_directorPhotoMode.active)
			return;

		auto& motion = g_directorPhotoMode;
		if (!motion.cameraMotionValid) {
			motion.cameraMotionPosition = freeCameraState->translation;
			motion.cameraMotionVelocity = {};
			motion.cameraMotionValid = true;
			return;
		}

		const float dt = std::clamp(
			static_cast<float>(RE::GetSecondsSinceLastFrame()),
			1.0f / 240.0f,
			1.0f / 20.0f);
		const bool shiftHeld =
			(GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0 ||
			freeCameraState->useRunSpeed;
		// Native TFC remains the input source, but ordinary movement is deliberately
		// slowed for precise framing. Shift restores a fast traversal gear.
		const float speedScale = shiftHeld ? 0.82f : 0.30f;

		const RE::NiPoint3 proposed = freeCameraState->translation;
		RE::NiPoint3 rawDelta{
			proposed.x - motion.cameraMotionPosition.x,
			proposed.y - motion.cameraMotionPosition.y,
			proposed.z - motion.cameraMotionPosition.z
		};
		const float rawDistanceSq =
			rawDelta.x * rawDelta.x + rawDelta.y * rawDelta.y + rawDelta.z * rawDelta.z;
		if (rawDistanceSq > 512.0f * 512.0f) {
			// Teleports/cell transitions are discontinuities, not camera input.
			motion.cameraMotionPosition = proposed;
			motion.cameraMotionVelocity = {};
			return;
		}

		RE::NiPoint3 desiredVelocity{
			rawDelta.x * speedScale / dt,
			rawDelta.y * speedScale / dt,
			rawDelta.z * speedScale / dt
		};
		const float response = 1.0f - std::exp(-dt * (shiftHeld ? 14.0f : 10.0f));
		motion.cameraMotionVelocity.x = std::lerp(motion.cameraMotionVelocity.x, desiredVelocity.x, response);
		motion.cameraMotionVelocity.y = std::lerp(motion.cameraMotionVelocity.y, desiredVelocity.y, response);
		motion.cameraMotionVelocity.z = std::lerp(motion.cameraMotionVelocity.z, desiredVelocity.z, response);
		motion.cameraMotionPosition.x += motion.cameraMotionVelocity.x * dt;
		motion.cameraMotionPosition.y += motion.cameraMotionVelocity.y * dt;
		motion.cameraMotionPosition.z += motion.cameraMotionVelocity.z * dt;
		freeCameraState->translation = motion.cameraMotionPosition;
	}

	void EnforceDirectorCameraBoundary(
		RE::PlayerCamera* playerCamera)
	{
		if (!playerCamera ||
			!g_directorPhotoMode.active)
			return;

		auto* freeCameraState =
			static_cast<RE::FreeCameraState*>(
				playerCamera->
					currentState
					.get());

		if (!freeCameraState)
			return;

		SmoothDirectorCameraMotion(freeCameraState);

		if (!g_directorPhotoMode
				 .cameraAnchorValid) {
			g_directorPhotoMode.cameraAnchor =
				freeCameraState->translation;
			g_directorPhotoMode.cameraAnchorValid =
				true;
		}

		const auto& anchor =
			g_directorPhotoMode.cameraAnchor;
		auto& position =
			freeCameraState->translation;

		const float dx =
			position.x - anchor.x;
		const float dy =
			position.y - anchor.y;
		const float dz =
			position.z - anchor.z;

		const float distanceSq =
			dx * dx +
			dy * dy +
			dz * dz;

		const float radius =
			std::max(
				g_directorPhotoMode
					.cameraBoundaryRadius,
				512.0f);

		if (distanceSq >
			radius * radius) {
			const float distance =
				std::sqrt(
					std::max(
						distanceSq,
						1.0f));
			const float correction =
				radius /
				distance;

			position.x =
				anchor.x +
				dx * correction;
			position.y =
				anchor.y +
				dy * correction;
			position.z =
				anchor.z +
				dz * correction;

			g_directorPhotoMode.cameraBoundaryUsage =
				1.0f;
			g_directorPhotoMode.cameraBoundaryHit =
				true;
			g_directorPhotoMode.cameraMotionPosition = position;
			g_directorPhotoMode.cameraMotionVelocity = {};
			return;
		}

		g_directorPhotoMode.cameraBoundaryUsage =
			std::clamp(
				std::sqrt(
					std::max(
						distanceSq,
						0.0f)) /
					radius,
				0.0f,
				1.0f);
		g_directorPhotoMode.cameraBoundaryHit =
			false;
		g_directorPhotoMode.cameraMotionPosition = position;
	}

	void DrawDirectorViewfinder(
		ImDrawList* draw,
		const ImVec2& displaySize,
		float scale)
	{
		const ImVec2 center(
			displaySize.x * 0.5f,
			displaySize.y * 0.5f);

		const float radius =
			30.0f * scale;

		const ImU32 focusColor =
			g_directorPhotoMode
					.focusTargetMode
				? g_directorPhotoMode
						  .focusTargetValid
					? PIXLUI::Colors::
						  CyanBright
					: PIXLUI::Colors::
						  TextDim
				: PIXLUI::Colors::
					  TextMuted;

		draw->AddCircle(
			center,
			radius,
			focusColor,
			40,
			1.4f * scale);

		draw->AddCircle(
			center,
			10.0f * scale,
			PIXLUI::ScaleAlpha(
				focusColor,
				0.75f),
			28,
			1.0f * scale);

		const float outer =
			radius +
			9.0f * scale;
		const float inner =
			radius -
			7.0f * scale;

		draw->AddLine(
			ImVec2(center.x, center.y - outer),
			ImVec2(center.x, center.y - inner),
			focusColor,
			1.2f * scale);
		draw->AddLine(
			ImVec2(center.x, center.y + inner),
			ImVec2(center.x, center.y + outer),
			focusColor,
			1.2f * scale);
		draw->AddLine(
			ImVec2(center.x - outer, center.y),
			ImVec2(center.x - inner, center.y),
			focusColor,
			1.2f * scale);
		draw->AddLine(
			ImVec2(center.x + inner, center.y),
			ImVec2(center.x + outer, center.y),
			focusColor,
			1.2f * scale);

		std::string label;

		if (g_directorPhotoMode
				.focusTargetMode) {
			if (g_directorPhotoMode
					.focusTargetValid) {
				label =
					std::format(
						"FOCUS LOCK  {:.0f}u",
						g_directorPhotoMode
							.focusTargetDistance);
			} else {
				label =
					"FOCUS TRACKING";
			}
		} else {
			const auto focus =
				GetDirectorQuickReadout(
					DirectorQuickOption::
						FocusDistance);

			label =
				std::format(
					"MANUAL FOCUS  {}",
					focus.value);
		}

		const ImVec2 textSize =
			ImGui::CalcTextSize(
				label.c_str());

		const ImVec2 textPos(
			center.x -
				textSize.x * 0.5f,
			center.y +
				radius +
				16.0f * scale);

		draw->AddRectFilled(
			ImVec2(
				textPos.x -
					7.0f * scale,
				textPos.y -
					3.0f * scale),
			ImVec2(
				textPos.x +
					textSize.x +
					7.0f * scale,
				textPos.y +
					textSize.y +
					3.0f * scale),
			IM_COL32(5, 8, 10, 175),
			2.0f * scale);

		draw->AddText(
			textPos,
			focusColor,
			label.c_str());
	}

	void DrawDirectorQuickPanel(
		ImDrawList* draw,
		const ImVec2& displaySize,
		float scale)
	{
		if (!g_directorPhotoMode
				 .quickPanelVisible)
			return;

		const float x =
			34.0f * scale;
		const float y =
			std::max(
				54.0f * scale,
				displaySize.y *
					0.09f);
		const float width =
			370.0f * scale;
		const float headerHeight =
			50.0f * scale;
		const float rowHeight =
			29.0f * scale;
		const float footerHeight =
			28.0f * scale;

		const float height =
			headerHeight +
			rowHeight *
				static_cast<float>(
					kDirectorQuickOptionCount) +
			footerHeight;

		const ImVec2 min(x, y);
		const ImVec2 max(
			x + width,
			y + height);

		draw->AddRectFilled(
			min,
			max,
			IM_COL32(5, 8, 11, 224),
			4.0f * scale);

		draw->AddRect(
			min,
			max,
			PIXLUI::Colors::BorderSoft,
			4.0f * scale,
			0,
			1.0f * scale);

		draw->AddLine(
			ImVec2(
				min.x +
					1.0f * scale,
				min.y +
					1.0f * scale),
			ImVec2(
				min.x +
					1.0f * scale,
				max.y -
					1.0f * scale),
			PIXLUI::Colors::Cyan,
			2.0f * scale);

		draw->AddText(
			ImVec2(
				x + 15.0f * scale,
				y + 10.0f * scale),
			PIXLUI::Colors::Text,
			"PIXL DIRECTOR");

		draw->AddText(
			ImVec2(
				x + 15.0f * scale,
				y + 28.0f * scale),
			PIXLUI::Colors::CyanSoft,
			"QUICK EFFECTS");

		for (int i = 0;
			 i < kDirectorQuickOptionCount;
			 ++i) {
			const bool selected =
				i ==
				g_directorPhotoMode
					.selectedQuickOption;

			const float rowY =
				y +
				headerHeight +
				rowHeight *
					static_cast<float>(i);

			const auto readout =
				GetDirectorQuickReadout(
					static_cast<
						DirectorQuickOption>(i));

			if (selected) {
				draw->AddRectFilled(
					ImVec2(
						x +
							7.0f * scale,
						rowY +
							1.0f * scale),
					ImVec2(
						x +
							width -
							7.0f * scale,
						rowY +
							rowHeight -
							1.0f * scale),
					IM_COL32(
						13,
						30,
						35,
						225));

				draw->AddRectFilled(
					ImVec2(
						x +
							8.0f * scale,
						rowY +
							5.0f * scale),
					ImVec2(
						x +
							10.0f * scale,
						rowY +
							rowHeight -
							5.0f * scale),
					PIXLUI::Colors::
						CyanBright);
			}

			const ImU32 textColor =
				selected
					? PIXLUI::Colors::Text
					: PIXLUI::Colors::TextMuted;

			draw->AddText(
				ImVec2(
					x +
						17.0f * scale,
					rowY +
						7.0f * scale),
				textColor,
				readout.label);

			const ImVec2 valueSize =
				ImGui::CalcTextSize(
					readout.value.c_str());

			draw->AddText(
				ImVec2(
					x +
						width -
						17.0f * scale -
						valueSize.x,
					rowY +
						7.0f * scale),
				selected
					? PIXLUI::Colors::
						  CyanBright
					: PIXLUI::Colors::
						  TextDim,
				readout.value.c_str());

			if (readout.normalized >= 0.0f) {
				const float railMinX =
					x +
					17.0f * scale;
				const float railMaxX =
					x +
					width -
					17.0f * scale;
				const float railY =
					rowY +
					rowHeight -
					4.0f * scale;

				draw->AddLine(
					ImVec2(
						railMinX,
						railY),
					ImVec2(
						railMaxX,
						railY),
					IM_COL32(
						50,
						61,
						65,
						185),
					1.0f * scale);

				draw->AddLine(
					ImVec2(
						railMinX,
						railY),
					ImVec2(
						railMinX +
							(railMaxX -
							 railMinX) *
								std::clamp(
									readout.normalized,
									0.0f,
									1.0f),
						railY),
					PIXLUI::Colors::Cyan,
					1.5f * scale);
			}
		}

		draw->AddText(
			ImVec2(
				x + 15.0f * scale,
				max.y -
					20.0f * scale),
			PIXLUI::Colors::TextDim,
			"ARROWS / D-PAD  NAVIGATE + ADJUST");
	}

	void DrawDirectorBottomBar(
		ImDrawList* draw,
		const ImVec2& displaySize,
		float scale)
	{
		const char* hint =
			"HOME  EXIT    END  TAKE PHOTO    TAB  FOCUS TARGET    INSERT  EFFECTS    DELETE  HIDE UI    SHIFT+ENTER  PIXL";

		const ImVec2 textSize =
			ImGui::CalcTextSize(hint);

		const float width =
			std::min(
				displaySize.x -
					40.0f * scale,
				textSize.x +
					46.0f * scale);

		const float height =
			34.0f * scale;

		const ImVec2 min(
			(displaySize.x -
			 width) *
				0.5f,
			displaySize.y -
				55.0f * scale);

		const ImVec2 max(
			min.x + width,
			min.y + height);

		draw->AddRectFilled(
			min,
			max,
			IM_COL32(5, 8, 11, 218),
			3.0f * scale);

		draw->AddRect(
			min,
			max,
			PIXLUI::Colors::BorderSoft,
			3.0f * scale,
			0,
			1.0f * scale);

		draw->AddLine(
			ImVec2(
				min.x +
					8.0f * scale,
				min.y +
					height -
					2.0f * scale),
			ImVec2(
				max.x -
					8.0f * scale,
				min.y +
					height -
					2.0f * scale),
			PIXLUI::Colors::Cyan,
			1.2f * scale);

		draw->AddText(
			ImVec2(
				min.x +
					(width -
					 textSize.x) *
						0.5f,
				min.y +
					9.0f * scale),
			PIXLUI::Colors::TextMuted,
			hint);
	}


	void DrawDirectorPhotoProcessingBar(
		ImDrawList* draw,
		const ImVec2& displaySize,
		float scale,
		PixelCapture* capture)
	{
		if (!draw ||
			!capture ||
			!capture->IsPhotoFinishProcessing())
			return;

		const float progress =
			capture->GetPhotoFinishProgress();
		const char* stage =
			capture->GetPhotoFinishStageLabel();

		const float width =
			std::min(
				420.0f * scale,
				displaySize.x -
					48.0f * scale);
		const float height =
			58.0f * scale;
		const ImVec2 min(
			(displaySize.x - width) * 0.5f,
			displaySize.y - 92.0f * scale);
		const ImVec2 max(
			min.x + width,
			min.y + height);

		draw->AddRectFilled(
			min,
			max,
			IM_COL32(4, 7, 9, 232),
			4.0f * scale);
		draw->AddRect(
			min,
			max,
			PIXLUI::Colors::BorderSoft,
			4.0f * scale,
			0,
			1.0f * scale);

		draw->AddText(
			ImVec2(
				min.x + 15.0f * scale,
				min.y + 10.0f * scale),
			PIXLUI::Colors::CyanBright,
			"PIXL PHOTO RENDER");
		draw->AddText(
			ImVec2(
				min.x + 15.0f * scale,
				min.y + 28.0f * scale),
			PIXLUI::Colors::TextDim,
			stage);

		const float railX0 =
			min.x + 15.0f * scale;
		const float railX1 =
			max.x - 15.0f * scale;
		const float railY =
			max.y - 9.0f * scale;

		draw->AddLine(
			ImVec2(railX0, railY),
			ImVec2(railX1, railY),
			IM_COL32(48, 58, 62, 210),
			3.0f * scale);
		draw->AddLine(
			ImVec2(railX0, railY),
			ImVec2(
				railX0 +
					(railX1 - railX0) *
						std::clamp(progress, 0.0f, 1.0f),
				railY),
			PIXLUI::Colors::Cyan,
			3.0f * scale);
	}

	void RenderDirectorPhotoModeOverlayInternal()
	{
		if (!g_directorPhotoMode.active)
			return;

		std::string unavailableReason;
		if (!EvaluateDirectorPhotoModeEligibility(
				&unavailableReason)) {
			logger::info(
				"[PIXL Director] Exiting Photo Mode safely: {}",
				unavailableReason);
			ExitDirectorPhotoMode();
			return;
		}

		auto* playerCamera =
			RE::PlayerCamera::GetSingleton();

		if (!playerCamera)
			return;

		if (!playerCamera->IsInFreeCameraMode()) {
			// A load/death/menu transition can tear down native TFC before PIXL's
			// overlay observes the corresponding UI state.  Restore once instead of
			// attempting to seize the camera again in an incompatible game state.
			ExitDirectorPhotoMode();
			return;
		}

		EnforceDirectorCameraBoundary(
			playerCamera);
		UpdateDirectorPlayerBodyFade(
			playerCamera);

		if (g_directorPhotoMode.focusTargetMode)
			UpdateDirectorFocusTarget();

		// Suppress every HUD primitive before triggering capture.
		if (g_directorPhotoMode.captureDelayFrames > 0) {
			--g_directorPhotoMode.captureDelayFrames;

			if (g_directorPhotoMode
					.captureDelayFrames ==
				0) {
				DispatchDirectorPhotoCapture();
			}

			return;
		}

		auto* directorCapture =
			GetDirectorCapture();

		if (directorCapture &&
			directorCapture->IsPhotoFinishProcessing() &&
			!directorCapture->IsPhotoFinishSampling()) {
			const ImVec2 displaySize =
				ImGui::GetIO().DisplaySize;

			if (displaySize.x > 0.0f &&
				displaySize.y > 0.0f) {
				DrawDirectorPhotoProcessingBar(
					ImGui::GetForegroundDrawList(),
					displaySize,
					Util::GetUIScale(),
					directorCapture);
			}

			return;
		}

		if (g_directorPhotoMode.captureHideFrames > 0) {
			--g_directorPhotoMode.captureHideFrames;
			return;
		}

		// Full PIXL menu has its own Director workspace.
		if (globals::menu &&
			globals::menu->IsEnabled)
			return;

		if (!g_directorPhotoMode.hudVisible)
			return;

		const ImVec2 displaySize =
			ImGui::GetIO().DisplaySize;

		if (displaySize.x <= 0.0f ||
			displaySize.y <= 0.0f)
			return;

		const float scale =
			Util::GetUIScale();

		ImDrawList* draw =
			ImGui::GetForegroundDrawList();

		DrawDirectorViewfinder(
			draw,
			displaySize,
			scale);

		DrawDirectorQuickPanel(
			draw,
			displaySize,
			scale);

		DrawDirectorBottomBar(
			draw,
			displaySize,
			scale);

		if (g_directorPhotoMode.cameraBoundaryUsage >
			0.78f) {
			const auto rangeText =
				std::format(
					"CAMERA RANGE  {:.0f}%",
					g_directorPhotoMode
						.cameraBoundaryUsage *
						100.0f);

			const ImVec2 rangeSize =
				ImGui::CalcTextSize(
					rangeText.c_str());

			draw->AddText(
				ImVec2(
					displaySize.x -
						rangeSize.x -
						24.0f * scale,
					24.0f * scale),
				g_directorPhotoMode
						.cameraBoundaryHit
					? PIXLUI::Colors::
						  CyanBright
					: PIXLUI::Colors::
						  TextDim,
				rangeText.c_str());
		}
	}


	void DrawPIXLProfilingPage()
	{
		const ImVec2 available = ImGui::GetContentRegionAvail();
		const float maxWidth = std::min(available.x, PIXLUI::Ref(930.0f));
		const float xOffset = std::max(0.0f, (available.x - maxWidth) * 0.5f);

		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + xOffset);

		PIXLUI::ChromeScope profilerSurface(
			"##PIXLProfilingPageSurface",
			ImVec2(maxWidth, available.y),
			PIXLUI::ChromeStyle::Dark,
			true,
			ImGuiWindowFlags_None,
			PIXLUI::Ref(10.0f));

		if (!profilerSurface)
			return;

		PIXLUI::ProfilerStyleScope profilerStyle;
		PIXLUI::SectionBanner("RENDER TRACE");

		auto& pulse =
			globals::pipeline::pulseProfiler;

		if (!pulse.settings.ShowInOverlay)
			pulse.UpdateGraphValues();

		PIXLUI::SectionBanner(
			"FRAME PACING");

		const auto frameHistory =
			pulse.state.frameTimeHistory.GetData();

		if (!frameHistory.empty()) {
			ImGui::PlotLines(
				"##PIXLProfilingFramePacing",
				frameHistory.data(),
				static_cast<int>(
					frameHistory.size()),
				static_cast<int>(
					pulse.state
						.frameTimeHistory
						.GetHeadIdx()),
				nullptr,
				pulse.state
					.smoothedMinFrameTime,
				pulse.state
					.smoothedMaxFrameTime,
				ImVec2(
					-1.0f,
					PIXLUI::Ref(82.0f)));

			ImGui::TextColored(
				PIXLUI::ToVec4(
					PIXLUI::Colors::TextDim),
				"120 FPS  8.3 ms     60 FPS  16.7 ms     30 FPS  33.3 ms");
		}

		ImGui::Dummy(
			ImVec2(
				0,
					PIXLUI::Ref(4.0f)));

		PIXLUI::SectionBanner(
			"PASS ANALYSIS");

		static bool cpuTrace = false;
		const ImVec2 modeSize(PIXLUI::Ref(112.0f), PIXLUI::Ref(30.0f));
		if (PIXLUI::ActionButton("GPU TRACE", modeSize, !cpuTrace))
			cpuTrace = false;
		ImGui::SameLine();
		if (PIXLUI::ActionButton("CPU TRACE", modeSize, cpuTrace))
			cpuTrace = true;
		ImGui::SameLine();
		ImGui::TextColored(
			PIXLUI::ToVec4(PIXLUI::Colors::TextDim),
			"Live pass cost / rolling AVG / P95 / P99");

		if (!globals::profiler) {
			ImGui::TextDisabled("Profiler backend unavailable.");
			return;
		}

		const auto& results = globals::profiler->GetResults();
		std::vector<const Profiler::TimerResult*> rows;
		rows.reserve(results.size());
		for (const auto& result : results) {
			if (result.valid)
				rows.push_back(&result);
		}

		const bool useCpuTrace = cpuTrace;
		auto averageOf = [useCpuTrace](const Profiler::TimerResult* result) {
			return useCpuTrace ? result->cpuAvgMs : result->avgMs;
		};
		std::ranges::sort(rows, [averageOf](const auto* lhs, const auto* rhs) {
			return averageOf(lhs) > averageOf(rhs);
		});

		const float totalMs = useCpuTrace ? globals::profiler->GetCpuTotalTimeMs() : globals::profiler->GetTotalTimeMs();
		const float maxAverage = rows.empty() ? 1.0f : std::max(averageOf(rows.front()), 0.001f);

		ImGui::Dummy(ImVec2(0.0f, PIXLUI::Ref(5.0f)));
		ImGui::TextColored(
			PIXLUI::ToVec4(PIXLUI::Colors::CyanBright),
			"%.3f ms",
			totalMs);
		ImGui::SameLine();
		ImGui::TextDisabled("CURRENT %s TOTAL", cpuTrace ? "CPU" : "GPU");
		ImGui::SameLine(PIXLUI::Ref(270.0f));
		ImGui::TextColored(
			PIXLUI::ToVec4(PIXLUI::Colors::Text),
			"%zu",
			rows.size());
		ImGui::SameLine();
		ImGui::TextDisabled("ACTIVE PASSES");

		PIXLUI::SectionBanner("PASS DISTRIBUTION");

		const ImVec2 distributionStart = ImGui::GetCursorScreenPos();
		const float distributionWidth = ImGui::GetContentRegionAvail().x;
		const float distributionHeight = PIXLUI::Ref(16.0f);
		ImDrawList* draw = ImGui::GetWindowDrawList();
		draw->AddRectFilled(
			distributionStart,
			ImVec2(distributionStart.x + distributionWidth, distributionStart.y + distributionHeight),
			IM_COL32(9, 14, 17, 255),
			PIXLUI::Ref(2.0f));
		draw->AddRect(
			distributionStart,
			ImVec2(distributionStart.x + distributionWidth, distributionStart.y + distributionHeight),
			PIXLUI::Colors::BorderSoft,
			PIXLUI::Ref(2.0f));

		float averageTotal = 0.0f;
		for (const auto* row : rows)
			averageTotal += std::max(averageOf(row), 0.0f);

		float cursorX = distributionStart.x;
		const size_t segmentCount = std::min<size_t>(10, rows.size());
		for (size_t i = 0; i < segmentCount && averageTotal > 0.0001f; ++i) {
			const float share = std::max(averageOf(rows[i]), 0.0f) / averageTotal;
			const float segmentWidth = distributionWidth * share;
			const float intensity = 1.0f - static_cast<float>(i) / static_cast<float>(std::max<size_t>(1, segmentCount));
			const ImU32 segmentColor = PIXLUI::MixColor(
				PIXLUI::Colors::CyanSoft,
				PIXLUI::Colors::CyanBright,
				intensity * 0.45f);
			draw->AddRectFilled(
				ImVec2(cursorX, distributionStart.y + PIXLUI::Ref(2.0f)),
				ImVec2(cursorX + segmentWidth, distributionStart.y + distributionHeight - PIXLUI::Ref(2.0f)),
				segmentColor);
			cursorX += segmentWidth;
		}
		ImGui::Dummy(ImVec2(distributionWidth, distributionHeight + PIXLUI::Ref(7.0f)));

		const ImVec2 headerStart = ImGui::GetCursorScreenPos();
		const float contentWidth = ImGui::GetContentRegionAvail().x;
		const float avgX = headerStart.x + contentWidth - PIXLUI::Ref(220.0f);
		const float p95X = headerStart.x + contentWidth - PIXLUI::Ref(145.0f);
		const float p99X = headerStart.x + contentWidth - PIXLUI::Ref(70.0f);
		draw->AddText(headerStart, PIXLUI::Colors::TextDim, "PASS / MODULE");
		draw->AddText(ImVec2(avgX, headerStart.y), PIXLUI::Colors::TextDim, "AVG");
		draw->AddText(ImVec2(p95X, headerStart.y), PIXLUI::Colors::TextDim, "P95");
		draw->AddText(ImVec2(p99X, headerStart.y), PIXLUI::Colors::TextDim, "P99");
		ImGui::Dummy(ImVec2(contentWidth, PIXLUI::Ref(22.0f)));

		const size_t visibleRows = std::min<size_t>(12, rows.size());
		for (size_t i = 0; i < visibleRows; ++i) {
			const auto* result = rows[i];
			const float averageMs = cpuTrace ? result->cpuAvgMs : result->avgMs;
			const float p95Ms = cpuTrace ? result->cpuP95Ms : result->p95Ms;
			const float p99Ms = cpuTrace ? result->cpuP99Ms : result->p99Ms;
			const float currentMs = cpuTrace ? result->cpuTimeMs : result->gpuTimeMs;

			std::string label = result->name;
			const auto separator = label.find("::");
			if (separator != std::string::npos) {
				const auto module = std::string_view(label).substr(0, separator);
				const auto pass = std::string_view(label).substr(separator + 2);
				label = std::format("{} / {}", PIXLRendererPage::GetPublicName(module, module), pass);
			}
			if (label.size() > 62) {
				label.resize(59);
				label += "...";
			}

			ImGui::PushID(static_cast<int>(i));
			const ImVec2 rowStart = ImGui::GetCursorScreenPos();
			const float rowHeight = PIXLUI::Ref(34.0f);
			const bool hovered = ImGui::InvisibleButton("##traceRow", ImVec2(contentWidth, rowHeight));
			const bool isHovered = ImGui::IsItemHovered();

			draw->AddRectFilled(
				rowStart,
				ImVec2(rowStart.x + contentWidth, rowStart.y + rowHeight - PIXLUI::Ref(2.0f)),
				isHovered ? IM_COL32(18, 26, 30, 245) : IM_COL32(10, 15, 18, 235),
				PIXLUI::Ref(2.0f));
			draw->AddRect(
				rowStart,
				ImVec2(rowStart.x + contentWidth, rowStart.y + rowHeight - PIXLUI::Ref(2.0f)),
				isHovered ? PIXLUI::Colors::BorderBright : PIXLUI::Colors::BorderSoft,
				PIXLUI::Ref(2.0f));

			const float barStartX = rowStart.x + PIXLUI::Ref(250.0f);
			const float barEndX = avgX - PIXLUI::Ref(18.0f);
			const float barWidth = std::max(PIXLUI::Ref(80.0f), barEndX - barStartX);
			const float barY = rowStart.y + rowHeight - PIXLUI::Ref(8.0f);
			const float fill = std::clamp(averageMs / maxAverage, 0.0f, 1.0f);
			draw->AddLine(ImVec2(barStartX, barY), ImVec2(barStartX + barWidth, barY), PIXLUI::Colors::BorderSoft, PIXLUI::Ref(3.0f));
			draw->AddLine(ImVec2(barStartX, barY), ImVec2(barStartX + barWidth * fill, barY), PIXLUI::Colors::CyanSoft, PIXLUI::Ref(3.0f));
			const float markerX = barStartX + barWidth * fill;
			const float markerR = PIXLUI::Ref(3.5f);
			draw->AddQuadFilled(
				ImVec2(markerX, barY - markerR),
				ImVec2(markerX + markerR, barY),
				ImVec2(markerX, barY + markerR),
				ImVec2(markerX - markerR, barY),
				PIXLUI::Colors::Text);

			draw->AddText(
				ImVec2(rowStart.x + PIXLUI::Ref(8.0f), rowStart.y + PIXLUI::Ref(7.0f)),
				PIXLUI::Colors::TextMuted,
				label.c_str());

			char value[32]{};
			std::snprintf(value, sizeof(value), "%.3f", averageMs);
			draw->AddText(ImVec2(avgX, rowStart.y + PIXLUI::Ref(7.0f)), PIXLUI::Colors::Text, value);
			std::snprintf(value, sizeof(value), "%.3f", p95Ms);
			draw->AddText(ImVec2(p95X, rowStart.y + PIXLUI::Ref(7.0f)), PIXLUI::Colors::Text, value);
			std::snprintf(value, sizeof(value), "%.3f", p99Ms);
			draw->AddText(ImVec2(p99X, rowStart.y + PIXLUI::Ref(7.0f)), PIXLUI::Colors::Text, value);

			if (isHovered) {
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::TextUnformatted(result->name.c_str());
					ImGui::Separator();
					ImGui::Text("Current: %.4f ms", currentMs);
					ImGui::Text("Average: %.4f ms", averageMs);
					ImGui::Text("P95: %.4f ms", p95Ms);
					ImGui::Text("P99: %.4f ms", p99Ms);
				}
			}

			(void)hovered;
			ImGui::PopID();
		}

		if (rows.empty()) {
			ImGui::TextDisabled("Waiting for stable profiler samples...");
		} else if (rows.size() > visibleRows) {
			ImGui::TextDisabled("+ %zu additional passes", rows.size() - visibleRows);
		}
	}

	std::string TranslateFeatureCategory(std::string_view category)
	{
		if (category == ModuleGroups::kCharacters)
			return T("feature.category.characters", "Characters");
		if (category == ModuleGroups::kDisplay)
			return T("feature.category.display", "Display");
		if (category == ModuleGroups::kGrass)
			return T("feature.category.grass", "Grass");
		if (category == ModuleGroups::kLandscapeAndTextures)
			return T("feature.category.landscape_and_textures", "Landscape & Textures");
		if (category == ModuleGroups::kLighting)
			return T("feature.category.lighting", "Lighting");
		if (category == ModuleGroups::kMaterials)
			return T("feature.category.materials", "Materials");
		if (category == "Post-Processing")
			return T("feature.category.post_processing", "Post-Processing");
		if (category == ModuleGroups::kOther)
			return T("feature.category.other", "Other");
		if (category == ModuleGroups::kSky)
			return T("feature.category.sky", "Sky");
		if (category == ModuleGroups::kUtility)
			return T("feature.category.utility", "Utility");
		if (category == ModuleGroups::kWater)
			return T("feature.category.water", "Water");
		if (category == PIXLRendererPage::Category)
			return T("feature.category.pixl_renderer", "PIXL Renderer");

		return std::string(category);
	}

	/**
	 * @brief Draws a feature header with the feature name in large text and version in smaller text
	 * @param featureName The display name of the feature
	 * @param version The version string (can be empty)
	 * @param description Short description shown below the title (single line, truncated if too long)
	 * @return The height of just the title line (for button alignment)
	 */
	float DrawFeatureHeader(const std::string& featureName, const std::string& version, const std::string& description = "", const std::string& stageTag = "", ImVec4 stageColor = {})
	{
		auto& themeSettings = globals::menu->GetTheme();
		auto& palette = themeSettings.Palette;
		auto& featureHeading = themeSettings.FeatureHeading;

		// Sanitize and clamp to UI slider range to prevent malformed theme JSON from destabilizing layout
		float titleScale = featureHeading.FeatureTitleScale;
		if (!std::isfinite(titleScale)) {
			titleScale = ThemeManager::Constants::DEFAULT_FEATURE_TITLE_SCALE;
		}
		titleScale = std::clamp(titleScale, 1.0f, 3.0f);

		ImVec2 startPos = ImGui::GetCursorScreenPos();

		// Calculate title size and draw feature name with Title font
		ImVec2 titleSize;
		{
			MenuFonts::FontRoleGuard titleGuard(Menu::FontRole::Title);
			titleSize = ImGui::CalcTextSize(featureName.c_str());
			titleSize.x *= titleScale;
			titleSize.y *= titleScale;

			ImGui::SetWindowFontScale(titleScale);
			ImGui::TextUnformatted(featureName.c_str());
			ImGui::SetWindowFontScale(1.0f);
		}

		// Store the title-only height for return value
		float titleOnlyHeight = titleSize.y;

		// Running x for bottom-aligned annotations (stage tag, then version) to the right of the title
		float annotationX = startPos.x + titleSize.x + ImGui::GetStyle().ItemSpacing.x;

		// Draw stage marker ([ALPHA]/[BETA]) on same line, bottom-aligned
		if (!stageTag.empty()) {
			ImVec2 tagSize;
			{
				MenuFonts::FontRoleGuard bodyGuard(Menu::FontRole::Body);
				tagSize = ImGui::CalcTextSize(stageTag.c_str());
				tagSize.x *= titleScale;
				tagSize.y *= titleScale;
			}

			ImGui::SetCursorScreenPos(ImVec2(annotationX, startPos.y + titleSize.y - tagSize.y));
			{
				MenuFonts::FontRoleGuard bodyGuard(Menu::FontRole::Body);
				ImGui::SetWindowFontScale(titleScale);
				ImGui::TextColored(stageColor, "%s", stageTag.c_str());
				ImGui::SetWindowFontScale(1.0f);
			}

			annotationX += tagSize.x + ImGui::GetStyle().ItemSpacing.x;
			ImGui::SetCursorScreenPos(ImVec2(startPos.x, startPos.y + titleSize.y + ImGui::GetStyle().ItemSpacing.y * 0.25f));
		}

		// Draw version on same line with Body font, bottom-aligned if version exists
		if (!version.empty()) {
			// Format version: replace dashes with dots for consistency
			std::string formattedVersion = version;
			std::replace(formattedVersion.begin(), formattedVersion.end(), '-', '.');

			// Calculate version text size at scaled size
			ImVec2 versionSize;
			{
				MenuFonts::FontRoleGuard bodyGuard(Menu::FontRole::Body);
				versionSize = ImGui::CalcTextSize(("v" + formattedVersion).c_str());
				versionSize.x *= titleScale;
				versionSize.y *= titleScale;
			}

			// Position version text: right of the stage tag (or title), bottom-aligned
			float versionX = annotationX;
			float versionY = startPos.y + titleSize.y - versionSize.y;

			ImGui::SetCursorScreenPos(ImVec2(versionX, versionY));

			// Use dimmed text color for version
			ImVec4 versionColor = palette.Text;
			versionColor.w *= ThemeManager::Constants::VERSION_TEXT_OPACITY;

			{
				MenuFonts::FontRoleGuard bodyGuard(Menu::FontRole::Body);
				ImGui::SetWindowFontScale(titleScale);
				ImGui::TextColored(versionColor, "v%s", formattedVersion.c_str());
				ImGui::SetWindowFontScale(1.0f);
			}

			// Reset cursor to after the title block
			ImGui::SetCursorScreenPos(ImVec2(startPos.x, startPos.y + titleSize.y + ImGui::GetStyle().ItemSpacing.y * 0.25f));
		}

		// Draw description if provided (wrapped to content width)
		if (!description.empty()) {
			MenuFonts::FontRoleGuard subtextGuard(Menu::FontRole::Subtext);
			ImVec4 descColor = palette.Text;
			descColor.w *= 0.7f;  // Slightly dimmed
			ImGui::PushStyleColor(ImGuiCol_Text, descColor);
			ImGui::TextWrapped("%s", description.c_str());
			ImGui::PopStyleColor();
		}

		// Draw plain separator below
		ImGui::Separator();

		return titleOnlyHeight;
	}



	void ApplyRadiancePreset(
		HybridGI* hybridGI,
		int preset)
	{
		if (!hybridGI)
			return;

		const int quality =
			std::clamp(
				preset,
				PIXLRenderer::QualityProfiles::Low,
				PIXLRenderer::QualityProfiles::Ultra);

		// Q1: Radiance Weave no longer owns a private duplicate preset table.
		// Its quick buttons apply the same unified Lighting contract used by the
		// main Quality page, so GI, contact shadows and Light Volumes stay synced.
		PIXLRenderer::QualityProfiles::Apply(
			PIXLRenderer::QualityProfiles::Group::Lighting,
			quality);
		globals::state->Save();
	}

	bool RenderRadianceReferenceModal(
		RenderModule* feature,
		bool& open,
		ImVec2 rowAnchor)
	{
		if (!feature ||
			feature->GetShortName() != "HybridGI" ||
			!open) {
			return false;
		}

		auto* hybridGI = static_cast<HybridGI*>(feature);
		static bool showFullControls = false;

		const ImVec2 modalSize(
			PIXLUI::Ref(620.0f),
			PIXLUI::Ref(470.0f));

		const ImGuiViewport* viewport =
			ImGui::GetMainViewport();

		const float desiredX =
			rowAnchor.x +
				PIXLUI::Ref(320.0f);
		const float desiredY =
			rowAnchor.y +
				PIXLUI::Ref(
					PIXLUI::Layout::TuneFeatureHeight +
					5.0f);

		const ImVec2 modalPos(
			std::clamp(
				desiredX,
				viewport->WorkPos.x +
					PIXLUI::Ref(12.0f),
				viewport->WorkPos.x +
					viewport->WorkSize.x -
					modalSize.x -
					PIXLUI::Ref(12.0f)),
			std::clamp(
				desiredY,
				viewport->WorkPos.y +
					PIXLUI::Ref(12.0f),
				viewport->WorkPos.y +
					viewport->WorkSize.y -
					modalSize.y -
					PIXLUI::Ref(12.0f)));

		ImGui::SetNextWindowPos(
			modalPos,
			ImGuiCond_Always);
		ImGui::SetNextWindowSize(modalSize, ImGuiCond_Always);

		ImGui::PushStyleVar(
			ImGuiStyleVar_WindowPadding,
			ImVec2(PIXLUI::Ref(18.0f), PIXLUI::Ref(16.0f)));
		ImGui::PushStyleColor(
			ImGuiCol_WindowBg,
			PIXLUI::ToVec4(PIXLUI::Colors::Inset));

		const ImGuiWindowFlags flags =
			ImGuiWindowFlags_NoDecoration |
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoSavedSettings;

		bool rendered = false;
		if (ImGui::Begin("##PIXLRadiancePanel", nullptr, flags)) {
			rendered = true;

			const ImVec2 panelMin = ImGui::GetWindowPos();
			const ImVec2 panelMax(
				panelMin.x + ImGui::GetWindowSize().x,
				panelMin.y + ImGui::GetWindowSize().y);
			PIXLUI::DrawChrome(
				panelMin, panelMax,
				PIXLUI::ChromeStyle::Raised, true);

			{
				MenuFonts::FontRoleGuard title(Menu::FontRole::Title);
				ImGui::SetWindowFontScale(1.18f);
				ImGui::TextColored(
					PIXLUI::ToVec4(PIXLUI::Colors::Text),
					"RADIANCE WEAVE");
				ImGui::SetWindowFontScale(1.0f);
			}
			ImGui::TextColored(
				PIXLUI::ToVec4(PIXLUI::Colors::TextMuted),
				"Choose the render budget. PIXL owns the rest.");

			ImGui::Dummy(ImVec2(0, PIXLUI::Ref(10.0f)));
			PIXLUI::SectionBanner("QUALITY");

			if (!showFullControls) {
				const float buttonW = PIXLUI::Ref(132.0f);
				const float buttonH = PIXLUI::Ref(38.0f);
				const int activeLightingTier =
					std::clamp(
						globals::menu->GetSettings().LightingQuality,
						PIXLRenderer::QualityProfiles::Low,
						PIXLRenderer::QualityProfiles::Ultra);

				if (PIXLUI::ActionButton("LOW", { buttonW, buttonH }, activeLightingTier == 0))
					ApplyRadiancePreset(hybridGI, 0);
				ImGui::SameLine(0.0f, PIXLUI::Ref(8.0f));
				if (PIXLUI::ActionButton("MEDIUM", { buttonW, buttonH }, activeLightingTier == 1))
					ApplyRadiancePreset(hybridGI, 1);
				ImGui::SameLine(0.0f, PIXLUI::Ref(8.0f));
				if (PIXLUI::ActionButton("HIGH", { buttonW, buttonH }, activeLightingTier == 2))
					ApplyRadiancePreset(hybridGI, 2);
				ImGui::SameLine(0.0f, PIXLUI::Ref(8.0f));
				if (PIXLUI::ActionButton("ULTRA", { buttonW, buttonH }, activeLightingTier == 3))
					ApplyRadiancePreset(hybridGI, 3);

				ImGui::Dummy(ImVec2(0, PIXLUI::Ref(14.0f)));
				static constexpr const char* tierNames[] = { "LOW", "MEDIUM", "HIGH", "ULTRA" };
				ImGui::TextColored(
					PIXLUI::ToVec4(PIXLUI::Colors::TextMuted),
					"Unified Lighting: %s | Radiance %u x %u | cache %u / %u | reflections %u",
					tierNames[activeLightingTier],
					hybridGI->settings.NumSlices,
					hybridGI->settings.NumSteps,
					hybridGI->settings.WorldCacheSampleCount,
					hybridGI->settings.WorldCacheTraceSteps,
					hybridGI->settings.ReflectionSteps);
				ImGui::TextColored(
					PIXLUI::ToVec4(PIXLUI::Colors::TextDim),
					"Ultra preserves the shipped PIXL baseline. Profiles change workload, not artistic lighting.");

				ImGui::Dummy(ImVec2(0, PIXLUI::Ref(18.0f)));

				if (PIXLUI::ActionButton(
						"RESTORE HIGH BASELINE",
						{ PIXLUI::Ref(190.0f), PIXLUI::Ref(34.0f) },
						false)) {
					ApplyRadiancePreset(hybridGI, PIXLRenderer::QualityProfiles::High);
				}

				ImGui::SameLine(0.0f, PIXLUI::Ref(10.0f));
				if (PIXLUI::ActionButton(
						"ENGINEER CONTROLS",
						{ PIXLUI::Ref(184.0f), PIXLUI::Ref(34.0f) },
						false)) {
					showFullControls = true;
				}
			} else {
				if (PIXLUI::ActionButton(
						"BACK",
						{ PIXLUI::Ref(84.0f), PIXLUI::Ref(30.0f) },
						false)) {
					showFullControls = false;
				}

				ImGui::Dummy(ImVec2(0, PIXLUI::Ref(8.0f)));

				if (ImGui::BeginChild(
						"##RadianceEngineerControls",
						ImVec2(0, modalSize.y - PIXLUI::Ref(125.0f)),
						ImGuiChildFlags_None,
						ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
					PIXLUI::EngineeringStyleScope
						engineerStyle;
					hybridGI->DrawSettings();
				}
				ImGui::EndChild();
			}

			if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
				open = false;
				showFullControls = false;
			}
		}

		ImGui::End();
		ImGui::PopStyleColor();
		ImGui::PopStyleVar();
		return rendered;
	}


	// ---------------------------------------------------------------------------
	// Persistent state for the reactive constraint warning popup.
	// DrawMenuVisitor is reconstructed every frame (it's a temporary passed to
	// std::visit), so member state is lost immediately.  These file-scope
	// variables survive across frames so the popup can actually render.
	// ---------------------------------------------------------------------------

	// Set of constraint keys we have already "seen" (and therefore warned about
	// or suppressed).  Keyed as "featureShortName|settingPath".
	std::unordered_set<std::string> g_knownConstraintKeys;
	bool g_knownConstraintKeysInitialised = false;

	// Pending popup state: non-empty when we have new constraints to show.
	bool g_reactiveWarningShow = false;
	std::vector<std::pair<ModuleRules::SettingId, ModuleRules::ConstraintResult>> g_reactiveWarningConstraints;

	// "Don't show again" checkbox state inside the modal (reset each time popup opens).
	bool g_dontShowAgainCheckbox = false;

	// Constraint enumeration builds temporary vectors/sets/strings. The old CS
	// path rebuilt that graph every ImGui frame while a module was open. PIXL
	// checks immediately after an edit and otherwise at a modest cadence.
	double g_nextConstraintScanTime = 0.0;
}

bool TuningWorkspaceRenderer::IsDirectorPhotoModeActive()
{
	return
		g_directorPhotoMode.active;
}

bool TuningWorkspaceRenderer::IsDirectorPhotoModeAvailable(
	std::string* reason)
{
	return EvaluateDirectorPhotoModeEligibility(reason);
}

bool TuningWorkspaceRenderer::OpenDirectorPhotoMode()
{
	// If the user reopened PIXL while composing, this is simply a return to the
	// existing viewfinder. Otherwise use the same guarded entry path as HOME.
	if (g_directorPhotoMode.active) {
		if (globals::menu)
			globals::menu->IsEnabled = false;
		return true;
	}

	if (!EnterDirectorPhotoMode())
		return false;

	if (globals::menu)
		globals::menu->IsEnabled = false;
	return true;
}

bool TuningWorkspaceRenderer::HandleDirectorKeyboardInput(
	std::uint32_t virtualKey)
{
	if (virtualKey == VK_HOME) {
		if (g_directorPhotoMode.active)
			ExitDirectorPhotoMode();
		else
			EnterDirectorPhotoMode();

		return true;
	}

	if (!g_directorPhotoMode.active)
		return false;

	if (!g_directorPhotoMode.hudVisible) {
		switch (virtualKey) {
		case VK_DELETE:
			g_directorPhotoMode.hudVisible =
				true;
			return true;

		case VK_END:
			ArmDirectorPhotoCapture();
			return true;

		default:
			// Hidden HUD = pure composition mode. Camera controls still reach
			// native TFC, but PIXL settings cannot change invisibly.
			return true;
		}
	}

	// Keep the existing PIXL menu reachable while Director owns the game.
	if (virtualKey == VK_RETURN &&
		(GetAsyncKeyState(VK_SHIFT) &
		 0x8000) &&
		globals::menu) {
		globals::menu->IsEnabled =
			true;
		return true;
	}

	switch (virtualKey) {
	case VK_END:
		ArmDirectorPhotoCapture();
		return true;

	case VK_TAB:
		SetDirectorFocusTargetMode(
			!g_directorPhotoMode
				 .focusTargetMode);
		return true;

	case VK_INSERT:
		g_directorPhotoMode.quickPanelVisible =
			!g_directorPhotoMode
				 .quickPanelVisible;
		return true;

	case VK_DELETE:
		g_directorPhotoMode.hudVisible =
			!g_directorPhotoMode.hudVisible;
		return true;

	case VK_UP:
		if (g_directorPhotoMode.quickPanelVisible) {
			g_directorPhotoMode.selectedQuickOption =
				(g_directorPhotoMode.selectedQuickOption +
				 kDirectorQuickOptionCount -
				 1) %
				kDirectorQuickOptionCount;
			return true;
		}
		break;

	case VK_DOWN:
		if (g_directorPhotoMode.quickPanelVisible) {
			g_directorPhotoMode.selectedQuickOption =
				(g_directorPhotoMode.selectedQuickOption +
				 1) %
				kDirectorQuickOptionCount;
			return true;
		}
		break;

	case VK_LEFT:
		if (g_directorPhotoMode.quickPanelVisible) {
			AdjustDirectorQuickOption(-1);
			return true;
		}
		break;

	case VK_RIGHT:
		if (g_directorPhotoMode.quickPanelVisible) {
			AdjustDirectorQuickOption(1);
			return true;
		}
		break;

	default:
		break;
	}

	return false;
}

bool TuningWorkspaceRenderer::HandleDirectorGamepadInput(
	std::uint32_t gamepadKeyCode)
{
	if (!g_directorPhotoMode.active)
		return false;

	if (!g_directorPhotoMode.hudVisible) {
		switch (gamepadKeyCode) {
		case SKSE::InputMap::
			kGamepadButtonOffset_BACK:
			g_directorPhotoMode.hudVisible =
				true;
			return true;

		case SKSE::InputMap::
			kGamepadButtonOffset_A:
			ArmDirectorPhotoCapture();
			return true;

		default:
			// Hidden HUD is framing-only. Thumbsticks remain native TFC input;
			// buttons cannot modify settings until the HUD is restored.
			return true;
		}
	}

	switch (gamepadKeyCode) {
	case SKSE::InputMap::kGamepadButtonOffset_DPAD_UP:
		if (g_directorPhotoMode.quickPanelVisible) {
			g_directorPhotoMode.selectedQuickOption =
				(g_directorPhotoMode.selectedQuickOption +
				 kDirectorQuickOptionCount -
				 1) %
				kDirectorQuickOptionCount;
			return true;
		}
		break;

	case SKSE::InputMap::kGamepadButtonOffset_DPAD_DOWN:
		if (g_directorPhotoMode.quickPanelVisible) {
			g_directorPhotoMode.selectedQuickOption =
				(g_directorPhotoMode.selectedQuickOption +
				 1) %
				kDirectorQuickOptionCount;
			return true;
		}
		break;

	case SKSE::InputMap::kGamepadButtonOffset_DPAD_LEFT:
		if (g_directorPhotoMode.quickPanelVisible) {
			AdjustDirectorQuickOption(-1);
			return true;
		}
		break;

	case SKSE::InputMap::kGamepadButtonOffset_DPAD_RIGHT:
		if (g_directorPhotoMode.quickPanelVisible) {
			AdjustDirectorQuickOption(1);
			return true;
		}
		break;

	case SKSE::InputMap::kGamepadButtonOffset_A:
		ArmDirectorPhotoCapture();
		return true;

	case SKSE::InputMap::kGamepadButtonOffset_B:
		// Director is modal. B is consumed so Skyrim cannot use it to back out
		// into another menu/state, but it intentionally does NOT exit Photo Mode.
		return true;

	case SKSE::InputMap::kGamepadButtonOffset_X:
		SetDirectorFocusTargetMode(
			!g_directorPhotoMode
				 .focusTargetMode);
		return true;

	case SKSE::InputMap::kGamepadButtonOffset_Y:
		g_directorPhotoMode.quickPanelVisible =
			!g_directorPhotoMode
				 .quickPanelVisible;
		return true;

	case SKSE::InputMap::kGamepadButtonOffset_BACK:
		g_directorPhotoMode.hudVisible =
			!g_directorPhotoMode.hudVisible;
		return true;

	default:
		break;
	}

	return false;
}

void TuningWorkspaceRenderer::RenderDirectorPhotoModeOverlay()
{
	RenderDirectorPhotoModeOverlayInternal();
}

void TuningWorkspaceRenderer::RenderFeatureList(
	float footerHeight,
	size_t& selectedMenu,
	std::string& featureSearch,
	std::string& pendingFeatureSelection,
	std::map<std::string, bool>& categoryExpansionStates,
	const std::function<void()>& drawGeneralSettings,
	const std::function<void()>& drawAdvancedSettings)
{
	// PASS A6.2.1: A6.2 moved the Advanced workspace to absolute reference
	// coordinates, so the old flow-layout footer reservation is intentionally
	// unused. Keep the public function signature stable and explicitly consume
	// the parameter because this project builds with warnings-as-errors.
	static_cast<void>(footerHeight);

	auto menuList =
		BuildMenuList(
			featureSearch,
			categoryExpansionStates,
			drawGeneralSettings,
			drawAdvancedSettings);

	HandlePendingFeatureSelection(
		pendingFeatureSelection,
		menuList,
		selectedMenu);

	if (selectedMenu >= menuList.size() ||
		std::holds_alternative<std::string>(menuList[selectedMenu]) ||
		std::holds_alternative<CategoryHeader>(menuList[selectedMenu]) ||
		std::holds_alternative<SubcategoryHeader>(menuList[selectedMenu])) {
		selectedMenu = 0;
	}

	const ImVec2 rootPos =
		ImGui::GetWindowPos();

	const ImVec2 sidebarPos(
		rootPos.x +
			PIXLUI::Ref(
				PIXLUI::Layout::TuneSidebarX),
		rootPos.y +
			PIXLUI::Ref(
				PIXLUI::Layout::TuneSidebarY));

	const ImVec2 contentPos(
		rootPos.x +
			PIXLUI::Ref(
				PIXLUI::Layout::TuneContentX),
		rootPos.y +
			PIXLUI::Ref(
				PIXLUI::Layout::TuneContentY));

	ImGui::SetCursorScreenPos(sidebarPos);
	{
		PIXLUI::ChromeScope sidebar(
			"##PIXLAdvancedSidebar",
			ImVec2(
				PIXLUI::Ref(
					PIXLUI::Layout::TuneSidebarFrameWidth),
				PIXLUI::Ref(
					PIXLUI::Layout::TuneSidebarFrameHeight)),
			PIXLUI::ChromeStyle::Sidebar,
			false,
			ImGuiWindowFlags_NoScrollbar,
			0.0f);

		if (sidebar) {
			const ImVec2 railOrigin =
				ImGui::GetWindowPos();

			{
				MenuFonts::FontRoleGuard railFont(
					Menu::FontRole::Subheading);

				const char* railTitle =
					"TUNE YOUR RENDERER";
				const ImVec2 railTitleSize =
					ImGui::CalcTextSize(
						railTitle);

				const float railWidth =
					PIXLUI::Ref(
						PIXLUI::Layout::
							TuneSidebarFrameWidth);

				const float railTitleX =
					railOrigin.x +
					std::max(
						0.0f,
						(railWidth -
						 railTitleSize.x) *
							0.5f);

				const float railTitleY =
					railOrigin.y +
					PIXLUI::Ref(
						PIXLUI::Layout::
							TuneSidebarTitleY);

				ImGui::SetCursorScreenPos(
					ImVec2(
						railTitleX,
						railTitleY));

				ImGui::TextColored(
					PIXLUI::ToVec4(
						PIXLUI::Colors::TextMuted),
					"%s",
					railTitle);
			}

			RenderLeftColumn(
				menuList,
				selectedMenu,
				featureSearch,
				categoryExpansionStates);
		}
	}

	ImGui::SetCursorScreenPos(contentPos);
	{
		PIXLUI::ChromeScope content(
			"##PIXLAdvancedContentFrame",
			ImVec2(
				PIXLUI::Ref(
					PIXLUI::Layout::TuneContentFrameWidth),
				PIXLUI::Ref(
					PIXLUI::Layout::TuneContentFrameHeight)),
			PIXLUI::ChromeStyle::Content,
			true,
			ImGuiWindowFlags_NoScrollbar,
			0.0f);

		if (content) {
			RenderRightColumn(
				menuList,
				selectedMenu,
				pendingFeatureSelection);
		}
	}

}

std::vector<TuningWorkspaceRenderer::MenuFuncInfo> TuningWorkspaceRenderer::BuildMenuList(
	const std::string& featureSearch,
	std::map<std::string, bool>& categoryExpansionStates,
	const std::function<void()>& drawGeneralSettings,
	const std::function<void()>& drawAdvancedSettings)
{
	(void)categoryExpansionStates;
	// Build the menu list
	auto& featureList = RenderModule::GetModuleList();
	auto sortedFeatureList{ featureList };  // need a copy so the load order is not lost
	std::ranges::sort(sortedFeatureList, [](RenderModule* a, RenderModule* b) {
		return a->GetDisplayName() < b->GetDisplayName();
	});

	// Filter features by search string
	if (!featureSearch.empty()) {
		auto it = std::remove_if(sortedFeatureList.begin(), sortedFeatureList.end(),
			[&featureSearch](RenderModule* feat) { return !Util::FeatureMatchesSearch(feat, featureSearch); });
		sortedFeatureList.erase(it, sortedFeatureList.end());
	}

	auto menuList = std::vector<MenuFuncInfo>{};
	// NOTE: The menu list is rebuilt every frame, so category expansion states
	// persist correctly. This is acceptable since the list is small and built
	// infrequently, but could be optimized if performance becomes an issue.

	// Group integrated features into four player-facing areas. Runtime feature
	// identities remain unchanged; only the presentation hierarchy is flattened.
	std::map<std::string, std::vector<RenderModule*>> categorizedFeatures;
	std::vector<RenderModule*> labFeatures;
	constexpr std::array<std::string_view, 2> developerOnlyFeatures{
		"PulseProfiler",
		"PixelCapture"
	};

	constexpr std::array<std::string_view, 2> labOnlyFeatures{
		"PulseProfiler",
		"PixelCapture"
	};
	for (RenderModule* feat : sortedFeatureList) {
		if (!feat->IsInMenu())
			continue;
		if (!globals::menu->GetSettings().DeveloperMode && std::ranges::find(developerOnlyFeatures, feat->GetShortName()) != developerOnlyFeatures.end())
			continue;
		const auto placement = PIXLRendererPage::GetPlacement(feat->GetShortName());
		if (!globals::menu->GetSettings().DeveloperMode && !placement)
			continue;
		if (placement) {
			categorizedFeatures[std::string(placement->category)].push_back(feat);
		} else if (
			globals::menu->GetSettings().DeveloperMode &&
			feat->loaded &&
			std::ranges::find(
				labOnlyFeatures,
				feat->GetShortName()) !=
				labOnlyFeatures.end()) {
			labFeatures.push_back(feat);
		}
	}

	// Sort features within each category
	for (auto& [category, features] : categorizedFeatures) {
		std::ranges::sort(features, [](RenderModule* a, RenderModule* b) {
			return a->GetDisplayName() < b->GetDisplayName();
		});
	}

	if (featureSearch.empty()) {
		for (const auto category : PIXLRendererPage::CategoryOrder) {
			auto& features = categorizedFeatures[std::string(category)];
			std::ranges::sort(features, [](RenderModule* lhs, RenderModule* rhs) {
				const auto left = PIXLRendererPage::GetPlacement(lhs->GetShortName());
				const auto right = PIXLRendererPage::GetPlacement(rhs->GetShortName());
				auto position = [](const auto& placement) {
					return static_cast<size_t>(std::distance(PIXLRendererPage::Placements.begin(),
						std::ranges::find_if(PIXLRendererPage::Placements, [&](const auto& item) { return item.featureShortName == placement->featureShortName; })));
				};
				return position(left) < position(right);
			});
			if (!features.empty())
				menuList.push_back(CategoryPage{ std::string(category), features });
		}
	} else {
		for (const auto category : PIXLRendererPage::CategoryOrder)
			std::ranges::copy(categorizedFeatures[std::string(category)], std::back_inserter(menuList));
	}

	if (globals::menu->GetSettings().DeveloperMode && !labFeatures.empty()) {
		menuList.push_back(CategoryHeader{ "LAB SYSTEMS" });
		std::ranges::copy(labFeatures, std::back_inserter(menuList));
	}

	// Product and general controls deliberately live below visual categories.
	menuList.push_back(BuiltInMenu{ "PIXL Renderer", []() { PIXLRendererPage::Render(); } });
	menuList.push_back(BuiltInMenu{ "General", drawGeneralSettings });
	if (globals::menu->GetSettings().DeveloperMode) {
		menuList.push_back(BuiltInMenu{ "Advanced", drawAdvancedSettings });
		menuList.push_back(BuiltInMenu{ "Profiling", []() { DrawPIXLProfilingPage(); } });
	}

	auto unloadedFeatures = sortedFeatureList | std::ranges::views::filter([](RenderModule* feat) {
		return !feat->loaded && !PIXLRendererPage::GetPlacement(feat->GetShortName()).has_value() && feat->IsInMenu() && !feat->IsHiddenUnreleased() && (!PipelineHealth::IsObsoleteFeature(feat->GetShortName()) || globals::state->IsDeveloperMode());
	});
	if (globals::menu->GetSettings().DeveloperMode && std::ranges::distance(unloadedFeatures) != 0) {
		menuList.push_back(T("menu.features.unloaded_features", "Unloaded Features"));
		std::ranges::copy(unloadedFeatures, std::back_inserter(menuList));
	}
	// Add top section for feature issues (rejected features, obsolete info, etc.)
	if (PipelineHealth::HasPipelineHealth()) {
		menuList.insert(menuList.begin(), BuiltInMenu{ T("menu.features.feature_issues", "RenderModule Issues"), []() {
														  PipelineHealth::DrawPipelineHealthUI();
													  } });
	}

	return menuList;
}

void TuningWorkspaceRenderer::HandlePendingFeatureSelection(
	std::string& pendingFeatureSelection,
	const std::vector<MenuFuncInfo>& menuList,
	size_t& selectedMenu)
{
	if (!pendingFeatureSelection.empty()) {
		for (size_t i = 0; i < menuList.size(); ++i) {
			if (std::holds_alternative<CategoryPage>(menuList[i])) {
				const auto& page = std::get<CategoryPage>(menuList[i]);
				if (std::ranges::any_of(page.features, [&](RenderModule* feature) { return feature->GetShortName() == pendingFeatureSelection; })) {
					selectedMenu = i;
					break;
				}
			}
			if (std::holds_alternative<RenderModule*>(menuList[i])) {
				RenderModule* feature = std::get<RenderModule*>(menuList[i]);
				if (feature->GetShortName() == pendingFeatureSelection) {
					selectedMenu = i;
					logger::info("Navigated to {} feature menu", pendingFeatureSelection);
					break;
				}
			}
		}
		pendingFeatureSelection.clear();  // Clear after processing
	}
}

void TuningWorkspaceRenderer::RenderLeftColumn(
	const std::vector<MenuFuncInfo>& menuList,
	size_t& selectedMenu,
	std::string& featureSearch,
	std::map<std::string, bool>& categoryExpansionStates)
{
	const ImVec2 railOrigin =
		ImGui::GetWindowPos();

	ImGui::PushStyleVar(
		ImGuiStyleVar_ItemSpacing,
		ImVec2(0.0f, 0.0f));
	ImGui::PushStyleColor(
		ImGuiCol_ChildBg,
		ImVec4(0, 0, 0, 0));

	if (ImGui::BeginChild(
			"##MenusList",
			ImVec2(0, 0),
			ImGuiChildFlags_None,
			ImGuiWindowFlags_NoScrollbar |
				ImGuiWindowFlags_NoBackground)) {
		const ImVec2 searchStart(
			railOrigin.x +
				PIXLUI::Ref(
					PIXLUI::Layout::TuneSidebarSearchX),
			railOrigin.y +
				PIXLUI::Ref(
					PIXLUI::Layout::TuneSidebarSearchY));

		const float searchWidth =
			PIXLUI::Ref(
				PIXLUI::Layout::TuneSearchWidth);
		const float searchHeight =
			PIXLUI::Ref(
				PIXLUI::Layout::TuneSearchHeight);

		ImDrawList* searchDraw = ImGui::GetWindowDrawList();
		PIXLUI::FillChamfered(
			searchDraw,
			searchStart,
			ImVec2(searchStart.x + searchWidth, searchStart.y + searchHeight),
			PIXLUI::Ref(3.0f),
			IM_COL32(12, 16, 19, 245));
		PIXLUI::StrokeChamfered(
			searchDraw,
			searchStart,
			ImVec2(searchStart.x + searchWidth, searchStart.y + searchHeight),
			PIXLUI::Ref(3.0f),
			PIXLUI::Colors::BorderSoft,
			PIXLUI::Ref(1.0f));

		ImGui::PushStyleVar(
			ImGuiStyleVar_FrameBorderSize,
			0.0f);
		ImGui::PushStyleVar(
			ImGuiStyleVar_FrameRounding,
			0.0f);
		ImGui::PushStyleVar(
			ImGuiStyleVar_FramePadding,
			ImVec2(
				PIXLUI::Ref(29.0f),
				PIXLUI::Ref(7.0f)));
		ImGui::PushStyleColor(
			ImGuiCol_FrameBg,
			ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(
			ImGuiCol_Border,
			ImVec4(0, 0, 0, 0));

		ImGui::SetCursorScreenPos(searchStart);
		ImGui::SetNextItemWidth(searchWidth);
		Util::DrawFeatureSearchBar(featureSearch);

		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar(3);

		// Four player-facing category buttons begin at the exact reference
		// baseline: 19px inset, 93px below the sidebar chassis top.
		ImGui::SetCursorScreenPos(
			ImVec2(
				railOrigin.x +
					PIXLUI::Ref(
						PIXLUI::Layout::TuneSidebarNavX),
				railOrigin.y +
					PIXLUI::Ref(
						PIXLUI::Layout::TuneSidebarNavY)));

		for (size_t i = 0; i < menuList.size(); i++) {
			if (std::holds_alternative<BuiltInMenu>(menuList[i]))
				continue;

			std::visit(
				ListMenuVisitor{
					i,
					selectedMenu,
					categoryExpansionStates },
				menuList[i]);
		}

		// The product controls remain below the visual categories, but they use
		// the same zero-spacing rhythm instead of accumulating default ImGui
		// ItemSpacing between every separator and button.
		ImGui::Dummy(
			ImVec2(
				0,
				PIXLUI::Ref(8.0f)));
		PIXLUI::RailDivider("RENDERER");

		for (size_t i = 0; i < menuList.size(); i++) {
			if (std::holds_alternative<BuiltInMenu>(menuList[i])) {
				std::visit(
					ListMenuVisitor{
						i,
						selectedMenu,
						categoryExpansionStates },
					menuList[i]);
			}
		}
	}

	ImGui::EndChild();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar();
}

void TuningWorkspaceRenderer::RenderRightColumn(
	const std::vector<MenuFuncInfo>& menuList,
	size_t selectedMenu,
	std::string& pendingFeatureSelection)
{
	if (selectedMenu < menuList.size()) {
		std::visit(DrawMenuVisitor{ pendingFeatureSelection }, menuList[selectedMenu]);
	} else {
		ImGui::TextDisabled("%s", T("menu.features.select_item_left", "Please select an item on the left."));
	}
}

void TuningWorkspaceRenderer::ListMenuVisitor::operator()(const BuiltInMenu& menu)
{
	MenuFonts::FontRoleGuard fontGuard(Menu::FontRole::Subheading);
	const bool isPipelineHealth = (menu.name == T("menu.features.feature_issues", "RenderModule Issues"));
	if (isPipelineHealth)
		ImGui::PushStyleColor(ImGuiCol_Text, globals::menu->GetSettings().Theme.StatusPalette.Error);

	if (PIXLUI::NavItem(fmt::format("BuiltIn{}", listId).c_str(), menu.name.c_str(), selectedMenuRef == listId))
		selectedMenuRef = listId;

	if (isPipelineHealth)
		ImGui::PopStyleColor();
}

void TuningWorkspaceRenderer::ListMenuVisitor::operator()(const std::string& label)
{
	// Style "Unloaded Features" to match category headers
	if (label == T("menu.features.unloaded_features", "Unloaded Features")) {
		Util::DrawSectionHeader(label.c_str(), true);
	} else {
		// Use default separator text for other labels - should be themed via ImGuiCol_Separator
		SeparatorTextWithFont(label, Menu::FontRole::Subheading);
	}
}

void TuningWorkspaceRenderer::ListMenuVisitor::operator()(const CategoryHeader& header)
{
	if (header.name == "LAB SYSTEMS") {
		PIXLUI::RailDivider("LAB SYSTEMS");
		return;
	}

	bool isExpanded =
		categoryExpansionStates[
			header.name];

	{
		MenuFonts::FontRoleGuard fontGuard(
			Menu::FontRole::Heading);
		const int count =
			Menu::categoryCounts[
				std::string(
					header.name)];
		const auto categoryLabel =
			TranslateFeatureCategory(
				header.name);

		Util::DrawCategoryHeader(
			header.name.c_str(),
			categoryLabel.c_str(),
			isExpanded,
			count);
	}

	categoryExpansionStates[
		header.name] =
			isExpanded;
}

void TuningWorkspaceRenderer::ListMenuVisitor::operator()(const SubcategoryHeader& header)
{
	MenuFonts::FontRoleGuard fontGuard(Menu::FontRole::Subtext);
	ImGui::Spacing();
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.66f, 0.64f, 0.59f, 1.0f));
	ImGui::SeparatorText(header.name.c_str());
	ImGui::PopStyleColor();
}

void TuningWorkspaceRenderer::ListMenuVisitor::operator()(const CategoryPage& page)
{
	MenuFonts::FontRoleGuard fontGuard(Menu::FontRole::Heading);
	if (PIXLUI::NavItem(
			fmt::format("Category{}", listId).c_str(),
			page.name.c_str(),
			selectedMenuRef == listId,
			PIXLUI::Ref(PIXLUI::Layout::NavigationHeight))) {
		selectedMenuRef = listId;
	}
	ImGui::Dummy(
		ImVec2(
			0,
			PIXLUI::Ref(
				PIXLUI::Layout::NavigationGap)));
}

void TuningWorkspaceRenderer::ListMenuVisitor::operator()(RenderModule* feat)
{
	MenuFonts::FontRoleGuard fontGuard(Menu::FontRole::Subheading);

	const auto featureName = feat->GetShortName();
	bool isDisabled = globals::state->IsFeatureDisabled(featureName);
	bool isLoaded = feat->loaded;
	bool hasFailedMessage = !feat->failedLoadedMessage.empty();
	auto& themeSettings = globals::menu->GetSettings().Theme;

	ImVec4 textColor;

	// Determine the text color based on the state
	if (isDisabled) {
		textColor = themeSettings.StatusPalette.Disable;
	} else if (isLoaded) {
		textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
	} else if (hasFailedMessage) {
		textColor = feat->version.empty() ? themeSettings.StatusPalette.Disable : themeSettings.StatusPalette.Error;
	} else {
		// Installed but not loaded means the feature is only pending a restart (green),
		// otherwise it is simply missing (grey).
		textColor = feat->installed ? themeSettings.StatusPalette.RestartNeeded : themeSettings.StatusPalette.Disable;
	}

	// Create selectable item with semantic color
	ImGui::PushStyleColor(ImGuiCol_Text, textColor);
	const auto nativeName = feat->GetDisplayName();
	const auto publicName = PIXLRendererPage::GetPublicName(feat->GetShortName(), nativeName);
	if (ImGui::Selectable(fmt::format(" {} ", publicName).c_str(), selectedMenuRef == listId, ImGuiSelectableFlags_SpanAllColumns)) {
		selectedMenuRef = listId;
	}
	ImGui::PopStyleColor();

	// Display the stage marker behind the name, regardless of loaded state
	if (const auto stage = feat->GetReleaseStage(); stage != RenderModule::ReleaseStage::Release) {
		ImGui::SameLine();
		ImGui::TextColored(StageTagColor(stage), "%s", RenderModule::GetReleaseStageTag(stage).c_str());
	}

	// Display version if loaded
	if (isLoaded) {
		ImGui::SameLine();
		std::string formattedVersion = feat->version;
		std::replace(formattedVersion.begin(), formattedVersion.end(), '-', '.');
		ImGui::TextDisabled(fmt::format("({})", formattedVersion).c_str());
	}
}

void TuningWorkspaceRenderer::DrawMenuVisitor::operator()(const BuiltInMenu& menu)
{
	ImGui::PushStyleColor(
		ImGuiCol_ChildBg,
		ImVec4(0, 0, 0, 0));

	if (ImGui::BeginChild(
			"##FeatureConfigFrame",
			{ 0, 0 },
			ImGuiChildFlags_None,
			ImGuiWindowFlags_None)) {
		// Add spacing only for Home menu.
		if (menu.name ==
			T(
				"menu.features.home",
				"Home")) {
			ImGui::Dummy(
				ImVec2(
					0,
					ThemeManager::Constants::
						BUTTON_SPACING));
		}

		if (menu.name ==
			"PIXL Renderer") {
			menu.func();
		} else if (menu.name ==
				   "Profiling") {
			// Profiling owns a dedicated constrained PIXL trace surface.
			menu.func();
		} else {
			// FINAL BUILT-IN ENGINEERING SURFACE
			// General / Advanced / Profiling originate in mature CS-era
			// renderer utilities. Keep their logic, but constrain them inside
			// one PIXL-authored viewport instead of letting their controls run
			// flush to the outer content chassis.
			const ImVec2 available =
				ImGui::GetContentRegionAvail();
			const float insetX =
				PIXLUI::Ref(14.0f);
			const float insetTop =
				PIXLUI::Ref(10.0f);
			const float insetBottom =
				PIXLUI::Ref(10.0f);

			ImGui::SetCursorPos(
				ImVec2(
					ImGui::GetCursorPosX() +
						insetX,
					ImGui::GetCursorPosY() +
						insetTop));

			const ImVec2 surfaceSize(
				std::max(
					PIXLUI::Ref(320.0f),
					available.x -
						insetX * 2.0f),
				std::max(
					PIXLUI::Ref(220.0f),
					available.y -
						insetTop -
						insetBottom));

			PIXLUI::ChromeScope
				engineeringSurface(
					"##PIXLBuiltInEngineeringSurface",
					surfaceSize,
					PIXLUI::ChromeStyle::Dark,
					true,
					ImGuiWindowFlags_None,
					PIXLUI::Ref(10.0f));

			if (engineeringSurface) {
				PIXLUI::EngineeringStyleScope
					builtInStyle;

				// Small internal inset keeps native tab bars, trees, text and
				// progress bars comfortably inside the PIXL chrome.
				ImGui::Dummy(
					ImVec2(
						0,
						PIXLUI::Ref(2.0f)));

				menu.func();
			}
		}
	}

	ImGui::EndChild();
	ImGui::PopStyleColor();
}

void TuningWorkspaceRenderer::DrawMenuVisitor::operator()(const std::string&)
{
	// std::unreachable() from c++23
	// you are not supposed to have selected a label!
}

void TuningWorkspaceRenderer::DrawMenuVisitor::operator()(const CategoryHeader&)
{
	// Category headers are not selectable in the right panel
	ImGui::TextDisabled("%s", T("menu.features.select_feature_left", "Please select a feature from the left."));
}

void TuningWorkspaceRenderer::DrawMenuVisitor::operator()(const SubcategoryHeader&)
{
	// Subcategory labels are navigation aids and are not selectable.
	ImGui::TextDisabled("%s", T("menu.features.select_feature_left", "Please select a feature from the left."));
}

void TuningWorkspaceRenderer::DrawMenuVisitor::operator()(const CategoryPage& page)
{
	ImGui::PushStyleColor(
		ImGuiCol_ChildBg,
		ImVec4(0, 0, 0, 0));
	ImGui::PushStyleVar(
		ImGuiStyleVar_ItemSpacing,
		ImVec2(0.0f, 0.0f));

	if (ImGui::BeginChild(
			"##PIXLCategoryCanvas",
			{ 0, 0 },
			ImGuiChildFlags_None,
			ImGuiWindowFlags_None)) {
		// This child is the sole vertical scroll owner for category/module rows.
		// Do not create a second scrollbar on the outer content chassis.
		// PASS A8.1: absolute PIXL geometry must still participate in ImGui
		// scrolling. GetWindowPos() is stationary, so subtract the child scroll
		// offset from the authored y-origin.
		const ImVec2 categoryWindowPos =
			ImGui::GetWindowPos();
		const ImVec2 contentOrigin(
			categoryWindowPos.x,
			categoryWindowPos.y - ImGui::GetScrollY());

		const char* description =
			page.name == "LIGHTING" ? "Global illumination, reflections, local light, shadow and atmospheric response." :
			page.name == "WORLD" ? "Materials, terrain, vegetation, water and weather working as one environment." :
			page.name == "CHARACTER" ? "SkinOptics, subsurface diffusion, hair and translucent material response." :
			"Exposure, colour, tone mapping, reconstruction and presentation.";

		// The authored title plate starts at the content-frame origin.
		ImGui::SetCursorScreenPos(contentOrigin);
		PIXLUI::PageTitle(
			page.name.c_str(),
			description);

		std::string_view currentSection;
		bool firstSection = true;

		for (auto* feature : page.features) {
			const auto placement =
				PIXLRendererPage::GetPlacement(
					feature->GetShortName());
			if (!placement)
				continue;

			if (placement->section != currentSection) {
				currentSection = placement->section;

				// For the first section the title image ends exactly where the
				// parchment starts (reference y=244). Later sections naturally
				// arrive at their reference y from the preceding row's 12px gap.
				if (firstSection) {
					ImGui::SetCursorScreenPos(
						ImVec2(
							contentOrigin.x +
								PIXLUI::Ref(
									PIXLUI::Layout::TuneSectionInsetX),
							contentOrigin.y +
								PIXLUI::Ref(
									PIXLUI::Layout::TuneTitleHeight)));
				} else {
					ImGui::SetCursorScreenPos(
						ImVec2(
							contentOrigin.x +
								PIXLUI::Ref(
									PIXLUI::Layout::TuneSectionInsetX),
							ImGui::GetCursorScreenPos().y));
				}

				PIXLUI::SectionBanner(
					std::string(currentSection).c_str());

				const float sectionToRowGap =
					firstSection
						? PIXLUI::Ref(
							PIXLUI::Layout::TuneFirstSectionRowGap)
						: PIXLUI::Ref(
							PIXLUI::Layout::TuneLaterSectionRowGap);

				ImGui::SetCursorScreenPos(
					ImVec2(
						contentOrigin.x +
							PIXLUI::Ref(
								PIXLUI::Layout::TuneFeatureInsetX),
						ImGui::GetCursorScreenPos().y +
							sectionToRowGap));

				firstSection = false;
			} else {
				// Every feature row returns with the exact 12px authored
				// vertical rhythm already registered by FeatureRow().
				ImGui::SetCursorScreenPos(
					ImVec2(
						contentOrigin.x +
							PIXLUI::Ref(
								PIXLUI::Layout::TuneFeatureInsetX),
						ImGui::GetCursorScreenPos().y));
			}

			RenderCategoryFeature(
				feature,
				false);
		}
	}

	ImGui::EndChild();
	ImGui::PopStyleVar();
	ImGui::PopStyleColor();

	RenderReactiveConstraintWarningDialog();
}


void TuningWorkspaceRenderer::DrawMenuVisitor::RenderSystemModal(
	RenderModule* feat,
	bool& open,
	ImVec2 rowAnchor)
{
	if (!feat || !open)
		return;

	const auto shortName =
		feat->GetShortName();
	const auto publicName =
		std::string(
			PIXLRendererPage::GetPublicName(
				shortName,
				feat->GetDisplayName()));

	const bool disabled =
		globals::state->IsFeatureDisabled(
			shortName);
	const bool loaded =
		feat->loaded;

	auto [description, keyFeatures] =
		feat->GetModuleSummary();
	static_cast<void>(keyFeatures);

	const ImVec2 modalSize(
		PIXLUI::Ref(690.0f),
		PIXLUI::Ref(520.0f));

	const ImGuiViewport* viewport =
		ImGui::GetMainViewport();

	const float desiredX =
		rowAnchor.x +
			PIXLUI::Ref(320.0f);
	const float desiredY =
		rowAnchor.y +
			PIXLUI::Ref(
				PIXLUI::Layout::TuneFeatureHeight +
				5.0f);

	const ImVec2 modalPos(
		std::clamp(
			desiredX,
			viewport->WorkPos.x +
				PIXLUI::Ref(12.0f),
			viewport->WorkPos.x +
				viewport->WorkSize.x -
				modalSize.x -
				PIXLUI::Ref(12.0f)),
		std::clamp(
			desiredY,
			viewport->WorkPos.y +
				PIXLUI::Ref(12.0f),
			viewport->WorkPos.y +
				viewport->WorkSize.y -
				modalSize.y -
				PIXLUI::Ref(12.0f)));

	ImGui::SetNextWindowPos(
		modalPos,
		ImGuiCond_Always);
	ImGui::SetNextWindowSize(
		modalSize,
		ImGuiCond_Always);

	ImGui::PushStyleVar(
		ImGuiStyleVar_WindowPadding,
		ImVec2(
			PIXLUI::Ref(18.0f),
			PIXLUI::Ref(16.0f)));
	ImGui::PushStyleColor(
		ImGuiCol_WindowBg,
		PIXLUI::ToVec4(
			PIXLUI::Colors::Inset));

	const ImGuiWindowFlags flags =
		ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoSavedSettings;

	const std::string windowId =
		"##PIXLSystemPanel_" +
		shortName;

	if (ImGui::Begin(
			windowId.c_str(),
			nullptr,
			flags)) {
		const ImVec2 panelMin =
			ImGui::GetWindowPos();
		const ImVec2 panelMax(
			panelMin.x +
				ImGui::GetWindowSize().x,
			panelMin.y +
				ImGui::GetWindowSize().y);

		PIXLUI::DrawChrome(
			panelMin,
			panelMax,
			PIXLUI::ChromeStyle::Raised,
			true);

		const ImVec2 titlePos =
			ImGui::GetCursorScreenPos();

		{
			MenuFonts::FontRoleGuard title(
				Menu::FontRole::Title);
			ImGui::SetWindowFontScale(1.12f);
			ImGui::TextColored(
				PIXLUI::ToVec4(
					PIXLUI::Colors::Text),
				"%s",
				publicName.c_str());
			ImGui::SetWindowFontScale(1.0f);
		}

		const float closeWidth =
			PIXLUI::Ref(78.0f);
		ImGui::SetCursorScreenPos(
			ImVec2(
				panelMax.x -
					closeWidth -
					PIXLUI::Ref(18.0f),
				titlePos.y));

		if (PIXLUI::ActionButton(
				"CLOSE",
				ImVec2(
					closeWidth,
					PIXLUI::Ref(30.0f)),
				false)) {
			open = false;
		}

		ImGui::SetCursorScreenPos(
			ImVec2(
				titlePos.x,
				titlePos.y +
					PIXLUI::Ref(31.0f)));

		ImGui::PushTextWrapPos(
			ImGui::GetCursorPosX() +
			modalSize.x -
			PIXLUI::Ref(170.0f));
		ImGui::TextColored(
			PIXLUI::ToVec4(
				PIXLUI::Colors::TextMuted),
			"%s",
			description.c_str());
		ImGui::PopTextWrapPos();

		ImGui::Dummy(
			ImVec2(
				0,
				PIXLUI::Ref(9.0f)));

		PIXLUI::SectionBanner(
			"ENGINEER CONTROLS");

		const float controlHeight =
			modalSize.y -
			PIXLUI::Ref(145.0f);

		ImGui::PushStyleColor(
			ImGuiCol_ChildBg,
			ImVec4(0, 0, 0, 0));

		if (ImGui::BeginChild(
				"##PIXLSystemControls",
				ImVec2(
					0,
					controlHeight),
				ImGuiChildFlags_None,
				ImGuiWindowFlags_None)) {
			if (disabled) {
				ImGui::TextDisabled(
					"This renderer service is disabled in the saved configuration.");
				ImGui::Dummy(
					ImVec2(
						0,
						PIXLUI::Ref(8.0f)));

				if (PIXLUI::ActionButton(
						"ENABLE AT NEXT START",
						ImVec2(
							PIXLUI::Ref(180.0f),
							PIXLUI::Ref(34.0f)),
						false)) {
					feat->ToggleAtBootSetting();
					globals::state->Save();
				}
			} else if (loaded) {
				auto* sceneManager =
					globals::sceneSettingsManager;
				const bool sceneControlled =
					sceneManager
						->HasActiveSettingsForFeature(
							shortName) &&
					!sceneManager
						->IsFeaturePaused(
							shortName);

				RenderFeatureSettings(
					feat,
					false,
					true,
					!feat
						->failedLoadedMessage
						.empty(),
					sceneControlled);
			} else if (feat->installed) {
				ImGui::TextDisabled(
					"Available after restarting Skyrim.");
			} else {
				ImGui::TextDisabled(
					"Renderer component is unavailable.");
			}
		}

		ImGui::EndChild();
		ImGui::PopStyleColor();

		if (!disabled &&
			loaded) {
			const ImVec2 resetPos(
				panelMin.x +
					PIXLUI::Ref(18.0f),
				panelMax.y -
					PIXLUI::Ref(48.0f));

			ImGui::SetCursorScreenPos(
				resetPos);

			if (PIXLUI::ActionButton(
					"RESTORE PIXL DEFAULT",
					ImVec2(
						PIXLUI::Ref(190.0f),
						PIXLUI::Ref(32.0f)),
					false)) {
				feat->RestoreDefaultSettings();
			}
		}

		if (ImGui::IsKeyPressed(
				ImGuiKey_Escape)) {
			open = false;
		}
	}

	ImGui::End();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar();
}


void TuningWorkspaceRenderer::DrawMenuVisitor::RenderCategoryFeature(RenderModule* feat, bool forceOpen)
{
	const auto shortName = feat->GetShortName();
	const auto publicName =
		PIXLRendererPage::GetPublicName(
			shortName,
			feat->GetDisplayName());
	const bool disabled =
		globals::state->IsFeatureDisabled(shortName);
	const auto placement =
		PIXLRendererPage::GetPlacement(shortName);

	// UI-only disclosure state. This replaces the visibly generic ImGui
	// TreeNode chrome while keeping exactly one interaction level.
	static std::unordered_map<std::string, bool> openRows;
	bool& open = openRows[shortName];
	if (forceOpen)
		open = true;

	ImGui::PushID(shortName.c_str());

	bool enabledAtBoot = !disabled;
	const ImVec2 rowAnchor =
		ImGui::GetCursorScreenPos();

	const auto row =
		PIXLUI::FeatureRow(
			"##PIXLFeatureRow",
			std::string(publicName).c_str(),
			&enabledAtBoot,
			open);

	if (row.togglePressed) {
		feat->ToggleAtBootSetting();
		globals::state->Save();
	}

	if (row.openPressed) {
		if (open) {
			open = false;
		} else {
			for (auto& [name, isOpen] :
				 openRows) {
				static_cast<void>(name);
				isOpen = false;
			}

			PIXLUI::SetAnimationValue(
				"##SystemPanelEntrance",
				0.0f);
			open = true;
		}
	}

	if (placement && row.rowHovered && !row.toggleHovered) {
		const auto guidance =
			std::string(
				placement->runtimeGuidance);

		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(
			ImGui::GetCursorPosX() +
			PIXLUI::Ref(330.0f));
		ImGui::TextUnformatted(guidance.c_str());
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}

	if (open) {
		const float entrance =
			PIXLUI::Animate01(
				"##SystemPanelEntrance",
				true,
				16.0f);

		ImVec2 animatedAnchor =
			rowAnchor;
		animatedAnchor.y -=
			PIXLUI::Ref(9.0f) *
			(1.0f - entrance);

		ImGui::PushStyleVar(
			ImGuiStyleVar_Alpha,
			0.78f +
				0.22f *
					entrance);

		if (shortName == "HybridGI") {
			RenderRadianceReferenceModal(
				feat,
				open,
				animatedAnchor);
		} else {
			RenderSystemModal(
				feat,
				open,
				animatedAnchor);
		}

		ImGui::PopStyleVar();
	}

	ImGui::PopID();
}

void TuningWorkspaceRenderer::DrawMenuVisitor::RenderCompactFeature(RenderModule* feat)
{
	RenderCategoryFeature(feat, false);
}

void TuningWorkspaceRenderer::DrawMenuVisitor::operator()(RenderModule* feat)
{
	const auto featureName =
		feat->GetShortName();

	if (featureName == "PixelCapture") {
		auto* capture =
			static_cast<PixelCapture*>(
				feat);

		auto* playerCamera =
			RE::PlayerCamera::GetSingleton();

		const bool foreignFreeCamera =
			playerCamera &&
			playerCamera->IsInFreeCameraMode() &&
			!g_directorPhotoMode.active;

		std::string photoModeUnavailableReason;
		const bool photoModeAvailable =
			TuningWorkspaceRenderer::
				IsDirectorPhotoModeAvailable(
					&photoModeUnavailableReason);

		ImGui::PushStyleColor(
			ImGuiCol_ChildBg,
			ImVec4(0, 0, 0, 0));

		if (ImGui::BeginChild(
				"##PIXLDirectorWorkspace",
				{ 0, 0 },
				ImGuiChildFlags_None,
				ImGuiWindowFlags_None)) {
			{
				MenuFonts::FontRoleGuard title(
					Menu::FontRole::Title);
				ImGui::SetWindowFontScale(1.18f);
				ImGui::TextColored(
					PIXLUI::ToVec4(
						PIXLUI::Colors::Text),
					"PIXL DIRECTOR");
				ImGui::SetWindowFontScale(1.0f);
			}

			ImGui::TextColored(
				PIXLUI::ToVec4(
					PIXLUI::Colors::TextMuted),
				"HOME enters photo mode. Frame continuously with the live viewfinder, quick effects and END capture hotkey.");

			ImGui::Dummy(
				ImVec2(
					0,
					PIXLUI::Ref(10.0f)));

			PIXLUI::SectionBanner(
				"PHOTO MODE");

			if (g_directorPhotoMode.active) {
				PIXLUI::StatusPill(
					"FROZEN / FREE CAMERA",
					PIXLUI::Colors::CyanBright);
			} else if (foreignFreeCamera) {
				PIXLUI::StatusPill(
					"EXTERNAL FREE CAMERA ACTIVE",
					PIXLUI::Colors::Warning);
			} else if (!photoModeAvailable) {
				PIXLUI::StatusPill(
					"WAITING FOR GAMEPLAY",
					PIXLUI::Colors::Warning);
			} else {
				PIXLUI::StatusPill(
					"GAMEPLAY",
					PIXLUI::Colors::TextDim);
			}

			ImGui::Dummy(
				ImVec2(
					0,
					PIXLUI::Ref(6.0f)));

			const ImVec2 commandButton(
				PIXLUI::Ref(184.0f),
				PIXLUI::Ref(34.0f));

			if (!g_directorPhotoMode.active) {
				ImGui::BeginDisabled(
					foreignFreeCamera ||
					!photoModeAvailable);

				if (PIXLUI::ActionButton(
						"ENTER PHOTO MODE",
						commandButton,
						true)) {
					EnterDirectorPhotoMode();
				}

				ImGui::EndDisabled();

				if (foreignFreeCamera) {
					ImGui::TextColored(
						PIXLUI::ToVec4(
							PIXLUI::Colors::TextDim),
						"Exit the existing free-camera session before PIXL Director takes control.");
				} else if (!photoModeAvailable) {
					ImGui::TextColored(
						PIXLUI::ToVec4(
							PIXLUI::Colors::TextDim),
						"%s",
						photoModeUnavailableReason.c_str());
				} else {
					ImGui::TextColored(
						PIXLUI::ToVec4(
							PIXLUI::Colors::TextDim),
						"HOME does the same anywhere in-world: freeze simulation, enter free camera, then use the live Director HUD without reopening PIXL.");
				}
			} else {
				if (PIXLUI::ActionButton(
						"COMPOSE SHOT",
						commandButton,
						true)) {
					if (globals::menu)
						globals::menu->IsEnabled =
							false;
				}

				ImGui::SameLine();

				if (PIXLUI::ActionButton(
						"EXIT PHOTO MODE",
						commandButton,
						false)) {
					ExitDirectorPhotoMode();
				}
			}

			ImGui::Dummy(
				ImVec2(
					0,
					PIXLUI::Ref(10.0f)));

			if (ImGui::BeginTable(
					"##PIXLDirectorQuickWorkspace",
					2,
					ImGuiTableFlags_SizingStretchSame |
						ImGuiTableFlags_BordersInnerV |
						ImGuiTableFlags_NoSavedSettings)) {
				ImGui::TableNextColumn();

				PIXLUI::SectionBanner(
					"WORLD");

				ImGui::TextColored(
					PIXLUI::ToVec4(
						PIXLUI::Colors::TextDim),
					"Temporary environmental direction for the current shot.");

				ImGui::Dummy(
					ImVec2(
						0,
						PIXLUI::Ref(5.0f)));

				ImGui::BeginDisabled(
					!g_directorPhotoMode.active);

				DrawDirectorEnvironment();

				ImGui::EndDisabled();

				ImGui::TableNextColumn();

				PIXLUI::SectionBanner(
					"LOOK & LENS");

				ImGui::TextColored(
					PIXLUI::ToVec4(
						PIXLUI::Colors::TextDim),
					"Fast photographic controls from Camera + Post FX.");

				ImGui::Dummy(
					ImVec2(
						0,
						PIXLUI::Ref(5.0f)));

				DrawDirectorQuickLook();

				if (g_directorPhotoMode.active &&
					g_directorPhotoMode.snapshotValid) {
					ImGui::Dummy(
						ImVec2(
							0,
							PIXLUI::Ref(5.0f)));

					if (PIXLUI::ActionButton(
							"RESET PHOTO LOOK",
							ImVec2(
								PIXLUI::Ref(166.0f),
								PIXLUI::Ref(30.0f)),
							false)) {
						auto& camera =
							globals::pipeline::
								cameraSuite;

						{
							std::lock_guard<std::mutex>
								lock(
									camera
										.settingsMutex);

							camera.settings =
								g_directorPhotoMode
									.originalCamera;
						}

						camera.LoadLookTexture();
						camera.UpdateHDRData();
					}

					ImGui::SameLine();

					if (PIXLUI::ActionButton(
							"KEEP LOOK",
							ImVec2(
								PIXLUI::Ref(126.0f),
								PIXLUI::Ref(30.0f)),
							false)) {
						auto& camera =
							globals::pipeline::
								cameraSuite;

						{
							std::lock_guard<std::mutex>
								lock(
									camera
										.settingsMutex);

							g_directorPhotoMode
								.originalCamera =
									camera.settings;
						}

						if (globals::state)
							globals::state->Save();
					}
				}

				ImGui::EndTable();
			}

			ImGui::Dummy(
				ImVec2(
					0,
					PIXLUI::Ref(12.0f)));

			PIXLUI::SectionBanner(
				"CAPTURE");

			ImGui::TextColored(
				PIXLUI::ToVec4(
					PIXLUI::Colors::TextDim),
				"END captures from live photo mode. Director hides its HUD before temporal sampling so saved images remain clean.");

			ImGui::Dummy(
				ImVec2(
					0,
					PIXLUI::Ref(6.0f)));

			ImGui::BeginDisabled(
				!capture ||
				!capture->loaded);

			if (PIXLUI::ActionButton(
					"TAKE PHOTO",
					ImVec2(
						PIXLUI::Ref(190.0f),
						PIXLUI::Ref(36.0f)),
					true)) {
				if (capture->photoFinishEnabled) {
					capture->
						RequestPhotoFinishCapture();
				} else {
					capture->captureRequested =
						true;
				}
			}

			ImGui::EndDisabled();

			if (capture) {
				const auto target =
					capture->
						GetPhotoFinishSamplesTarget();

				if (target > 0u) {
					const auto captured =
						capture->
							GetPhotoFinishSamplesCaptured();

					const auto samplingText =
						std::format(
							"SAMPLING {}/{}",
							captured,
							target);

					PIXLUI::StatusPill(
						samplingText.c_str(),
						PIXLUI::Colors::CyanBright);
				} else if (capture->
						   GetPendingCaptureCount() >
					   0u) {
					PIXLUI::StatusPill(
						"RECONSTRUCTING / ENCODING",
						PIXLUI::Colors::CyanSoft);
				}
			}

			ImGui::Dummy(
				ImVec2(
					0,
					PIXLUI::Ref(8.0f)));

			if (ImGui::BeginChild(
					"##PIXLDirectorCaptureSettings",
					ImVec2(
						0,
						PIXLUI::Ref(300.0f)),
					ImGuiChildFlags_None,
					ImGuiWindowFlags_None)) {
				PIXLUI::EngineeringStyleScope
					directorStyle;

				PIXLUI::SectionBanner(
					"OUTPUT & FRAMING");

				feat->DrawSettings();
			}
			ImGui::EndChild();

			ImGui::Dummy(
				ImVec2(
					0,
					PIXLUI::Ref(10.0f)));

			PIXLUI::SectionBanner(
				"PHOTO FINISH");

			ImGui::TextColored(
				PIXLUI::ToVec4(
					PIXLUI::Colors::TextMuted),
				"Multi-frame reconstruction and high-resolution final-image processing.");

			ImGui::Dummy(
				ImVec2(
					0,
					PIXLUI::Ref(5.0f)));

			if (capture) {
				PIXLUI::LabeledToggle(
					"Photo Finish",
					&capture->
						photoFinishEnabled);

				ImGui::BeginDisabled(
					!capture->
						photoFinishEnabled ||
					capture->
						IsPhotoFinishSampling());

				const char* resolutionLabels[] = {
					"Native",
					"2x Super Resolution",
					"4x Ultra Resolution"
				};

				int resolutionIndex =
					capture->photoFinishScale >= 4u
						? 2
						: capture->photoFinishScale >= 2u
							? 1
							: 0;

				if (PIXLUI::CycleSelector(
						"Output resolution",
						&resolutionIndex,
						resolutionLabels,
						static_cast<int>(
							std::size(
								resolutionLabels)))) {
					capture->photoFinishScale =
						resolutionIndex == 2
							? 4u
							: resolutionIndex == 1
								? 2u
								: 1u;
				}

				const char* temporalLabels[] = {
					"1 frame",
					"4 frame resolve",
					"8 frame resolve",
					"16 frame cinematic",
					"24 frame ultra"
				};

				int temporalIndex =
					capture->photoFinishTemporalSamples >= 24u
						? 4
						: capture->photoFinishTemporalSamples >= 16u
							? 3
							: capture->photoFinishTemporalSamples >= 8u
								? 2
								: capture->photoFinishTemporalSamples >= 4u
									? 1
									: 0;

				if (PIXLUI::CycleSelector(
						"Temporal detail",
						&temporalIndex,
						temporalLabels,
						static_cast<int>(
							std::size(
								temporalLabels)))) {
					capture->photoFinishTemporalSamples =
						temporalIndex == 4
							? 24u
							: temporalIndex == 3
								? 16u
								: temporalIndex == 2
									? 8u
									: temporalIndex == 1
										? 4u
										: 1u;
				}

				PIXLUI::SliderFloatField(
					"Detail reconstruction",
					&capture->
						photoFinishDetailStrength,
					0.0f,
					1.0f,
					"%.2f");

				PIXLUI::LabeledToggle(
					"Motion finish",
					&capture->
						photoFinishMotionEnabled);

				ImGui::BeginDisabled(
					!capture->
						photoFinishMotionEnabled);

				PIXLUI::SliderFloatField(
					"Shutter strength",
					&capture->
						photoFinishMotionStrength,
					0.0f,
					1.0f,
					"%.2f");

				PIXLUI::SliderFloatField(
					"Shutter direction",
					&capture->
						photoFinishMotionAngleDegrees,
					-180.0f,
					180.0f,
					"%+.0f deg");

				ImGui::EndDisabled();
				ImGui::EndDisabled();

				ImGui::TextColored(
					PIXLUI::ToVec4(
						PIXLUI::Colors::TextDim),
					"C5E.3 uses each deterministic projection-jitter position as real sub-pixel reconstruction data; cubic filtering is retained only as a low-weight hole-fill prior.");

				ImGui::TextColored(
					PIXLUI::ToVec4(
						PIXLUI::Colors::TextDim),
					"4x is automatically reduced on very high input resolutions if the final image would exceed PIXL's 48 MP safety budget.");
			} else {
				ImGui::TextDisabled(
					"PixelCapture backend unavailable.");
			}
		}

		ImGui::EndChild();
		ImGui::PopStyleColor();

		RenderReactiveConstraintWarningDialog();
		return;
	}

	bool isDisabled =
		globals::state->IsFeatureDisabled(featureName);
	bool isLoaded = feat->loaded;
	bool hasFailedMessage = !feat->failedLoadedMessage.empty();

	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
	if (ImGui::BeginChild("##FeatureConfigFrame", { 0, 0 }, ImGuiChildFlags_None, ImGuiWindowFlags_None)) {
		// Compute scene-controlled state once for both header and settings
		auto* sceneManager = globals::sceneSettingsManager;
		bool sceneControlled = sceneManager->HasActiveSettingsForFeature(featureName) && !sceneManager->IsFeaturePaused(featureName);

		// Render feature header with integrated action buttons
		RenderFeatureHeader(feat, isDisabled, isLoaded, sceneControlled);

		// Render feature settings content
		RenderFeatureSettings(feat, isDisabled, isLoaded, hasFailedMessage, sceneControlled);

		// Render restore defaults button (floating in bottom-right)
		RenderRestoreDefaultsButton(feat, isDisabled, isLoaded);
	}
	ImGui::EndChild();
	ImGui::PopStyleColor();
	// Render reactive constraint warning outside the child window so it can appear as a top-level popup
	RenderReactiveConstraintWarningDialog();
}

void TuningWorkspaceRenderer::DrawMenuVisitor::RenderFeatureHeader(RenderModule* feat, bool isDisabled, bool isLoaded, bool sceneControlled)
{
	auto& themeSettings = globals::menu->GetSettings().Theme;
	const auto featureName = feat->GetShortName();

	// Calculate action button widths
	float buttonPadding = ThemeManager::Constants::BUTTON_PADDING;
	float buttonSpacing = ThemeManager::Constants::BUTTON_SPACING;

	const char* overrideButtonText = T("menu.features.apply_override", "Apply Override");
	float bootToggleWidth = ImGui::GetFrameHeight() * 1.6f;
	float overrideButtonWidth = ImGui::CalcTextSize(overrideButtonText).x + buttonPadding;

	// Check if override is available for this feature
	auto overrideManager = SettingsOverrideManager::GetSingleton();
	bool hasOverrides = overrideManager && overrideManager->HasFeatureOverrides(featureName);

	float totalButtonWidth = bootToggleWidth;
	if (!isDisabled && isLoaded && hasOverrides) {
		totalButtonWidth += overrideButtonWidth + buttonSpacing;
	}

	// Get available content width for positioning
	float availableWidth = ImGui::GetContentRegionAvail().x;

	// Save position before drawing title
	ImVec2 titleStartPos = ImGui::GetCursorScreenPos();

	// Get feature description for subtitle
	auto [description, keyFeatures] = feat->GetModuleSummary();
	(void)keyFeatures;  // Not used for subtitle display

	// Draw feature title, version, and description on the left
	// Returns title-only height for button alignment
	const auto stage = feat->GetReleaseStage();
	const std::string stageTag = RenderModule::GetReleaseStageTag(stage);  // empty for Release; color unused when tag is empty
	float titleOnlyHeight = DrawFeatureHeader(std::string(PIXLRendererPage::GetPublicName(featureName, feat->GetDisplayName())), isLoaded ? feat->version : "", description, stageTag, StageTagColor(stage));

	// Save cursor position after header (for restoring after buttons are drawn)
	ImVec2 cursorPosAfterHeader = ImGui::GetCursorScreenPos();

	// Position action buttons to the right of the header, middle-aligned with title only
	float buttonHeight = ImGui::GetFrameHeight();

	// Calculate Y position to middle-align buttons with title text only (not description)
	float buttonY = titleStartPos.y + (titleOnlyHeight - buttonHeight) * 0.5f;

	ImGui::SetCursorScreenPos(ImVec2(titleStartPos.x + availableWidth - totalButtonWidth, buttonY));

	// Enable/Disable at boot toggle
	bool bootEnabled = !isDisabled;

	// Apply disabled styling if feature has failed to load
	if (!feat->failedLoadedMessage.empty()) {
		ImGui::PushStyleColor(ImGuiCol_Text, themeSettings.StatusPalette.Error);
	}

	if (PIXLUI::Toggle("##BootToggle", &bootEnabled)) {
		bool newState = feat->ToggleAtBootSetting();
		logger::info("{}: {} at boot.", featureName, newState ? "Enabled" : "Disabled");
	}

	if (!feat->failedLoadedMessage.empty()) {
		ImGui::PopStyleColor();
	}

	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			T("menu.features.boot_toggle_tooltip",
				"Toggle feature loading at boot.\n"
				"Current state: %s\n"
				"Restart required for changes to take effect.\n"
				"Disabling removes performance impact."),
			bootEnabled ? T("menu.features.enabled", "Enabled") : T("menu.features.disabled", "Disabled"));
	}

	// Apply Override button (when feature has available overrides)
	if (!isDisabled && isLoaded && hasOverrides) {
		ImGui::SameLine();
		if (sceneControlled)
			ImGui::BeginDisabled();
		if (PIXLUI::ActionButton(overrideButtonText, { overrideButtonWidth, 0 }, false)) {
			if (feat->ReapplyOverrideSettings()) {
				logger::info("Successfully reapplied override settings for {}", featureName);
			} else {
				logger::warn("Failed to reapply override settings for {}", featureName);
			}
		}
		if (sceneControlled)
			ImGui::EndDisabled();

		if (auto _tt = Util::HoverTooltipWrapper()) {
			if (sceneControlled) {
				ImGui::Text(
					"%s",
					T("menu.features.cannot_apply_overrides_scene",
						"Cannot apply overrides while scene-specific settings are active.\n"
						"Pause scene settings for this feature first."));
			} else {
				ImGui::Text(
					"%s",
					T("menu.features.restore_override_tooltip",
						"Restores original override settings from mod files.\n"
						"This will discard your customizations and revert to\n"
						"the mod author's recommended settings."));
			}
		}
	}

	// Restore cursor position after the title and separator
	ImGui::SetCursorScreenPos(cursorPosAfterHeader);
}

void TuningWorkspaceRenderer::DrawMenuVisitor::RenderFeatureSettings(RenderModule* feat, bool isDisabled, bool isLoaded, bool hasFailedMessage, bool sceneControlled)
{
	auto& themeSettings = globals::menu->GetSettings().Theme;

	if (isDisabled) {
		ImGui::TextColored(themeSettings.StatusPalette.Disable, "%s", T("menu.features.settings_hidden_disabled", "RenderModule settings are hidden because this feature is disabled at boot."));
		ImGui::Spacing();
		ImGui::Text("%s", T("menu.features.enable_to_access_config", "Enable the feature above to access its configuration options."));
	} else {
		if (isLoaded) {
			auto weatherRegistry = WeatherVariables::GlobalWeatherRegistry::GetSingleton();
			if (weatherRegistry->HasWeatherSupport(feat->GetShortName())) {
				bool paused =
					weatherRegistry->IsFeaturePaused(
						feat->GetShortName());

				if (PIXLUI::LabeledToggle(
						T(
							"menu.features.pause_weather_overrides",
							"Pause Weather Overrides"),
						&paused)) {
					weatherRegistry->SetFeaturePaused(
						feat->GetShortName(),
						paused);
				}

				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text(
						"%s",
						T("menu.features.pause_weather_tooltip",
							"Temporarily disable weather-based setting adjustments for this feature.\n"
							"This state is not saved."));
				}
				ImGui::Separator();
			}

			// Scene-specific settings toggle (Interior Only / TimeOfDay / Weather-Specific)
			// Show toggle whenever scene entries exist for this feature, even if feature-paused
			{
				const auto& featureShortName = feat->GetShortName();
				auto* sceneMgr = globals::sceneSettingsManager;
				bool scenePaused = sceneMgr->IsFeaturePaused(featureShortName);
				if (sceneControlled || scenePaused) {
					bool active =
						!scenePaused;

					if (PIXLUI::LabeledToggle(
							T(
								"menu.features.scene_specific_settings",
								"Scene Specific Settings"),
							&active)) {
						sceneMgr->SetFeaturePaused(
							featureShortName,
							!active);
					}

					if (auto _tt = Util::HoverTooltipWrapper()) {
						ImGui::Text("%s", T(scenePaused ? "menu.features.scene_paused_tooltip" : "menu.features.scene_active_tooltip",
											  scenePaused ? "Paused - click to resume" : "Active - click to pause"));
					}
					ImGui::Separator();
				}
			}

			// Disable feature settings while scene overrides are actively applied (not paused)
			if (sceneControlled)
				ImGui::BeginDisabled();

			ImVec2 cursorPosBefore =
				ImGui::GetCursorPos();

			{
				PIXLUI::EngineeringStyleScope
					engineerStyle;
				feat->DrawSettings();
			}

			if (globals::menu
					->GetSettings()
					.DeveloperMode &&
				PulsePanelRenderer::
					HasFeatureTimers(
						feat->GetShortName())) {
				PIXLUI::SectionBanner(
					T(
						"menu.features.profiling",
						"PROFILING"));

				PIXLUI::EngineeringStyleScope
					profilingStyle;
				PulsePanelRenderer::
					RenderFeatureTimers(
						feat->GetShortName());
			}

			ImVec2 cursorPosAfter =
				ImGui::GetCursorPos();

			if (sceneControlled)
				ImGui::EndDisabled();

			// --- Reactive constraint detection ---
			// Compare the current full constraint set against g_knownConstraintKeys.
			// On the very first frame we just seed the set (no popup); after that
			// any key that wasn't previously known triggers the warning.
			// This catches both same-frame changes (e.g. TerrainSeam toggle)
			// and next-frame changes (e.g. ImageReconstruction, whose resolutionScale is
			// updated in the render loop, not in DrawSettings).
			const double constraintNow =
				ImGui::GetTime();
			const bool constraintScanDue =
				!g_knownConstraintKeysInitialised ||
				ImGui::IsMouseReleased(
					ImGuiMouseButton_Left) ||
				constraintNow >=
					g_nextConstraintScanTime;

			if (!g_reactiveWarningShow &&
				constraintScanDue) {
				g_nextConstraintScanTime =
					constraintNow +
					0.125;

				auto currentConstraints =
					ModuleRules::
						GetAllActiveConstraints();

				if (!g_knownConstraintKeysInitialised) {
					// First time: seed known set, no popup
					for (const auto& [settingId, result] : currentConstraints) {
						g_knownConstraintKeys.insert(settingId.featureShortName + "|" + settingId.settingPath);
					}
					g_knownConstraintKeysInitialised = true;
				} else {
					// Diff: find keys present now but not previously known
					std::vector<std::pair<ModuleRules::SettingId, ModuleRules::ConstraintResult>> newConstraints;
					std::unordered_set<std::string> currentKeys;
					for (const auto& [settingId, result] : currentConstraints) {
						std::string key = settingId.featureShortName + "|" + settingId.settingPath;
						currentKeys.insert(key);
						if (g_knownConstraintKeys.find(key) == g_knownConstraintKeys.end()) {
							newConstraints.emplace_back(settingId, result);
						}
					}
					// Update known set to current (removes keys for constraints that went away)
					g_knownConstraintKeys = std::move(currentKeys);

					if (!newConstraints.empty() && !globals::menu->GetSettings().SkipConstraintWarning) {
						logger::info("Reactive constraint detection: {} new constraints", newConstraints.size());
						for (const auto& [settingId, result] : newConstraints) {
							logger::info("  - {}.{} forced to {} by {}", settingId.featureShortName, settingId.settingPath, ModuleRules::FormatConstraintValue(result.forcedValue), result.sources.empty() ? "?" : result.sources[0].featureName);
						}
						g_reactiveWarningShow = true;
						g_reactiveWarningConstraints = std::move(newConstraints);
						g_dontShowAgainCheckbox = false;
					}
				}
			}

			const float cursorEpsilon = 0.1f;
			bool cursorMoved = (std::abs(cursorPosAfter.x - cursorPosBefore.x) > cursorEpsilon ||
								std::abs(cursorPosAfter.y - cursorPosBefore.y) > cursorEpsilon);
			if (!cursorMoved) {
				ImGui::TextColored(themeSettings.StatusPalette.Disable, "%s", T("menu.features.no_settings_available", "There are no settings available for this feature."));
			}
		} else {
			if (PipelineHealth::IsObsoleteFeature(feat->GetShortName())) {
				feat->DrawUnloadedUI();
			} else if (feat->installed) {
				ImGui::Text("%s", T("menu.features.available_after_restart", "This feature will be available after restart."));
			} else {
				feat->DrawUnloadedUI();
				if (!feat->GetModuleSupportLink().empty()) {
					ImGui::Spacing();
					auto featureModLink = feat->GetModuleSupportLink();
					const auto downloadText = std::vformat(
						T("menu.features.download_link", "Click here to download this feature ({})"), std::make_format_args(featureModLink));
					if (ImGui::Selectable(downloadText.c_str())) {
						ShellExecuteA(NULL, "open", featureModLink.c_str(), NULL, NULL, SW_SHOWNORMAL);
					}
					if (auto _tt = Util::HoverTooltipWrapper()) {
						ImGui::Text("%s", T("menu.features.download_tooltip", "Download the feature from the mod page."));
					}
				}
			}
		}
	}

	if (hasFailedMessage && feat->DrawFailLoadMessage() && !PipelineHealth::IsObsoleteFeature(feat->GetShortName())) {
		ImGui::Spacing();
		SeparatorTextWithFont(T("menu.features.error_header", "Error"), Menu::FontRole::Subheading);
		ImGui::TextColored(themeSettings.StatusPalette.Error, feat->failedLoadedMessage.c_str());
	}
}

void TuningWorkspaceRenderer::DrawMenuVisitor::RenderRestoreDefaultsButton(RenderModule* feat, bool isDisabled, bool isLoaded)
{
	if (isDisabled || !isLoaded) {
		return;
	}

	// PIXL uses a compact text action rather than the inherited floating icon.
	const auto& style = ImGui::GetStyle();
	ImVec2 windowPos = ImGui::GetWindowPos();
	ImVec2 windowSize = ImGui::GetWindowSize();
	float scrollbarWidth = ImGui::GetScrollMaxY() > 0 ? style.ScrollbarSize : 0.0f;
	const char* label = "RESET MODULE";
	ImVec2 frameSize(ImGui::CalcTextSize(label).x + style.FramePadding.x * 2, ImGui::GetFrameHeight());
	ImGui::SetCursorScreenPos(ImVec2(
		windowPos.x + windowSize.x - frameSize.x - style.WindowPadding.x - scrollbarWidth,
		windowPos.y + windowSize.y - frameSize.y - style.WindowPadding.y));

	if (PIXLUI::ActionButton(label, frameSize, false))
		feat->RestoreDefaultSettings();

	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T("menu.features.restore_defaults_tooltip", "Restore default settings for this feature"));
	}
}

void TuningWorkspaceRenderer::DrawMenuVisitor::RenderReactiveConstraintWarningDialog()
{
	if (!g_reactiveWarningShow) {
		return;
	}

	constexpr const char* popupId = "###SettingChangeWarning";
	const std::string popupTitle = fmt::format("{}{}", T("menu.features.setting_change_warning_title", "Setting Change Warning"), popupId);

	// OpenPopup is idempotent while the popup is already open, so calling it
	// every frame while the flag is set is safe and ensures we don't miss the
	// one-frame window where ImGui expects it.
	ImGui::OpenPopup(popupId);

	// Center the popup (ImGuiCond_Always matches the Clear Cache dialog pattern)
	ImVec2 center = ImGui::GetMainViewport()->GetCenter();
	ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

	if (Util::BeginPopupModalWithRoundedClose(popupTitle.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::TextWrapped("%s", T("menu.features.settings_adjusted_warning", "Some of your settings have been automatically adjusted due to feature incompatibilities."));
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// Table columns: Impacted RenderModule | Setting | Constrained By | Forced To
		if (ImGui::BeginTable("##ReactiveConstraintTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
			ImGui::TableSetupColumn(T("menu.features.col_impacted_feature", "Impacted RenderModule"), ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn(T("menu.features.col_setting", "Setting"), ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn(T("menu.features.col_constrained_by", "Constrained By"), ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn(T("menu.features.col_forced_to", "Forced To"), ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();

			size_t rowIndex = 0;
			for (const auto& [settingId, result] : g_reactiveWarningConstraints) {
				ImGui::TableNextRow();

				// --- Column 0: Impacted RenderModule (clickable -> navigate to that feature) ---
				ImGui::TableSetColumnIndex(0);
				{
					// Look up the display name of the target feature from its short name
					std::string targetDisplayName = settingId.featureShortName;
					for (auto* f : RenderModule::GetModuleList()) {
						if (f->GetShortName() == settingId.featureShortName) {
							targetDisplayName = f->GetDisplayName();
							break;
						}
					}
					if (ImGui::Selectable(fmt::format("{}##imp{}", targetDisplayName, rowIndex).c_str())) {
						pendingFeatureSelection = settingId.featureShortName;
						ImGui::CloseCurrentPopup();
						g_reactiveWarningShow = false;
						g_reactiveWarningConstraints.clear();
						return;
					}
					if (auto _tt = Util::HoverTooltipWrapper()) {
						ImGui::Text(T("menu.features.click_to_navigate", "Click to navigate to %s"), targetDisplayName.c_str());
					}
				}

				// --- Column 1: Setting name ---
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%s", settingId.settingPath.c_str());

				// --- Column 2: Constrained By (source features, clickable) ---
				ImGui::TableSetColumnIndex(2);
				if (!result.sources.empty()) {
					if (ImGui::Selectable(fmt::format("{}##src{}", result.sources[0].featureName, rowIndex).c_str())) {
						pendingFeatureSelection = result.sources[0].featureShortName;
						ImGui::CloseCurrentPopup();
						g_reactiveWarningShow = false;
						g_reactiveWarningConstraints.clear();
						return;
					}
					if (auto _tt = Util::HoverTooltipWrapper()) {
						ImGui::Text(T("menu.features.click_to_navigate", "Click to navigate to %s"), result.sources[0].featureName.c_str());
						if (result.sources.size() > 1) {
							ImGui::Separator();
							for (size_t i = 1; i < result.sources.size(); ++i) {
								ImGui::Text(T("menu.features.also_feature", "Also: %s"), result.sources[i].featureName.c_str());
							}
						}
						ImGui::Separator();
						ImGui::Text("%s", result.sources[0].reason.c_str());
					}
				}

				// --- Column 3: Forced value ---
				ImGui::TableSetColumnIndex(3);
				ImGui::Text("%s", ModuleRules::FormatConstraintValue(result.forcedValue).c_str());

				rowIndex++;
			}

			ImGui::EndTable();
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		ImGui::TextWrapped(
			"%s",
			T("menu.features.constraints_explanation",
				"These settings are disabled in their respective feature menus while the constraints are active. "
				"Adjust the constraining features to remove them."));

		ImGui::Spacing();

		// "Don't show again" checkbox -- same pattern as Clear Cache dialog
		ImGui::Checkbox(T("menu.features.dont_show_warning", "Don't show this warning again"), &g_dontShowAgainCheckbox);

		ImGui::Spacing();

		// Centered OK button
		constexpr float buttonWidth = ThemeManager::Constants::POPUP_BUTTON_WIDTH;
		const float windowWidth = ImGui::GetWindowWidth();
		const float offset = (windowWidth - buttonWidth) * 0.5f;
		if (offset > 0)
			ImGui::SetCursorPosX(offset);

		if (ImGui::Button(T("menu.features.ok_button", "OK"), ImVec2(buttonWidth, 0))) {
			if (g_dontShowAgainCheckbox) {
				if (auto* menu = globals::menu) {
					menu->GetSettings().SkipConstraintWarning = true;
				}
			}
			g_reactiveWarningShow = false;
			g_reactiveWarningConstraints.clear();
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	} else {
		// Popup was closed externally (e.g. clicked outside), reset state
		g_reactiveWarningShow = false;
		g_reactiveWarningConstraints.clear();
	}
}
