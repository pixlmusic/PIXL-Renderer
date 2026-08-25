#include "RenderModule.h"

#include "PipelineHealth.h"
#include "ModuleVersions.h"
#include "Modules/SkyVeil.h"
#include "Modules/WorldProbes.h"
#include "Modules/Atmosphere.h"
#include "Modules/MaterialLayers.h"
#include "Modules/ThinSurface.h"
#include "Modules/GroundResponse.h"
#include "Modules/FoliageDynamics.h"
#include "Modules/CameraSuite.h"
#include "Modules/StrandShading.h"
#include "Modules/HorizonBlend.h"
#include "Modules/AmbientProbe.h"
#include "Modules/InteriorDaylight.h"
#include "Modules/NaturalLighting.h"
#include "Modules/DistanceBlend.h"
#include "Modules/RadiantGrid.h"
#include "Modules/LinearLightCore.h"
#include "Modules/PulseProfiler.h"
#include "Modules/HybridGI.h"
#include "Modules/ContactShadows.h"
#include "Modules/PixelCapture.h"
#include "Modules/SkinOptics.h"
#include "Modules/WindowLife.h"
#include "Modules/SkyContinuity.h"
#include "Modules/SkyBounce.h"
#include "Modules/TissueDiffusion.h"
#include "Modules/TerrainSeam.h"
#include "Modules/TerrainField.h"
#include "Modules/TerrainOcclusion.h"
#include "Modules/TerrainDetail.h"
#include "Modules/Waterbody.h"
#include "Modules/ImageReconstruction.h"
#include "Modules/LightVolumes.h"
#include "Modules/VolumeOcclusion.h"
#include "Modules/WaterOptics.h"
#include "Modules/RainResponse.h"
#include "I18n/I18n.h"
#include "Menu.h"
#include "SettingsOverrideManager.h"
#include "Utils/Format.h"
#include "WeatherManager.h"
#include "WeatherVariableRegistry.h"

#include "State.h"
#include "MaterialForge.h"

void RenderModule::Load(json& o_json)
{
	// Convert string to wstring
	auto ini_filename = std::format("{}.ini", GetShortName());
	std::wstring ini_filename_w;
	std::ranges::copy(ini_filename, std::back_inserter(ini_filename_w));
	auto ini_path = L"Data\\Shaders\\PIXL\\Modules\\" + ini_filename_w;

	CSimpleIniA ini;
	ini.SetUnicode();
	SI_Error rc = ini.LoadFile(ini_path.c_str());

	if (rc < 0) {
		if (!PipelineHealth::IsObsoleteFeature(GetShortName()))
			logger::warn("{} failed to load, feature disabled", ini_filename);
		loaded = false;
		return;
	}

	bool hasError = false;
	std::string errorVersion;
	PipelineHealth::FeatureIssueInfo::IssueType errorType = PipelineHealth::FeatureIssueInfo::IssueType::UNKNOWN;

	if (PipelineHealth::IsObsoleteFeature(GetShortName())) {
		hasError = true;
		errorVersion = "N/A";
		errorType = PipelineHealth::FeatureIssueInfo::IssueType::OBSOLETE;
		failedLoadedMessage = std::format("{} is an obsolete feature that has been removed", GetShortName());
	} else if (auto value = ini.GetValue("PIXL Module", "Version")) {
		try {
			REL::Version featureVersion(std::regex_replace(value, std::regex("-"), "."));

			// Check if feature exists in minimal versions
			REL::Version minimalFeatureVersion;
			if (!RenderModule::IsModuleKnown(GetShortName(), &minimalFeatureVersion)) {
				hasError = true;
				errorVersion = value;
				errorType = PipelineHealth::FeatureIssueInfo::IssueType::UNKNOWN;
				failedLoadedMessage = std::format("{} {} is an unknown feature not supported by this PIXL build. This may be a feature from a development branch.", GetShortName(), value);
			} else {
				// Version compatibility check
				bool oldFeature = featureVersion.compare(minimalFeatureVersion) == std::strong_ordering::less;
				bool majorVersionMismatch = featureVersion.major() < minimalFeatureVersion.major();

				if (!oldFeature && !majorVersionMismatch) {
					loaded = true;
					logger::info("{} {} successfully loaded", ini_filename, value);
				} else {
					hasError = true;
					errorVersion = value;
					errorType = PipelineHealth::FeatureIssueInfo::IssueType::VERSION_MISMATCH;

					std::string minimalVersionString = Util::GetFormattedVersion(minimalFeatureVersion);

					if (IsCore()) {
						failedLoadedMessage = std::format("This legacy add-on is already integrated into PIXL Renderer. Remove the duplicate add-on with your mod manager.");
					} else if (majorVersionMismatch) {
						failedLoadedMessage = std::format("{} {} is too old, major version incompatibility detected. Required: {}", GetShortName(), value, minimalVersionString);
					} else {
						failedLoadedMessage = std::format("{} {} is an old feature version, required: {}", GetShortName(), value, minimalVersionString);
					}
				}
			}

			version = value;
		} catch (const std::exception& e) {
			hasError = true;
			errorVersion = value;
			errorType = PipelineHealth::FeatureIssueInfo::IssueType::VERSION_MISMATCH;
			failedLoadedMessage = std::format("{} {} has invalid version format: {}", GetShortName(), value, e.what());
		}
	} else {
		hasError = true;
		errorVersion = "unknown";
		errorType = PipelineHealth::FeatureIssueInfo::IssueType::VERSION_MISMATCH;

		// Get the minimum required version to include in the error message
		std::string requiredVersion = RenderModule::GetModuleRequiredVersion(GetShortName());

		failedLoadedMessage = std::format("The {} file is missing. This feature is not installed! Version required: {}", ini_filename, requiredVersion);
	}

	if (hasError) {
		loaded = false;
		logger::warn("{}", failedLoadedMessage);

		// Guard against empty shortName to prevent bogus filesystem access
		std::string shortName = GetShortName();
		if (!shortName.empty()) {
			PipelineHealth::FeatureFileInfo fileInfo = PipelineHealth::GetFeatureFileInfo(shortName);

			// For version mismatch, also pass the minimum required version
			std::string minimumVersion;
			if (errorType == PipelineHealth::FeatureIssueInfo::IssueType::VERSION_MISMATCH) {
				minimumVersion = RenderModule::GetModuleRequiredVersion(shortName);
			}

			PipelineHealth::AddFeatureIssue(shortName, errorVersion, failedLoadedMessage, errorType, fileInfo, minimumVersion);

		} else {
			logger::error("RenderModule has empty short name, cannot add to feature issues list");
		}
	} else {
		// No errors, load settings now
		if (o_json[GetName()].is_structured()) {
			logger::info("Loading {} settings", GetName());
			try {
				LoadSettings(o_json[GetName()]);
			} catch (...) {
				logger::warn("Invalid settings for {}, using default.", GetName());
				RestoreDefaultSettings();
			}
		} else {
			logger::info("Loading default settings for {}", GetName());
			RestoreDefaultSettings();
		}
	}
}

void RenderModule::Save(json& o_json)
{
	SaveSettings(o_json[GetName()]);
}

bool RenderModule::ValidateCache(CSimpleIniA& a_ini)
{
	auto name = GetName();
	auto ini_name = GetShortName();

	logger::info("Validating {}", name);

	auto enabledInCache = a_ini.GetBoolValue(ini_name.c_str(), "Enabled", false);
	if (enabledInCache && !loaded) {
		logger::info("RenderModule was uninstalled");
		return false;
	}
	if (!enabledInCache && loaded) {
		logger::info("RenderModule was installed");
		return false;
	}

	if (loaded) {
		auto versionInCache = a_ini.GetValue(ini_name.c_str(), "Version");
		if (!versionInCache || strcmp(versionInCache, version.c_str()) != 0) {
			logger::info("Change in version detected. Installed {} but {} in Disk Cache", version, versionInCache ? versionInCache : "(missing)");
			return false;
		} else {
			logger::info("Installed version and cached version match.");
		}
	}

	logger::info("Cached feature is valid");
	return true;
}

void RenderModule::WriteDiskCacheInfo(CSimpleIniA& a_ini)
{
	auto ini_name = GetShortName();
	a_ini.SetBoolValue(ini_name.c_str(), "Enabled", loaded);
	a_ini.SetValue(ini_name.c_str(), "Version", version.c_str());
}

/**
 * @brief Provides access to the registry of all known features.
 * @return A constant reference to the vector of all known feature instances.
 */
const std::vector<RenderModule*>& RenderModule::GetModuleList()
{
	static std::vector<RenderModule*> features = {
		&globals::pipeline::materialForge,
		&globals::pipeline::volumeOcclusion,
		&globals::pipeline::foliageDynamics,
		&globals::pipeline::groundResponse,
		&globals::pipeline::contactShadows,
		&globals::pipeline::materialLayers,
		&globals::pipeline::rainResponse,
		&globals::pipeline::radiantGrid,
		&globals::pipeline::worldProbes,
		&globals::pipeline::skyVeil,
		&globals::pipeline::waterOptics,
		&globals::pipeline::pulseProfiler,
		&globals::pipeline::tissueDiffusion,
		&globals::pipeline::terrainOcclusion,
		&globals::pipeline::hybridGI,
		&globals::pipeline::skyBounce,
		&globals::pipeline::skyContinuity,
		&globals::pipeline::terrainSeam,
		&globals::pipeline::terrainField,
		&globals::pipeline::lightVolumes,
		&globals::pipeline::distanceBlend,
		&globals::pipeline::naturalLighting,
		&globals::pipeline::strandShading,
		&globals::pipeline::interiorDaylight,
		&globals::pipeline::terrainDetail,
		&globals::pipeline::ambientProbe,
		&globals::pipeline::thinSurface,
		&globals::pipeline::imageReconstruction,
		&globals::pipeline::pixelCapture,
		&globals::pipeline::linearLightCore,
		&globals::pipeline::waterbody,
		&globals::pipeline::horizonBlend,
		&globals::pipeline::atmosphere,
		&globals::pipeline::cameraSuite,
		&globals::pipeline::skinOptics,
		// Keep WindowLife last so its SetupGeometry hook chains after SkinOptics.
		&globals::pipeline::windowLife
	};

	return features;
}

RenderModule* RenderModule::FindModuleById(const std::string& shortName)
{
	for (auto* feature : GetModuleList()) {
		if (feature->loaded && feature->GetShortName() == shortName)
			return feature;
	}
	return nullptr;
}

std::vector<std::string> RenderModule::GetLoadedModuleNames()
{
	std::vector<std::string> names;
	for (auto* feature : GetModuleList()) {
		if (feature->loaded && feature->IsInMenu())
			names.push_back(feature->GetShortName());
	}
	std::sort(names.begin(), names.end());
	return names;
}

bool RenderModule::ToggleAtBootSetting()
{
	auto state = globals::state;
	const std::string featureName = GetShortName();
	auto disabled = state->IsFeatureDisabled(featureName);
	state->SetFeatureDisabled(featureName, !disabled);

	// Callers and the UI reason in terms of "enabled at next boot". Returning
	// the disabled-map value inverted the status text and made a successful
	// enable action appear as "Disabled" in the log.
	return !state->IsFeatureDisabled(featureName);
}

bool RenderModule::ReapplyOverrideSettings()
{
	auto overrideManager = SettingsOverrideManager::GetSingleton();
	std::string featureName = GetShortName();

	if (!overrideManager || !overrideManager->HasFeatureOverrides(featureName)) {
		return false;
	}

	// Delete user override file to restore original override behavior
	overrideManager->DeleteUserOverride(featureName);

	// Get base settings and apply overrides fresh
	json featureJson;
	SaveSettings(featureJson);

	// Apply overrides to the settings (without user customizations)
	size_t appliedCount = overrideManager->ReapplyFeatureOverrides(featureName, featureJson);

	if (appliedCount > 0) {
		// Load the override settings back into the feature
		LoadSettings(featureJson);
		return true;
	}

	return false;
}

std::string RenderModule::GetDisplayCategory() const
{
	const auto category = GetCategory();
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
	if (category == ModuleGroups::kOther)
		return T("feature.category.other", "Other");
	if (category == ModuleGroups::kSky)
		return T("feature.category.sky", "Sky");
	if (category == ModuleGroups::kUtility)
		return T("feature.category.utility", "Utility");
	if (category == ModuleGroups::kWater)
		return T("feature.category.water", "Water");

	return std::string(category);
}

std::string RenderModule::GetReleaseStageTag(ReleaseStage stage)
{
	switch (stage) {
	case ReleaseStage::Alpha:
		return T("menu.features.tag_alpha", "[ALPHA]");
	case ReleaseStage::Beta:
		return T("menu.features.tag_beta", "[BETA]");
	default:
		return {};
	}
}

void RenderModule::DrawUnloadedUI()
{
	// Prioritize detailed failure message if available
	if (!failedLoadedMessage.empty()) {
		// Use error color for all failure messages
		auto& themeSettings = Menu::GetSingleton()->GetTheme();
		ImGui::TextColored(themeSettings.StatusPalette.Error, failedLoadedMessage.c_str());
		return;
	}

	// Fallback: Always show missing file message when no specific failure message exists
	auto& themeSettings = Menu::GetSingleton()->GetTheme();
	auto ini_filename = std::format("{}.ini", GetShortName());
	// Get the minimum required version to include in the error message
	std::string requiredVersion = RenderModule::GetModuleRequiredVersion(GetShortName());

	auto missingFileMessage = std::format("The {} file is missing. This feature is not installed! Version required: {}", ini_filename, requiredVersion);
	ImGui::TextColored(themeSettings.StatusPalette.Error, missingFileMessage.c_str());

	// Also show feature summary if available
	auto [description, keyFeatures] = GetModuleSummary();
	if (!description.empty()) {
		ImGui::Spacing();
		ImGui::TextWrapped("%s", description.c_str());
	}

	if (!keyFeatures.empty()) {
		if (description.empty()) {
			ImGui::Spacing();
		}
		ImGui::TextWrapped("%s", T("feature.key_features", "Key features:"));
		for (const auto& feature : keyFeatures) {
			ImGui::BulletText("%s", feature.c_str());
		}
	}
}

std::string RenderModule::GetModuleRequiredVersion(const std::string& shortName)
{
	if (shortName.empty()) {
		return "unknown";
	}

	auto iter = ModuleVersions::FEATURE_MINIMAL_VERSIONS.find(shortName);
	if (iter != ModuleVersions::FEATURE_MINIMAL_VERSIONS.end()) {
		return Util::GetFormattedVersion(iter->second);
	}

	return "unknown";
}

bool RenderModule::IsModuleKnown(const std::string& shortName, REL::Version* outVersion)
{
	if (shortName.empty()) {
		return false;
	}

	auto iter = ModuleVersions::FEATURE_MINIMAL_VERSIONS.find(shortName);
	if (iter != ModuleVersions::FEATURE_MINIMAL_VERSIONS.end()) {
		if (outVersion) {
			*outVersion = iter->second;
		}
		return true;
	}

	return false;
}
