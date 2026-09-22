#pragma once

namespace ExternalPostProcessing
{
	void Initialize();
	void DrawSettings();
	void DrawENBQuickStyles();
	// Compact, apply-only presentation for the public Camera page.  The full
	// management surface remains available to engineering UI callers.
	bool DrawENBQuickStylePalette();
}
