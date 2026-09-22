#include "StrandShading.h"

#include <algorithm>

#include "../I18n/I18n.h"
#include "Utils/D3D.h"
#include <DirectXTex.h>

#define I18N_KEY_PREFIX "feature.strand_shading."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	StrandShading::Settings,
	Enabled,
	HairGlossiness,
	SpecularMult,
	DiffuseMult,
	EnableTangentShift,
	PrimaryTangentShift,
	SecondaryTangentShift,
	HairSaturation,
	SpecularIndirectMult,
	DiffuseIndirectMult,
	BaseColorMult,
	Transmission,
	EnableSelfShadow,
	SelfShadowStrength,
	SelfShadowExponent,
	SelfShadowScale,
	HairMode)

void StrandShading::DrawSettings()
{
	auto tooltip = [](const char* text) {
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", text);
	};

	Util::UIntCheckbox(T(TKEY("enabled"), "Enable Advanced Hair Shading"), &settings.Enabled);
	int hairMode = static_cast<int>(std::min(settings.HairMode, 1u));
	if (ImGui::Combo(T(TKEY("hair_mode"), "Hair Mode"), &hairMode, "Kajiya-Kay\0Marschner\0"))
		settings.HairMode = static_cast<uint32_t>(std::clamp(hairMode, 0, 1));
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("hair_mode_tooltip"),
							  "Select the hair shading model to use.\n"
							  "Kajiya-Kay is an empirical model that simulates hair specular highlights.\n"
							  "Marschner is a more physically-based model that simulates hair light interaction.\n"
							  "Both models are anisotropic and support tangent-based shading.\n"
							  "Without self-shadowing, Marschner may look overly bright because of transmission.\n"));
	}
	ImGui::Spacing();
	ImGui::SliderFloat(T(TKEY("glossiness"), "Glossiness"), &settings.HairGlossiness, 0.0f, settings.HairMode == 0 ? 256.0f : 100.0f, "%.0f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("glossiness_tooltip"),
							  "Controls the glossiness of the hair.\n"
							  "Glossiness in Kajiya-Kay mode maps to the specular exponent.\n"
							  "In Marschner mode, it controls the roughness of the hair surface.\n"));
	}
	ImGui::SliderFloat(T(TKEY("specular_multiplier"), "Direct Highlight Strength"), &settings.SpecularMult, 0.0f, 4.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	tooltip("Scales direct R/TT/TRT fibre highlights. The PIXL path bounds individual-fibre energy before applying this artistic multiplier.");
	ImGui::SliderFloat(T(TKEY("diffuse_multiplier"), "Direct Light Strength"), &settings.DiffuseMult, 0.0f, 4.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	tooltip("Scales broad multiple scattering between unresolved hair strands.");
	ImGui::SliderFloat(T(TKEY("indirect_specular_multiplier"), "Environment Highlight Strength"), &settings.SpecularIndirectMult, 0.0f, 4.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	tooltip("Controls environment reflection carried by the anisotropic fibre specular lobe.");
	ImGui::SliderFloat(T(TKEY("indirect_diffuse_multiplier"), "Environment Light Strength"), &settings.DiffuseIndirectMult, 0.0f, 4.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	tooltip("Controls broad environment lighting scattered through the hair volume.");
	ImGui::SliderFloat(T(TKEY("hair_base_color_multiplier"), "Hair Colour Strength"), &settings.BaseColorMult, 0.0f, 4.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	tooltip("Scales the authored absorption colour before the hair scattering model. Values near 1.0 preserve texture intent.");
	ImGui::SliderFloat(T(TKEY("hair_saturation"), "Hair Saturation"), &settings.HairSaturation, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	tooltip("Adjusts chroma of the fibre absorption colour without changing its average luminance.");
	ImGui::SliderFloat(T(TKEY("transmission"), "Transmission"), &settings.Transmission, 0.0f, 1.0f, "%.2f");
	tooltip("Strength of light transmitted through and around backlit strands.");
	ImGui::Spacing();
	Util::UIntCheckbox(T(TKEY("enable_tangent_shift"), "Enable Tangent Shift"), &settings.EnableTangentShift);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("enable_tangent_shift_tooltip"),
							  "Enables the use of a tangent shift texture to vary specular highlights across hair strands.\n"
							  "Result may vary based on the hair model used.\n"));
	}
	if (settings.HairMode == 0) {
		ImGui::SliderFloat(T(TKEY("primary_tangent_shift"), "Primary Specular Tangent Shift"), &settings.PrimaryTangentShift, -1.0f, 1.0f, "%.2f");
		ImGui::SliderFloat(T(TKEY("secondary_tangent_shift"), "Secondary Specular Tangent Shift"), &settings.SecondaryTangentShift, -1.0f, 1.0f, "%.2f");
	}
	ImGui::Spacing();
	Util::UIntCheckbox(T(TKEY("enable_self_shadow"), "Enable Screen-Space Self Shadow"), &settings.EnableSelfShadow);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("enable_self_shadow_tooltip"),
							  "Enables screen-space self-shadowing for hair.\n"
							  "Marschner hair model might have overly bright transmission without self-shadowing.\n"));
	}
	ImGui::BeginDisabled(settings.EnableSelfShadow == 0);
	ImGui::SliderFloat(T(TKEY("self_shadow_strength"), "Self Shadow Strength"), &settings.SelfShadowStrength, 0.0f, 1.0f, "%.2f");
	tooltip("Blends the screen-space strand occlusion into direct hair lighting.");
	ImGui::SliderFloat(T(TKEY("self_shadow_exponent"), "Self Shadow Exponent"), &settings.SelfShadowExponent, 0.0f, 10.0f, "%.2f");
	tooltip("Shapes how rapidly multiple screen-space blockers turn into a dense strand shadow.");
	ImGui::SliderFloat(T(TKEY("self_shadow_scale"), "Self Shadow Scale"), &settings.SelfShadowScale, 0.0f, 10.0f, "%.2f");
	tooltip("Length of the bounded screen-space visibility ray. Large values can cross unrelated geometry.");
	ImGui::EndDisabled();
}

void StrandShading::LoadSettings(json& o_json)
{
	settings = o_json;
}

void StrandShading::SaveSettings(json& o_json)
{
	o_json = settings;
}

void StrandShading::RestoreDefaultSettings()
{
	settings = {};
}

void StrandShading::SetupResources()
{
	auto device = globals::d3d::device;

	logger::debug("Loading Hair Tangent Shift Texture...");
	{
		DirectX::ScratchImage image;
		try {
			std::filesystem::path path = "Data\\Shaders\\Hair\\TangentShift.dds";

			DX::ThrowIfFailed(LoadFromDDSFile(path.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image));
		} catch (const DX::com_exception& e) {
			logger::error("{}", e.what());
			return;
		}

		ID3D11Resource* pResource = nullptr;
		try {
			DX::ThrowIfFailed(CreateTexture(device,
				image.GetImages(), image.GetImageCount(),
				image.GetMetadata(), &pResource));
		} catch (const DX::com_exception& e) {
			logger::error("{}", e.what());
			return;
		}

		texTangentShift = eastl::make_unique<Texture2D>(reinterpret_cast<ID3D11Texture2D*>(pResource), "StrandShading::TangentShift");

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texTangentShift->desc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = 10 }
		};
		texTangentShift->CreateSRV(srvDesc);
	}
}

void StrandShading::Prepass()
{
	auto context = globals::d3d::context;

	if (texTangentShift) {
		ID3D11ShaderResourceView* srv = texTangentShift->srv.get();
		context->PSSetShaderResources(73, 1, &srv);
	}
}

#undef I18N_KEY_PREFIX
