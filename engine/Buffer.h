#pragma once

#include <d3d11.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <Windows.Foundation.h>
#include <stdio.h>
#include <winrt/base.h>
#include <wrl\client.h>
#include <wrl\wrappers\corewrappers.h>

// Forward declaration — keeps Buffer.h free of Utils/D3D.h's game-type dependencies
namespace Util
{
	void SetResourceName(ID3D11DeviceChild* Resource, const char* Format, ...);
}

namespace detail
{
	inline void SetD3DName(ID3D11DeviceChild* resource, const std::string& name, const char* suffix = nullptr)
	{
		if (!resource || name.empty())
			return;
		if (suffix)
			Util::SetResourceName(resource, "%s%s", name.c_str(), suffix);
		else
			Util::SetResourceName(resource, "%s", name.c_str());
	}
}

#define STATIC_ASSERT_ALIGNAS_16(structName) \
	static_assert(sizeof(structName) % 16 == 0, #structName " is not a multiple of 16.");

/** @brief Rounds a buffer size up to the next 64-byte boundary for constant buffer alignment. */
static constexpr std::uint32_t GetCBufferSize(std::uint32_t buffer_size)
{
	// A wraparound must never turn an oversized request into a tiny buffer.
	return buffer_size > (std::numeric_limits<std::uint32_t>::max)() - 63u ? 0u : (buffer_size + 63u) & ~63u;
}

/** @brief Creates a D3D11 buffer descriptor for a constant buffer of the given byte size. */
inline D3D11_BUFFER_DESC ConstantBufferDesc(uint32_t size, bool dynamic = true)
{
	D3D11_BUFFER_DESC desc{};
	desc.Usage = dynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	desc.CPUAccessFlags = dynamic ? D3D11_CPU_ACCESS_WRITE : 0;
	desc.ByteWidth = GetCBufferSize(size);
	return desc;
}

/** @brief Creates a D3D11 constant buffer descriptor sized for type T. */
template <typename T>
D3D11_BUFFER_DESC ConstantBufferDesc(bool dynamic = true)
{
	return ConstantBufferDesc(sizeof(T), dynamic);
}

/** @brief RAII wrapper around a D3D11 constant buffer with map/update support. */
class ConstantBuffer
{
public:
	explicit ConstantBuffer(D3D11_BUFFER_DESC const& a_desc, const char* name = nullptr) :
		desc(a_desc)
	{
		auto device = globals::d3d::device;
		DX::ThrowIfFailed(device->CreateBuffer(&desc, nullptr, resource.put()));
		if (name)
			detail::SetD3DName(resource.get(), name);
	}

	/** @brief Gets the underlying ID3D11Buffer pointer. */
	ID3D11Buffer* CB() const { return resource.get(); }

	/**
	 * @brief Uploads data to the constant buffer.
	 * @param src_data Pointer to source data.
	 * @param data_size Size in bytes to copy.
	 */
	void Update(void const* src_data, size_t data_size)
	{
		// UpdateSubresource reads the entire ByteWidth, even if the caller has
		// supplied only sizeof(T). Guard both the read and the GPU-visible padding.
		if (data_size > desc.ByteWidth)
			throw std::out_of_range("ConstantBuffer::Update exceeds buffer capacity");
		if (data_size != 0 && src_data == nullptr)
			throw std::invalid_argument("ConstantBuffer::Update received null data");

		auto ctx = globals::d3d::context;
		if (desc.Usage == D3D11_USAGE_DYNAMIC) {
			D3D11_MAPPED_SUBRESOURCE mapped{};
			DX::ThrowIfFailed(ctx->Map(resource.get(), 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped));
			if (data_size != 0)
				std::memcpy(mapped.pData, src_data, data_size);
			if (data_size < desc.ByteWidth)
				std::memset(static_cast<std::byte*>(mapped.pData) + data_size, 0, desc.ByteWidth - data_size);
			ctx->Unmap(resource.get(), 0);
		} else if (data_size == desc.ByteWidth) {
			ctx->UpdateSubresource(resource.get(), 0, nullptr, src_data, 0, 0);
		} else {
			// DEFAULT buffers cannot be mapped. Pad to the full GPU ByteWidth.
			// Most PIXL constant buffers fit in the small, allocation-free path.
			constexpr std::size_t kStackCapacity = 256;
			if (desc.ByteWidth <= kStackCapacity) {
				std::array<std::byte, kStackCapacity> padded{};
				if (data_size != 0)
					std::memcpy(padded.data(), src_data, data_size);
				ctx->UpdateSubresource(resource.get(), 0, nullptr, padded.data(), 0, 0);
			} else {
				std::vector<std::byte> padded(desc.ByteWidth);
				if (data_size != 0)
					std::memcpy(padded.data(), src_data, data_size);
				ctx->UpdateSubresource(resource.get(), 0, nullptr, padded.data(), 0, 0);
			}
		}
	}

	/** @brief Uploads a typed value to the constant buffer. */
	template <typename T>
	void Update(T const& src_data)
	{
		Update(&src_data, sizeof(T));
	}

private:
	winrt::com_ptr<ID3D11Buffer> resource;
	D3D11_BUFFER_DESC desc;
};

/** @brief RAII wrapper around a generic D3D11 buffer with SRV and UAV support. */
class Buffer
{
public:
	explicit Buffer(D3D11_BUFFER_DESC const& a_desc, D3D11_SUBRESOURCE_DATA* a_init = nullptr, const char* name = nullptr) :
		desc(a_desc)
	{
		auto device = globals::d3d::device;
		DX::ThrowIfFailed(device->CreateBuffer(&desc, a_init, resource.put()));
		if (name) {
			name_ = name;
			detail::SetD3DName(resource.get(), name_);
		}
	}

	/** @brief Creates a shader resource view from the given descriptor. */
	void CreateSRV(D3D11_SHADER_RESOURCE_VIEW_DESC const& a_desc)
	{
		auto device = globals::d3d::device;
		winrt::com_ptr<ID3D11ShaderResourceView> view;
		DX::ThrowIfFailed(device->CreateShaderResourceView(resource.get(), &a_desc, view.put()));
		srv = std::move(view);
		detail::SetD3DName(srv.get(), name_, " SRV");
	}

	/** @brief Creates an unordered access view from the given descriptor. */
	void CreateUAV(D3D11_UNORDERED_ACCESS_VIEW_DESC const& a_desc)
	{
		auto device = globals::d3d::device;
		winrt::com_ptr<ID3D11UnorderedAccessView> view;
		DX::ThrowIfFailed(device->CreateUnorderedAccessView(resource.get(), &a_desc, view.put()));
		uav = std::move(view);
		detail::SetD3DName(uav.get(), name_, " UAV");
	}

	D3D11_BUFFER_DESC desc;
	winrt::com_ptr<ID3D11Buffer> resource;
	winrt::com_ptr<ID3D11ShaderResourceView> srv;
	winrt::com_ptr<ID3D11UnorderedAccessView> uav;

private:
	std::string name_;
};

/** @brief RAII wrapper around a D3D11 1D texture with SRV, UAV, and RTV support. */
class Texture1D
{
public:
	explicit Texture1D(D3D11_TEXTURE1D_DESC const& a_desc, const char* name = nullptr) :
		desc(a_desc)
	{
		auto device = globals::d3d::device;
		DX::ThrowIfFailed(device->CreateTexture1D(&desc, nullptr, resource.put()));
		if (name) {
			name_ = name;
			detail::SetD3DName(resource.get(), name_);
		}
	}

	/** @brief Creates a shader resource view from the given descriptor. */
	void CreateSRV(D3D11_SHADER_RESOURCE_VIEW_DESC const& a_desc)
	{
		auto device = globals::d3d::device;
		winrt::com_ptr<ID3D11ShaderResourceView> view;
		DX::ThrowIfFailed(device->CreateShaderResourceView(resource.get(), &a_desc, view.put()));
		srv = std::move(view);
		detail::SetD3DName(srv.get(), name_, " SRV");
	}

	/** @brief Creates an unordered access view from the given descriptor. */
	void CreateUAV(D3D11_UNORDERED_ACCESS_VIEW_DESC const& a_desc)
	{
		auto device = globals::d3d::device;
		winrt::com_ptr<ID3D11UnorderedAccessView> view;
		DX::ThrowIfFailed(device->CreateUnorderedAccessView(resource.get(), &a_desc, view.put()));
		uav = std::move(view);
		detail::SetD3DName(uav.get(), name_, " UAV");
	}

	/** @brief Creates a render target view from the given descriptor. */
	void CreateRTV(D3D11_RENDER_TARGET_VIEW_DESC const& a_desc)
	{
		auto device = globals::d3d::device;
		winrt::com_ptr<ID3D11RenderTargetView> view;
		DX::ThrowIfFailed(device->CreateRenderTargetView(resource.get(), &a_desc, view.put()));
		rtv = std::move(view);
		detail::SetD3DName(rtv.get(), name_, " RTV");
	}

	D3D11_TEXTURE1D_DESC desc;
	winrt::com_ptr<ID3D11Texture1D> resource;
	winrt::com_ptr<ID3D11ShaderResourceView> srv;
	winrt::com_ptr<ID3D11UnorderedAccessView> uav;
	winrt::com_ptr<ID3D11RenderTargetView> rtv;

private:
	std::string name_;
};

/** @brief RAII wrapper around a D3D11 2D texture with SRV, UAV, RTV, and DSV support. */
class Texture2D
{
public:
	explicit Texture2D(D3D11_TEXTURE2D_DESC const& a_desc, const char* name = nullptr) :
		desc(a_desc)
	{
		auto device = globals::d3d::device;
		DX::ThrowIfFailed(device->CreateTexture2D(&desc, nullptr, resource.put()));
		if (name) {
			name_ = name;
			detail::SetD3DName(resource.get(), name_);
		}
	}

	explicit Texture2D(ID3D11Texture2D* a_resource, const char* name = nullptr)
	{
		if (!a_resource)
			throw std::invalid_argument("Texture2D requires a non-null resource");
		a_resource->GetDesc(&desc);
		// attach() takes ownership of the existing COM reference (not AddRef).
		resource.attach(a_resource);
		if (name) {
			name_ = name;
			detail::SetD3DName(resource.get(), name_);
		}
	}

	/** @brief Creates a shader resource view from the given descriptor. */
	void CreateSRV(D3D11_SHADER_RESOURCE_VIEW_DESC const& a_desc)
	{
		auto device = globals::d3d::device;
		winrt::com_ptr<ID3D11ShaderResourceView> view;
		DX::ThrowIfFailed(device->CreateShaderResourceView(resource.get(), &a_desc, view.put()));
		srv = std::move(view);
		detail::SetD3DName(srv.get(), name_, " SRV");
	}

	/** @brief Creates an unordered access view from the given descriptor. */
	void CreateUAV(D3D11_UNORDERED_ACCESS_VIEW_DESC const& a_desc)
	{
		auto device = globals::d3d::device;
		winrt::com_ptr<ID3D11UnorderedAccessView> view;
		DX::ThrowIfFailed(device->CreateUnorderedAccessView(resource.get(), &a_desc, view.put()));
		uav = std::move(view);
		detail::SetD3DName(uav.get(), name_, " UAV");
	}

	/** @brief Creates a render target view from the given descriptor. */
	void CreateRTV(D3D11_RENDER_TARGET_VIEW_DESC const& a_desc)
	{
		auto device = globals::d3d::device;
		winrt::com_ptr<ID3D11RenderTargetView> view;
		DX::ThrowIfFailed(device->CreateRenderTargetView(resource.get(), &a_desc, view.put()));
		rtv = std::move(view);
		detail::SetD3DName(rtv.get(), name_, " RTV");
	}

	/** @brief Creates a depth-stencil view from the given descriptor. */
	void CreateDSV(D3D11_DEPTH_STENCIL_VIEW_DESC const& a_desc)
	{
		auto device = globals::d3d::device;
		winrt::com_ptr<ID3D11DepthStencilView> view;
		DX::ThrowIfFailed(device->CreateDepthStencilView(resource.get(), &a_desc, view.put()));
		dsv = std::move(view);
		detail::SetD3DName(dsv.get(), name_, " DSV");
	}

	D3D11_TEXTURE2D_DESC desc;
	winrt::com_ptr<ID3D11Texture2D> resource;
	winrt::com_ptr<ID3D11ShaderResourceView> srv;
	winrt::com_ptr<ID3D11UnorderedAccessView> uav;
	winrt::com_ptr<ID3D11RenderTargetView> rtv;
	winrt::com_ptr<ID3D11DepthStencilView> dsv;

private:
	std::string name_;
};

/** @brief RAII wrapper around a D3D11 3D texture with SRV, UAV, and RTV support. */
class Texture3D
{
public:
	explicit Texture3D(D3D11_TEXTURE3D_DESC const& a_desc, const char* name = nullptr) :
		desc(a_desc)
	{
		auto device = globals::d3d::device;
		DX::ThrowIfFailed(device->CreateTexture3D(&desc, nullptr, resource.put()));
		if (name) {
			name_ = name;
			detail::SetD3DName(resource.get(), name_);
		}
	}

	/** @brief Creates a shader resource view from the given descriptor. */
	void CreateSRV(D3D11_SHADER_RESOURCE_VIEW_DESC const& a_desc)
	{
		auto device = globals::d3d::device;
		winrt::com_ptr<ID3D11ShaderResourceView> view;
		DX::ThrowIfFailed(device->CreateShaderResourceView(resource.get(), &a_desc, view.put()));
		srv = std::move(view);
		detail::SetD3DName(srv.get(), name_, " SRV");
	}

	/** @brief Creates an unordered access view from the given descriptor. */
	void CreateUAV(D3D11_UNORDERED_ACCESS_VIEW_DESC const& a_desc)
	{
		auto device = globals::d3d::device;
		winrt::com_ptr<ID3D11UnorderedAccessView> view;
		DX::ThrowIfFailed(device->CreateUnorderedAccessView(resource.get(), &a_desc, view.put()));
		uav = std::move(view);
		detail::SetD3DName(uav.get(), name_, " UAV");
	}

	/** @brief Creates a render target view from the given descriptor. */
	void CreateRTV(D3D11_RENDER_TARGET_VIEW_DESC const& a_desc)
	{
		auto device = globals::d3d::device;
		winrt::com_ptr<ID3D11RenderTargetView> view;
		DX::ThrowIfFailed(device->CreateRenderTargetView(resource.get(), &a_desc, view.put()));
		rtv = std::move(view);
		detail::SetD3DName(rtv.get(), name_, " RTV");
	}

	D3D11_TEXTURE3D_DESC desc;
	winrt::com_ptr<ID3D11Texture3D> resource;
	winrt::com_ptr<ID3D11ShaderResourceView> srv;
	winrt::com_ptr<ID3D11UnorderedAccessView> uav;
	winrt::com_ptr<ID3D11RenderTargetView> rtv;

private:
	std::string name_;
};
