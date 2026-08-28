#include "AmbientProbe.h"

#include "Deferred.h"
#include "WorldProbes.h"
#include "Shadercache.h"
#include "State.h"
#include "WeatherVariableRegistry.h"

#include "Globals.h"

#include "../I18n/I18n.h"
#include <DDSTextureLoader.h>
#include <DirectXTex.h>

#define I18N_KEY_PREFIX "feature.ambient_probe."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	AmbientProbe::Settings,
	EnableAmbientProbe,
	PreserveFogLuminance,
	UseStaticAmbientProbe,
	DALCAmount,
	EnvironmentProbeScale,
	SkyProbeScale,
	EnvironmentProbeSaturation,
	SkyProbeSaturation,
	FogAmount,
	DALCMode,
	DisableInInteriors,
	DisableInWorldMap,
	DisableInLoadingScreen)

void AmbientProbe::DrawSettings()
{
	Util::WeatherUI::UIntCheckbox(T(TKEY("enable_ibl"), "Enable Ambient Lighting"), this, "EnableAmbientProbe", &settings.EnableAmbientProbe);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("enable_ibl_tooltip"), "Toggle AmbientProbe. When enabled, ambient lighting is derived from cubemap spherical harmonics instead of the vanilla system."));
	}
	Util::WeatherUI::SliderFloat(T(TKEY("env_ibl_scale"), "Environment Lighting Strength"), this, "EnvironmentProbeScale", &settings.EnvironmentProbeScale, 0.0f, 10.0f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("env_ibl_scale_tooltip"), "Intensity multiplier for the environment AmbientProbe (from World Probes).\nControls how strongly the surrounding environment contributes to ambient lighting."));
	}
	Util::WeatherUI::SliderFloat(T(TKEY("sky_ibl_scale"), "Sky Lighting Strength"), this, "SkyProbeScale", &settings.SkyProbeScale, 0.0f, 10.0f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("sky_ibl_scale_tooltip"), "Intensity multiplier for the sky AmbientProbe (from the game's native reflections cubemap).\nControls how strongly the sky contributes to ambient lighting."));
	}
	Util::WeatherUI::SliderFloat(T(TKEY("env_ibl_saturation"), "Environment Lighting Saturation"), this, "EnvironmentProbeSaturation", &settings.EnvironmentProbeSaturation, 0.0f, 2.0f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("env_ibl_saturation_tooltip"), "Color saturation of the environment AmbientProbe.\nLower values produce more neutral ambient light; higher values produce more vivid color."));
	}
	Util::WeatherUI::SliderFloat(T(TKEY("sky_ibl_saturation"), "Sky Lighting Saturation"), this, "SkyProbeSaturation", &settings.SkyProbeSaturation, 0.0f, 2.0f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("sky_ibl_saturation_tooltip"), "Color saturation of the sky AmbientProbe.\nLower values produce more neutral ambient light; higher values produce more vivid color."));
	}
	Util::WeatherUI::SliderFloat(T(TKEY("dalc_amount"), "Vanilla Ambient Matching"), this, "DALCAmount", &settings.DALCAmount, 0.0f, 1.0f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("dalc_amount_tooltip"),
							  "Blends the AmbientProbe brightness toward the game's vanilla ambient (DALC) level.\n"
							  "0 = no matching (pure AmbientProbe brightness), 1 = fully matched to vanilla ambient."));
	}
	{
		const char* dalcModeNames[] = {
			T(TKEY("dalc_mode_luminance_ratio"), "Luminance Ratio"),
			T(TKEY("dalc_mode_color_ratio"), "Color Ratio"),
			T(TKEY("dalc_mode_dalc_plus_sky"), "DALC + Sky"),
			T(TKEY("dalc_mode_dalc_plus_sky_directional"), "DALC + Sky (Directional)")
		};
		int dalcMode = static_cast<int>(settings.DALCMode);
		if (ImGui::Combo(T(TKEY("dalc_mode"), "Ambient Matching Mode"), &dalcMode, dalcModeNames, IM_ARRAYSIZE(dalcModeNames))) {
			settings.DALCMode = static_cast<uint>(dalcMode);
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("dalc_mode_tooltip"),
								  "How the DALC-to-AmbientProbe brightness ratio is computed:\n"
								  "Luminance Ratio: Scalar ratio from overall luminance (loses DALC color tint).\n"
								  "Color Ratio: Per-channel ratio (preserves DALC color tint).\n"
								  "DALC + Sky: Uses vanilla ambient as base, sky AmbientProbe on top. SkyBounce only affects sky.\n"
								  "DALC + Sky (Directional): Same, but SkyBounce also dims vanilla ambient per-direction."));
		}
	}
	Util::UIntCheckbox(T(TKEY("use_static_ibl"), "Use Stable Lighting Outside the World"), &settings.UseStaticAmbientProbe);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("use_static_ibl_tooltip"), "Uses pre-baked static AmbientProbe cubemap textures for objects rendered outside the game world (e.g. inventory items, loading screens)."));
	}
	Util::WeatherUI::SliderFloat(T(TKEY("fog_mix"), "Fog Mix"), this, "FogAmount", &settings.FogAmount, 0.0f, 1.0f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("fog_mix_tooltip"), "Blends the fog color toward the AmbientProbe ambient color.\n0 = vanilla fog, 1 = fog fully tinted by AmbientProbe."));
	}
	Util::UIntCheckbox(T(TKEY("preserve_fog_luminance"), "Preserve Fog Luminance"), &settings.PreserveFogLuminance);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("preserve_fog_luminance_tooltip"), "When Fog Mix is active, rescales the AmbientProbe-tinted fog to keep the original fog brightness.\nPrevents fog from becoming too bright or too dark."));
	}
	ImGui::Checkbox(T(TKEY("disable_in_interiors"), "Disable in interiors"), &settings.DisableInInteriors);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("disable_in_interiors_tooltip"), "Disables AmbientProbe in interior cells."));
	}
	ImGui::Checkbox(T(TKEY("disable_in_world_map"), "Disable in world map"), &settings.DisableInWorldMap);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("disable_in_world_map_tooltip"), "Disables AmbientProbe while the world map is open."));
	}
	ImGui::Checkbox(T(TKEY("disable_in_loading_screen"), "Disable in loading screens"), &settings.DisableInLoadingScreen);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("disable_in_loading_screen_tooltip"), "Disables AmbientProbe during loading screens and the main menu."));
	}
}

#undef I18N_KEY_PREFIX

void AmbientProbe::LoadSettings(json& o_json)
{
	settings = o_json;
}

void AmbientProbe::SaveSettings(json& o_json)
{
	o_json = settings;
}

void AmbientProbe::RestoreDefaultSettings()
{
	settings = {};
}

void AmbientProbe::RegisterWeatherVariables()
{
	auto* registry = WeatherVariables::GlobalWeatherRegistry::GetSingleton()
	                     ->GetOrCreateFeatureRegistry(GetShortName());
	// Toggle AmbientProbe for this weather (SH-based ambient replaces vanilla)
	registry->RegisterVariable(std::make_shared<WeatherVariables::WeatherVariable<bool>>(
		"EnableAmbientProbe",
		"Enable AmbientProbe",
		"Enable or disable SH-based ambient lighting for this weather",
		(bool*)&settings.EnableAmbientProbe,
		true,
		[](const bool& from, const bool& to, float factor) {
			return factor > 0.5f ? to : from;  // Switch at transition midpoint
		}));

	// Intensity of environment AmbientProbe (from World Probes)
	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"EnvironmentProbeScale",
		"Env AmbientProbe Scale",
		"Intensity of environment AmbientProbe from the World Probes environment cubemap",
		&settings.EnvironmentProbeScale,
		1.0f,
		0.0f, 10.0f));

	// Intensity of sky AmbientProbe (from the game's native reflections cubemap)
	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"SkyProbeScale",
		"Sky AmbientProbe Scale",
		"Intensity of sky AmbientProbe from the game's native reflections cubemap",
		&settings.SkyProbeScale,
		1.0f,
		0.0f, 10.0f));

	// Color saturation of environment AmbientProbe
	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"EnvironmentProbeSaturation",
		"Env AmbientProbe Saturation",
		"Color saturation of the environment AmbientProbe ambient contribution",
		&settings.EnvironmentProbeSaturation,
		1.0f,
		0.0f, 2.0f));

	// Color saturation of sky AmbientProbe
	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"SkyProbeSaturation",
		"Sky AmbientProbe Saturation",
		"Color saturation of the sky AmbientProbe ambient contribution",
		&settings.SkyProbeSaturation,
		1.0f,
		0.0f, 2.0f));

	// How much AmbientProbe brightness is matched to vanilla ambient (DALC)
	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"DALCAmount",
		"DALC Amount",
		"Blend factor toward vanilla ambient brightness (0 = pure AmbientProbe, 1 = fully matched to DALC)",
		&settings.DALCAmount,
		1.0f,
		0.0f, 1.0f));

	// Fog color blending toward AmbientProbe ambient color
	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"FogAmount",
		"Fog Mix",
		"Blends fog color toward AmbientProbe ambient color (0 = vanilla fog, 1 = fully AmbientProbe-tinted)",
		&settings.FogAmount,
		0.0f,
		0.0f, 1.0f));
}

AmbientProbe::PerFrame AmbientProbe::GetCommonBufferData() const
{
	PerFrame data = {
		.EnableAmbientProbe = IsDisabledForCurrentScene() ? 0u : settings.EnableAmbientProbe,
		.PreserveFogLuminance = settings.PreserveFogLuminance,
		.UseStaticAmbientProbe = settings.UseStaticAmbientProbe,
		.DALCAmount = settings.DALCAmount,
		.EnvironmentProbeScale = settings.EnvironmentProbeScale,
		.SkyProbeScale = settings.SkyProbeScale,
		.EnvironmentProbeSaturation = settings.EnvironmentProbeSaturation,
		.SkyProbeSaturation = settings.SkyProbeSaturation,
		.FogAmount = settings.FogAmount,
		.DALCMode = settings.DALCMode
	};

	return data;
}

bool AmbientProbe::IsDisabledForCurrentScene() const
{
	const auto state = globals::state;
	if (!state)
		return false;

	const bool inLoadingScreen = settings.DisableInLoadingScreen && state->IsMainOrLoadingMenuOpen();
	const bool inWorldMap = settings.DisableInWorldMap && state->isMapMenuOpen;
	const bool inInterior = settings.DisableInInteriors && Util::IsInterior();
	return inLoadingScreen || inWorldMap || inInterior;
}

void AmbientProbe::ReflectionsPrepass()
{
	if (loaded) {
		auto context = globals::d3d::context;

		bool sceneDisabled = IsDisabledForCurrentScene();
		auto* envProbe = envIBLTexture && envIBLTexture->srv ? envIBLTexture->srv.get() : nullptr;
		auto* skyProbe = skyIBLTexture && skyIBLTexture->srv ? skyIBLTexture->srv.get() : nullptr;
		auto* staticDiffuse = staticDiffuseAmbientTexture && staticDiffuseAmbientTexture->srv ? staticDiffuseAmbientTexture->srv.get() : nullptr;
		auto* staticSpecular = staticSpecularIBLTexture && staticSpecularIBLTexture->srv ? staticSpecularIBLTexture->srv.get() : nullptr;

		// Set PS shader resource
		{
			std::array<ID3D11ShaderResourceView*, 4> srvs = {
				sceneDisabled ? nullptr : envProbe,
				sceneDisabled ? nullptr : skyProbe,
				staticDiffuse,
				staticSpecular
			};
			context->PSSetShaderResources(76, 4, srvs.data());
		}
	}
}

void AmbientProbe::Prepass()
{
	if (IsDisabledForCurrentScene())
		return;

	auto* diffuseAmbientCS = GetDiffuseAmbientCS();
	const bool resourcesReady = diffuseAmbientCS && envIBLTexture && envIBLTexture->srv && envIBLTexture->uav &&
	                            skyIBLTexture && skyIBLTexture->srv && skyIBLTexture->uav;
	if (!resourcesReady) {
		if (!unavailableResourcesReported) {
			logger::error("Ambient Probe prepass disabled: required shader or GPU resources are unavailable");
			unavailableResourcesReported = true;
		}
		return;
	}

	auto context = globals::d3d::context;

	auto& worldProbes = globals::pipeline::worldProbes;

	auto& envTexture = worldProbes.envTexture;

	// Unset PS shader resource
	{
		ID3D11ShaderResourceView* views[2]{ nullptr, nullptr };
		context->PSSetShaderResources(76, 2, views);
	}

	std::array<ID3D11ShaderResourceView*, 1> srvs = { (worldProbes.loaded && envTexture) ? envTexture->srv.get() : nullptr };
	std::array<ID3D11UnorderedAccessView*, 1> uavs = { envIBLTexture->uav.get() };
	std::array<ID3D11SamplerState*, 1> samplers = { Deferred::GetSingleton()->linearSampler };

	// AmbientProbe - Environment cubemap SH projection (skip for DALC-based modes that don't use EnvIBL)
	if (settings.DALCMode < 2) {
		samplers[0] = Deferred::GetSingleton()->linearSampler;

		context->CSSetSamplers(0, (uint)samplers.size(), samplers.data());
		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(diffuseAmbientCS, nullptr, 0);
		globals::profiler->BeginPass("AmbientProbe::EnvDiffuseAmbient");
		context->Dispatch(1, 1, 1);
		globals::profiler->EndPass();
	} else {
		// Still need to set sampler and shader for sky AmbientProbe dispatch below
		context->CSSetSamplers(0, (uint)samplers.size(), samplers.data());
		context->CSSetShader(diffuseAmbientCS, nullptr, 0);
	}

	// AmbientProbe with sky (use game's native reflections cubemap directly)
	{
		auto renderer = globals::game::renderer;
		auto& reflections = renderer->GetRendererData().cubemapRenderTargets[RE::RENDER_TARGETS_CUBEMAP::kREFLECTIONS];
		srvs.at(0) = reflections.SRV;
		uavs.at(0) = skyIBLTexture->uav.get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		globals::profiler->BeginPass("AmbientProbe::SkyDiffuseAmbient");
		context->Dispatch(1, 1, 1);
		globals::profiler->EndPass();
	}

	// Reset
	{
		srvs.fill(nullptr);
		uavs.fill(nullptr);
		samplers.fill(nullptr);

		context->CSSetSamplers(0, (uint)samplers.size(), samplers.data());
		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(nullptr, nullptr, 0);
	}

	// Set PS shader resource
	{
		ID3D11ShaderResourceView* views[2]{ envIBLTexture->srv.get(), skyIBLTexture->srv.get() };
		context->PSSetShaderResources(76, 2, views);
	}
}

void AmbientProbe::SetupResources()
{
	unavailableResourcesReported = false;
	GetDiffuseAmbientCS();

	{
		D3D11_TEXTURE2D_DESC texDesc{
			.Width = 3,
			.Height = 1,
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
			.SampleDesc = { 1, 0 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
			.CPUAccessFlags = 0,
			.MiscFlags = 0
		};
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = texDesc.MipLevels }
		};
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		envIBLTexture = new Texture2D(texDesc);
		envIBLTexture->CreateSRV(srvDesc);
		envIBLTexture->CreateUAV(uavDesc);
		skyIBLTexture = new Texture2D(texDesc);
		skyIBLTexture->CreateSRV(srvDesc);
		skyIBLTexture->CreateUAV(uavDesc);
	}

	auto device = globals::d3d::device;

	logger::debug("Loading static Diffuse AmbientProbe textures...");
	{
		DirectX::ScratchImage image;
		try {
			std::filesystem::path path = "Data\\Shaders\\AmbientProbe\\DiffuseAmbientProbe.dds";

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

		staticDiffuseAmbientTexture = eastl::make_unique<Texture2D>(reinterpret_cast<ID3D11Texture2D*>(pResource), "AmbientProbe::StaticDiffuse");

		staticDiffuseAmbientTexture->desc.MiscFlags |= D3D11_RESOURCE_MISC_TEXTURECUBE;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = staticDiffuseAmbientTexture->desc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE,
			.TextureCube = {
				.MostDetailedMip = 0,
				.MipLevels = 1 }
		};
		staticDiffuseAmbientTexture->CreateSRV(srvDesc);
	}

	logger::debug("Loading static Specular AmbientProbe textures...");
	{
		DirectX::ScratchImage image;
		try {
			std::filesystem::path path = "Data\\Shaders\\AmbientProbe\\SpecAmbientProbe.dds";

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

		staticSpecularIBLTexture = eastl::make_unique<Texture2D>(reinterpret_cast<ID3D11Texture2D*>(pResource), "AmbientProbe::StaticSpecular");

		staticSpecularIBLTexture->desc.MiscFlags |= D3D11_RESOURCE_MISC_TEXTURECUBE;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = staticSpecularIBLTexture->desc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE,
			.TextureCube = {
				.MostDetailedMip = 0,
				.MipLevels = 8 }
		};
		staticSpecularIBLTexture->CreateSRV(srvDesc);
	}
}

void AmbientProbe::ClearShaderCache()
{
	if (diffuseIBLCS)
		diffuseIBLCS->Release();
	diffuseIBLCS = nullptr;
	diffuseAmbientCompileAttempted = false;
	unavailableResourcesReported = false;
}

ID3D11ComputeShader* AmbientProbe::GetDiffuseAmbientCS()
{
	std::vector<std::pair<const char*, const char*>> defines;
	if (globals::pipeline::worldProbes.loaded)
		defines.push_back({ "WORLD_PROBES", nullptr });
	if (!diffuseIBLCS && !diffuseAmbientCompileAttempted) {
		diffuseAmbientCompileAttempted = true;
		diffuseIBLCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\AmbientProbe\\DiffuseAmbientProbeCS.hlsl", defines, "cs_5_0"));
		if (!diffuseIBLCS)
			logger::error("Ambient Probe compute shader is unavailable; projection will remain disabled until the shader cache is reloaded");
	}
	return diffuseIBLCS;
}
