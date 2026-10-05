// PIXL Renderer - typed current-frame render graph contracts.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <limits>

#include <d3d11.h>
#include <winrt/base.h>

namespace PIXL::Renderer
{
	enum class ViewType : std::uint8_t
	{
		MainWorld,
		Reflection,
		Cubemap,
		Shadow,
		Auxiliary
	};

	enum class ResolutionDomain : std::uint8_t
	{
		Unknown,
		Backing,
		ActiveRender,
		Output,
		Half,
		Quarter
	};

	enum class CoordinateSpace : std::uint8_t
	{
		Unknown,
		CameraRelativeWorld,
		AbsoluteWorld,
		View,
		Clip,
		Screen
	};

	enum class DepthEpoch : std::uint8_t
	{
		None,
		LiveMain,
		OpaqueFinal,
		PreWater,
		Effects,
		Reconstruction,
		Reflection,
		Shadow
	};

	struct ResourceEpoch
	{
		std::uint64_t value{};

		friend constexpr bool operator==(ResourceEpoch, ResourceEpoch) noexcept = default;
	};

	struct FrameToken
	{
		static constexpr std::uint64_t kInvalidFrame = std::numeric_limits<std::uint64_t>::max();

		std::uint64_t frame{ kInvalidFrame };
		ResourceEpoch resources{};
		ViewType view{ ViewType::Auxiliary };
		std::uint32_t viewSerial{};

		constexpr bool Valid() const noexcept { return frame != kInvalidFrame && resources.value != 0; }
		constexpr bool SameFrame(const FrameToken& other) const noexcept
		{
			return Valid() && other.Valid() && frame == other.frame && resources == other.resources;
		}
		constexpr bool Matches(const FrameToken& other) const noexcept
		{
			return SameFrame(other) && view == other.view && viewSerial == other.viewSerial;
		}

		friend constexpr bool operator==(const FrameToken&, const FrameToken&) noexcept = default;
	};

	struct RenderRect
	{
		std::uint32_t x{};
		std::uint32_t y{};
		std::uint32_t width{};
		std::uint32_t height{};

		constexpr bool Valid() const noexcept { return width != 0 && height != 0; }
	};

	struct RenderExtent
	{
		std::uint32_t backingWidth{};
		std::uint32_t backingHeight{};
		RenderRect active{};
		std::uint32_t outputWidth{};
		std::uint32_t outputHeight{};
		ResolutionDomain domain{ ResolutionDomain::ActiveRender };

		constexpr bool Valid() const noexcept
		{
			return backingWidth != 0 && backingHeight != 0 && active.Valid() &&
			       active.x <= backingWidth && active.y <= backingHeight &&
			       active.width <= backingWidth - active.x && active.height <= backingHeight - active.y;
		}

		constexpr bool SameAllocation(const RenderExtent& other) const noexcept
		{
			return backingWidth == other.backingWidth && backingHeight == other.backingHeight;
		}

		constexpr bool SameActiveRegion(const RenderExtent& other) const noexcept
		{
			return active.x == other.active.x && active.y == other.active.y &&
			       active.width == other.active.width && active.height == other.active.height;
		}
	};

	struct ViewContext
	{
		FrameToken token{};
		RenderExtent extent{};
		ViewType type{ ViewType::Auxiliary };
		bool advancesMainTemporal{};

		constexpr bool Valid() const noexcept
		{
			return token.Valid() && token.view == type && extent.Valid() &&
			       (type == ViewType::MainWorld || !advancesMainTemporal);
		}
	};

	using ViewMask = std::uint32_t;
	inline constexpr ViewMask ViewBit(ViewType view) noexcept
	{
		return 1u << static_cast<std::uint32_t>(view);
	}
	inline constexpr ViewMask AllViews = ViewBit(ViewType::MainWorld) | ViewBit(ViewType::Reflection) |
		ViewBit(ViewType::Cubemap) | ViewBit(ViewType::Shadow) | ViewBit(ViewType::Auxiliary);

	struct DepthView
	{
		winrt::com_ptr<ID3D11ShaderResourceView> srv;
		FrameToken token{};
		RenderExtent extent{};
		DepthEpoch epoch{ DepthEpoch::None };
		CoordinateSpace space{ CoordinateSpace::Screen };
		DXGI_FORMAT format{ DXGI_FORMAT_UNKNOWN };

		bool ValidFor(const FrameToken& expected, DepthEpoch required = DepthEpoch::None) const noexcept
		{
			return srv && token.Matches(expected) && extent.Valid() && epoch != DepthEpoch::None &&
			       (required == DepthEpoch::None || epoch == required);
		}
	};
}
