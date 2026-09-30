// PIXL Renderer runtime relocation selection.
// Derived from Community Shaders 1.9.1; retained for compatible multi-runtime hooks.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <REL/REL.h>
#include <SKSE/Version.h>

namespace Util::VersionedRelocation
{
	template <class T>
	[[nodiscard]] T Select(T a_se, T a_ae, [[maybe_unused]] T a_futureAe) noexcept
	{
		// PIXL's supported CommonLib contract currently ends at Skyrim 1.6.1170.
		// Keep the third argument in the call-site ABI so a future runtime can be
		// enabled centrally after its relocations are validated.
		return REL::Module::IsAE() ? a_ae : a_se;
	}
}
