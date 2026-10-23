// Community Shaders Screen-Space Shadows-derived file.
// Modified for PIXL Renderer, 2026: Directional SSS source tracking, tuning and UI.
// SPDX-License-Identifier: GPL-3.0-or-later
// Third-party Bend/Sony code keeps its separate notices in the adjacent files.

#include "ContactShadows.h"

#include "Modules/TerrainSeam.h"
#include "I18n/I18n.h"
#include "State.h"
#include "Utils/D3D.h"

#include <algorithm>
#include <cmath>
#include <exception>

#define I18N_KEY_PREFIX "feature.contact_shadows."

#pragma warning(push)
#pragma warning(disable: 4838 4244)
#include "ContactShadows/bend_sss_cpu.h"
#pragma warning(pop)

using RE::RENDER_TARGETS;

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	ContactShadows::BendSettings,
	Enable,
	SampleCount,
	SurfaceThickness,
	BilinearThreshold,
	ShadowContrast,
	Strength,
	FalloffStart,
	FalloffEnd)

void ContactShadows::DrawSettings()
{
	if (ImGui::TreeNodeEx(T(TKEY("general"), "General"), ImGuiTreeNodeFlags_DefaultOpen)) {
		Util::UIntCheckbox(T(TKEY("enable"), "Directional SSS"), &bendSettings.Enable);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", T(TKEY("enable_tooltip"), "Refines the active Skyrim sun or moon directional shadow. The source is read every frame; PBR Local Contact Shadows remains separate for point and clustered lights."));
		ImGui::Checkbox("Objects Cast Shadows on Grass", &EnableGrassShadowCasting);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Controls Skyrim's bShadowsOnGrass receiver option: nearby objects can cast shadows onto grass. This does not make grass cast shadows. The setting works independently of Directional SSS and the original Skyrim value is restored when disabled.");

		Util::UIntSlider(T(TKEY("sample_count"), "Directional Ray Quality"), &bendSettings.SampleCount, 1, 12);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("sample_count_tooltip"), "Multiplier for directional ray samples. High uses 3; Cinematic uses 6. Adapts to render resolution and rebuilds the ray-march shader when needed."));

		ImGui::SliderFloat(T(TKEY("surface_thickness"), "Surface Thickness"), &bendSettings.SurfaceThickness, 0.005f, 0.05f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("surface_thickness_tooltip"), "Assumed thickness of surfaces for shadow detection. Lower values produce thinner, more precise shadows."));

		ImGui::SliderFloat(T(TKEY("bilinear_threshold"), "Edge Smoothing Threshold"), &bendSettings.BilinearThreshold, 0.02f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("bilinear_threshold_tooltip"), "Depth threshold for edge detection during bilinear interpolation. Higher values smooth more aggressively across edges."));

		ImGui::SliderFloat(T(TKEY("shadow_contrast"), "Shadow Edge Contrast"), &bendSettings.ShadowContrast, 1.0f, 4.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("shadow_contrast_tooltip"), "Contrast boost for the shadow transition. 1 preserves the traced visibility; higher values produce harder contact edges."));

		ImGui::SliderFloat("Directional SSS Strength", &bendSettings.Strength, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Blends PIXL's depth-reconstructed sun/moon visibility with Skyrim's directional shadow map. Zero keeps Skyrim shadows; one applies the complete PIXL refinement.");

		ImGui::SliderFloat("SSS Falloff Start", &bendSettings.FalloffStart, 256.0f, 20000.0f, "%.0f units", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("SSS Falloff End", &bendSettings.FalloffEnd, 512.0f, 24000.0f, "%.0f units", ImGuiSliderFlags_AlwaysClamp);
		bendSettings.FalloffEnd = std::max(bendSettings.FalloffEnd, bendSettings.FalloffStart + 256.0f);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Fades the screen-space directional shadow to neutral over view distance. Lower values reduce raymarch work and avoid unstable far-depth shadows; Skyrim's normal shadow map remains active beyond this range.");
		ImGui::TextDisabled("Current fade-out: %.0f m (%.0f Skyrim units)", bendSettings.FalloffEnd / 70.0f, bendSettings.FalloffEnd);

		ImGui::Spacing();
		ImGui::Spacing();
		ImGui::TreePop();
	}
}

void ContactShadows::UpdateGrassShadowSetting()
{
	auto* setting = globals::game::bShadowsOnGrass;
	if (!setting)
		return;

	const bool shouldEnable = EnableGrassShadowCasting;
	if (shouldEnable) {
		if (!capturedGrassShadowSetting) {
			originalGrassShadowSetting = setting->data.b;
			capturedGrassShadowSetting = true;
			logger::info("[PIXL][ShadowsOnGrass] Skyrim object-to-grass shadows requested; prior bShadowsOnGrass was {}.",
				originalGrassShadowSetting ? "enabled" : "disabled");
		}
		if (!setting->data.b) {
			setting->data.b = true;
			logger::info("[PIXL][ShadowsOnGrass] Enabled Skyrim object shadows on grass (grass remains a receiver, not a caster).");
		}
	} else if (capturedGrassShadowSetting) {
		if (setting->data.b != originalGrassShadowSetting) {
			setting->data.b = originalGrassShadowSetting;
			logger::info("[PIXL][ShadowsOnGrass] Restored Skyrim bShadowsOnGrass to its prior setting.");
		}
		capturedGrassShadowSetting = false;
	}
}

bool ContactShadows::EnsureShadowTexture(std::uint32_t width, std::uint32_t height)
{
	if (!globals::d3d::device || !globals::d3d::context || width == 0 || height == 0)
		return false;
	if (contactShadowsTexture &&
		contactShadowsTexture->desc.Width == width &&
		contactShadowsTexture->desc.Height == height)
		return true;

	ID3D11ShaderResourceView* nullView = nullptr;
	globals::d3d::context->PSSetShaderResources(45, 1, &nullView);
	contactShadowsTexture.reset();

	D3D11_TEXTURE2D_DESC textureDesc{};
	textureDesc.Width = width;
	textureDesc.Height = height;
	textureDesc.MipLevels = 1;
	textureDesc.ArraySize = 1;
	textureDesc.Format = DXGI_FORMAT_R8_UNORM;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.Usage = D3D11_USAGE_DEFAULT;
	textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

	try {
		auto texture = std::make_unique<Texture2D>(textureDesc, "ContactShadows::ShadowTexture");
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = textureDesc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;
		texture->CreateSRV(srvDesc);
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = textureDesc.Format;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Texture2D.MipSlice = 0;
		texture->CreateUAV(uavDesc);
		contactShadowsTexture = std::move(texture);
		logger::info("[PIXL][DirectionalSSS] Shadow field matches render resolution: {}x{}.", width, height);
		return true;
	} catch (const std::exception& error) {
		logger::error("[PIXL][DirectionalSSS] Could not create {}x{} shadow field: {}", width, height, error.what());
		return false;
	}
}

void ContactShadows::UpdateDirectionalSource(const void* source)
{
	if (activeDirectionalSource == source)
		return;

	activeDirectionalSource = source;
	++directionalSourceEpoch;
	if (source) {
		logger::info("[PIXL][DirectionalSSS] Active directional source updated (epoch {}).", directionalSourceEpoch);
	} else {
		logger::info("[PIXL][DirectionalSSS] No active directional source; directional visibility remains neutral.");
	}
}

void ContactShadows::InvalidateRaymarchShaders()
{
	if (raymarchCS) {
		raymarchCS->Release();
		raymarchCS = nullptr;
	}
}

void ContactShadows::ClearShaderCache()
{
	InvalidateRaymarchShaders();
}

uint ContactShadows::GetScaledSampleCount()
{
	const auto* graphicsState = globals::game::graphicsState;
	if (!graphicsState)
		return 8u;

	float2 renderSize = Util::ConvertToDynamic(float2{ (float)graphicsState->screenWidth, (float)graphicsState->screenHeight });
	renderSize.x = std::max(renderSize.x, 1.0f);
	renderSize.y = std::max(renderSize.y, 1.0f);

	// Scale sample count based on both dimensions relative to 1920x1080 reference
	float2 referenceRes = { 1920.0f, 1080.0f };
	float referenceArea = referenceRes.x * referenceRes.y;
	float currentArea = renderSize.x * renderSize.y;
	float areaScale = std::clamp(std::sqrt(currentArea / referenceArea), 0.5f, 4.0f);
	uint scaledSampleCount = static_cast<uint>(std::round(bendSettings.SampleCount * 60 * areaScale));

	// Quantize to steps of 8 to prevent frequent recompilation from small DRS oscillations
	scaledSampleCount = ((scaledSampleCount + 7u) / 8u) * 8u;
	scaledSampleCount = std::clamp(scaledSampleCount, 8u, 960u);

	return scaledSampleCount;
}

ID3D11ComputeShader* ContactShadows::GetComputeRaymarch()
{
	uint scaledSampleCount = GetScaledSampleCount();

	if (scaledSampleCount != lastCompiledSampleCount) {
		lastCompiledSampleCount = scaledSampleCount;
		InvalidateRaymarchShaders();
	}

	if (!raymarchCS) {
		auto sampleCount = std::format("{}", scaledSampleCount);
		std::vector<std::pair<const char*, const char*>> defines{ { "SAMPLE_COUNT", sampleCount.c_str() } };
		// TERRAIN_SEAM flips DepthTexture's HLSL type from `Texture2D<unorm float>`
		// (R24_UNORM_X8_TYPELESS game depth) to `Texture2D<float>` (R32_FLOAT blendedDepth).
		if (globals::pipeline::terrainSeam.loaded)
			defines.push_back({ "TERRAIN_SEAM", "" });
		raymarchCS = (ID3D11ComputeShader*)Util::CompileShader(L"Data\\PIXL-Shaders\\ContactShadows\\RaymarchCS.hlsl", defines, "cs_5_0");
	}
	return raymarchCS;
}

void ContactShadows::DrawShadows()
{
	ZoneScopedS(8);
	if (!globals::state || !globals::profiler ||
		!globals::d3d::context || !globals::game::graphicsState || !raymarchCB ||
		!contactShadowsTexture || !contactShadowsTexture->uav || !pointBorderSampler) {
		return;
	}

	TracyD3D11Zone(globals::state->tracyCtx, "Contact Shadows");

	auto context = globals::d3d::context;
	// State::UpdateSharedData uploads this same direction to b5 immediately before
	// module prepasses. Reuse that CPU mirror instead of independently rebuilding
	// it from a potentially different active-shadow-node light. This also follows
	// Sky Continuity's final sun/moon direction and its transition fader exactly.
	RE::ShadowSceneNode* shadowSceneNode = globals::game::smState ?
		globals::game::smState->shadowSceneNode[0] : nullptr;
	if (!shadowSceneNode || !shadowSceneNode->GetRuntimeData().sunLight ||
		!shadowSceneNode->GetRuntimeData().sunLight->light) {
		UpdateDirectionalSource(nullptr);
		return;
	}
	UpdateDirectionalSource(shadowSceneNode->GetRuntimeData().sunLight->light.get());
	float3 light = globals::state->currentDirectionalLightDirection;
	if (!std::isfinite(light.x) || !std::isfinite(light.y) || !std::isfinite(light.z) ||
		light.LengthSquared() < 1.0e-6f) {
		UpdateDirectionalSource(nullptr);
		return;
	}
	light.Normalize();
	float4 lightProjection = float4(light.x, light.y, light.z, 0.0f);

	// Helper lambda to calculate light projection
	auto CalculateLightProjection = [&]() -> std::array<float, 4> {
		auto viewProjMat = globals::game::frameBufferCached.GetCameraViewProj().Transpose();
		auto projectedLight = DirectX::SimpleMath::Vector4::Transform(lightProjection, viewProjMat);
		return { projectedLight.x, projectedLight.y, projectedLight.z, projectedLight.w };
	};

	auto lightProjectionF = CalculateLightProjection();

	float2 renderSize = Util::ConvertToDynamic(float2{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight });
	int viewportSize[2] = { (int)renderSize.x, (int)renderSize.y };
	if (viewportSize[0] <= 0 || viewportSize[1] <= 0)
		return;

	int minRenderBounds[2] = { 0, 0 };
	int maxRenderBounds[2] = { viewportSize[0], viewportSize[1] };

	// Setup common render state.
	// SSS always uses 24/32-bit depth, never the R16_UNORM half-precision path.
	// With TerrainSeam loaded the SRV is R32_FLOAT (blendedDepthTexture);
	// without it, the game's kPOST_ZPREPASS_COPY (R24_UNORM_X8_TYPELESS).
	// The shader's DepthTexture declaration is conditional on TERRAIN_SEAM:
	// `<float>` for the R32_FLOAT path, `<unorm float>` for the R24_UNORM path.
	auto* depthSRV = Util::GetCurrentSceneDepthSRV(false);
	auto* raymarchShader = GetComputeRaymarch();
	if (!depthSRV || !raymarchShader)
		return;
	context->CSSetShaderResources(0, 1, &depthSRV);

	auto uav = contactShadowsTexture->uav.get();
	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

	context->CSSetSamplers(0, 1, &pointBorderSampler);

	auto buffer = raymarchCB->CB();
	context->CSSetConstantBuffers(1, 1, &buffer);

	auto viewport = globals::game::graphicsState;

	float2 dynamicRes = { viewport->GetRuntimeData().dynamicResolutionWidthRatio, viewport->GetRuntimeData().dynamicResolutionHeightRatio };

	// Shared dispatch logic
	auto Dispatch = [&](ID3D11ComputeShader* shader, const float* lightProj,
						float invTexSizeX, float invTexSizeY) {
		globals::profiler->BeginPass("ContactShadows::RayMarch");

		if (globals::state->frameAnnotations) {
			globals::state->BeginPerfEvent("Contact Shadows - Ray March");
		}

		context->CSSetShader(shader, nullptr, 0);

		auto dispatchList = Bend::BuildDispatchList(const_cast<float*>(lightProj), viewportSize, minRenderBounds, maxRenderBounds);

		for (int i = 0; i < dispatchList.DispatchCount; i++) {
			auto dispatchData = dispatchList.Dispatch[i];

			{
				TracyD3D11Zone(globals::state->tracyCtx, "Contact Shadows - Dispatch CB");

				RaymarchCB data{};
				data.LightCoordinate[0] = dispatchList.LightCoordinate_Shader[0];
				data.LightCoordinate[1] = dispatchList.LightCoordinate_Shader[1];
				data.LightCoordinate[2] = dispatchList.LightCoordinate_Shader[2];
				data.LightCoordinate[3] = dispatchList.LightCoordinate_Shader[3];

				data.WaveOffset[0] = dispatchData.WaveOffset_Shader[0];
				data.WaveOffset[1] = dispatchData.WaveOffset_Shader[1];

				// Bend's Skyrim integration was runtime-validated with this ordering.
				// Reversing the pair makes the generated visibility buffer approach
				// zero over the complete screen and suppresses all directional light.
				data.FarDepthValue = 1.0f;
				data.NearDepthValue = 0.0f;

				data.DynamicRes = dynamicRes;

				data.InvDepthTextureSize[0] = invTexSizeX;
				data.InvDepthTextureSize[1] = invTexSizeY;

				data.settings = bendSettings;

				raymarchCB->Update(data);
			}

			{
				TracyD3D11Zone(globals::state->tracyCtx, "Contact Shadows - Dispatch Sweep");
				context->Dispatch(dispatchData.WaveCount[0], dispatchData.WaveCount[1], dispatchData.WaveCount[2]);
			}
		}

		if (globals::state->frameAnnotations) {
			globals::state->EndPerfEvent();
		}

		globals::profiler->EndPass();
	};

	float InvTexSizeX = 1.0f / (float)viewportSize[0];
	float InvTexSizeY = 1.0f / (float)viewportSize[1];

	Dispatch(raymarchShader, lightProjectionF.data(), InvTexSizeX, InvTexSizeY);

	ID3D11ShaderResourceView* views[1]{ nullptr };
	context->CSSetShaderResources(0, 1, views);

	ID3D11UnorderedAccessView* uavs[1]{ nullptr };
	context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

	context->CSSetShader(nullptr, nullptr, 0);

	ID3D11SamplerState* sampler = nullptr;
	context->CSSetSamplers(0, 1, &sampler);

	buffer = nullptr;
	context->CSSetConstantBuffers(1, 1, &buffer);
}

void ContactShadows::Prepass()
{
	UpdateGrassShadowSetting();
	auto context = globals::d3d::context;
	if (!context || !globals::game::graphicsState) {
		return;
	}
	auto renderSize = Util::ConvertToDynamic(float2{
		static_cast<float>(globals::game::graphicsState->screenWidth),
		static_cast<float>(globals::game::graphicsState->screenHeight) });
	const auto width = static_cast<std::uint32_t>(std::max(std::lround(renderSize.x), 0l));
	const auto height = static_cast<std::uint32_t>(std::max(std::lround(renderSize.y), 0l));
	if (!EnsureShadowTexture(width, height) || !contactShadowsTexture->uav || !contactShadowsTexture->srv)
		return;

	// Unbind last frame's pixel-shader SRV before clearing/writing through the UAV.
	ID3D11ShaderResourceView* nullView = nullptr;
	context->PSSetShaderResources(45, 1, &nullView);

	float white[4] = { 1, 1, 1, 1 };
	context->ClearUnorderedAccessViewFloat(contactShadowsTexture->uav.get(), white);

	// Source availability, not sky mode, decides whether the pass can run. Skyrim
	// can expose a valid directional light in partial-sky/interior transition states.
	if (bendSettings.Enable) {
		DrawShadows();
	} else {
		UpdateDirectionalSource(nullptr);
	}

	auto view = contactShadowsTexture->srv.get();
	context->PSSetShaderResources(45, 1, &view);
}

void ContactShadows::LoadSettings(json& o_json)
{
	bendSettings = o_json;
	const BendSettings defaults{};
	auto finiteClamp = [](float value, float fallback, float minimum, float maximum) {
		return std::clamp(std::isfinite(value) ? value : fallback, minimum, maximum);
	};
	EnableGrassShadowCasting = o_json.is_object() ? o_json.value("EnableGrassShadowCasting", true) : true;
	bendSettings.Enable = bendSettings.Enable ? 1u : 0u;
	bendSettings.SampleCount = std::clamp(bendSettings.SampleCount, 1u, 12u);
	bendSettings.SurfaceThickness = finiteClamp(bendSettings.SurfaceThickness, defaults.SurfaceThickness, 0.005f, 0.05f);
	bendSettings.BilinearThreshold = finiteClamp(bendSettings.BilinearThreshold, defaults.BilinearThreshold, 0.02f, 1.0f);
	bendSettings.ShadowContrast = finiteClamp(bendSettings.ShadowContrast, defaults.ShadowContrast, 1.0f, 4.0f);
	bendSettings.Strength = finiteClamp(bendSettings.Strength, defaults.Strength, 0.0f, 1.0f);
	bendSettings.FalloffStart = finiteClamp(bendSettings.FalloffStart, defaults.FalloffStart, 256.0f, 20000.0f);
	bendSettings.FalloffEnd = std::max(finiteClamp(bendSettings.FalloffEnd, defaults.FalloffEnd, 512.0f, 24000.0f),
		bendSettings.FalloffStart + 256.0f);
}

void ContactShadows::SaveSettings(json& o_json)
{
	o_json = bendSettings;
	o_json["EnableGrassShadowCasting"] = EnableGrassShadowCasting;
}

void ContactShadows::Reset()
{
	// Reset is called from the presentation lifecycle, not only on cell changes.
	// DrawShadows reacquires the source every prepass and never dereferences this
	// diagnostic identity, so clearing it here caused false source churn each frame.
}

void ContactShadows::RestoreDefaultSettings()
{
	bendSettings = {};
}

bool ContactShadows::HasShaderDefine(RE::BSShader::Type shaderType)
{
	return shaderType == RE::BSShader::Type::Lighting ||
	       shaderType == RE::BSShader::Type::Grass ||
	       shaderType == RE::BSShader::Type::DistantTree;
}

void ContactShadows::SetupResources()
{
	if (!globals::d3d::device || !globals::d3d::context || !globals::game::renderer || !globals::game::graphicsState) {
		logger::error("[PIXL Contact Shadows] D3D11 renderer state unavailable; resources were not created");
		return;
	}

	raymarchCB = new ConstantBuffer(ConstantBufferDesc<RaymarchCB>(), "ContactShadows::RaymarchCB");

	{
		auto device = globals::d3d::device;

		D3D11_SAMPLER_DESC samplerDesc = {};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
		samplerDesc.MaxAnisotropy = 1;
		samplerDesc.MinLOD = 0;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		// Match the far-depth sentinel supplied to Bend above. Its sweep samples
		// outside the viewport deliberately, so the border must remain invalid.
		samplerDesc.BorderColor[0] = 1.0f;
		samplerDesc.BorderColor[1] = 1.0f;
		samplerDesc.BorderColor[2] = 1.0f;
		samplerDesc.BorderColor[3] = 1.0f;
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, &pointBorderSampler));
		Util::SetResourceName(pointBorderSampler, "ContactShadows::PointBorderSampler");
	}

	auto renderSize = Util::ConvertToDynamic(float2{
		static_cast<float>(globals::game::graphicsState->screenWidth),
		static_cast<float>(globals::game::graphicsState->screenHeight) });
	EnsureShadowTexture(
		static_cast<std::uint32_t>(std::max(std::lround(renderSize.x), 0l)),
		static_cast<std::uint32_t>(std::max(std::lround(renderSize.y), 0l)));
}
#undef I18N_KEY_PREFIX
