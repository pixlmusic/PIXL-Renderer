#include "RenderPresetLibrary.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <ctime>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <limits>
#include <mutex>
#include <optional>
#include <system_error>

#include <Windows.h>

namespace PIXL::Presets
{
	namespace
	{
		constexpr std::string_view kFormat = "PIXL.RenderPreset";
		constexpr int kSchemaVersion = 1;
		constexpr std::size_t kMaxName = 80;
		constexpr std::size_t kMaxAuthor = 64;
		constexpr std::size_t kMaxDescription = 768;
		std::atomic_uint64_t g_writeSequence{ 0 };
		std::mutex g_pendingPresetMutex;
		std::optional<json> g_pendingPresetSettings;

		bool IsTextField(const json& metadata, const char* key, std::size_t maxLength,
			bool required, std::string& output, std::string& error)
		{
			if (!metadata.contains(key)) {
				if (required) {
					error = std::string("Preset metadata is missing '") + key + "'.";
					return false;
				}
				output.clear();
				return true;
			}
			if (!metadata[key].is_string()) {
				error = std::string("Preset metadata '") + key + "' must be text.";
				return false;
			}
			output = metadata[key].get<std::string>();
			if (output.size() > maxLength) {
				error = std::string("Preset metadata '") + key + "' is too long.";
				return false;
			}
			for (const unsigned char value : output) {
				if (value < 0x20 && value != '\n' && value != '\r' && value != '\t') {
					error = std::string("Preset metadata '") + key + "' contains a control character.";
					return false;
				}
			}
			if (required && output.empty()) {
				error = std::string("Preset metadata '") + key + "' cannot be empty.";
				return false;
			}
			return true;
		}

		bool ValidateSettings(const json& settings, std::string& error)
		{
			if (!settings.is_object() || settings.empty()) {
				error = "Preset settings must be a non-empty JSON object.";
				return false;
			}
			if (settings.contains("Menu") || settings.contains("Advanced") || settings.contains("Version")) {
				error = "Preset contains personal, developer or version settings that are not shareable.";
				return false;
			}
			if (settings.contains("General")) {
				const auto& general = settings["General"];
				if (!general.is_object()) {
					error = "Preset General settings must be an object.";
					return false;
				}
				for (const auto& [key, _] : general.items()) {
					if (key != "Enable Shaders") {
						error = "Preset contains non-renderer General settings.";
						return false;
					}
				}
			}
			if (settings.contains("Disable at Boot") && !settings["Disable at Boot"].is_object()) {
				error = "Preset module enablement settings must be an object.";
				return false;
			}
			if (settings.contains("Replace Original Shaders") && !settings["Replace Original Shaders"].is_object()) {
				error = "Preset shader-class settings must be an object.";
				return false;
			}
			if (settings.contains("Pixel Capture") && settings["Pixel Capture"].is_object() &&
				settings["Pixel Capture"].contains("ScreenshotPath")) {
				error = "Preset contains a machine-local screenshot destination.";
				return false;
			}
			return true;
		}

		bool CompatibleType(const json& target, const json& incoming)
		{
			if (target.is_number() && incoming.is_number())
				return true;
			if (target.is_object() && incoming.is_object())
				return true;
			if (target.is_array() && incoming.is_array())
				return true;
			return target.type() == incoming.type();
		}

		void MergeValue(json& target, const json& incoming, MergeSummary& summary)
		{
			if (target.is_object() && incoming.is_object()) {
				for (const auto& [key, value] : incoming.items()) {
					auto found = target.find(key);
					if (found == target.end()) {
						++summary.unsupported;
						continue;
					}
					MergeValue(*found, value, summary);
				}
				return;
			}
			if (!CompatibleType(target, incoming)) {
				++summary.typeMismatches;
				return;
			}
			if (target.is_array()) {
				if (target.size() != incoming.size()) {
					++summary.typeMismatches;
					return;
				}
				json candidate = target;
				const auto oldMismatchCount = summary.typeMismatches;
				for (std::size_t index = 0; index < incoming.size(); ++index)
					MergeValue(candidate[index], incoming[index], summary);
				if (summary.typeMismatches != oldMismatchCount) {
					return;
				}
				target = std::move(candidate);
				return;
			}
			target = incoming;
			++summary.applied;
		}

		bool WriteAtomically(const std::filesystem::path& path, std::string_view contents,
			std::string& error, bool replaceExisting, bool* targetExists = nullptr)
		{
			if (targetExists)
				*targetExists = false;
			static_assert(sizeof(wchar_t) == 2);
			auto temporary = path;
			temporary += L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." +
				std::to_wstring(g_writeSequence.fetch_add(1, std::memory_order_relaxed));
			try {
				std::filesystem::create_directories(path.parent_path());
				{
					std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
					if (!output.is_open()) {
						error = "Could not create the temporary preset file.";
						return false;
					}
					output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
					output.flush();
					if (!output.good()) {
						error = "Could not finish writing the preset file.";
						output.close();
						std::filesystem::remove(temporary);
						return false;
					}
				}
				const DWORD moveFlags = MOVEFILE_WRITE_THROUGH |
					(replaceExisting ? MOVEFILE_REPLACE_EXISTING : 0);
				if (!MoveFileExW(temporary.c_str(), path.c_str(), moveFlags)) {
					const DWORD code = GetLastError();
					if (targetExists && (code == ERROR_FILE_EXISTS || code == ERROR_ALREADY_EXISTS))
						*targetExists = true;
					error = "Could not publish the file (Windows error " + std::to_string(code) + ").";
					std::filesystem::remove(temporary);
					return false;
				}
				return true;
			} catch (const std::exception& exception) {
				error = exception.what();
				std::error_code ignored;
				std::filesystem::remove(temporary, ignored);
				return false;
			}
		}

		std::string Timestamp()
		{
			const auto now = std::chrono::system_clock::now();
			const auto value = std::chrono::system_clock::to_time_t(now);
			std::tm localTime{};
			localtime_s(&localTime, &value);
			char buffer[24]{};
			std::strftime(buffer, sizeof(buffer), "%Y%m%d-%H%M%S", &localTime);
			return buffer;
		}

		bool IsJsonFile(const std::filesystem::path& path)
		{
			auto extension = path.extension().wstring();
			std::transform(extension.begin(), extension.end(), extension.begin(),
				[](wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
			return extension == L".json";
		}
	}

	json CaptureRendererSettings(const json& completeSettings)
	{
		if (!completeSettings.is_object())
			return json::object();
		auto output = completeSettings;
		output.erase("Menu");
		output.erase("Advanced");
		output.erase("Version");
		// A screenshot destination is a machine-local path, not a renderer look.
		// Keep the recipient's current destination when compatible settings merge.
		if (output.contains("Pixel Capture") && output["Pixel Capture"].is_object())
			output["Pixel Capture"].erase("ScreenshotPath");
		if (output.contains("General")) {
			json rendererGeneral = json::object();
			const auto& general = output["General"];
			if (general.is_object() && general.contains("Enable Shaders") && general["Enable Shaders"].is_boolean())
				rendererGeneral["Enable Shaders"] = general["Enable Shaders"];
			if (rendererGeneral.empty())
				output.erase("General");
			else
				output["General"] = std::move(rendererGeneral);
		}
		return output;
	}

	json CreateDocument(std::string name, std::string author,
		std::string description, std::string rendererVersion,
		const json& completeSettings)
	{
		return json{
			{ "format", kFormat },
			{ "schemaVersion", kSchemaVersion },
			{ "rendererVersion", std::move(rendererVersion) },
			{ "metadata", {
				{ "name", std::move(name) },
				{ "author", std::move(author) },
				{ "description", std::move(description) }
			} },
			{ "settings", CaptureRendererSettings(completeSettings) }
		};
	}

	bool ParseDocument(const json& source, const std::filesystem::path& sourcePath,
		RenderPreset& output, std::string& error)
	{
		output = {};
		RenderPreset candidate;
		if (!source.is_object()) {
			error = "Preset root must be a JSON object.";
			return false;
		}

		if (source.contains("format") &&
			(!source["format"].is_string() || source["format"].get<std::string>() != kFormat)) {
			error = "Preset format is not supported.";
			return false;
		}
		if (source.contains("format")) {
			if (!source.contains("schemaVersion") || !source["schemaVersion"].is_number_integer() ||
				source["schemaVersion"].get<int>() != kSchemaVersion) {
				error = "This PIXL preset uses an unsupported schema version.";
				return false;
			}
			if (!source.contains("metadata") || !source["metadata"].is_object()) {
				error = "Preset metadata is missing or invalid.";
				return false;
			}
			const auto& metadata = source["metadata"];
			if (!IsTextField(metadata, "name", kMaxName, true, candidate.name, error) ||
				!IsTextField(metadata, "author", kMaxAuthor, false, candidate.author, error) ||
				!IsTextField(metadata, "description", kMaxDescription, false, candidate.description, error))
				return false;
			if (source.contains("rendererVersion")) {
				if (!source["rendererVersion"].is_string() || source["rendererVersion"].get<std::string>().size() > 32) {
					error = "Preset rendererVersion is invalid.";
					return false;
				}
				candidate.rendererVersion = source["rendererVersion"].get<std::string>();
			}
			if (!source.contains("settings")) {
				error = "Preset settings are missing.";
				return false;
			}
			candidate.settings = source["settings"];
		} else {
			// Older shipped preset files are complete PIXL settings snapshots. Read
			// them as renderer presets, but never import their UI/compiler state.
			if (!source.contains("Version") || !source["Version"].is_string()) {
				error = "JSON file is neither a PIXL preset nor a legacy PIXL settings snapshot.";
				return false;
			}
			candidate.name = sourcePath.stem().string();
			constexpr std::string_view legacySuffix = ".pixlpreset";
			if (candidate.name.ends_with(legacySuffix))
				candidate.name.resize(candidate.name.size() - legacySuffix.size());
			candidate.author = "PIXL legacy profile";
			candidate.description = "A renderer profile from an earlier PIXL settings snapshot.";
			candidate.rendererVersion = source["Version"].get<std::string>();
			candidate.settings = CaptureRendererSettings(source);
			candidate.legacy = true;
		}

		if (!ValidateSettings(candidate.settings, error))
			return false;
		candidate.filePath = sourcePath;
		output = std::move(candidate);
		return true;
	}

	json MergeCompatibleSettings(const json& currentSettings,
		const json& presetSettings, MergeSummary& summary)
	{
		summary = {};
		if (!currentSettings.is_object() || !presetSettings.is_object()) {
			++summary.typeMismatches;
			return currentSettings;
		}
		json merged = currentSettings;
		MergeValue(merged, presetSettings, summary);
		return merged;
	}

	std::vector<RenderPreset> Discover(const std::filesystem::path& root,
		std::size_t maxPresets, std::uintmax_t maxFileBytes)
	{
		std::vector<RenderPreset> presets;
		std::error_code error;
		if (!std::filesystem::is_directory(root, error) || error || maxPresets == 0)
			return presets;

		const auto presetLimit = std::min<std::size_t>(maxPresets, 512);
		std::size_t examined = 0;
		std::size_t visited = 0;
		const auto maxVisited = presetLimit * 16;
		for (std::filesystem::recursive_directory_iterator it(root,
				std::filesystem::directory_options::skip_permission_denied, error), end;
			!error && it != end && presets.size() < presetLimit && examined < presetLimit * 4 && visited < maxVisited;
			it.increment(error)) {
			++visited;
			if (it.depth() >= 4 && it->is_directory(error)) {
				it.disable_recursion_pending();
				continue;
			}
			if (error) {
				error.clear();
				continue;
			}
			if (!it->is_regular_file(error) || error || !IsJsonFile(it->path())) {
				error.clear();
				continue;
			}
			if (it->is_symlink(error) || error) {
				error.clear();
				continue;
			}
			++examined;
			const auto bytes = it->file_size(error);
			if (error || bytes == 0 || bytes > maxFileBytes) {
				error.clear();
				continue;
			}
			try {
				std::ifstream input(it->path(), std::ios::binary);
				if (!input.is_open())
					continue;
				json source;
				input >> source;
				RenderPreset preset;
				std::string parseError;
				if (ParseDocument(source, it->path(), preset, parseError))
					presets.push_back(std::move(preset));
			} catch (const std::exception&) {
				// One malformed or partially downloaded file must not hide other presets.
			}
		}
		std::sort(presets.begin(), presets.end(), [](const RenderPreset& lhs, const RenderPreset& rhs) {
			std::string left = lhs.name, right = rhs.name;
			std::transform(left.begin(), left.end(), left.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			std::transform(right.begin(), right.end(), right.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return left < right;
		});
		return presets;
	}

	std::string MakeFileStem(std::string_view name)
	{
		std::string stem;
		stem.reserve(name.size());
		bool previousSeparator = false;
		for (const unsigned char value : name) {
			if (std::isalnum(value) || value == '-' || value == '_' || value == '.') {
				stem.push_back(static_cast<char>(value));
				previousSeparator = false;
			} else if (std::isspace(value) && !stem.empty() && !previousSeparator) {
				stem.push_back('-');
				previousSeparator = true;
			}
			if (stem.size() >= kMaxName)
				break;
		}
		while (!stem.empty() && (stem.back() == '.' || stem.back() == '-' || stem.back() == '_'))
			stem.pop_back();
		while (!stem.empty() && stem.front() == '.')
			stem.erase(stem.begin());
		if (stem.empty() || stem == "..")
			stem = "PIXL-Preset";
		std::string device = stem;
		std::transform(device.begin(), device.end(), device.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
		const auto dot = device.find('.');
		if (dot != std::string::npos)
			device.resize(dot);
		static constexpr std::string_view reserved[]{ "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9" };
		if (std::find(std::begin(reserved), std::end(reserved), device) != std::end(reserved))
			stem.insert(stem.begin(), '_');
		return stem;
	}

	bool SaveNewPreset(const std::filesystem::path& directory, const json& document,
		std::filesystem::path& savedPath, std::string& error)
	{
		RenderPreset parsed;
		if (!ParseDocument(document, {}, parsed, error))
			return false;
		const auto stem = MakeFileStem(parsed.name);
		if (stem.empty()) {
			error = "Choose a preset name containing letters or numbers.";
			return false;
		}
		try {
			std::filesystem::create_directories(directory);
			for (unsigned int suffix = 1; suffix <= 1000; ++suffix) {
				const auto candidateStem = suffix == 1 ? stem : stem + "-" + std::to_string(suffix);
				auto candidate = directory / (candidateStem + ".pixlpreset.json");
				const auto serialized = document.dump(2);
				bool targetExists = false;
				if (!WriteAtomically(candidate, serialized, error, false, &targetExists)) {
					if (targetExists)
						continue;
					return false;
				}
				savedPath = std::move(candidate);
				return true;
			}
			error = "Could not find an unused filename for this preset.";
			return false;
		} catch (const std::exception& exception) {
			error = exception.what();
			return false;
		}
	}

	ApplyResult ApplyToUserConfig(const std::filesystem::path& userConfigPath,
		const json& currentSettings, const RenderPreset& preset)
	{
		ApplyResult result;
		std::string validationError;
		if (!ValidateSettings(preset.settings, validationError)) {
			result.error = std::move(validationError);
			return result;
		}
		result.settings = MergeCompatibleSettings(currentSettings, preset.settings, result.merge);
		if (result.merge.applied == 0) {
			result.error = "This preset contains no settings compatible with the installed renderer.";
			return result;
		}

		try {
			std::filesystem::create_directories(userConfigPath.parent_path());
			if (std::filesystem::exists(userConfigPath)) {
				auto backup = userConfigPath;
				backup += ".bak." + Timestamp();
				for (unsigned int suffix = 2; std::filesystem::exists(backup); ++suffix) {
					backup = userConfigPath;
					backup += ".bak." + Timestamp() + "-" + std::to_string(suffix);
				}
				std::filesystem::copy_file(userConfigPath, backup, std::filesystem::copy_options::none);
				result.backupPath = std::move(backup);
			}
			const auto serialized = result.settings.dump(1);
			if (!WriteAtomically(userConfigPath, serialized, result.error, true))
				return result;
			result.success = true;
			return result;
		} catch (const std::exception& exception) {
			result.error = exception.what();
			return result;
		}
	}

	void StagePendingRendererSettings(const json& presetSettings)
	{
		std::lock_guard lock(g_pendingPresetMutex);
		g_pendingPresetSettings = CaptureRendererSettings(presetSettings);
	}

	void MergePendingRendererSettings(json& completeSettings)
	{
		std::lock_guard lock(g_pendingPresetMutex);
		if (!g_pendingPresetSettings || !completeSettings.is_object())
			return;
		MergeSummary ignored;
		completeSettings = MergeCompatibleSettings(completeSettings, *g_pendingPresetSettings, ignored);
	}

	void ClearPendingRendererSettings()
	{
		std::lock_guard lock(g_pendingPresetMutex);
		g_pendingPresetSettings.reset();
	}
}
