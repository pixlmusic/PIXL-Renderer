// PIXL Renderer - shared reconstruction inputs and reactive contributors.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "PixelAnnotations.h"
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include <d3d11.h>
#include <winrt/base.h>

namespace PIXL::Renderer
{
	enum class ReconstructionBackend : std::uint8_t { Native, TAA, DLSS, FSR, Neural };
	enum class ReactiveContributionKind : std::uint8_t
	{
		Other, Water, Refraction, WindowInterior, Magic, Fire, Particles, Precipitation, Foliage, ContainedLiquid
	};
	using ReactiveContributor = std::function<void(ID3D11UnorderedAccessView*, std::uint32_t, std::uint32_t)>;

	struct ReconstructionFrame
	{
		winrt::com_ptr<ID3D11ShaderResourceView> depth, motion, reactive, transparency, exposure;
		winrt::com_ptr<ID3D11ShaderResourceView> confidenceDisocclusion;
		FrameToken token{};
		RenderExtent extent{};
		FrameAnnotationViews annotations;
		ReconstructionBackend backend{ ReconstructionBackend::Native };
		std::uint32_t renderWidth{}, renderHeight{}, outputWidth{}, outputHeight{};
		std::uint64_t frame{};
		bool temporalValid{};
	};

	struct ReconstructionDiagnostics
	{
		ReconstructionBackend backend{ ReconstructionBackend::Native };
		std::uint32_t renderWidth{}, renderHeight{}, outputWidth{}, outputHeight{};
		std::size_t contributors{};
		bool valid{}, depth{}, motion{}, annotations{}, reactive{}, transparency{}, temporalValid{};
	};
	struct ContributorDiagnostics
	{
		std::string name;
		ReactiveContributionKind kind{ ReactiveContributionKind::Other };
	};

	class ReconstructionContext
	{
	public:
		static ReconstructionContext& Get();
		// Clears the published frame at the start of a new render frame while
		// preserving the setup-time contributor registry.
		void BeginFrame(std::uint64_t frame);
		void BeginFrame(const FrameToken& token, const RenderExtent& extent);
		void Invalidate();
		std::uint64_t RegisterReactiveContributor(std::string name, ReactiveContributor callback);
		std::uint64_t RegisterReactiveContributor(std::string name, ReactiveContributionKind kind,
			ReactiveContributor callback);
		void UnregisterReactiveContributor(std::uint64_t id);
		void ApplyReactiveContributors(ID3D11UnorderedAccessView* target, std::uint32_t width, std::uint32_t height) const;
		void ApplyReactiveContributors(const FrameToken& token, const RenderExtent& extent,
			ID3D11UnorderedAccessView* target, std::uint32_t width, std::uint32_t height) const;
		void Publish(ReconstructionFrame frame);
		[[nodiscard]] ReconstructionFrame Acquire() const;
		[[nodiscard]] ReconstructionFrame Acquire(const FrameToken& token) const;
		[[nodiscard]] ReconstructionDiagnostics GetDiagnostics() const;
		[[nodiscard]] std::vector<ContributorDiagnostics> GetContributorDiagnostics() const;
		static std::string_view ToString(ReconstructionBackend backend) noexcept;
	private:
		struct Contributor { std::uint64_t id{}; std::string name; ReactiveContributionKind kind{}; ReactiveContributor callback; };
		mutable std::mutex mutex;
		ReconstructionFrame frame;
		FrameToken expectedToken{};
		RenderExtent expectedExtent{};
		std::vector<Contributor> contributors;
		std::uint64_t nextId{ 1 };
	};
}
