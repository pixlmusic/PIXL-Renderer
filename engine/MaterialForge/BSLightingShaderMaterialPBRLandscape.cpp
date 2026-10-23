#include "BSLightingShaderMaterialPBRLandscape.h"

#include "MaterialForge/PhysicalMaterialRegistry.h"

BSLightingShaderMaterialPBRLandscape::BSLightingShaderMaterialPBRLandscape()
{
	std::fill(isPbr.begin(), isPbr.end(), false);
	std::fill(roughnessScales.begin(), roughnessScales.end(), 1.f);
	std::fill(displacementScales.begin(), displacementScales.end(), 1.f);
	std::fill(specularLevels.begin(), specularLevels.end(), 0.04f);
}

BSLightingShaderMaterialPBRLandscape::~BSLightingShaderMaterialPBRLandscape()
{
	PhysicalMaterial::Registry::GetSingleton().Release(this);
	All.erase(this);
	// PIXL_GROUND_SNOW_METADATA_V2_ERASE
	SnowMetadataByMaterial.erase(this);
}

BSLightingShaderMaterialPBRLandscape* BSLightingShaderMaterialPBRLandscape::Make()
{
	auto* scrapHeap = globals::game::memoryManager->GetThreadScrapHeap();
	auto* material = static_cast<BSLightingShaderMaterialPBRLandscape*>(scrapHeap->Allocate(sizeof(BSLightingShaderMaterialPBRLandscape), 8));
	if (material) {
		std::memset(material, 0, sizeof(BSLightingShaderMaterialPBRLandscape));
		std::construct_at(material);
	}
	return material;
}

RE::BSShaderMaterial* BSLightingShaderMaterialPBRLandscape::Create()
{
	// Must use regular heap (not scrap heap like Make()). BSLightingShaderProperty::LinkObject
	// calls ScrapHeap::Free() after LinkMaterial — if Create() used scrap heap, it would pop
	// the canonical off the stack, causing immediate use-after-free in property->material.
	return new BSLightingShaderMaterialPBRLandscape();
}

void BSLightingShaderMaterialPBRLandscape::CopyMembers(RE::BSShaderMaterial* that)
{
	BSLightingShaderMaterialBase::CopyMembers(that);

	auto* pbrThat = static_cast<BSLightingShaderMaterialPBRLandscape*>(that);

	numLandscapeTextures = pbrThat->numLandscapeTextures;

	for (uint32_t textureIndex = 0; textureIndex < NumTiles; ++textureIndex) {
		landscapeBaseColorTextures[textureIndex] = pbrThat->landscapeBaseColorTextures[textureIndex];
		landscapeNormalTextures[textureIndex] = pbrThat->landscapeNormalTextures[textureIndex];
		landscapeDisplacementTextures[textureIndex] = pbrThat->landscapeDisplacementTextures[textureIndex];
		landscapeRMAOSTextures[textureIndex] = pbrThat->landscapeRMAOSTextures[textureIndex];
	}
	terrainOverlayTexture = pbrThat->terrainOverlayTexture;
	terrainNoiseTexture = pbrThat->terrainNoiseTexture;
	landBlendParams = pbrThat->landBlendParams;
	isPbr = pbrThat->isPbr;
	roughnessScales = pbrThat->roughnessScales;
	displacementScales = pbrThat->displacementScales;
	specularLevels = pbrThat->specularLevels;
	terrainTexOffsetX = pbrThat->terrainTexOffsetX;
	terrainTexOffsetY = pbrThat->terrainTexOffsetY;
	terrainTexFade = pbrThat->terrainTexFade;
	glintParameters = pbrThat->glintParameters;

	// Copy authoring from the source, matching the base material and object path.
	if (const auto extension = All.find(pbrThat); extension != All.end())
		All[this] = extension->second;
	else
		All.erase(this);

	// PIXL_GROUND_SNOW_METADATA_V2_COPY
	// The argument is the source; never overwrite its snow classification.
	if (auto snowIt = SnowMetadataByMaterial.find(pbrThat); snowIt != SnowMetadataByMaterial.end()) {
		SnowMetadataByMaterial[this] = snowIt->second;
	} else {
		SnowMetadataByMaterial.erase(this);
	}
}

RE::BSShaderMaterial::Feature BSLightingShaderMaterialPBRLandscape::GetFeature() const
{
	return RE::BSShaderMaterial::Feature::kMultiTexLandLODBlend;
	//return FEATURE;
}

void BSLightingShaderMaterialPBRLandscape::ClearTextures()
{
	BSLightingShaderMaterialBase::ClearTextures();
	for (auto& texture : landscapeBaseColorTextures) {
		texture.reset();
	}
	for (auto& texture : landscapeNormalTextures) {
		texture.reset();
	}
	for (auto& texture : landscapeDisplacementTextures) {
		texture.reset();
	}
	for (auto& texture : landscapeRMAOSTextures) {
		texture.reset();
	}
	terrainOverlayTexture.reset();
	terrainNoiseTexture.reset();
}

void BSLightingShaderMaterialPBRLandscape::ReceiveValuesFromRootMaterial(bool skinned, bool rimLighting, bool softLighting, bool backLighting, bool MSN)
{
	BSLightingShaderMaterialBase::ReceiveValuesFromRootMaterial(skinned, rimLighting, softLighting, backLighting, MSN);
	const auto& stateData = globals::game::graphicsState->GetRuntimeData();
	if (terrainOverlayTexture == nullptr) {
		terrainOverlayTexture = stateData.defaultTextureNormalMap;
	}
	if (terrainNoiseTexture == nullptr) {
		terrainNoiseTexture = stateData.defaultTextureNormalMap;
	}
	for (uint32_t textureIndex = 0; textureIndex < numLandscapeTextures; ++textureIndex) {
		if (landscapeBaseColorTextures[textureIndex] == nullptr) {
			landscapeBaseColorTextures[textureIndex] = stateData.defaultTextureBlack;
		}
		if (landscapeNormalTextures[textureIndex] == nullptr) {
			landscapeNormalTextures[textureIndex] = stateData.defaultTextureNormalMap;
		}
		if (landscapeDisplacementTextures[textureIndex] == nullptr) {
			landscapeDisplacementTextures[textureIndex] = stateData.defaultTextureBlack;
		}
		if (landscapeRMAOSTextures[textureIndex] == nullptr) {
			landscapeRMAOSTextures[textureIndex] = stateData.defaultTextureWhite;
		}
	}
}

uint32_t BSLightingShaderMaterialPBRLandscape::GetTextures(RE::NiSourceTexture** textures)
{
	uint32_t textureIndex = 0;
	if (rimSoftLightingTexture != nullptr) {
		textures[textureIndex++] = rimSoftLightingTexture.get();
	}
	if (specularBackLightingTexture != nullptr) {
		textures[textureIndex++] = specularBackLightingTexture.get();
	}
	for (uint32_t tileIndex = 0; tileIndex < numLandscapeTextures; ++tileIndex) {
		if (landscapeBaseColorTextures[tileIndex] != nullptr) {
			textures[textureIndex++] = landscapeBaseColorTextures[tileIndex].get();
		}
		if (landscapeNormalTextures[tileIndex] != nullptr) {
			textures[textureIndex++] = landscapeNormalTextures[tileIndex].get();
		}
		if (landscapeDisplacementTextures[tileIndex] != nullptr) {
			textures[textureIndex++] = landscapeDisplacementTextures[tileIndex].get();
		}
		if (landscapeRMAOSTextures[tileIndex] != nullptr) {
			textures[textureIndex++] = landscapeRMAOSTextures[tileIndex].get();
		}
	}
	if (terrainOverlayTexture != nullptr) {
		textures[textureIndex++] = terrainOverlayTexture.get();
	}
	if (terrainNoiseTexture != nullptr) {
		textures[textureIndex++] = terrainNoiseTexture.get();
	}

	return textureIndex;
}

bool BSLightingShaderMaterialPBRLandscape::HasGlint() const
{
	for (uint32_t textureIndex = 0; textureIndex < numLandscapeTextures; ++textureIndex) {
		if (glintParameters[textureIndex].enabled) {
			return true;
		}
	}
	return false;
}

PhysicalMaterial::Descriptor BSLightingShaderMaterialPBRLandscape::GetPhysicalMaterialDescriptor(
	std::uint32_t textureIndex,
	const RE::NiSourceTexture* defaultBlack,
	const RE::NiSourceTexture* defaultWhite) const
{
	PhysicalMaterial::Descriptor descriptor;
	if (textureIndex >= NumTiles || !isPbr[textureIndex]) {
		descriptor.shadingModel = static_cast<std::uint32_t>(PhysicalMaterial::ShadingModel::LegacySpecular);
		return descriptor;
	}

	auto setTexture = [&](PhysicalMaterial::Texture semantic, const RE::NiPointer<RE::NiSourceTexture>& texture, const RE::NiSourceTexture* fallback = nullptr) {
		if (texture != nullptr && texture.get() != fallback) {
			descriptor.textureMask |= static_cast<std::uint32_t>(semantic);
		}
	};
	setTexture(PhysicalMaterial::Texture::BaseColor, landscapeBaseColorTextures[textureIndex]);
	setTexture(PhysicalMaterial::Texture::Normal, landscapeNormalTextures[textureIndex]);
	setTexture(PhysicalMaterial::Texture::Rmaos, landscapeRMAOSTextures[textureIndex], defaultWhite);
	setTexture(PhysicalMaterial::Texture::Displacement, landscapeDisplacementTextures[textureIndex], defaultBlack);
	const auto extension = All.find(const_cast<BSLightingShaderMaterialPBRLandscape*>(this));
	const auto* config = extension != All.end() ? extension->second[textureIndex] : nullptr;
	descriptor.surfaceProfile = MaterialSurface::Pack(config ? config->surface : MaterialSurface::Profile{}, descriptor.HasTexture(PhysicalMaterial::Texture::Rmaos));

	descriptor.roughnessScale = roughnessScales[textureIndex];
	descriptor.displacementScale = displacementScales[textureIndex];
	descriptor.specularLevel = specularLevels[textureIndex];
	descriptor.f0Factor = { descriptor.specularLevel, descriptor.specularLevel, descriptor.specularLevel };
	descriptor.opacity = materialAlpha;

	const auto& sourceGlint = glintParameters[textureIndex];
	if (sourceGlint.enabled) {
		descriptor.featureMask |= static_cast<std::uint32_t>(PhysicalMaterial::MaterialTrait::Glint);
	}
	descriptor.glint.screenSpaceScale = sourceGlint.screenSpaceScale;
	descriptor.glint.logMicrofacetDensity = sourceGlint.logMicrofacetDensity;
	descriptor.glint.microfacetRoughness = sourceGlint.microfacetRoughness;
	descriptor.glint.densityRandomization = sourceGlint.densityRandomization;

	return descriptor;
}
