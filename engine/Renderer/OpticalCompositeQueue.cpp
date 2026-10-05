// PIXL Renderer - bounded same-frame optical replay admission.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later

#include "OpticalCompositeQueue.h"

#include "Globals.h"

#include <algorithm>
#include <exception>

namespace PIXL::Renderer
{
	OpticalCompositeQueue& OpticalCompositeQueue::Get()
	{
		static OpticalCompositeQueue queue;
		return queue;
	}

	void OpticalCompositeQueue::BeginFrame(std::uint64_t nextFrame) noexcept
	{
		if (frame == nextFrame)
			return;
		Invalidate();
		frame = nextFrame;
	}

	void OpticalCompositeQueue::Invalidate() noexcept
	{
		for (auto& command : commands)
			command = {};
		count = 0;
		requirements = 0;
		frame = FrameToken::kInvalidFrame;
	}

	void OpticalCompositeQueue::DropOwner(void* owner) noexcept
	{
		std::size_t kept{};
		for (std::size_t i = 0; i < count; ++i) {
			if (commands[i].owner != owner)
				commands[kept++] = commands[i];
		}
		for (std::size_t i = kept; i < count; ++i)
			commands[i] = {};
		count = kept;
		requirements = 0;
		for (std::size_t i = 0; i < count; ++i)
			requirements |= commands[i].requirements;
	}

	bool OpticalCompositeQueue::Enqueue(const OpticalReplayCommand& command) noexcept
	{
		if (!command.token.Valid() || command.token.view != ViewType::MainWorld ||
			!command.pass || !command.geometryKey || !command.owner || !command.callback)
			return false;
		if (frame == FrameToken::kInvalidFrame)
			BeginFrame(command.token.frame);
		if (frame != command.token.frame)
			return false;
		for (std::size_t i = 0; i < count; ++i) {
			if (commands[i].owner == command.owner && commands[i].geometryKey == command.geometryKey)
				return false;
		}
		if (count == commands.size())
			return false;
		commands[count++] = command;
		requirements |= command.requirements;
		return true;
	}

	std::size_t OpticalCompositeQueue::ReplayOwner(void* owner, const FrameToken& token) noexcept
	{
		if (!owner || !token.Valid() || token.view != ViewType::MainWorld || token.frame != frame ||
			!globals::d3d::context || !globals::game::renderer || !globals::game::shadowState)
			return 0;
		auto* context = globals::d3d::context;
		auto* renderer = globals::game::renderer;
		auto& shadow = globals::game::shadowState->GetRuntimeData();
		std::array<ID3D11RenderTargetView*, 4> forward{};
		for (std::size_t i = 0; i < forward.size(); ++i) {
			const auto id = shadow.renderTargets[i];
			if (id != RE::RENDER_TARGET::kNONE)
				forward[i] = renderer->GetRuntimeData().renderTargets[id].RTV;
		}
		auto* depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN].views[0];
		if (!forward[0] || !depth)
			return 0;
		std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> saved{};
		ID3D11DepthStencilView* savedDepth{};
		context->OMGetRenderTargets(static_cast<UINT>(saved.size()), saved.data(), &savedDepth);
		context->OMSetRenderTargets(static_cast<UINT>(forward.size()), forward.data(), depth);
		std::size_t replayed{};
		for (std::size_t i = 0; i < count; ++i) {
			const auto& command = commands[i];
			if (command.owner != owner || !command.token.Matches(token))
				continue;
			try {
				command.callback(command.owner, command.pass, command.technique, command.alphaTest, command.flags);
				++replayed;
			} catch (const std::exception& e) {
				logger::error("[PIXL Optical] Same-frame replay skipped: {}", e.what());
			} catch (...) {
				logger::error("[PIXL Optical] Same-frame replay skipped after unknown exception");
			}
		}
		context->OMSetRenderTargets(static_cast<UINT>(saved.size()), saved.data(), savedDepth);
		for (auto* target : saved)
			if (target) target->Release();
		if (savedDepth) savedDepth->Release();
		shadow.stateUpdateFlags.set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);
		DropOwner(owner);
		return replayed;
	}
}
