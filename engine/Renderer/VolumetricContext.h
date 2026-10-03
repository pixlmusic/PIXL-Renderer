// PIXL Renderer - shared publication for atmosphere froxel resources.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <mutex>

#include <d3d11.h>
#include <winrt/base.h>

namespace PIXL::Renderer
{
	enum class VolumetricLightingFlags : std::uint32_t
	{
		None = 0,
		DirectionalShadow = 1u << 0,
		SceneDepth = 1u << 1,
		ImageBasedLighting = 1u << 2,
		SkyBounce = 1u << 3,
		LocalLights = 1u << 4
	};

	struct VolumetricFrame
	{
		winrt::com_ptr<ID3D11ShaderResourceView> materialExtinction;
		winrt::com_ptr<ID3D11ShaderResourceView> scattering;
		winrt::com_ptr<ID3D11ShaderResourceView> integratedScattering;
		winrt::com_ptr<ID3D11ShaderResourceView> conservativeDepth;
		std::uint32_t width{};
		std::uint32_t height{};
		std::uint32_t depth{};
		std::uint32_t lightingFlags{};
		std::uint64_t frame{};
		std::uint64_t memoryBytes{};
		bool temporalValid{};
		bool valid{};
	};

	class VolumetricContext
	{
	public:
		static VolumetricContext& Get();

		void Publish(VolumetricFrame frame);
		void Invalidate();
		[[nodiscard]] VolumetricFrame Acquire() const;

	private:
		mutable std::mutex mutex;
		VolumetricFrame frame;
	};
}
