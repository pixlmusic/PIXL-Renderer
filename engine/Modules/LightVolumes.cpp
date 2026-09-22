#include "LightVolumes.h"
#include "Atmosphere.h"

#include "I18n/I18n.h"
#include "InteriorDaylight.h"
#include "ShaderCache.h"
#include "State.h"

#define I18N_KEY_PREFIX "feature.light_volumes."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	LightVolumes::TextureSize,
	Width,
	Height,
	Depth);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	LightVolumes::Settings,
	ExteriorEnabled,
	ExteriorQuality,
	ExteriorCustomSize,
	InteriorEnabled,
	InteriorQuality,
	InteriorCustomSize);

void LightVolumes::DrawSettings()
{
	if (ImGui::Checkbox(T(TKEY("enable_exteriors"), "Enable Light Volumes in Exteriors"), &settings.ExteriorEnabled))
		SetupVL();

	if (settings.ExteriorEnabled)
		DrawVolumetricLightingSettings(settings.ExteriorQuality, settings.ExteriorCustomSize, false, !inInterior);

	if (ImGui::Checkbox(T(TKEY("enable_interiors"), "Enable Light Volumes in Interiors"), &settings.InteriorEnabled))
		SetupVL();

	if (settings.InteriorEnabled)
		DrawVolumetricLightingSettings(settings.InteriorQuality, settings.InteriorCustomSize, true, inInterior);
}

void LightVolumes::ApplyRendererQualityChange()
{
	if (initialised)
		SetupVL();
}

void LightVolumes::ApplyRendererQualityTier(const int32_t quality)
{
	// Skyrim's descriptor has three native presets. Keep the user's Custom slot
	// untouched so selecting PIXL Ultra never destroys hand-authored VL sizes.
	// A dedicated PIXL Ultra grid can be introduced here later without changing
	// the central quality-profile contract.
	const int32_t nativeQuality = std::clamp(quality, 0, static_cast<int32_t>(Quality::High));
	const bool changed =
		settings.ExteriorQuality != nativeQuality ||
		settings.InteriorQuality != nativeQuality;

	settings.ExteriorQuality = nativeQuality;
	settings.InteriorQuality = nativeQuality;

	if (changed && initialised)
		SetupVL();
}

void LightVolumes::DrawVolumetricLightingSettings(int32_t& quality, TextureSize& customSize, const bool isInterior, const bool inLocationType)
{
	auto& [Width, Height, Depth] = FetchCurrentSizeInUnits(isInterior);
	const char* qualityNames[] = {
		T(TKEY("quality_low"), "Low"),
		T(TKEY("quality_medium"), "Medium"),
		T(TKEY("quality_high"), "High"),
		T(TKEY("quality_custom"), "Custom")
	};

	if (ImGui::SliderInt(isInterior ? T(TKEY("interior_quality"), "Interior Quality") : T(TKEY("exterior_quality"), "Exterior Quality"), &quality, 0, static_cast<uint8_t>(Quality::Count) - 1, qualityNames[quality])) {
		if (inLocationType)
			SetupVL();
	}

	const bool isCustomQuality = static_cast<Quality>(quality) == Quality::Custom;
	if (!isCustomQuality)
		ImGui::BeginDisabled();

	if (ImGui::SliderInt(isInterior ? T(TKEY("interior_width"), "Interior Width") : T(TKEY("exterior_width"), "Exterior Width"), &Width, 1, 20, FromUnits(Width, 32), ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
		customSize.Width = Width * 32;
		if (inLocationType)
			SetupVL();
	}

	if (ImGui::SliderInt(isInterior ? T(TKEY("interior_height"), "Interior Height") : T(TKEY("exterior_height"), "Exterior Height"), &Height, 1, 20, FromUnits(Height, 32), ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
		customSize.Height = Height * 32;
		if (inLocationType)
			SetupVL();
	}

	if (ImGui::SliderInt(isInterior ? T(TKEY("interior_depth"), "Interior Depth") : T(TKEY("exterior_depth"), "Exterior Depth"), &Depth, 1, 64, FromUnits(Depth, 10), ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
		customSize.Depth = Depth * 10;
		if (inLocationType)
			SetupVL();
	}

	if (!isCustomQuality)
		ImGui::EndDisabled();
}

inline const char* LightVolumes::FromUnits(const int32_t value, const int32_t unitScale)
{
	static std::string s;
	s = std::to_string(value * unitScale);
	return s.c_str();
}

LightVolumes::TextureSize& LightVolumes::FetchCurrentSizeInUnits(const bool interior)
{
	auto& size = interior ? interiorSizeInUnits : exteriorSizeInUnits;
	if (interior) {
		switch (static_cast<Quality>(settings.InteriorQuality)) {
		case Quality::Low:
			size = *gVolumetricLightingSizeLow;
			break;
		case Quality::Medium:
			size = *gVolumetricLightingSizeMedium;
			break;
		case Quality::High:
			size = defaultSizeHigh;
			break;
		case Quality::Custom:
			size = settings.InteriorCustomSize;
			break;
		default:
			break;
		}
	} else {
		switch (static_cast<Quality>(settings.ExteriorQuality)) {
		case Quality::Low:
			size = *gVolumetricLightingSizeLow;
			break;
		case Quality::Medium:
			size = *gVolumetricLightingSizeMedium;
			break;
		case Quality::High:
			size = defaultSizeHigh;
			break;
		case Quality::Custom:
			size = settings.ExteriorCustomSize;
			break;
		default:
			break;
		}
	}

	size.Height /= 32;
	size.Width /= 32;
	size.Depth /= 10;

	return size;
}

void LightVolumes::LoadSettings(json& o_json)
{
	settings = o_json;
	settings.ExteriorQuality = std::clamp(settings.ExteriorQuality, 0, static_cast<int32_t>(Quality::Count) - 1);
	settings.InteriorQuality = std::clamp(settings.InteriorQuality, 0, static_cast<int32_t>(Quality::Count) - 1);
}

void LightVolumes::SaveSettings(json& o_json)
{
	o_json = settings;
}

void LightVolumes::RestoreDefaultSettings()
{
	settings = {};
}

void LightVolumes::DataLoaded()
{
}

void LightVolumes::PostPostLoad()
{
	gVolumetricLightingSizeLow = reinterpret_cast<TextureSize*>(REL::RelocationID(527970, 414916).address());
	gVolumetricLightingSizeMedium = reinterpret_cast<TextureSize*>(REL::RelocationID(527973, 414919).address());
	gVolumetricLightingSizeHigh = reinterpret_cast<TextureSize*>(REL::RelocationID(527976, 414922).address());
	defaultSizeHigh = *gVolumetricLightingSizeHigh;

	// Ensure the VL raymarch compute shader is only dispatched once, rather than once for every level of depth
	// The updated raymarch shader iterates through the depth now instead
	// Skip the first call, the second call has read/write texture setup in the correct order
	REL::safe_fill(REL::RelocationID(100309, 107023).address() + REL::Relocate(0xA4, 0x406), REL::NOP, 3);
	// Exit the loop after the first iteration
	REL::safe_fill(REL::RelocationID(100309, 107023).address() + REL::Relocate(0x147, 0x4A9), REL::NOP, 6);
}

void LightVolumes::SetupResources()
{
	vlDataCB = new ConstantBuffer(ConstantBufferDesc<VLData>());
}

void LightVolumes::EarlyPrepass()
{
	if (!globals::game::graphicsState || !vlDataCB)
		return;

	int32_t width = static_cast<int32_t>((float)globals::game::graphicsState->screenWidth);
	int32_t height = static_cast<int32_t>((float)globals::game::graphicsState->screenHeight);
	if (width <= 0 || height <= 0)
		return;

	if (width != vlData.screenX || height != vlData.screenY) {
		blurHCS = nullptr;
		blurVCS = nullptr;
	}

	vlData.screenX = width;
	vlData.screenY = height;
	vlData.screenXMin1 = width - 1;
	vlData.screenYMin1 = height - 1;
	vlDataCB->Update(vlData);

	auto* tes = RE::TES::GetSingleton();
	if (!tes)
		return;
	const auto interiorCell = tes->interiorCell;
	const bool currentlyInInterior = interiorCell != nullptr;
	const bool currentlyInMapMenu = globals::state && globals::state->isMapMenuOpen;
	auto& atmosphere = globals::pipeline::atmosphere;
	const bool currentlyAtmosphereMapProfileActive =
		atmosphere.loaded && atmosphere.settings.enabled != 0 && atmosphere.settings.mapAtmosphereEnabled != 0;

	if (initialised && currentlyInInterior == inInterior && currentlyInMapMenu == inMapMenu &&
		currentlyAtmosphereMapProfileActive == atmosphereMapProfileActive)
		return;

	initialised = true;
	inInterior = currentlyInInterior;
	inInteriorWithSun = InteriorDaylight::IsInteriorWithSun(interiorCell);
	inMapMenu = currentlyInMapMenu;
	atmosphereMapProfileActive = currentlyAtmosphereMapProfileActive;
	SetupVL();
}

void LightVolumes::SetupVL()
{
	// World/local maps use Atmosphere's dedicated analytical aerial perspective.
	// Skyrim's legacy light volume is screen-space/temporal and adds haze without
	// useful map depth cues, so disable it while the map camera is active.
	if (inMapMenu && atmosphereMapProfileActive) {
		*globals::game::bEnableVolumetricLighting = false;
		return;
	}

	if (inInterior) {
		*globals::game::bEnableVolumetricLighting = settings.InteriorEnabled && inInteriorWithSun;
		*gVolumetricLightingSizeHigh = static_cast<Quality>(settings.InteriorQuality) == Quality::Custom ? settings.InteriorCustomSize : defaultSizeHigh;
		SetVLQuality(GetVLDescriptor(), settings.InteriorQuality);
	} else {
		*globals::game::bEnableVolumetricLighting = settings.ExteriorEnabled;
		*gVolumetricLightingSizeHigh = static_cast<Quality>(settings.ExteriorQuality) == Quality::Custom ? settings.ExteriorCustomSize : defaultSizeHigh;
		SetVLQuality(GetVLDescriptor(), settings.ExteriorQuality);
	}
}

LightVolumes::VolumetricLightingDescriptor& LightVolumes::GetVLDescriptor()
{
	using func_t = decltype(&LightVolumes::GetVLDescriptor);
	static REL::Relocation<func_t> func{ REL::RelocationID(100297, 107014) };
	return func();
}

void LightVolumes::SetVLQuality(VolumetricLightingDescriptor& descriptor, const uint32_t quality)
{
	using func_t = decltype(&LightVolumes::SetVLQuality);
	static REL::Relocation<func_t> func{ REL::RelocationID(100299, 107016).address() };
	func(descriptor, std::clamp<uint32_t>(quality, 0, 2));
}

RE::BSImagespaceShader* LightVolumes::CreateShader(const std::string_view& name, const std::string_view& fileName, RE::BSComputeShader* computeShader)
{
	auto shader = RE::BSImagespaceShader::Create();
	shader->shaderType = RE::BSShader::Type::ImageSpace;
	shader->fxpFilename = fileName.data();
	shader->name = name.data();
	shader->originalShaderName = fileName.data();
	shader->computeShader = computeShader;
	shader->isComputeShader = true;
	return shader;
}

RE::BSImagespaceShader* LightVolumes::GetOrCreateGenerateCS(RE::BSComputeShader* computeShader)
{
	if (generateCS == nullptr)
		generateCS = CreateShader("BSImagespaceShaderVolumetricLightingGenerateCS", "ISVolumetricLightingGenerateCS", computeShader);
	return generateCS;
}

RE::BSImagespaceShader* LightVolumes::GetOrCreateRaymarchCS(RE::BSComputeShader* computeShader)
{
	if (raymarchCS == nullptr)
		raymarchCS = CreateShader("BSImagespaceShaderVolumetricLightingRaymarchCS", "ISVolumetricLightingRaymarchCS", computeShader);
	return raymarchCS;
}

RE::BSImagespaceShader* LightVolumes::GetOrCreateBlurHCS(RE::BSComputeShader* computeShader)
{
	if (blurHCS == nullptr)
		blurHCS = CreateShader("BSImagespaceShaderVolumetricLightingBlurHCS", "ISVolumetricLightingBlurHCS", computeShader);
	return blurHCS;
}

RE::BSImagespaceShader* LightVolumes::GetOrCreateBlurVCS(RE::BSComputeShader* computeShader)
{
	if (blurVCS == nullptr)
		blurVCS = CreateShader("BSImagespaceShaderVolumetricLightingBlurVCS", "ISVolumetricLightingBlurVCS", computeShader);
	return blurVCS;
}

void LightVolumes::SetDimensionsCB() const
{
	auto cb = vlDataCB->CB();
	globals::d3d::context->CSSetConstantBuffers(1, 1, &cb);
}

void LightVolumes::SetGroupCountsHCS(uint32_t& threadGroupCountX) const
{
	threadGroupCountX = (vlData.screenX + BlurThreadGroupSizeX - BlurWindow * 2u - 1u) / (BlurThreadGroupSizeX - BlurWindow * 2u);
}

void LightVolumes::SetGroupCountsVCS(uint32_t& threadGroupCountY) const
{
	threadGroupCountY = (vlData.screenY + BlurThreadGroupSizeY - BlurWindow * 2u - 1u) / (BlurThreadGroupSizeY - BlurWindow * 2u);
}

#undef I18N_KEY_PREFIX
