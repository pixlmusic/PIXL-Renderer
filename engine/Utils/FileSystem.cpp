#include "FileSystem.h"

#include <array>
#include <Windows.h>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <psapi.h>

namespace Util
{
	// Path helper utilities implementation
	namespace PathHelpers
	{
		std::filesystem::path GetDataPath()
		{
			try {
				// Get the current process (game) executable path
				wchar_t buffer[MAX_PATH];
				DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
				if (length == 0 || length == MAX_PATH) {
					throw std::runtime_error("Failed to get module filename");
				}

				auto executablePath = std::filesystem::path(buffer);

				auto gamePath = executablePath.parent_path();
				auto dataPath = gamePath / "Data";
				return dataPath;
			} catch (const std::exception& e) {
				// Fallback to current_path if Windows API method fails
				logger::warn("Failed to get game path via Windows API, falling back to current_path: {}", e.what());
				return std::filesystem::current_path() / "Data";
			}
		}

		std::filesystem::path GetPluginPath()
		{
			return GetDataPath() / "SKSE" / "Plugins" / "PIXL";
		}

		void MigrateLegacyPluginState()
		{
			const auto plugins = GetDataPath() / "SKSE" / "Plugins";
			const auto current = GetPluginPath();
			const auto retiredHostDirectory = std::string("Community") + "Shaders";
			const std::array oldRoots{ plugins / "PIXLRenderer", plugins / retiredHostDirectory };
			const std::array mappings{
				std::pair{ "SettingsUser.json", "Config/UserGraphics.json" },
				std::pair{ "SettingsTest.json", "Config/ValidationGraphics.json" },
				std::pair{ "SettingsTheme.json", "Config/Interface.json" },
				std::pair{ "AppliedOverrides.json", "Config/AppliedOverrides.json" },
				std::pair{ "Themes", "Interface/Themes" },
				std::pair{ "Translations", "Interface/Locale" },
				std::pair{ "SceneSettings", "World/Scenes" },
				std::pair{ "Weathers", "World/Weather" },
				std::pair{ "WaterbodyCache", "World/Water" },
				std::pair{ "UWLoadOrder.hash", "World/Water/LoadOrder.hash" },
				std::pair{ "Overrides", "Overrides" }
			};
			std::error_code ec;
			for (const auto& oldRoot : oldRoots) {
				if (!std::filesystem::is_directory(oldRoot, ec))
					continue;
				for (const auto& [oldName, newName] : mappings) {
					const auto source = oldRoot / oldName;
					const auto destination = current / newName;
					ec.clear();
					if (!std::filesystem::exists(source, ec) || std::filesystem::exists(destination, ec))
						continue;
					std::filesystem::create_directories(destination.parent_path(), ec);
					if (!ec)
						std::filesystem::copy(source, destination,
							std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing, ec);
					if (ec)
						logger::warn("PIXL state migration skipped '{}': {}", source.string(), ec.message());
					else
						logger::info("Migrated PIXL state into {}", newName);
				}
			}
		}

		std::filesystem::path GetDiagnosticsPath()
		{
			return GetPluginPath() / "Diagnostics" / "Captures";
		}

		std::filesystem::path GetImGuiIniPath()
		{
			return GetPluginPath() / "Interface" / "Layout.ini";
		}

		std::filesystem::path GetInterfacePath()
		{
			return GetPluginPath() / "Interface";
		}

		std::filesystem::path GetFontsPath()
		{
			return GetInterfacePath() / "Fonts";
		}

		std::filesystem::path GetIconsPath()
		{
			return GetInterfacePath() / "Visuals";
		}

		std::filesystem::path GetCursorsPath()
		{
			return GetInterfacePath() / "Cursors";
		}

		std::filesystem::path GetSettingsUserPath()
		{
			return GetPluginPath() / "Config" / "UserGraphics.json";
		}

		std::filesystem::path GetSettingsTestPath()
		{
			return GetPluginPath() / "Config" / "ValidationGraphics.json";
		}

		std::filesystem::path GetSettingsDefaultPath()
		{
			return GetPluginPath() / "Config" / "RendererDefaults.json";
		}

		std::filesystem::path GetSettingsThemePath()
		{
			return GetPluginPath() / "Config" / "Interface.json";
		}

		std::filesystem::path GetThemesPath()
		{
			return GetPluginPath() / "Interface" / "Themes";
		}

		std::filesystem::path GetTranslationsPath()
		{
			return GetPluginPath() / "Interface" / "Locale";
		}

		std::filesystem::path GetOverridesPath()
		{
			return GetPluginPath() / "Overrides";
		}

		std::filesystem::path GetUserOverridesPath()
		{
			return GetOverridesPath() / "User";
		}

		std::filesystem::path GetAppliedOverridesPath()
		{
			return GetPluginPath() / "Config" / "AppliedOverrides.json";
		}

		std::filesystem::path GetSceneSettingsPath()
		{
			return GetPluginPath() / "World" / "Scenes";
		}

		std::filesystem::path GetWaterbodyCachePath()
		{
			return GetPluginPath() / "World" / "Water";
		}

		std::filesystem::path GetShadersPath()
		{
			return GetDataPath() / "Shaders";
		}

		std::filesystem::path GetModuleCatalogPath()
		{
			return GetShadersPath() / "PIXL" / "Modules";
		}

		std::filesystem::path GetCurrentModuleRealPath()
		{
			try {
				HMODULE selfModule = nullptr;
				if (!GetModuleHandleExW(
						GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
						reinterpret_cast<LPCWSTR>(&GetCurrentModuleRealPath),
						&selfModule)) {
					return {};
				}
				wchar_t buffer[MAX_PATH]{};
				DWORD size = GetModuleFileNameExW(GetCurrentProcess(), selfModule, buffer, MAX_PATH);
				if (size == 0 || size == MAX_PATH) {
					throw std::runtime_error("Failed to get module filename");
				}
				return std::filesystem::path(buffer);
			} catch (const std::exception& e) {
				logger::error("GetCurrentModuleRealPath: Exception caught: {}", e.what());
				return {};
			}
		}

		std::filesystem::path GetRootRealPath()
		{
			static std::filesystem::path cachedPath = []() {
				std::filesystem::path dllPath = GetCurrentModuleRealPath();
				if (dllPath.empty())
					return std::filesystem::path{};
				return dllPath.parent_path().parent_path().parent_path();
			}();
			return cachedPath;
		}

		std::filesystem::path GetShadersRealPath()
		{
			return GetRootRealPath() / "Shaders";
		}

		std::filesystem::path GetThemesRealPath()
		{
			return GetRootRealPath() / "SKSE" / "Plugins" / "PIXL" / "Interface" / "Themes";
		}

		std::filesystem::path GetModuleCatalogRealPath()
		{
			return GetShadersRealPath() / "PIXL" / "Modules";
		}

		std::filesystem::path GetRealPathFromDataRelative(const std::filesystem::path& dataRelativePath)
		{
			std::filesystem::path root = GetRootRealPath();
			if (root.empty())
				return {};
			auto it = dataRelativePath.begin();
			// Skip leading "Data" — MO2 maps the game's Data directory to the mod's physical root
			if (it != dataRelativePath.end() && _wcsicmp(it->c_str(), L"Data") == 0)
				++it;
			for (; it != dataRelativePath.end(); ++it)
				root /= *it;
			return root;
		}

		std::filesystem::path GetModuleDescriptorPath(const std::string& moduleId)
		{
			return GetModuleCatalogPath() / (moduleId + ".ini");
		}

		std::filesystem::path GetModuleKernelPath(const std::string& moduleId)
		{
			return GetShadersPath() / moduleId;
		}

		std::filesystem::path GetLogPath()
		{
			auto path = logger::log_directory();
			if (!path) {
				return {};
			}
			*path /= std::format("{}.log", std::string(Plugin::NAME));
			return *path;
		}
	}

	// File system utilities implementation
	namespace FileHelpers
	{
		DeletionResult SafeDelete(const std::string& path, const std::string& description)
		{
			DeletionResult result;
			result.deletedDescription = description + ": " + path;

			try {
				if (path.empty() || !std::filesystem::exists(path)) {
					result.success = true;  // Non-existent files are already gone.
					return result;
				}
				if (std::filesystem::is_directory(path)) {
					std::filesystem::remove_all(path);
				} else {
					std::filesystem::remove(path);
				}
				result.success = true;
				logger::info("Deleted {}: {}", description, path);
			} catch (const std::filesystem::filesystem_error& e) {
				result.success = false;
				result.errorMessage = e.what();
				logger::error("Failed to delete {}: {} - {}", description, path, e.what());
			}

			return result;
		}

		void EnsureDirectoryExists(const std::filesystem::path& path)
		{
			std::error_code ec;
			std::filesystem::create_directories(path, ec);
			if (ec) {
				logger::warn("Failed to create directory '{}': {}", path.string(), ec.message());
			}
		}

		std::string SanitizeFileName(std::string name)
		{
			// Trim
			constexpr std::string_view trimLeadingChars = " \t\r\n\v\f-";
			auto first = name.find_first_not_of(trimLeadingChars);
			if (first == std::string::npos)
				return "";
			constexpr std::string_view trimTrailingChars = " \t\r\n\v\f.";
			auto last = name.find_last_not_of(trimTrailingChars);
			if (last == std::string::npos)
				last = first;
			name = name.substr(first, last - first + 1);

			// Replace invalid characters
			std::replace_if(name.begin(), name.end(), [](char c) {
				auto u = static_cast<unsigned char>(c);
				// Only perform "illegal" checks if it's a standard ASCII character (0-127)
				if (u < 128u) {
					return c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' ||
					       c == '"' || c == '<' || c == '>' || c == '|' ||
					       u < 32u || u == 127u;
				}
				return false; }, '_');

			// Windows reserved device names
			static constexpr const char* reserved[] = {
				"CON", "PRN", "AUX", "NUL",
				"COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
				"LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
			};

			const auto extension = name.find('.');
			const auto baseName = name.substr(0, extension);
			for (const char* r : reserved) {
				if (Util::IEquals(baseName, r)) {
					// Device names remain reserved when followed by an extension.
					name.insert(extension == std::string::npos ? name.size() : extension, 1, '_');
					break;
				}
			}

			// Limit length
			if (name.length() > 255u) {
				name = name.substr(0, 255u);
			}

			return name;
		}
	}
}

std::vector<SettingsDiffEntry> Util::FileSystem::DiffJson(const nlohmann::json& userJson, const nlohmann::json& testJson, float epsilon)
{
	std::vector<SettingsDiffEntry> diffEntries;

	try {
		auto diff = nlohmann::json::diff(userJson, testJson);

		for (const auto& change : diff) {
			try {
				std::string op = change.value("op", "");
				std::string path = change.value("path", "");
				std::string aVal, bVal;

				if (op == "replace") {
					auto aJson = userJson.at(nlohmann::json::json_pointer(path));
					auto bJson = testJson.at(nlohmann::json::json_pointer(path));

					// If both values are numbers, check if difference is within epsilon (double precision)
					if (aJson.is_number() && bJson.is_number()) {
						double aDouble = aJson.get<double>();
						double bDouble = bJson.get<double>();
						if (std::abs(aDouble - bDouble) < static_cast<double>(epsilon)) {
							continue;  // Skip insignificant numeric differences
						}
					}

					aVal = aJson.dump();
					bVal = bJson.dump();
				} else if (op == "add") {
					aVal = "(none)";
					bVal = testJson.at(nlohmann::json::json_pointer(path)).dump();
				} else if (op == "remove") {
					aVal = userJson.at(nlohmann::json::json_pointer(path)).dump();
					bVal = "(none)";
				} else {
					logger::warn("Unknown JSON diff operation '{}' at path '{}'", op, path);
					continue;
				}

				diffEntries.push_back({ path, aVal, bVal });
			} catch (const std::exception& e) {
				logger::warn("Failed to process JSON diff change: {}", e.what());
				// Continue processing other changes
			}
		}
	} catch (const std::exception& e) {
		logger::warn("Failed to compute JSON diff: {}", e.what());
	}

	return diffEntries;
}

std::vector<SettingsDiffEntry> Util::FileSystem::LoadJsonDiff(const std::filesystem::path& userPath, const std::filesystem::path& testPath, float epsilon)
{
	std::vector<SettingsDiffEntry> diffEntries;

	try {
		if (!std::filesystem::exists(userPath)) {
			logger::warn("User config file does not exist: {}", userPath.string());
			return diffEntries;
		}

		if (!std::filesystem::exists(testPath)) {
			logger::warn("Test config file does not exist: {}", testPath.string());
			return diffEntries;
		}

		std::ifstream userFile(userPath);
		std::ifstream testFile(testPath);

		if (!userFile.is_open()) {
			logger::warn("Failed to open user config file: {}", userPath.string());
			return diffEntries;
		}

		if (!testFile.is_open()) {
			logger::warn("Failed to open test config file: {}", testPath.string());
			return diffEntries;
		}

		nlohmann::json userJson, testJson;

		try {
			userFile >> userJson;
		} catch (const std::exception& e) {
			logger::warn("Failed to parse user config JSON from '{}': {}", userPath.string(), e.what());
			return diffEntries;
		}

		try {
			testFile >> testJson;
		} catch (const std::exception& e) {
			logger::warn("Failed to parse test config JSON from '{}': {}", testPath.string(), e.what());
			return diffEntries;
		}

		// Use shared diffing logic
		return DiffJson(userJson, testJson, epsilon);
	} catch (const std::exception& e) {
		logger::warn("Failed to load JSON diff from '{}' and '{}': {}", userPath.string(), testPath.string(), e.what());
	}

	return diffEntries;
}
