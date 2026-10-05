// PIXL Renderer - scoped DX11 copy/read binding conflict handling.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later

#include "D3D11BindingScope.h"

#include <algorithm>
#include <ranges>

namespace PIXL::Renderer
{
	namespace
	{
		bool References(ID3D11View* view, ID3D11Resource* resource)
		{
			if (!view || !resource)
				return false;
			winrt::com_ptr<ID3D11Resource> bound;
			view->GetResource(bound.put());
			return bound.get() == resource;
		}
	}

	ScopedD3D11BindingState::ScopedD3D11BindingState(ID3D11DeviceContext* a_context) noexcept
	{
		if (a_context)
			context.copy_from(a_context);
	}

	ScopedD3D11BindingState::~ScopedD3D11BindingState()
	{
		Restore();
	}

	bool ScopedD3D11BindingState::PrepareForCopy(ID3D11Resource* source, ID3D11Resource* destination)
	{
		if (!context || !source || !destination || source == destination)
			return false;
		copyResources = { source, destination };
		UnbindOutputMerger();
		UnbindComputeOutputs();
		UnbindPixelOutputs();
		UnbindShaderInputs();
		return true;
	}

	void ScopedD3D11BindingState::UnbindOutputMerger()
	{
		if (!outputMergerCaptured) {
			std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> raw{};
			ID3D11DepthStencilView* depth{};
			context->OMGetRenderTargets(static_cast<UINT>(raw.size()), raw.data(), &depth);
			for (std::size_t i = 0; i < raw.size(); ++i)
				savedRTVs[i].attach(raw[i]);
			savedDSV.attach(depth);
			outputMergerCaptured = true;
		}
		std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> filtered{};
		bool changed = false;
		for (std::size_t i = 0; i < filtered.size(); ++i) {
			filtered[i] = savedRTVs[i].get();
			if (References(filtered[i], copyResources[0]) || References(filtered[i], copyResources[1])) {
				filtered[i] = nullptr;
				changed = true;
				++conflictCount;
			}
		}
		auto* depth = savedDSV.get();
		if (References(depth, copyResources[0]) || References(depth, copyResources[1])) {
			depth = nullptr;
			changed = true;
			++conflictCount;
		}
		if (changed)
			context->OMSetRenderTargets(static_cast<UINT>(filtered.size()), filtered.data(), depth);
	}

	void ScopedD3D11BindingState::UnbindComputeOutputs()
	{
		if (!computeOutputsCaptured) {
			std::array<ID3D11UnorderedAccessView*, D3D11_PS_CS_UAV_REGISTER_COUNT> raw{};
			context->CSGetUnorderedAccessViews(0, static_cast<UINT>(raw.size()), raw.data());
			for (std::size_t i = 0; i < raw.size(); ++i)
				savedComputeUAVs[i].attach(raw[i]);
			computeOutputsCaptured = true;
		}
		for (UINT i = 0; i < savedComputeUAVs.size(); ++i) {
			if (References(savedComputeUAVs[i].get(), copyResources[0]) || References(savedComputeUAVs[i].get(), copyResources[1])) {
				ID3D11UnorderedAccessView* nullView{};
				context->CSSetUnorderedAccessViews(i, 1, &nullView, nullptr);
				changedComputeUAVs[i] = true;
				++conflictCount;
			}
		}
	}

	void ScopedD3D11BindingState::UnbindPixelOutputs()
	{
		if (!pixelOutputsCaptured) {
			std::array<ID3D11UnorderedAccessView*, D3D11_PS_CS_UAV_REGISTER_COUNT> raw{};
			context->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0, static_cast<UINT>(raw.size()), raw.data());
			for (std::size_t i = 0; i < raw.size(); ++i)
				savedPixelUAVs[i].attach(raw[i]);
			pixelOutputsCaptured = true;
		}
		for (UINT i = 0; i < savedPixelUAVs.size(); ++i) {
			if (References(savedPixelUAVs[i].get(), copyResources[0]) || References(savedPixelUAVs[i].get(), copyResources[1])) {
				ID3D11UnorderedAccessView* nullView{};
				context->OMSetRenderTargetsAndUnorderedAccessViews(
					D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL, nullptr, nullptr, i, 1, &nullView, nullptr);
				changedPixelUAVs[i] = true;
				++conflictCount;
			}
		}
	}

	void ScopedD3D11BindingState::UnbindShaderInputs()
	{
		using Getter = void (ID3D11DeviceContext::*)(UINT, UINT, ID3D11ShaderResourceView**);
		using Setter = void (ID3D11DeviceContext::*)(UINT, UINT, ID3D11ShaderResourceView* const*);
		constexpr std::array<Getter, 6> getters{ &ID3D11DeviceContext::VSGetShaderResources,
			&ID3D11DeviceContext::HSGetShaderResources, &ID3D11DeviceContext::DSGetShaderResources,
			&ID3D11DeviceContext::GSGetShaderResources, &ID3D11DeviceContext::PSGetShaderResources,
			&ID3D11DeviceContext::CSGetShaderResources };
		constexpr std::array<Setter, 6> setters{ &ID3D11DeviceContext::VSSetShaderResources,
			&ID3D11DeviceContext::HSSetShaderResources, &ID3D11DeviceContext::DSSetShaderResources,
			&ID3D11DeviceContext::GSSetShaderResources, &ID3D11DeviceContext::PSSetShaderResources,
			&ID3D11DeviceContext::CSSetShaderResources };
		for (std::size_t stage = 0; stage < getters.size(); ++stage) {
			std::array<ID3D11ShaderResourceView*, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> raw{};
			(context.get()->*getters[stage])(0, static_cast<UINT>(raw.size()), raw.data());
			for (UINT slot = 0; slot < raw.size(); ++slot) {
				winrt::com_ptr<ID3D11ShaderResourceView> view;
				view.attach(raw[slot]);
				if (!References(view.get(), copyResources[0]) && !References(view.get(), copyResources[1]))
					continue;
				if (std::ranges::any_of(std::span(savedInputs.data(), savedInputCount), [&](const SavedInput& saved) {
					return static_cast<std::size_t>(saved.stage) == stage && saved.slot == slot;
				}))
					continue;
				ID3D11ShaderResourceView* nullView{};
				(context.get()->*setters[stage])(slot, 1, &nullView);
				savedInputs[savedInputCount++] = { static_cast<Stage>(stage), slot, std::move(view) };
				++conflictCount;
			}
		}
	}

	void ScopedD3D11BindingState::Restore() noexcept
	{
		if (!context)
			return;
		using Setter = void (ID3D11DeviceContext::*)(UINT, UINT, ID3D11ShaderResourceView* const*);
		constexpr std::array<Setter, 6> setters{ &ID3D11DeviceContext::VSSetShaderResources,
			&ID3D11DeviceContext::HSSetShaderResources, &ID3D11DeviceContext::DSSetShaderResources,
			&ID3D11DeviceContext::GSSetShaderResources, &ID3D11DeviceContext::PSSetShaderResources,
			&ID3D11DeviceContext::CSSetShaderResources };
		if (outputMergerCaptured) {
			std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> raw{};
			for (std::size_t i = 0; i < raw.size(); ++i)
				raw[i] = savedRTVs[i].get();
			context->OMSetRenderTargets(static_cast<UINT>(raw.size()), raw.data(), savedDSV.get());
		}
		if (computeOutputsCaptured) {
			for (UINT i = 0; i < savedComputeUAVs.size(); ++i) {
				auto* view = savedComputeUAVs[i].get();
				if (changedComputeUAVs[i])
					context->CSSetUnorderedAccessViews(i, 1, &view, nullptr);
			}
		}
		if (pixelOutputsCaptured) {
			for (UINT i = 0; i < savedPixelUAVs.size(); ++i) {
				if (changedPixelUAVs[i]) {
					auto* view = savedPixelUAVs[i].get();
					context->OMSetRenderTargetsAndUnorderedAccessViews(
						D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL, nullptr, nullptr, i, 1, &view, nullptr);
				}
			}
		}
		for (const auto& saved : std::span(savedInputs.data(), savedInputCount)) {
			auto* view = saved.view.get();
			(context.get()->*setters[static_cast<std::size_t>(saved.stage)])(saved.slot, 1, &view);
		}
		context = nullptr;
	}
}
