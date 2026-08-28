#include "MaterialForge/PhysicalMaterialRegistry.h"

#include "MaterialForge/BSLightingShaderMaterialPBR.h"
#include "MaterialForge/BSLightingShaderMaterialPBRLandscape.h"
#include "MaterialForge/DX11TextureResolver.h"

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
				character == '-' || character == '.' || character == ' ';
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

	float ClassifyFurMaterial(std::string_view texturePath, std::string_view meshPath = {})
	{
		const std::string evidence =
			NormalizeResourceName(std::string(texturePath) + "\\" + std::string(meshPath));
		if (evidence.empty())
			return 0.0f;

		// Explicit authoring vocabulary is authoritative. Boundary-aware matching
		// avoids false positives such as "furniture" and "furnace".
		for (const auto token : { "fur", "furry", "pelt", "fleece", "sheepskin" }) {
			if (HasFurToken(evidence, token))
				return 1.0f;
		}

		// Creature names alone are not enough (eyes, claws and armour often share a
		// directory), so require a body/skin/coat cue as corroborating evidence.
		bool creature = false;
		for (const auto token : {
				 "wolf", "fox", "bear", "sabrecat", "sabercat", "werewolf",
				 "khajiit", "mammoth", "dog", "rabbit", "hare", "goat" }) {
			creature = creature || HasFurToken(evidence, token);
		}
		bool coat = false;
		for (const auto token : { "body", "skin", "coat", "torso", "hide" })
			coat = coat || HasFurToken(evidence, token);

		return creature && coat ? 0.86f : 0.0f;
	}

	void ApplyFurSemantics(PhysicalMaterial::Descriptor& descriptor, float confidence)
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
		if (material.diffuseTexture)
			ApplyFurSemantics(descriptor, ClassifyFurMaterial(NormalizeResourceName(*material.diffuseTexture)));

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
		ApplyFurSemantics(
			resolvedDescriptor,
			ClassifyFurMaterial(diffusePath, material.inputFilePath));
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
}
