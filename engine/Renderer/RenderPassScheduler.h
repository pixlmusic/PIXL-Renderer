// PIXL Renderer - lightweight DX11 frame-pass scheduling and diagnostics.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional permissions are described in the repository EXCEPTIONS.md.

#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct RenderModule;

namespace PIXL::Renderer
{
	enum class RenderPhase : std::uint8_t
	{
		FrameBegin,
		SceneAcquire,
		DepthPreparation,
		TerrainPreparation,
		LightingPreparation,
		IndirectLighting,
		Atmosphere,
		Water,
		Transparency,
		Reconstruction,
		Presentation,
		FrameEnd
	};

	// Skyrim owns the outer render pipeline. Execution points identify the
	// established hook at which a PIXL pass is legal to run.
	enum class PassExecutionPoint : std::uint8_t
	{
		ReflectionsPrepass,
		EarlyPrepass,
		Prepass
	};

	enum class RenderResource : std::uint8_t
	{
		SharedFrameData,
		SceneColor,
		SceneDepth,
		GBuffer,
		DirectionalShadows,
		ReflectionScene,
		ModuleOwnedResources,
		TemporalHistory
	};

	enum class QualityGroup : std::uint8_t
	{
		Core,
		Terrain,
		Lighting,
		Atmosphere,
		Water,
		Reconstruction,
		Presentation,
		Utility
	};

	enum class SchedulerEvent : std::uint8_t
	{
		HistoryInvalidated,
		ResolutionChanged,
		ResourcesRecreated
	};

	using PassId = std::uint64_t;
	using ResourceUsage = std::vector<RenderResource>;

	class RenderPassContext
	{
	public:
		bool HistoryValid() const noexcept { return historyValid; }
		std::uint32_t Width() const noexcept { return width; }
		std::uint32_t Height() const noexcept { return height; }
		std::uint64_t FrameIndex() const noexcept { return frameIndex; }

		// Native scheduler passes use these accessors to make their declared
		// resource use developer-verifiable. Legacy callbacks retain direct DX11
		// access until they are migrated deliberately.
		bool Read(RenderResource resource) const;
		bool Write(RenderResource resource) const;

	private:
		friend class RenderPassScheduler;
		const ResourceUsage* reads{};
		const ResourceUsage* writes{};
		std::string_view passName{};
		std::uint64_t frameIndex{};
		std::uint32_t width{};
		std::uint32_t height{};
		bool historyValid{};
	};

	struct PassDesc
	{
		PassId id{};
		std::string name;
		RenderPhase phase{ RenderPhase::FrameBegin };
		PassExecutionPoint executionPoint{ PassExecutionPoint::EarlyPrepass };
		RenderModule* owner{};
		ResourceUsage reads;
		ResourceUsage writes;
		std::vector<PassId> dependencies;
		std::function<void(RenderPassContext&)> execute;
		std::function<void(SchedulerEvent)> notify;
		bool temporal{};
		bool optional{ true };
		bool enabled{ true };
		bool profilingEnabled{};
		QualityGroup qualityGroup{ QualityGroup::Utility };
	};

	struct PassDiagnostics
	{
		PassId id{};
		std::string name;
		std::string owner;
		RenderPhase phase{};
		PassExecutionPoint executionPoint{};
		ResourceUsage reads;
		ResourceUsage writes;
		bool temporal{};
		bool optional{};
		bool enabled{};
		bool failed{};
		bool profilingEnabled{};
		float lastCpuMs{};
		std::uint64_t invocationCount{};
		std::string failureReason;
	};

	class RenderPassScheduler
	{
	public:
		static RenderPassScheduler& Get();

		void Clear();
		bool RegisterPass(PassDesc pass);
		void RegisterLegacyModulePasses(std::span<RenderModule* const> modules);
		bool Finalize();

		// Returns false only when no scheduler registry is available, allowing the
		// caller to preserve the original direct module loop as a fail-safe.
		bool Execute(PassExecutionPoint point);

		void BeginFrame(std::uint64_t frameIndex, std::uint32_t width, std::uint32_t height, bool historyValid);
		void InvalidateHistory(std::string_view reason);
		void NotifyResourcesRecreated();

		bool IsReady() const noexcept { return ready; }
		bool IsHistoryValid() const noexcept { return historyValid; }
		std::uint64_t HistoryEpoch() const noexcept { return historyEpoch; }
		std::uint64_t ResourceEpoch() const noexcept { return resourceEpoch; }
		std::span<const std::string> ValidationMessages() const noexcept { return validationMessages; }
		std::vector<PassDiagnostics> GetDiagnostics() const;

		static PassId MakePassId(std::string_view name, PassExecutionPoint point) noexcept;
		static std::string_view ToString(RenderPhase phase) noexcept;
		static std::string_view ToString(PassExecutionPoint point) noexcept;
		static std::string_view ToString(RenderResource resource) noexcept;

	private:
		struct PassRecord
		{
			PassDesc desc;
			std::uint64_t registrationOrder{};
			std::uint64_t invocationCount{};
			float lastCpuMs{};
			bool failed{};
			std::string failureReason;
		};

		void Notify(SchedulerEvent event);
		void ValidateResources();
		std::vector<std::size_t> BuildExecutionOrder(PassExecutionPoint point);

		std::vector<PassRecord> passes;
		std::vector<std::size_t> reflectionOrder;
		std::vector<std::size_t> earlyOrder;
		std::vector<std::size_t> prepassOrder;
		std::vector<std::string> validationMessages;
		std::string historyReason;
		std::uint64_t nextRegistrationOrder{};
		std::uint64_t frameIndex{};
		std::uint64_t historyEpoch{};
		std::uint64_t resourceEpoch{};
		std::uint32_t width{};
		std::uint32_t height{};
		bool historyValid{};
		bool frameObserved{};
		bool ready{};
	};
}
