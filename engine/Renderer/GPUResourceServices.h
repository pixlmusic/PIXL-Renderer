// PIXL Renderer - shared DX11 transient resources, uploads and history registry.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <d3d11.h>
#include <winrt/base.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace PIXL::Renderer
{
	struct ResourceServiceDiagnostics
	{
		std::size_t pooledTextures = 0;
		std::size_t activeTextures = 0;
		std::size_t pooledBuffers = 0;
		std::size_t activeBuffers = 0;
		std::uint64_t textureCreates = 0;
		std::uint64_t textureReuses = 0;
		std::uint64_t bufferCreates = 0;
		std::uint64_t bufferReuses = 0;
		std::uint64_t uploadCalls = 0;
		std::uint64_t uploadBytes = 0;
		std::size_t registeredHistories = 0;
		std::size_t validHistories = 0;
	};

	class GPUResourceServices
	{
	public:
		struct TextureHandle
		{
			ID3D11Texture2D* resource = nullptr;
			std::uint32_t slot = UINT32_MAX;
			std::uint32_t generation = 0;
			explicit operator bool() const noexcept { return resource != nullptr; }
		};

		struct BufferHandle
		{
			ID3D11Buffer* resource = nullptr;
			std::uint32_t slot = UINT32_MAX;
			std::uint32_t generation = 0;
			explicit operator bool() const noexcept { return resource != nullptr; }
		};

		using HistoryResetCallback = std::function<void()>;

		static GPUResourceServices& Get();

		void Initialize(ID3D11Device* a_device, ID3D11DeviceContext* a_context);
		void BeginFrame(std::uint64_t a_frameIndex);
		void OnResolutionChanged();
		void OnResourcesRecreated(ID3D11Device* a_device, ID3D11DeviceContext* a_context);
		void Shutdown();

		[[nodiscard]] TextureHandle AcquireTexture(const D3D11_TEXTURE2D_DESC& a_desc, std::string_view a_debugName);
		void ReleaseTexture(TextureHandle& a_handle);
		[[nodiscard]] BufferHandle AcquireBuffer(const D3D11_BUFFER_DESC& a_desc, std::string_view a_debugName);
		void ReleaseBuffer(BufferHandle& a_handle);

		bool UploadDiscard(ID3D11Buffer* a_buffer, const void* a_data, std::size_t a_bytes);

		std::uint64_t RegisterHistory(std::string_view a_name, HistoryResetCallback a_reset);
		void SetHistoryValid(std::uint64_t a_id, bool a_valid);
		void UnregisterHistory(std::uint64_t a_id);
		void InvalidateHistories(std::string_view a_reason);

		[[nodiscard]] ResourceServiceDiagnostics GetDiagnostics() const;

	private:
		struct TextureEntry
		{
			D3D11_TEXTURE2D_DESC desc{};
			winrt::com_ptr<ID3D11Texture2D> resource;
			std::uint64_t lastUsedFrame = 0;
			std::uint32_t generation = 1;
			bool inUse = false;
		};

		struct BufferEntry
		{
			D3D11_BUFFER_DESC desc{};
			winrt::com_ptr<ID3D11Buffer> resource;
			std::uint64_t lastUsedFrame = 0;
			std::uint32_t generation = 1;
			bool inUse = false;
		};

		void TrimIdleResources();
		static bool SameTextureDesc(const D3D11_TEXTURE2D_DESC& a_lhs, const D3D11_TEXTURE2D_DESC& a_rhs) noexcept;
		static bool SameBufferDesc(const D3D11_BUFFER_DESC& a_lhs, const D3D11_BUFFER_DESC& a_rhs) noexcept;

		mutable std::mutex mutex;
		winrt::com_ptr<ID3D11Device> device;
		winrt::com_ptr<ID3D11DeviceContext> context;
		std::vector<TextureEntry> textures;
		std::vector<BufferEntry> buffers;
		std::uint64_t frameIndex = 0;
		ResourceServiceDiagnostics totals{};
	};
}
