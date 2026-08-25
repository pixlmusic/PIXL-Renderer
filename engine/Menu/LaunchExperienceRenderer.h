#pragma once

#include <cstdint>

/** Minimal first-launch experience for the standalone PIXL pipeline. */
class LaunchExperienceRenderer
{
public:
	static bool ShouldShowFirstTimeSetup();
	static void RenderFirstTimeSetupDialog();
	static bool ShouldSkipKeyRelease(uint32_t key);

private:
	static void MarkFirstTimeSetupComplete(uint32_t closingKey = 0);

	static bool isFirstTimeSetupShown;
	static uint32_t keyThatClosedDialog;
};
