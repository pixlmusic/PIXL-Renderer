// PIXL Renderer - shared opaque-surface temporal validity guide.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TemporalValidityGPU.h"
#include "TemporalContext.h"
#include "PixelAnnotations.h"

#include "Utils/D3D.h"

#include <array>
#include <stdexcept>

namespace PIXL::Renderer
{
	namespace
	{
		struct alignas(16) ValidityConstants
		{
			std::uint32_t width{};
			std::uint32_t height{};
			std::uint32_t historyValid{};
			std::uint32_t annotationsEnabled{};
			std::uint32_t activeX{};
			std::uint32_t activeY{};
			std::uint32_t unused2{};
			std::uint32_t unused3{};
		};
		static_assert(sizeof(ValidityConstants) == 32);
	}

	TemporalValidityGPU& TemporalValidityGPU::Get()
	{
		static TemporalValidityGPU instance;
		return instance;
	}

	void TemporalValidityGPU::Invalidate() noexcept
	{
		packedHistory = {};
		packedSRV = {};
		packedUAV = {};
		confidenceTexture = nullptr;
		confidenceSRV = nullptr;
		confidenceUAV = nullptr;
		annotationTexture = nullptr;
		annotationSRV = nullptr;
		annotationUAV = nullptr;
		shader = nullptr;
		constants = nullptr;
		owningDevice = nullptr;
		width = height = 0;
		lastFrame = FrameToken::kInvalidFrame;
		lastEpoch = {};
		activeX = activeY = 0;
		creationFailed = false;
		lastLoggedAvailable = false;
		lastLoggedAnnotations = false;
	}

	bool TemporalValidityGPU::EnsureResources(ID3D11Device* device, std::uint32_t nextWidth, std::uint32_t nextHeight)
	{
		if (owningDevice.get() != device || width != nextWidth || height != nextHeight)
			Invalidate();
		if (creationFailed)
			return false;
		if (shader && confidenceUAV)
			return true;
		if (!device || !nextWidth || !nextHeight)
			return false;
		owningDevice.copy_from(device);
		width = nextWidth;
		height = nextHeight;
		try {
			shader.attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(
				L"Data\\Shaders\\TemporalValidityCS.hlsl", {}, "cs_5_0")));
			if (!shader)
				throw std::runtime_error("temporal validity shader unavailable");
			D3D11_BUFFER_DESC cb{};
			cb.ByteWidth = sizeof(ValidityConstants);
			cb.Usage = D3D11_USAGE_DYNAMIC;
			cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			if (FAILED(device->CreateBuffer(&cb, nullptr, constants.put())))
				throw std::runtime_error("temporal validity constants unavailable");
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = width;
			desc.Height = height;
			desc.MipLevels = 1;
			desc.ArraySize = 1;
			desc.SampleDesc.Count = 1;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			desc.Format = DXGI_FORMAT_R32_UINT;
			for (std::size_t i = 0; i < packedHistory.size(); ++i) {
				if (FAILED(device->CreateTexture2D(&desc, nullptr, packedHistory[i].put())) ||
					FAILED(device->CreateShaderResourceView(packedHistory[i].get(), nullptr, packedSRV[i].put())) ||
					FAILED(device->CreateUnorderedAccessView(packedHistory[i].get(), nullptr, packedUAV[i].put())))
					throw std::runtime_error("temporal validity history unavailable");
			}
			desc.Format = DXGI_FORMAT_R8G8_UNORM;
			if (FAILED(device->CreateTexture2D(&desc, nullptr, confidenceTexture.put())) ||
				FAILED(device->CreateShaderResourceView(confidenceTexture.get(), nullptr, confidenceSRV.put())) ||
				FAILED(device->CreateUnorderedAccessView(confidenceTexture.get(), nullptr, confidenceUAV.put())))
				throw std::runtime_error("temporal validity output unavailable");
			// R16_UINT is not consistently exposed as a typed UAV on Skyrim's
			// supported DX11 adapters. Use the broadly supported 32-bit integer
			// path first, with a typeless-resource fallback for drivers that reject
			// a typed resource but accept typed R32_UINT views.
			const auto tryCreateAnnotation = [&](DXGI_FORMAT resourceFormat) {
				D3D11_TEXTURE2D_DESC annotationDesc = desc;
				annotationDesc.Format = resourceFormat;
				D3D11_SHADER_RESOURCE_VIEW_DESC annotationSrvDesc{};
				annotationSrvDesc.Format = DXGI_FORMAT_R32_UINT;
				annotationSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
				annotationSrvDesc.Texture2D.MostDetailedMip = 0;
				annotationSrvDesc.Texture2D.MipLevels = 1;
				D3D11_UNORDERED_ACCESS_VIEW_DESC annotationUavDesc{};
				annotationUavDesc.Format = DXGI_FORMAT_R32_UINT;
				annotationUavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
				annotationUavDesc.Texture2D.MipSlice = 0;
				if (FAILED(device->CreateTexture2D(&annotationDesc, nullptr, annotationTexture.put())) ||
					FAILED(device->CreateShaderResourceView(annotationTexture.get(), &annotationSrvDesc, annotationSRV.put())) ||
					FAILED(device->CreateUnorderedAccessView(annotationTexture.get(), &annotationUavDesc, annotationUAV.put()))) {
					annotationTexture = nullptr;
					annotationSRV = nullptr;
					annotationUAV = nullptr;
					return false;
				}
				return true;
			};
			if (!tryCreateAnnotation(DXGI_FORMAT_R32_UINT))
				tryCreateAnnotation(DXGI_FORMAT_R32_TYPELESS);
			if (!annotationUAV)
				logger::warn("[PIXL Annotations] Compact typed UAV unavailable; CPU classification remains active");
			else
				logger::info("[PIXL Annotations] Compact R32_UINT GPU annotation UAV ready");
			return true;
		} catch (const std::exception& e) {
			logger::warn("[PIXL Temporal] GPU validity disabled until resources change: {}", e.what());
			creationFailed = true;
			return false;
		}
	}

	bool TemporalValidityGPU::Resolve(ID3D11Device* device, ID3D11DeviceContext* context,
		const FrameToken& token, const RenderExtent& extent,
		ID3D11ShaderResourceView* depth, ID3D11ShaderResourceView* normalRoughness,
		ID3D11ShaderResourceView* motion, ID3D11ShaderResourceView* materialMask,
		ID3D11ShaderResourceView* normalWaterMask, ID3D11ShaderResourceView* taaMask,
		ID3D11Buffer* sharedData, ID3D11Buffer* frameData,
		bool historyValid)
	{
		if (!token.Valid() || token.view != ViewType::MainWorld || !extent.Valid() || !context ||
			!depth || !normalRoughness || !motion || !sharedData || !frameData)
			return false;
		if (creationFailed && attemptedEpoch != token.resources)
			Invalidate();
		const bool resourcesReady = EnsureResources(device, extent.active.width, extent.active.height);
		attemptedEpoch = token.resources;
		if (!resourcesReady)
			return false;
		const bool available = IsAvailable();
		const bool annotationsAvailable = AnnotationsAvailable();
		if (available != lastLoggedAvailable || annotationsAvailable != lastLoggedAnnotations) {
			logger::info("[PIXL TemporalGPU] availability changed: TemporalValidity={} Annotations={}",
				available ? "Active" : "Unavailable", annotationsAvailable ? "Active" : "Unavailable");
			lastLoggedAvailable = available;
			lastLoggedAnnotations = annotationsAvailable;
		}
		if (activeX != extent.active.x || activeY != extent.active.y) {
			lastFrame = FrameToken::kInvalidFrame;
			activeX = extent.active.x;
			activeY = extent.active.y;
		}
		if (lastFrame == token.frame && lastEpoch == token.resources)
			return true;
		const UINT currentIndex = static_cast<UINT>(token.frame & 1u);
		const UINT previousIndex = currentIndex ^ 1u;
		const bool usableHistory = historyValid && lastFrame != FrameToken::kInvalidFrame &&
			lastFrame + 1 == token.frame && lastEpoch == token.resources;
		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (FAILED(context->Map(constants.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			return false;
		const bool annotationsEnabled = annotationUAV && materialMask && normalWaterMask && taaMask;
		*static_cast<ValidityConstants*>(mapped.pData) = {
			width, height, usableHistory ? 1u : 0u, annotationsEnabled ? 1u : 0u, activeX, activeY, 0u, 0u };
		context->Unmap(constants.get(), 0);

		std::array<ID3D11ShaderResourceView*, 7> views{ depth, normalRoughness, motion,
			usableHistory ? packedSRV[previousIndex].get() : nullptr,
			annotationsEnabled ? materialMask : nullptr,
			annotationsEnabled ? normalWaterMask : nullptr,
			annotationsEnabled ? taaMask : nullptr };
		std::array<ID3D11UnorderedAccessView*, 3> outputs{ packedUAV[currentIndex].get(), confidenceUAV.get(),
			annotationsEnabled ? annotationUAV.get() : nullptr };
		std::array<ID3D11ShaderResourceView*, 7> savedViews{};
		std::array<ID3D11UnorderedAccessView*, 3> savedOutputs{};
		std::array<ID3D11Buffer*, 3> savedBuffers{};
		ID3D11ComputeShader* savedShader{};
		context->CSGetShaderResources(0, static_cast<UINT>(savedViews.size()), savedViews.data());
		context->CSGetUnorderedAccessViews(0, static_cast<UINT>(savedOutputs.size()), savedOutputs.data());
		context->CSGetConstantBuffers(0, 1, &savedBuffers[0]);
		context->CSGetConstantBuffers(5, 1, &savedBuffers[1]);
		context->CSGetConstantBuffers(12, 1, &savedBuffers[2]);
		context->CSGetShader(&savedShader, nullptr, nullptr);
		ID3D11Buffer* cb0 = constants.get();
		context->CSSetShaderResources(0, static_cast<UINT>(views.size()), views.data());
		context->CSSetUnorderedAccessViews(0, static_cast<UINT>(outputs.size()), outputs.data(), nullptr);
		context->CSSetConstantBuffers(0, 1, &cb0);
		context->CSSetConstantBuffers(5, 1, &sharedData);
		context->CSSetConstantBuffers(12, 1, &frameData);
		context->CSSetShader(shader.get(), nullptr, 0);
		context->Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1);
		views.fill(nullptr);
		outputs.fill(nullptr);
		context->CSSetShaderResources(0, static_cast<UINT>(views.size()), views.data());
		context->CSSetUnorderedAccessViews(0, static_cast<UINT>(outputs.size()), outputs.data(), nullptr);
		context->CSSetShaderResources(0, static_cast<UINT>(savedViews.size()), savedViews.data());
		context->CSSetUnorderedAccessViews(0, static_cast<UINT>(savedOutputs.size()), savedOutputs.data(), nullptr);
		context->CSSetConstantBuffers(0, 1, &savedBuffers[0]);
		context->CSSetConstantBuffers(5, 1, &savedBuffers[1]);
		context->CSSetConstantBuffers(12, 1, &savedBuffers[2]);
		context->CSSetShader(savedShader, nullptr, 0);
		for (auto* view : savedViews) if (view) view->Release();
		for (auto* output : savedOutputs) if (output) output->Release();
		for (auto* buffer : savedBuffers) if (buffer) buffer->Release();
		if (savedShader) savedShader->Release();
		lastFrame = token.frame;
		lastEpoch = token.resources;
		TemporalContext::Get().PublishGPUConfidence(token, extent, confidenceSRV.get());
		if (annotationsEnabled)
			PixelAnnotations::Get().PublishCompact(token, extent, annotationSRV.get());
		return true;
	}
}
