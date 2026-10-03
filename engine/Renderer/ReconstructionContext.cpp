// PIXL Renderer - shared reconstruction inputs and reactive contributors.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ReconstructionContext.h"
#include <algorithm>

namespace PIXL::Renderer
{
	ReconstructionContext& ReconstructionContext::Get() { static ReconstructionContext value; return value; }
	std::uint64_t ReconstructionContext::RegisterReactiveContributor(std::string name, ReactiveContributor callback)
	{
		std::scoped_lock lock(mutex);
		if (!callback || contributors.size() >= 32) return 0;
		const auto id = nextId++;
		contributors.push_back({ id, std::move(name), std::move(callback) });
		return id;
	}
	void ReconstructionContext::UnregisterReactiveContributor(std::uint64_t id)
	{
		std::scoped_lock lock(mutex);
		std::erase_if(contributors, [id](const Contributor& c) { return c.id == id; });
	}
	void ReconstructionContext::ApplyReactiveContributors(ID3D11UnorderedAccessView* target, std::uint32_t width, std::uint32_t height) const
	{
		// Registration is setup-time only. Holding the registry lock avoids a
		// per-frame std::function snapshot/allocation on this hot path.
		std::scoped_lock lock(mutex);
		for (const auto& contributor : contributors)
			contributor.callback(target, width, height);
	}
	void ReconstructionContext::Publish(ReconstructionFrame next) { std::scoped_lock lock(mutex); frame = std::move(next); }
	ReconstructionFrame ReconstructionContext::Acquire() const { std::scoped_lock lock(mutex); return frame; }
	ReconstructionDiagnostics ReconstructionContext::GetDiagnostics() const
	{
		std::scoped_lock lock(mutex);
		return { frame.backend, frame.renderWidth, frame.renderHeight, frame.outputWidth, frame.outputHeight,
			contributors.size(), frame.renderWidth != 0 && frame.renderHeight != 0, frame.depth != nullptr, frame.motion != nullptr,
			frame.annotations.valid, frame.reactive != nullptr, frame.transparency != nullptr, frame.temporalValid };
	}
	std::string_view ReconstructionContext::ToString(ReconstructionBackend backend) noexcept
	{
		switch (backend) { case ReconstructionBackend::Native: return "Native"; case ReconstructionBackend::TAA: return "TAA";
		case ReconstructionBackend::DLSS: return "DLSS"; case ReconstructionBackend::FSR: return "FSR";
		case ReconstructionBackend::Neural: return "Neural"; default: return "Unknown"; }
	}
}
