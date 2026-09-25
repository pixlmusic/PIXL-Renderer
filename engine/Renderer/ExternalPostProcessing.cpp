#include "PCH.h"
#include "ExternalPostProcessing.h"
#include "../Globals.h"
#include "../State.h"
#include "../Modules/CameraSuite.h"
#include "../../extern/ReShade/include/reshade_events.hpp"
#include <psapi.h>
#include <fstream>
#include <mutex>
#include <unordered_set>

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
	struct ENBPreset
	{
		std::string path;
		std::string label;
		std::filesystem::path effectConfig;
		bool hasPalette = false;
		bool hasAnnotatedEffect = false;
		bool usesWeatherSeparatedParameters = false;
	};

	bool enbBridgeEnabled = false;
	std::vector<ENBPreset> enbPresets;
	std::string enbActivePreset;
	std::string enbSelectedPreset;
	std::string enbStatus;
	std::optional<QuickStyle> enbImportBaseline;
	std::array<QuickStyle, 5> quickStyles{};
	bool quickStylesInitialized = false;

	QuickStyle CaptureCurrentStyle(std::string_view defaultName);
	void ApplyQuickStyle(const QuickStyle& style);

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

	constexpr std::uintmax_t kMaxExternalPresetFileBytes = 4u * 1024u * 1024u;
	constexpr std::size_t kMaxExternalPresetCandidates = 512;
	constexpr std::size_t kMaxExternalPresetDirectories = 8192;
	constexpr std::size_t kMaxExternalPresetDepth = 7;

	bool IsSafeExternalPresetFile(const std::filesystem::path& file)
	{
		std::error_code ec;
		if (!std::filesystem::is_regular_file(file, ec) || std::filesystem::is_symlink(file, ec))
			return false;
		const auto size = std::filesystem::file_size(file, ec);
		return !ec && size <= kMaxExternalPresetFileBytes;
	}

	bool EqualsInsensitive(std::wstring_view lhs, std::wstring_view rhs)
	{
		return lhs.size() == rhs.size() &&
			CompareStringOrdinal(lhs.data(), static_cast<int>(lhs.size()), rhs.data(), static_cast<int>(rhs.size()), TRUE) == CSTR_EQUAL;
	}

	bool IsIgnoredPresetDirectory(std::wstring_view name)
	{
		// These roots are large game payloads or volatile output; presets belong
		// beside the game or in a user-created preset folder, never inside them.
		for (const auto& ignored : { L"Data", L"Creations", L"Downloads", L"Screenshots", L"Cache", L"Logs", L"build", L"bin", L".git" }) {
			if (EqualsInsensitive(name, ignored))
				return true;
		}
		return false;
	}

	std::string ToUtf8(const std::filesystem::path& path)
	{
		const auto value = path.u8string();
		return { reinterpret_cast<const char*>(value.data()), value.size() };
	}

	std::string DisplayPresetPath(const std::filesystem::path& root, const std::filesystem::path& file)
	{
		std::error_code ec;
		auto relative = std::filesystem::relative(file.parent_path(), root, ec);
		if (ec || relative.empty() || relative == ".")
			return "Game root";
		return ToUtf8(relative);
	}

	std::filesystem::path FindENBEffectShader(const std::filesystem::path& preset)
	{
		for (const auto& candidate : { preset.parent_path() / "enbseries" / "enbeffect.fx", preset.parent_path() / "enbeffect.fx" }) {
			if (IsSafeExternalPresetFile(candidate))
				return candidate;
		}
		return {};
	}

	void InspectEffectAnnotations(const std::filesystem::path& preset, ENBPreset& result)
	{
		const auto shader = FindENBEffectShader(preset);
		if (shader.empty())
			return;
		std::ifstream stream(shader, std::ios::binary);
		if (!stream)
			return;
		std::string source((std::istreambuf_iterator<char>(stream)), {});
		// These tags belong to the original effect runtime. PIXL uses them only
		// as capability metadata; no foreign HLSL is compiled or executed here.
		result.hasAnnotatedEffect = source.find("UIName") != std::string::npos || source.find("UIGroup") != std::string::npos;
		result.usesWeatherSeparatedParameters = source.find("Separation") != std::string::npos;
	}

	std::filesystem::path FindENBEffectConfig(const std::filesystem::path& preset)
	{
		for (const auto& candidate : { preset.parent_path() / "enbseries" / "enbeffect.fx.ini", preset.parent_path() / "enbeffect.fx.ini" }) {
			if (IsSafeExternalPresetFile(candidate))
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
		enbSelectedPreset.clear();
		wchar_t executable[32768]{};
		const auto length = GetModuleFileNameW(nullptr, executable, 32768);
		if (!length || length >= 32768) { enbStatus = "Could not locate the game folder."; return; }
		const auto root = std::filesystem::path(executable).parent_path();
		std::unordered_set<std::wstring> seen;
		std::error_code ec;
		std::size_t inspectedDirectories = 0;
		for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec), end;
			it != end && enbPresets.size() < kMaxExternalPresetCandidates;
			it.increment(ec)) {
			if (ec) {
				ec.clear();
				continue;
			}
			const auto& entry = *it;
			if (entry.is_symlink(ec)) {
				if (entry.is_directory(ec))
					it.disable_recursion_pending();
				continue;
			}
			if (entry.is_directory(ec)) {
				if (++inspectedDirectories > kMaxExternalPresetDirectories || it.depth() >= kMaxExternalPresetDepth || IsIgnoredPresetDirectory(entry.path().filename().wstring()))
					it.disable_recursion_pending();
				continue;
			}
			if (!entry.is_regular_file(ec) || !EqualsInsensitive(entry.path().filename().wstring(), L"enbseries.ini") || !IsSafeExternalPresetFile(entry.path()))
				continue;

			auto canonical = std::filesystem::weakly_canonical(entry.path(), ec);
			if (ec) {
				ec.clear();
				continue;
			}
			const auto canonicalKey = canonical.wstring();
			if (!seen.insert(canonicalKey).second)
				continue;

			ENBPreset preset;
			preset.path = ToUtf8(canonical);
			preset.label = DisplayPresetPath(root, canonical);
			preset.effectConfig = FindENBEffectConfig(canonical);
			preset.hasPalette = HasENBPalette(canonical);
			InspectEffectAnnotations(canonical, preset);
			enbPresets.emplace_back(std::move(preset));
		}
		std::sort(enbPresets.begin(), enbPresets.end(), [](const auto& lhs, const auto& rhs) { return lhs.label < rhs.label; });
		if (!enbActivePreset.empty())
			enbSelectedPreset = enbActivePreset;
		enbStatus = std::format("Found {} preset{} in the game folder. Scan is bounded to {} files and ignores game data, caches and links.",
			enbPresets.size(), enbPresets.size() == 1 ? "" : "s", kMaxExternalPresetCandidates);
	}

	void ApplyENBPreset(const ENBPreset& preset)
	{
		const auto file = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(preset.path.data()), preset.path.size()));
		if (!IsSafeExternalPresetFile(file)) {
			enbStatus = "The selected preset is no longer available. Refresh the library before trying again.";
			return;
		}
		if (!enbImportBaseline)
			enbImportBaseline = CaptureCurrentStyle("Before imported visual style");
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
		const auto effectConfig = preset.effectConfig.empty() ? FindENBEffectConfig(file) : preset.effectConfig;
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
		if (preset.hasPalette)
			toneSource += "; palette asset detected (translated safely)";
		globals::pipeline::cameraSuite.ApplyExternalLook(exposureEV, contrast, saturation, adaptation, highlightProtection, shadowDetail, toe, shoulder, bloomEnabled, bloomStrength, lookPreset, lookOpacity);
		globals::pipeline::cameraSuite.LoadLookTexture();
		globals::pipeline::cameraSuite.UpdateHDRData();
		if (globals::state)
			globals::state->Save();
		enbActivePreset = preset.path;
		enbSelectedPreset = preset.path;
		enbBridgeEnabled = true;
		enbStatus = std::format("{} is translated into PIXL. {}. Grade {:.0f}%, exposure {:+.2f} EV, contrast {:.2f}, saturation {:.2f}, bloom {}.", preset.label, toneSource, lookOpacity * 100.0f, exposureEV, contrast, saturation, bloomEnabled ? "on" : "off");
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
		enbSelectedPreset.clear();
		enbImportBaseline.reset();
		enbBridgeEnabled = false;
		enbStatus = std::format("{} is active as a PIXL visual style.", style.name.data());
	}

	void RestoreENBImportBaseline()
	{
		if (!enbImportBaseline)
			return;
		const auto baseline = *enbImportBaseline;
		ApplyQuickStyle(baseline);
		if (globals::state)
			globals::state->Save();
		enbStatus = "Restored the PIXL camera state from before the imported visual style.";
	}

	void InitializeQuickStyles()
	{
		if (quickStylesInitialized)
			return;
		quickStylesInitialized = true;
		const auto base = CaptureCurrentStyle("Custom Style");
		quickStyles = { base, base, base, base, base };
	// Gem names keep the quick styles original, neutral, and easy to scan.
	const std::array<std::string_view, 5> names{
		"Obsidian",
		"Amethyst",
		"Citrine",
		"Moonstone",
		"Labradorite"
	};
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
	// The preset library can walk a meaningful part of the game install, so it
	// is intentionally user-triggered from the experimental panel rather than
	// adding filesystem work to every startup.
	enbStatus = "Refresh the game-folder library to discover installed preset configurations.";
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
		ImGui::TextWrapped("Import a familiar preset response into PIXL without loading an external renderer. Supported exposure, contrast, colour, adaptation and bloom values are mapped through Camera Suite; original files are read-only.");
		if (ImGui::Button("Refresh game-folder preset library"))
			ScanENBPresets();
		ImGui::TextWrapped("%s", enbStatus.c_str());
		ImGui::TextDisabled("The scan follows preset folders under the Skyrim install, rejects links and oversized files, and never scans your drives.");

		const ENBPreset* selected = nullptr;
		for (const auto& preset : enbPresets) {
			if (preset.path == enbSelectedPreset) {
				selected = &preset;
				break;
			}
		}
		const char* preview = selected ? selected->label.c_str() : "Choose a detected preset";
		if (ImGui::BeginCombo("Detected preset", preview)) {
			for (const auto& preset : enbPresets) {
				ImGui::PushID(preset.path.c_str());
				const bool isSelected = preset.path == enbSelectedPreset;
				if (ImGui::Selectable(preset.label.c_str(), isSelected))
					enbSelectedPreset = preset.path;
				if (ImGui::IsItemHovered()) {
					ImGui::BeginTooltip();
					ImGui::TextUnformatted(preset.path.c_str());
					ImGui::Separator();
					ImGui::TextUnformatted(preset.effectConfig.empty() ? "Base configuration only" : "Effect configuration found");
					if (preset.hasPalette) ImGui::TextUnformatted("Palette/LUT asset found");
					if (preset.hasAnnotatedEffect) ImGui::TextUnformatted("Effect UI annotations found");
					if (preset.usesWeatherSeparatedParameters) ImGui::TextUnformatted("Weather-separated parameters found; PIXL uses the base/day values it supports.");
					ImGui::EndTooltip();
				}
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
		if (selected) {
			ImGui::TextDisabled("%s%s%s", selected->effectConfig.empty() ? "Base settings" : "Base + effect settings", selected->hasPalette ? "  |  palette asset" : "", selected->hasAnnotatedEffect ? "  |  annotated controls" : "");
			if (selected->usesWeatherSeparatedParameters)
				ImGui::TextWrapped("This preset declares weather-separated controls. PIXL preserves its own weather model and imports only stable base/day camera values.");
		}
		ImGui::BeginDisabled(selected == nullptr);
		if (ImGui::Button("Apply translated PIXL style"))
			ApplyENBPreset(*selected);
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(enbActivePreset.empty());
		if (ImGui::Button("Refresh active style")) {
			for (const auto& preset : enbPresets) {
				if (preset.path == enbActivePreset) {
					ApplyENBPreset(preset);
					break;
				}
			}
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(!enbImportBaseline.has_value());
		if (ImGui::Button("Restore before import"))
			RestoreENBImportBaseline();
		ImGui::EndDisabled();
		ImGui::TextDisabled("PIXL never executes imported effect shaders, loads an external renderer, or writes into a preset folder.");
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
		if (ImGui::TreeNodeEx("##style", ImGuiTreeNodeFlags_None, "%s%s", style.name.data(), saved ? "" : " (empty)")) {
			ImGui::SetNextItemWidth(-FLT_MIN);
			ImGui::InputTextWithHint("##name", "Style name", style.name.data(), style.name.size());
			ImGui::BeginDisabled(!saved);
			if (ImGui::Button("Apply"))
				ApplyQuickStyle(style);
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button(saved ? "Replace with current" : "Save current")) {
				const auto name = std::string(style.name.data()).empty() ? std::format("Style {}", i + 1) : std::string(style.name.data());
				style = CaptureCurrentStyle(name);
				enbStatus = std::format("{} saved from the current PIXL look.", style.name.data());
			}
			if (saved && ImGui::Button("Clear snapshot"))
				style.valid = false;
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
}

bool ExternalPostProcessing::DrawENBQuickStylePalette()
{
	InitializeQuickStyles();
	bool changed = false;

	const float gap = ImGui::GetStyle().ItemSpacing.x;
	const float buttonWidth = std::max(1.0f, (ImGui::GetContentRegionAvail().x - gap) * 0.5f);
	for (std::size_t i = 0; i < quickStyles.size(); ++i) {
		ImGui::PushID(static_cast<int>(i));
		auto& style = quickStyles[i];
		const bool canApply = style.valid;
		ImGui::BeginDisabled(!canApply);
		if (ImGui::Button(style.name.data(), ImVec2(buttonWidth, 0.0f))) {
			ApplyQuickStyle(style);
			changed = true;
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip(canApply ? "Apply this PIXL visual style. It is a starting look; every camera setting remains adjustable." : "This style has not been saved yet.");
		if ((i & 1u) == 0u && i + 1u < quickStyles.size())
			ImGui::SameLine(0.0f, gap);
		ImGui::PopID();
	}
	return changed;
}
