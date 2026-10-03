// PIXL Renderer - shared frame-scoped spatial-lighting publication service.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

#include <d3d11.h>
#include <winrt/base.h>

namespace PIXL::Renderer
{
	enum class ProbeKind : std::uint8_t
	{
		AmbientEnvironmentSH,
		AmbientSkySH,
		SkyVisibility,
		WorldIrradiance,
		Count
	};

	struct LocalLightingView
	{
		winrt::com_ptr<ID3D11ShaderResourceView> lights;
		winrt::com_ptr<ID3D11ShaderResourceView> lightIndices;
		winrt::com_ptr<ID3D11ShaderResourceView> lightGrid;
		std::uint32_t emitterStart{};
		std::uint32_t emitterCount{};
		std::uint32_t totalLightCount{};
		std::uint64_t frame{};
		std::uint64_t epoch{};

		[[nodiscard]] bool ValidFor(std::uint64_t expectedFrame) const noexcept
		{
			return frame == expectedFrame && lights && lightIndices && lightGrid;
		}
	};

	struct ProbeView
	{
		winrt::com_ptr<ID3D11ShaderResourceView> resource;
		std::uint32_t width{};
		std::uint32_t height{};
		std::uint32_t depth{};
		std::uint64_t frame{};
		std::uint64_t epoch{};
	};

	struct LightTransportDiagnostics
	{
		std::uint64_t frame{};
		std::uint64_t epoch{};
		std::uint32_t localLightCount{};
		std::uint32_t localEmitterCount{};
		std::uint32_t validProbeCount{};
		std::string invalidationReason;
	};

	// Frame-scoped rendezvous for compatible light-transport data. This service
	// does not merge lighting algorithms or own producer resources; COM references
	// keep published SRVs alive only until the next frame/reset.
	class LightTransportWorld
	{
	public:
		static LightTransportWorld& Get();

		void BeginFrame(std::uint64_t frame);
		void Invalidate(std::string_view reason);

		void PublishLocalLights(
			ID3D11ShaderResourceView* lights,
			ID3D11ShaderResourceView* lightIndices,
			ID3D11ShaderResourceView* lightGrid,
			std::uint32_t emitterStart,
			std::uint32_t emitterCount,
			std::uint32_t totalLightCount);
		[[nodiscard]] LocalLightingView AcquireLocalLights() const;

		void PublishProbe(
			ProbeKind kind,
			ID3D11ShaderResourceView* resource,
			std::uint32_t width,
			std::uint32_t height,
			std::uint32_t depth = 1);
		[[nodiscard]] ProbeView AcquireProbe(ProbeKind kind) const;
		[[nodiscard]] LightTransportDiagnostics GetDiagnostics() const;

		static std::string_view ToString(ProbeKind kind);

	private:
		mutable std::mutex mutex;
		LocalLightingView localLights;
		std::array<ProbeView, static_cast<std::size_t>(ProbeKind::Count)> probes;
		std::string invalidationReason{ "not initialized" };
		std::uint64_t frame{};
		std::uint64_t epoch{};
	};
}
