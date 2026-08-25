#pragma once

#include "MaterialForge/PhysicalMaterial.h"

#include <d3d11.h>
#include <winrt/base.h>

#include <atomic>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace RE
{
	class NiSourceTexture;
}

namespace PhysicalMaterial::DX11
{
	/** @brief One retained D3D11 texture view associated with an API-neutral texture identity. */
	struct ResolvedTexture
	{
		TextureID textureID = 0;
		winrt::com_ptr<ID3D11ShaderResourceView> view;
	};

	/** @brief Coherent copy of currently resolved D3D11 texture views. */
	struct TextureSnapshot
	{
		std::uint64_t generation = 0;
		std::vector<ResolvedTexture> textures;
	};

	/** @brief Captures renderer-owned texture views without exposing them through the physical contract. */
	class TextureResolver
	{
	public:
		/** @brief Returns the process-wide D3D11 physical-material texture resolver. */
		static TextureResolver& GetSingleton();

		/** @brief Enables observation, or releases all retained D3D11 views. */
		void SetEnabled(bool enabled);

		/** @brief Associates a physical texture identity with its currently loaded Skyrim view. */
		void Observe(TextureID textureID, const RE::NiSourceTexture& texture);

		/** @brief Copies the currently resolved views for render-thread binding. */
		[[nodiscard]] TextureSnapshot Snapshot() const;

		/** @brief Returns the current resolver generation without retaining another view copy. */
		[[nodiscard]] std::uint64_t GetGeneration() const;

	private:
		static constexpr std::size_t MaxRetainedTextureViews = 64;

		mutable std::shared_mutex mutex_;
		std::unordered_map<TextureID, winrt::com_ptr<ID3D11ShaderResourceView>> textures_;
		std::atomic_bool enabled_ = false;
		std::uint64_t generation_ = 0;
	};
}
