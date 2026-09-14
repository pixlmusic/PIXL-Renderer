#include "PCH.h"
#include "ExternalPostProcessing.h"
#include "../Globals.h"
#include "../Modules/CameraSuite.h"
#include "../../extern/ReShade/include/reshade_events.hpp"
#include <psapi.h>
#include <fstream>
#include <mutex>

namespace
{
	std::mutex stateMutex;
	reshade::api::effect_runtime* activeRuntime = nullptr;  // Identity only outside callbacks.
	std::string currentPreset;
	std::string pendingPreset;
	bool effectsEnabled = true;
	std::optional<bool> pendingEnabled;
	bool registered = false;
	std::vector<std::string> presets;
	std::string scanStatus;
	bool enbBridgeEnabled = false;
	std::vector<std::string> enbPresets;
	std::unordered_map<std::string, std::string> enbPresetLabels;
	std::string enbActivePreset;
	std::string enbStatus;
	struct QuickStyle
	{
		bool valid = false;
		std::array<char, 48> name{};
		float exposureEV = 0.0f;
		float contrast = 1.0f;
		float saturation = 1.0f;
		float adaptation = 1.20f;
		float highlightProtection = 0.65f;
		float shadowDetail = 0.14f;
		float toe = 0.12f;
		float shoulder = 0.72f;
		bool bloomEnabled = false;
		float bloomStrength = 0.80f;
		uint lookPreset = 0;
		float lookOpacity = 0.0f;
		float influence = 1.0f;
	};
	std::array<QuickStyle, 5> quickStyles{};
	bool quickStylesInitialized = false;

	std::optional<float> ReadIniValue(const std::filesystem::path& file, std::string_view section, std::string_view key)
	{
		std::ifstream stream(file);
		std::string line, current;
		while (std::getline(stream, line)) {
			const auto first = line.find_first_not_of(" \t\r");
			if (first == std::string::npos || line[first] == ';')
				continue;
			if (line[first] == '[') {
				const auto close = line.find(']', first + 1);
				current = close == std::string::npos ? std::string{} : line.substr(first + 1, close - first - 1);
				continue;
			}
			if (current != section)
				continue;
			const auto equals = line.find('=', first);
			if (equals == std::string::npos || line.substr(first, equals - first) != key)
				continue;
			try { return std::stof(line.substr(equals + 1)); } catch (...) { return std::nullopt; }
		}
		return std::nullopt;
	}

	std::optional<float> ReadNamedValue(const std::filesystem::path& file, std::string_view key)
	{
		std::ifstream stream(file);
		std::string line;
		while (std::getline(stream, line)) {
			const auto first = line.find_first_not_of(" \t\r");
			if (first == std::string::npos || line[first] == ';' || line[first] == '#')
				continue;
			const auto equals = line.find('=', first);
			if (equals == std::string::npos)
				continue;
			auto name = line.substr(first, equals - first);
			while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.pop_back();
			if (name != key)
				continue;
			try { return std::stof(line.substr(equals + 1)); } catch (...) { return std::nullopt; }
		}
		return std::nullopt;
	}

	std::filesystem::path FindENBEffectConfig(const std::filesystem::path& preset)
	{
		for (const auto& candidate : { preset.parent_path() / "enbseries" / "enbeffect.fx.ini", preset.parent_path() / "enbeffect.fx.ini" }) {
			std::error_code ec;
			if (std::filesystem::is_regular_file(candidate, ec))
				return candidate;
		}
		return {};
	}

	bool HasENBPalette(const std::filesystem::path& preset)
	{
		for (const auto& directory : { preset.parent_path(), preset.parent_path() / "enbseries" }) {
			for (const auto& name : { "enbpalette.bmp", "enbpalette.png", "enbpalette.dds" }) {
				std::error_code ec;
				if (std::filesystem::is_regular_file(directory / name, ec))
					return true;
			}
			std::error_code ec;
			if (std::filesystem::is_directory(directory / "Textures" / "LUTs", ec)) {
				for (const auto& entry : std::filesystem::directory_iterator(directory / "Textures" / "LUTs", ec)) {
					if (!entry.is_regular_file(ec))
						continue;
					const auto name = entry.path().filename().wstring();
					if (name.find(L"LUT") != std::wstring::npos || name.find(L"lut") != std::wstring::npos || name.find(L"palette") != std::wstring::npos)
						return true;
				}
			}
		}
		return false;
	}

	void ScanENBPresets()
	{
		enbPresets.clear();
		enbPresetLabels.clear();
		wchar_t executable[32768]{};
		const auto length = GetModuleFileNameW(nullptr, executable, 32768);
		if (!length || length >= 32768) { enbStatus = "Could not locate the game folder."; return; }
		const auto root = std::filesystem::path(executable).parent_path();
		std::vector<std::filesystem::path> candidates{ root / "enbseries.ini" };
		const auto bridgeRoot = root / "ENB-Bridge" / "Presets";
		std::error_code scanError;
		if (std::filesystem::is_directory(bridgeRoot, scanError)) {
			for (const auto& entry : std::filesystem::recursive_directory_iterator(bridgeRoot, scanError)) {
				if (entry.is_regular_file(scanError) && entry.path().filename() == L"enbseries.ini")
					candidates.push_back(entry.path());
			}
		}
		for (const auto& file : candidates) {
			std::error_code ec;
			if (std::filesystem::is_regular_file(file, ec) && !std::filesystem::is_symlink(file, ec)) {
				const auto utf8 = file.u8string();
				const std::string presetPath(reinterpret_cast<const char*>(utf8.data()), utf8.size());
				enbPresets.push_back(presetPath);
				if (file.parent_path().filename() == "Presets")
					enbPresetLabels[presetPath] = file.parent_path().parent_path().filename().string();
				else if (const auto effect = FindENBEffectConfig(file); !effect.empty()) {
					const auto rubyBrightness = ReadNamedValue(effect, "EBrightnessV2Day");
					const auto rubyCurve = ReadNamedValue(effect, "EToneMappingCurveV2Day");
					if (rubyBrightness && rubyCurve && std::abs(*rubyBrightness - 0.63f) < 0.02f && std::abs(*rubyCurve - 1.60f) < 0.05f)
						enbPresetLabels[presetPath] = "RUBY ENB 2.1 (detected)";
					else
						enbPresetLabels[presetPath] = "Installed ENB style (translated)";
				} else
					enbPresetLabels[presetPath] = "Installed ENB style (enbseries.ini)";
			}
		}
		enbStatus = std::format("Detected {} ENB preset file{}.", enbPresets.size(), enbPresets.size() == 1 ? "" : "s");
	}

	void ApplyENBPreset(const std::string& path)
	{
		const auto file = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(path.data()), path.size()));
		float exposureEV = 0.0f;
		float contrast = 1.0f;
		float saturation = 1.0f;
		float adaptation = 1.20f;
		float highlightProtection = 0.65f;
		float shadowDetail = 0.14f;
		float toe = 0.12f;
		float shoulder = 0.72f;
		uint lookPreset = 0;
		float lookOpacity = 0.0f;
		std::string toneSource = "PIXL neutral translation";
		bool bloomEnabled = false;
		float bloomStrength = 0.80f;
		if (const auto brightness = ReadIniValue(file, "COLORCORRECTION", "Brightness"))
			exposureEV = std::clamp(std::log2(std::max(*brightness, 0.01f)), -4.0f, 4.0f);
		if (const auto gamma = ReadIniValue(file, "COLORCORRECTION", "GammaCurve"))
			contrast = std::clamp(1.0f / std::max(*gamma, 0.1f), 0.75f, 1.30f);
		if (const auto adaptationValue = ReadIniValue(file, "ADAPTATION", "AdaptationSensitivity"))
			adaptation = std::clamp(*adaptationValue, 0.05f, 4.0f);
		// ENB presets express much of their signature through environment light.
		// Translate the daytime values into PIXL's camera response so a preset
		// remains recognisable without taking over Skyrim's renderer.
		if (const auto directIntensity = ReadIniValue(file, "ENVIRONMENT", "DirectLightingIntensityDay"))
			exposureEV += std::clamp(std::log2(std::max(*directIntensity, 0.25f)) * 0.65f, -1.5f, 1.5f);
		if (const auto directCurve = ReadIniValue(file, "ENVIRONMENT", "DirectLightingCurveDay"))
			contrast = std::clamp(contrast + (*directCurve - 1.0f) * 0.08f, 0.75f, 1.30f);
		if (const auto desaturation = ReadIniValue(file, "ENVIRONMENT", "DirectLightingDesaturationDay"))
			saturation = std::clamp(1.0f - *desaturation * 0.35f, 0.70f, 1.25f);
		if (const auto ambientMin = ReadIniValue(file, "SKYLIGHTING", "AmbientMinLevelDay"))
			shadowDetail = std::clamp(0.10f + *ambientMin * 0.16f, 0.0f, 0.5f);
		if (const auto bloom = ReadIniValue(file, "BLOOM", "AmountDay")) {
			bloomEnabled = *bloom > 0.001f;
			bloomStrength = std::clamp(*bloom * 8.0f, 0.0f, 3.0f);
		}
		const auto effectConfig = FindENBEffectConfig(file);
		if (!effectConfig.empty()) {
			// ENB effect configuration is a flat key/value file rather than a
			// sectioned INI. Read the daytime tone-map controls when present and
			// translate them into PIXL's bounded camera response.
			if (const auto brightness = ReadNamedValue(effectConfig, "EBrightnessV2Day"))
				exposureEV += std::clamp(std::log2(std::max(*brightness, 0.05f)) * 0.55f, -2.0f, 2.0f);
			if (const auto curve = ReadNamedValue(effectConfig, "EToneMappingCurveV2Day")) {
				lookPreset = 11; // PIXL Cinematic is the bounded fallback grade for tone-map sources.
				contrast = std::clamp(contrast + (*curve - 1.0f) * 0.10f, 0.75f, 1.30f);
				lookOpacity = std::clamp(0.08f + std::abs(*curve - 1.0f) * 0.06f, 0.08f, 0.20f);
			}
			if (const auto intensityContrast = ReadNamedValue(effectConfig, "EIntensityContrastV2Day"))
				contrast = std::clamp(contrast + (*intensityContrast - 1.0f) * 0.12f, 0.75f, 1.30f);
			if (const auto colourSaturation = ReadNamedValue(effectConfig, "EColorSaturationV2Day"))
				saturation = std::clamp(saturation * std::clamp(*colourSaturation, 0.70f, 1.40f), 0.70f, 1.25f);
			// Cabbage/LUX uses named ENB effect controls instead of the older
			// EBrightnessV2 family. These keys are still portable to any preset
			// that ships the same ENBEFFECT.FX configuration style.
			if (const auto ev = ReadNamedValue(effectConfig, "CG.HDR.|- Day - Exposure (in EVs)"))
				exposureEV += std::clamp(*ev, -2.0f, 2.0f);
			if (const auto cgContrast = ReadNamedValue(effectConfig, "CG.HDR.|- Day - Contrast"))
				contrast = std::clamp(contrast + (*cgContrast - 1.0f) * 0.25f, 0.75f, 1.30f);
			if (const auto cgSaturation = ReadNamedValue(effectConfig, "CG.HDR.|- Day - Saturation"))
				saturation = std::clamp(saturation * std::clamp(*cgSaturation, 0.70f, 1.40f), 0.70f, 1.25f);
			if (const auto kitsuuneToe = ReadNamedValue(effectConfig, "Tonemap.|- Day - Kitsuune - Toe"))
				toe = std::clamp(*kitsuuneToe * 0.12f, 0.0f, 0.5f);
			if (const auto kitsuuneShoulder = ReadNamedValue(effectConfig, "Tonemap.|- Day - Kitsuune - Shoulder"))
				shoulder = std::clamp(0.45f + *kitsuuneShoulder * 0.18f, 0.2f, 1.5f);
			toneSource = "ENB effect tone-map translation";
		} else {
			// Without an effect file there is no defensible source grade. Keep
			// PIXL neutral rather than applying Ruby's cinematic LUT universally.
			lookPreset = 0;
			lookOpacity = 0.0f;
		}
		if (HasENBPalette(file))
			toneSource += "; palette asset detected (translated safely)";
		globals::pipeline::cameraSuite.ApplyExternalLook(exposureEV, contrast, saturation, adaptation, highlightProtection, shadowDetail, toe, shoulder, bloomEnabled, bloomStrength, lookPreset, lookOpacity);
		globals::pipeline::cameraSuite.LoadLookTexture();
		globals::pipeline::cameraSuite.UpdateHDRData();
		enbActivePreset = path;
		enbBridgeEnabled = true;
		enbStatus = std::format("{} is active in PIXL. {}. Grade {:.0f}%%, exposure {:+.2f} EV, contrast {:.2f}, saturation {:.2f}, bloom {}.", enbPresetLabels.contains(path) ? enbPresetLabels[path] : "Preset", toneSource, lookOpacity * 100.0f, exposureEV, contrast, saturation, bloomEnabled ? "on" : "off");
	}

	QuickStyle CaptureCurrentStyle(std::string_view defaultName)
	{
		const auto& settings = globals::pipeline::cameraSuite.settings;
		QuickStyle style;
		style.valid = true;
		std::snprintf(style.name.data(), style.name.size(), "%s", defaultName.data());
		style.exposureEV = settings.cameraExposureCompensationEV;
		style.contrast = settings.cameraContrast;
		style.saturation = settings.cameraSaturation;
		style.adaptation = settings.cameraAdaptBrightToDark;
		style.highlightProtection = settings.cameraHighlightProtection;
		style.shadowDetail = settings.cameraShadowDetail;
		style.toe = settings.cameraToe;
		style.shoulder = settings.cameraShoulder;
		style.bloomEnabled = settings.enableBloom;
		style.bloomStrength = settings.bloomStrength;
		style.lookPreset = settings.lookPreset;
		style.lookOpacity = settings.lookOpacity;
		style.influence = settings.cameraInfluence;
		return style;
	}

	void ApplyQuickStyle(const QuickStyle& style)
	{
		globals::pipeline::cameraSuite.ApplyExternalLook(
			style.exposureEV, style.contrast, style.saturation, style.adaptation,
			style.highlightProtection, style.shadowDetail, style.toe, style.shoulder,
			style.bloomEnabled, style.bloomStrength, style.lookPreset, style.lookOpacity,
			style.influence);
		globals::pipeline::cameraSuite.LoadLookTexture();
		globals::pipeline::cameraSuite.UpdateHDRData();
		enbActivePreset.clear();
		enbBridgeEnabled = false;
		enbStatus = std::format("{} is active as a PIXL visual style.", style.name.data());
	}

	void InitializeQuickStyles()
	{
		if (quickStylesInitialized)
			return;
		quickStylesInitialized = true;
		const auto base = CaptureCurrentStyle("Custom Style");
		quickStyles = { base, base, base, base, base };
		const std::array<std::string_view, 5> names{ "Kale Preset", "Topaz Preset", "C.A.T Preset", "Ruby Inspired", "Cabbage Inspired" };
		for (std::size_t i = 0; i < quickStyles.size(); ++i)
			std::snprintf(quickStyles[i].name.data(), quickStyles[i].name.size(), "%s", names[i].data());
		// Fictional starter looks provide useful PIXL-native baselines without
		// claiming to reproduce or redistribute any third-party ENB preset.
		quickStyles[0].contrast = 1.04f;
		quickStyles[0].saturation = 0.98f;
		quickStyles[0].lookPreset = 11;
		quickStyles[0].lookOpacity = 0.12f;
		quickStyles[1].exposureEV = 0.12f;
		quickStyles[1].saturation = 1.06f;
		quickStyles[1].lookPreset = 8;
		quickStyles[1].lookOpacity = 0.12f;
		quickStyles[2].saturation = 0.98f;
		quickStyles[2].lookPreset = 1;
		quickStyles[2].lookOpacity = 0.10f;
		quickStyles[3].exposureEV = 0.30f;
		quickStyles[3].contrast = 1.06f;
		quickStyles[3].saturation = 0.92f;
		quickStyles[3].lookPreset = 11;
		quickStyles[3].lookOpacity = 0.15f;
		quickStyles[4].saturation = 1.02f;
		quickStyles[4].lookPreset = 11;
		quickStyles[4].lookOpacity = 0.12f;
	}

	void OnPresent(reshade::api::effect_runtime* runtime)
	{
		if (runtime->get_device()->get_api() != reshade::api::device_api::d3d11)
			return;  // Do not attach to PIXL's optional DX12 presentation sidecar.
		std::string requested;
		std::optional<bool> enabled;
		{
			std::lock_guard lock(stateMutex);
			if (activeRuntime && activeRuntime != runtime)
				return;
			activeRuntime = runtime;
			requested.swap(pendingPreset);
			enabled.swap(pendingEnabled);
		}
		// Runtime calls stay on ReShade's callback thread; never invoke them from
		// PIXL's ImGui thread or hold our lock through another add-on's callbacks.
		if (!requested.empty())
			runtime->set_current_preset_path(requested.c_str());
		if (enabled)
			runtime->set_effects_state(*enabled);
		char path[32768]{};
		runtime->get_current_preset_path(path);
		const bool active = runtime->get_effects_state();
		std::lock_guard lock(stateMutex);
		currentPreset = path;
		effectsEnabled = active;
	}

	void OnDestroy(reshade::api::effect_runtime* runtime)
	{
		std::lock_guard lock(stateMutex);
		if (activeRuntime != runtime)
			return;
		activeRuntime = nullptr;
		currentPreset.clear();
		pendingPreset.clear();
		pendingEnabled.reset();
	}

	void ScanPresets()
	{
		presets.clear();
		try {
			wchar_t executable[32768]{};
			const auto length = GetModuleFileNameW(nullptr, executable, 32768);
			if (!length || length >= 32768) {
				scanStatus = "Could not locate the game folder.";
				return;
			}
			const auto root = std::filesystem::path(executable).parent_path();
			// Bounded, non-recursive discovery: never inspect a user's whole disk.
			for (const auto& folder : { root, root / "ReShadePresets" }) {
				std::error_code ec;
				if (!std::filesystem::is_directory(folder, ec) || std::filesystem::is_symlink(folder, ec))
					continue;
				std::size_t inspected = 0;
				for (const auto& entry : std::filesystem::directory_iterator(folder)) {
					if (++inspected > 2048)
						break;
					if (!entry.is_regular_file() || entry.is_symlink() || entry.file_size() > 1024 * 1024)
						continue;
					auto extension = entry.path().extension().wstring();
					std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);
					if (extension != L".ini")
						continue;
					std::ifstream file(entry.path());
					std::string line;
					while (std::getline(file, line)) {
						const auto start = line.find_first_not_of(" \t\r");
						if (start == std::string::npos)
							continue;
						const auto key = line.substr(start, line.find('=', start) - start);
						if (key == "Techniques" || key == "TechniqueSorting") {
							const auto utf8 = entry.path().u8string();
							presets.emplace_back(reinterpret_cast<const char*>(utf8.data()), utf8.size());
							break;
						}
					}
				}
			}
			std::sort(presets.begin(), presets.end());
			scanStatus = std::format("Found {} presets in the game folder and ReShadePresets.", presets.size());
		} catch (const std::exception&) {
			scanStatus = "Some preset files could not be read. Use ReShade's own selector for other locations.";
		}
	}
}

void ExternalPostProcessing::Initialize()
{
	ScanENBPresets();
	// Register only with an already loaded, API-compatible ReShade. Never load
	// proxy DLLs, copy presets or alter ENB configuration to manufacture support.
	HMODULE modules[1024]{};
	DWORD bytes = 0;
	if (!EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &bytes))
		return;
	for (std::size_t i = 0; i < std::min<std::size_t>(bytes / sizeof(HMODULE), 1024); ++i) {
		auto registerAddon = reinterpret_cast<bool (*)(void*, uint32_t)>(GetProcAddress(modules[i], "ReShadeRegisterAddon"));
		auto registerEvent = reinterpret_cast<void (*)(reshade::addon_event, void*)>(GetProcAddress(modules[i], "ReShadeRegisterEvent"));
		if (!registerAddon || !registerEvent)
			continue;
		HMODULE self = nullptr;
		if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(&Initialize), &self))
			return;
		if (!registerAddon(self, 20)) {
			logger::warn("[PIXL ReShade] Optional camera bridge unavailable: add-on API 20 registration rejected.");
			return;
		}
		registerEvent(reshade::addon_event::reshade_present, reinterpret_cast<void*>(&OnPresent));
		registerEvent(reshade::addon_event::destroy_effect_runtime, reinterpret_cast<void*>(&OnDestroy));
		registered = true;
		logger::info("[PIXL ReShade] Optional camera preset bridge registered (API 20); live compatibility requires validation.");
		return;
	}
}

void ExternalPostProcessing::DrawSettings()
{
	const ImVec4 testOrange(0.96f, 0.68f, 0.38f, 1.0f);
	ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.32f, 0.20f, 0.10f, 0.72f));
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.45f, 0.28f, 0.13f, 0.86f));
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.52f, 0.31f, 0.14f, 0.94f));
	const bool open = ImGui::TreeNode("Experimental: External post-processing");
	ImGui::PopStyleColor(3);
	if (!open)
		return;
	ImGui::PushStyleColor(ImGuiCol_Text, testOrange);
	ImGui::TextUnformatted("Experimental workspace");
	ImGui::PopStyleColor();
	ImGui::TextWrapped("This playful compatibility space is safe to explore. Imported styles are translated into PIXL settings and can be changed back at any time.");
	ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.30f, 0.19f, 0.10f, 0.65f));
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.42f, 0.26f, 0.13f, 0.82f));
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.50f, 0.30f, 0.14f, 0.90f));
	const bool bridgeOpen = ImGui::TreeNode("PIXL ENB-Bridge");
	ImGui::PopStyleColor(3);
	if (bridgeOpen) {
	ImGui::TextWrapped("Bring a familiar ENB preset style into PIXL while keeping the original files safely in place. PIXL translates supported colour, exposure, adaptation and bloom settings without loading ENB's renderer or d3d9_smaa.dll.");
	if (ImGui::Checkbox("Use PIXL ENB-Bridge", &enbBridgeEnabled)) {
		if (!enbBridgeEnabled) enbActivePreset.clear();
	}
	if (ImGui::Button("Find installed ENB presets")) ScanENBPresets();
	ImGui::TextWrapped("%s", enbStatus.c_str());
	std::string selectedPreset = enbActivePreset;
	const auto activeLabel = enbPresetLabels.contains(enbActivePreset) ? enbPresetLabels[enbActivePreset] : enbActivePreset;
	if (ImGui::BeginCombo("Visual style", activeLabel.empty() ? "Choose a detected preset" : activeLabel.c_str())) {
		for (const auto& preset : enbPresets) {
			const auto label = enbPresetLabels.contains(preset) ? enbPresetLabels[preset] : preset;
			if (ImGui::Selectable(label.c_str(), enbActivePreset == preset)) {
				selectedPreset = preset;
				ApplyENBPreset(preset);
			}
		}
		ImGui::EndCombo();
	}
	if (selectedPreset != enbActivePreset && ImGui::Button("Apply selected visual style"))
		ApplyENBPreset(selectedPreset);
	if (!enbActivePreset.empty() && ImGui::Button("Refresh active visual style")) ApplyENBPreset(enbActivePreset);
	ImGui::TreePop();
	}

	if (ImGui::TreeNode("ReShade compatibility")) {
	ImGui::TextWrapped("Use a ReShade preset alongside PIXL's camera and lighting systems. PIXL only connects to a ReShade runtime that is already installed; it never installs or replaces one.");
	bool ready;
	bool enabled;
	std::string path;
	{
		std::lock_guard lock(stateMutex);
		ready = activeRuntime != nullptr;
		enabled = pendingEnabled.value_or(effectsEnabled);
		path = pendingPreset.empty() ? currentPreset : pendingPreset;
	}
	if (!ready)
		ImGui::TextWrapped(registered ? "Waiting for ReShade's DX11 runtime." : "Camera bridge unavailable. Install an API 20 compatible ReShade with add-on support, then restart. PIXL does not install ReShade for you.");
	ImGui::TextWrapped("For the smoothest first test, begin with SDR and keep frame generation and Neural Rendering off. Changing a preset can trigger ReShade's own shader compilation.");
	if (ImGui::Button("Find installed ReShade presets"))
		ScanPresets();
	ImGui::TextWrapped("%s", scanStatus.c_str());
	ImGui::BeginDisabled(!ready);
	if (ImGui::Checkbox("Enable ReShade effects", &enabled)) {
		std::lock_guard lock(stateMutex);
		pendingEnabled = enabled;
	}
	if (ImGui::BeginCombo("ReShade preset", path.empty() ? "Select installed preset" : path.c_str())) {
		for (const auto& preset : presets) {
			if (ImGui::Selectable(preset.c_str(), path == preset)) {
				std::lock_guard lock(stateMutex);
				pendingPreset = preset;
			}
		}
		ImGui::EndCombo();
	}
	ImGui::EndDisabled();
	ImGui::TreePop();
	}
	ImGui::TreePop();
}

void ExternalPostProcessing::DrawENBQuickStyles()
{
	InitializeQuickStyles();
	ImGui::TextWrapped("Five PIXL visual styles for quick testing and photo sessions. They capture the current PIXL result whether it came from an ENB translation or your own tuning.");
	for (std::size_t i = 0; i < quickStyles.size(); ++i) {
		ImGui::PushID(static_cast<int>(i));
		auto& style = quickStyles[i];
		const bool saved = style.valid;
		ImGui::ColorButton("##state", saved ? ImVec4(0.35f, 0.82f, 0.55f, 1.0f) : ImVec4(0.96f, 0.68f, 0.38f, 1.0f), ImGuiColorEditFlags_NoTooltip, ImVec2(10.0f, 10.0f));
		ImGui::SameLine();
		ImGui::SetNextItemWidth(160.0f);
		ImGui::InputText("##name", style.name.data(), style.name.size());
		ImGui::SameLine();
		if (ImGui::SmallButton("Apply") && saved)
			ApplyQuickStyle(style);
		ImGui::SameLine();
		if (ImGui::SmallButton(saved ? "Replace with current" : "Save current")) {
			const auto name = std::string(style.name.data()).empty() ? std::format("Style {}", i + 1) : std::string(style.name.data());
			style = CaptureCurrentStyle(name);
			enbStatus = std::format("{} saved from the current PIXL look.", style.name.data());
		}
		if (saved) {
			ImGui::SameLine();
			if (ImGui::SmallButton("Clear")) style.valid = false;
		}
		ImGui::PopID();
	}
}
