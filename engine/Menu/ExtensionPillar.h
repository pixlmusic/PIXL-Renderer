#pragma once

#include "ExtensionRegistry.h"

namespace PIXLUI::Extensions
{
	Registry& GetRegistry();
	void DrawPillar();
	bool IsPillarOpen() noexcept;
	// Closed UI performs no extension availability or draw callbacks.
	void ClosePillar();
	void Shutdown();
}
