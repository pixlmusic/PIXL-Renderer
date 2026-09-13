#include "PCH.h"
#include "ExternalPostProcessing.h"
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
	if (!ImGui::TreeNode("External post-processing (experimental)"))
		return;
	ImGui::TextWrapped("ReShade presets can be selected here when a compatible ReShade add-on runtime is installed. ENB presets require ENB's renderer and cannot be selected as PIXL camera effects.");
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
	ImGui::TextWrapped("Begin testing with SDR, frame generation and Neural Rendering off. Depth effects and alternative presenters are not validated. Changing a preset can trigger ReShade's own compilation.");
	if (ImGui::Button("Refresh installed ReShade presets"))
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
