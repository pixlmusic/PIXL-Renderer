#include "HorizonBlend.h"

#include <imgui.h>

void HorizonBlend::DrawSettings()
{
	ImGui::TextWrapped(
		"This feature provides compatibility with the Horizon Blend SKSE plugin, which extends the water far clip plane to allow water to be rendered beyond the vanilla far clip distance. This feature is only active when the Horizon Blend plugin is installed.");
}

void HorizonBlend::PostPostLoad()
{
	if (!loaded)
		return;
	// The shader-side far-water support is only wanted while the HorizonBlend plugin is
	// actually installed; without it water keeps the vanilla far-clip look. Checked here
	// because every SKSE plugin has loaded by now and the shader disk cache has not been
	// validated yet, so installing or removing the plugin invalidates the cache through
	// regular feature validation.
	// Older releases used HorizonBlend.dll; the current rebrand ships as
	// HorizonFix.dll. Both provide the far-horizon water compatibility contract.
	const bool horizonPluginLoaded =
		GetModuleHandleW(L"HorizonBlend.dll") != nullptr ||
		GetModuleHandleW(L"HorizonFix.dll") != nullptr;
	if (!horizonPluginLoaded) {
		loaded = false;
		failedLoadedMessage = "HorizonBlend/HorizonFix is not installed, compatibility is disabled.";
		logger::info("[Horizon Blend] HorizonBlend/HorizonFix plugin not detected, compatibility disabled");
	} else {
		logger::info("[Horizon Blend] HorizonBlend/HorizonFix plugin detected, compatibility enabled");
	}
}
