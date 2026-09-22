#include "MaterialLayers.h"
#include "../I18n/I18n.h"
#include "Utils/D3D.h"
#include "State.h"
#include "HybridGI.h"

#define I18N_KEY_PREFIX "feature.material_layers."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	MaterialLayers::Settings,
	EnableComplexMaterial,
	EnableParallax,
	EnableTerrain,
	EnableHeightBlending,
	EnableShadows,
	EnableParallaxWarpingFix)

// TuningSettings has 76 serialized members. The nlohmann convenience
// macro is intentionally not used here: its variadic FOR_EACH expansion is
// bounded, and the v3.14 tuning block exceeds that limit. When that happens
// MSVC reports NLOHMANN_JSON_TO / every member name as undeclared at the macro
// invocation line.
//
// Keep explicit member serialization so the dedicated b9 tuning block can grow
// without turning JSON macro arity into a compile-time ABI constraint.
void to_json(nlohmann::json& j, const MaterialLayers::TuningSettings& value)
{
	j = nlohmann::json::object();

#define PIXL_JSON_WRITE(Member) j[#Member] = value.Member
	PIXL_JSON_WRITE(Magic);
	PIXL_JSON_WRITE(Version);
	PIXL_JSON_WRITE(EnableObjectAutoPOM);
	PIXL_JSON_WRITE(EnableTerrainAutoPOM);
	PIXL_JSON_WRITE(TerrainHeightMode);
	PIXL_JSON_WRITE(EnableAutoPOMSelfShadows);
	PIXL_JSON_WRITE(EnableTerrainSelfShadows);
	PIXL_JSON_WRITE(EnableDetailReconstruction);
	PIXL_JSON_WRITE(ObjectAuthoredDepthScale);
	PIXL_JSON_WRITE(ObjectAutoHeightScale);
	PIXL_JSON_WRITE(ObjectMaxTexelShift);
	PIXL_JSON_WRITE(ObjectGrazingProtection);
	PIXL_JSON_WRITE(ObjectFadeStart);
	PIXL_JSON_WRITE(ObjectFadeEnd);
	PIXL_JSON_WRITE(ObjectMaxMip);
	PIXL_JSON_WRITE(AutoMinTexelShift);
	PIXL_JSON_WRITE(ObjectNearSteps);
	PIXL_JSON_WRITE(ObjectMaxSteps);
	PIXL_JSON_WRITE(ObjectRefinementSteps);
	PIXL_JSON_WRITE(pad0);
	PIXL_JSON_WRITE(AutoHeightContrast);
	PIXL_JSON_WRITE(AutoHeightNormalInfluence);
	PIXL_JSON_WRITE(AutoHeightReferenceMipOffset);
	PIXL_JSON_WRITE(AutoHeightChromaRejection);
	PIXL_JSON_WRITE(ObjectShadowStrength);
	PIXL_JSON_WRITE(ObjectShadowBias);
	PIXL_JSON_WRITE(ObjectShadowRayScale);
	PIXL_JSON_WRITE(pad1);
	PIXL_JSON_WRITE(TerrainDepthScale);
	PIXL_JSON_WRITE(TerrainMaxTexelShift);
	PIXL_JSON_WRITE(TerrainGrazingProtection);
	PIXL_JSON_WRITE(TerrainHeightStrength);
	PIXL_JSON_WRITE(TerrainFadeStart);
	PIXL_JSON_WRITE(TerrainFadeEnd);
	PIXL_JSON_WRITE(TerrainMaxMip);
	PIXL_JSON_WRITE(TerrainHeightContrast);
	PIXL_JSON_WRITE(TerrainNearSteps);
	PIXL_JSON_WRITE(TerrainMaxSteps);
	PIXL_JSON_WRITE(TerrainRefinementSteps);
	PIXL_JSON_WRITE(pad2);
	PIXL_JSON_WRITE(TerrainReferenceMipOffset);
	PIXL_JSON_WRITE(TerrainHeightBlendStrength);
	PIXL_JSON_WRITE(TerrainShadowStrength);
	PIXL_JSON_WRITE(TerrainShadowBias);
	PIXL_JSON_WRITE(TerrainShadowRayScale);
	PIXL_JSON_WRITE(DetailObjectStrength);
	PIXL_JSON_WRITE(DetailTerrainStrength);
	PIXL_JSON_WRITE(DetailAlbedoStrength);
	PIXL_JSON_WRITE(DetailNormalStrength);
	PIXL_JSON_WRITE(DetailRoughnessStrength);
	PIXL_JSON_WRITE(DetailMipSeparation);
	PIXL_JSON_WRITE(DetailContrast);
	PIXL_JSON_WRITE(DetailAntiShimmer);
	PIXL_JSON_WRITE(DetailFadeStart);
	PIXL_JSON_WRITE(DetailFadeEnd);
	PIXL_JSON_WRITE(DetailMaxMip);
	PIXL_JSON_WRITE(DetailDarkProtection);
	PIXL_JSON_WRITE(DetailSmoothProtection);
	PIXL_JSON_WRITE(DetailDominantTerrainMinWeight);
	PIXL_JSON_WRITE(DetailQuality);
	PIXL_JSON_WRITE(TerrainSyntheticPolarity);
	PIXL_JSON_WRITE(TerrainSourceMipBias);
	PIXL_JSON_WRITE(TerrainHeightDeadZone);
	PIXL_JSON_WRITE(TerrainAlphaAssist);
	PIXL_JSON_WRITE(TerrainAlphaEvidenceThreshold);
	PIXL_JSON_WRITE(TerrainSyntheticScaleFloor);
	PIXL_JSON_WRITE(TerrainVirtualDepthStrength);
	PIXL_JSON_WRITE(TerrainVirtualDepthMaxWorld);
	PIXL_JSON_WRITE(TerrainVirtualDepthMaxUV);
	PIXL_JSON_WRITE(TerrainVirtualDepthProtrusion);
	PIXL_JSON_WRITE(EnableTerrainVirtualDepth);
	PIXL_JSON_WRITE(TerrainHeightDebugMode);
	PIXL_JSON_WRITE(TerrainReliefGamma);
	PIXL_JSON_WRITE(TerrainSyntheticGain);
	PIXL_JSON_WRITE(ObjectVirtualDepthStrength);
	PIXL_JSON_WRITE(ObjectVirtualDepthMaxWorld);
#undef PIXL_JSON_WRITE
}

void from_json(const nlohmann::json& j, MaterialLayers::TuningSettings& value)
{
	// Match WITH_DEFAULT semantics: missing keys leave the struct's existing
	// default member initializer intact.
#define PIXL_JSON_READ(Member) \
	do { \
		const auto it = j.find(#Member); \
		if (it != j.end() && !it->is_null()) \
			it->get_to(value.Member); \
	} while (false)

	PIXL_JSON_READ(Magic);
	PIXL_JSON_READ(Version);
	PIXL_JSON_READ(EnableObjectAutoPOM);
	PIXL_JSON_READ(EnableTerrainAutoPOM);
	PIXL_JSON_READ(TerrainHeightMode);
	PIXL_JSON_READ(EnableAutoPOMSelfShadows);
	PIXL_JSON_READ(EnableTerrainSelfShadows);
	PIXL_JSON_READ(EnableDetailReconstruction);
	PIXL_JSON_READ(ObjectAuthoredDepthScale);
	PIXL_JSON_READ(ObjectAutoHeightScale);
	PIXL_JSON_READ(ObjectMaxTexelShift);
	PIXL_JSON_READ(ObjectGrazingProtection);
	PIXL_JSON_READ(ObjectFadeStart);
	PIXL_JSON_READ(ObjectFadeEnd);
	PIXL_JSON_READ(ObjectMaxMip);
	PIXL_JSON_READ(AutoMinTexelShift);
	PIXL_JSON_READ(ObjectNearSteps);
	PIXL_JSON_READ(ObjectMaxSteps);
	PIXL_JSON_READ(ObjectRefinementSteps);
	PIXL_JSON_READ(pad0);
	PIXL_JSON_READ(AutoHeightContrast);
	PIXL_JSON_READ(AutoHeightNormalInfluence);
	PIXL_JSON_READ(AutoHeightReferenceMipOffset);
	PIXL_JSON_READ(AutoHeightChromaRejection);
	PIXL_JSON_READ(ObjectShadowStrength);
	PIXL_JSON_READ(ObjectShadowBias);
	PIXL_JSON_READ(ObjectShadowRayScale);
	PIXL_JSON_READ(pad1);
	PIXL_JSON_READ(TerrainDepthScale);
	PIXL_JSON_READ(TerrainMaxTexelShift);
	PIXL_JSON_READ(TerrainGrazingProtection);
	PIXL_JSON_READ(TerrainHeightStrength);
	PIXL_JSON_READ(TerrainFadeStart);
	PIXL_JSON_READ(TerrainFadeEnd);
	PIXL_JSON_READ(TerrainMaxMip);
	PIXL_JSON_READ(TerrainHeightContrast);
	PIXL_JSON_READ(TerrainNearSteps);
	PIXL_JSON_READ(TerrainMaxSteps);
	PIXL_JSON_READ(TerrainRefinementSteps);
	PIXL_JSON_READ(pad2);
	PIXL_JSON_READ(TerrainReferenceMipOffset);
	PIXL_JSON_READ(TerrainHeightBlendStrength);
	PIXL_JSON_READ(TerrainShadowStrength);
	PIXL_JSON_READ(TerrainShadowBias);
	PIXL_JSON_READ(TerrainShadowRayScale);
	PIXL_JSON_READ(DetailObjectStrength);
	PIXL_JSON_READ(DetailTerrainStrength);
	PIXL_JSON_READ(DetailAlbedoStrength);
	PIXL_JSON_READ(DetailNormalStrength);
	PIXL_JSON_READ(DetailRoughnessStrength);
	PIXL_JSON_READ(DetailMipSeparation);
	PIXL_JSON_READ(DetailContrast);
	PIXL_JSON_READ(DetailAntiShimmer);
	PIXL_JSON_READ(DetailFadeStart);
	PIXL_JSON_READ(DetailFadeEnd);
	PIXL_JSON_READ(DetailMaxMip);
	PIXL_JSON_READ(DetailDarkProtection);
	PIXL_JSON_READ(DetailSmoothProtection);
	PIXL_JSON_READ(DetailDominantTerrainMinWeight);
	PIXL_JSON_READ(DetailQuality);
	PIXL_JSON_READ(TerrainSyntheticPolarity);
	PIXL_JSON_READ(TerrainSourceMipBias);
	PIXL_JSON_READ(TerrainHeightDeadZone);
	PIXL_JSON_READ(TerrainAlphaAssist);
	PIXL_JSON_READ(TerrainAlphaEvidenceThreshold);
	PIXL_JSON_READ(TerrainSyntheticScaleFloor);
	PIXL_JSON_READ(TerrainVirtualDepthStrength);
	PIXL_JSON_READ(TerrainVirtualDepthMaxWorld);
	PIXL_JSON_READ(TerrainVirtualDepthMaxUV);
	PIXL_JSON_READ(TerrainVirtualDepthProtrusion);
	PIXL_JSON_READ(EnableTerrainVirtualDepth);
	PIXL_JSON_READ(TerrainHeightDebugMode);
	PIXL_JSON_READ(TerrainReliefGamma);
	PIXL_JSON_READ(TerrainSyntheticGain);
	PIXL_JSON_READ(ObjectVirtualDepthStrength);
	PIXL_JSON_READ(ObjectVirtualDepthMaxWorld);
#undef PIXL_JSON_READ
}

void MaterialLayers::DataLoaded()
{
	const bool terrainParallaxRequested =
		settings.EnableParallax != 0u &&
		(tuningSettings.EnableTerrainAutoPOM != 0u ||
		 tuningSettings.TerrainHeightMode != 1u ||
		 settings.EnableTerrain != 0u);

	if (terrainParallaxRequested) {
		if (auto* iniSettings = globals::game::iniSettingCollection; iniSettings) {
			if (auto bLandSpecular = iniSettings->GetSetting("bLandSpecular:Landscape"); bLandSpecular) {
				if (!bLandSpecular->data.b) {
					logger::info("[PIXL MaterialLayers] Enabling bLandSpecular for terrain parallax/Auto-POM");
					bLandSpecular->data.b = true;
				}
			}
		}
	}
}

void MaterialLayers::SetupResources()
{
	logger::debug("[MaterialLayers] SetupResources begin (b9 tuning CB)");

	if (!tuningCB) {
		try {
			auto desc = ConstantBufferDesc<TuningSettings>();
			logger::debug(
				"[MaterialLayers] Creating tuning CB: struct={} bytes, D3D ByteWidth={} bytes",
				sizeof(TuningSettings),
				desc.ByteWidth);

			tuningCB = new ConstantBuffer(desc, "MaterialLayers::TuningCB");
			logger::debug("[MaterialLayers] Tuning CB created successfully");
		} catch (const std::exception& e) {
			logger::error("[MaterialLayers] Tuning CB creation failed: {}", e.what());
			tuningCB = nullptr;
		} catch (...) {
			logger::error("[MaterialLayers] Tuning CB creation failed with unknown exception");
			tuningCB = nullptr;
		}
	} else {
		logger::debug("[MaterialLayers] Tuning CB already exists");
	}

	logger::debug("[MaterialLayers] SetupResources end");
}

void MaterialLayers::ResolveEffectsDepth(ID3D11ShaderResourceView* depth, ID3D11ShaderResourceView* masks)
{
	effectsDepthReady = false;
	if (!loaded || !settings.EnableParallax || !depth || !masks || effectsDepthFailed ||
		(tuningSettings.ObjectVirtualDepthStrength <= 0.0f &&
		 (!tuningSettings.EnableTerrainVirtualDepth || tuningSettings.TerrainVirtualDepthStrength <= 0.0f)))
		return;
	auto* device = globals::d3d::device;
	auto* context = globals::d3d::context;
	if (!device || !context || !globals::state || !globals::state->sharedDataCB) {
		static bool loggedMissingDependency = false;
		if (!loggedMissingDependency) {
			logger::warn("[MaterialLayers] Effects depth deferred: D3D/shared-frame resources are not ready");
			loggedMissingDependency = true;
		}
		return;
	}
	try {
		if (!effectsDepthCS)
			effectsDepthCS.attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(
				L"Data\\Shaders\\MaterialLayers\\EffectsDepth.hlsl", {}, "cs_5_0")));
		if (!effectsDepthCS)
			throw std::runtime_error("effects-depth shader unavailable");
		winrt::com_ptr<ID3D11Resource> resource;
		depth->GetResource(resource.put());
		auto texture = resource.as<ID3D11Texture2D>();
		D3D11_TEXTURE2D_DESC desc{};
		texture->GetDesc(&desc);
		D3D11_TEXTURE2D_DESC current{};
		if (effectsDepth)
			effectsDepth->GetDesc(&current);
		if (!effectsDepth || !effectsDepthSRV || !effectsDepthUAV || current.Width != desc.Width || current.Height != desc.Height) {
			effectsDepthUAV = nullptr;
			effectsDepthSRV = nullptr;
			effectsDepth = nullptr;
			desc.Format = DXGI_FORMAT_R32_FLOAT;
			desc.MipLevels = desc.ArraySize = 1;
			desc.SampleDesc = { 1, 0 };
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			desc.CPUAccessFlags = desc.MiscFlags = 0;
			winrt::check_hresult(device->CreateTexture2D(&desc, nullptr, effectsDepth.put()));
			winrt::check_hresult(device->CreateShaderResourceView(effectsDepth.get(), nullptr, effectsDepthSRV.put()));
			winrt::check_hresult(device->CreateUnorderedAccessView(effectsDepth.get(), nullptr, effectsDepthUAV.put()));
		}
		ID3D11Buffer* shared = globals::state->sharedDataCB->CB();
		context->CSSetConstantBuffers(5, 1, &shared);
		ID3D11ShaderResourceView* inputs[] = { depth, masks };
		ID3D11UnorderedAccessView* output = effectsDepthUAV.get();
		context->CSSetShaderResources(0, 2, inputs);
		context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
		context->CSSetShader(effectsDepthCS.get(), nullptr, 0);
		context->Dispatch((desc.Width + 7) / 8, (desc.Height + 7) / 8, 1);
		ID3D11ShaderResourceView* nullInputs[2]{};
		ID3D11UnorderedAccessView* nullOutput = nullptr;
		context->CSSetShaderResources(0, 2, nullInputs);
		context->CSSetUnorderedAccessViews(0, 1, &nullOutput, nullptr);
		context->CSSetShader(nullptr, nullptr, 0);
		effectsDepthReady = true;
	} catch (const std::exception& error) {
		effectsDepthFailed = true;
		logger::warn("[MaterialLayers] Effects depth unavailable; preserving raster depth: {}", error.what());
	} catch (...) {
		effectsDepthFailed = true;
		logger::warn("[MaterialLayers] Effects depth unavailable; preserving raster depth");
	}
}

void MaterialLayers::Prepass()
{
	effectsDepthReady = false;
	const std::array<float, 8> depthSettings{
		static_cast<float>(settings.EnableParallax), tuningSettings.ObjectVirtualDepthStrength,
		tuningSettings.ObjectVirtualDepthMaxWorld, static_cast<float>(tuningSettings.EnableTerrainVirtualDepth),
		tuningSettings.TerrainVirtualDepthStrength, tuningSettings.TerrainVirtualDepthMaxWorld,
		tuningSettings.TerrainVirtualDepthMaxUV, tuningSettings.TerrainVirtualDepthProtrusion };
	if (depthSettings != lastEffectsDepthSettings) {
		globals::pipeline::hybridGI.queuedResetTemporalHistory = true;
		lastEffectsDepthSettings = depthSettings;
	}
	auto* context = globals::d3d::context;
	if (!tuningCB || !context)
		return;

	try {
		// Signature/version are runtime guards for stale/unbound shader state.
		tuningSettings.Magic = TuningMagic;
		tuningSettings.Version = TuningVersion;
		tuningCB->Update(tuningSettings);

		auto* buffer = tuningCB->CB();
		context->PSSetConstantBuffers(9, 1, &buffer);
	} catch (const std::exception& e) {
		logger::error("[MaterialLayers] b9 tuning upload/bind failed: {}", e.what());
		delete tuningCB;
		tuningCB = nullptr;
	} catch (...) {
		logger::error("[MaterialLayers] b9 tuning upload/bind failed with unknown exception");
		delete tuningCB;
		tuningCB = nullptr;
	}
}

void MaterialLayers::DrawSettings()
{
	ImGui::TextWrapped("Authored height maps take priority. Synthetic relief adds depth where compatible authored data is unavailable.");
	ImGui::Separator();
	if (ImGui::TreeNodeEx(T(TKEY("complex_material"), "Complex Material"), ImGuiTreeNodeFlags_DefaultOpen)) {
		Util::UIntCheckbox(T(TKEY("enable_complex_material"), "Enable Complex Material"), &settings.EnableComplexMaterial);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("enable_complex_material_tooltip"),
								  "Enables support for the Complex Material specification which makes use of the environment mask. "
								  "This includes parallax, as well as more realistic metals and specular reflections. "
								  "May lead to some warped textures on modded content which have an invalid alpha channel in their environment mask. "));
		}

		ImGui::Spacing();
		ImGui::Spacing();
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx(T(TKEY("parallax"), "Parallax & Material Detail"), ImGuiTreeNodeFlags_DefaultOpen)) {
		Util::UIntCheckbox(T(TKEY("enable_parallax"), "Enable Parallax"), &settings.EnableParallax);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Master switch for authored object POM, terrain POM and synthetic Auto-POM.");

		static constexpr const char* terrainHeightModes[] = {
			"Auto Hybrid: Authored -> Alpha -> Synthetic RGB",
			"Authored Height Only",
			"Synthetic Auto-POM Only",
			"Legacy Alpha Compatibility"
		};
		int terrainMode = static_cast<int>(std::min(tuningSettings.TerrainHeightMode, 3u));
		if (ImGui::Combo("Terrain Height Source", &terrainMode, terrainHeightModes, static_cast<int>(std::size(terrainHeightModes)))) {
			tuningSettings.TerrainHeightMode = static_cast<uint>(std::clamp(terrainMode, 0, 3));
			settings.EnableTerrain = tuningSettings.TerrainHeightMode == 3u ? 1u : 0u;
			DataLoaded();
		}
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Auto uses real displacement maps first, then generates a virtual height channel from vanilla diffuse RGB when a layer has no authored height. No texture files or alpha channels are written.");

		Util::UIntCheckbox("Object Auto-POM", &tuningSettings.EnableObjectAutoPOM);
		if (Util::UIntCheckbox("Terrain Auto-POM", &tuningSettings.EnableTerrainAutoPOM))
			DataLoaded();

		if (ImGui::CollapsingHeader("Authored / Object POM", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::SliderFloat("Object Authored Depth", &tuningSettings.ObjectAuthoredDepthScale, 0.10f, 4.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::TextWrapped("Scales displacement from authored PBR/CM height data. Higher values make relief more pronounced; the texel-shift limit below still protects against stretched UVs.");
			ImGui::SliderFloat("Object Auto-POM Depth", &tuningSettings.ObjectAutoHeightScale, 0.001f, 0.080f, "%.4f", ImGuiSliderFlags_AlwaysClamp);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::TextWrapped("Scales synthetic relief when a material has no usable authored height. Higher values improve depth on ordinary texture replacers but cost more and can exaggerate noisy albedo.");
			ImGui::SliderFloat("Object Max Texel Shift", &tuningSettings.ObjectMaxTexelShift, 1.0f, 64.0f, "%.1f texels", ImGuiSliderFlags_AlwaysClamp);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::TextWrapped("Maximum visible UV displacement. Raise this together with depth when the effect appears capped; lower it if silhouettes stretch at grazing angles.");
			ImGui::SliderFloat("Object Grazing Protection", &tuningSettings.ObjectGrazingProtection, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Object Fade Start", &tuningSettings.ObjectFadeStart, 128.0f, 2048.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Object Fade End", &tuningSettings.ObjectFadeEnd, 512.0f, 4096.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
			tuningSettings.ObjectFadeEnd = std::max(tuningSettings.ObjectFadeEnd, tuningSettings.ObjectFadeStart + 32.0f);
			ImGui::SliderFloat("Object Max Height Mip", &tuningSettings.ObjectMaxMip, 2.0f, 10.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
			int objectNearSteps = static_cast<int>(tuningSettings.ObjectNearSteps);
			int objectMaxSteps = static_cast<int>(tuningSettings.ObjectMaxSteps);
			int objectRefinement = static_cast<int>(tuningSettings.ObjectRefinementSteps);
			if (ImGui::SliderInt("Object Near Steps", &objectNearSteps, 4, 24))
				tuningSettings.ObjectNearSteps = static_cast<uint>(std::clamp(objectNearSteps, 4, 24));
			if (ImGui::SliderInt("Object Maximum Steps", &objectMaxSteps, 4, 32))
				tuningSettings.ObjectMaxSteps = static_cast<uint>(std::clamp(objectMaxSteps, 4, 32));
			if (ImGui::SliderInt("Object Refinement Steps", &objectRefinement, 2, 12))
				tuningSettings.ObjectRefinementSteps = static_cast<uint>(std::clamp(objectRefinement, 2, 12));
		}
		if (ImGui::CollapsingHeader("Synthetic Height Reconstruction")) {
			ImGui::SliderFloat("Height Contrast", &tuningSettings.AutoHeightContrast, 0.25f, 5.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Normal-map Influence", &tuningSettings.AutoHeightNormalInfluence, 0.0f, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Reference Mip Offset", &tuningSettings.AutoHeightReferenceMipOffset, 1.0f, 8.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Chroma Rejection", &tuningSettings.AutoHeightChromaRejection, 0.0f, 3.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Minimum Visible Shift", &tuningSettings.AutoMinTexelShift, 0.0f, 2.0f, "%.2f texels", ImGuiSliderFlags_AlwaysClamp);
		}
		if (ImGui::CollapsingHeader("Terrain Auto-Height Classifier")) {
			static constexpr const char* polarityNames[] = {
				"Dark Features Rise (recommended for current vanilla terrain)",
				"Bright Features Rise"
			};
			int terrainPolarity = tuningSettings.TerrainSyntheticPolarity >= 0.0f ? 1 : 0;
			if (ImGui::Combo("Synthetic Height Polarity", &terrainPolarity, polarityNames, 2))
				tuningSettings.TerrainSyntheticPolarity = terrainPolarity != 0 ? 1.0f : -1.0f;
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::TextWrapped("The old Auto-POM always raised brighter fine detail. On vanilla terrain that can turn painted grass/fibres into POM spikes. Flip this only if the reconstructed rocks are visibly inverted.");
			ImGui::SliderFloat("Geometry Source Mip Bias", &tuningSettings.TerrainSourceMipBias, 0.0f, 3.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::TextWrapped("Builds geometry from a slightly coarser mip so tiny grass/fibre albedo detail does not become vertical relief. PIXL microdetail still restores those frequencies visually.");
			ImGui::SliderFloat("Fine Detail Rejection", &tuningSettings.TerrainHeightDeadZone, 0.0f, 0.15f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Legacy Alpha Assist", &tuningSettings.TerrainAlphaAssist, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Alpha Evidence Threshold", &tuningSettings.TerrainAlphaEvidenceThreshold, 0.001f, 0.10f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Synthetic Layer Scale", &tuningSettings.TerrainSyntheticScaleFloor, 0.25f, 4.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::TextWrapped("Minimum POM scale for terrain layers with no authored displacement scale. This fixes roads/PBR layers that had HeightScale=0 and could never respond to the v3.12 Auto-POM sliders.");
			ImGui::SliderFloat("Synthetic Gain", &tuningSettings.TerrainSyntheticGain, 0.25f, 4.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Relief Gamma", &tuningSettings.TerrainReliefGamma, 0.35f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		}
		if (ImGui::CollapsingHeader("Terrain Relief-POM")) {
			ImGui::SliderFloat("Terrain Depth", &tuningSettings.TerrainDepthScale, 0.10f, 8.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Terrain Synthetic Height", &tuningSettings.TerrainHeightStrength, 0.0f, 5.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Terrain Height Contrast", &tuningSettings.TerrainHeightContrast, 0.25f, 6.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Terrain Max Texel Shift", &tuningSettings.TerrainMaxTexelShift, 4.0f, 256.0f, "%.0f texels", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Terrain Grazing Protection", &tuningSettings.TerrainGrazingProtection, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Terrain Reference Mip Offset", &tuningSettings.TerrainReferenceMipOffset, 1.5f, 8.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Terrain Fade Start", &tuningSettings.TerrainFadeStart, 256.0f, 3072.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Terrain Fade End", &tuningSettings.TerrainFadeEnd, 768.0f, 6144.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
			tuningSettings.TerrainFadeEnd = std::max(tuningSettings.TerrainFadeEnd, tuningSettings.TerrainFadeStart + 64.0f);
			ImGui::SliderFloat("Terrain Max Height Mip", &tuningSettings.TerrainMaxMip, 2.0f, 10.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Terrain Height Blend Strength", &tuningSettings.TerrainHeightBlendStrength, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			int terrainNearSteps = static_cast<int>(tuningSettings.TerrainNearSteps);
			int terrainMaxSteps = static_cast<int>(tuningSettings.TerrainMaxSteps);
			int terrainRefinement = static_cast<int>(tuningSettings.TerrainRefinementSteps);
			if (ImGui::SliderInt("Terrain Near Steps", &terrainNearSteps, 4, 32))
				tuningSettings.TerrainNearSteps = static_cast<uint>(std::clamp(terrainNearSteps, 4, 32));
			if (ImGui::SliderInt("Terrain Maximum Steps", &terrainMaxSteps, 4, 64))
				tuningSettings.TerrainMaxSteps = static_cast<uint>(std::clamp(terrainMaxSteps, 4, 64));
			if (ImGui::SliderInt("Terrain Refinement Steps", &terrainRefinement, 2, 16))
				tuningSettings.TerrainRefinementSteps = static_cast<uint>(std::clamp(terrainRefinement, 2, 16));

			Util::UIntCheckbox(T(TKEY("enable_height_blending"), "Enable Terrain Height Blending"), &settings.EnableHeightBlending);
			Util::UIntCheckbox(T(TKEY("enable_parallax_warping_fix"), "Enable Parallax Warping Fix"), &settings.EnableParallaxWarpingFix);
		}
		if (ImGui::CollapsingHeader("Virtual Relief Depth")) {
			ImGui::TextWrapped("Virtual depth affects deferred lighting and GI only; it does not change silhouettes, collision, or hardware visibility.");
			ImGui::Checkbox("Preview Virtual Depth Offsets", &showEffectsDepthDebug);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::TextWrapped("Diagnostic view: blue is recessed relief, orange is protrusion, black is unchanged. Shows the generated offset before the near-camera safety limit. Turn off for normal lighting.");
			ImGui::TextDisabled("%s", effectsDepthFailed ? "Effects depth unavailable - see PIXLRenderer.log" :
									  effectsDepthReady  ? "Effects depth active" :
														   "Effects depth inactive");
			ImGui::SliderFloat("Object Virtual Depth Strength", &tuningSettings.ObjectVirtualDepthStrength, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Object Virtual Depth Limit", &tuningSettings.ObjectVirtualDepthMaxWorld, 1.0f, 16.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
			Util::UIntCheckbox("Enable Terrain Virtual Depth", &tuningSettings.EnableTerrainVirtualDepth);
			ImGui::SliderFloat("Virtual Depth Strength", &tuningSettings.TerrainVirtualDepthStrength, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Virtual Depth Max World", &tuningSettings.TerrainVirtualDepthMaxWorld, 2.0f, 64.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Virtual Depth Max UV", &tuningSettings.TerrainVirtualDepthMaxUV, 0.02f, 0.50f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Virtual Protrusion", &tuningSettings.TerrainVirtualDepthProtrusion, 0.0f, 0.50f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::TextWrapped("Virtual depth makes depth-based effects see the POM surface. Protrusion is experimental and cannot create pixels outside the original terrain mesh silhouette.");
		}
		if (ImGui::CollapsingHeader("Material Detail Reconstruction")) {
			Util::UIntCheckbox("Enable Detail Reconstruction", &tuningSettings.EnableDetailReconstruction);
			if (tuningSettings.EnableDetailReconstruction != 0u) {
				int quality = static_cast<int>(std::min(tuningSettings.DetailQuality, 2u));
				static constexpr const char* qualityNames[] = { "Off", "Albedo", "Albedo + Normal + Roughness" };
				if (ImGui::Combo("Detail Quality", &quality, qualityNames, static_cast<int>(std::size(qualityNames))))
					tuningSettings.DetailQuality = static_cast<uint>(std::clamp(quality, 0, 2));
				ImGui::SliderFloat("Object Detail Strength", &tuningSettings.DetailObjectStrength, 0.0f, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				ImGui::SliderFloat("Terrain Detail Strength", &tuningSettings.DetailTerrainStrength, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				ImGui::SliderFloat("Albedo Microcontrast", &tuningSettings.DetailAlbedoStrength, 0.0f, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				ImGui::SliderFloat("Normal Microdetail", &tuningSettings.DetailNormalStrength, 0.0f, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				ImGui::SliderFloat("Roughness Microdetail", &tuningSettings.DetailRoughnessStrength, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				ImGui::SliderFloat("Detail Mip Separation", &tuningSettings.DetailMipSeparation, 0.5f, 4.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				ImGui::SliderFloat("Detail Contrast", &tuningSettings.DetailContrast, 0.25f, 2.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				ImGui::SliderFloat("Anti-Shimmer", &tuningSettings.DetailAntiShimmer, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				ImGui::SliderFloat("Detail Fade Start", &tuningSettings.DetailFadeStart, 64.0f, 1536.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
				ImGui::SliderFloat("Detail Fade End", &tuningSettings.DetailFadeEnd, 256.0f, 4096.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
				tuningSettings.DetailFadeEnd = std::max(tuningSettings.DetailFadeEnd, tuningSettings.DetailFadeStart + 32.0f);
				ImGui::SliderFloat("Detail Max Mip", &tuningSettings.DetailMaxMip, 1.0f, 8.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
				ImGui::SliderFloat("Protect Dark Materials", &tuningSettings.DetailDarkProtection, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				ImGui::SliderFloat("Protect Smooth Materials", &tuningSettings.DetailSmoothProtection, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				ImGui::SliderFloat("Dominant Terrain Layer Gate", &tuningSettings.DetailDominantTerrainMinWeight, 0.35f, 0.95f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			}
		}
		ImGui::Spacing();
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx(T(TKEY("soft_shadows"), "Parallax Self Shadows"))) {
		Util::UIntCheckbox(T(TKEY("enable_shadows"), "Enable Authored POM Shadows"), &settings.EnableShadows);
		Util::UIntCheckbox("Enable Auto-POM Shadows", &tuningSettings.EnableAutoPOMSelfShadows);
		Util::UIntCheckbox("Enable Terrain POM Shadows", &tuningSettings.EnableTerrainSelfShadows);

		ImGui::SliderFloat("Object Shadow Strength", &tuningSettings.ObjectShadowStrength, 0.0f, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Object Shadow Bias", &tuningSettings.ObjectShadowBias, 0.25f, 3.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Object Shadow Ray Scale", &tuningSettings.ObjectShadowRayScale, 0.25f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Terrain Shadow Strength", &tuningSettings.TerrainShadowStrength, 0.0f, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Terrain Shadow Bias", &tuningSettings.TerrainShadowBias, 0.25f, 3.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Terrain Shadow Ray Scale", &tuningSettings.TerrainShadowRayScale, 0.25f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

		ImGui::Spacing();
		ImGui::TreePop();
	}
}

#undef I18N_KEY_PREFIX

void MaterialLayers::LoadSettings(json& o_json)
{
	settings = o_json;
	if (const auto it = o_json.find("PIXL Tuning"); it != o_json.end() && it->is_object())
		tuningSettings = it->get<TuningSettings>();

	// Old configs used EnableTerrain as the legacy-alpha switch. Preserve that
	// intent only when no v3.12 tuning object exists.
	if (!o_json.contains("PIXL Tuning") && settings.EnableTerrain != 0u)
		tuningSettings.TerrainHeightMode = 3u;

	tuningSettings.Magic = TuningMagic;
	tuningSettings.Version = TuningVersion;
}

void MaterialLayers::SaveSettings(json& o_json)
{
	o_json = settings;
	o_json["PIXL Tuning"] = tuningSettings;
}

void MaterialLayers::RestoreDefaultSettings()
{
	settings = {};
	tuningSettings = {};
}

bool MaterialLayers::HasShaderDefine(RE::BSShader::Type shaderType)
{
	switch (shaderType) {
	case RE::BSShader::Type::Lighting:
		return true;
	default:
		return false;
	}
}
