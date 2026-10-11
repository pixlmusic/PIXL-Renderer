// PIXL Renderer - world-space volumetric cloud prototype.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Buffer.h"
#include "RenderModule.h"

#include <array>
#include <cstdint>
#include <memory>

struct VolumetricClouds : RenderModule
{
	struct Settings
	{
		bool Enabled = true;
		std::uint32_t Quality = 2;  // Performance, Balanced, High, Ultra.
		float Coverage = 0.76f;
		float Density = 1.55f;
		float VanillaCloudVisibility = 0.15f;
		float CloudBase = 18124.0f;
		float CloudThickness = 40893.0f;
		float MaximumDistance = 525100.0f;
		float ShapeScale = 1.08f;
		float DetailScale = 1.25f;
		float Erosion = 0.62f;
		float Turbulence = 0.44f;
		float WindSpeed = 0.41f;
		float Extinction = 1.17f;
		float Scattering = 0.8f;
		float AmbientLight = 0.68f;
		float ForwardScattering = 0.72f;
		float BackScattering = -0.18f;
		float SilverLining = 0.85f;
		float Powder = 0.55f;
		float MultipleScattering = 0.65f;
		float ShadowStrength = 0.20f;
		float TemporalResponse = 0.92f;
		std::uint32_t DebugMode = 0;
	};

	struct alignas(16) CloudConstants
	{
		float4 renderSize{};
		float4 fullSize{};
		float4 layer{};
		float4 weather{};
		float4 weatherSecondary{};
		float4 windTime{};
		float4 cloudMotion{};
		float4 weatherMap{};
		float4 previousWeatherMap{};
		float4 shape{};
		float4 lighting{};
		float4 phase{};
		float4 temporal{};
		float4 composite{};
		std::uint32_t primarySteps{};
		std::uint32_t lightSteps{};
		std::uint32_t weatherHistoryValid{};
		std::uint32_t updateWeather{};
	};
	STATIC_ASSERT_ALIGNAS_16(CloudConstants);
	static_assert(sizeof(CloudConstants) == 240, "Volumetric cloud CPU/HLSL ABI mismatch");

	VolumetricClouds();
	~VolumetricClouds();
	VolumetricClouds(const VolumetricClouds&) = delete;
	VolumetricClouds& operator=(const VolumetricClouds&) = delete;

	std::string GetName() override { return "PIXL Volumetric Clouds"; }
	std::string GetDisplayName() override { return "PIXL Volumetric Clouds"; }
	std::string GetShortName() override { return "VolumetricClouds"; }
	std::string_view GetCategory() const override { return ModuleGroups::kSky; }
	// This experimental core module is being shipped for direct live testing.
	// Keep its render pass enabled by default; users can still disable it at boot.
	bool IsDisabledByDefault() const override { return false; }
	std::string_view GetShaderDefineName() override { return {}; }
	bool HasShaderDefine(RE::BSShader::Type) override { return false; }
	bool HasNoPipelinePermutationDependencies() const override { return true; }
	bool AffectsCachedShader(RE::BSShader::Type, std::uint32_t, CachedShaderStage) override { return false; }
	std::pair<std::string, std::vector<std::string>> GetModuleSummary() override;

	void SetupResources() override;
	void Reset() override;
	void DrawSettings() override;
	void LoadSettings(json&) override;
	void SaveSettings(json&) override;
	void RestoreDefaultSettings() override;
	void RegisterWeatherVariables() override;
	void ClearShaderCache() override;

	/** Dispatches weather synthesis, raymarch, temporal resolve, and HDR composition. */
	void DrawClouds();
	/** Low-resolution resolved cloud transmittance for DOF protection; null when inactive. */
	ID3D11ShaderResourceView* GetDofCloudMaskSRV() const noexcept;

	Settings settings{};

private:
	struct Runtime;
	std::unique_ptr<Runtime> runtime;

	bool CompileShaders();
	bool EnsureFrameResources(std::uint32_t width, std::uint32_t height);
	void InvalidateHistory();
};

