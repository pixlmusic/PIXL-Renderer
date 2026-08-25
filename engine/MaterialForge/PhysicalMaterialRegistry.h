#pragma once

#include "MaterialForge/PhysicalMaterial.h"

#include <shared_mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace RE
{
	class BSLightingShaderMaterialBase;
	class NiSourceTexture;
}

class BSLightingShaderMaterialPBR;
class BSLightingShaderMaterialPBRLandscape;

namespace PhysicalMaterial
{
	/** @brief API-neutral description of a texture source for backend resource creation. */
	struct TextureSource
	{
		TextureID textureID = 0;
		std::uint32_t revision = 1;
		bool fileBacked = false;
		std::string resourceName;
	};

	/** @brief Immutable copy of the current material and texture tables. */
	struct TableSnapshot
	{
		std::uint64_t generation = 0;
		std::vector<TableEntry> materials;
		std::vector<TextureSource> textures;
	};

	/**
	 * @brief Extracts Skyrim materials into stable, backend-neutral table rows.
	 *
	 * The registry owns no game or graphics resources. Owner addresses are used only as internal
	 * live-slot keys; they never appear in snapshots consumed by a rendering backend.
	 */
	class Registry
	{
	public:
		/** @brief Returns the process-wide material registry. */
		static Registry& GetSingleton();

		/** @brief Observes an ordinary Skyrim lighting material and returns its stable table ID. */
		MaterialID ObserveLegacy(const RE::BSLightingShaderMaterialBase& material);

		/** @brief Observes a standard Material Forge material and returns its stable table ID. */
		MaterialID ObservePBR(const BSLightingShaderMaterialPBR& material, const Descriptor& descriptor);

		/** @brief Observes one Material Forge landscape layer and returns that layer's stable table ID. */
		MaterialID ObservePBRLandscape(
			const BSLightingShaderMaterialPBRLandscape& material,
			std::uint32_t layer,
			const Descriptor& descriptor);

		/** @brief Returns the existing legacy material ID without changing registry state. */
		[[nodiscard]] MaterialID FindLegacy(const RE::BSLightingShaderMaterialBase& material) const;

		/** @brief Returns the existing standard PBR material ID without changing registry state. */
		[[nodiscard]] MaterialID FindPBR(const BSLightingShaderMaterialPBR& material) const;

		/** @brief Returns the existing PBR landscape-layer ID without changing registry state. */
		[[nodiscard]] MaterialID FindPBRLandscape(
			const BSLightingShaderMaterialPBRLandscape& material,
			std::uint32_t layer) const;

		/** @brief Removes all rows owned by a destroyed material. */
		void Release(const void* owner);

		/** @brief Copies a coherent table generation for asynchronous backend upload. */
		[[nodiscard]] TableSnapshot Snapshot() const;

		/** @brief Returns the current table generation without copying table rows. */
		[[nodiscard]] std::uint64_t GetGeneration() const;

	private:
		struct OwnerKey
		{
			const void* owner = nullptr;
			std::uint32_t layer = 0;

			bool operator==(const OwnerKey&) const = default;
		};

		struct OwnerKeyHash
		{
			std::size_t operator()(const OwnerKey& key) const noexcept;
		};

		struct SourceBinding
		{
			Texture semantic;
			ColorSpace colorSpace;
			const RE::NiSourceTexture* texture;
		};

		MaterialID Observe(
			OwnerKey owner,
			Descriptor descriptor,
			std::span<const SourceBinding> sourceBindings);
		TextureID ResolveTexture(const RE::NiSourceTexture& texture);
		[[nodiscard]] MaterialID Find(OwnerKey owner) const;

		mutable std::shared_mutex mutex_;
		std::unordered_map<OwnerKey, MaterialID, OwnerKeyHash> owners_;
		std::unordered_map<MaterialID, TableEntry> materials_;
		std::unordered_map<std::string, TextureID> namedTextures_;
		std::unordered_map<const RE::NiSourceTexture*, TextureID> runtimeTextures_;
		std::unordered_map<TextureID, TextureSource> textures_;
		MaterialID nextMaterialID_ = 1;
		TextureID nextTextureID_ = 1;
		std::uint64_t generation_ = 0;
	};
}
