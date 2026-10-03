// PIXL Renderer - shared DX11 transient resources, uploads and history registry.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later

#include "GPUResourceServices.h"
#include "TemporalContext.h"

#include "Utils/D3D.h"

#include <algorithm>
#include <cstring>

namespace PIXL::Renderer
{
	namespace
	{
		constexpr std::uint64_t kIdleFrameLifetime = 180u;
		constexpr std::size_t kMaxPooledTextures = 64u;
		constexpr std::size_t kMaxPooledBuffers = 64u;
	}

	GPUResourceServices& GPUResourceServices::Get()
	{
		static GPUResourceServices instance;
		return instance;
	}

	void GPUResourceServices::Initialize(ID3D11Device* a_device, ID3D11DeviceContext* a_context)
	{
		std::scoped_lock lock(mutex);
		device.copy_from(a_device);
		context.copy_from(a_context);
	}

	void GPUResourceServices::BeginFrame(std::uint64_t a_frameIndex)
	{
		std::scoped_lock lock(mutex);
		frameIndex = a_frameIndex;
		TrimIdleResources();
	}

	void GPUResourceServices::OnResolutionChanged()
	{
		std::scoped_lock lock(mutex);
		for (auto& entry : textures) {
			if (!entry.inUse)
				entry.resource = nullptr;
		}
	}

	void GPUResourceServices::OnResourcesRecreated(ID3D11Device* a_device, ID3D11DeviceContext* a_context)
	{
		std::scoped_lock lock(mutex);
		textures.clear();
		buffers.clear();
		device.copy_from(a_device);
		context.copy_from(a_context);
	}

	void GPUResourceServices::Shutdown()
	{
		std::scoped_lock lock(mutex);
		textures.clear();
		buffers.clear();
		context = nullptr;
		device = nullptr;
	}

	GPUResourceServices::TextureHandle GPUResourceServices::AcquireTexture(
		const D3D11_TEXTURE2D_DESC& a_desc,
		std::string_view a_debugName)
	{
		std::scoped_lock lock(mutex);
		if (!device || a_desc.Width == 0 || a_desc.Height == 0 || a_desc.ArraySize == 0 || a_desc.MipLevels == 0)
			return {};
		for (std::uint32_t index = 0; index < textures.size(); ++index) {
			auto& entry = textures[index];
			if (!entry.inUse && entry.resource && SameTextureDesc(entry.desc, a_desc)) {
				entry.inUse = true;
				entry.lastUsedFrame = frameIndex;
				++entry.generation;
				++totals.textureReuses;
				if (!a_debugName.empty())
					Util::SetResourceName(entry.resource.get(), "%.*s", static_cast<int>(a_debugName.size()), a_debugName.data());
				return { entry.resource.get(), index, entry.generation };
			}
		}

		TextureEntry entry{};
		entry.desc = a_desc;
		entry.inUse = true;
		entry.lastUsedFrame = frameIndex;
		if (FAILED(device->CreateTexture2D(&a_desc, nullptr, entry.resource.put())))
			return {};
		if (!a_debugName.empty())
			Util::SetResourceName(entry.resource.get(), "%.*s", static_cast<int>(a_debugName.size()), a_debugName.data());
		auto empty = std::ranges::find_if(textures, [](const TextureEntry& a_entry) {
			return !a_entry.inUse && !a_entry.resource;
		});
		if (empty != textures.end()) {
			entry.generation = empty->generation + 1u;
			*empty = std::move(entry);
			const auto index = static_cast<std::uint32_t>(std::distance(textures.begin(), empty));
			++totals.textureCreates;
			return { empty->resource.get(), index, empty->generation };
		}
		if (textures.size() >= kMaxPooledTextures) {
			auto oldest = std::ranges::min_element(textures, {}, &TextureEntry::lastUsedFrame);
			if (oldest == textures.end() || oldest->inUse)
				return {};
			entry.generation = oldest->generation + 1u;
			*oldest = std::move(entry);
			const auto index = static_cast<std::uint32_t>(std::distance(textures.begin(), oldest));
			++totals.textureCreates;
			return { oldest->resource.get(), index, oldest->generation };
		}
		textures.push_back(std::move(entry));
		++totals.textureCreates;
		const auto index = static_cast<std::uint32_t>(textures.size() - 1u);
		return { textures[index].resource.get(), index, textures[index].generation };
	}

	void GPUResourceServices::ReleaseTexture(TextureHandle& a_handle)
	{
		std::scoped_lock lock(mutex);
		if (a_handle.slot < textures.size()) {
			auto& entry = textures[a_handle.slot];
			if (entry.generation == a_handle.generation && entry.resource.get() == a_handle.resource) {
				entry.inUse = false;
				entry.lastUsedFrame = frameIndex;
			}
		}
		a_handle = {};
	}

	GPUResourceServices::BufferHandle GPUResourceServices::AcquireBuffer(
		const D3D11_BUFFER_DESC& a_desc,
		std::string_view a_debugName)
	{
		std::scoped_lock lock(mutex);
		if (!device || a_desc.ByteWidth == 0)
			return {};
		for (std::uint32_t index = 0; index < buffers.size(); ++index) {
			auto& entry = buffers[index];
			if (!entry.inUse && entry.resource && SameBufferDesc(entry.desc, a_desc)) {
				entry.inUse = true;
				entry.lastUsedFrame = frameIndex;
				++entry.generation;
				++totals.bufferReuses;
				if (!a_debugName.empty())
					Util::SetResourceName(entry.resource.get(), "%.*s", static_cast<int>(a_debugName.size()), a_debugName.data());
				return { entry.resource.get(), index, entry.generation };
			}
		}

		BufferEntry entry{};
		entry.desc = a_desc;
		entry.inUse = true;
		entry.lastUsedFrame = frameIndex;
		if (FAILED(device->CreateBuffer(&a_desc, nullptr, entry.resource.put())))
			return {};
		if (!a_debugName.empty())
			Util::SetResourceName(entry.resource.get(), "%.*s", static_cast<int>(a_debugName.size()), a_debugName.data());
		auto empty = std::ranges::find_if(buffers, [](const BufferEntry& a_entry) {
			return !a_entry.inUse && !a_entry.resource;
		});
		if (empty != buffers.end()) {
			entry.generation = empty->generation + 1u;
			*empty = std::move(entry);
			const auto index = static_cast<std::uint32_t>(std::distance(buffers.begin(), empty));
			++totals.bufferCreates;
			return { empty->resource.get(), index, empty->generation };
		}
		if (buffers.size() >= kMaxPooledBuffers) {
			auto oldest = std::ranges::min_element(buffers, {}, &BufferEntry::lastUsedFrame);
			if (oldest == buffers.end() || oldest->inUse)
				return {};
			entry.generation = oldest->generation + 1u;
			*oldest = std::move(entry);
			const auto index = static_cast<std::uint32_t>(std::distance(buffers.begin(), oldest));
			++totals.bufferCreates;
			return { oldest->resource.get(), index, oldest->generation };
		}
		buffers.push_back(std::move(entry));
		++totals.bufferCreates;
		const auto index = static_cast<std::uint32_t>(buffers.size() - 1u);
		return { buffers[index].resource.get(), index, buffers[index].generation };
	}

	void GPUResourceServices::ReleaseBuffer(BufferHandle& a_handle)
	{
		std::scoped_lock lock(mutex);
		if (a_handle.slot < buffers.size()) {
			auto& entry = buffers[a_handle.slot];
			if (entry.generation == a_handle.generation && entry.resource.get() == a_handle.resource) {
				entry.inUse = false;
				entry.lastUsedFrame = frameIndex;
			}
		}
		a_handle = {};
	}

	bool GPUResourceServices::UploadDiscard(ID3D11Buffer* a_buffer, const void* a_data, std::size_t a_bytes)
	{
		if (!a_buffer || !a_data || a_bytes == 0)
			return false;
		winrt::com_ptr<ID3D11DeviceContext> localContext;
		{
			std::scoped_lock lock(mutex);
			localContext = context;
		}
		if (!localContext)
			return false;
		D3D11_BUFFER_DESC desc{};
		a_buffer->GetDesc(&desc);
		if (desc.Usage != D3D11_USAGE_DYNAMIC || !(desc.CPUAccessFlags & D3D11_CPU_ACCESS_WRITE) || a_bytes > desc.ByteWidth)
			return false;
		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (FAILED(localContext->Map(a_buffer, 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped)))
			return false;
		std::memcpy(mapped.pData, a_data, a_bytes);
		localContext->Unmap(a_buffer, 0u);
		{
			std::scoped_lock lock(mutex);
			++totals.uploadCalls;
			totals.uploadBytes += a_bytes;
		}
		return true;
	}

	std::uint64_t GPUResourceServices::RegisterHistory(std::string_view a_name, HistoryResetCallback a_reset)
	{
		return TemporalContext::Get().RegisterHistory({
			.name = std::string(a_name),
			.owner = "GPUResourceServices compatibility",
			.invalidateOn = AllTemporalInvalidations,
			.reset = [reset = std::move(a_reset)](TemporalInvalidationReason) { if (reset) reset(); }
		});
	}

	void GPUResourceServices::SetHistoryValid(std::uint64_t a_id, bool a_valid)
	{
		TemporalContext::Get().SetHistoryValid(a_id, a_valid);
	}

	void GPUResourceServices::UnregisterHistory(std::uint64_t a_id)
	{
		TemporalContext::Get().UnregisterHistory(a_id);
	}

	void GPUResourceServices::InvalidateHistories(std::string_view a_reason)
	{
		TemporalContext::Get().Invalidate(TemporalInvalidationReason::ModuleReset, a_reason);
	}

	ResourceServiceDiagnostics GPUResourceServices::GetDiagnostics() const
	{
		std::scoped_lock lock(mutex);
		auto result = totals;
		result.pooledTextures = std::ranges::count_if(textures, [](const TextureEntry& a_entry) { return !!a_entry.resource; });
		result.activeTextures = std::ranges::count(textures, true, &TextureEntry::inUse);
		result.pooledBuffers = std::ranges::count_if(buffers, [](const BufferEntry& a_entry) { return !!a_entry.resource; });
		result.activeBuffers = std::ranges::count(buffers, true, &BufferEntry::inUse);
		const auto temporal = TemporalContext::Get().GetDiagnostics();
		result.registeredHistories = temporal.histories.size();
		result.validHistories = std::ranges::count(temporal.histories, true, &HistoryDiagnostics::valid);
		return result;
	}

	void GPUResourceServices::TrimIdleResources()
	{
		const auto stale = [this](const auto& a_entry) {
			return !a_entry.inUse && frameIndex > a_entry.lastUsedFrame &&
			       frameIndex - a_entry.lastUsedFrame > kIdleFrameLifetime;
		};
		for (auto& entry : textures) {
			if (entry.resource && stale(entry))
				entry.resource = nullptr;
		}
		for (auto& entry : buffers) {
			if (entry.resource && stale(entry))
				entry.resource = nullptr;
		}
	}

	bool GPUResourceServices::SameTextureDesc(const D3D11_TEXTURE2D_DESC& a, const D3D11_TEXTURE2D_DESC& b) noexcept
	{
		return a.Width == b.Width && a.Height == b.Height && a.MipLevels == b.MipLevels &&
		       a.ArraySize == b.ArraySize && a.Format == b.Format &&
		       a.SampleDesc.Count == b.SampleDesc.Count && a.SampleDesc.Quality == b.SampleDesc.Quality &&
		       a.Usage == b.Usage && a.BindFlags == b.BindFlags &&
		       a.CPUAccessFlags == b.CPUAccessFlags && a.MiscFlags == b.MiscFlags;
	}

	bool GPUResourceServices::SameBufferDesc(const D3D11_BUFFER_DESC& a, const D3D11_BUFFER_DESC& b) noexcept
	{
		return a.ByteWidth == b.ByteWidth && a.Usage == b.Usage && a.BindFlags == b.BindFlags &&
		       a.CPUAccessFlags == b.CPUAccessFlags && a.MiscFlags == b.MiscFlags &&
		       a.StructureByteStride == b.StructureByteStride;
	}
}
