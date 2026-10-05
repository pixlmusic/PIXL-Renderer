// PIXL Renderer - hysteretic adaptive GPU workload controller.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <string_view>
class Profiler;
namespace PIXL::Renderer
{
	enum class WorkloadDomain : std::uint8_t { HybridGI, SkyBounce, Atmosphere, Reflections, GroundResponse, Water, CameraSuite, Reconstruction, Volumetrics, Count };
	struct WorkloadState { float lastMs{}, averageMs{}; std::uint8_t level{3}, maximumLevel{3}; std::uint32_t holdFrames{}; const char* reason{"stable"}; };
	class GPUWorkloadBudgeter
	{
	public:
		static GPUWorkloadBudgeter& Get();
		void Update(const Profiler& profiler, float deltaSeconds);
		void SetEnabled(bool value) noexcept { enabled = value; }
		[[nodiscard]] bool IsEnabled() const noexcept { return enabled; }
		void SetTargetFrameMs(float value) noexcept;
		[[nodiscard]] float GetTargetFrameMs() const noexcept { return targetFrameMs; }
		void SetMaximumLevel(WorkloadDomain domain, std::uint8_t level) noexcept;
		[[nodiscard]] float GetScale(WorkloadDomain domain) const noexcept;
		[[nodiscard]] const auto& GetStates() const noexcept { return states; }
		static std::string_view ToString(WorkloadDomain domain) noexcept;
	private:
		std::array<WorkloadState, static_cast<std::size_t>(WorkloadDomain::Count)> states{};
		float targetFrameMs{16.67f};
		bool enabled{};
	};
}
