#include "InteriorDaylight.h"
#include "I18n/I18n.h"
#include "State.h"

#define I18N_KEY_PREFIX "feature.interior_daylight."

#include <numbers>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	InteriorDaylight::Settings,
	AutoEnableCompatibleSkyInteriors,
	ForceDoubleSidedRendering,
	InteriorShadowDistance)

void InteriorDaylight::DrawSettings()
{
	ImGui::Checkbox("Automatic Sky-Interior Daylight", &settings.AutoEnableCompatibleSkyInteriors);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextWrapped(
			"Lets PIXL use Skyrim's exterior sun, moon and weather lighting in interior cells that explicitly expose the sky and use sky lighting, even when an older cell omitted the sunlight-shadow extension flag. Fully enclosed interiors remain untouched.");
	}
	ImGui::Checkbox(T(TKEY("force_double_sided"), "Force Double-Sided Rendering"), &settings.ForceDoubleSidedRendering);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("force_double_sided_tooltip"),
							  "Disables backface culling during sun shadowmap rendering in interiors. "
							  "Will prevent most light leaking through unmasked/unprepared interiors at a small performance cost. "));
	}
	if (ImGui::SliderFloat(T(TKEY("interior_shadow_distance"), "Interior Shadow Distance"), &settings.InteriorShadowDistance, 1000.0f, 8000.0f, "%.0f units", ImGuiSliderFlags_AlwaysClamp)) {
		*gInteriorShadowDistance = settings.InteriorShadowDistance;
		auto tes = RE::TES::GetSingleton();
		SetShadowDistance(tes && tes->interiorCell);
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("interior_shadow_distance_tooltip"),
							  "Sets the distance shadows are rendered at in interiors. "
							  "Lower values provide higher quality shadows and improved performance but may cause distant interior spaces to light up incorrectly. "));
	}
}

void InteriorDaylight::LoadSettings(json& o_json)
{
	settings = o_json;
}

void InteriorDaylight::SaveSettings(json& o_json)
{
	o_json = settings;
}

void InteriorDaylight::RestoreDefaultSettings()
{
	settings = {};
}

void InteriorDaylight::PostPostLoad()
{
	stl::write_thunk_call<BSBatchRenderer_RenderPassImmediately>(REL::RelocationID(100852, 107642).address() + REL::Relocate(0x29E, 0x28F));

	// Hooks and patch to enable directional lighting for interiors
	stl::write_thunk_call<GetWorldSpace>(REL::RelocationID(35562, 36561).address() + REL::Relocate(0x399, 0x37D));
	stl::write_thunk_call<GetWorldSpace>(REL::RelocationID(35562, 36561).address() + REL::Relocate(0x3AE, 0x392));
	REL::safe_fill(REL::RelocationID(35562, 36561).address() + REL::Relocate(0x397, 0x37B), REL::NOP, 2);

	// Hook for overriding the rooms and portals passed to the directional light culling step to fix light leaking through unrendered geometry
	stl::detour_thunk<DirShadowLightCulling>(REL::RelocationID(101498, 108492));

	// Hooks and patches in AIProcess::CalculateLightValue to force interior cells with directional lights to perform raycast checks
	REL::safe_fill(REL::RelocationID(38900, 39946).address() + REL::Relocate(0x1E7, 0x1F1), REL::NOP, REL::Module::IsAE() ? 2 : 6);
	stl::write_thunk_call<GetWorldSpace>(REL::RelocationID(38900, 39946).address() + REL::Relocate(0x1ED, 0x1F3));
	REL::safe_fill(REL::RelocationID(38900, 39946).address() + REL::Relocate(0x2CA, 0x22B), REL::NOP, REL::Module::IsAE() ? 6 : 2);

	gShadowDistance = reinterpret_cast<float*>(REL::RelocationID(528314, 415263).address());
	gInteriorShadowDistance = reinterpret_cast<float*>(REL::RelocationID(513755, 391724).address());

	// Patches BSShadowDirectionalLight::SetFrameCamera to read the correct shadow distance value in interior cells
	const std::uintptr_t address = REL::RelocationID(101499, 108496).address() + REL::Relocate(0xD62, 0xE6C);
	const std::int32_t displacement = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(gShadowDistance) - (address + 8));
	REL::safe_write(address + 4, &displacement, sizeof(displacement));

	rasterStateCullMode = &globals::game::shadowState->GetRuntimeData().rasterStateCullMode;

	logger::info("[Interior Daylight] Installed hooks");
}

void InteriorDaylight::EarlyPrepass()
{
	isInteriorWithSun = IsInteriorWithSun(RE::TES::GetSingleton()->interiorCell);
}

inline bool InteriorDaylight::IsInteriorWithSun(const RE::TESObjectCELL* cell)
{
	if (!cell || !cell->cellFlags.all(
			RE::TESObjectCELL::Flag::kIsInteriorCell,
			RE::TESObjectCELL::Flag::kShowSky,
			RE::TESObjectCELL::Flag::kUseSkyLighting)) {
		return false;
	}

	const bool authoredSunShadows = cell->cellFlags.any(
		static_cast<RE::TESObjectCELL::Flag>(CellFlagExt::kSunlightShadows));
	return authoredSunShadows ||
		globals::pipeline::interiorDaylight.settings.AutoEnableCompatibleSkyInteriors;
}

RE::TESWorldSpace* InteriorDaylight::GetWorldSpace::thunk(RE::TES* tes)
{
	if (const auto cell = tes->interiorCell)
		return IsInteriorWithSun(cell) ? enableInteriorDaylight : disableInteriorDaylight;
	return func(tes);
}

RE::TESWorldSpace* InteriorDaylight::enableInteriorDaylight = [] {
	alignas(RE::TESWorldSpace) static char buffer[sizeof(RE::TESWorldSpace)]{};
	return reinterpret_cast<RE::TESWorldSpace*>(buffer);
}();

RE::TESWorldSpace* InteriorDaylight::disableInteriorDaylight = [] {
	alignas(RE::TESWorldSpace) static char buffer[sizeof(RE::TESWorldSpace)] = {};
	const auto noShadows = reinterpret_cast<RE::TESWorldSpace*>(buffer);
	noShadows->flags.set(RE::TESWorldSpace::Flag::kNoSky, RE::TESWorldSpace::Flag::kFixedDimensions);
	return noShadows;
}();

void InteriorDaylight::DirShadowLightCulling::thunk(RE::BSShadowDirectionalLight* dirLight, RE::BSTArray<RE::BSTArray<RE::NiPointer<RE::NiAVObject>>>& jobArrays, RE::BSTArray<RE::NiPointer<RE::NiAVObject>>& nodes)
{
	auto& singleton = globals::pipeline::interiorDaylight;
	const auto cell = RE::TES::GetSingleton()->interiorCell;
	auto* passedJobArrays = &jobArrays;

	if (cell && singleton.isInteriorWithSun) {
		const auto* loadedData = cell->GetRuntimeData().loadedData;
		const auto portalGraph = loadedData ? loadedData->portalGraph : nullptr;
		if (portalGraph) {
			singleton.PopulateReplacementJobArrays(cell, portalGraph, dirLight, jobArrays);
			passedJobArrays = &singleton.replacementJobArrays;
		} else
			singleton.currentCell = nullptr;
	} else {
		if (!singleton.arraysCleared)
			singleton.ClearArrays();
		singleton.currentCell = nullptr;
	}

	func(dirLight, *passedJobArrays, nodes);
}

void InteriorDaylight::BSBatchRenderer_RenderPassImmediately::thunk(RE::BSRenderPass* a_pass, uint32_t a_technique, bool a_alphaTest, uint32_t a_renderFlags)
{
	globals::pipeline::interiorDaylight.UpdateRasterStateCullMode(a_pass, a_technique);
	func(a_pass, a_technique, a_alphaTest, a_renderFlags);
}

RE::BSEventNotifyControl InteriorDaylight::MenuOpenCloseEventHandler::ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
	if (a_event->menuName == RE::MainMenu::MENU_NAME) {
		if (a_event->opening)
			globals::pipeline::interiorDaylight.isInteriorWithSun = false;
	}

	return RE::BSEventNotifyControl::kContinue;
}

void InteriorDaylight::ClearArrays()
{
	currentCellRoomsAndPortals.clear();

	for (auto& jobArray : replacementJobArrays)
		jobArray.clear();

	arraysCleared = true;
}

void InteriorDaylight::PopulateReplacementJobArrays(RE::TESObjectCELL* cell, const RE::NiPointer<RE::BSPortalGraph>& portalGraph, const RE::BSShadowDirectionalLight* dirLight, RE::BSTArray<RE::BSTArray<RE::NiPointer<RE::NiAVObject>>>& jobArrays)
{
	if (cell != currentCell) {
		InitialiseOnNewCell(portalGraph);
		currentCell = cell;
	}

	const auto jobArraySize = jobArrays.size();

	if (replacementJobArrays.size() != jobArraySize)
		replacementJobArrays.resize(jobArraySize);

	for (auto& jobArray : replacementJobArrays)
		jobArray.clear();

	addedSet.clear();

	// Copy the original job arrays contents into the replacement job arrays
	uint32_t count = 0;
	for (uint32_t i = 0; i < jobArraySize; ++i) {
		for (const auto& object : jobArrays[i]) {
			replacementJobArrays[i].push_back(object);
			addedSet.insert(object.get());
			count++;
		}
	}

	const auto playerPos = RE::PlayerCharacter::GetSingleton()->GetPosition();
	auto lightDir = -dirLight->GetShadowDirectionalLightRuntimeData().sunVector;
	lightDir.Unitize();

	// Add extra rooms and portals that are in the direction of the sun
	for (const auto& object : currentCellRoomsAndPortals) {
		if (addedSet.find(object.get()) != addedSet.end() || !IsInSunDirectionAndWithinShadowDistance(object, lightDir, playerPos))
			continue;

		addedSet.insert(object.get());
		replacementJobArrays[count++ % jobArraySize].push_back(object);
	}

	arraysCleared = false;
}

void InteriorDaylight::InitialiseOnNewCell(const RE::NiPointer<RE::BSPortalGraph>& portalGraph)
{
	currentCellRoomsAndPortals.clear();

	if (const auto portalSharedNode = portalGraph->portalSharedNode) {
		for (const auto room : portalGraph->rooms)
			currentCellRoomsAndPortals.push_back(room);

		for (auto child : portalGraph->portalSharedNode->GetChildren())
			currentCellRoomsAndPortals.push_back(child);
	}
}

bool InteriorDaylight::IsInSunDirectionAndWithinShadowDistance(const RE::NiPointer<RE::NiAVObject>& object, const RE::NiPoint3& lightDir, const RE::NiPoint3& playerPos) const
{
	const float radius = object->worldBound.radius;
	const auto diff = object->worldBound.center - playerPos;
	const float distance = diff.Length();
	const float projection = lightDir.Dot(diff);
	return projection >= -radius && (distance - radius) <= *gShadowDistance;
}

void InteriorDaylight::SetShadowDistance(bool inInterior)
{
	using func_t = decltype(SetShadowDistance);
	static REL::Relocation<func_t> func{ REL::RelocationID(98978, 105631).address() };
	func(inInterior);
}
#undef I18N_KEY_PREFIX
