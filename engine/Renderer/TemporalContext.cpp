// PIXL Renderer - authoritative temporal frame state and selective history registry.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TemporalContext.h"

#include <algorithm>
#include <cmath>

namespace PIXL::Renderer
{
	namespace
	{
		double DistanceSquared(const std::array<double, 3>& left, const std::array<double, 3>& right)
		{
			const double x = left[0] - right[0];
			const double y = left[1] - right[1];
			const double z = left[2] - right[2];
			return x * x + y * y + z * z;
		}
	}

	TemporalContext& TemporalContext::Get()
	{
		static TemporalContext instance;
		return instance;
	}

	void TemporalContext::BeginFrame(const TemporalFrameInput& input)
	{
		std::vector<std::pair<TemporalInvalidationReason, std::function<void(TemporalInvalidationReason)>>> callbacks;
		{
			std::scoped_lock lock(mutex);
			if (initialized && frame.current.frameIndex == input.frameIndex)
				return;

			TemporalInvalidationMask reasons = 0;
			if (initialized) {
				if (input.worldContext != frame.current.worldContext)
					reasons |= TemporalMask(TemporalInvalidationReason::WorldspaceChange);
				if (input.renderOriginEpoch != frame.current.renderOriginEpoch || input.renderOriginShifted)
					reasons |= TemporalMask(TemporalInvalidationReason::RenderOriginShift);
				if (input.renderWidth != frame.current.renderWidth || input.renderHeight != frame.current.renderHeight ||
					input.outputWidth != frame.current.outputWidth || input.outputHeight != frame.current.outputHeight)
					reasons |= TemporalMask(TemporalInvalidationReason::ResolutionChange);
				const double cameraDistanceSq = DistanceSquared(input.absoluteCameraPosition, frame.current.absoluteCameraPosition);
				if (cameraDistanceSq > 32768.0 * 32768.0)
					reasons |= TemporalMask(TemporalInvalidationReason::Teleport);
				else if (cameraDistanceSq > 4096.0 * 4096.0 || input.cameraMode != frame.current.cameraMode ||
					std::abs(input.verticalFov - frame.current.verticalFov) > 1.0e-4f)
					reasons |= TemporalMask(TemporalInvalidationReason::CameraCut);
			}

			frame.previous = initialized ? frame.current : input;
			frame.current = input;
			frame.frameInvalidations = reasons;
			frame.previousFrameValid = initialized && reasons == 0;
			frame.motion = {};
			if (input.motionVectors) {
				frame.motion.motionVectors.copy_from(input.motionVectors);
				frame.motion.valid = true;
			}
			if (input.disocclusion)
				frame.motion.disocclusion.copy_from(input.disocclusion);
			frame.motion.width = input.renderWidth;
			frame.motion.height = input.renderHeight;
			initialized = true;

			for (std::uint32_t bit = 0; bit < 8; ++bit) {
				const auto mask = TemporalInvalidationMask{ 1u } << bit;
				if ((reasons & mask) == 0)
					continue;
				const auto reason = static_cast<TemporalInvalidationReason>(mask);
				std::vector<std::function<void(TemporalInvalidationReason)>> pending;
				InvalidateLocked(reason, pending);
				for (auto& callback : pending)
					callbacks.emplace_back(reason, std::move(callback));
			}
		}
		for (auto& [reason, callback] : callbacks)
			if (callback)
				callback(reason);
	}

	void TemporalContext::PublishDisocclusion(ID3D11ShaderResourceView* resource, std::uint32_t width, std::uint32_t height)
	{
		std::scoped_lock lock(mutex);
		frame.motion.disocclusion = nullptr;
		if (resource)
			frame.motion.disocclusion.copy_from(resource);
		frame.motion.width = width;
		frame.motion.height = height;
	}

	void TemporalContext::Invalidate(TemporalInvalidationReason reason, std::string_view detail)
	{
		if (reason == TemporalInvalidationReason::None)
			return;
		std::vector<std::function<void(TemporalInvalidationReason)>> callbacks;
		{
			std::scoped_lock lock(mutex);
			frame.frameInvalidations |= TemporalMask(reason);
			frame.previousFrameValid = false;
			lastInvalidationDetail.assign(detail);
			InvalidateLocked(reason, callbacks);
		}
		for (auto& callback : callbacks)
			if (callback)
				callback(reason);
	}

	std::uint64_t TemporalContext::RegisterHistory(HistoryDesc desc)
	{
		std::scoped_lock lock(mutex);
		const auto id = nextHistoryId++;
		histories.push_back({ id, std::move(desc), false, TemporalInvalidationReason::ModuleReset, frame.current.frameIndex });
		return id;
	}

	void TemporalContext::UnregisterHistory(std::uint64_t id)
	{
		std::scoped_lock lock(mutex);
		std::erase_if(histories, [id](const HistoryEntry& entry) { return entry.id == id; });
	}

	void TemporalContext::SetHistoryValid(std::uint64_t id, bool valid)
	{
		std::scoped_lock lock(mutex);
		if (auto it = std::ranges::find(histories, id, &HistoryEntry::id); it != histories.end())
			it->valid = valid;
	}

	void TemporalContext::SetHistoryMemory(std::uint64_t id, std::uint64_t bytes)
	{
		std::scoped_lock lock(mutex);
		if (auto it = std::ranges::find(histories, id, &HistoryEntry::id); it != histories.end())
			it->desc.memoryBytes = bytes;
	}

	void TemporalContext::InvalidateHistory(std::uint64_t id, TemporalInvalidationReason reason)
	{
		std::function<void(TemporalInvalidationReason)> callback;
		{
			std::scoped_lock lock(mutex);
			if (auto it = std::ranges::find(histories, id, &HistoryEntry::id); it != histories.end()) {
				it->valid = false;
				it->lastReason = reason;
				it->lastInvalidatedFrame = frame.current.frameIndex;
				callback = it->desc.reset;
			}
		}
		if (callback)
			callback(reason);
	}

	bool TemporalContext::IsHistoryValid(std::uint64_t id) const
	{
		std::scoped_lock lock(mutex);
		const auto it = std::ranges::find(histories, id, &HistoryEntry::id);
		return it != histories.end() && it->valid;
	}

	TemporalFrameSnapshot TemporalContext::GetFrameSnapshot() const
	{
		std::scoped_lock lock(mutex);
		return frame;
	}

	TemporalDiagnostics TemporalContext::GetDiagnostics() const
	{
		std::scoped_lock lock(mutex);
		TemporalDiagnostics diagnostics{};
		diagnostics.frameIndex = frame.current.frameIndex;
		diagnostics.frameInvalidations = frame.frameInvalidations;
		diagnostics.frameValid = frame.previousFrameValid;
		diagnostics.motionValid = frame.motion.valid;
		diagnostics.disocclusionAvailable = static_cast<bool>(frame.motion.disocclusion);
		diagnostics.renderWidth = frame.current.renderWidth;
		diagnostics.renderHeight = frame.current.renderHeight;
		for (const auto& history : histories) {
			diagnostics.historyMemoryBytes += history.desc.memoryBytes;
			diagnostics.histories.push_back({ history.id, history.desc.name, history.desc.owner, history.valid,
				history.lastReason, history.lastInvalidatedFrame, history.desc.memoryBytes });
		}
		return diagnostics;
	}

	std::string TemporalContext::GetLastInvalidationDetail() const
	{
		std::scoped_lock lock(mutex);
		return lastInvalidationDetail;
	}

	void TemporalContext::InvalidateLocked(TemporalInvalidationReason reason,
		std::vector<std::function<void(TemporalInvalidationReason)>>& callbacks)
	{
		const auto mask = TemporalMask(reason);
		for (auto& history : histories) {
			if ((history.desc.invalidateOn & mask) == 0)
				continue;
			history.valid = false;
			history.lastReason = reason;
			history.lastInvalidatedFrame = frame.current.frameIndex;
			if (history.desc.reset)
				callbacks.push_back(history.desc.reset);
		}
	}

	std::string_view TemporalContext::ToString(TemporalInvalidationReason reason) noexcept
	{
		switch (reason) {
		case TemporalInvalidationReason::None: return "None";
		case TemporalInvalidationReason::CameraCut: return "CameraCut";
		case TemporalInvalidationReason::Teleport: return "Teleport";
		case TemporalInvalidationReason::WorldspaceChange: return "WorldspaceChange";
		case TemporalInvalidationReason::RenderOriginShift: return "RenderOriginShift";
		case TemporalInvalidationReason::ResolutionChange: return "ResolutionChange";
		case TemporalInvalidationReason::SettingsChange: return "SettingsChange";
		case TemporalInvalidationReason::ModuleReset: return "ModuleReset";
		case TemporalInvalidationReason::DeviceReset: return "DeviceReset";
		default: return "Unknown";
		}
	}
}
