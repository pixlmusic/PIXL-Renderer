#include "Presets/RenderPresetLibrary.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>

namespace fs = std::filesystem;
using PIXL::Presets::ApplyToUserConfig;
using PIXL::Presets::CaptureRendererSettings;
using PIXL::Presets::CreateDocument;
using PIXL::Presets::Discover;
using PIXL::Presets::MakeFileStem;
using PIXL::Presets::MergeCompatibleSettings;
using PIXL::Presets::MergePendingRendererSettings;
using PIXL::Presets::ParseDocument;
using PIXL::Presets::RenderPreset;
using PIXL::Presets::SaveNewPreset;
using PIXL::Presets::StagePendingRendererSettings;
using PIXL::Presets::ClearPendingRendererSettings;
using PIXL::Presets::json;

namespace
{
	void Check(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	void Write(const fs::path& path, const json& value)
	{
		fs::create_directories(path.parent_path());
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		output << value.dump(2);
		if (!output)
			throw std::runtime_error("failed to write preset fixture");
	}
}

int main(int argc, char** argv)
{
	const auto root = fs::temp_directory_path() / "PIXL-RenderPreset-Test";
	try {
		const auto sourceRoot = fs::current_path();
		const auto shippedRoot = sourceRoot / "distribution" / "RendererPresets";
		const auto shipped = Discover(shippedRoot);
		Check(shipped.size() == 6, "all six bundled portable renderer presets are recognized");
		Check(std::none_of(shipped.begin(), shipped.end(), [](const auto& preset) { return preset.legacy; }),
			"bundled profiles use the portable renderer-only schema");
		const auto legacyRoot = sourceRoot / "distribution" / "SKSE" / "Plugins" / "PIXLRenderer" / "Presets";
		for (const auto& legacyPath : fs::directory_iterator(legacyRoot)) {
			if (!legacyPath.is_regular_file() || legacyPath.path().extension() != ".json")
				continue;
			std::ifstream input(legacyPath.path(), std::ios::binary);
			const auto legacySettings = json::parse(input);
			const auto portablePath = shippedRoot / (legacyPath.path().stem().string() + ".pixlpreset.json");
			std::ifstream portableInput(portablePath, std::ios::binary);
			const auto portable = json::parse(portableInput);
			Check(portable.at("settings") == CaptureRendererSettings(legacySettings),
				"bundled portable settings match their renderer-only legacy source");
		}
		if (argc > 1) {
			const auto packaged = Discover(fs::path(argv[1]));
			Check(packaged.size() == 6, "the staged package contains six profiles discoverable by the runtime library");
			Check(std::none_of(packaged.begin(), packaged.end(), [](const auto& preset) { return preset.legacy; }),
				"the staged package preserves portable preset documents");
		}

		fs::remove_all(root);
		const json complete{
			{ "Version", "1-0-9-0" },
			{ "Menu", {{"SelectedThemePreset", "PIXL"}, {"ToggleKey", "PageDown"}} },
			{ "Advanced", {{"Shader Defines", "LOCAL_DEBUG=1"}, {"Compiler Threads", 8}} },
			{ "General", {{"Enable Shaders", true}, {"Language", "en"}, {"Enable Disk Cache", true}} },
			{ "Disable at Boot", {{"OceanDynamics", true}, {"HybridGI", false}} },
			{ "HybridGI", {{"Enable", true}, {"Quality", 2}, {"RayBudget", 96}} },
			{ "Camera Suite", {{"Physical Camera", true}, {"Exposure", 0.6}} },
			{ "Pixel Capture", {{"ScreenshotPath", "C:/Users/Private/Pictures"}, {"Format", 1}} }
		};
		const auto captured = CaptureRendererSettings(complete);
		Check(!captured.contains("Menu") && !captured.contains("Advanced") && !captured.contains("Version"),
			"personal, developer and version config excluded");
		Check(captured["General"].size() == 1 && captured["General"]["Enable Shaders"],
			"renderer master enable is shared while machine preferences are excluded");
		Check(!captured["Pixel Capture"].contains("ScreenshotPath") && captured["Pixel Capture"]["Format"] == 1,
			"machine-local screenshot path is excluded while adjacent renderer controls remain shareable");
		Check(captured.contains("Disable at Boot") && captured.contains("HybridGI"),
			"module activation and module parameters are captured");

		auto document = CreateDocument("Aurora | Balanced", "PIXL User",
			"A daylight-oriented renderer look.", "1.0.9.0", complete);
		document["settings"]["FutureModule"] = {{"Enabled", true}};
		RenderPreset parsed, rejected;
		std::string error;
		Check(ParseDocument(document, "aurora.pixlpreset.json", parsed, error), "portable preset parses");
		Check(parsed.name == "Aurora | Balanced" && !parsed.legacy, "portable metadata retained");
		auto unsupportedSchema = document;
		unsupportedSchema["schemaVersion"] = 77;
		Check(!ParseDocument(unsupportedSchema, "future.json", rejected, error) && rejected.settings.empty(),
			"unknown schema rejected without returning partial data");
		auto unsafeFields = document;
		unsafeFields["settings"]["Advanced"] = json::object();
		Check(!ParseDocument(unsafeFields, "unsafe.json", rejected, error) && rejected.settings.empty(),
			"non-renderer settings rejected without returning partial data");
		auto unknownFormat = document;
		unknownFormat["format"] = 17;
		unknownFormat["Version"] = "legacy-marker";
		Check(!ParseDocument(unknownFormat, "unknown.json", rejected, error),
			"unknown declared formats are not reinterpreted as legacy settings");
		auto privatePath = document;
		privatePath["settings"]["Pixel Capture"]["ScreenshotPath"] = "C:/Private/Folder";
		Check(!ParseDocument(privatePath, "private-path.json", rejected, error),
			"manually authored presets cannot change the recipient's local screenshot destination");

		const json installed{
			{ "Menu", {{"SelectedThemePreset", "PIXL"}} },
			{ "General", {{"Enable Shaders", false}, {"Language", "fr"}, {"Enable Disk Cache", false}} },
			{ "Disable at Boot", {{"OceanDynamics", false}, {"HybridGI", true}, {"NewModule", false}} },
			{ "HybridGI", {{"Enable", false}, {"Quality", 0}, {"RayBudget", "not-a-number"}, {"FutureSetting", 1}} },
			{ "Camera Suite", {{"Physical Camera", false}, {"Exposure", 0.2}} },
			{ "Pixel Capture", {{"ScreenshotPath", "D:/User/OwnCaptureFolder"}, {"Format", 0}} }
		};
		PIXL::Presets::MergeSummary merge;
		const auto merged = MergeCompatibleSettings(installed, parsed.settings, merge);
		Check(merged["General"]["Enable Shaders"] == true && merged["General"]["Language"] == "fr",
			"renderer values merge without changing personal language");
		Check(merged["Menu"]["SelectedThemePreset"] == "PIXL", "interface theme remains separate");
		Check(merged["HybridGI"]["Enable"] == true && merged["HybridGI"]["Quality"] == 2,
			"compatible nested values apply");
		Check(merged["HybridGI"]["RayBudget"] == "not-a-number" && merged["HybridGI"]["FutureSetting"] == 1,
			"type changes are rejected and pre-existing future fields are preserved");
		Check(merged["Pixel Capture"]["ScreenshotPath"] == "D:/User/OwnCaptureFolder" && merged["Pixel Capture"]["Format"] == 1,
			"applying a renderer preset leaves the recipient's private capture path untouched");
		Check(merge.unsupported >= 1 && merge.typeMismatches >= 1, "compatibility skips are counted");
		StagePendingRendererSettings(parsed.settings);
		auto laterSave = installed;
		laterSave["Menu"]["SelectedThemePreset"] = "User Night Theme";
		laterSave["HybridGI"]["Quality"] = 0;
		MergePendingRendererSettings(laterSave);
		Check(laterSave["HybridGI"]["Quality"] == 2 && laterSave["Menu"]["SelectedThemePreset"] == "User Night Theme",
			"later interface saves preserve the staged renderer preset while retaining current UI preferences");
		Check(laterSave["Pixel Capture"]["ScreenshotPath"] == "D:/User/OwnCaptureFolder",
			"pending preset preservation still protects the local capture destination");
		ClearPendingRendererSettings();

		Check(MakeFileStem("../Aurora / Northern Lights") == "Aurora-Northern-Lights",
			"filename sanitized against path traversal");
		Check(MakeFileStem("CON").starts_with('_'), "reserved Windows filename protected");
		Check(MakeFileStem("CON.profile").starts_with('_'), "reserved Windows basename before extension protected");
		Check(MakeFileStem("\xE5\x86\xAC\xE3\x81\xAE\xE5\xA4\x9C") == "PIXL-Preset", "unicode-only names retain metadata with a safe portable filename");

		fs::path firstPath, secondPath;
		const auto userDir = root / "Data" / "SKSE" / "Plugins" / "PIXLRenderer" / "Presets" / "User";
		Check(SaveNewPreset(userDir, document, firstPath, error), "first user preset saved");
		Check(SaveNewPreset(userDir, document, secondPath, error) && firstPath != secondPath,
			"saving a duplicate name preserves the earlier file");
		{
			std::ifstream firstInput(firstPath, std::ios::binary);
			json firstSaved;
			firstInput >> firstSaved;
			Check(firstSaved == document, "duplicate-name save does not replace the first preset");
		}
		std::mutex pathsMutex;
		std::set<fs::path> concurrentPaths;
		bool concurrentSavesSucceeded = true;
		std::string concurrentError;
		std::vector<std::thread> writers;
		for (int index = 0; index < 8; ++index) {
			writers.emplace_back([&] {
				fs::path path;
				std::string saveError;
				const bool saved = SaveNewPreset(userDir, document, path, saveError);
				std::lock_guard lock(pathsMutex);
				if (saved)
					concurrentPaths.insert(std::move(path));
				else {
					concurrentSavesSucceeded = false;
					concurrentError = std::move(saveError);
				}
			});
		}
		for (auto& writer : writers)
			writer.join();
		Check(concurrentSavesSucceeded, concurrentError.c_str());
		Check(concurrentPaths.size() == writers.size(), "concurrent same-name saves publish unique files");
		Write(root / "Data" / "SKSE" / "Plugins" / "PIXLRenderer" / "Presets" / "Legacy.json", complete);
		const auto found = Discover(root / "Data" / "SKSE" / "Plugins" / "PIXLRenderer" / "Presets");
		Check(found.size() == 11, "portable and legacy presets auto-discovered recursively");
		Check(std::any_of(found.begin(), found.end(), [](const auto& preset) { return preset.legacy; }),
			"legacy full-config presets remain discoverable");

		const auto userConfig = root / "Config" / "UserGraphics.json";
		Write(userConfig, json{{"before", "user config"}});
		const auto apply = ApplyToUserConfig(userConfig, installed, parsed);
		if (!apply.success)
			throw std::runtime_error("preset application failed: " + apply.error);
		Check(fs::exists(apply.backupPath), "applying a preset creates a rollback copy");
		Check(apply.settings["HybridGI"]["Quality"] == 2, "applied profile contains merged renderer values");
		std::ifstream output(userConfig, std::ios::binary);
		json saved;
		output >> saved;
		Check(saved["Menu"]["SelectedThemePreset"] == "PIXL" && saved["General"]["Language"] == "fr",
			"published config preserves interface and local preferences");
		Check(saved["HybridGI"]["Quality"] == 2, "published user config applies the complete preset payload");

		output.close();
		fs::remove_all(root);
		std::cout << "Render presets: schema, privacy filtering, compatibility merge, pending-save protection, legacy discovery, safe naming, rollback and atomic apply passed\n";
		return 0;
	} catch (const std::exception& exception) {
		std::cerr << exception.what() << '\n';
		std::error_code ignored;
		fs::remove_all(root, ignored);
		return 1;
	}
}
