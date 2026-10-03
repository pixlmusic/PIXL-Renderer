// PIXL Renderer - shared conservative hierarchical-depth visibility service.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later

#include "VisibilityContext.h"

namespace PIXL::Renderer
{
	VisibilityContext& VisibilityContext::Get()
	{
		static VisibilityContext instance;
		return instance;
	}

	void VisibilityContext::SetupResources()
	{
		depthPyramid.SetupResources();
		initialized = true;
		Invalidate("resources initialized");
	}

	void VisibilityContext::ClearShaderCache()
	{
		depthPyramid.ClearShaderCache();
		Invalidate("shader cache cleared");
	}

	bool VisibilityContext::Build(ID3D11Device* a_device, ID3D11DeviceContext* a_context, std::uint64_t a_frameIndex)
	{
		if (!initialized || !a_device || !a_context)
			return false;
		if (builtFrame == a_frameIndex)
			return depthPyramid.IsValid();
		builtFrame = a_frameIndex;
		const bool valid = depthPyramid.Build(a_device, a_context);
		if (valid) {
			++buildCount;
			invalidationReason.clear();
		} else {
			invalidationReason = "depth pyramid build unavailable";
		}
		return valid;
	}

	void VisibilityContext::Invalidate(std::string_view a_reason)
	{
		depthPyramid.Invalidate();
		builtFrame = UINT64_MAX;
		invalidationReason.assign(a_reason);
	}

	VisibilityDiagnostics VisibilityContext::GetDiagnostics() const
	{
		return {
			initialized,
			depthPyramid.IsValid(),
			builtFrame != UINT64_MAX,
			depthPyramid.GetWidth(),
			depthPyramid.GetHeight(),
			depthPyramid.GetMipCount(),
			depthPyramid.GetTexelPixels(),
			builtFrame == UINT64_MAX ? 0u : builtFrame,
			buildCount,
			invalidationReason
		};
	}
}
