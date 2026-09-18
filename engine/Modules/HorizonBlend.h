#pragma once

#include "RenderModule.h"

/**
 * @brief Core feature enabling water rendering beyond the far clip plane for HorizonBlend.
 *
 * HorizonBlend (an SKSE plugin) fills the horizon gap between the farthest visible water and the
 * sky with a skirt of distant water tiles. Those tiles need shader-side support: the HORIZON_BLEND
 * define in Water.hlsl folds beyond-far-plane water back onto the far plane, shades it as
 * bottomless where nothing rendered behind it, and fades the distant skirt into atmospheric fog
 * at grazing angles.
 *
 * This feature's only job is to enable that define while the HorizonBlend plugin is installed. It
 * self-disables in PostPostLoad when the plugin is absent, so water keeps exact vanilla
 * far-clip behavior without it - and because that runs before shader cache validation, regular
 * feature validation recompiles the water shaders whenever HorizonBlend is installed or removed.
 */
struct HorizonBlend : RenderModule
{
	virtual inline std::string GetName() override { return "Horizon Blend"; }
	virtual inline std::string GetShortName() override { return "HorizonBlend"; }
	virtual inline std::string_view GetShaderDefineName() override { return "HORIZON_BLEND"; }
	virtual inline bool HasShaderDefine(RE::BSShader::Type t) override { return t == RE::BSShader::Type::Water; }
	virtual std::string_view GetCategory() const override { return ModuleGroups::kWater; }

	/** @brief Returns a summary description for the UI. */
	virtual std::pair<std::string, std::vector<std::string>> GetModuleSummary() override
	{
		return { "Enables water rendering beyond the far clip plane in support of the HorizonBlend plugin, which fills the horizon gap between the farthest visible water and the sky.",
			{ "Active only while the HorizonBlend SKSE plugin is installed.",
				"Without HorizonBlend, water keeps exact vanilla far clip behavior." } };
	}

	virtual void DrawSettings() override;

	/** @brief Disables the feature when the HorizonBlend plugin is not installed. */
	virtual void PostPostLoad() override;

	virtual bool IsCore() const override { return true; }
};
