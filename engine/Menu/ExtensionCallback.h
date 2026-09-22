#pragma once

#include "ExtensionRegistry.h"
#include <imgui_internal.h>

namespace PIXLUI::Extensions
{
	// Recover ordinary ImGui stack mistakes in a provider without leaving
	// the main workspace disabled or recoloured. This is not a memory sandbox:
	// native extensions must still honour the in-process API contract.
	template <class Callback>
	bool InvokeIsolated(Entry& entry, Callback&& callback)
	{
		if (!entry.registered || entry.faulted)
			return false;
		auto& io = ImGui::GetIO();
		const bool oldAssert = io.ConfigErrorRecoveryEnableAssert;
		const bool oldTooltip = io.ConfigErrorRecoveryEnableTooltip;
		const bool oldRecovery = io.ConfigErrorRecovery;
		io.ConfigErrorRecovery = true;
		io.ConfigErrorRecoveryEnableAssert = false;
		io.ConfigErrorRecoveryEnableTooltip = false;
		const auto style = ImGui::GetStyle();
		const int errors = ImGui::GetCurrentContext()->ErrorCountCurrentFrame;
		ImGuiErrorRecoveryState saved;
		ImGui::ErrorRecoveryStoreState(&saved);
		try {
			callback();
		} catch (...) {
			entry.faulted = true;
		}
		ImGui::ErrorRecoveryTryToRecoverState(&saved);
		entry.faulted |= ImGui::GetCurrentContext()->ErrorCountCurrentFrame != errors;
		ImGui::GetStyle() = style;
		io.ConfigErrorRecoveryEnableAssert = oldAssert;
		io.ConfigErrorRecoveryEnableTooltip = oldTooltip;
		io.ConfigErrorRecovery = oldRecovery;
		return !entry.faulted && entry.registered;
	}
}
