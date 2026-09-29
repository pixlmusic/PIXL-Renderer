// PIXL Renderer - curved-surface parallax controls.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional permissions are described in the repository EXCEPTIONS.md.

#pragma once

#include "RenderModule.h"

struct CurvedSurfaceMapping : RenderModule
{
	std::string GetName() override { return "Curved Surface Mapping"; }
	std::string GetShortName() override { return "CurvedSurfaceMapping"; }
	std::string_view GetCategory() const override { return ModuleGroups::kMaterials; }
	// Dormant for 1.0.4. Keep the module instance and shader define alive so
	// existing CSPOM permutations remain addressable without a cache rebuild.
	bool IsInMenu() const override { return false; }
	std::string_view GetShaderDefineName() override { return "PIXL_CSPOM"; }
	bool HasShaderDefine(RE::BSShader::Type a_type) override { return a_type == RE::BSShader::Type::Lighting; }
	bool AffectsCachedShader(RE::BSShader::Type a_type, std::uint32_t a_descriptor, CachedShaderStage a_stage) override;
	std::pair<std::string, std::vector<std::string>> GetModuleSummary() override;

	struct Settings
	{
		bool Enabled = false;
		std::uint32_t Quality = 2;  // 0 Low, 1 Medium, 2 High, 3 Ultra
		float Depth = 1.0f;
		float HeightBias = 0.0f;
		float CurvatureStrength = 0.65f;
		float SilhouetteStrength = 0.75f;
		float FullQualityDistance = 480.0f;
		float MaxDistance = 1536.0f;
		float MaxTexelShift = 18.0f;
		float NormalStrength = 0.55f;
		std::uint32_t MinSteps = 8;
		std::uint32_t MaxSteps = 28;
		std::uint32_t BinarySteps = 4;
		std::uint32_t ShadowSteps = 6;
		bool CurvedSurface = true;
		bool SilhouetteClipping = true;
		bool SelfOcclusion = true;
		bool SelfShadow = true;
		bool DepthWrite = true;
		bool StaticOpaque = true;
		bool Trees = true;
		bool Terrain = false;
		float OcclusionStrength = 0.42f;
		float ShadowStrength = 0.55f;
		float GrazingProtection = 0.78f;
		std::uint32_t DebugMode = 0;
	} settings;

	void SetupResources() override;
	void DrawSettings() override;
	void LoadSettings(json&) override;
	void SaveSettings(json&) override;
	void RestoreDefaultSettings() override { settings = {}; }
};
