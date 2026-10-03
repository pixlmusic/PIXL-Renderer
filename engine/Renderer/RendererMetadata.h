// PIXL Renderer - declarative renderer settings and shader ABI metadata.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace PIXL::Metadata
{
	enum class SettingType : std::uint8_t
	{
		Boolean,
		Unsigned,
		FloatingPoint
	};

	struct SettingDescriptor
	{
		std::string_view id;
		std::string_view module;
		std::string_view label;
		std::string_view category;
		SettingType type;
		double defaultValue;
		double minimum;
		double maximum;
		bool live;
		bool shaderVisible;
		bool restartRequired;
	};

	namespace Settings
	{
		inline constexpr SettingDescriptor MaterialVertexAO{
			"MaterialForge.VertexAOStrength", "MaterialForge", "Vertex AO Strength", "Materials",
			SettingType::FloatingPoint, 1.0, 0.0, 1.0, true, true, false };
		inline constexpr SettingDescriptor CameraDofFStop{
			"CameraSuite.dofFStop", "CameraSuite", "Auto-DOF F-Stop", "Camera",
			SettingType::FloatingPoint, 3.4, 0.7, 32.0, true, true, false };
		inline constexpr SettingDescriptor HybridWorldCacheStrength{
			"HybridGI.WorldCacheStrength", "HybridGI", "World Cache Strength", "Lighting",
			SettingType::FloatingPoint, 0.60, 0.0, 1.5, true, true, false };
		inline constexpr SettingDescriptor GroundResponseStrength{
			"GroundResponse.GroundResponseStrength", "GroundResponse", "Interaction / Compaction Strength", "Terrain",
			SettingType::FloatingPoint, 0.71, 0.0, 3.0, true, true, false };
		inline constexpr SettingDescriptor RainWetness{
			"RainResponse.MaxRainWetness", "RainResponse", "Rain Wetness", "Weather",
			SettingType::FloatingPoint, 1.388, 0.0, 2.5, true, true, false };
		inline constexpr SettingDescriptor WaterCausticsStrength{
			"WaterOptics.CausticsStrength", "WaterOptics", "Caustics Intensity", "Water",
			SettingType::FloatingPoint, 1.2, 0.0, 2.0, true, true, false };

		inline constexpr std::array All{
			MaterialVertexAO,
			CameraDofFStop,
			HybridWorldCacheStrength,
			GroundResponseStrength,
			RainWetness,
			WaterCausticsStrength
		};
	}

	struct ShaderABIDescriptor
	{
		std::string_view module;
		std::string_view buffer;
		std::string_view version;
		std::uint32_t registerSlot;
		std::uint32_t sizeBytes;
		std::uint32_t alignment;
	};

	namespace ABI
	{
		inline constexpr ShaderABIDescriptor MaterialForge{
			"MaterialForge", "FeatureData.MaterialForgeSettings", "PIXL.MaterialForge.FeatureData.v1", 6, 80, 16 };
		inline constexpr ShaderABIDescriptor CameraHDR{
			"CameraSuite", "HDRDataCB", "PIXL.CameraSuite.HDR.v1", 0, 352, 16 };
		inline constexpr ShaderABIDescriptor CameraDOF{
			"CameraSuite", "DofControl", "PIXL.CameraSuite.DOF.v1", 1, 128, 16 };
		inline constexpr ShaderABIDescriptor HybridGI{
			"HybridGI", "HybridGICB", "PIXL.HybridGI.vNext1", 1, 416, 16 };
		inline constexpr ShaderABIDescriptor GroundResponse{
			"GroundResponse", "GroundResponseRuntimeCB", "PIXL.GroundResponse.Runtime.v1", 13, 208, 16 };
		inline constexpr ShaderABIDescriptor RainResponse{
			"RainResponse", "FeatureData.RainResponse", "PIXL.RainResponse.FeatureData.v1", 6, 256, 16 };
		inline constexpr ShaderABIDescriptor WaterOptics{
			"WaterOptics", "FeatureData.WaterOpticsSettings", "PIXL.WaterOptics.FeatureData.v1", 6, 64, 16 };

		inline constexpr std::array All{
			MaterialForge,
			CameraHDR,
			CameraDOF,
			HybridGI,
			GroundResponse,
			RainResponse,
			WaterOptics
		};
	}

	constexpr std::uint64_t HashText(std::uint64_t hash, std::string_view text)
	{
		for (const auto character : text) {
			hash ^= static_cast<std::uint8_t>(character);
			hash *= 1099511628211ull;
		}
		return hash;
	}

	constexpr std::uint64_t ShaderABIHash()
	{
		std::uint64_t hash = 14695981039346656037ull;
		for (const auto& abi : ABI::All) {
			hash = HashText(hash, abi.module);
			hash = HashText(hash, abi.buffer);
			hash = HashText(hash, abi.version);
			for (const auto value : { abi.registerSlot, abi.sizeBytes, abi.alignment }) {
				for (std::uint32_t shift = 0; shift < 32; shift += 8) {
					hash ^= static_cast<std::uint8_t>(value >> shift);
					hash *= 1099511628211ull;
				}
			}
		}
		return hash;
	}
}
