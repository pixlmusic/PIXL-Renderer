#include "WaterOptics.h"

#include <DDSTextureLoader.h>
#include <WICTextureLoader.h>

#include "I18n/I18n.h"
#include "State.h"

#define I18N_KEY_PREFIX "feature.water_optics."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	WaterOptics::Settings,
	EnableEnhancedCaustics,
	CausticsStrength,
	CausticsDispersion,
	CausticsFocus,
	EnableEnhancedSSR,
	SSRThicknessScale,
	SSRDistanceScale,
	SSREdgeFade,
	SurfaceSSRStrength,
	CausticsVisibility,
	WaterTintStrength,
	ReflectionBrightness,
	EnableDynamicFoam,
	FoamStrength,
	FoamScale,
	PlayerWakeStrength)

void WaterOptics::DrawSettings()
{
	bool changed = false;
	if (ImGui::TreeNodeEx(T(TKEY("caustics"), "Physical Caustics"), ImGuiTreeNodeFlags_DefaultOpen)) {
		changed |= Util::UIntCheckbox(T(TKEY("enhanced_caustics"), "Enhanced Caustics"), &settings.EnableEnhancedCaustics);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", T(TKEY("enhanced_caustics_tooltip"), "Adds sun-projected multi-scale focusing, chromatic dispersion and depth-dependent absorption. Changes are real-time."));
		ImGui::BeginDisabled(settings.EnableEnhancedCaustics == 0);
		changed |= ImGui::SliderFloat(T(TKEY("caustics_strength"), "Intensity"), &settings.CausticsStrength, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", T(TKEY("caustics_strength_tooltip"), "Brightness contrast of focused underwater sunlight. The shader remains energy bounded."));
		changed |= ImGui::SliderFloat(T(TKEY("caustics_dispersion"), "Color Dispersion"), &settings.CausticsDispersion, 0.0f, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", T(TKEY("caustics_dispersion_tooltip"), "Separates red and blue caustic wavelengths at depth."));
		changed |= ImGui::SliderFloat(T(TKEY("caustics_focus"), "Focus Sharpness"), &settings.CausticsFocus, 0.25f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", T(TKEY("caustics_focus_tooltip"), "Controls the bright refractive folds reconstructed from the caustic texture curvature."));
		changed |= ImGui::SliderFloat(T(TKEY("caustics_visibility"), "Visibility"), &settings.CausticsVisibility, 0.0f, 2.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", T(TKEY("caustics_visibility_tooltip"), "Controls pattern contrast after depth and sunlight attenuation. Raise this if weather lighting or dark water makes caustics difficult to see."));
		ImGui::EndDisabled();
		ImGui::TextDisabled("Visible on submerged receivers under directional sunlight; not drawn on the water surface itself.");
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx(T(TKEY("ssr"), "Water Reflection Quality"), ImGuiTreeNodeFlags_DefaultOpen)) {
		changed |= Util::UIntCheckbox(T(TKEY("enhanced_ssr"), "Use Enhanced SSR Trace"), &settings.EnableEnhancedSSR);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", T(TKEY("enhanced_ssr_tooltip"), "Uses perspective-distributed tracing, binary hit refinement, temporal confidence and directional color filtering. Image-space shaders require a shader-cache rebuild after first enabling; sliders are real-time."));
		ImGui::BeginDisabled(settings.EnableEnhancedSSR == 0);
		changed |= ImGui::SliderFloat(T(TKEY("ssr_thickness"), "Hit Thickness"), &settings.SSRThicknessScale, 0.25f, 3.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", T(TKEY("ssr_thickness_tooltip"), "Tolerance for accepting a depth crossing. Raise to fill holes; lower to reduce reflection leaks."));
		changed |= ImGui::SliderFloat(T(TKEY("ssr_distance"), "Trace Distance"), &settings.SSRDistanceScale, 0.25f, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", T(TKEY("ssr_distance_tooltip"), "Maximum screen-space ray reach. Long traces cover more of the screen but can expose off-screen gaps."));
		changed |= ImGui::SliderFloat(T(TKEY("ssr_edge_fade"), "Edge Fade"), &settings.SSREdgeFade, 0.25f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", T(TKEY("ssr_edge_fade_tooltip"), "Width of the confidence fade near screen borders and invalid reflection regions."));
		changed |= ImGui::SliderFloat(T(TKEY("water_ssr_strength"), "Reflection Presence"), &settings.SurfaceSSRStrength, 0.0f, 1.5f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", T(TKEY("water_ssr_strength_tooltip"), "Scales valid screen-space reflections when composited onto water. It does not brighten the cubemap fallback or invent reflections outside the screen."));
		changed |= ImGui::SliderFloat("Reflection Balance", &settings.ReflectionBrightness, 0.5f, 1.15f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Balances the complete reflected lobe against Skyrim's authored refraction and weather lighting.");
		changed |= ImGui::SliderFloat("Water Tint", &settings.WaterTintStrength, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Controls depth-dependent colour absorption without replacing the underlying scene with a flat water colour.");
		ImGui::EndDisabled();
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Flow & Contact Foam", ImGuiTreeNodeFlags_DefaultOpen)) {
		changed |= Util::UIntCheckbox("Enable Dynamic Foam", &settings.EnableDynamicFoam);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Builds a high-resolution water-surface foam layer from shallow contacts and changing currents. It follows world-space flow, never camera rotation.");
		ImGui::BeginDisabled(settings.EnableDynamicFoam == 0);
		changed |= ImGui::SliderFloat("Foam Presence", &settings.FoamStrength, 0.0f, 2.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
		changed |= ImGui::SliderFloat("Foam Detail Scale", &settings.FoamScale, 0.5f, 2.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Foam remains attached to the water surface and is restricted to current convergence, turbulence and geometry contact. Player movement does not project a wake decal.");
		ImGui::EndDisabled();
		ImGui::TreePop();
	}

	if (changed)
		globals::state->UpdateFeatureData(globals::state->inWorld);
}

void WaterOptics::LoadSettings(json& o_json)
{
	// Older PIXL Renderer presets serialized this settings-less feature as null.
	// Treat that legacy value as defaults so existing users can upgrade in place.
	if (o_json.is_object())
		settings = o_json;
	else
		settings = {};

	settings.EnableEnhancedCaustics = settings.EnableEnhancedCaustics ? 1u : 0u;
	settings.CausticsStrength = std::clamp(settings.CausticsStrength, 0.0f, 2.0f);
	settings.CausticsDispersion = std::clamp(settings.CausticsDispersion, 0.0f, 1.5f);
	settings.CausticsFocus = std::clamp(settings.CausticsFocus, 0.25f, 2.0f);
	settings.EnableEnhancedSSR = settings.EnableEnhancedSSR ? 1u : 0u;
	settings.SSRThicknessScale = std::clamp(settings.SSRThicknessScale, 0.25f, 3.0f);
	settings.SSRDistanceScale = std::clamp(settings.SSRDistanceScale, 0.25f, 1.5f);
	settings.SSREdgeFade = std::clamp(settings.SSREdgeFade, 0.25f, 2.0f);
	settings.SurfaceSSRStrength = std::clamp(settings.SurfaceSSRStrength, 0.0f, 1.5f);
	settings.CausticsVisibility = std::clamp(settings.CausticsVisibility, 0.0f, 2.5f);
	settings.WaterTintStrength = std::clamp(settings.WaterTintStrength, 0.0f, 1.0f);
	settings.ReflectionBrightness = std::clamp(settings.ReflectionBrightness, 0.5f, 1.15f);
	settings.EnableDynamicFoam = settings.EnableDynamicFoam ? 1u : 0u;
	settings.FoamStrength = std::clamp(settings.FoamStrength, 0.0f, 2.0f);
	settings.FoamScale = std::clamp(settings.FoamScale, 0.5f, 2.0f);
	// Retain the serialized lane for ABI/config compatibility, but player-centred
	// wake projection is intentionally retired. Contact foam is water-owned.
	settings.PlayerWakeStrength = 0.0f;
}
void WaterOptics::SaveSettings(json& o_json) { o_json = settings; }
void WaterOptics::RestoreDefaultSettings() { settings = {}; }

void WaterOptics::SetupResources()
{
	auto device = globals::d3d::device;
	auto context = globals::d3d::context;
	if (!device || !context) {
		logger::error("[PIXL Water Optics] D3D11 device/context unavailable; caustics will remain disabled");
		return;
	}

	constexpr auto causticsPath = L"Data\\Shaders\\WaterOptics\\watercaustics.dds";
	const auto result = DirectX::CreateDDSTextureFromFile(device, context, causticsPath, nullptr, causticsView.put());
	if (FAILED(result) || !causticsView) {
		logger::error("[Water Optics] Failed to load required caustics texture (HRESULT 0x{:08X}): {}", static_cast<std::uint32_t>(result), "Data/Shaders/WaterOptics/watercaustics.dds");
	} else {
		logger::info("[Water Optics] Loaded caustics texture: {}", "Data/Shaders/WaterOptics/watercaustics.dds");
	}

	foamStencilView = nullptr;
	constexpr auto foamStencilPath = L"Data\\Shaders\\WaterOptics\\FoamStencil2K.png";
	// Grayscale values are shader data, not display colour. Supplying the immediate
	// context lets DirectXTK allocate and generate the complete mip chain, avoiding
	// shimmer when the high-resolution mask recedes into the distance.
	const auto foamResult = DirectX::CreateWICTextureFromFileEx(
		device,
		context,
		foamStencilPath,
		0,
		D3D11_USAGE_DEFAULT,
		D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET,
		0,
		D3D11_RESOURCE_MISC_GENERATE_MIPS,
		DirectX::WIC_LOADER_IGNORE_SRGB,
		nullptr,
		foamStencilView.put());
	if (FAILED(foamResult) || !foamStencilView) {
		logger::warn(
			"[PIXL Water Optics] Optional foam stencil unavailable (HRESULT 0x{:08X}); dynamic contact foam will remain disabled",
			static_cast<std::uint32_t>(foamResult));
	} else {
		logger::info("[PIXL Water Optics] Loaded 2048x2048 flow/contact foam stencil with generated mips");
	}
}

void WaterOptics::Prepass()
{
	auto context = globals::d3d::context;
	if (!context)
		return;
	ID3D11ShaderResourceView* srvs[] = { causticsView.get(), foamStencilView.get() };
	context->PSSetShaderResources(65, static_cast<UINT>(std::size(srvs)), srvs);
}

bool WaterOptics::HasShaderDefine(RE::BSShader::Type shaderType)
{
	// Lighting consumes WATER_OPTICS for submerged-receiver caustics; Water
	// consumes it for the surface parallax include. ImageSpace retains module
	// ownership of the enhanced SSR source and its targeted cache invalidation,
	// even though that shader selects the enhanced trace from FeatureData at
	// runtime. No other Skyrim shader family contains a WATER_OPTICS consumer.
	return shaderType == RE::BSShader::Type::Lighting ||
	       shaderType == RE::BSShader::Type::Water ||
	       shaderType == RE::BSShader::Type::ImageSpace;
}

#undef I18N_KEY_PREFIX

