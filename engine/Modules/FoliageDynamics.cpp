#include "FoliageDynamics.h"

#include "I18n/I18n.h"

#define I18N_KEY_PREFIX "feature.foliage_dynamics."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	FoliageDynamics::Settings,
	Glossiness,
	SpecularStrength,
	TissueDiffusionAmount,
	OverrideComplexGrassSettings,
	BasicGrassBrightness,
	ComplexGrassThreshold,
	TreeFlipNormalY,
	GrassMacroSpecular,
	EnableEnhancedVegetation,
	EnableEnhancedWind,
	LeafTransmission,
	LeafDiffuseWrap,
	WindStrength,
	GustStrength,
	FlutterStrength,
	WindSpatialScale,
	GustSpeed,
	FlutterSpeed,
	SpecularAA,
	ComplexGrassMode)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	FoliageDynamics::TuningSettings,
	EnableGrassAlphaControl,
	GrassFlipNormalX,
	GrassFlipNormalY,
	GrassNormalStrength,
	GrassCardNormalBlend,
	GrassAlphaCoverage,
	GrassCutoutBias,
	GrassAlphaPower,
	GrassEdgeDither,
	GrassSaturation,
	GrassContrast,
	GrassWetSpecularBoost,
	GrassTransmissionBoost,
	GrassLocalLightBoost,
	GrassDetailDistanceScale,
	GrassDetailTransitionSoftness,
	GrassSpecularNormalization,
	GrassComplexSpecularMapInfluence,
	GrassMirrorSpecularY)

void FoliageDynamics::SetupResources()
{
	if (tuningCB)
		return;

	try {
		auto desc = ConstantBufferDesc<TuningSettings>();
		logger::info(
			"[FoliageDynamics] Creating grass tuning CB: struct={} bytes, D3D ByteWidth={} bytes",
			sizeof(TuningSettings), desc.ByteWidth);
		tuningCB = new ConstantBuffer(desc, "FoliageDynamics::TuningCB");
	} catch (const std::exception& e) {
		logger::error("[FoliageDynamics] Grass tuning CB creation failed: {}", e.what());
		tuningCB = nullptr;
	} catch (...) {
		logger::error("[FoliageDynamics] Grass tuning CB creation failed with unknown exception");
		tuningCB = nullptr;
	}
}

void FoliageDynamics::Prepass()
{
	if (!tuningCB)
		return;

	try {
		tuningSettings.Magic = TuningMagic;
		tuningSettings.Version = TuningVersion;
		tuningCB->Update(tuningSettings);
		BindGrassTuning();
	} catch (const std::exception& e) {
		logger::error("[FoliageDynamics] b13 grass tuning upload/bind failed: {}", e.what());
		delete tuningCB;
		tuningCB = nullptr;
	} catch (...) {
		logger::error("[FoliageDynamics] b13 grass tuning upload/bind failed with unknown exception");
		delete tuningCB;
		tuningCB = nullptr;
	}
}

void FoliageDynamics::BindGrassTuning() const
{
	if (!tuningCB)
		return;

	auto* buffer = tuningCB->CB();
	if (auto* context = globals::d3d::context; context && buffer)
		context->PSSetConstantBuffers(13, 1, &buffer);
}

void FoliageDynamics::DrawSettings()
{
	if (ImGui::TreeNodeEx(T(TKEY("vegetation_model"), "PIXL Vegetation Model"), ImGuiTreeNodeFlags_DefaultOpen)) {
		Util::UIntCheckbox(T(TKEY("enable_enhanced_vegetation"), "Enhanced Vegetation Lighting (No Motion)"), &settings.EnableEnhancedVegetation);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", T(TKEY("enable_enhanced_vegetation_tooltip"), "Controls grass/leaf lighting only: energy-aware GGX, wrapped diffuse light, transmission and specular anti-aliasing. It never enables or scales wind motion."));
		ImGui::BeginDisabled(settings.EnableEnhancedVegetation == 0);
		ImGui::SliderFloat(T(TKEY("leaf_transmission"), "Leaf Transmission"), &settings.LeafTransmission, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", T(TKEY("leaf_transmission_tooltip"), "Controls colored sunlight transmitted through grass blades and animated leaves."));
		ImGui::SliderFloat(T(TKEY("leaf_diffuse_wrap"), "Diffuse Wrap"), &settings.LeafDiffuseWrap, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", T(TKEY("leaf_diffuse_wrap_tooltip"), "Softens the light terminator on thin vegetation while conserving average brightness."));
		ImGui::SliderFloat(T(TKEY("vegetation_specular_aa"), "Specular Anti-Aliasing"), &settings.SpecularAA, 0.0f, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", T(TKEY("vegetation_specular_aa_tooltip"), "Suppresses shimmering highlights from detailed foliage normals at distance."));
		ImGui::SliderFloat("Grass Card Specular Coherence", &settings.GrassMacroSpecular, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Blends direct/wet grass reflections toward the actual rasterized card plane. High values make both triangles of a grass quad share one coherent reflection instead of isolated corner glints.");
		ImGui::EndDisabled();
		Util::UIntCheckbox("Flip Tree Normal Y", &settings.TreeFlipNormalY);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Flips only animated-tree tangent-space normal maps. Grass has a separate format/convention control below.");
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx(T(TKEY("vegetation_wind"), "Vegetation Wind"), ImGuiTreeNodeFlags_DefaultOpen)) {
		Util::UIntCheckbox(T(TKEY("enable_enhanced_wind"), "Natural Grass Gusts"), &settings.EnableEnhancedWind);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", T(TKEY("enable_enhanced_wind_tooltip"), "Adds one bounded world-stable travelling gust and restrained tip flutter to ordinary terrain grass. Skyrim's authored tree and grass motion remains the structural baseline; previous-frame deformation stays deterministic for TAA/DLSS."));
		ImGui::BeginDisabled(settings.EnableEnhancedWind == 0);
		ImGui::SliderFloat(T(TKEY("wind_strength"), "Wind Response"), &settings.WindStrength, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", T(TKEY("wind_strength_tooltip"), "Overall response of PIXL's added motion. 0 is exact vanilla movement; real weather dominates, with a restrained ambient floor so ordinary terrain grass does not remain rigid."));
		ImGui::SliderFloat(T(TKEY("gust_strength"), "Gust Strength"), &settings.GustStrength, 0.0f, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", T(TKEY("gust_strength_tooltip"), "Amplitude of broad, spatially coherent gusts."));
		ImGui::SliderFloat(T(TKEY("flutter_strength"), "Leaf Flutter"), &settings.FlutterStrength, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", T(TKEY("flutter_strength_tooltip"), "Fine cross-wind movement at blade and leaf tips. Keep modest to avoid noisy distant foliage."));
		ImGui::SliderFloat(T(TKEY("wind_spatial_scale"), "Gust Size"), &settings.WindSpatialScale, 0.25f, 3.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", T(TKEY("wind_spatial_scale_tooltip"), "Spatial scale of gust cells. Lower values produce broader coordinated motion."));
		ImGui::SliderFloat(T(TKEY("gust_speed"), "Gust Speed"), &settings.GustSpeed, 0.25f, 2.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", T(TKEY("gust_speed_tooltip"), "Travel speed of broad wind pulses."));
		ImGui::SliderFloat(T(TKEY("flutter_speed"), "Flutter Speed"), &settings.FlutterSpeed, 0.25f, 3.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", T(TKEY("flutter_speed_tooltip"), "Frequency multiplier for fine leaf and grass-tip motion."));
		ImGui::EndDisabled();
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Grass Material Controls")) {
		ImGui::TextWrapped("Fine control over grass card normals, alpha coverage and world-fit response. These settings use a dedicated b13 buffer and do not expand FeatureData.");

		ImGui::SeparatorText("Normals");
		Util::UIntCheckbox("Flip Grass Normal X", &tuningSettings.GrassFlipNormalX);
		Util::UIntCheckbox("Flip Grass Normal Y", &tuningSettings.GrassFlipNormalY);
		Util::UIntCheckbox("Mirror Complex Normal Y (Specular)", &tuningSettings.GrassMirrorSpecularY);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Adds an energy-preserving second GGX lobe from the packed complex normal mirrored across tangent Y. This gives opposite blade/card directions a shared distant sheen without changing diffuse lighting or SSS. Use Specular Strength or Normalized GGX Response to reduce the final intensity.");
		ImGui::SliderFloat("Grass Normal Strength", &tuningSettings.GrassNormalStrength, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Blend Normal Toward Card", &tuningSettings.GrassCardNormalBlend, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("0 keeps the packed/detail normal. 1 uses the geometric grass-card normal. Useful when a grass normal map is too lumpy or points in the wrong visual direction.");
		ImGui::SliderFloat("Complex Normal Filter Distance", &tuningSettings.GrassDetailDistanceScale, 0.50f, 2.00f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Controls where distant packed complex-grass normals begin receiving extra mip filtering. It no longer fades normal amplitude or creates a near/far lighting ring. This updates in real time and does not rebuild shaders.");
		ImGui::SliderFloat("Complex Normal Filter Softness", &tuningSettings.GrassDetailTransitionSoftness, 0.50f, 2.00f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Widens the continuous mip-filtering interval. Higher values transition more gradually without changing the packed normal's strength.");

		ImGui::SeparatorText("Alpha / Cutout");
		Util::UIntCheckbox("Override Grass Alpha Cutout", &tuningSettings.EnableGrassAlphaControl);
		ImGui::BeginDisabled(tuningSettings.EnableGrassAlphaControl == 0);
		ImGui::SliderFloat("Alpha Coverage", &tuningSettings.GrassAlphaCoverage, 0.25f, 2.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Cutout Bias", &tuningSettings.GrassCutoutBias, -0.35f, 0.35f, "%+.3f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Alpha Shape", &tuningSettings.GrassAlphaPower, 0.25f, 4.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Edge Dither", &tuningSettings.GrassEdgeDither, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("This is grass coverage/cutout control, not true deferred transparency. It is independent from both vegetation lighting and wind. Negative Cutout Bias and higher Alpha Coverage make blades fuller; positive bias thins them. Edge Dither gives a softer stochastic transition without changing the blend state.");
		ImGui::EndDisabled();

		ImGui::SeparatorText("World Fit");
		ImGui::SliderFloat("Grass Saturation", &tuningSettings.GrassSaturation, 0.0f, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Grass Contrast", &tuningSettings.GrassContrast, 0.5f, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Wet Specular Boost", &tuningSettings.GrassWetSpecularBoost, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Normalized GGX Response", &tuningSettings.GrassSpecularNormalization, 0.0f, 4.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Scales PIXL's energy-normalized GGX foliage response after Skyrim/PBR light calibration. 1.0 is calibrated; 2-4x is an intentional artistic backlit-blade boost and remains separate from Specular Strength.");
		ImGui::SliderFloat("Complex Specular Map Influence", &tuningSettings.GrassComplexSpecularMapInfluence, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Controls how strongly the packed complex-grass alpha channel modulates reflection. PIXL defaults to 0.15 so an author mask can add variation but cannot restrict the whole GGX response to blade tips. 0 gives uniform material response; 1 uses the authored channel exactly.");
		ImGui::SliderFloat("Transmission Boost", &tuningSettings.GrassTransmissionBoost, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Local Light Boost", &tuningSettings.GrassLocalLightBoost, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx(T(TKEY("complex_grass"), "Vegetation Specular"), ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::TextWrapped("Controls directional/local GGX highlights for grass and animated tree foliage.");
		ImGui::SliderFloat(T(TKEY("glossiness"), "Glossiness"), &settings.Glossiness, 1.0f, 100.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextWrapped("Maps directly to foliage GGX roughness. Higher values produce tighter highlights on both grass and animated leaves.");
		}

		ImGui::SliderFloat(T(TKEY("specular_strength"), "Specular Strength"), &settings.SpecularStrength, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextWrapped("Scales the foliage dielectric reflection. 0 disables the lobe; 1 is calibrated; values above 1 are an artistic boost.");
		}

		ImGui::Spacing();
		ImGui::TextWrapped("%s", T(TKEY("detection_header"), "Complex Grass Detection"));
		static constexpr const char* grassModes[] = {
			"Auto (Safe Multi-Sample)",
			"Basic / Vanilla Texture",
			"Auto Layout (DirectX Y)",
			"Auto Layout (Flip Y)"
		};
		int complexMode = static_cast<int>(std::min(settings.ComplexGrassMode, 3u));
		if (ImGui::Combo("Grass Texture / Normal Mode", &complexMode, grassModes, static_cast<int>(std::size(grassModes))))
			settings.ComplexGrassMode = static_cast<uint>(std::clamp(complexMode, 0, 3));
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Every Auto mode validates several pixels from the packed normal half before enabling Complex Grass. DirectX Y and Flip Y override only the normal convention after validation, so ordinary vegetation keeps its full diffuse UV range. Basic/Vanilla never interprets diffuse colour as a normal map.");
		ImGui::SliderFloat(T(TKEY("detection_threshold"), "Detection Threshold"), &settings.ComplexGrassThreshold, 0.001f, 0.1f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("detection_threshold_tooltip"),
								  "Tolerance used by Auto mode. Lower values require the packed normal sentinel to be closer to unit length."));
		}

		ImGui::Spacing();
		ImGui::Spacing();
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx(T(TKEY("effects"), "Effects"), ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderFloat(T(TKEY("sss_amount"), "SSS Amount"), &settings.TissueDiffusionAmount, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("sss_tooltip"),
								  "Tissue Diffusion (SSS) amount. "
								  "Soft lighting controls how evenly lit an object is. "
								  "Back lighting illuminates the back face of an object. "
								  "Combined to model the transport of light through the surface."));
		}

		ImGui::Spacing();
		ImGui::Spacing();
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx(T(TKEY("lighting"), "Lighting"), ImGuiTreeNodeFlags_DefaultOpen)) {
		Util::UIntCheckbox(T(TKEY("override_complex"), "Override Complex Foliage Dynamics Settings"), &settings.OverrideComplexGrassSettings);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("override_complex_tooltip"),
								  "Override the settings set by the grass mesh author. "
								  "Complex grass authors can define the brightness for their grass meshes. "
								  "However, some authors may not account for the extra lights available from PIXL Renderer. "
								  "This option will treat their grass settings like non-complex grass. "
								  "This was the default in PIXL Renderer < 0.7.0"));
		}
		ImGui::Spacing();
		ImGui::Spacing();
		ImGui::TextWrapped("%s", T(TKEY("basic_grass"), "Basic Grass"));
		ImGui::SliderFloat(T(TKEY("brightness"), "Brightness"), &settings.BasicGrassBrightness, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("brightness_tooltip"), "Darkens the grass textures to look better with the new lighting"));
		}

		ImGui::TreePop();
	}
}

#undef I18N_KEY_PREFIX

void FoliageDynamics::LoadSettings(json& o_json)
{
	settings = o_json;
	// Legacy presets omit the extension. Reloading one must not retain grass
	// material overrides from whichever configuration was loaded previously.
	tuningSettings = {};
	if (auto it = o_json.find("PIXLGrassTuning"); it != o_json.end() && it->is_object())
		tuningSettings = *it;

	settings.Glossiness = std::clamp(settings.Glossiness, 1.0f, 100.0f);
	settings.SpecularStrength = std::clamp(settings.SpecularStrength, 0.0f, 2.0f);
	settings.TissueDiffusionAmount = std::clamp(settings.TissueDiffusionAmount, 0.0f, 1.0f);
	settings.OverrideComplexGrassSettings = settings.OverrideComplexGrassSettings ? 1u : 0u;
	settings.BasicGrassBrightness = std::clamp(settings.BasicGrassBrightness, 0.0f, 1.0f);
	settings.ComplexGrassThreshold = std::clamp(settings.ComplexGrassThreshold, 0.001f, 0.1f);
	settings.TreeFlipNormalY = settings.TreeFlipNormalY ? 1u : 0u;
	settings.GrassMacroSpecular = std::clamp(settings.GrassMacroSpecular, 0.0f, 1.0f);
	settings.EnableEnhancedVegetation = settings.EnableEnhancedVegetation ? 1u : 0u;
	settings.EnableEnhancedWind = settings.EnableEnhancedWind ? 1u : 0u;
	settings.LeafTransmission = std::clamp(settings.LeafTransmission, 0.0f, 2.0f);
	settings.LeafDiffuseWrap = std::clamp(settings.LeafDiffuseWrap, 0.0f, 1.0f);
	settings.WindStrength = std::clamp(settings.WindStrength, 0.0f, 2.0f);
	settings.GustStrength = std::clamp(settings.GustStrength, 0.0f, 1.5f);
	settings.FlutterStrength = std::clamp(settings.FlutterStrength, 0.0f, 1.0f);
	settings.WindSpatialScale = std::clamp(settings.WindSpatialScale, 0.25f, 3.0f);
	settings.GustSpeed = std::clamp(settings.GustSpeed, 0.25f, 2.5f);
	settings.FlutterSpeed = std::clamp(settings.FlutterSpeed, 0.25f, 3.0f);
	settings.SpecularAA = std::clamp(settings.SpecularAA, 0.0f, 1.5f);
	settings.ComplexGrassMode = std::min(settings.ComplexGrassMode, 3u);

	tuningSettings.Magic = TuningMagic;
	tuningSettings.Version = TuningVersion;
	tuningSettings.EnableGrassAlphaControl = tuningSettings.EnableGrassAlphaControl ? 1u : 0u;
	tuningSettings.GrassFlipNormalX = tuningSettings.GrassFlipNormalX ? 1u : 0u;
	tuningSettings.GrassFlipNormalY = tuningSettings.GrassFlipNormalY ? 1u : 0u;
	tuningSettings.GrassNormalStrength = std::clamp(tuningSettings.GrassNormalStrength, 0.0f, 2.0f);
	tuningSettings.GrassCardNormalBlend = std::clamp(tuningSettings.GrassCardNormalBlend, 0.0f, 1.0f);
	tuningSettings.GrassAlphaCoverage = std::clamp(tuningSettings.GrassAlphaCoverage, 0.25f, 2.0f);
	tuningSettings.GrassCutoutBias = std::clamp(tuningSettings.GrassCutoutBias, -0.35f, 0.35f);
	tuningSettings.GrassAlphaPower = std::clamp(tuningSettings.GrassAlphaPower, 0.25f, 4.0f);
	tuningSettings.GrassEdgeDither = std::clamp(tuningSettings.GrassEdgeDither, 0.0f, 1.0f);
	tuningSettings.GrassSaturation = std::clamp(tuningSettings.GrassSaturation, 0.0f, 1.5f);
	tuningSettings.GrassContrast = std::clamp(tuningSettings.GrassContrast, 0.5f, 1.5f);
	tuningSettings.GrassWetSpecularBoost = std::clamp(tuningSettings.GrassWetSpecularBoost, 0.0f, 2.0f);
	tuningSettings.GrassTransmissionBoost = std::clamp(tuningSettings.GrassTransmissionBoost, 0.0f, 2.0f);
	tuningSettings.GrassLocalLightBoost = std::clamp(tuningSettings.GrassLocalLightBoost, 0.0f, 2.0f);
	tuningSettings.GrassDetailDistanceScale = std::clamp(tuningSettings.GrassDetailDistanceScale, 0.5f, 2.0f);
	tuningSettings.GrassDetailTransitionSoftness = std::clamp(tuningSettings.GrassDetailTransitionSoftness, 0.5f, 2.0f);
	tuningSettings.GrassSpecularNormalization = std::clamp(tuningSettings.GrassSpecularNormalization, 0.0f, 4.0f);
	tuningSettings.GrassComplexSpecularMapInfluence = std::clamp(tuningSettings.GrassComplexSpecularMapInfluence, 0.0f, 1.0f);
	tuningSettings.GrassMirrorSpecularY = tuningSettings.GrassMirrorSpecularY ? 1u : 0u;
}

void FoliageDynamics::SaveSettings(json& o_json)
{
	o_json = settings;
	o_json["PIXLGrassTuning"] = tuningSettings;
}

void FoliageDynamics::RestoreDefaultSettings()
{
	settings = {};
	tuningSettings = {};
}
