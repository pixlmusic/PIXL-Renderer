// PIXL Renderer - stable large-world render-origin service.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional permissions are described in the repository EXCEPTIONS.md.

#include "RenderOrigin.h"

#include "Globals.h"

namespace PIXL::RenderOrigin
{
	std::string_view ToString(DiscontinuityReason reason)
	{
		switch (reason) {
		case DiscontinuityReason::None: return "continuous";
		case DiscontinuityReason::Startup: return "startup/load";
		case DiscontinuityReason::FrameRegression: return "frame regression";
		case DiscontinuityReason::WorldContextChanged: return "worldspace/cell context changed";
		case DiscontinuityReason::InvalidCamera: return "invalid camera";
		case DiscontinuityReason::LargeCameraJump: return "teleport/large camera jump";
		case DiscontinuityReason::ModeChanged: return "render-origin mode changed";
		default: return "unknown";
		}
	}

    Manager& Get()
    {
        static Manager manager;
        return manager;
    }

}
