#include "MaterialForge/PhysicalMaterialRegistry.h"

#include "MaterialForge/BSLightingShaderMaterialPBR.h"
#include "MaterialForge/BSLightingShaderMaterialPBRLandscape.h"
#include "MaterialForge/DX11TextureResolver.h"
#include "MaterialForge/FurEligibility.h"

#include <RE/B/BSLightingShaderMaterialEnvmap.h>
#include <RE/B/BSLightingShaderMaterialGlowmap.h>
#include <RE/B/BSLightingShaderMaterialMultiLayerParallax.h>
#include <RE/B/BSLightingShaderMaterialParallax.h>
#include <RE/B/BSLightingShaderMaterialParallaxOcc.h>
#include <RE/N/NiSourceTexture.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <mutex>

namespace
{
	std::string NormalizeResourceName(const RE::NiSourceTexture& texture)
	{
		const char* source = texture.name.c_str();
		if (source == nullptr || *source == '\0') {
			return {};
		}

		std::string normalized(source);
		std::ranges::transform(normalized, normalized.begin(), [](unsigned char character) {
			if (character == '/') {
				return '\\';
			}
			return static_cast<char>(std::tolower(character));
		});
		return normalized;
	}

	std::string NormalizeResourceName(std::string_view source)
	{
		std::string normalized(source);
		std::ranges::transform(normalized, normalized.begin(), [](unsigned char character) {
			if (character == '/')
				return '\\';
			return static_cast<char>(std::tolower(character));
		});
		return normalized;
	}

	bool HasFurToken(std::string_view path, std::string_view token)
	{
		auto isBoundary = [](char character) {
			return character == '\\' || character == '/' || character == '_' ||
				character == '-' || character == '.' || character == ' ' ||
				(character >= '0' && character <= '9');
		};

		std::size_t cursor = 0;
		while ((cursor = path.find(token, cursor)) != std::string_view::npos) {
			const std::size_t end = cursor + token.size();
			const bool left = cursor == 0 || isBoundary(path[cursor - 1]);
			const bool right = end == path.size() || isBoundary(path[end]);
			if (left && right)
				return true;
			++cursor;
		}
		return false;
	}

	std::string_view ResourceLeaf(std::string_view path)
	{
		const auto separator = path.find_last_of("\\/");
		return separator == std::string_view::npos ? path : path.substr(separator + 1u);
	}

	template <class Range>
	bool ContainsAny(std::string_view text, const Range& tokens)
	{
		return std::ranges::any_of(tokens, [text](std::string_view token) {
			return text.find(token) != std::string_view::npos;
		});
	}

	bool ContainsAny(std::string_view text, std::initializer_list<std::string_view> tokens)
	{
		return std::ranges::any_of(tokens, [text](std::string_view token) {
			return text.find(token) != std::string_view::npos;
		});
	}

	float ClassifyFurEvidence(std::string_view texturePath, std::string_view meshPath = {})
	{
		const std::string texture = NormalizeResourceName(texturePath);
		const std::string mesh = NormalizeResourceName(meshPath);
		const std::string_view textureLeaf = ResourceLeaf(texture);
		// MaterialForge builds this hierarchy from the draw geometry upward. Only
		// the draw's own name is reliable for classifying a submesh; parent actor or
		// scene names commonly describe unrelated equipment in the same hierarchy.
		const auto parentSeparator = mesh.find('\\');
		const std::string_view meshLeaf = parentSeparator == std::string::npos
			? std::string_view(mesh)
			: std::string_view(mesh).substr(0u, parentSeparator);
		if (texture.empty() && mesh.empty())
			return 0.0f;

		// Hard anatomical and manufactured surfaces always win, even when a mod
		// stores their textures beneath a creature/fur folder.
		constexpr std::string_view hardSurfaceTokens[]{
			"eye", "teeth", "tooth", "claw", "talon", "horn", "antler", "tusk",
			"skull", "bone", "weapon", "hoof", "beak" };
		if (ContainsAny(textureLeaf, hardSurfaceTokens) || ContainsAny(meshLeaf, hardSurfaceTokens))
			return 0.0f;

		// Generic equipment names are excluded before species fallback. This keeps
		constexpr std::string_view apparelTokens[]{
			"armor", "armour", "helmet", "hood", "robe", "cloth", "shirt", "dress",
			"glove", "boot", "shoe", "cloak", "cape", "tunic", "sleeve", "skirt",
			"pants", "trousers", "belt", "bracer", "cuirass", "outfit", "clothing",
			"saddle", "quiver", "backpack" };
		if (ContainsAny(texture, apparelTokens) || ContainsAny(meshLeaf, apparelTokens))
			return 0.0f;

		// Explicitly named fur sub-materials opt in only when their own material or
		// draw name is fur-specific and not a whole garment. Fur cuffs/trim and pelt
		// submeshes can therefore be selected without inheriting parent directories.
		for (const auto token : { "fur", "furry", "pelt", "fleece", "sheepskin" }) {
			if (HasFurToken(textureLeaf, token) || HasFurToken(meshLeaf, token))
				return 1.0f;
		}
		// Bethesda and common replacers use concatenated names such as
		// sabrecatpelt01; recognize pelt compounds and dedicated pelt folders.
		if (texture.find("pelt") != std::string::npos ||
			textureLeaf.find("pelt") != std::string_view::npos ||
			meshLeaf.find("pelt") != std::string_view::npos)
			return 1.0f;

		// Use only the local material and draw names for automatic creature fallback.
		// Do not inherit species or body cues from the rest of an actor hierarchy.
		const std::string localNames = std::string(textureLeaf) + "\\" + std::string(meshLeaf);
		// Automatic classification is deliberately limited to familiar fur-bearing
		// animals. Do not infer fur from humanoid races (including Khajiit), generic
		// mammals, or broad actor names: those cues can include clothing and hard
		// anatomy in the same draw hierarchy. Other content can opt in with an
		// explicitly named fur/pelt material above.
		const bool creature = ContainsAny(localNames, {
			"wolf", "fox", "bear", "dog", "rabbit", "hare" });
		const bool coat = ContainsAny(localNames, { "body", "skin", "coat", "torso", "hide", "tail" });
		return creature ? (coat ? 0.90f : 0.82f) : 0.0f;
	}

	float ClassifyHairEvidence(std::string_view texturePath, std::string_view meshPath = {})
	{
		const std::string texture = NormalizeResourceName(texturePath);
		const std::string mesh = NormalizeResourceName(meshPath);
		const std::string evidence = texture + "\\" + mesh;
		if (evidence.empty())
			return 0.0f;

		// Strong author vocabulary.  "hair" is intentionally a substring here to
		// support common concatenated assets (hairfemale, hairline, facialhair), but
		// known furniture false positives are rejected below and the renderer still
		// requires skinned alpha geometry before accepting the score.
		float confidence = 0.0f;
		for (const auto token : { "hair", "hairstyle", "hairline", "beard", "moustache", "mustache", "sideburn", "ponytail", "braid" }) {
			if (evidence.find(token) != std::string::npos)
				confidence = std::max(confidence, 0.96f);
		}
		for (const auto token : { "brow", "eyebrow", "lash", "whisker" }) {
			if (evidence.find(token) != std::string::npos)
				confidence = std::max(confidence, 0.74f);
		}

		// These materials commonly sit on the same actor/head hierarchy but must not
		// inherit fibre motion or the hair BRDF merely because a parent mesh says head.
		for (const auto token : {
				 "helmet", "hood", "circlet", "crown", "armor", "armour", "cloth", "robe",
				 "skin", "face", "headhuman", "eye", "teeth", "mouth", "tongue", "ear" }) {
			if (texture.find(token) != std::string::npos)
				return 0.0f;
		}

		// "chair" contains the literal substring "hair". Check these lexical false
		// positives against the full hierarchy, unlike actor-head exclusions above,
		// so an alpha-tested animated furnishing can never become a hair candidate.
		for (const auto token : { "chair", "furniture", "furnish" }) {
			if (evidence.find(token) != std::string::npos)
				return 0.0f;
		}

		return confidence;
	}

	void ApplyFurSemanticsImpl(PhysicalMaterial::Descriptor& descriptor, float confidence)
	{
		if (confidence < 0.80f)
			return;
		descriptor.featureMask |=
			static_cast<std::uint32_t>(PhysicalMaterial::MaterialTrait::FurShell) |
			static_cast<std::uint32_t>(PhysicalMaterial::MaterialTrait::Fuzz);
		descriptor.furConfidence = std::clamp(confidence, 0.0f, 1.0f);
		descriptor.furShellLength = 1.0f;
		descriptor.furDensity = std::lerp(0.72f, 1.0f, descriptor.furConfidence);
		descriptor.furSoftness = std::lerp(0.68f, 0.92f, descriptor.furConfidence);
		descriptor.fuzzWeight = std::max(descriptor.fuzzWeight, 0.42f * descriptor.furConfidence);
		if (descriptor.fuzzColor[0] + descriptor.fuzzColor[1] + descriptor.fuzzColor[2] <= 1.0e-4f)
			descriptor.fuzzColor = { 1.0f, 1.0f, 1.0f };
	}

	bool SamePayload(const PhysicalMaterial::TableEntry& left, const PhysicalMaterial::TableEntry& right)
	{
		if (left.bindingCount != right.bindingCount ||
			std::memcmp(&left.descriptor, &right.descriptor, sizeof(left.descriptor)) != 0) {
			return false;
		}

		return std::equal(
			left.bindings.begin(),
			left.bindings.begin() + left.bindingCount,
			right.bindings.begin(),
			[](const auto& a, const auto& b) {
				return a.textureID == b.textureID && a.semantic == b.semantic && a.colorSpace == b.colorSpace;
			});
	}
}


namespace PhysicalMaterial
{
	bool IsFurExcluded(const RE::BSLightingShaderMaterialBase& material, std::string_view geometryName)
	{
		if (IsNonFurAnatomy(geometryName)) return true;
		const char* textureName = material.diffuseTexture ? material.diffuseTexture->name.c_str() : nullptr;
		return textureName && IsNonFurAnatomy(ResourceLeaf(textureName));
	}
	float ClassifyAutomaticFur(
		const RE::BSLightingShaderMaterialBase& material,
		std::string_view meshPath)
	{
		const std::string diffusePath = material.diffuseTexture
			? NormalizeResourceName(*material.diffuseTexture)
			: std::string{};
		const auto separator = meshPath.find_first_of("\\/");
		if (IsNonFurAnatomy(ResourceLeaf(diffusePath)) || IsNonFurAnatomy(meshPath.substr(0, separator)))
			return 0.0f;
		return ClassifyFurEvidence(diffusePath, meshPath);
	}

	float ClassifyAutomaticHair(
		const RE::BSLightingShaderMaterialBase& material,
		std::string_view meshPath)
	{
		const std::string diffusePath = material.diffuseTexture
			? NormalizeResourceName(*material.diffuseTexture)
			: std::string{};
		return ClassifyHairEvidence(diffusePath, meshPath);
	}

	void ApplyAutomaticFurSemantics(Descriptor& descriptor, float confidence)
	{
		ApplyFurSemanticsImpl(descriptor, confidence);
	}

	Registry& Registry::GetSingleton()
	{
		static Registry singleton;
		return singleton;
	}

	std::size_t Registry::OwnerKeyHash::operator()(const OwnerKey& key) const noexcept
	{
		const auto pointerHash = std::hash<const void*>{}(key.owner);
		return pointerHash ^ (static_cast<std::size_t>(key.layer) + 0x9E3779B9u + (pointerHash << 6) + (pointerHash >> 2));
	}

	MaterialID Registry::ObserveLegacy(const RE::BSLightingShaderMaterialBase& material)
	{
		Descriptor descriptor;
		descriptor.shadingModel = static_cast<std::uint32_t>(ShadingModel::LegacySpecular);
		descriptor.opacity = std::clamp(material.materialAlpha, 0.f, 1.f);

		const float shininess = std::max(material.specularPower, 0.f);
		descriptor.roughnessScale = std::clamp(std::pow(2.f / (shininess + 2.f), 0.25f), 0.04f, 1.f);
		descriptor.f0Factor = {
			std::clamp(0.04f * std::max(material.specularColor.red, 0.f) * std::max(material.specularColorScale, 0.f), 0.f, 1.f),
			std::clamp(0.04f * std::max(material.specularColor.green, 0.f) * std::max(material.specularColorScale, 0.f), 0.f, 1.f),
			std::clamp(0.04f * std::max(material.specularColor.blue, 0.f) * std::max(material.specularColorScale, 0.f), 0.f, 1.f)
		};
		descriptor.specularLevel =
			0.2126f * descriptor.f0Factor[0] + 0.7152f * descriptor.f0Factor[1] + 0.0722f * descriptor.f0Factor[2];

		std::array<SourceBinding, MaxTextureBindings> bindings{};
		std::size_t bindingCount = 0;
		auto addBinding = [&](Texture semantic, ColorSpace colorSpace, const RE::NiPointer<RE::NiSourceTexture>& texture) {
			if (texture != nullptr && bindingCount < bindings.size()) {
				bindings[bindingCount++] = { semantic, colorSpace, texture.get() };
				descriptor.textureMask |= static_cast<std::uint32_t>(semantic);
			}
		};

		addBinding(Texture::BaseColor, ColorSpace::SRGB, material.diffuseTexture);
		addBinding(Texture::Normal, ColorSpace::Linear, material.normalTexture);
		ApplyAutomaticFurSemantics(descriptor, ClassifyAutomaticFur(material));

		using enum RE::BSShaderMaterial::Feature;
		switch (material.GetFeature()) {
		case kGlowMap:
			addBinding(Texture::Emissive, ColorSpace::SRGB,
				static_cast<const RE::BSLightingShaderMaterialGlowmap&>(material).glowTexture);
			break;
		case kEnvironmentMap:
			{
				const auto& environment = static_cast<const RE::BSLightingShaderMaterialEnvmap&>(material);
				addBinding(Texture::Environment, ColorSpace::SRGB, environment.envTexture);
				addBinding(Texture::EnvironmentMask, ColorSpace::Linear, environment.envMaskTexture);
				break;
			}
		case kParallax:
			addBinding(Texture::Displacement, ColorSpace::Linear,
				static_cast<const RE::BSLightingShaderMaterialParallax&>(material).heightTexture);
			break;
		case kParallaxOcc:
			addBinding(Texture::Displacement, ColorSpace::Linear,
				static_cast<const RE::BSLightingShaderMaterialParallaxOcc&>(material).heightTexture);
			break;
		case kMultilayerParallax:
			{
				const auto& multilayer = static_cast<const RE::BSLightingShaderMaterialMultiLayerParallax&>(material);
				addBinding(Texture::Features0, ColorSpace::SRGB, multilayer.layerTexture);
				addBinding(Texture::Environment, ColorSpace::SRGB, multilayer.envTexture);
				addBinding(Texture::EnvironmentMask, ColorSpace::Linear, multilayer.envMaskTexture);
				break;
			}
		default:
			break;
		}

		return Observe({ &material, std::numeric_limits<std::uint32_t>::max() }, descriptor, std::span(bindings.data(), bindingCount));
	}

	MaterialID Registry::ObservePBR(const BSLightingShaderMaterialPBR& material, const Descriptor& descriptor)
	{
		Descriptor resolvedDescriptor = descriptor;
		const std::string diffusePath = material.diffuseTexture
			? NormalizeResourceName(*material.diffuseTexture)
			: std::string{};
		ApplyAutomaticFurSemantics(
			resolvedDescriptor,
			ClassifyFurEvidence(diffusePath, material.inputFilePath));
		const std::array bindings{
			SourceBinding{ Texture::BaseColor, ColorSpace::SRGB, material.diffuseTexture.get() },
			SourceBinding{ Texture::Normal, ColorSpace::Linear, material.normalTexture.get() },
			SourceBinding{ Texture::Rmaos, ColorSpace::Linear, material.rmaosTexture.get() },
			SourceBinding{ Texture::Emissive, ColorSpace::SRGB, material.emissiveTexture.get() },
			SourceBinding{ Texture::Displacement, ColorSpace::Linear, material.displacementTexture.get() },
			SourceBinding{ Texture::Features0, ColorSpace::SRGB, material.featuresTexture0.get() },
			SourceBinding{ Texture::Features1, ColorSpace::Linear, material.featuresTexture1.get() }
		};
		return Observe({ &material, 0 }, resolvedDescriptor, bindings);
	}

	MaterialID Registry::ObservePBRLandscape(
		const BSLightingShaderMaterialPBRLandscape& material,
		std::uint32_t layer,
		const Descriptor& descriptor)
	{
		if (layer >= BSLightingShaderMaterialPBRLandscape::NumTiles) {
			return 0;
		}

		const std::array bindings{
			SourceBinding{ Texture::BaseColor, ColorSpace::SRGB, material.landscapeBaseColorTextures[layer].get() },
			SourceBinding{ Texture::Normal, ColorSpace::Linear, material.landscapeNormalTextures[layer].get() },
			SourceBinding{ Texture::Rmaos, ColorSpace::Linear, material.landscapeRMAOSTextures[layer].get() },
			SourceBinding{ Texture::Displacement, ColorSpace::Linear, material.landscapeDisplacementTextures[layer].get() }
		};
		return Observe({ &material, layer }, descriptor, bindings);
	}

	MaterialID Registry::FindLegacy(const RE::BSLightingShaderMaterialBase& material) const
	{
		return Find({ &material, std::numeric_limits<std::uint32_t>::max() });
	}

	MaterialID Registry::FindPBR(const BSLightingShaderMaterialPBR& material) const
	{
		return Find({ &material, 0 });
	}

	MaterialID Registry::FindPBRLandscape(
		const BSLightingShaderMaterialPBRLandscape& material,
		std::uint32_t layer) const
	{
		if (layer >= BSLightingShaderMaterialPBRLandscape::NumTiles) {
			return 0;
		}
		return Find({ &material, layer });
	}

	MaterialID Registry::Find(OwnerKey owner) const
	{
		std::shared_lock lock(mutex_);
		if (const auto found = owners_.find(owner); found != owners_.end()) {
			return found->second;
		}
		return 0;
	}

	MaterialID Registry::Observe(
		OwnerKey owner,
		Descriptor descriptor,
		std::span<const SourceBinding> sourceBindings)
	{
		std::unique_lock lock(mutex_);

		TableEntry candidate;
		candidate.descriptor = descriptor;
		for (const auto& source : sourceBindings) {
			if (source.texture == nullptr || !descriptor.HasTexture(source.semantic) ||
				candidate.bindingCount >= candidate.bindings.size()) {
				continue;
			}
			const auto textureID = ResolveTexture(*source.texture);
			candidate.bindings[candidate.bindingCount++] = {
				textureID,
				static_cast<std::uint32_t>(source.semantic),
				static_cast<std::uint32_t>(source.colorSpace)
			};
			if (source.semantic == Texture::BaseColor || source.semantic == Texture::Normal ||
				source.semantic == Texture::Rmaos || source.semantic == Texture::Emissive) {
				DX11::TextureResolver::GetSingleton().Observe(textureID, *source.texture);
			}
		}

		if (const auto ownerIt = owners_.find(owner); ownerIt != owners_.end()) {
			auto& current = materials_.at(ownerIt->second);
			candidate.materialID = current.materialID;
			candidate.revision = current.revision;
			if (!SamePayload(current, candidate)) {
				candidate.revision++;
				current = candidate;
				generation_++;
			}
			return current.materialID;
		}

		candidate.materialID = nextMaterialID_++;
		candidate.revision = 1;
		owners_.emplace(owner, candidate.materialID);
		materials_.emplace(candidate.materialID, candidate);
		generation_++;
		return candidate.materialID;
	}

	TextureID Registry::ResolveTexture(const RE::NiSourceTexture& texture)
	{
		const auto resourceName = NormalizeResourceName(texture);
		if (!resourceName.empty()) {
			if (const auto found = namedTextures_.find(resourceName); found != namedTextures_.end()) {
				return found->second;
			}

			const TextureID id = nextTextureID_++;
			namedTextures_.emplace(resourceName, id);
			textures_.emplace(id, TextureSource{ id, 1, true, resourceName });
			generation_++;
			return id;
		}

		if (const auto found = runtimeTextures_.find(&texture); found != runtimeTextures_.end()) {
			return found->second;
		}

		const TextureID id = nextTextureID_++;
		runtimeTextures_.emplace(&texture, id);
		textures_.emplace(id, TextureSource{ id, 1, false, {} });
		generation_++;
		return id;
	}

	void Registry::Release(const void* owner)
	{
		std::unique_lock lock(mutex_);
		bool released = false;
		for (auto iterator = owners_.begin(); iterator != owners_.end();) {
			if (iterator->first.owner == owner) {
				materials_.erase(iterator->second);
				iterator = owners_.erase(iterator);
				released = true;
			} else {
				++iterator;
			}
		}
		if (released) {
			generation_++;
		}
	}

	TableSnapshot Registry::Snapshot() const
	{
		std::shared_lock lock(mutex_);
		TableSnapshot snapshot;
		snapshot.generation = generation_;
		snapshot.materials.reserve(materials_.size());
		for (const auto& [id, material] : materials_) {
			snapshot.materials.push_back(material);
		}
		snapshot.textures.reserve(textures_.size());
		for (const auto& [id, texture] : textures_) {
			snapshot.textures.push_back(texture);
		}
		std::ranges::sort(snapshot.materials, {}, &TableEntry::materialID);
		std::ranges::sort(snapshot.textures, {}, &TextureSource::textureID);
		return snapshot;
	}

	std::uint64_t Registry::GetGeneration() const
	{
		std::shared_lock lock(mutex_);
		return generation_;
	}

	RegistryDiagnostics Registry::GetDiagnostics() const
	{
		std::shared_lock lock(mutex_);

		RegistryDiagnostics diagnostics;
		diagnostics.generation = generation_;
		diagnostics.materialCount = materials_.size();
		diagnostics.textureCount = textures_.size();

		auto finite3 = [](const std::array<float, 3>& value) {
			return std::ranges::all_of(value, [](float component) { return std::isfinite(component); });
		};
		auto finiteGlint = [](const Glint& value) {
			return std::isfinite(value.screenSpaceScale) &&
				std::isfinite(value.logMicrofacetDensity) &&
				std::isfinite(value.microfacetRoughness) &&
				std::isfinite(value.densityRandomization);
		};

		for (const auto& [id, entry] : materials_) {
			(void)id;
			const auto& descriptor = entry.descriptor;
			if (descriptor.shadingModel == static_cast<std::uint32_t>(ShadingModel::LegacySpecular))
				diagnostics.legacyMaterialCount++;
			else if (descriptor.shadingModel == static_cast<std::uint32_t>(ShadingModel::MetallicRoughness))
				diagnostics.metallicRoughnessMaterialCount++;

			if (descriptor.HasTrait(MaterialTrait::FurShell))
				diagnostics.furMaterialCount++;

			const bool finitePayload =
				finite3(descriptor.baseColorFactor) && std::isfinite(descriptor.opacity) &&
				finite3(descriptor.emissiveFactor) && std::isfinite(descriptor.emissiveStrength) &&
				finite3(descriptor.f0Factor) && std::isfinite(descriptor.furShellLength) &&
				std::isfinite(descriptor.roughnessScale) && std::isfinite(descriptor.metallicScale) &&
				std::isfinite(descriptor.ambientOcclusionStrength) && std::isfinite(descriptor.specularLevel) &&
				std::isfinite(descriptor.normalScale) && std::isfinite(descriptor.displacementScale) &&
				std::isfinite(descriptor.subsurfaceOpacity) && std::isfinite(descriptor.coatStrength) &&
				finite3(descriptor.subsurfaceColor) && std::isfinite(descriptor.coatRoughness) &&
				finite3(descriptor.coatColor) && std::isfinite(descriptor.coatSpecularLevel) &&
				finite3(descriptor.fuzzColor) && std::isfinite(descriptor.fuzzWeight) &&
				finite3(descriptor.projectedBaseColorScale) && std::isfinite(descriptor.projectedRoughness) &&
				std::isfinite(descriptor.projectedSpecularLevel) && std::isfinite(descriptor.furConfidence) &&
				std::isfinite(descriptor.furDensity) && std::isfinite(descriptor.furSoftness) &&
				finiteGlint(descriptor.glint) && finiteGlint(descriptor.projectedGlint);

			if (descriptor.schemaVersion != SchemaVersion ||
				descriptor.shadingModel > static_cast<std::uint32_t>(ShadingModel::MetallicRoughness) ||
				entry.bindingCount > entry.bindings.size() || !finitePayload) {
				diagnostics.invalidDescriptorCount++;
			}
		}

		for (const auto& [id, texture] : textures_) {
			(void)id;
			if (texture.fileBacked)
				diagnostics.fileBackedTextureCount++;
			else
				diagnostics.runtimeTextureCount++;
		}

		return diagnostics;
	}
}
