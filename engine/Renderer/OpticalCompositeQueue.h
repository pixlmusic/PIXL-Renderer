// PIXL Renderer - bounded same-frame optical replay admission.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "FrameGraphTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace RE { class BSRenderPass; }

namespace PIXL::Renderer
{
	enum class OpticalRequirement : std::uint32_t
	{
		SceneColor = 1u << 0,
		OpaqueDepth = 1u << 1,
		RefractionNormals = 1u << 2,
		Reactive = 1u << 3,
		Transparency = 1u << 4,
		InternalVolume = 1u << 5,
		GlassShell = 1u << 6,
		AuthoredDecals = 1u << 7,
		WindowInterior = 1u << 8,
		ContainedLiquid = 1u << 9
	};

	using OpticalRequirements = std::uint32_t;
	constexpr OpticalRequirements OpticalBit(OpticalRequirement value) noexcept
	{
		return static_cast<OpticalRequirements>(value);
	}

	struct OpticalReplayCommand
	{
		using Callback = void (*)(void*, RE::BSRenderPass*, std::uint32_t, bool, std::uint32_t);
		FrameToken token{};
		RE::BSRenderPass* pass{};
		const void* geometryKey{};
		void* owner{};
		Callback callback{};
		std::uint32_t technique{};
		std::uint32_t flags{};
		OpticalRequirements requirements{};
		bool alphaTest{};
	};

	class OpticalCompositeQueue
	{
	public:
		static constexpr std::size_t kCapacity = 32;
		static OpticalCompositeQueue& Get();
		void BeginFrame(std::uint64_t frame) noexcept;
		void Invalidate() noexcept;
		void DropOwner(void* owner) noexcept;
		bool Enqueue(const OpticalReplayCommand& command) noexcept;
		std::size_t ReplayOwner(void* owner, const FrameToken& token) noexcept;
		std::size_t PendingCount() const noexcept { return count; }

	private:
		std::array<OpticalReplayCommand, kCapacity> commands{};
		std::size_t count{};
		std::uint64_t frame{ FrameToken::kInvalidFrame };
	};
}
