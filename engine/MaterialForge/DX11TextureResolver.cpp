#include "MaterialForge/DX11TextureResolver.h"

#include <RE/N/NiSourceTexture.h>

#include <algorithm>
#include <mutex>

namespace PhysicalMaterial::DX11
{
	TextureResolver& TextureResolver::GetSingleton()
	{
		static TextureResolver singleton;
		return singleton;
	}

	void TextureResolver::SetEnabled(bool enabled)
	{
		enabled_.store(enabled, std::memory_order_release);
		if (!enabled) {
			std::unique_lock lock(mutex_);
			if (!textures_.empty()) {
				textures_.clear();
				generation_++;
			}
		}
	}

	void TextureResolver::Observe(TextureID textureID, const RE::NiSourceTexture& texture)
	{
		if (!enabled_.load(std::memory_order_acquire) || textureID == 0 || texture.rendererTexture == nullptr ||
			texture.rendererTexture->resourceView == nullptr) {
			return;
		}

		auto* sourceView = texture.rendererTexture->resourceView;
		std::unique_lock lock(mutex_);
		if (const auto current = textures_.find(textureID);
			current != textures_.end() && current->second.get() == sourceView) {
			return;
		}
		if (!textures_.contains(textureID) && textures_.size() >= MaxRetainedTextureViews) {
			return;
		}
		winrt::com_ptr<ID3D11ShaderResourceView> retained;
		retained.copy_from(sourceView);
		textures_.insert_or_assign(textureID, std::move(retained));
		generation_++;
	}

	TextureSnapshot TextureResolver::Snapshot() const
	{
		std::shared_lock lock(mutex_);
		TextureSnapshot snapshot;
		snapshot.generation = generation_;
		snapshot.textures.reserve(textures_.size());
		for (const auto& [textureID, view] : textures_) {
			snapshot.textures.push_back(ResolvedTexture{ textureID, view });
		}
		std::ranges::sort(snapshot.textures, {}, &ResolvedTexture::textureID);
		return snapshot;
	}

	std::uint64_t TextureResolver::GetGeneration() const
	{
		std::shared_lock lock(mutex_);
		return generation_;
	}
}
