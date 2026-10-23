// PIXL Renderer runtime relocation selection.
// Derived from Community Shaders 1.9.1; retained for compatible multi-runtime hooks.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <REL/REL.h>
#include <SKSE/Version.h>

namespace Util::VersionedRelocation
{
	template <class T>
	[[nodiscard]] T Select(T a_se, T a_ae, T a_futureAe) noexcept
	{
		// The third call-site value is the newer AE patch-site offset, not VR.
		// Steam 1.7.104 uses these offsets; older SE/AE runtimes retain their
		// established values until their own call-site data is selected.
		if (REL::Module::IsAE() && REL::Module::get().version() >= SKSE::RUNTIME_SSE_1_7_104)
			return a_futureAe;
		return REL::Module::IsAE() ? a_ae : a_se;
	}
}
