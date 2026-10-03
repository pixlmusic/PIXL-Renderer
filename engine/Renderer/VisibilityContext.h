// PIXL Renderer - shared conservative hierarchical-depth visibility service.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Modules/FoliageOptimizer/HiZPyramid.h"

#include <cstdint>
#include <string>

namespace PIXL::Renderer
{
	enum class DepthConvention : std::uint8_t
	{
		StandardZeroNearOneFar
	};

	struct VisibilityDiagnostics
	{
		bool initialized = false;
		bool valid = false;
		bool builtThisFrame = false;
		std::uint32_t width = 0;
		std::uint32_t height = 0;
		std::uint32_t mipCount = 0;
		float texelPixels = 0.0f;
		std::uint64_t frameIndex = 0;
		std::uint64_t buildCount = 0;
		std::string invalidationReason;
	};

	class VisibilityContext
	{
	public:
		static VisibilityContext& Get();

		void SetupResources();
		void ClearShaderCache();
		bool Build(ID3D11Device* a_device, ID3D11DeviceContext* a_context, std::uint64_t a_frameIndex);
		void Invalidate(std::string_view a_reason = "explicit invalidation");

		[[nodiscard]] ID3D11ShaderResourceView* GetDepthPyramidSRV() const { return depthPyramid.GetSRV(); }
		[[nodiscard]] bool IsValid() const { return depthPyramid.IsValid(); }
		[[nodiscard]] std::uint32_t GetWidth() const { return depthPyramid.GetWidth(); }
		[[nodiscard]] std::uint32_t GetHeight() const { return depthPyramid.GetHeight(); }
		[[nodiscard]] std::uint32_t GetMipCount() const { return depthPyramid.GetMipCount(); }
		[[nodiscard]] float GetTexelPixels() const { return depthPyramid.GetTexelPixels(); }
		[[nodiscard]] DepthConvention GetDepthConvention() const { return DepthConvention::StandardZeroNearOneFar; }
		[[nodiscard]] VisibilityDiagnostics GetDiagnostics() const;

	private:
		HiZPyramid depthPyramid;
		bool initialized = false;
		std::uint64_t builtFrame = UINT64_MAX;
		std::uint64_t buildCount = 0;
		std::string invalidationReason;
	};
}
