#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace PIXL::Presets
{
	using json = nlohmann::json;

	struct RenderPreset
	{
		std::filesystem::path filePath;
		std::string name;
		std::string author;
		std::string description;
		std::string rendererVersion;
		json settings = json::object();
		bool legacy = false;
	};

	struct MergeSummary
	{
		std::size_t applied = 0;
		std::size_t unsupported = 0;
		std::size_t typeMismatches = 0;
	};

	struct ApplyResult
	{
		bool success = false;
		std::filesystem::path backupPath;
		json settings = json::object();
		MergeSummary merge;
		std::string error;
	};

	/**
	 * Create a portable, renderer-only document from a complete PIXL settings
	 * snapshot. User interface, compiler and machine-local preferences are kept
	 * out of shared presets.
	 */
	[[nodiscard]] json CreateDocument(std::string name, std::string author,
		std::string description, std::string rendererVersion,
		const json& completeSettings);

	/** Parse the current portable schema and older full-settings snapshots. */
	[[nodiscard]] bool ParseDocument(const json& source,
		const std::filesystem::path& sourcePath, RenderPreset& output,
		std::string& error);

	/** Remove personal/UI/compiler fields and leave only renderer settings. */
	[[nodiscard]] json CaptureRendererSettings(const json& completeSettings);

	/** Overlay known, shape-compatible preset settings onto a current snapshot. */
	[[nodiscard]] json MergeCompatibleSettings(const json& currentSettings,
		const json& presetSettings, MergeSummary& summary);

	/** Bounded local discovery for shipped, user-saved and mod-manager presets. */
	[[nodiscard]] std::vector<RenderPreset> Discover(const std::filesystem::path& root,
		std::size_t maxPresets = 128, std::uintmax_t maxFileBytes = 2 * 1024 * 1024);

	/** Save a new preset without replacing an existing file. */
	[[nodiscard]] bool SaveNewPreset(const std::filesystem::path& directory,
		const json& document, std::filesystem::path& savedPath, std::string& error);

	/** Apply to UserGraphics.json with a timestamped rollback copy and atomic publish. */
	[[nodiscard]] ApplyResult ApplyToUserConfig(const std::filesystem::path& userConfigPath,
		const json& currentSettings, const RenderPreset& preset);

	/** Preserve an applied next-launch renderer preset if the session saves other settings. */
	void StagePendingRendererSettings(const json& presetSettings);
	void MergePendingRendererSettings(json& completeSettings);
	void ClearPendingRendererSettings();

	/** Create a filesystem-safe stem used for user-created preset filenames. */
	[[nodiscard]] std::string MakeFileStem(std::string_view name);
}
