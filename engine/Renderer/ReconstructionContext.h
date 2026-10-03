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
	using ReactiveContributor = std::function<void(ID3D11UnorderedAccessView*, std::uint32_t, std::uint32_t)>;

	struct ReconstructionFrame
	{
		winrt::com_ptr<ID3D11ShaderResourceView> depth, motion, reactive, transparency, exposure;
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

	class ReconstructionContext
	{
	public:
		static ReconstructionContext& Get();
		std::uint64_t RegisterReactiveContributor(std::string name, ReactiveContributor callback);
		void UnregisterReactiveContributor(std::uint64_t id);
		void ApplyReactiveContributors(ID3D11UnorderedAccessView* target, std::uint32_t width, std::uint32_t height) const;
		void Publish(ReconstructionFrame frame);
		[[nodiscard]] ReconstructionFrame Acquire() const;
		[[nodiscard]] ReconstructionDiagnostics GetDiagnostics() const;
		static std::string_view ToString(ReconstructionBackend backend) noexcept;
	private:
		struct Contributor { std::uint64_t id{}; std::string name; ReactiveContributor callback; };
		mutable std::mutex mutex;
		ReconstructionFrame frame;
		std::vector<Contributor> contributors;
		std::uint64_t nextId{ 1 };
	};
}
