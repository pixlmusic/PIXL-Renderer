// PIXL Renderer - compact renderer-owned material and temporal annotations.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PixelAnnotations.h"

#include <algorithm>

namespace PIXL::Renderer
{
	PixelAnnotations& PixelAnnotations::Get()
	{
		static PixelAnnotations instance;
		return instance;
	}

	MaterialAnnotation PixelAnnotations::Classify(const MaterialClassifyInput& in) noexcept
	{
		MaterialAnnotation out{};
		// Specific authored PIXL domains win over broad shader-family hints.
		if (in.containedLiquid) out.material = MaterialClass::ContainedLiquid;
		else if (in.windowInterior) out.material = MaterialClass::WindowInterior;
		else if (in.water) out.material = MaterialClass::Water;
		else if (in.sky) out.material = MaterialClass::Sky;
		else if (in.skin) out.material = MaterialClass::Skin;
		else if (in.hair) out.material = MaterialClass::Hair;
		else if (in.grass) out.material = MaterialClass::Grass;
		else if (in.foliage) out.material = MaterialClass::Foliage;
		else if (in.snow) out.material = MaterialClass::Snow;
		else if (in.mud) out.material = MaterialClass::Mud;
		else if (in.terrain) out.material = MaterialClass::Terrain;
		else if (in.glass) out.material = MaterialClass::Glass;
		else if (in.particle) out.material = MaterialClass::Particle;
		else if (in.metal) out.material = MaterialClass::Metal;
		else if (in.wood) out.material = MaterialClass::Wood;
		else if (in.rock) out.material = MaterialClass::Rock;
		else if (in.cloth) out.material = MaterialClass::Cloth;
		else if (in.emissive) out.material = MaterialClass::Emissive;

		out.materialId = in.materialId;
		if (in.transparent || in.glass || in.water || in.containedLiquid || in.particle)
			out.flags |= static_cast<std::uint16_t>(AnnotationFlag::Transparent);
		if (in.deformable || in.grass || in.foliage || in.water || in.particle)
			out.flags |= static_cast<std::uint16_t>(AnnotationFlag::Reactive);
		if (in.grass || in.foliage || in.hair || in.particle || in.water)
			out.flags |= static_cast<std::uint16_t>(AnnotationFlag::FastTemporal);
		if (in.thinSurface || in.grass || in.foliage || in.hair || in.cloth)
			out.flags |= static_cast<std::uint16_t>(AnnotationFlag::ThinSurface);
		if (in.deformable)
			out.flags |= static_cast<std::uint16_t>(AnnotationFlag::Deformable);
		if (in.emissive)
			out.flags |= static_cast<std::uint16_t>(AnnotationFlag::Emissive);
		out.transparencyConfidence = in.transparent ? 0.25f : (in.glass || in.water ? 0.45f : 1.0f);
		out.reactiveStrength = (out.flags & static_cast<std::uint16_t>(AnnotationFlag::Reactive)) ? 1.0f : 0.0f;
		out.temporalResponsiveness = (out.flags & static_cast<std::uint16_t>(AnnotationFlag::FastTemporal)) ? 0.75f : 0.15f;
		return out;
	}

	void PixelAnnotations::BeginFrame(std::uint64_t frame)
	{
		std::scoped_lock lock(mutex);
		if (views.frame == frame)
			return;
		views = {};
		views.frame = frame;
	}

	void PixelAnnotations::PublishBase(ID3D11ShaderResourceView* material, ID3D11ShaderResourceView* normalWater,
		ID3D11ShaderResourceView* taa, std::uint32_t width, std::uint32_t height)
	{
		std::scoped_lock lock(mutex);
		views.deferredMaterialMask.copy_from(material);
		views.normalWaterMask.copy_from(normalWater);
		views.temporalAAMask.copy_from(taa);
		views.width = width;
		views.height = height;
		views.valid = material && normalWater && taa && width && height;
	}

	void PixelAnnotations::PublishReconstruction(ID3D11ShaderResourceView* reactive,
		ID3D11ShaderResourceView* transparency, std::uint32_t width, std::uint32_t height)
	{
		std::scoped_lock lock(mutex);
		views.reactiveMask.copy_from(reactive);
		views.transparencyMask.copy_from(transparency);
		views.width = width;
		views.height = height;
	}

	FrameAnnotationViews PixelAnnotations::Acquire() const
	{
		std::scoped_lock lock(mutex);
		return views;
	}

	std::string_view PixelAnnotations::ToString(MaterialClass material) noexcept
	{
		static constexpr std::string_view names[] = { "Unknown", "Terrain", "Rock", "Wood", "Metal", "Grass",
			"Foliage", "Skin", "Hair", "Cloth", "Glass", "Contained Liquid", "Water", "Snow", "Mud",
			"Emissive", "Particle", "Sky", "WindowLife" };
		const auto index = static_cast<std::size_t>(material);
		return index < std::size(names) ? names[index] : names[0];
	}
}
