// PIXL Renderer - scoped DX11 copy/read binding conflict handling.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <d3d11.h>
#include <winrt/base.h>

namespace PIXL::Renderer
{
	// For a short copy/resolve interval only. The original output set and each
	// conflicting shader input are restored, including Skyrim-owned bindings.
	class ScopedD3D11BindingState
	{
	public:
		explicit ScopedD3D11BindingState(ID3D11DeviceContext* context) noexcept;
		~ScopedD3D11BindingState();

		ScopedD3D11BindingState(const ScopedD3D11BindingState&) = delete;
		ScopedD3D11BindingState& operator=(const ScopedD3D11BindingState&) = delete;

		bool PrepareForCopy(ID3D11Resource* source, ID3D11Resource* destination);
		std::uint32_t ConflictCount() const noexcept { return conflictCount; }

	private:
		enum class Stage : std::uint8_t { VS, HS, DS, GS, PS, CS };
		struct SavedInput
		{
			Stage stage{};
			std::uint32_t slot{};
			winrt::com_ptr<ID3D11ShaderResourceView> view;
		};

		void Restore() noexcept;
		void UnbindShaderInputs();
		void UnbindComputeOutputs();
		void UnbindPixelOutputs();
		void UnbindOutputMerger();

		winrt::com_ptr<ID3D11DeviceContext> context;
		std::array<winrt::com_ptr<ID3D11RenderTargetView>, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> savedRTVs{};
		winrt::com_ptr<ID3D11DepthStencilView> savedDSV;
		std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, D3D11_PS_CS_UAV_REGISTER_COUNT> savedComputeUAVs{};
		std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, D3D11_PS_CS_UAV_REGISTER_COUNT> savedPixelUAVs{};
		std::array<bool, D3D11_PS_CS_UAV_REGISTER_COUNT> changedComputeUAVs{};
		std::array<bool, D3D11_PS_CS_UAV_REGISTER_COUNT> changedPixelUAVs{};
		std::array<ID3D11Resource*, 2> copyResources{};
		std::array<SavedInput, 6 * D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> savedInputs{};
		std::size_t savedInputCount{};
		bool outputMergerCaptured{};
		bool computeOutputsCaptured{};
		bool pixelOutputsCaptured{};
		std::uint32_t conflictCount{};
	};
}
