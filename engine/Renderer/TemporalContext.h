// PIXL Renderer - authoritative temporal frame state and selective history registry.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "FrameGraphTypes.h"

#include <array>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <d3d11.h>
#include <winrt/base.h>

namespace PIXL::Renderer
{
	enum class TemporalInvalidationReason : std::uint32_t
	{
		None = 0,
		CameraCut = 1u << 0,
		Teleport = 1u << 1,
		WorldspaceChange = 1u << 2,
		RenderOriginShift = 1u << 3,
		ResolutionChange = 1u << 4,
		SettingsChange = 1u << 5,
		ModuleReset = 1u << 6,
		DeviceReset = 1u << 7
	};

	using TemporalInvalidationMask = std::uint32_t;
	inline constexpr TemporalInvalidationMask TemporalMask(TemporalInvalidationReason reason) noexcept
	{
		return static_cast<TemporalInvalidationMask>(reason);
	}
	inline constexpr TemporalInvalidationMask AllTemporalInvalidations =
		TemporalMask(TemporalInvalidationReason::CameraCut) |
		TemporalMask(TemporalInvalidationReason::Teleport) |
		TemporalMask(TemporalInvalidationReason::WorldspaceChange) |
		TemporalMask(TemporalInvalidationReason::RenderOriginShift) |
		TemporalMask(TemporalInvalidationReason::ResolutionChange) |
		TemporalMask(TemporalInvalidationReason::SettingsChange) |
		TemporalMask(TemporalInvalidationReason::ModuleReset) |
		TemporalMask(TemporalInvalidationReason::DeviceReset);

	struct TemporalFrameInput
	{
		FrameToken token{};
		RenderExtent extent{};
		ViewType viewType{ ViewType::MainWorld };
		std::array<float, 16> view{};
		std::array<float, 16> projection{};
		std::array<float, 16> viewProjection{};
		std::array<float, 16> unjitteredViewProjection{};
		std::array<float, 16> enginePreviousUnjitteredViewProjection{};
		std::array<float, 16> inverseView{};
		std::array<double, 3> absoluteCameraPosition{};
		ID3D11ShaderResourceView* motionVectors{};
		ID3D11ShaderResourceView* disocclusion{};
		std::uint64_t frameIndex{};
		std::uint64_t worldContext{};
		std::uint64_t renderOriginEpoch{};
		std::uint32_t renderWidth{};
		std::uint32_t renderHeight{};
		std::uint32_t outputWidth{};
		std::uint32_t outputHeight{};
		std::uint32_t cameraMode{};
		float deltaTime{};
		float verticalFov{};
		bool renderOriginShifted{};
	};

	struct MotionContext
	{
		FrameToken token{};
		RenderExtent extent{};
		winrt::com_ptr<ID3D11ShaderResourceView> motionVectors;
		winrt::com_ptr<ID3D11ShaderResourceView> disocclusion;
		// R = geometric disocclusion, G = reusable history confidence.
		winrt::com_ptr<ID3D11ShaderResourceView> confidenceDisocclusion;
		std::uint32_t width{};
		std::uint32_t height{};
		bool valid{};
	};

	struct TemporalFrameSnapshot
	{
		TemporalFrameInput current{};
		TemporalFrameInput previous{};
		MotionContext motion;
		TemporalInvalidationMask frameInvalidations{};
		bool previousFrameValid{};
	};

	struct HistoryDesc
	{
		std::string name;
		std::string owner;
		TemporalInvalidationMask invalidateOn{ AllTemporalInvalidations };
		std::uint64_t memoryBytes{};
		std::function<void(TemporalInvalidationReason)> reset;
	};

	struct HistoryDiagnostics
	{
		std::uint64_t id{};
		std::string name;
		std::string owner;
		bool valid{};
		TemporalInvalidationReason lastReason{ TemporalInvalidationReason::None };
		std::uint64_t lastInvalidatedFrame{};
		std::uint64_t memoryBytes{};
	};

	struct TemporalDiagnostics
	{
		std::uint64_t frameIndex{};
		TemporalInvalidationMask frameInvalidations{};
		bool frameValid{};
		bool motionValid{};
		bool disocclusionAvailable{};
		std::uint32_t renderWidth{};
		std::uint32_t renderHeight{};
		std::uint64_t historyMemoryBytes{};
		std::vector<HistoryDiagnostics> histories;
	};

	class TemporalContext
	{
	public:
		static TemporalContext& Get();

		void BeginFrame(const TemporalFrameInput& input);
		void PublishDisocclusion(const FrameToken& token, ID3D11ShaderResourceView* resource,
			std::uint32_t width, std::uint32_t height);
		void PublishGPUConfidence(const FrameToken& token, const RenderExtent& extent,
			ID3D11ShaderResourceView* resource);
		void Invalidate(TemporalInvalidationReason reason, std::string_view detail = {});

		std::uint64_t RegisterHistory(HistoryDesc desc);
		void UnregisterHistory(std::uint64_t id);
		void SetHistoryValid(std::uint64_t id, bool valid = true);
		void SetHistoryMemory(std::uint64_t id, std::uint64_t bytes);
		void InvalidateHistory(std::uint64_t id, TemporalInvalidationReason reason);
		[[nodiscard]] bool IsHistoryValid(std::uint64_t id) const;

		[[nodiscard]] TemporalFrameSnapshot GetFrameSnapshot() const;
		[[nodiscard]] TemporalDiagnostics GetDiagnostics() const;
		[[nodiscard]] std::string GetLastInvalidationDetail() const;

		static std::string_view ToString(TemporalInvalidationReason reason) noexcept;

	private:
		struct HistoryEntry
		{
			std::uint64_t id{};
			HistoryDesc desc;
			bool valid{};
			TemporalInvalidationReason lastReason{ TemporalInvalidationReason::None };
			std::uint64_t lastInvalidatedFrame{};
		};

		void InvalidateLocked(TemporalInvalidationReason reason,
			std::vector<std::function<void(TemporalInvalidationReason)>>& callbacks);

		mutable std::mutex mutex;
		TemporalFrameSnapshot frame;
		std::vector<HistoryEntry> histories;
		std::string lastInvalidationDetail;
		std::uint64_t nextHistoryId{ 1 };
		TemporalInvalidationMask pendingFrameInvalidations{};
		bool initialized{};
	};
}
