#include "MaterialForge.h"

#include "MaterialForge/BSLightingShaderMaterialPBR.h"
#include "MaterialForge/BSLightingShaderMaterialPBRLandscape.h"
#include "MaterialForge/PhysicalMaterialRegistry.h"

#include "Modules/InteriorDaylight.h"
#include "Modules/ActorSurfaceEffects.h"
#include "Modules/HairReconstruction.h"
#include "Modules/MaterialLayers.h"
#include "Hooks.h"
#include "I18n/I18n.h"
#include "ShaderCache.h"
#include "State.h"
#include "Util.h"

#include <cmath>
#define I18N_KEY_PREFIX "feature.material_forge."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	GlintParameters,
	enabled,
	screenSpaceScale,
	logMicrofacetDensity,
	microfacetRoughness,
	densityRandomization);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	MaterialForge::PBRTextureSetData,
	roughnessScale,
	displacementScale,
	specularLevel,
	subsurfaceColor,
	subsurfaceOpacity,
	coatColor,
	coatStrength,
	coatRoughness,
	coatSpecularLevel,
	innerLayerDisplacementOffset,
	fuzzColor,
	fuzzWeight,
	glintParameters);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	MaterialForge::PBRMaterialObjectData,
	baseColorScale,
	roughness,
	specularLevel,
	glintParameters);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	MaterialForge::Settings,
	VertexAOStrength,
	EnableLegacyPhysicalDirectLighting,
	LegacyPhysicalDebugMode,
	LegacyPhysicalSpecularScale,
	EnableLegacyMetalInference,
	LegacyMetalInferenceStrength,
	LegacyMetalInferenceThreshold,
	LegacyMetalInferenceMaximum,
	EnablePhysicalLocalLightFalloff,
	EnableLocalContactShadows,
	LocalContactShadowLightCount,
	LocalContactShadowLength,
	LocalContactShadowStrength,
	EnableSpecularAA,
	SpecularAAStrength,
	SpecularAAVarianceClamp,
	EnableGGXMultiScatter,
	GGXMultiScatterStrength,
	LocalLightMinimumDistance,
	PhysicalLocalLightFalloffStrength);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	MaterialForge::LegacyTuningSettings,
	Magic,
	Version,
	EnableTuning,
	pad0,
	ConversionStrength,
	ShininessScale,
	RoughnessCurve,
	RoughnessScale,
	RoughnessBias,
	MinRoughness,
	MaxRoughness,
	DielectricF0,
	SpecularColorInfluence,
	SpecularStrengthInfluence,
	BaseColorEnergy,
	MetallicDiffuseSuppression,
	MetalEnvironmentWeight,
	MetalSpecularWeight,
	MetalSmoothnessWeight,
	MetalColorWeight,
	MetalNoMaskPenalty,
	MetalNeutralPrior,
	MetalCrossEvidenceFloor,
	F0Scale,
	MetalTintChromaLow,
	MetalTintChromaHigh,
	MetalColorCorrelationLow,
	MetalColorCorrelationHigh);

#define CHECK_PBR_TEXTURE(textureName)                                                                         \
	if (!(pbrMaterial->textureName)) {                                                                         \
		logger::warn("[MaterialForge] {} missing {}; treating as nonPBR", pbrMaterial->inputFilePath, #textureName); \
		return false;                                                                                          \
	}

namespace PNState
{
	void ReadPBRRecordConfigs(const std::string& rootPath, std::function<void(const std::string&, const json&)> recordReader)
	{
		if (std::filesystem::exists(rootPath)) {
			auto configs = clib_util::distribution::get_configs(rootPath, "", ".json");

			if (configs.empty()) {
				logger::warn("[MaterialForge] no .json files were found within the {} folder, aborting...", rootPath);
				return;
			}

			logger::info("[MaterialForge] {} matching jsons found", configs.size());

			for (auto& path : configs) {
				logger::info("[MaterialForge] loading json : {}", path);

				std::ifstream fileStream(path);
				if (!fileStream.is_open()) {
					logger::error("[MaterialForge] failed to read {}", path);
					continue;
				}

				json config;
				try {
					fileStream >> config;
				} catch (const nlohmann::json::parse_error& e) {
					logger::error("[MaterialForge] failed to parse {} : {}", path, e.what());
					continue;
				}

				const auto editorId = std::filesystem::path(path).stem().string();
				recordReader(editorId, config);
			}
		}
	}

	void SavePBRRecordConfig(const std::string& rootPath, const std::string& editorId, const json& config)
	{
		std::filesystem::create_directory(rootPath);

		const std::string outputPath = std::format("{}\\{}.json", rootPath, editorId);
		std::ofstream fileStream(outputPath);
		if (!fileStream.is_open()) {
			logger::error("[MaterialForge] failed to write {}", outputPath);
			return;
		}
		try {
			fileStream << std::setw(4) << config;
		} catch (const nlohmann::json::type_error& e) {
			logger::error("[MaterialForge] failed to serialize {} : {}", outputPath, e.what());
			return;
		}
	}
}

void SetupPBRLandscapeTextureParameters(BSLightingShaderMaterialPBRLandscape& material, const MaterialForge::PBRTextureSetData& textureSetData, uint32_t textureIndex);

namespace
{
	// ImGui::Checkbox writes a C++ bool (one byte). The renderer ABI stores feature
	// toggles as 32-bit uints, so aliasing uint* as bool* leaves the upper bytes
	// dependent on prior memory contents and is formally undefined behavior. Keep
	// the UI type-safe and write a canonical 0/1 uint on every edit.
	bool DrawUIntCheckbox(const char* label, uint& value)
	{
		bool checked = value != 0u;
		if (!ImGui::Checkbox(label, &checked))
			return false;
		value = checked ? 1u : 0u;
		return true;
	}

	void DrawTooltip(const char* text)
	{
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", text);
	}
}

void MaterialForge::DrawSettings()
{
	if (ImGui::TreeNodeEx(T(TKEY("global_settings"), "Global Settings"), ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderFloat(T(TKEY("vertex_ao_strength"), "Vertex AO Strength"), &settings.VertexAOStrength, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		DrawTooltip("Scales authored vertex ambient occlusion before it modulates indirect lighting. Updates in real time; 1.0 preserves the authored value.");
		DrawUIntCheckbox(T(TKEY("legacy_physical_direct_lighting"), "Physical Direct Lighting for Legacy Materials"), settings.EnableLegacyPhysicalDirectLighting);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextWrapped("%s", T(TKEY("legacy_physical_direct_lighting_tooltip"),
										 "Uses an energy-conserving GGX BRDF for ordinary Skyrim materials. Disable this compatibility switch to restore Skyrim's original Blinn-Phong direct highlights."));
		}
		ImGui::SliderFloat(T(TKEY("legacy_physical_specular_scale"), "Legacy GGX Highlight Scale"),
			&settings.LegacyPhysicalSpecularScale, 0.f, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextWrapped("%s", T(TKEY("legacy_physical_specular_scale_tooltip"),
										 "Calibrates normalized GGX highlights against Skyrim's non-radiometric legacy lights. This does not affect authored Material Forge materials."));
		}
		DrawUIntCheckbox(T(TKEY("legacy_metal_inference"), "Infer Metalness for Legacy Materials"), settings.EnableLegacyMetalInference);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextWrapped("%s", T(TKEY("legacy_metal_inference_tooltip"),
										 "Reconstructs a conservative conductor weight from Material Layers data or Skyrim's environment-mask, gloss, specular, opacity, and base-colour response. Emission is never used."));
		}
		if (settings.EnableLegacyMetalInference != 0) {
			ImGui::SliderFloat(T(TKEY("legacy_metal_inference_strength"), "Inferred Metal Strength"),
				&settings.LegacyMetalInferenceStrength, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			DrawTooltip("Scales the conservative conductor weight reconstructed for legacy and Material Layers surfaces. Authored Material Forge metalness remains authoritative.");
			ImGui::SliderFloat(T(TKEY("legacy_metal_inference_threshold"), "Vanilla Metal Confidence Threshold"),
				&settings.LegacyMetalInferenceThreshold, 0.05f, 0.8f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			DrawTooltip("Minimum environment-mask, gloss and material confidence required before an ordinary Skyrim surface is treated as partly metallic. Higher values reduce false positives.");
			ImGui::SliderFloat(T(TKEY("legacy_metal_inference_maximum"), "Vanilla Metalness Ceiling"),
				&settings.LegacyMetalInferenceMaximum, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			DrawTooltip("Maximum guessed metalness for vanilla materials. This ceiling does not limit authored Material Forge or high-confidence Material Layers conductors.");
		}
		if (globals::state->IsDeveloperMode() && ImGui::TreeNodeEx("Legacy -> Physical Calibration", ImGuiTreeNodeFlags_DefaultOpen)) {
			DrawUIntCheckbox("Enable Calibration", legacyTuningSettings.EnableTuning);
			ImGui::SliderFloat("Calibration Strength", &legacyTuningSettings.ConversionStrength, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			DrawTooltip("0 restores the original PIXL legacy conversion; 1 applies all calibration controls below. Authored Material Forge PBR is unaffected.");

			ImGui::SeparatorText("Gloss -> Roughness");
			ImGui::SliderFloat("Legacy Shininess Scale", &legacyTuningSettings.ShininessScale, 0.25f, 4.0f, "%.2fx", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Roughness Curve", &legacyTuningSettings.RoughnessCurve, 0.35f, 2.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Roughness Scale", &legacyTuningSettings.RoughnessScale, 0.5f, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Roughness Bias", &legacyTuningSettings.RoughnessBias, -0.35f, 0.35f, "%+.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Minimum Roughness", &legacyTuningSettings.MinRoughness, 0.02f, 0.40f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Maximum Roughness", &legacyTuningSettings.MaxRoughness, 0.40f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			legacyTuningSettings.MaxRoughness = std::max(legacyTuningSettings.MaxRoughness, legacyTuningSettings.MinRoughness + 0.01f);

			ImGui::SeparatorText("Dielectric Fresnel");
			ImGui::SliderFloat("Base Dielectric F0", &legacyTuningSettings.DielectricF0, 0.01f, 0.08f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("F0 Scale", &legacyTuningSettings.F0Scale, 0.25f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Legacy Specular Color Influence", &legacyTuningSettings.SpecularColorInfluence, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Legacy Specular Strength Influence", &legacyTuningSettings.SpecularStrengthInfluence, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Legacy Base Color Energy", &legacyTuningSettings.BaseColorEnergy, 0.5f, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

			ImGui::SeparatorText("Legacy Metal Evidence");
			ImGui::SliderFloat("Environment Mask Weight", &legacyTuningSettings.MetalEnvironmentWeight, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Specular Weight", &legacyTuningSettings.MetalSpecularWeight, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Smoothness Weight", &legacyTuningSettings.MetalSmoothnessWeight, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Color Correlation Weight", &legacyTuningSettings.MetalColorWeight, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("No-mask Confidence", &legacyTuningSettings.MetalNoMaskPenalty, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Neutral Metal Prior", &legacyTuningSettings.MetalNeutralPrior, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Cross-evidence Floor", &legacyTuningSettings.MetalCrossEvidenceFloor, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Metallic Diffuse Suppression", &legacyTuningSettings.MetallicDiffuseSuppression, 0.0f, 1.25f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

			ImGui::SeparatorText("Metal Color Classifier");
			ImGui::SliderFloat("Tint Chroma Low", &legacyTuningSettings.MetalTintChromaLow, 0.0f, 0.30f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Tint Chroma High", &legacyTuningSettings.MetalTintChromaHigh, 0.10f, 0.75f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Color Correlation Low", &legacyTuningSettings.MetalColorCorrelationLow, 0.30f, 0.95f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Color Correlation High", &legacyTuningSettings.MetalColorCorrelationHigh, 0.60f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

			if (ImGui::Button("Reset Legacy Conversion Calibration"))
				legacyTuningSettings = {};

			ImGui::TreePop();
		}

		ImGui::SeparatorText("Physical Light Quality");
		DrawUIntCheckbox("Inverse-Square Local Lights", settings.EnablePhysicalLocalLightFalloff);
		DrawTooltip("Blends local lights toward PIXL's regularized inverse-square attenuation. Disable to retain Skyrim/Natural Lighting falloff exactly.");
		if (settings.EnablePhysicalLocalLightFalloff != 0) {
			ImGui::SliderFloat("Physical Falloff Strength", &settings.PhysicalLocalLightFalloffStrength,
				0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			DrawTooltip("0.00 matches Skyrim/Natural Lighting, 1.00 is fully inverse-square. The balanced 0.65 default keeps physical depth without making interiors impractically dark.");
			ImGui::SliderFloat("Local Emitter Radius", &settings.LocalLightMinimumDistance,
				7.0f, 70.0f, "%.0f units", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
			DrawTooltip("Finite emitter radius used near the light to prevent an inverse-square singularity. Larger values produce broader, softer near-light response.");
		}
		DrawUIntCheckbox("Local Contact Shadows", settings.EnableLocalContactShadows);
		DrawTooltip("Traces a short, temporally stable screen-space visibility ray for the strongest local lights. Updates in real time; disable for the lowest GPU cost.");
		if (settings.EnableLocalContactShadows != 0) {
			int contactLightCount = static_cast<int>(settings.LocalContactShadowLightCount);
			if (ImGui::SliderInt("Contact-Shadow Lights / Pixel", &contactLightCount, 1, 2, "%d", ImGuiSliderFlags_AlwaysClamp))
				settings.LocalContactShadowLightCount = static_cast<uint>(std::clamp(contactLightCount, 1, 2));
			DrawTooltip("Number of strongest local lights eligible for a contact-shadow ray per pixel. Two is the bounded quality maximum.");
			ImGui::SliderFloat("Contact-Shadow Length", &settings.LocalContactShadowLength,
				16.0f, 256.0f, "%.0f units", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
			DrawTooltip("Maximum world-space reach of each local-light contact ray. Long rays cover more geometry but can expose screen-space misses.");
			ImGui::SliderFloat("Contact-Shadow Strength", &settings.LocalContactShadowStrength,
				0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			DrawTooltip("Opacity of accepted local contact shadows. Lower values blend more gently with Skyrim's existing shadowing.");
		}
		DrawUIntCheckbox("Geometric Specular AA", settings.EnableSpecularAA);
		DrawTooltip("Widens GGX roughness from screen-space normal variance to reduce shimmering on small or highly detailed geometry. Updates in real time.");
		if (settings.EnableSpecularAA != 0) {
			ImGui::SliderFloat("Specular AA Strength", &settings.SpecularAAStrength,
				0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			DrawTooltip("Scales the measured shading-normal variance before it is added to GGX roughness.");
			ImGui::SliderFloat("Specular AA Variance Clamp", &settings.SpecularAAVarianceClamp,
				0.01f, 0.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			DrawTooltip("Maximum roughness variance added by geometric anti-aliasing. Lower values preserve sharper highlights; higher values suppress more shimmer.");
		}
		DrawUIntCheckbox("Rough-Metal Multi-Scatter", settings.EnableGGXMultiScatter);
		DrawTooltip("Compensates energy lost by single-scatter GGX on rough conductors. Updates in real time and falls back to the original single-scatter response when disabled.");
		if (settings.EnableGGXMultiScatter != 0) {
			ImGui::SliderFloat("Multi-Scatter Strength", &settings.GGXMultiScatterStrength,
				0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			DrawTooltip("Blends between the legacy single-scatter lobe and the energy-compensated rough-conductor response.");
		}
		if (globals::state->IsDeveloperMode()) {
		static constexpr const char* debugModes[] = {
			"Off",
			"Adapter Coverage",
			"Base Color",
			"Roughness",
			"Metal / Dielectric Classification",
			"F0 (Sqrt Display)",
			"World Normal",
			"Direct Diffuse",
			"Direct Specular",
			"Physical vs Vanilla Delta (4x)",
			"Emissive",
			"Raw Authored Metalness",
			"Effective Metalness (Authored + Inferred)"
		};
		int debugMode = static_cast<int>(settings.LegacyPhysicalDebugMode);
		if (ImGui::Combo(T(TKEY("legacy_physical_debug_mode"), "Physical Material Debug"), &debugMode,
				debugModes, static_cast<int>(std::size(debugModes)))) {
			settings.LegacyPhysicalDebugMode = static_cast<uint>(std::max(debugMode, 0));
		}
		DrawTooltip("Selects a real-time material/lighting diagnostic. Off restores the normal composite; diagnostic modes do not require shader recompilation.");
		if (settings.LegacyPhysicalDebugMode == 1) {
			ImGui::TextDisabled("Coverage: green converted legacy | blue authored PBR | red bypassed");
		} else if (settings.LegacyPhysicalDebugMode == 4) {
			ImGui::TextDisabled("Classification: blue dielectric | conductor tint from authored or inferred metal/F0");
		} else if (settings.LegacyPhysicalDebugMode == 11) {
			ImGui::TextDisabled("Raw metalness: black dielectric/legacy | white authored Material Forge metal");
		} else if (settings.LegacyPhysicalDebugMode == 12) {
			ImGui::TextDisabled("Effective metalness: includes conservative legacy inference; black dielectric | white conductor");
		} else if (settings.LegacyPhysicalDebugMode == 3) {
			ImGui::TextDisabled("Roughness: black smooth | white rough");
		} else if (settings.LegacyPhysicalDebugMode == 5) {
			ImGui::TextDisabled("F0: square-root display preserves low dielectric reflectance without clipping");
		}
		if (settings.LegacyPhysicalDebugMode != 0) {
			ImGui::TextDisabled("Lighting geometry only; particles, flames, sky, and UI may remain visible");
		}
		if (ImGui::TreeNodeEx("Physical Material Registry", ImGuiTreeNodeFlags_None)) {
			const auto diagnostics = PhysicalMaterial::Registry::GetSingleton().GetDiagnostics();
			ImGui::Text("Generation: %llu", static_cast<unsigned long long>(diagnostics.generation));
			ImGui::Text("Materials: %zu  (legacy model %zu, metallic/roughness %zu)",
				diagnostics.materialCount,
				diagnostics.legacyMaterialCount,
				diagnostics.metallicRoughnessMaterialCount);
			ImGui::Text("Textures: %zu  (file %zu, runtime %zu)",
				diagnostics.textureCount,
				diagnostics.fileBackedTextureCount,
				diagnostics.runtimeTextureCount);
			ImGui::Text("Automatic fur descriptors: %zu", diagnostics.furMaterialCount);
			if (diagnostics.invalidDescriptorCount == 0) {
				ImGui::TextDisabled("Descriptor validation: OK");
			} else {
				ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f),
					"Descriptor validation: %zu invalid", diagnostics.invalidDescriptorCount);
			}
			DrawTooltip("Developer-only registry health view. It inspects backend-neutral material metadata and does not alter rendering or compile shader permutations.");
			ImGui::TreePop();
		}
		}
		ImGui::TreePop();
	}

	if (globals::state->IsDeveloperMode() && ImGui::TreeNodeEx(T(TKEY("texture_set_settings"), "Texture Set Settings"), ImGuiTreeNodeFlags_DefaultOpen)) {
		if (Util::SearchableCombo(T(TKEY("texture_set"), "Texture Set"), selectedPbrTextureSetName, pbrTextureSets)) {
			selectedPbrTextureSet = &pbrTextureSets[selectedPbrTextureSetName];
		}

		if (selectedPbrTextureSet != nullptr) {
			bool wasEdited = false;
			if (ImGui::SliderFloat(T(TKEY("displacement_scale"), "Displacement Scale"), &selectedPbrTextureSet->displacementScale, 0.f, 3.f, "%.3f")) {
				wasEdited = true;
			}
			if (ImGui::SliderFloat(T(TKEY("roughness_scale"), "Roughness Scale"), &selectedPbrTextureSet->roughnessScale, 0.f, 3.f, "%.3f")) {
				wasEdited = true;
			}
			if (ImGui::SliderFloat(T(TKEY("specular_level"), "Specular Level"), &selectedPbrTextureSet->specularLevel, 0.f, 3.f, "%.3f")) {
				wasEdited = true;
			}
			if (ImGui::TreeNodeEx(T(TKEY("subsurface"), "Subsurface"))) {
				if (ImGui::ColorPicker3(T(TKEY("subsurface_color"), "Subsurface Color"), &selectedPbrTextureSet->subsurfaceColor.red)) {
					wasEdited = true;
				}
				if (ImGui::SliderFloat(T(TKEY("subsurface_opacity"), "Subsurface Opacity"), &selectedPbrTextureSet->subsurfaceOpacity, 0.f, 1.f, "%.3f")) {
					wasEdited = true;
				}

				ImGui::TreePop();
			}
			if (ImGui::TreeNodeEx(T(TKEY("coat"), "Coat"))) {
				if (ImGui::ColorPicker3(T(TKEY("coat_color"), "Coat Color"), &selectedPbrTextureSet->coatColor.red)) {
					wasEdited = true;
				}
				if (ImGui::SliderFloat(T(TKEY("coat_strength"), "Coat Strength"), &selectedPbrTextureSet->coatStrength, 0.f, 1.f, "%.3f")) {
					wasEdited = true;
				}
				if (ImGui::SliderFloat(T(TKEY("coat_roughness"), "Coat Roughness"), &selectedPbrTextureSet->coatRoughness, 0.f, 1.f, "%.3f")) {
					wasEdited = true;
				}
				if (ImGui::SliderFloat(T(TKEY("coat_specular_level"), "Coat Specular Level"), &selectedPbrTextureSet->coatSpecularLevel, 0.f, 1.f, "%.3f")) {
					wasEdited = true;
				}
				if (ImGui::SliderFloat(T(TKEY("inner_layer_displacement_offset"), "Inner Layer Displacement Offset"), &selectedPbrTextureSet->innerLayerDisplacementOffset, 0.f, 3.f, "%.3f")) {
					wasEdited = true;
				}
				ImGui::TreePop();
			}
			if (ImGui::TreeNodeEx(T(TKEY("glint"), "Glint"))) {
				if (ImGui::Checkbox(T(TKEY("enabled"), "Enabled"), &selectedPbrTextureSet->glintParameters.enabled)) {
					wasEdited = true;
				}
				if (selectedPbrTextureSet->glintParameters.enabled) {
					if (ImGui::SliderFloat(T(TKEY("screenspace_scale"), "Screenspace Scale"), &selectedPbrTextureSet->glintParameters.screenSpaceScale, 0.f, 3.f, "%.3f")) {
						wasEdited = true;
					}
					if (ImGui::SliderFloat(T(TKEY("log_microfacet_density"), "Log Microfacet Density"), &selectedPbrTextureSet->glintParameters.logMicrofacetDensity, 0.f, 40.f, "%.3f")) {
						wasEdited = true;
					}
					if (ImGui::SliderFloat(T(TKEY("microfacet_roughness"), "Microfacet Roughness"), &selectedPbrTextureSet->glintParameters.microfacetRoughness, 0.f, 1.f, "%.3f")) {
						wasEdited = true;
					}
					if (ImGui::SliderFloat(T(TKEY("density_randomization"), "Density Randomization"), &selectedPbrTextureSet->glintParameters.densityRandomization, 0.f, 5.f, "%.3f")) {
						wasEdited = true;
					}
				}
				ImGui::TreePop();
			}
			if (wasEdited) {
				for (auto& [material, extensions] : BSLightingShaderMaterialPBR::All) {
					if (extensions.textureSetData == selectedPbrTextureSet) {
						material->ApplyTextureSetData(*extensions.textureSetData);
					}
				}
				for (auto& [material, textureSets] : BSLightingShaderMaterialPBRLandscape::All) {
					for (uint32_t textureSetIndex = 0; textureSetIndex < BSLightingShaderMaterialPBRLandscape::NumTiles; ++textureSetIndex) {
						if (textureSets[textureSetIndex] == selectedPbrTextureSet) {
							SetupPBRLandscapeTextureParameters(*material, *textureSets[textureSetIndex], textureSetIndex);
						}
					}
				}
			}
			if (selectedPbrTextureSet != nullptr) {
				if (ImGui::Button(T(TKEY("save"), "Save"))) {
					PNState::SavePBRRecordConfig("Data\\PBRTextureSets", selectedPbrTextureSetName, *selectedPbrTextureSet);
				}
			}
		}
		ImGui::TreePop();
	}

	if (globals::state->IsDeveloperMode() && ImGui::TreeNodeEx(T(TKEY("material_object_settings"), "Material Object Settings"), ImGuiTreeNodeFlags_DefaultOpen)) {
		if (Util::SearchableCombo(T(TKEY("material_object"), "Material Object"), selectedPbrMaterialObjectName, pbrMaterialObjects)) {
			selectedPbrMaterialObject = &pbrMaterialObjects[selectedPbrMaterialObjectName];
		}

		if (selectedPbrMaterialObject != nullptr) {
			bool wasEdited = false;
			if (ImGui::TreeNodeEx(T(TKEY("base_color_scale"), "Base Color Scale"), ImGuiTreeNodeFlags_DefaultOpen)) {
				auto resetBaseColorScaleLabel = std::string(T(TKEY("reset_to_1_0"), "Reset to 1.0")) + "##BaseColorScale";
				if (ImGui::Button(resetBaseColorScaleLabel.c_str())) {
					selectedPbrMaterialObject->baseColorScale = { 1.f, 1.f, 1.f };
					wasEdited = true;
				}

				const float indent = ImGui::GetCursorPosX();
				const float defaultItemWidth = ImGui::CalcItemWidth();
				const float letterColWidth = ImGui::CalcTextSize("Green").x + ImGui::GetStyle().ItemSpacing.x;
				const float sliderStartX = indent + letterColWidth;
				const float sliderWidth = defaultItemWidth - (sliderStartX - ImGui::GetStyle().ItemSpacing.x);
				const float colorLabelStartX = sliderStartX - ImGui::GetStyle().ItemSpacing.x - letterColWidth;

				ImGui::AlignTextToFramePadding();
				ImGui::SetCursorPosX(colorLabelStartX);
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
				ImGui::TextUnformatted(T(TKEY("red"), "Red"));
				ImGui::PopStyleColor();
				ImGui::SameLine(sliderStartX);
				ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.4f, 0.1f, 0.1f, 0.6f));
				ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
				ImGui::SetNextItemWidth(sliderWidth);
				if (ImGui::SliderFloat("##BaseColorScaleR", &selectedPbrMaterialObject->baseColorScale[0], 0.f, 10.f, "%.3f")) {
					wasEdited = true;
				}
				ImGui::PopStyleColor(2);

				ImGui::AlignTextToFramePadding();
				ImGui::SetCursorPosX(colorLabelStartX);
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.9f, 0.3f, 1.0f));
				ImGui::TextUnformatted(T(TKEY("green"), "Green"));
				ImGui::PopStyleColor();
				ImGui::SameLine(sliderStartX);
				ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1f, 0.4f, 0.1f, 0.6f));
				ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.3f, 0.9f, 0.3f, 1.0f));
				ImGui::SetNextItemWidth(sliderWidth);
				if (ImGui::SliderFloat("##BaseColorScaleG", &selectedPbrMaterialObject->baseColorScale[1], 0.f, 10.f, "%.3f")) {
					wasEdited = true;
				}
				ImGui::PopStyleColor(2);

				ImGui::AlignTextToFramePadding();
				ImGui::SetCursorPosX(colorLabelStartX);
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.3f, 0.9f, 1.0f));
				ImGui::TextUnformatted(T(TKEY("blue"), "Blue"));
				ImGui::PopStyleColor();
				ImGui::SameLine(sliderStartX);
				ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1f, 0.1f, 0.4f, 0.6f));
				ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.3f, 0.3f, 0.9f, 1.0f));
				ImGui::SetNextItemWidth(sliderWidth);
				if (ImGui::SliderFloat("##BaseColorScaleB", &selectedPbrMaterialObject->baseColorScale[2], 0.f, 10.f, "%.3f")) {
					wasEdited = true;
				}
				ImGui::PopStyleColor(2);

				ImGui::TreePop();
			}
			if (ImGui::SliderFloat(T(TKEY("roughness"), "Roughness"), &selectedPbrMaterialObject->roughness, 0.f, 1.f, "%.3f")) {
				wasEdited = true;
			}
			if (ImGui::SliderFloat(T(TKEY("material_specular_level"), "Specular Level"), &selectedPbrMaterialObject->specularLevel, 0.f, 1.f, "%.3f")) {
				wasEdited = true;
			}
			if (ImGui::TreeNodeEx(T(TKEY("material_glint"), "Glint"))) {
				if (ImGui::Checkbox(T(TKEY("material_glint_enabled"), "Enabled"), &selectedPbrMaterialObject->glintParameters.enabled)) {
					wasEdited = true;
				}
				if (selectedPbrMaterialObject->glintParameters.enabled) {
					if (ImGui::SliderFloat(T(TKEY("material_screenspace_scale"), "Screenspace Scale"), &selectedPbrMaterialObject->glintParameters.screenSpaceScale, 0.f, 3.f, "%.3f")) {
						wasEdited = true;
					}
					if (ImGui::SliderFloat(T(TKEY("material_log_microfacet_density"), "Log Microfacet Density"), &selectedPbrMaterialObject->glintParameters.logMicrofacetDensity, 0.f, 40.f, "%.3f")) {
						wasEdited = true;
					}
					if (ImGui::SliderFloat(T(TKEY("material_microfacet_roughness"), "Microfacet Roughness"), &selectedPbrMaterialObject->glintParameters.microfacetRoughness, 0.f, 1.f, "%.3f")) {
						wasEdited = true;
					}
					if (ImGui::SliderFloat(T(TKEY("material_density_randomization"), "Density Randomization"), &selectedPbrMaterialObject->glintParameters.densityRandomization, 0.f, 5.f, "%.3f")) {
						wasEdited = true;
					}
				}
				ImGui::TreePop();
			}
			if (wasEdited) {
				for (auto& [material, extensions] : BSLightingShaderMaterialPBR::All) {
					if (extensions.materialObjectData == selectedPbrMaterialObject) {
						material->ApplyMaterialObjectData(*extensions.materialObjectData);
					}
				}
			}
			if (selectedPbrMaterialObject != nullptr) {
				if (ImGui::Button(T(TKEY("material_save"), "Save"))) {
					PNState::SavePBRRecordConfig("Data\\PBRMaterialObjects", selectedPbrMaterialObjectName, *selectedPbrMaterialObject);
				}
			}
		}
		ImGui::TreePop();
	}
}

void MaterialForge::SaveSettings(json& o_json)
{
	o_json = settings;
	o_json["Legacy Conversion Tuning"] = legacyTuningSettings;
}

void MaterialForge::LoadSettings(json& o_json)
{
	settings = o_json;
	if (const auto it = o_json.find("Legacy Conversion Tuning"); it != o_json.end() && it->is_object())
		legacyTuningSettings = it->get<LegacyTuningSettings>();
	legacyTuningSettings.Magic = LegacyTuningMagic;
	legacyTuningSettings.Version = LegacyTuningVersion;
}

void MaterialForge::RestoreDefaultSettings()
{
	settings = {};
	legacyTuningSettings = {};
}

#undef I18N_KEY_PREFIX

void MaterialForge::SetupResources()
{
	logger::debug("[MaterialForge] SetupResources begin");
	SetupTextureSetData();
	SetupMaterialObjectData();

	if (!legacyTuningCB) {
		try {
			auto desc = ConstantBufferDesc<LegacyTuningSettings>();
			logger::debug(
				"[MaterialForge] Creating legacy tuning CB: struct={} bytes, D3D ByteWidth={} bytes",
				sizeof(LegacyTuningSettings),
				desc.ByteWidth);

			legacyTuningCB = new ConstantBuffer(desc, "MaterialForge::LegacyTuningCB");
			logger::debug("[MaterialForge] Legacy tuning CB created successfully");
		} catch (const std::exception& e) {
			logger::error("[MaterialForge] Legacy tuning CB creation failed: {}", e.what());
			legacyTuningCB = nullptr;
		} catch (...) {
			logger::error("[MaterialForge] Legacy tuning CB creation failed with unknown exception");
			legacyTuningCB = nullptr;
		}
	} else {
		logger::debug("[MaterialForge] Legacy tuning CB already exists");
	}

	logger::debug("[MaterialForge] SetupResources end");
}

void MaterialForge::Prepass()
{
	SetupDefaultPBRLandTextureSet();

	auto context = globals::d3d::context;
	if (legacyTuningCB) {
		try {
			legacyTuningSettings.Magic = LegacyTuningMagic;
			legacyTuningSettings.Version = LegacyTuningVersion;
			legacyTuningCB->Update(legacyTuningSettings);
			auto* legacyTuningBuffer = legacyTuningCB->CB();
			context->PSSetConstantBuffers(10, 1, &legacyTuningBuffer);
		} catch (const std::exception& e) {
			logger::error("[MaterialForge] b10 legacy tuning upload/bind failed: {}", e.what());
			delete legacyTuningCB;
			legacyTuningCB = nullptr;
		} catch (...) {
			logger::error("[MaterialForge] b10 legacy tuning upload/bind failed with unknown exception");
			delete legacyTuningCB;
			legacyTuningCB = nullptr;
		}
	}
	if (!glintsNoiseTexture)
		SetupGlintsTexture();
	ID3D11ShaderResourceView* srv = glintsNoiseTexture->srv.get();
	context->PSSetShaderResources(20, 1, &srv);
}

void MaterialForge::SetupGlintsTexture()
{
	constexpr uint noiseTexSize = 128;

	D3D11_TEXTURE2D_DESC tex_desc{
		.Width = noiseTexSize,
		.Height = noiseTexSize,
		.MipLevels = 1,
		.ArraySize = 1,
		.Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
		.SampleDesc = { .Count = 1, .Quality = 0 },
		.Usage = D3D11_USAGE_DEFAULT,
		.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
		.CPUAccessFlags = 0,
		.MiscFlags = 0
	};
	D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {
		.Format = tex_desc.Format,
		.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
		.Texture2D = {
			.MostDetailedMip = 0,
			.MipLevels = 1 }
	};
	D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {
		.Format = tex_desc.Format,
		.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
		.Texture2D = { .MipSlice = 0 }
	};

	glintsNoiseTexture = eastl::make_unique<Texture2D>(tex_desc);
	glintsNoiseTexture->CreateSRV(srv_desc);
	glintsNoiseTexture->CreateUAV(uav_desc);

	// Compile
	auto noiseGenProgram = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\Common\\Glints\\noisegen.cs.hlsl", {}, "cs_5_0"));
	if (!noiseGenProgram) {
		logger::error("Failed to compile glints noise generation shader!");
		return;
	}

	// Generate the noise
	{
		auto context = globals::d3d::context;

		struct OldState
		{
			ID3D11ComputeShader* shader;
			ID3D11UnorderedAccessView* uav[1];
			ID3D11ClassInstance* instance;
			UINT numInstances;
		};

		OldState newer{}, old{};
		context->CSGetShader(&old.shader, &old.instance, &old.numInstances);
		context->CSGetUnorderedAccessViews(0, ARRAYSIZE(old.uav), old.uav);

		{
			newer.uav[0] = glintsNoiseTexture->uav.get();
			context->CSSetShader(noiseGenProgram, nullptr, 0);
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(newer.uav), newer.uav, nullptr);
			context->Dispatch((noiseTexSize + 31) >> 5, (noiseTexSize + 31) >> 5, 1);
		}

		context->CSSetShader(old.shader, &old.instance, old.numInstances);
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(old.uav), old.uav, nullptr);

		// Release COM objects to prevent memory leaks
		if (old.shader)
			old.shader->Release();
		for (auto& uav : old.uav) {
			if (uav)
				uav->Release();
		}
	}

	noiseGenProgram->Release();
}

void MaterialForge::SetupTextureSetData()
{
	logger::info("[MaterialForge] loading PBR texture set configs");

	pbrTextureSets.clear();

	PNState::ReadPBRRecordConfigs("Data\\PBRTextureSets", [this](const std::string& editorId, const json& config) {
		try {
			pbrTextureSets.insert_or_assign(editorId, config);
		} catch (const std::exception& e) {
			logger::error("Failed to deserialize config for {}: {}.", editorId, e.what());
		}
	});
}

void MaterialForge::ReloadTextureSetData()
{
	logger::info("[MaterialForge] reloading PBR texture set configs");

	PNState::ReadPBRRecordConfigs("Data\\PBRTextureSets", [this](const std::string& editorId, const json& config) {
		try {
			if (auto it = pbrTextureSets.find(editorId); it != pbrTextureSets.cend()) {
				it->second = config;
			}
		} catch (const std::exception& e) {
			logger::error("Failed to deserialize config for {}: {}.", editorId, e.what());
		}
	});

	const auto& textureDefaults = globals::game::graphicsState->GetRuntimeData();
	for (const auto& [material, extensions] : BSLightingShaderMaterialPBR::All) {
		if (extensions.textureSetData != nullptr) {
			material->ApplyTextureSetData(*extensions.textureSetData);
		}
		if (extensions.materialObjectData != nullptr) {
			material->ApplyMaterialObjectData(*extensions.materialObjectData);
		}
		const auto descriptor = material->GetPhysicalMaterialDescriptor(
			textureDefaults.defaultTextureBlack.get(), textureDefaults.defaultTextureWhite.get(), true);
		PhysicalMaterial::Registry::GetSingleton().ObservePBR(*material, descriptor);
	}

	for (const auto& [material, textureSets] : BSLightingShaderMaterialPBRLandscape::All) {
		for (uint32_t textureSetIndex = 0; textureSetIndex < BSLightingShaderMaterialPBRLandscape::NumTiles; ++textureSetIndex) {
			SetupPBRLandscapeTextureParameters(*material, *textureSets[textureSetIndex], textureSetIndex);
			if (textureSetIndex < material->numLandscapeTextures) {
				const auto descriptor = material->GetPhysicalMaterialDescriptor(
					textureSetIndex, textureDefaults.defaultTextureBlack.get(), textureDefaults.defaultTextureWhite.get());
				PhysicalMaterial::Registry::GetSingleton().ObservePBRLandscape(
					*material, textureSetIndex, descriptor);
			}
		}
	}
}

MaterialForge::PBRTextureSetData* MaterialForge::GetPBRTextureSetData(const RE::TESForm* textureSet)
{
	if (textureSet == nullptr) {
		return nullptr;
	}

	auto it = pbrTextureSets.find(textureSet->GetFormEditorID());
	if (it == pbrTextureSets.end()) {
		return nullptr;
	}
	return &it->second;
}

bool MaterialForge::IsPBRTextureSet(const RE::TESForm* textureSet)
{
	return GetPBRTextureSetData(textureSet) != nullptr;
}

void MaterialForge::SetupMaterialObjectData()
{
	logger::info("[MaterialForge] loading PBR material object configs");

	pbrMaterialObjects.clear();

	PNState::ReadPBRRecordConfigs("Data\\PBRMaterialObjects", [this](const std::string& editorId, const json& config) {
		try {
			pbrMaterialObjects.insert_or_assign(editorId, config);
		} catch (const std::exception& e) {
			logger::error("Failed to deserialize config for {}: {}.", editorId, e.what());
		}
	});
}

MaterialForge::PBRMaterialObjectData* MaterialForge::GetPBRMaterialObjectData(const RE::TESForm* materialObject)
{
	if (materialObject == nullptr) {
		return nullptr;
	}

	auto it = pbrMaterialObjects.find(materialObject->GetFormEditorID());
	if (it == pbrMaterialObjects.end()) {
		return nullptr;
	}
	return &it->second;
}

bool MaterialForge::IsPBRMaterialObject(const RE::TESForm* materialObject)
{
	return GetPBRMaterialObjectData(materialObject) != nullptr;
}

namespace Permutations
{
	template <typename RangeType>
	std::unordered_set<uint32_t> GenerateFlagPermutations(const RangeType& flags, uint32_t constantFlags)
	{
		std::vector<uint32_t> flagValues;
		std::ranges::transform(flags, std::back_inserter(flagValues), [](auto flag) { return static_cast<uint32_t>(flag); });
		const uint32_t size = static_cast<uint32_t>(flagValues.size());

		std::unordered_set<uint32_t> result;
		for (uint32_t mask = 0; mask < (1u << size); ++mask) {
			uint32_t flag = constantFlags;
			for (size_t index = 0; index < size; ++index) {
				if (mask & (1 << index)) {
					flag |= flagValues[index];
				}
			}
			result.insert(flag);
		}

		return result;
	}

	uint32_t GetLightingShaderDescriptor(SIE::ShaderCache::LightingShaderTechniques technique, uint32_t flags)
	{
		return ((static_cast<uint32_t>(technique) & 0x3F) << 24) | flags;
	}

	void AddLightingShaderDescriptors(SIE::ShaderCache::LightingShaderTechniques technique, const std::unordered_set<uint32_t>& flags, std::unordered_set<uint32_t>& result)
	{
		for (uint32_t flag : flags) {
			result.insert(GetLightingShaderDescriptor(technique, flag));
		}
	}

	std::unordered_set<uint32_t> GeneratePBRLightingPixelPermutations()
	{
		using enum SIE::ShaderCache::LightingShaderFlags;

		constexpr std::array defaultFlags{ Deferred, AnisoLighting, Skinned, DoAlphaTest };
		constexpr std::array projectedUvFlags{ Deferred, AnisoLighting, DoAlphaTest, Snow };
		constexpr std::array lodObjectsFlags{ Deferred, WorldMap, DoAlphaTest, ProjectedUV };
		constexpr std::array treeFlags{ Deferred, AnisoLighting, Skinned, DoAlphaTest };
		constexpr std::array landFlags{ Deferred, AnisoLighting };

		constexpr uint32_t defaultConstantFlags = static_cast<uint32_t>(MaterialForge) | static_cast<uint32_t>(VC);
		constexpr uint32_t projectedUvConstantFlags = static_cast<uint32_t>(MaterialForge) | static_cast<uint32_t>(VC) | static_cast<uint32_t>(ProjectedUV);

		const std::unordered_set<uint32_t> defaultFlagValues = GenerateFlagPermutations(defaultFlags, defaultConstantFlags);
		const std::unordered_set<uint32_t> projectedUvFlagValues = GenerateFlagPermutations(projectedUvFlags, projectedUvConstantFlags);
		const std::unordered_set<uint32_t> lodObjectsFlagValues = GenerateFlagPermutations(lodObjectsFlags, defaultConstantFlags);
		const std::unordered_set<uint32_t> treeFlagValues = GenerateFlagPermutations(treeFlags, defaultConstantFlags);
		const std::unordered_set<uint32_t> landFlagValues = GenerateFlagPermutations(landFlags, defaultConstantFlags);

		std::unordered_set<uint32_t> result;
		AddLightingShaderDescriptors(SIE::ShaderCache::LightingShaderTechniques::None, defaultFlagValues, result);
		AddLightingShaderDescriptors(SIE::ShaderCache::LightingShaderTechniques::None, projectedUvFlagValues, result);
		AddLightingShaderDescriptors(SIE::ShaderCache::LightingShaderTechniques::LODObjects, lodObjectsFlagValues, result);
		AddLightingShaderDescriptors(SIE::ShaderCache::LightingShaderTechniques::LODObjectHD, lodObjectsFlagValues, result);
		AddLightingShaderDescriptors(SIE::ShaderCache::LightingShaderTechniques::TreeAnim, treeFlagValues, result);
		AddLightingShaderDescriptors(SIE::ShaderCache::LightingShaderTechniques::MTLand, landFlagValues, result);
		AddLightingShaderDescriptors(SIE::ShaderCache::LightingShaderTechniques::MTLandLODBlend, landFlagValues, result);
		return result;
	}
}

void MaterialForge::GenerateShaderPermutations(RE::BSShader* shader)
{
	auto state = globals::state;
	auto shaderCache = globals::shaderCache;
	if (shader->shaderType == RE::BSShader::Type::Lighting) {
		const auto pixelPermutations = Permutations::GeneratePBRLightingPixelPermutations();
		for (auto descriptor : pixelPermutations) {
			auto vertexShaderDesriptor = descriptor;
			auto pixelShaderDescriptor = descriptor;
			state->ModifyShaderLookup(*shader, vertexShaderDesriptor, pixelShaderDescriptor);
			std::ignore = shaderCache->GetPixelShader(*shader, pixelShaderDescriptor);
		}
	}
}

struct ExtendedRendererState
{
	static constexpr uint32_t NumPSTextures = 12;
	static constexpr uint32_t FirstPSTexture = 80;

	uint32_t PSResourceModifiedBits = 0;
	std::array<ID3D11ShaderResourceView*, NumPSTextures> PSTexture;

	void SetPSTexture(size_t textureIndex, RE::BSGraphics::Texture* newTexture)
	{
		ID3D11ShaderResourceView* resourceView = newTexture ? newTexture->resourceView : nullptr;
		//if (PSTexture[textureIndex] != resourceView)
		{
			PSTexture[textureIndex] = resourceView;
			PSResourceModifiedBits |= (1 << textureIndex);
		}
	}

	ExtendedRendererState()
	{
		std::fill(PSTexture.begin(), PSTexture.end(), nullptr);
	}
} extendedRendererState;

struct BSLightingShaderProperty_LoadBinary
{
	static void thunk(RE::BSLightingShaderProperty* property, RE::NiStream& stream)
	{
		using enum RE::BSShaderProperty::EShaderPropertyFlag;

		RE::BSShaderMaterial::Feature feature = RE::BSShaderMaterial::Feature::kDefault;
		stream.iStr->read(&feature, 1);

		{
			auto vtable = REL::Relocation<void***>(RE::NiShadeProperty::VTABLE[0]);
			auto baseMethod = reinterpret_cast<void (*)(RE::NiShadeProperty*, RE::NiStream&)>((vtable.get()[0x18]));
			baseMethod(property, stream);
		}

		stream.iStr->read(&property->flags, 1);

		bool isPbr = false;
		{
			RE::BSLightingShaderMaterialBase* material = nullptr;
			if (property->flags.any(kMenuScreen)) {
				auto* pbrMaterial = BSLightingShaderMaterialPBR::Make();
				pbrMaterial->inputFilePath = stream.inputFilePath;
				pbrMaterial->loadedWithFeature = feature;
				material = pbrMaterial;
				isPbr = true;
			} else {
				material = RE::BSLightingShaderMaterialBase::CreateMaterial(feature);
			}
			property->SetMaterial(nullptr, false);
			property->material = material;
		}

		{
			stream.iStr->read(&property->material->texCoordOffset[0].x, 1);
			stream.iStr->read(&property->material->texCoordOffset[0].y, 1);
			stream.iStr->read(&property->material->texCoordScale[0].x, 1);
			stream.iStr->read(&property->material->texCoordScale[0].y, 1);

			property->material->texCoordOffset[1] = property->material->texCoordOffset[0];
			property->material->texCoordScale[1] = property->material->texCoordScale[0];
		}

		stream.LoadLinkID();

		{
			RE::NiColor emissiveColor{};
			stream.iStr->read(&emissiveColor.red, 1);
			stream.iStr->read(&emissiveColor.green, 1);
			stream.iStr->read(&emissiveColor.blue, 1);

			if (property->emissiveColor != nullptr && property->flags.any(kOwnEmit)) {
				*property->emissiveColor = emissiveColor;
			}
		}

		stream.iStr->read(&property->emissiveMult, 1);

		static_cast<RE::BSLightingShaderMaterialBase*>(property->material)->LoadBinary(stream);

		if (isPbr) {
			auto pbrMaterial = static_cast<BSLightingShaderMaterialPBR*>(property->material);
			if (property->flags.any(kMultiLayerParallax)) {
				pbrMaterial->pbrFlags.set(PBRFlags::TwoLayer);
				if (property->flags.any(kSoftLighting)) {
					pbrMaterial->pbrFlags.set(PBRFlags::InterlayerParallax);
				}
				if (property->flags.any(kBackLighting)) {
					pbrMaterial->pbrFlags.set(PBRFlags::CoatNormal);
				}
				if (property->flags.any(kEffectLighting)) {
					pbrMaterial->pbrFlags.set(PBRFlags::ColoredCoat);
				}
			} else if (property->flags.any(kBackLighting)) {
				pbrMaterial->pbrFlags.set(PBRFlags::HairMarschner);
			} else {
				if (property->flags.any(kRimLighting)) {
					pbrMaterial->pbrFlags.set(PBRFlags::Subsurface);
				}
				if (property->flags.any(kSoftLighting)) {
					pbrMaterial->pbrFlags.set(PBRFlags::Fuzz);
				} else if (property->flags.any(kFitSlope)) {
					pbrMaterial->glintParameters.enabled = true;
				}
			}

			// It was a bad idea originally to use kMenuScreen flag to enable PBR since it's actually used for world map
			// meshes. But it was too late to move to use different flag so internally we use kVertexLighting instead
			// as it's not used for Lighting shader.
			property->flags.set(kVertexLighting);

			property->flags.reset(kMenuScreen, kSpecular, kGlowMap, kEnvMap, kMultiLayerParallax, kSoftLighting, kRimLighting, kBackLighting, kAnisotropicLighting, kEffectLighting, kFitSlope);
		} else {
			// There are some modded non-PBR meshes with kVertexLighting enabled.
			property->flags.reset(kVertexLighting);
		}
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct BSLightingShaderProperty_GetRenderPasses
{
	static RE::BSShaderProperty::RenderPassArray* thunk(RE::BSLightingShaderProperty* property, RE::BSGeometry* geometry, std::uint32_t renderFlags, RE::BSShaderAccumulator* accumulator)
	{
		auto renderPasses = func(property, geometry, renderFlags, accumulator);
		if (renderPasses == nullptr) {
			return renderPasses;
		}

		const auto issEnabledAndInteriorWithSun = globals::pipeline::interiorDaylight.loaded && globals::pipeline::interiorDaylight.isInteriorWithSun;

		bool isPbr = false;

		if (property->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kVertexLighting) && (property->material->GetFeature() == RE::BSShaderMaterial::Feature::kDefault || property->material->GetFeature() == RE::BSShaderMaterial::Feature::kMultiTexLandLODBlend)) {
			isPbr = true;
		}
		const char* geometryName = geometry != nullptr ? geometry->name.c_str() : nullptr;
		std::string geometryHierarchy;
		for (auto* object = static_cast<RE::NiAVObject*>(geometry); object != nullptr && geometryHierarchy.size() < 512u; object = object->parent) {
			const char* objectName = object->name.c_str();
			if (objectName != nullptr && *objectName != '\0') {
				if (!geometryHierarchy.empty())
					geometryHierarchy.push_back('\\');
				geometryHierarchy.append(objectName);
			}
		}
		const float automaticFurConfidence =
			!isPbr && property->material != nullptr
				? PhysicalMaterial::ClassifyAutomaticFur(
					  *static_cast<RE::BSLightingShaderMaterialBase*>(property->material),
					  geometryName != nullptr ? std::string_view(geometryName) : std::string_view{})
				: 0.0f;
		float automaticHairConfidence = property->material != nullptr
			? PhysicalMaterial::ClassifyAutomaticHair(
				  *static_cast<RE::BSLightingShaderMaterialBase*>(property->material),
				  geometryHierarchy)
			: 0.0f;
		const bool hairHasAlpha = geometry != nullptr &&
			geometry->GetGeometryRuntimeData().alphaProperty != nullptr;
		if (automaticHairConfidence > 0.0f && hairHasAlpha)
			automaticHairConfidence = std::min(1.0f, automaticHairConfidence + 0.03f);
		if (automaticHairConfidence > 0.0f &&
			property->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kBackLighting))
			automaticHairConfidence = std::min(1.0f, automaticHairConfidence + 0.02f);

		auto currentPass = renderPasses->head;
		while (currentPass != nullptr) {
			if (currentPass->shader->shaderType == RE::BSShader::Type::Lighting) {
				constexpr uint32_t LightingTechniqueStart = 0x4800002D;
				auto lightingTechnique = currentPass->passEnum - LightingTechniqueStart;
				auto lightingFlags = lightingTechnique & ~(~0u << 24);
				auto lightingType = static_cast<SIE::ShaderCache::LightingShaderTechniques>((lightingTechnique >> 24) & 0x3F);
				// Bits 3..7 are PIXL-owned descriptor metadata. Rebuild them from
				// authoritative material state so a recycled render pass cannot retain a
				// stale fur/hair classification after an equipment or hairstyle change.
				lightingFlags &= ~0b11111000u;
				const bool automaticFurEligible =
					automaticFurConfidence >= 0.80f &&
					(lightingFlags & static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::Skinned)) != 0 &&
					(lightingType == SIE::ShaderCache::LightingShaderTechniques::None ||
					 lightingType == SIE::ShaderCache::LightingShaderTechniques::TreeAnim);
				if (automaticFurEligible)
					lightingFlags |= static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::AutoFur);

				const bool nativeHair =
					lightingType == SIE::ShaderCache::LightingShaderTechniques::Hair;
				const bool automaticHairGeometryEligible =
					hairHasAlpha &&
					(lightingFlags & static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::Skinned)) != 0 &&
					lightingType == SIE::ShaderCache::LightingShaderTechniques::None;
				if (globals::pipeline::hairReconstruction.loaded) {
					if (nativeHair ||
						(automaticHairGeometryEligible && automaticHairConfidence >=
							globals::pipeline::hairReconstruction.settings.DetectionThreshold)) {
						lightingFlags |= static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::AutoHair);
					} else if (automaticHairGeometryEligible && automaticHairConfidence >= 0.40f) {
						lightingFlags |= static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::HairCandidate);
					}
				}
				if (isPbr) {
					lightingFlags |= static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::MaterialForge);
					lightingFlags &= ~static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::Specular);
					if (property->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kMultiTextureLandscape)) {
						auto* material = static_cast<BSLightingShaderMaterialPBRLandscape*>(property->material);
						if (material->HasGlint()) {
							lightingFlags |= static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::AnisoLighting);
						}
					} else {
						auto* material = static_cast<BSLightingShaderMaterialPBR*>(property->material);
						if (material->glintParameters.enabled || (property->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kProjectedUV) && material->projectedMaterialGlintParameters.enabled)) {
							lightingFlags |= static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::AnisoLighting);
						}
					}
				}

				if (issEnabledAndInteriorWithSun)
					lightingFlags |= static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::ShadowDir) | static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::DefShadow);

				lightingTechnique = (static_cast<uint32_t>(lightingType) << 24) | lightingFlags;
				currentPass->passEnum = lightingTechnique + LightingTechniqueStart;
			}
			currentPass = currentPass->next;
		}

		return renderPasses;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

bool MaterialForge::BSLightingShader_SetupMaterial(RE::BSLightingShader* shader, RE::BSLightingShaderMaterialBase const* material)
{
	using enum SIE::ShaderCache::LightingShaderTechniques;

	const auto& lightingPSConstants = ShaderConstants::LightingPS::Get();

	auto lightingFlags = shader->currentRawTechnique & ~(~0u << 24);
	auto lightingType = static_cast<SIE::ShaderCache::LightingShaderTechniques>((shader->currentRawTechnique >> 24) & 0x3F);
	const bool materialForgeDraw = !(lightingType == LODLand || lightingType == LODLandNoise) &&
	                         (lightingFlags & static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::MaterialForge));
	if (!materialForgeDraw) {
		PhysicalMaterial::Registry::GetSingleton().ObserveLegacy(*material);
	}
	if (materialForgeDraw) {
		auto shadowState = globals::game::shadowState;
		auto renderer = globals::game::renderer;
		auto graphicsState = globals::game::graphicsState;
		auto smState = globals::game::smState;

		RE::BSGraphics::Renderer::PrepareVSConstantGroup(RE::BSGraphics::ConstantGroupLevel::PerMaterial);
		RE::BSGraphics::Renderer::PreparePSConstantGroup(RE::BSGraphics::ConstantGroupLevel::PerMaterial);

		if (lightingType == MTLand || lightingType == MTLandLODBlend) {
			auto* pbrMaterial = static_cast<const BSLightingShaderMaterialPBRLandscape*>(material);
			std::array<PhysicalMaterial::Descriptor, BSLightingShaderMaterialPBRLandscape::NumTiles> physicalMaterials;
			for (std::uint32_t textureIndex = 0; textureIndex < physicalMaterials.size(); ++textureIndex) {
				physicalMaterials[textureIndex] = pbrMaterial->GetPhysicalMaterialDescriptor(
					textureIndex,
					graphicsState->GetRuntimeData().defaultTextureBlack.get(),
					graphicsState->GetRuntimeData().defaultTextureWhite.get());
				if (textureIndex < pbrMaterial->numLandscapeTextures) {
					PhysicalMaterial::Registry::GetSingleton().ObservePBRLandscape(
						*pbrMaterial, textureIndex, physicalMaterials[textureIndex]);
				}
			}

			constexpr size_t NormalStartIndex = 7;

			for (uint32_t textureIndex = 0; textureIndex < BSLightingShaderMaterialPBRLandscape::NumTiles; ++textureIndex) {
				if (pbrMaterial->landscapeBaseColorTextures[textureIndex] != nullptr) {
					shadowState->SetPSTexture(textureIndex, pbrMaterial->landscapeBaseColorTextures[textureIndex]->rendererTexture);
					shadowState->SetPSTextureAddressMode(textureIndex, RE::BSGraphics::TextureAddressMode::kWrapSWrapT);
					shadowState->SetPSTextureFilterMode(textureIndex, RE::BSGraphics::TextureFilterMode::kAnisotropic);
				}
				if (pbrMaterial->landscapeNormalTextures[textureIndex] != nullptr) {
					const uint32_t normalTextureIndex = NormalStartIndex + textureIndex;
					shadowState->SetPSTexture(normalTextureIndex, pbrMaterial->landscapeNormalTextures[textureIndex]->rendererTexture);
					shadowState->SetPSTextureAddressMode(normalTextureIndex, RE::BSGraphics::TextureAddressMode::kWrapSWrapT);
					shadowState->SetPSTextureFilterMode(normalTextureIndex, RE::BSGraphics::TextureFilterMode::kAnisotropic);
				}
				if (pbrMaterial->landscapeDisplacementTextures[textureIndex] != nullptr) {
					extendedRendererState.SetPSTexture(textureIndex, pbrMaterial->landscapeDisplacementTextures[textureIndex]->rendererTexture);
				}
				if (pbrMaterial->landscapeRMAOSTextures[textureIndex] != nullptr) {
					extendedRendererState.SetPSTexture(BSLightingShaderMaterialPBRLandscape::NumTiles + textureIndex, pbrMaterial->landscapeRMAOSTextures[textureIndex]->rendererTexture);
				}
			}

			if (pbrMaterial->terrainOverlayTexture != nullptr) {
				shadowState->SetPSTexture(13, pbrMaterial->terrainOverlayTexture->rendererTexture);
				shadowState->SetPSTextureAddressMode(13, RE::BSGraphics::TextureAddressMode::kClampSClampT);
				shadowState->SetPSTextureFilterMode(13, RE::BSGraphics::TextureFilterMode::kAnisotropic);
			}

			if (pbrMaterial->terrainNoiseTexture != nullptr) {
				shadowState->SetPSTexture(15, pbrMaterial->terrainNoiseTexture->rendererTexture);
				shadowState->SetPSTextureAddressMode(15, RE::BSGraphics::TextureAddressMode::kWrapSWrapT);
				shadowState->SetPSTextureFilterMode(15, RE::BSGraphics::TextureFilterMode::kBilinear);
			}

			{
				uint32_t flags = 0;
				for (uint32_t textureIndex = 0; textureIndex < BSLightingShaderMaterialPBRLandscape::NumTiles; ++textureIndex) {
					if (pbrMaterial->isPbr[textureIndex]) {
						flags |= (1 << textureIndex);
						if (physicalMaterials[textureIndex].HasTexture(PhysicalMaterial::Texture::Displacement)) {
							flags |= (1 << (BSLightingShaderMaterialPBRLandscape::NumTiles + textureIndex));
						}
						if (physicalMaterials[textureIndex].HasTrait(PhysicalMaterial::MaterialTrait::Glint)) {
							flags |= (1 << (2 * BSLightingShaderMaterialPBRLandscape::NumTiles + textureIndex));
						}
					}
				}
				shadowState->SetPSConstant(flags, RE::BSGraphics::ConstantGroupLevel::PerMaterial, lightingPSConstants.PBRFlags);
			}

			{
				const size_t PBRParamsStartIndex = lightingPSConstants.PBRParams1;
				const size_t GlintParametersStartIndex = lightingPSConstants.LandscapeTexture1GlintParameters;

				for (uint32_t textureIndex = 0; textureIndex < BSLightingShaderMaterialPBRLandscape::NumTiles; ++textureIndex) {
					std::array<float, 3> PBRParams;
					PBRParams[0] = physicalMaterials[textureIndex].roughnessScale;
					PBRParams[1] = physicalMaterials[textureIndex].displacementScale;
					PBRParams[2] = physicalMaterials[textureIndex].specularLevel;
					shadowState->SetPSConstant(PBRParams, RE::BSGraphics::ConstantGroupLevel::PerMaterial, PBRParamsStartIndex + textureIndex);

					std::array<float, 4> glintParameters;
					glintParameters[0] = physicalMaterials[textureIndex].glint.screenSpaceScale;
					glintParameters[1] = 40.f - physicalMaterials[textureIndex].glint.logMicrofacetDensity;
					glintParameters[2] = physicalMaterials[textureIndex].glint.microfacetRoughness;
					glintParameters[3] = physicalMaterials[textureIndex].glint.densityRandomization;
					shadowState->SetPSConstant(glintParameters, RE::BSGraphics::ConstantGroupLevel::PerMaterial, GlintParametersStartIndex + textureIndex);
				}
			}

			{
				std::array<float, 4> lodTexParams;
				lodTexParams[0] = pbrMaterial->terrainTexOffsetX;
				lodTexParams[1] = pbrMaterial->terrainTexOffsetY;
				lodTexParams[2] = 1.f;
				lodTexParams[3] = pbrMaterial->terrainTexFade;
				shadowState->SetPSConstant(lodTexParams, RE::BSGraphics::ConstantGroupLevel::PerMaterial, lightingPSConstants.LODTexParams);
			}
		} else if (lightingType == None || lightingType == TreeAnim) {
			auto* pbrMaterial = static_cast<const BSLightingShaderMaterialPBR*>(material);
			CHECK_PBR_TEXTURE(diffuseTexture);
			CHECK_PBR_TEXTURE(normalTexture);
			CHECK_PBR_TEXTURE(rmaosTexture);
			const bool projectedUVActive = (lightingFlags & static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::ProjectedUV)) != 0;
			const auto physicalMaterial = pbrMaterial->GetPhysicalMaterialDescriptor(
				graphicsState->GetRuntimeData().defaultTextureBlack.get(),
				graphicsState->GetRuntimeData().defaultTextureWhite.get(),
				projectedUVActive);
			PhysicalMaterial::Registry::GetSingleton().ObservePBR(*pbrMaterial, physicalMaterial);
			if (pbrMaterial->diffuseRenderTargetSourceIndex != -1) {
				shadowState->SetPSTexture(0, renderer->GetRuntimeData().renderTargets[pbrMaterial->diffuseRenderTargetSourceIndex]);
			} else {
				shadowState->SetPSTexture(0, pbrMaterial->diffuseTexture->rendererTexture);
			}
			shadowState->SetPSTextureAddressMode(0, static_cast<RE::BSGraphics::TextureAddressMode>(pbrMaterial->textureClampMode));
			shadowState->SetPSTextureFilterMode(0, RE::BSGraphics::TextureFilterMode::kAnisotropic);

			shadowState->SetPSTexture(1, pbrMaterial->normalTexture->rendererTexture);
			shadowState->SetPSTextureAddressMode(1, static_cast<RE::BSGraphics::TextureAddressMode>(pbrMaterial->textureClampMode));
			shadowState->SetPSTextureFilterMode(1, RE::BSGraphics::TextureFilterMode::kAnisotropic);

			shadowState->SetPSTexture(5, pbrMaterial->rmaosTexture->rendererTexture);
			shadowState->SetPSTextureAddressMode(5, static_cast<RE::BSGraphics::TextureAddressMode>(pbrMaterial->textureClampMode));
			shadowState->SetPSTextureFilterMode(5, RE::BSGraphics::TextureFilterMode::kAnisotropic);

			stl::enumeration<PBRShaderFlags> shaderFlags;
			if (physicalMaterial.HasTrait(PhysicalMaterial::MaterialTrait::TwoLayer)) {
				shaderFlags.set(PBRShaderFlags::TwoLayer);
				if (physicalMaterial.HasTrait(PhysicalMaterial::MaterialTrait::InterlayerParallax)) {
					shaderFlags.set(PBRShaderFlags::InterlayerParallax);
				}
				if (physicalMaterial.HasTrait(PhysicalMaterial::MaterialTrait::CoatNormal)) {
					shaderFlags.set(PBRShaderFlags::CoatNormal);
				}
				if (physicalMaterial.HasTrait(PhysicalMaterial::MaterialTrait::ColoredCoat)) {
					shaderFlags.set(PBRShaderFlags::ColoredCoat);
				}

				std::array<float, 4> PBRParams2;
				PBRParams2[0] = physicalMaterial.coatColor[0];
				PBRParams2[1] = physicalMaterial.coatColor[1];
				PBRParams2[2] = physicalMaterial.coatColor[2];
				PBRParams2[3] = physicalMaterial.coatStrength;
				shadowState->SetPSConstant(PBRParams2, RE::BSGraphics::ConstantGroupLevel::PerMaterial, lightingPSConstants.PBRParams2);

				std::array<float, 4> PBRParams3;
				PBRParams3[0] = physicalMaterial.coatRoughness;
				PBRParams3[1] = physicalMaterial.coatSpecularLevel;
				shadowState->SetPSConstant(PBRParams3, RE::BSGraphics::ConstantGroupLevel::PerMaterial, lightingPSConstants.MultiLayerParallaxData);
			} else if (physicalMaterial.HasTrait(PhysicalMaterial::MaterialTrait::HairMarschner)) {
				shaderFlags.set(PBRShaderFlags::HairMarschner);
			} else {
				if (physicalMaterial.HasTrait(PhysicalMaterial::MaterialTrait::Subsurface)) {
					shaderFlags.set(PBRShaderFlags::Subsurface);

					std::array<float, 4> PBRParams2;
					PBRParams2[0] = physicalMaterial.subsurfaceColor[0];
					PBRParams2[1] = physicalMaterial.subsurfaceColor[1];
					PBRParams2[2] = physicalMaterial.subsurfaceColor[2];
					PBRParams2[3] = physicalMaterial.subsurfaceOpacity;
					shadowState->SetPSConstant(PBRParams2, RE::BSGraphics::ConstantGroupLevel::PerMaterial, lightingPSConstants.PBRParams2);
				}
				if (physicalMaterial.HasTrait(PhysicalMaterial::MaterialTrait::Fuzz)) {
					shaderFlags.set(PBRShaderFlags::Fuzz);

					std::array<float, 4> PBRParams3;
					PBRParams3[0] = physicalMaterial.fuzzColor[0];
					PBRParams3[1] = physicalMaterial.fuzzColor[1];
					PBRParams3[2] = physicalMaterial.fuzzColor[2];
					PBRParams3[3] = physicalMaterial.fuzzWeight;
					shadowState->SetPSConstant(PBRParams3, RE::BSGraphics::ConstantGroupLevel::PerMaterial, lightingPSConstants.MultiLayerParallaxData);
				} else {
					if (physicalMaterial.HasTrait(PhysicalMaterial::MaterialTrait::Glint)) {
						shaderFlags.set(PBRShaderFlags::Glint);

						std::array<float, 4> GlintParameters;
						GlintParameters[0] = physicalMaterial.glint.screenSpaceScale;
						GlintParameters[1] = 40.f - physicalMaterial.glint.logMicrofacetDensity;
						GlintParameters[2] = physicalMaterial.glint.microfacetRoughness;
						GlintParameters[3] = physicalMaterial.glint.densityRandomization;
						shadowState->SetPSConstant(GlintParameters, RE::BSGraphics::ConstantGroupLevel::PerMaterial, lightingPSConstants.MultiLayerParallaxData);
					}
					if (physicalMaterial.HasTrait(PhysicalMaterial::MaterialTrait::ProjectedGlint)) {
						shaderFlags.set(PBRShaderFlags::ProjectedGlint);

						std::array<float, 4> ProjectedGlintParameters;
						ProjectedGlintParameters[0] = physicalMaterial.projectedGlint.screenSpaceScale;
						ProjectedGlintParameters[1] = 40.f - physicalMaterial.projectedGlint.logMicrofacetDensity;
						ProjectedGlintParameters[2] = physicalMaterial.projectedGlint.microfacetRoughness;
						ProjectedGlintParameters[3] = physicalMaterial.projectedGlint.densityRandomization;
						shadowState->SetPSConstant(ProjectedGlintParameters, RE::BSGraphics::ConstantGroupLevel::PerMaterial, lightingPSConstants.SparkleParams);
					}
				}
			}

			{
				std::array<float, 4> PBRProjectedUVParams1;
				PBRProjectedUVParams1[0] = physicalMaterial.projectedBaseColorScale[0];
				PBRProjectedUVParams1[1] = physicalMaterial.projectedBaseColorScale[1];
				PBRProjectedUVParams1[2] = physicalMaterial.projectedBaseColorScale[2];
				shadowState->SetPSConstant(PBRProjectedUVParams1, RE::BSGraphics::ConstantGroupLevel::PerMaterial, lightingPSConstants.MaterialObjectRGBScale);

				std::array<float, 4> PBRProjectedUVParams2;
				PBRProjectedUVParams2[0] = physicalMaterial.projectedRoughness;
				PBRProjectedUVParams2[1] = physicalMaterial.projectedSpecularLevel;
				shadowState->SetPSConstant(PBRProjectedUVParams2, RE::BSGraphics::ConstantGroupLevel::PerMaterial, lightingPSConstants.ParallaxOccData);
			}

			const bool hasEmissive = physicalMaterial.HasTexture(PhysicalMaterial::Texture::Emissive);
			if (hasEmissive) {
				shadowState->SetPSTexture(6, pbrMaterial->emissiveTexture->rendererTexture);
				shadowState->SetPSTextureAddressMode(6, static_cast<RE::BSGraphics::TextureAddressMode>(pbrMaterial->textureClampMode));
				shadowState->SetPSTextureFilterMode(6, RE::BSGraphics::TextureFilterMode::kAnisotropic);

				shaderFlags.set(PBRShaderFlags::HasEmissive);
			}

			const bool hasDisplacement = physicalMaterial.HasTexture(PhysicalMaterial::Texture::Displacement);
			if (hasDisplacement) {
				shadowState->SetPSTexture(4, pbrMaterial->displacementTexture->rendererTexture);
				shadowState->SetPSTextureAddressMode(4, static_cast<RE::BSGraphics::TextureAddressMode>(pbrMaterial->textureClampMode));
				shadowState->SetPSTextureFilterMode(4, RE::BSGraphics::TextureFilterMode::kAnisotropic);

				shaderFlags.set(PBRShaderFlags::HasDisplacement);
			}

			const bool hasFeaturesTexture0 = physicalMaterial.HasTexture(PhysicalMaterial::Texture::Features0);
			if (hasFeaturesTexture0) {
				shadowState->SetPSTexture(12, pbrMaterial->featuresTexture0->rendererTexture);
				shadowState->SetPSTextureAddressMode(12, static_cast<RE::BSGraphics::TextureAddressMode>(pbrMaterial->textureClampMode));
				shadowState->SetPSTextureFilterMode(12, RE::BSGraphics::TextureFilterMode::kAnisotropic);

				shaderFlags.set(PBRShaderFlags::HasFeaturesTexture0);
			}

			const bool hasFeaturesTexture1 = physicalMaterial.HasTexture(PhysicalMaterial::Texture::Features1);
			if (hasFeaturesTexture1) {
				shadowState->SetPSTexture(9, pbrMaterial->featuresTexture1->rendererTexture);
				shadowState->SetPSTextureAddressMode(9, static_cast<RE::BSGraphics::TextureAddressMode>(pbrMaterial->textureClampMode));
				shadowState->SetPSTextureFilterMode(9, RE::BSGraphics::TextureFilterMode::kAnisotropic);

				shaderFlags.set(PBRShaderFlags::HasFeaturesTexture1);
			}

			{
				shadowState->SetPSConstant(shaderFlags, RE::BSGraphics::ConstantGroupLevel::PerMaterial, lightingPSConstants.PBRFlags);
			}

			{
				std::array<float, 3> PBRParams1;
				PBRParams1[0] = physicalMaterial.roughnessScale;
				PBRParams1[1] = physicalMaterial.displacementScale;
				PBRParams1[2] = physicalMaterial.specularLevel;
				shadowState->SetPSConstant(PBRParams1, RE::BSGraphics::ConstantGroupLevel::PerMaterial, lightingPSConstants.PBRParams1);
			}
		}

		{
			const uint32_t bufferIndex = smState->textureTransformCurrentBuffer;

			std::array<float, 4> texCoordOffsetScale;
			texCoordOffsetScale[0] = material->texCoordOffset[bufferIndex].x;
			texCoordOffsetScale[1] = material->texCoordOffset[bufferIndex].y;
			texCoordOffsetScale[2] = material->texCoordScale[bufferIndex].x;
			texCoordOffsetScale[3] = material->texCoordScale[bufferIndex].y;
			shadowState->SetVSConstant(texCoordOffsetScale, RE::BSGraphics::ConstantGroupLevel::PerMaterial, 11);
		}

		if (lightingFlags & static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::CharacterLight)) {
			static const REL::Relocation<RE::ImageSpaceTexture*> characterLightTexture{ RELOCATION_ID(513464, 391302) };

			if (characterLightTexture->renderTarget >= RE::RENDER_TARGET::kFRAMEBUFFER) {
				shadowState->SetPSTexture(11, renderer->GetRuntimeData().renderTargets[characterLightTexture->renderTarget]);
				shadowState->SetPSTextureAddressMode(11, RE::BSGraphics::TextureAddressMode::kClampSClampT);
			}

			std::array<float, 4> characterLightParams{};  // in C++, arrays will be zero-initialized
			if (smState->characterLightEnabled) {
				std::copy_n(smState->characterLightParams, 4, characterLightParams.data());
			}
			shadowState->SetPSConstant(characterLightParams, RE::BSGraphics::ConstantGroupLevel::PerMaterial, lightingPSConstants.CharacterLightParams);
		}

		RE::BSGraphics::Renderer::FlushVSConstantGroup(RE::BSGraphics::ConstantGroupLevel::PerMaterial);
		RE::BSGraphics::Renderer::FlushPSConstantGroup(RE::BSGraphics::ConstantGroupLevel::PerMaterial);
		RE::BSGraphics::Renderer::ApplyVSConstantGroup(RE::BSGraphics::ConstantGroupLevel::PerMaterial);
		RE::BSGraphics::Renderer::ApplyPSConstantGroup(RE::BSGraphics::ConstantGroupLevel::PerMaterial);

		return true;
	}

	return false;
}

struct BSLightingShader_SetupGeometry
{
	static void thunk(RE::BSLightingShader* shader, RE::BSRenderPass* pass, uint32_t renderFlags)
	{
		const auto originalTechnique = shader->currentRawTechnique;
		if ((shader->currentRawTechnique & static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::MaterialForge)) != 0) {
			shader->currentRawTechnique |= static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::AmbientSpecular);
			shader->currentRawTechnique ^= static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::AnisoLighting);
		}

		shader->currentRawTechnique &= ~0b111000u;
		shader->currentRawTechnique |= (std::min((pass->numLights - 1), 7) << 3);

		func(shader, pass, renderFlags);

		shader->currentRawTechnique = originalTechnique;

		// b9/b10 are dedicated PIXL material tuning buffers. Skyrim applies its
		// per-draw pixel state inside SetupGeometry, so rebind after the original
		// function to make these settings authoritative for the draw.
		auto* context = globals::d3d::context;
		if (globals::pipeline::materialLayers.tuningCB) {
			ID3D11Buffer* materialLayersTuning = globals::pipeline::materialLayers.tuningCB->CB();
			context->PSSetConstantBuffers(9, 1, &materialLayersTuning);
		}
		if (globals::pipeline::materialForge.legacyTuningCB) {
			ID3D11Buffer* legacyTuning = globals::pipeline::materialForge.legacyTuningCB->CB();
			context->PSSetConstantBuffers(10, 1, &legacyTuning);
		}
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct BSLightingShader_GetPixelTechnique
{
	static uint32_t thunk(uint32_t rawTechnique)
	{
		uint32_t pixelTechnique = rawTechnique;

		const uint32_t pixlHairFlags =
			globals::pipeline::hairReconstruction.settings.Enabled != 0u
			? pixelTechnique &
				  (static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::AutoHair) |
				   static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::HairCandidate))
			: 0u;
		pixelTechnique &= ~0b111000000u;
		pixelTechnique |= pixlHairFlags;
		// Vanilla tangent-normal skinned draws normally collapse onto the matching
		// static pixel permutation because skinning itself is vertex-only. Actor
		// Surface Effects needs that distinction in the pixel shader so equipped
		// armour/clothing declares the character b13 payload and evaluates the same
		// actor-local mask as FaceGen/model-space-normal skin. Retain the bit only
		// while the module exists; the historical permutation collapse remains the
		// fallback if Actor Surface Effects fails to load.
		const bool retainSkinnedPixelPermutation =
			globals::pipeline::actorSurfaceEffects.loaded;
		if (!retainSkinnedPixelPermutation &&
			(pixelTechnique & static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::ModelSpaceNormals)) == 0) {
			pixelTechnique &= ~static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::Skinned);
		}
		pixelTechnique |= static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::VC);

		return pixelTechnique;
	}
};

void SetupPBRLandscapeTextureParameters(BSLightingShaderMaterialPBRLandscape& material, const MaterialForge::PBRTextureSetData& textureSetData, uint32_t textureIndex)
{
	material.displacementScales[textureIndex] = textureSetData.displacementScale;
	material.roughnessScales[textureIndex] = textureSetData.roughnessScale;
	material.specularLevels[textureIndex] = textureSetData.specularLevel;
	material.glintParameters[textureIndex] = textureSetData.glintParameters;
}

void SetupLandscapeTexture(BSLightingShaderMaterialPBRLandscape& material, RE::TESLandTexture& landTexture, uint32_t textureIndex, std::array<MaterialForge::PBRTextureSetData*, BSLightingShaderMaterialPBRLandscape::NumTiles>& textureSets)
{
	if (textureIndex >= 6) {
		return;
	}

	auto textureSet = Util::GetSeasonalSwap(landTexture.textureSet);
	if (textureSet == nullptr) {
		return;
	}

	auto materialForge = &globals::pipeline::materialForge;
	auto* textureSetData = materialForge->GetPBRTextureSetData(textureSet);
	const bool isPbr = textureSetData != nullptr;

	textureSets[textureIndex] = textureSetData;

	textureSet->SetTexture(BSLightingShaderMaterialPBRLandscape::BaseColorTexture, material.landscapeBaseColorTextures[textureIndex]);
	textureSet->SetTexture(BSLightingShaderMaterialPBRLandscape::NormalTexture, material.landscapeNormalTextures[textureIndex]);

	if (isPbr) {
		textureSet->SetTexture(BSLightingShaderMaterialPBRLandscape::RmaosTexture, material.landscapeRMAOSTextures[textureIndex]);
		textureSet->SetTexture(BSLightingShaderMaterialPBRLandscape::DisplacementTexture, material.landscapeDisplacementTextures[textureIndex]);
		SetupPBRLandscapeTextureParameters(material, *textureSetData, textureIndex);
	}
	material.isPbr[textureIndex] = isPbr;

	if (material.landscapeBaseColorTextures[textureIndex] != nullptr) {
		material.numLandscapeTextures = std::max(material.numLandscapeTextures, textureIndex + 1);
	}
}

RE::TESLandTexture* GetDefaultLandTexture()
{
	static const auto defaultLandTextureAddress = REL::Relocation<RE::TESLandTexture**>(RELOCATION_ID(514783, 400936));
	return *defaultLandTextureAddress;
}

bool MaterialForge::TESObjectLAND_SetupMaterial(RE::TESObjectLAND* land)
{
	if (land == nullptr) {
		return false;
	}

	auto singleton = &globals::pipeline::materialForge;

	bool isPbr = false;
	if (land->loadedData != nullptr) {
		for (uint32_t quadIndex = 0; quadIndex < 4; ++quadIndex) {
			if (land->loadedData->defQuadTextures[quadIndex] != nullptr) {
				if (singleton->IsPBRTextureSet(Util::GetSeasonalSwap(land->loadedData->defQuadTextures[quadIndex]->textureSet))) {
					isPbr = true;
					break;
				}
			} else if (singleton->defaultPbrLandTextureSet != nullptr) {
				isPbr = true;
			}
			for (uint32_t textureIndex = 0; textureIndex < 6; ++textureIndex) {
				if (land->loadedData->quadTextures[quadIndex][textureIndex] != nullptr) {
					if (singleton->IsPBRTextureSet(Util::GetSeasonalSwap(land->loadedData->quadTextures[quadIndex][textureIndex]->textureSet))) {
						isPbr = true;
						break;
					}
				}
			}
		}
	}

	if (!isPbr) {
		return false;
	}

	auto memoryManager = RE::MemoryManager::GetSingleton();

	if (land->loadedData != nullptr && land->loadedData->mesh[0] != nullptr) {
		land->data.flags.set(static_cast<RE::OBJ_LAND::Flag>(8));
		for (uint32_t quadIndex = 0; quadIndex < 4; ++quadIndex) {
			// PIXL_GROUND_SNOW_METADATA_V2_CAPTURE
			// The outer MaterialForge hook calls Skyrim's vanilla LAND setup first, so
			// the quad still owns the authoritative vanilla landscape material here.
			BSLightingShaderMaterialPBRLandscape::SnowMetadata snowMetadata{};
			if (auto* quadNode = land->loadedData->mesh[quadIndex]) {
				const auto& children = quadNode->GetChildren();
				auto* geometry = children.empty() ? nullptr : static_cast<RE::BSGeometry*>(children[0].get());
				if (geometry) {
					auto* oldProperty = static_cast<RE::BSLightingShaderProperty*>(
						geometry->GetGeometryRuntimeData().shaderProperty.get());
					if (oldProperty && oldProperty->material &&
						oldProperty->material->GetFeature() == RE::BSShaderMaterial::Feature::kMultiTexLandLODBlend) {
						auto* possiblePbr = reinterpret_cast<BSLightingShaderMaterialPBRLandscape*>(oldProperty->material);
						if (auto it = BSLightingShaderMaterialPBRLandscape::SnowMetadataByMaterial.find(possiblePbr);
							it != BSLightingShaderMaterialPBRLandscape::SnowMetadataByMaterial.end() && it->second.valid) {
							snowMetadata = it->second;
						} else if (!oldProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kVertexLighting)) {
							auto* vanillaLandscape = static_cast<RE::BSLightingShaderMaterialLandscape*>(oldProperty->material);
							for (uint32_t layer = 0; layer < BSLightingShaderMaterialPBRLandscape::NumTiles; ++layer) {
								const float snow = vanillaLandscape->textureIsSnow[layer];
								snowMetadata.textureIsSnow[layer] = std::isfinite(snow) ? std::clamp(snow, 0.0f, 1.0f) : 0.0f;
							}
							snowMetadata.valid = true;
						}
					}
				}
			}

			auto shaderProperty = static_cast<RE::BSLightingShaderProperty*>(memoryManager->Allocate(sizeof(RE::BSLightingShaderProperty), 0, false));
			shaderProperty->Ctor();

			{
				BSLightingShaderMaterialPBRLandscape srcMaterial;
				shaderProperty->SetMaterial(&srcMaterial, true);
			}

			auto material = static_cast<BSLightingShaderMaterialPBRLandscape*>(shaderProperty->material);
			// PIXL_GROUND_SNOW_METADATA_V2_ATTACH
			if (snowMetadata.valid) {
				BSLightingShaderMaterialPBRLandscape::SnowMetadataByMaterial.insert_or_assign(material, snowMetadata);
			} else {
				BSLightingShaderMaterialPBRLandscape::SnowMetadataByMaterial.erase(material);
			}
			const auto& stateData = globals::game::graphicsState->GetRuntimeData();

			for (uint32_t textureIndex = 0; textureIndex < BSLightingShaderMaterialPBRLandscape::NumTiles; ++textureIndex) {
				material->landscapeBaseColorTextures[textureIndex] = stateData.defaultTextureBlack;
				material->landscapeNormalTextures[textureIndex] = stateData.defaultTextureNormalMap;
				material->landscapeDisplacementTextures[textureIndex] = stateData.defaultTextureBlack;
				material->landscapeRMAOSTextures[textureIndex] = stateData.defaultTextureWhite;
			}

			auto& textureSets = BSLightingShaderMaterialPBRLandscape::All[material];

			if (auto defTexture = land->loadedData->defQuadTextures[quadIndex]) {
				SetupLandscapeTexture(*material, *defTexture, 0, textureSets);
			} else {
				SetupLandscapeTexture(*material, *GetDefaultLandTexture(), 0, textureSets);
			}
			for (uint32_t textureIndex = 0; textureIndex < BSLightingShaderMaterialPBRLandscape::NumTiles - 1; ++textureIndex) {
				if (auto landTexture = land->loadedData->quadTextures[quadIndex][textureIndex]) {
					SetupLandscapeTexture(*material, *landTexture, textureIndex + 1, textureSets);
				}
			}

			if (globals::game::bEnableLandFade->GetBool()) {
				shaderProperty->unk108 = false;
			}

			bool noLODLandBlend = false;
			auto tes = RE::TES::GetSingleton();
			auto worldSpace = tes->GetRuntimeData2().worldSpace;
			if (worldSpace != nullptr) {
				if (auto terrainManager = worldSpace->GetTerrainManager()) {
					noLODLandBlend = reinterpret_cast<bool*>(terrainManager)[0x36];
				}
			}
			shaderProperty->SetFlags(RE::BSShaderProperty::EShaderPropertyFlag8::kMultiTextureLandscape, true);
			shaderProperty->SetFlags(RE::BSShaderProperty::EShaderPropertyFlag8::kReceiveShadows, true);

			shaderProperty->SetFlags(RE::BSShaderProperty::EShaderPropertyFlag8::kCastShadows, true);
			shaderProperty->SetFlags(RE::BSShaderProperty::EShaderPropertyFlag8::kNoLODLandBlend, noLODLandBlend);

			shaderProperty->SetFlags(RE::BSShaderProperty::EShaderPropertyFlag8::kVertexLighting, true);

			const auto& children = land->loadedData->mesh[quadIndex]->GetChildren();
			auto geometry = children.empty() ? nullptr : static_cast<RE::BSGeometry*>(children[0].get());
			shaderProperty->SetupGeometry(geometry);
			if (geometry != nullptr) {
				geometry->GetGeometryRuntimeData().shaderProperty = RE::NiPointer(shaderProperty);
			}

			globals::game::smState->shadowSceneNode[0]->AttachObject(geometry);
		}

		return true;
	}

	return false;
}

struct TESForm_GetFormEditorID
{
	static const char* thunk(const RE::TESForm* form)
	{
		auto* singleton = &globals::pipeline::materialForge;
		auto it = singleton->editorIDs.find(form->GetFormID());
		if (it == singleton->editorIDs.cend()) {
			return "";
		}
		return it->second.c_str();
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct TESForm_SetFormEditorID
{
	static bool thunk(RE::TESForm* form, const char* editorId)
	{
		auto* singleton = &globals::pipeline::materialForge;
		singleton->editorIDs[form->GetFormID()] = editorId;
		return func(form, editorId);
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct BSTempEffectSimpleDecal_SetupGeometry
{
	static void thunk(RE::BSTempEffectSimpleDecal* decal, RE::BSGeometry* geometry, RE::BGSTextureSet* textureSet, bool blended)
	{
		func(decal, geometry, textureSet, blended);
		auto* singleton = &globals::pipeline::materialForge;
		auto unknownProperty = geometry->GetGeometryRuntimeData().shaderProperty.get();
		if (auto shaderProperty = unknownProperty->GetRTTI() == globals::rtti::BSLightingShaderPropertyRTTI.get() ? static_cast<RE::BSLightingShaderProperty*>(unknownProperty) : nullptr;
			shaderProperty != nullptr && singleton->IsPBRTextureSet(textureSet)) {
			{
				BSLightingShaderMaterialPBR srcMaterial;
				shaderProperty->SetMaterial(&srcMaterial, true);
			}

			auto pbrMaterial = static_cast<BSLightingShaderMaterialPBR*>(shaderProperty->material);
			pbrMaterial->OnLoadTextureSet(0, textureSet);

			constexpr static RE::NiColor whiteColor(1.f, 1.f, 1.f);
			*shaderProperty->emissiveColor = whiteColor;
			const bool hasEmissive = pbrMaterial->emissiveTexture != nullptr && pbrMaterial->emissiveTexture != globals::game::graphicsState->GetRuntimeData().defaultTextureBlack;
			shaderProperty->emissiveMult = hasEmissive ? 1.f : 0.f;

			{
				using enum RE::BSShaderProperty::EShaderPropertyFlag8;
				shaderProperty->SetFlags(kParallaxOcclusion, false);
				shaderProperty->SetFlags(kParallax, false);
				shaderProperty->SetFlags(kGlowMap, false);
				shaderProperty->SetFlags(kEnvMap, false);
				shaderProperty->SetFlags(kSpecular, false);

				shaderProperty->SetFlags(kVertexLighting, true);
			}
		}
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct BSTempEffectGeometryDecal_Initialize
{
	static void thunk(RE::BSTempEffectGeometryDecal* decal)
	{
		func(decal);
		auto* singleton = &globals::pipeline::materialForge;

		if (decal->decal != nullptr && singleton->IsPBRTextureSet(decal->texSet)) {
			auto shaderProperty = static_cast<RE::BSLightingShaderProperty*>(RE::MemoryManager::GetSingleton()->Allocate(sizeof(RE::BSLightingShaderProperty), 0, false));
			shaderProperty->Ctor();

			{
				BSLightingShaderMaterialPBR srcMaterial;
				shaderProperty->SetMaterial(&srcMaterial, true);
			}

			auto pbrMaterial = static_cast<BSLightingShaderMaterialPBR*>(shaderProperty->material);
			pbrMaterial->OnLoadTextureSet(0, decal->texSet);

			constexpr static RE::NiColor whiteColor(1.f, 1.f, 1.f);
			*shaderProperty->emissiveColor = whiteColor;
			const bool hasEmissive = pbrMaterial->emissiveTexture != nullptr && pbrMaterial->emissiveTexture != globals::game::graphicsState->GetRuntimeData().defaultTextureBlack;
			shaderProperty->emissiveMult = hasEmissive ? 1.f : 0.f;

			{
				using enum RE::BSShaderProperty::EShaderPropertyFlag8;

				shaderProperty->SetFlags(kSkinned, true);
				shaderProperty->SetFlags(kDynamicDecal, true);
				shaderProperty->SetFlags(kZBufferTest, true);
				shaderProperty->SetFlags(kZBufferWrite, false);

				shaderProperty->SetFlags(kVertexLighting, true);
			}

			if (auto* alphaProperty = static_cast<RE::NiAlphaProperty*>(decal->decal->GetGeometryRuntimeData().alphaProperty.get())) {
				alphaProperty->alphaFlags = (alphaProperty->alphaFlags & ~0x1FE) | 0xED;
			}

			shaderProperty->SetupGeometry(decal->decal.get());
			decal->decal->GetGeometryRuntimeData().shaderProperty = RE::NiPointer(shaderProperty);
		}
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct TESBoundObject_Clone3D
{
	static RE::NiAVObject* thunk(RE::TESBoundObject* object, RE::TESObjectREFR* ref, bool arg3)
	{
		auto materialForge = &globals::pipeline::materialForge;
		auto* result = func(object, ref, arg3);
		if (result != nullptr && ref != nullptr && ref->data.objectReference != nullptr && ref->data.objectReference->formType == RE::FormType::Static) {
			auto* stat = static_cast<RE::TESObjectSTAT*>(ref->data.objectReference);
			RE::BGSMaterialObject* currentMato = stat->data.materialObj;

			// Resolve PBR MATO data: non-null applies MATO to geometries with
			// fork-before-write protection; null means no PBR config and is a no-op.
			auto* pbrData = (currentMato != nullptr && currentMato->directionalData.singlePass) ? materialForge->GetPBRMaterialObjectData(currentMato) : nullptr;

			if (pbrData != nullptr) {
				RE::BSVisit::TraverseScenegraphGeometries(result, [pbrData, ref](RE::BSGeometry* geometry) {
					if (auto* shaderProperty = static_cast<RE::BSShaderProperty*>(geometry->GetGeometryRuntimeData().shaderProperty.get())) {
						if (shaderProperty->GetMaterialType() == RE::BSShaderMaterial::Type::kLighting &&
							shaderProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kVertexLighting)) {
							if (auto* material = static_cast<BSLightingShaderMaterialPBR*>(shaderProperty->material)) {
								auto& ext = BSLightingShaderMaterialPBR::All[material];
								const auto prevOwnerRefID = ext.lastOwnerRefFormID;

								// Fork-before-write: if this material instance is already owned
								// by a different ref whose MATO payload differs from the incoming
								// one, clone it so we don't contaminate the previous owner's
								// geometry.  Use pointer identity: GetPBRMaterialObjectData
								// returns stable addresses into pbrMaterialObjects, so two
								// different MATOs always produce different pointers regardless of
								// whether their individual fields (baseColorScale, roughness,
								// specularLevel, glint) happen to match.
								const bool wouldContaminate =
									(prevOwnerRefID != 0) &&
									(prevOwnerRefID != ref->GetFormID()) &&
									(ext.materialObjectData != pbrData);

								BSLightingShaderMaterialPBR* targetMat = material;

								if (wouldContaminate) {
									auto* freshMat = BSLightingShaderMaterialPBR::Make();
									if (freshMat) {
										freshMat->CopyMembers(material);
										shaderProperty->material = freshMat;
										targetMat = freshMat;
									} else {
										logger::warn("[MaterialForge] failed to clone PBR material for ref {:08X}; skipping to avoid contamination", ref->GetFormID());
										return RE::BSVisit::BSVisitControl::kContinue;
									}
								}

								targetMat->ApplyMaterialObjectData(*pbrData);
								auto& targetExt = BSLightingShaderMaterialPBR::All[targetMat];
								targetExt.materialObjectData = pbrData;
								targetExt.lastOwnerRefFormID = ref->GetFormID();
							}
						}
					}

					return RE::BSVisit::BSVisitControl::kContinue;
				});
			}
		}
		return result;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct BGSTextureSet_ToShaderTextureSet
{
	static RE::BSShaderTextureSet* thunk(RE::BGSTextureSet* textureSet)
	{
		auto materialForge = &globals::pipeline::materialForge;
		materialForge->currentTextureSet = textureSet;

		return func(textureSet);
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct BSLightingShaderProperty_OnLoadTextureSet
{
	static void thunk(RE::BSLightingShaderProperty* property, void* a2)
	{
		func(property, a2);

		auto materialForge = &globals::pipeline::materialForge;
		materialForge->currentTextureSet = nullptr;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct PBR_TESObjectLAND_SetupMaterial
{
	static bool thunk(RE::TESObjectLAND* land)
	{
		bool vanillaResult = func(land);

		if (globals::pipeline::materialForge.TESObjectLAND_SetupMaterial(land)) {
			return true;
		}

		return vanillaResult;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct PBR_BSLightingShader_SetupMaterial
{
	static void thunk(RE::BSLightingShader* shader, RE::BSLightingShaderMaterialBase const* material)
	{
		if (globals::pipeline::materialForge.BSLightingShader_SetupMaterial(shader, material)) {
			return;
		}

		func(shader, material);
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

void MaterialForge::PostPostLoad()
{
	logger::info("[MaterialForge] Hooking BGSTextureSet");
	stl::detour_thunk<BGSTextureSet_ToShaderTextureSet>(REL::RelocationID(20905, 21361));

	logger::info("[MaterialForge] Hooking BSLightingShaderProperty");
	stl::write_vfunc<0x18, BSLightingShaderProperty_LoadBinary>(RE::VTABLE_BSLightingShaderProperty[0]);
	stl::write_vfunc<0x2A, BSLightingShaderProperty_GetRenderPasses>(RE::VTABLE_BSLightingShaderProperty[0]);
	stl::detour_thunk<BSLightingShaderProperty_OnLoadTextureSet>(REL::RelocationID(99865, 106510));

	logger::info("[MaterialForge] Hooking BSLightingShader");
	stl::write_vfunc<0x6, BSLightingShader_SetupGeometry>(RE::VTABLE_BSLightingShader[0]);
	stl::detour_thunk_ignore_func<BSLightingShader_GetPixelTechnique>(REL::RelocationID(101633, 108700));

	logger::info("[MaterialForge] Hooking TESLandTexture");
	stl::write_vfunc<0x32, TESForm_GetFormEditorID>(RE::VTABLE_TESLandTexture[0]);
	stl::write_vfunc<0x33, TESForm_SetFormEditorID>(RE::VTABLE_TESLandTexture[0]);
	stl::write_vfunc<0x32, TESForm_GetFormEditorID>(RE::VTABLE_BGSTextureSet[0]);
	stl::write_vfunc<0x33, TESForm_SetFormEditorID>(RE::VTABLE_BGSTextureSet[0]);
	stl::write_vfunc<0x32, TESForm_GetFormEditorID>(RE::VTABLE_BGSMaterialObject[0]);
	stl::write_vfunc<0x33, TESForm_SetFormEditorID>(RE::VTABLE_BGSMaterialObject[0]);
	stl::write_vfunc<0x32, TESForm_GetFormEditorID>(RE::VTABLE_BGSLightingTemplate[0]);
	stl::write_vfunc<0x33, TESForm_SetFormEditorID>(RE::VTABLE_BGSLightingTemplate[0]);
	stl::write_vfunc<0x32, TESForm_GetFormEditorID>(RE::VTABLE_TESWeather[0]);
	stl::write_vfunc<0x33, TESForm_SetFormEditorID>(RE::VTABLE_TESWeather[0]);

	logger::info("[MaterialForge] Hooking BSTempEffectSimpleDecal");
	stl::detour_thunk<BSTempEffectSimpleDecal_SetupGeometry>(REL::RelocationID(29253, 30108));

	logger::info("[MaterialForge] Hooking BSTempEffectGeometryDecal");
	stl::write_vfunc<0x25, BSTempEffectGeometryDecal_Initialize>(RE::VTABLE_BSTempEffectGeometryDecal[0]);

	logger::info("[MaterialForge] Hooking TESObjectSTAT");
	stl::write_vfunc<0x4A, TESBoundObject_Clone3D>(RE::VTABLE_TESObjectSTAT[0]);

	logger::info("[MaterialForge] Hooking TESObjectLAND");
	stl::detour_thunk<PBR_TESObjectLAND_SetupMaterial>(REL::RelocationID(18368, 18791));

	logger::info("[MaterialForge] Hooking BSLightingShader::SetupMaterial");
	stl::write_vfunc<0x4, PBR_BSLightingShader_SetupMaterial>(RE::VTABLE_BSLightingShader[0]);
}

void MaterialForge::DataLoaded()
{
	defaultPbrLandTextureSet = RE::TESForm::LookupByEditorID<RE::BGSTextureSet>("DefaultPBRLand");
	SetupDefaultPBRLandTextureSet();
}

void MaterialForge::SetupDefaultPBRLandTextureSet()
{
	if (!defaultLandTextureSetReplaced && defaultPbrLandTextureSet != nullptr) {
		if (auto* defaultLandTexture = GetDefaultLandTexture()) {
			logger::info("[MaterialForge] replacing default land texture set record with {}", defaultPbrLandTextureSet->GetFormEditorID());
			defaultLandTexture->textureSet = defaultPbrLandTextureSet;
			defaultLandTextureSetReplaced = true;
		}
	}
}

void MaterialForge::SetShaderResources(ID3D11DeviceContext* a_context)
{
	uint32_t mask = extendedRendererState.PSResourceModifiedBits;

	if (mask == 0) [[likely]] {
		// No dirty slots, early exit
		return;
	}

	constexpr uint32_t firstTexture = ExtendedRendererState::FirstPSTexture;
	auto& textures = extendedRendererState.PSTexture;

	while (mask) {
		// Find index of the least significant set bit
		uint32_t batchStart = std::countr_zero(mask);

		// Check for consecutive set bits and batch them
		uint32_t shiftedMask = mask >> batchStart;
		uint32_t batchCount = std::countr_one(shiftedMask);

		// Issue one API call for this batch
		a_context->PSSetShaderResources(
			firstTexture + batchStart,
			batchCount,
			&textures[batchStart]);

		// Clear the bits we just processed
		uint32_t clearMask = ((1u << batchCount) - 1u) << batchStart;
		mask &= ~clearMask;
	}

	// Reset modified bits
	extendedRendererState.PSResourceModifiedBits = 0;
}
