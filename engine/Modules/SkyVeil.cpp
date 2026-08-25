#include "SkyVeil.h"

#include "../I18n/I18n.h"
#include "Globals.h"
#include "State.h"
#include "Utils/D3D.h"

#define I18N_KEY_PREFIX "feature.sky_veil."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	SkyVeil::Settings,
	Opacity,
	EnableVolumetricClouds,
	CloudDensity,
	CloudDepth,
	SelfShadowStrength,
	SilverLining,
	AmbientLighting,
	DetailStrength,
	PhaseEccentricity,
	HorizonFade)

void SkyVeil::DrawSettings()
{
	ImGui::SliderFloat(T(TKEY("opacity"), "Opacity"), &settings.Opacity, 0.0f, 4.0f, "%.1f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("opacity_tooltip"),
							  "Higher values make cloud shadows darker."));
	}

	ImGui::Spacing();
	if (ImGui::TreeNodeEx(T(TKEY("volumetric_clouds"), "Volumetric Cloud Lighting"), ImGuiTreeNodeFlags_DefaultOpen)) {
		Util::UIntCheckbox(T(TKEY("enable_volumetric_clouds"), "Enable Layered Volumetrics"), &settings.EnableVolumetricClouds);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextWrapped("%s", T(TKEY("enable_volumetric_clouds_tooltip"),
				"Adds depth, self-shadowing and directional scattering to Skyrim's animated cloud layers. Real-time; no restart required."));
		}

		ImGui::BeginDisabled(settings.EnableVolumetricClouds == 0);
		ImGui::SliderFloat(T(TKEY("cloud_density"), "Optical Density"), &settings.CloudDensity, 0.25f, 2.5f, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", T(TKEY("cloud_density_tooltip"), "Controls cloud extinction and perceived body. Higher values produce denser storm clouds."));
		ImGui::SliderFloat(T(TKEY("cloud_depth"), "Layer Depth"), &settings.CloudDepth, 0.0f, 1.0f, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", T(TKEY("cloud_depth_tooltip"), "Controls parallax separation between density strata without changing the cloud mesh."));
		ImGui::SliderFloat(T(TKEY("cloud_self_shadow"), "Self-Shadow Strength"), &settings.SelfShadowStrength, 0.0f, 1.5f, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", T(TKEY("cloud_self_shadow_tooltip"), "Darkens dense cloud interiors according to the sun direction."));
		ImGui::SliderFloat(T(TKEY("cloud_silver_lining"), "Silver Lining"), &settings.SilverLining, 0.0f, 2.0f, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", T(TKEY("cloud_silver_lining_tooltip"), "Strength of forward-scattered sunlight around thin cloud edges."));
		ImGui::SliderFloat(T(TKEY("cloud_ambient"), "Ambient Fill"), &settings.AmbientLighting, 0.0f, 1.0f, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", T(TKEY("cloud_ambient_tooltip"), "Minimum multiple-scattering fill inside dense clouds. Raise this if overcast clouds become too dark."));
		ImGui::SliderFloat(T(TKEY("cloud_detail"), "Density Detail"), &settings.DetailStrength, 0.0f, 1.0f, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", T(TKEY("cloud_detail_tooltip"), "Blends additional offset density strata to reduce the flat-card appearance."));
		ImGui::SliderFloat(T(TKEY("cloud_phase"), "Forward Scattering"), &settings.PhaseEccentricity, 0.0f, 0.85f, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", T(TKEY("cloud_phase_tooltip"), "Anisotropy of sunlight through clouds. High values create a tighter glow near the sun."));
		ImGui::SliderFloat(T(TKEY("cloud_horizon_fade"), "Horizon Stability"), &settings.HorizonFade, 0.0f, 1.0f, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", T(TKEY("cloud_horizon_fade_tooltip"), "Reduces texture stretching and excessive layer separation near the horizon."));
		ImGui::EndDisabled();
		ImGui::TreePop();
	}
}

#undef I18N_KEY_PREFIX

void SkyVeil::LoadSettings(json& o_json)
{
	settings = o_json;
}

void SkyVeil::SaveSettings(json& o_json)
{
	o_json = settings;
}

void SkyVeil::RestoreDefaultSettings()
{
	settings = {};
}

SkyVeil::Settings SkyVeil::GetCommonBufferData()
{
	if (!loaded)
		return settings;

	auto data = settings;

	return data;
}

void SkyVeil::CheckResourcesSide(int side)
{
	static Util::FrameChecker frame_checker[6];
	if (!frame_checker[side].IsNewFrame())
		return;

	if (previouslyRenderedSide >= 0 && previouslyRenderedSide != side)
		PropagateToCompletion(previouslyRenderedSide);
	previouslyRenderedSide = side;

	auto context = globals::d3d::context;

	float black[4] = { 0, 0, 0, 0 };
	context->ClearRenderTargetView(cloudShadowLayerRTVs[0][side], black);
	renderedLayersMask[side] = 0;
}

void SkyVeil::PropagateToCompletion(int side)
{
	uint32_t mask = renderedLayersMask[side];
	unsigned long highBit;
	int fromLayer = _BitScanReverse(&highBit, mask) ? static_cast<int>(highBit) : 0;

	auto context = globals::d3d::context;

	uint32_t newLayers = mask & ~globalRenderedMask;
	if (newLayers) {
		unsigned long bit;
		uint32_t remaining = newLayers;
		while (_BitScanForward(&bit, remaining)) {
			int newLayer = static_cast<int>(bit);
			for (int otherSide = 0; otherSide < 6; otherSide++) {
				if (otherSide == side)
					continue;
				if (renderedLayersMask[otherSide] & (1u << newLayer))
					continue;
				uint32_t belowMask = renderedLayersMask[otherSide] & ((1u << newLayer) - 1);
				unsigned long nearestBit;
				int srcLayer = _BitScanReverse(&nearestBit, belowMask) ? static_cast<int>(nearestBit) : 0;
				UINT otherSub = D3D11CalcSubresource(0, otherSide, cubemapMipLevels);
				context->CopySubresourceRegion(
					texCloudShadowLayers[newLayer]->resource.get(), otherSub, 0, 0, 0,
					texCloudShadowLayers[srcLayer]->resource.get(), otherSub, nullptr);
				renderedLayersMask[otherSide] |= (1u << newLayer);
			}
			remaining &= ~(1u << bit);
		}
		globalRenderedMask |= mask;
	}

	if (fromLayer < kMaxCloudLayers - 1) {
		UINT subresource = D3D11CalcSubresource(0, side, cubemapMipLevels);
		context->CopySubresourceRegion(
			texCloudShadowLayers[kMaxCloudLayers - 1]->resource.get(), subresource, 0, 0, 0,
			texCloudShadowLayers[fromLayer]->resource.get(), subresource, nullptr);
	}
}

void SkyVeil::SkyShaderHacks()
{
	if (overrideSky) {
		auto renderer = globals::game::renderer;
		auto context = globals::d3d::context;

		auto reflections = renderer->GetRendererData().cubemapRenderTargets[RE::RENDER_TARGET_CUBEMAP::kREFLECTIONS];

		// render targets
		ID3D11RenderTargetView* rtvs[4];
		ID3D11DepthStencilView* dsv;
		context->OMGetRenderTargets(3, rtvs, &dsv);

		int side = -1;
		for (int i = 0; i < 6; ++i)
			if (rtvs[0] == reflections.cubeSideRTV[i]) {
				side = i;
				break;
			}
		if (side == -1)
			return;

		CheckResourcesSide(side);

		int layer = currentLayerForDraw;

		unsigned long highBit;
		int prevLayer = _BitScanReverse(&highBit, renderedLayersMask[side]) ? static_cast<int>(highBit) : -1;

		UINT subresource = D3D11CalcSubresource(0, side, cubemapMipLevels);

		int fromLayer = std::max(prevLayer, 0);

		context->CopyResource(texSelfShadowCopy->resource.get(), texCloudShadowLayers[layer]->resource.get());

		if (layer > 0) {
			context->CopySubresourceRegion(
				texCloudShadowLayers[layer]->resource.get(), subresource, 0, 0, 0,
				texCloudShadowLayers[fromLayer]->resource.get(), subresource, nullptr);
		}

		ID3D11ShaderResourceView* selfShadowSrv = texSelfShadowCopy->srv.get();
		context->PSSetShaderResources(26, 1, &selfShadowSrv);

		rtvs[3] = cloudShadowLayerRTVs[layer][side];
		context->OMSetRenderTargets(4, rtvs, nullptr);

		float blendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		UINT sampleMask = 0xffffffff;

		context->OMSetBlendState(cloudShadowBlendState, blendFactor, sampleMask);

		auto cubemapDepth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kCUBEMAP_REFLECTIONS];
		context->PSSetShaderResources(17, 1, &cubemapDepth.depthSRV);

		// Release COM objects to prevent memory leaks
		for (int i = 0; i < 3; ++i) {
			if (rtvs[i])
				rtvs[i]->Release();
		}
		if (dsv)
			dsv->Release();

		renderedLayersMask[side] |= (1u << layer);

		overrideSky = false;
	}
}

int SkyVeil::FindCloudLayer(RE::BSRenderPass* Pass)
{
	auto sky = globals::game::sky;
	if (!sky || !sky->clouds)
		return -1;

	for (int i = 0; i < kMaxCloudLayers; i++) {
		if (sky->clouds->clouds[i].get() == Pass->geometry)
			return i;
	}
	return -1;
}

void SkyVeil::ModifySky(RE::BSRenderPass* Pass)
{
	auto shadowState = globals::game::shadowState;

	auto& cubeMapRenderTarget = shadowState->GetRuntimeData().cubeMapRenderTarget;

	auto skyProperty = static_cast<const RE::BSSkyShaderProperty*>(Pass->shaderProperty);

	if (skyProperty->uiSkyObjectType != RE::BSSkyShaderProperty::SkyObject::SO_CLOUDS)
		return;

	int layer = FindCloudLayer(Pass);
	if (layer < 0)
		return;

	if (cubeMapRenderTarget == RE::RENDER_TARGETS_CUBEMAP::kREFLECTIONS) {
		currentLayerForDraw = layer;
		overrideSky = true;
	} else {
		auto context = globals::d3d::context;
		ID3D11ShaderResourceView* srv = texCloudShadowLayers[layer]->srv.get();
		context->PSSetShaderResources(26, 1, &srv);
	}
}

void SkyVeil::ReflectionsPrepass()
{
	Util::FrameChecker frameChecker;
	if (frameChecker.IsNewFrame()) {
		if ((globals::game::sky->mode.get() != RE::Sky::Mode::kFull) ||
			!globals::game::sky->currentClimate)
			return;

		auto context = globals::d3d::context;

		context->CopyResource(texCubemapCloudOccCopy->resource.get(), texCloudShadowLayers[kMaxCloudLayers - 1]->resource.get());

		ID3D11ShaderResourceView* srv = texCubemapCloudOccCopy->srv.get();
		context->PSSetShaderResources(25, 1, &srv);
		context->CSSetShaderResources(25, 1, &srv);
	}
}

void SkyVeil::EarlyPrepass()
{
	if (previouslyRenderedSide >= 0) {
		PropagateToCompletion(previouslyRenderedSide);
		previouslyRenderedSide = -1;
	}
	globalRenderedMask = 0;
}

void SkyVeil::SetupResources()
{
	auto renderer = globals::game::renderer;
	auto device = globals::d3d::device;

	{
		auto reflections = renderer->GetRendererData().cubemapRenderTargets[RE::RENDER_TARGET_CUBEMAP::kREFLECTIONS];

		D3D11_TEXTURE2D_DESC texDesc{};
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};

		reflections.texture->GetDesc(&texDesc);
		reflections.SRV->GetDesc(&srvDesc);

		texDesc.Format = srvDesc.Format = DXGI_FORMAT_R8_UNORM;
		cubemapMipLevels = texDesc.MipLevels;

		for (int layer = 0; layer < kMaxCloudLayers; ++layer) {
			char name[64];
			snprintf(name, sizeof(name), "SkyVeil::Layer[%d]", layer);
			texCloudShadowLayers[layer] = new Texture2D(texDesc, name);
			texCloudShadowLayers[layer]->CreateSRV(srvDesc);

			for (int face = 0; face < 6; ++face) {
				reflections.cubeSideRTV[face]->GetDesc(&rtvDesc);
				rtvDesc.Format = texDesc.Format;
				DX::ThrowIfFailed(device->CreateRenderTargetView(texCloudShadowLayers[layer]->resource.get(), &rtvDesc, &cloudShadowLayerRTVs[layer][face]));
				Util::SetResourceName(cloudShadowLayerRTVs[layer][face], "SkyVeil::Layer[%d] RTV[%d]", layer, face);
			}
		}

		texCubemapCloudOccCopy = new Texture2D(texDesc, "SkyVeil::CubemapCloudOccCopy");
		texCubemapCloudOccCopy->CreateSRV(srvDesc);

		texSelfShadowCopy = new Texture2D(texDesc, "SkyVeil::SelfShadowCopy");
		texSelfShadowCopy->CreateSRV(srvDesc);
	}
	{
		D3D11_BLEND_DESC blendDesc = {};
		blendDesc.AlphaToCoverageEnable = false;
		blendDesc.IndependentBlendEnable = false;

		blendDesc.RenderTarget[0].BlendEnable = true;
		blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

		DX::ThrowIfFailed(device->CreateBlendState(&blendDesc, &cloudShadowBlendState));
		Util::SetResourceName(cloudShadowBlendState, "SkyVeil::BlendState");
	}
}

void SkyVeil::Hooks::BSSkyShader_SetupMaterial::thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
{
	globals::state->UpdateSkyShaderPermutation(Pass);
	globals::pipeline::skyVeil.ModifySky(Pass);
	func(This, Pass, RenderFlags);
}
