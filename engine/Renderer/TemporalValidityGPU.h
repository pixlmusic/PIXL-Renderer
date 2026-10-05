// PIXL Renderer - shared opaque-surface temporal validity guide.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "FrameGraphTypes.h"

#include <array>
#include <cstdint>

#include <d3d11.h>
#include <winrt/base.h>

namespace PIXL::Renderer
{
	class TemporalValidityGPU
	{
	public:
		static TemporalValidityGPU& Get();
		bool Resolve(ID3D11Device* device, ID3D11DeviceContext* context,
			const FrameToken& token, const RenderExtent& extent,
			ID3D11ShaderResourceView* depth, ID3D11ShaderResourceView* normalRoughness,
			ID3D11ShaderResourceView* motion, ID3D11ShaderResourceView* materialMask,
			ID3D11ShaderResourceView* normalWaterMask, ID3D11ShaderResourceView* taaMask,
			ID3D11Buffer* sharedData, ID3D11Buffer* frameData,
			bool historyValid);
		void Invalidate() noexcept;

	private:
		bool EnsureResources(ID3D11Device* device, std::uint32_t width, std::uint32_t height);
		winrt::com_ptr<ID3D11Device> owningDevice;
		winrt::com_ptr<ID3D11ComputeShader> shader;
		winrt::com_ptr<ID3D11Buffer> constants;
		std::array<winrt::com_ptr<ID3D11Texture2D>, 2> packedHistory;
		std::array<winrt::com_ptr<ID3D11ShaderResourceView>, 2> packedSRV;
		std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, 2> packedUAV;
		winrt::com_ptr<ID3D11Texture2D> confidenceTexture;
		winrt::com_ptr<ID3D11ShaderResourceView> confidenceSRV;
		winrt::com_ptr<ID3D11UnorderedAccessView> confidenceUAV;
		winrt::com_ptr<ID3D11Texture2D> annotationTexture;
		winrt::com_ptr<ID3D11ShaderResourceView> annotationSRV;
		winrt::com_ptr<ID3D11UnorderedAccessView> annotationUAV;
		std::uint32_t width{};
		std::uint32_t height{};
		std::uint64_t lastFrame{ FrameToken::kInvalidFrame };
		ResourceEpoch lastEpoch{};
		ResourceEpoch attemptedEpoch{};
		std::uint32_t activeX{};
		std::uint32_t activeY{};
		bool creationFailed{};
	};
}
