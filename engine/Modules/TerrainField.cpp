#include "TerrainField.h"

#include "Globals.h"
#include "ShaderCache.h"
#include "State.h"

namespace
{
	RE::TESLandTexture* GetActiveDefaultLandTexture()
	{
		// This is the same engine-owned default landscape record used by
		// MaterialForge. Skyrim may initialize it after DataLoaded, so callers
		// resolve it lazily from the landscape-material hook.
		static const auto defaultLandTextureAddress = REL::Relocation<RE::TESLandTexture**>(RELOCATION_ID(514783, 400936));
		auto** defaultLandTexture = defaultLandTextureAddress.get();
		return defaultLandTexture ? *defaultLandTexture : nullptr;
	}
}

void TerrainField::DataLoaded()
{
	// Get the default landscape texture set for terrain helper
	auto* defaultLandTextureSet = RE::TESForm::LookupByEditorID<RE::BGSTextureSet>("LandscapeDefault");
	if (defaultLandTextureSet == nullptr) {
		// Texture-set editor IDs are not populated consistently in every runtime
		// environment. The bundled light plugin owns local form 0x800, so use the
		// load-order-aware lookup as the authoritative fallback.
		if (auto* dataHandler = RE::TESDataHandler::GetSingleton()) {
			defaultLandTextureSet = dataHandler->LookupForm<RE::BGSTextureSet>(0x800, "PIXL-TerrainField.esp");
		}
	}
	if (defaultLandTextureSet != nullptr) {
		logger::info("[Terrain Field] LandscapeDefault plugin texture set found");
		defaultLandTexture = defaultLandTextureSet;
		enabled = true;
	} else {
		// The engine-owned default landscape record is initialized later than the
		// DataLoaded message in some setups. Keep the feature active and resolve it
		// on demand once TESObjectLAND material setup begins.
		defaultLandTexture = nullptr;
		enabled = true;
		logger::warn("[Terrain Field] PIXL-TerrainField.esp was not loaded; default-tile textures will use Skyrim's active landscape record when terrain initializes. Extended default-tile maps require the bundled plugin.");
	}
}

bool TerrainField::TESObjectLAND_SetupMaterial(RE::TESObjectLAND* land)
{
	if (!enabled) {
		// terrain helper is not enabled
		return false;
	}

	if (land == nullptr || land->loadedData == nullptr || land->loadedData->mesh[0] == nullptr) {
		// this is not terrain or vanilla material failed
		return false;
	}

	auto* resolvedDefaultLandTexture = defaultLandTexture;
	if (resolvedDefaultLandTexture == nullptr) {
		if (auto* activeDefaultLand = GetActiveDefaultLandTexture()) {
			resolvedDefaultLandTexture = activeDefaultLand->textureSet;
		}
	}

	for (uint32_t quadI = 0; quadI < 4; ++quadI) {
		// Get hash key of vanilla material
		uint32_t hashKey = 0;

		if (land->loadedData->mesh[quadI] == nullptr) {
			// continue if cannot find mesh
			continue;
		}

		const auto& children = land->loadedData->mesh[quadI]->GetChildren();
		auto geometry = children.empty() ? nullptr : static_cast<RE::BSGeometry*>(children[0].get());
		if (geometry != nullptr) {
			const auto shaderProp = static_cast<RE::BSLightingShaderProperty*>(geometry->GetGeometryRuntimeData().shaderProperty.get());
			if (shaderProp != nullptr) {
				if (auto* baseMaterial = shaderProp->GetBaseMaterial())
					hashKey = baseMaterial->hashKey;
			}
		}

		if (hashKey == 0) {
			// continue if cannot find hash key
			continue;
		}

		// Create array of texture sets (6 tiles)
		std::array<RE::BGSTextureSet*, 6> textureSets{};
		auto defTexture = land->loadedData->defQuadTextures[quadI];
		if (defTexture != nullptr && defTexture->formID != 0) {
			textureSets[0] = Util::GetSeasonalSwap(defTexture->textureSet);
		} else {
			// this is a default texture
			textureSets[0] = Util::GetSeasonalSwap(resolvedDefaultLandTexture);
		}
		for (uint32_t textureI = 0; textureI < 5; ++textureI) {
			auto curTexture = land->loadedData->quadTextures[quadI][textureI];
			if (curTexture == nullptr) {
				textureSets[textureI + 1] = nullptr;
				continue;
			}

			if (curTexture->formID == 0) {
				// this is a default texture
				textureSets[textureI + 1] = Util::GetSeasonalSwap(resolvedDefaultLandTexture);
			} else {
				textureSets[textureI + 1] = Util::GetSeasonalSwap(land->loadedData->quadTextures[quadI][textureI]->textureSet);
			}
		}

		// Copy the retained texture references while locked, then perform engine texture-set
		// updates without holding the shared cache lock across engine calls.
		std::array<RE::NiSourceTexturePtr, 6> parallaxTextures{};
		{
			const std::unique_lock lock(extendedSlotsMutex);
			auto& slot = extendedSlots.try_emplace(hashKey).first->second;
			parallaxTextures = slot.parallax;
		}

		// Assign only valid replacement maps. A missing extended map keeps the source
		// texture set's existing parallax slot intact instead of clearing it.
		for (uint32_t textureI = 0; textureI < 6; ++textureI) {
			auto* txSet = textureSets[textureI];
			auto* replacement = parallaxTextures[textureI].get();
			if (txSet == nullptr || replacement == nullptr)
				continue;

			const char* existingPath = txSet->GetTexturePath(static_cast<RE::BSTextureSet::Texture>(3));
			if (existingPath != nullptr && existingPath[0] != '\0') {
				txSet->SetTexture(static_cast<RE::BSTextureSet::Texture>(3), parallaxTextures[textureI]);
			}
		}
	}

	return true;
}

struct THExtendedRendererState
{
	static constexpr uint32_t NumPSTextures = 6;
	static constexpr uint32_t FirstPSTexture = 92;

	uint32_t PSResourceModifiedBits = 0;
	std::array<ID3D11ShaderResourceView*, NumPSTextures> PSTexture;

	void SetPSTexture(size_t textureIndex, RE::BSGraphics::Texture* newTexture)
	{
		if (textureIndex >= NumPSTextures)
			return;
		ID3D11ShaderResourceView* resourceView = newTexture ? REX::W32::AsReal(newTexture->resourceView) : nullptr;
		if (PSTexture[textureIndex] == resourceView)
			return;

		PSTexture[textureIndex] = resourceView;
		PSResourceModifiedBits |= (1 << textureIndex);
	}

	THExtendedRendererState()
	{
		std::fill(PSTexture.begin(), PSTexture.end(), nullptr);
	}
} thExtendedRendererState;

void TerrainField::SetShaderResources(ID3D11DeviceContext* a_context)
{
	if (!a_context)
		return;
	uint32_t mask = thExtendedRendererState.PSResourceModifiedBits;

	if (mask == 0) [[likely]] {
		return;  // Nothing to update
	}

	constexpr uint32_t firstTexture = THExtendedRendererState::FirstPSTexture;
	auto& textures = thExtendedRendererState.PSTexture;

	while (mask) {
		// Find the position of the first set bit
		uint32_t batchStart = std::countr_zero(mask);

		// Count consecutive 1s starting from batchStart
		uint32_t shiftedMask = mask >> batchStart;
		uint32_t batchCount = std::countr_one(shiftedMask);

		a_context->PSSetShaderResources(
			firstTexture + batchStart,
			batchCount,
			&textures[batchStart]);

		// Clear the processed bits
		uint32_t clearMask = ((1u << batchCount) - 1u) << batchStart;
		mask &= ~clearMask;
	}

	thExtendedRendererState.PSResourceModifiedBits = 0;
}

void TerrainField::BSLightingShader_SetupMaterial(RE::BSLightingShaderMaterialBase const* material)
{
	if (!enabled) {
		// terrain helper is not enabled
		return;
	}

	if (material == nullptr) {
		return;
	}

	ExtendedSlots materialBase;
	{
		const std::shared_lock lock(extendedSlotsMutex);

		const auto found = extendedSlots.find(material->hashKey);
		if (found == extendedSlots.end()) {
			// A previous landscape draw may have left extended displacement SRVs
			// active. Clear only Terrain Field's six reserved slots and bits.
			for (uint32_t textureI = 0; textureI < 6; ++textureI)
				thExtendedRendererState.SetPSTexture(textureI, nullptr);
			if (auto* state = globals::state)
				state->permutationData.ExtraFeatureDescriptor &=
					~(0x3Fu | uint(State::ExtraFeatureDescriptors::THLandHasDisplacement));
			return;
		}
		materialBase = found->second;
	}

	const auto state = globals::state;
	if (!state || !globals::game::graphicsState)
		return;
	const auto& stateData = globals::game::graphicsState->GetRuntimeData();

	state->permutationData.ExtraFeatureDescriptor &= ~uint(State::ExtraFeatureDescriptors::THLandHasDisplacement);

	// Populate extended slots
	// Bits 0-5 track individual texture displacement; THLandHasDisplacement (bit 9) tracks if any texture has displacement
	for (uint32_t textureI = 0; textureI < 6; ++textureI) {
		if (materialBase.parallax[textureI] != nullptr && materialBase.parallax[textureI] != stateData.defaultTextureNormalMap) {
			thExtendedRendererState.SetPSTexture(textureI, materialBase.parallax[textureI]->rendererTexture);
			state->permutationData.ExtraFeatureDescriptor |= 1 << textureI;
			state->permutationData.ExtraFeatureDescriptor |= uint(State::ExtraFeatureDescriptors::THLandHasDisplacement);
		} else {
			thExtendedRendererState.SetPSTexture(textureI, nullptr);
			state->permutationData.ExtraFeatureDescriptor &= ~(1 << textureI);
		}
	}
}

void TerrainField::InvalidateSeasonalMaterialCache(std::uint32_t a_generation)
{
	std::size_t removed = 0;
	{
		const std::unique_lock lock(extendedSlotsMutex);
		removed = extendedSlots.size();
		extendedSlots.clear();
	}

	logger::info(
		"[PIXL][TerrainField] Invalidated {} season-dependent material entries (generation {}).",
		removed,
		a_generation);
}

struct TH_TESObjectLAND_SetupMaterial
{
	static bool thunk(RE::TESObjectLAND* land)
	{
		bool result = func(land);

		auto& terrainField = globals::pipeline::terrainField;
		if (result && terrainField.loaded) {
			terrainField.TESObjectLAND_SetupMaterial(land);
		}

		return result;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct TH_BSLightingShader_SetupMaterial
{
	static void thunk(RE::BSLightingShader* shader, RE::BSLightingShaderMaterialBase const* material)
	{
		// A geometry with no resolvable material (e.g. a decal whose textures all
		// failed to load) can reach here with material == nullptr; the original
		// engine function dereferences it unconditionally.
		if (!material)
			return;

		func(shader, material);

		auto& terrainField = globals::pipeline::terrainField;
		if (terrainField.loaded) {
			terrainField.BSLightingShader_SetupMaterial(material);
		}
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

void TerrainField::Load()
{
	// Install TESObjectLAND hook early so TH is inner relative to MaterialForge's PostPostLoad hook.
	// This ensures TH reads the vanilla material hashKey before MaterialForge replaces it with a PBR material.
	// This intentionally matches MaterialForge's REL::RelocationID(18368, 18791).
	logger::info("[Terrain Field] Hooking TESObjectLAND");
	stl::detour_thunk<TH_TESObjectLAND_SetupMaterial>(REL::RelocationID(18368, 18791));
}

void TerrainField::PostPostLoad()
{
	logger::info("[Terrain Field] Hooking BSLightingShader::SetupMaterial");
	stl::write_vfunc<0x4, TH_BSLightingShader_SetupMaterial>(RE::VTABLE_BSLightingShader[0]);
}
