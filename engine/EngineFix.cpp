#include "EngineFix.h"

#include "EngineFixes/EffectShaderNoDecalsFix.h"
#include "EngineFixes/LODBlendRingFix.h"
#include "EngineFixes/ShadowmapCascadeCullingFix.h"
#include "EngineFixes/ShadowmapCascadeRasterizerFix.h"

const std::vector<EngineFix*>& EngineFix::GetOnPostPostLoadFixesList()
{
	// Function-local statics guarantee that fix instances exist before the
	// registration list is first used and remain alive for the plugin lifetime.
	static EffectShaderNoDecalsFix effectShaderNoDecalsFix;
	auto& lodBlendRingFix = LODBlendRingFix::GetInstance();
	static ShadowmapCascadeCullingFix shadowmapCascadeCullingFix;
	static ShadowmapRasterizerFix shadowmapRasterizerFix;

	static const std::vector<EngineFix*> fixes = {
		&effectShaderNoDecalsFix,
		&lodBlendRingFix,
		&shadowmapCascadeCullingFix,
		&shadowmapRasterizerFix
	};

	return fixes;
}

const std::vector<EngineFix*>& EngineFix::GetOnDataLoadedFixesList()
{
	// Reserved for fixes that require Skyrim's data-loaded stage.
	static const std::vector<EngineFix*> fixes;

	return fixes;
}

void EngineFix::InstallFixes(const std::vector<EngineFix*>& fixes)
{
	for (EngineFix* const fix : fixes) {
		if (!fix) {
			logger::warn("[Engine Fixes] Skipped null fix registration");
			continue;
		}
		fix->Install();
		// Install() may intentionally preserve the native path for an unverified
		// runtime. Do not report an installed hook when the fix declined to patch.
		logger::info("[Engine Fixes] Processed setup for {}", fix->GetName());
	}
}

void EngineFix::InstallOnPostPostLoadFixes()
{
	InstallFixes(GetOnPostPostLoadFixesList());
}

void EngineFix::InstallOnDataLoadedFixes()
{
	InstallFixes(GetOnDataLoadedFixesList());
}
