// PIXL Renderer - compact renderer-owned material and temporal annotations.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "FrameGraphTypes.h"

#include <cstdint>
#include <mutex>
#include <string_view>

#include <d3d11.h>
#include <winrt/base.h>

namespace PIXL::Renderer
{
	enum class MaterialClass : std::uint8_t
	{
		Unknown, Terrain, Rock, Wood, Metal, Grass, Foliage, Skin, Hair, Cloth,
		Glass, ContainedLiquid, Water, Snow, Mud, Emissive, Particle, Sky, WindowInterior
	};
	static_assert(static_cast<std::uint8_t>(MaterialClass::Terrain) == 1);
	static_assert(static_cast<std::uint8_t>(MaterialClass::Skin) == 7);
	static_assert(static_cast<std::uint8_t>(MaterialClass::Water) == 12);
	static_assert(static_cast<std::uint8_t>(MaterialClass::Sky) == 17);

	enum class AnnotationFlag : std::uint16_t
	{
		None = 0,
		Transparent = 1u << 0,
		Reactive = 1u << 1,
		FastTemporal = 1u << 2,
		ThinSurface = 1u << 3,
		Deformable = 1u << 4,
		Emissive = 1u << 5
	};

	struct MaterialAnnotation
	{
		MaterialClass material{ MaterialClass::Unknown };
		std::uint16_t flags{};
		std::uint16_t materialId{};
		float transparencyConfidence{ 1.0f };
		float reactiveStrength{};
		float temporalResponsiveness{};
	};

	struct MaterialClassifyInput
	{
		bool terrain{}, rock{}, wood{}, metal{}, grass{}, foliage{}, skin{}, hair{}, cloth{};
		bool glass{}, containedLiquid{}, water{}, snow{}, mud{}, emissive{}, particle{}, sky{}, windowInterior{};
		bool transparent{}, deformable{}, thinSurface{};
		std::uint16_t materialId{};
	};

	struct FrameAnnotationViews
	{
		winrt::com_ptr<ID3D11ShaderResourceView> compactClassFlags;
		FrameToken token{};
		RenderExtent extent{};
		winrt::com_ptr<ID3D11ShaderResourceView> deferredMaterialMask;
		winrt::com_ptr<ID3D11ShaderResourceView> normalWaterMask;
		winrt::com_ptr<ID3D11ShaderResourceView> temporalAAMask;
		winrt::com_ptr<ID3D11ShaderResourceView> reactiveMask;
		winrt::com_ptr<ID3D11ShaderResourceView> transparencyMask;
		std::uint32_t width{};
		std::uint32_t height{};
		std::uint64_t frame{};
		bool valid{};
	};

	class PixelAnnotations
	{
	public:
		static PixelAnnotations& Get();
		static MaterialAnnotation Classify(const MaterialClassifyInput& input) noexcept;
		static std::string_view ToString(MaterialClass material) noexcept;

		void BeginFrame(std::uint64_t frame);
		void BeginFrame(const FrameToken& token, const RenderExtent& extent);
		void Invalidate();
		void PublishBase(ID3D11ShaderResourceView* materialMask, ID3D11ShaderResourceView* normalWaterMask,
			ID3D11ShaderResourceView* temporalAAMask, std::uint32_t width, std::uint32_t height);
		void PublishReconstruction(ID3D11ShaderResourceView* reactiveMask,
			ID3D11ShaderResourceView* transparencyMask, std::uint32_t width, std::uint32_t height);
		void PublishCompact(const FrameToken& token, const RenderExtent& extent,
			ID3D11ShaderResourceView* compactClassFlags);
		[[nodiscard]] FrameAnnotationViews Acquire() const;

	private:
		mutable std::mutex mutex;
		FrameAnnotationViews views;
	};
}
