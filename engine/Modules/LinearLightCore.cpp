#include "LinearLightCore.h"

#include "../I18n/I18n.h"
#include "State.h"

#include "Globals.h"
#include "Utils/Game.h"

#define I18N_KEY_PREFIX "feature.linear_light_core."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	LinearLightCore::Settings,
	enableLinearLightCore,
	lightGamma,
	colorGamma,
	emitColorGamma,
	glowmapGamma,
	ambientGamma,
	fogGamma,
	fogAlphaGamma,
	effectGamma,
	effectAlphaGamma,
	skyGamma,
	waterGamma,
	vlGamma,
	vanillaDiffuseColorMult,
	directionalLightMult,
	pointLightMult,
	ambientMult,
	emitColorMult,
	glowmapMult,
	effectLightingMult,
	membraneEffectMult,
	bloodEffectMult,
	projectedEffectMult,
	deferredEffectMult,
	otherEffectMult)

void LinearLightCore::DrawSettings()
{
	Util::UIntCheckbox(T(TKEY("enable"), "Enable Linear Light Core"), &settings.enableLinearLightCore);

	if (ImGui::BeginTabBar("##LinearLightingTabs", ImGuiTabBarFlags_None)) {
		if (ImGui::BeginTabItem(T(TKEY("tab_general"), "General"))) {
			ImGui::SeparatorText(T(TKEY("gamma_settings"), "Gamma Settings"));
			ImGui::SliderFloat(T(TKEY("fog_gamma"), "Fog Gamma"), &settings.fogGamma, 0.1f, 3.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat(T(TKEY("fog_transparency_gamma"), "Fog Transparency Gamma"), &settings.fogAlphaGamma, 0.1f, 3.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat(T(TKEY("sky_gamma"), "Sky Gamma"), &settings.skyGamma, 0.1f, 3.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat(T(TKEY("vl_gamma"), "Light Volumes Gamma"), &settings.vlGamma, 0.1f, 3.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat(T(TKEY("water_gamma"), "Water Gamma"), &settings.waterGamma, 0.1f, 3.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

			ImGui::SeparatorText(T(TKEY("multipliers"), "Multipliers"));
			ImGui::SliderFloat(T(TKEY("directional_light_multiplier"), "Directional Light Multiplier"), &settings.directionalLightMult, 0.0f, 10.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat(T(TKEY("ambient_multiplier"), "Ambient Multiplier"), &settings.ambientMult, 0.0f, 10.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat(T(TKEY("glowmap_multiplier"), "Glowmap Multiplier"), &settings.glowmapMult, 0.0f, 10.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem(T(TKEY("tab_advanced"), "Advanced"))) {
			ImGui::SeparatorText(T(TKEY("gamma_settings"), "Gamma Settings"));
			ImGui::SliderFloat(T(TKEY("light_gamma"), "Light Gamma"), &settings.lightGamma, 0.1f, 3.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat(T(TKEY("color_gamma"), "Color Gamma"), &settings.colorGamma, 0.1f, 3.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat(T(TKEY("emissive_color_gamma"), "Emissive Color Gamma"), &settings.emitColorGamma, 0.1f, 3.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat(T(TKEY("glowmap_gamma"), "Glowmap Gamma"), &settings.glowmapGamma, 0.1f, 3.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat(T(TKEY("ambient_gamma"), "Ambient Gamma"), &settings.ambientGamma, 0.1f, 3.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat(T(TKEY("effect_gamma"), "Effect Gamma"), &settings.effectGamma, 0.1f, 3.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat(T(TKEY("effect_transparency_gamma"), "Effect Transparency Gamma"), &settings.effectAlphaGamma, 0.1f, 3.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

			ImGui::SeparatorText(T(TKEY("multipliers"), "Multipliers"));
			ImGui::SliderFloat(T(TKEY("vanilla_diffuse_color_multiplier"), "Vanilla Diffuse Color Multiplier"), &settings.vanillaDiffuseColorMult, 0.0f, 10.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat(T(TKEY("emissive_color_multiplier"), "Emissive Color Multiplier"), &settings.emitColorMult, 0.0f, 10.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat(T(TKEY("point_light_multiplier"), "Point Light Multiplier"), &settings.pointLightMult, 0.0f, 10.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

			if (ImGui::TreeNodeEx(T(TKEY("effects"), "Effects"), ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::SliderFloat(T(TKEY("effect_lighting_multiplier"), "Effect Lighting Multiplier"), &settings.effectLightingMult, 0.0f, 10.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				ImGui::SliderFloat(T(TKEY("membrane_effects_multiplier"), "Membrane Effects Multiplier"), &settings.membraneEffectMult, 0.0f, 10.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				ImGui::SliderFloat(T(TKEY("blood_effects_multiplier"), "Blood Effects Multiplier"), &settings.bloodEffectMult, 0.0f, 10.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				ImGui::SliderFloat(T(TKEY("projected_effects_multiplier"), "Projected Effects Multiplier"), &settings.projectedEffectMult, 0.0f, 10.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				ImGui::SliderFloat(T(TKEY("deferred_effects_multiplier"), "Deferred Effects Multiplier"), &settings.deferredEffectMult, 0.0f, 10.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				ImGui::SliderFloat(T(TKEY("other_effects_multiplier"), "Other Effects Multiplier"), &settings.otherEffectMult, 0.0f, 10.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				ImGui::TreePop();
			}

			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}
}

void LinearLightCore::LoadSettings(json& o_json)
{
	settings = o_json;
}

void LinearLightCore::SaveSettings(json& o_json)
{
	o_json = settings;
}

void LinearLightCore::RestoreDefaultSettings()
{
	settings = {};
}

void LinearLightCore::SetupResources()
{
	PerGeometryCB = new ConstantBuffer(ConstantBufferDesc<PerGeometryData>());
}

void LinearLightCore::Prepass()
{
	bool isMainLoadingMenu = globals::state->isMainMenuOpen || globals::state->isLoadingMenuOpen;
	dirLightMult = 1.0f;
	if (!settings.enableLinearLightCore || isMainLoadingMenu)
		return;

	auto imageSpaceManager = globals::game::imageSpaceManager;
	if (!imageSpaceManager)
		return;

	dirLightMult = imageSpaceManager->GetRuntimeData().data.baseData.hdr.sunlightScale;
}

struct LinearLightCore::Hooks
{
	struct BSLightingShader_SetupGeometry
	{
		static void thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
		{
			globals::pipeline::linearLightCore.BSLightingShader_SetupGeometry(Pass);
			func(This, Pass, RenderFlags);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	static void Install()
	{
		stl::write_vfunc<0x6, BSLightingShader_SetupGeometry>(RE::VTABLE_BSLightingShader[0]);
		logger::info("[LinearLightCore] Installed hooks - BSLightingShader_SetupGeometry");
	}
};

void LinearLightCore::PostPostLoad()
{
	LinearLightCore::Hooks::Install();
}

LinearLightCore::PerFrameData LinearLightCore::GetCommonBufferData()
{
	if (!loaded) {
		auto data = PerFrameData{};
		data.enableLinearLightCore = false;
		return data;
	}
	bool isMainLoadingMenu = globals::state->isMainMenuOpen || globals::state->isLoadingMenuOpen;
	auto data = PerFrameData{};
	data.enableLinearLightCore = settings.enableLinearLightCore && !isMainLoadingMenu;
	data.isDirLightLinear = isDirLightLinear;
	data.dirLightMult = dirLightMult;
	data.lightGamma = settings.lightGamma;
	data.colorGamma = settings.colorGamma;
	data.emitColorGamma = settings.emitColorGamma;
	data.glowmapGamma = settings.glowmapGamma;
	data.ambientGamma = settings.ambientGamma;
	data.fogGamma = settings.fogGamma;
	data.fogAlphaGamma = settings.fogAlphaGamma;
	data.effectGamma = settings.effectGamma;
	data.effectAlphaGamma = settings.effectAlphaGamma;
	data.skyGamma = settings.skyGamma;
	data.waterGamma = settings.waterGamma;
	data.vlGamma = settings.vlGamma;

	data.vanillaDiffuseColorMult = settings.vanillaDiffuseColorMult;
	data.directionalLightMult = settings.directionalLightMult;
	data.pointLightMult = settings.pointLightMult;
	data.ambientMult = settings.ambientMult;
	data.emitColorMult = settings.emitColorMult;
	data.glowmapMult = settings.glowmapMult;
	data.effectLightingMult = settings.effectLightingMult;
	data.membraneEffectMult = settings.membraneEffectMult;
	data.bloodEffectMult = settings.bloodEffectMult;
	data.projectedEffectMult = settings.projectedEffectMult;
	data.deferredEffectMult = settings.deferredEffectMult;
	data.otherEffectMult = settings.otherEffectMult;

	return data;
}

RE::NiColor LinearLightCore::ColorToLinear(RE::NiColor inColor, float gamma)
{
	const auto linearizeChannel = [gamma](float channel) {
		// Skyrim light colours are expected to be non-negative.  Guard malformed
		// data here so a fractional gamma cannot seed NaNs into every downstream
		// lighting calculation.
		return std::pow(std::max(std::isfinite(channel) ? channel : 0.0f, 0.0f), gamma);
	};
	RE::NiColor outColor;
	outColor.red = linearizeChannel(inColor.red);
	outColor.green = linearizeChannel(inColor.green);
	outColor.blue = linearizeChannel(inColor.blue);
	return outColor;
}

void LinearLightCore::BSLightingShader_SetupGeometry(RE::BSRenderPass* a_pass)
{
	if (!a_pass || !a_pass->geometry || !PerGeometryCB || !globals::d3d::context)
		return;

	auto& property1 = a_pass->geometry->GetGeometryRuntimeData().shaderProperty;
	auto lightProperty = property1 && property1->GetRTTI() == globals::rtti::BSLightingShaderPropertyRTTI.get() ? static_cast<RE::BSLightingShaderProperty*>(property1.get()) : nullptr;

	if (lightProperty != nullptr) {
		float emissiveMult = 1.0f;
		if (settings.enableLinearLightCore) {
			emissiveMult = lightProperty->emissiveMult;
			PerGeometryData perGeometryData{};
			perGeometryData.emissiveMult = emissiveMult;
			PerGeometryCB->Update(perGeometryData);

			ID3D11Buffer* buffer = { PerGeometryCB->CB() };
			auto context = globals::d3d::context;
			context->PSSetConstantBuffers(8, 1, &buffer);
		}
	}
}

#undef I18N_KEY_PREFIX
