// PIXL Renderer - lightweight DX11 frame-pass scheduling and diagnostics.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional permissions are described in the repository EXCEPTIONS.md.

#include "RenderPassScheduler.h"
#include "GPUResourceServices.h"
#include "LightTransportWorld.h"
#include "TemporalContext.h"
#include "TemporalValidityGPU.h"
#include "OpticalCompositeQueue.h"
#include "VisibilityContext.h"
#include "PixelAnnotations.h"
#include "ReconstructionContext.h"

#include "Globals.h"
#include "Profiler.h"
#include "RenderModule.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <format>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace PIXL::Renderer
{
	namespace
	{
		constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
		constexpr std::uint64_t kFnvPrime = 1099511628211ull;

		bool Contains(std::span<const RenderResource> resources, RenderResource resource)
		{
			return std::ranges::find(resources, resource) != resources.end();
		}

		bool IsSharedWritableResource(RenderResource resource)
		{
			return resource != RenderResource::ModuleOwnedResources &&
			       resource != RenderResource::TemporalHistory;
		}

		bool ProfileLegacyPass(std::string_view module, PassExecutionPoint point)
		{
			// Representative compatibility passes prove profiler integration without
			// consuming one D3D11 query pair for every inherited no-op callback.
			return (point == PassExecutionPoint::EarlyPrepass && module == "GroundResponse") ||
			       (point == PassExecutionPoint::ReflectionsPrepass && module == "AmbientProbe") ||
			       (point == PassExecutionPoint::Prepass && module == "Atmosphere");
		}

		QualityGroup LegacyQualityGroup(std::string_view module)
		{
			if (module == "GroundResponse" || module == "TerrainField" || module == "TerrainOcclusion" ||
				module == "TerrainSeam" || module == "FoliageDynamics" || module == "FoliageOptimizer")
				return QualityGroup::Terrain;
			if (module == "Atmosphere" || module == "SkyVeil" || module == "VolumeOcclusion" ||
				module == "RainResponse")
				return QualityGroup::Atmosphere;
			if (module == "WaterOptics" || module == "Waterbody" || module == "ContainedLiquids")
				return QualityGroup::Water;
			if (module == "ImageReconstruction" || module == "CameraSuite")
				return QualityGroup::Reconstruction;
			if (module == "HybridGI" || module == "AmbientProbe" || module == "WorldProbes" ||
				module == "SkyBounce" || module == "RadiantGrid" || module == "LightVolumes")
				return QualityGroup::Lighting;
			return QualityGroup::Utility;
		}
	}

	bool RenderPassContext::Read(RenderResource resource) const
	{
		const bool declared = reads && Contains(*reads, resource);
		if (!declared)
			logger::error("[PIXL Scheduler] Pass '{}' read undeclared resource '{}'", passName, RenderPassScheduler::ToString(resource));
		return declared;
	}

	bool RenderPassContext::Write(RenderResource resource) const
	{
		const bool declared = writes && Contains(*writes, resource);
		if (!declared)
			logger::error("[PIXL Scheduler] Pass '{}' wrote undeclared resource '{}'", passName, RenderPassScheduler::ToString(resource));
		return declared;
	}

	RenderPassScheduler& RenderPassScheduler::Get()
	{
		static RenderPassScheduler scheduler;
		return scheduler;
	}

	void RenderPassScheduler::Clear()
	{
		passes.clear();
		reflectionOrder.clear();
		earlyOrder.clear();
		prepassOrder.clear();
		validationMessages.clear();
		historyReason.clear();
		depthViews.clear();
		currentView = {};
		nextRegistrationOrder = 0;
		ready = false;
	}

	bool RenderPassScheduler::RegisterPass(PassDesc pass)
	{
		if (ready) {
			logger::error("[PIXL Scheduler] Registration rejected after finalization: {}", pass.name);
			return false;
		}
		if (pass.name.empty() || !pass.execute) {
			validationMessages.push_back("Pass registration rejected: name and callback are required.");
			return false;
		}
		if (pass.id == 0)
			pass.id = MakePassId(pass.name, pass.executionPoint);
		if (std::ranges::any_of(passes, [&](const PassRecord& existing) { return existing.desc.id == pass.id || existing.desc.name == pass.name; })) {
			validationMessages.push_back(std::format("Duplicate pass registration: {}", pass.name));
			return false;
		}
		passes.push_back({ std::move(pass), nextRegistrationOrder++ });
		return true;
	}

	void RenderPassScheduler::RegisterLegacyModulePasses(std::span<RenderModule* const> modules)
	{
		Clear();
		for (auto* module : modules) {
			if (!module)
				continue;
			const auto moduleName = module->GetShortName();
			auto addLegacy = [&](std::string_view callbackName,
				PassExecutionPoint point,
				RenderPhase phase,
				ResourceUsage reads,
				ResourceUsage writes,
				auto callback) {
				PassDesc pass{};
				pass.name = std::format("{}::{}", moduleName, callbackName);
				pass.id = MakePassId(pass.name, point);
				pass.phase = phase;
				pass.executionPoint = point;
				pass.owner = module;
				pass.reads = std::move(reads);
				pass.writes = std::move(writes);
				pass.optional = true;
				pass.allowedViews = point == PassExecutionPoint::ReflectionsPrepass ?
					ViewBit(ViewType::Reflection) | ViewBit(ViewType::Cubemap) : ViewBit(ViewType::MainWorld);
				pass.profilingEnabled = ProfileLegacyPass(moduleName, point);
				pass.qualityGroup = LegacyQualityGroup(moduleName);
				pass.resolutionDomain = point == PassExecutionPoint::ReflectionsPrepass ?
					ResolutionDomain::Backing : ResolutionDomain::ActiveRender;
				pass.execute = [module, callback](RenderPassContext&) { (module->*callback)(); };
				RegisterPass(std::move(pass));
			};

			addLegacy("ReflectionsPrepass",
				PassExecutionPoint::ReflectionsPrepass,
				RenderPhase::LightingPreparation,
				{ RenderResource::SharedFrameData, RenderResource::ReflectionScene },
				{ RenderResource::ModuleOwnedResources },
				&RenderModule::ReflectionsPrepass);
			addLegacy("EarlyPrepass",
				PassExecutionPoint::EarlyPrepass,
				RenderPhase::DepthPreparation,
				{ RenderResource::SharedFrameData, RenderResource::DirectionalShadows },
				{ RenderResource::ModuleOwnedResources },
				&RenderModule::EarlyPrepass);
			addLegacy("Prepass",
				PassExecutionPoint::Prepass,
				RenderPhase::LightingPreparation,
				{ RenderResource::SharedFrameData, RenderResource::SceneDepth, RenderResource::GBuffer },
				{ RenderResource::ModuleOwnedResources },
				&RenderModule::Prepass);
		}
		Finalize();
	}

	std::vector<std::size_t> RenderPassScheduler::BuildExecutionOrder(PassExecutionPoint point)
	{
		std::vector<std::size_t> candidates;
		for (std::size_t index = 0; index < passes.size(); ++index) {
			if (passes[index].desc.executionPoint == point)
				candidates.push_back(index);
		}

		std::unordered_map<PassId, std::size_t> indexById;
		for (auto index : candidates)
			indexById.emplace(passes[index].desc.id, index);

		std::unordered_map<std::size_t, std::uint32_t> indegree;
		std::unordered_map<std::size_t, std::vector<std::size_t>> dependents;
		for (auto index : candidates)
			indegree[index] = 0;
		for (auto index : candidates) {
			for (const auto dependency : passes[index].desc.dependencies) {
				const auto found = indexById.find(dependency);
				if (found == indexById.end()) {
					validationMessages.push_back(std::format("Pass '{}' has a missing or cross-hook dependency ({:016X}).", passes[index].desc.name, dependency));
					continue;
				}
				++indegree[index];
				dependents[found->second].push_back(index);
			}
		}

		std::vector<std::size_t> ordered;
		ordered.reserve(candidates.size());
		while (ordered.size() < candidates.size()) {
			std::size_t selected = std::numeric_limits<std::size_t>::max();
			for (auto index : candidates) {
				if (indegree[index] == 0 && std::ranges::find(ordered, index) == ordered.end() &&
					(selected == std::numeric_limits<std::size_t>::max() || passes[index].registrationOrder < passes[selected].registrationOrder)) {
					selected = index;
				}
			}
			if (selected == std::numeric_limits<std::size_t>::max()) {
				validationMessages.push_back(std::format("Dependency cycle detected at execution point '{}'.", ToString(point)));
				return candidates;
			}
			ordered.push_back(selected);
			indegree[selected] = std::numeric_limits<std::uint32_t>::max();
			for (auto dependent : dependents[selected])
				--indegree[dependent];
		}
		return ordered;
	}

	void RenderPassScheduler::ValidateResources()
	{
		for (const auto* order : { &reflectionOrder, &earlyOrder, &prepassOrder }) {
			std::unordered_map<RenderResource, std::size_t> writer;
			for (const auto index : *order) {
				for (const auto resource : passes[index].desc.writes) {
					if (!IsSharedWritableResource(resource))
						continue;
					if (const auto found = writer.find(resource); found != writer.end()) {
						const auto& first = passes[found->second].desc;
						const auto& second = passes[index].desc;
						if (std::ranges::find(second.dependencies, first.id) == second.dependencies.end())
							validationMessages.push_back(std::format("Shared resource '{}' is written by '{}' and '{}' without an explicit dependency.", ToString(resource), first.name, second.name));
					}
					writer[resource] = index;
				}
			}
		}
	}

	bool RenderPassScheduler::Finalize()
	{
		reflectionOrder = BuildExecutionOrder(PassExecutionPoint::ReflectionsPrepass);
		earlyOrder = BuildExecutionOrder(PassExecutionPoint::EarlyPrepass);
		prepassOrder = BuildExecutionOrder(PassExecutionPoint::Prepass);
		ValidateResources();
		ready = !passes.empty();
		if (ready)
			logger::info("[PIXL Scheduler] Registered {} compatibility passes ({} validation message(s))", passes.size(), validationMessages.size());
		for (const auto& message : validationMessages)
			logger::warn("[PIXL Scheduler] {}", message);
		return ready;
	}

	bool RenderPassScheduler::Execute(PassExecutionPoint point)
	{
		if (!ready)
			return false;
		if (!frameObserved || !currentView.Valid() || currentView.token.frame != frameIndex ||
			currentView.token.resources.value != resourceEpoch) {
			logger::error("[PIXL Scheduler] {} rejected: no valid current-frame view context", ToString(point));
			return false;
		}
		const auto& order = point == PassExecutionPoint::ReflectionsPrepass ? reflectionOrder :
		                    point == PassExecutionPoint::EarlyPrepass ? earlyOrder : prepassOrder;
		for (const auto index : order) {
			auto& record = passes[index];
			auto& pass = record.desc;
			if (!pass.enabled || record.failed || (pass.owner && !pass.owner->loaded))
				continue;
			if ((pass.allowedViews & ViewBit(currentView.type)) == 0)
				continue;
			if (pass.requiresCurrentFrame && currentView.token.frame != frameIndex)
				continue;
			if (pass.temporal && !currentView.advancesMainTemporal)
				continue;
			const auto* depth = GetDepth(pass.requiredDepth);
			if (pass.requiredDepth != DepthEpoch::None && !depth) {
				logger::warn("[PIXL Scheduler] Pass '{}' skipped: required depth epoch {} is unavailable for the current view",
					pass.name, static_cast<unsigned>(pass.requiredDepth));
				continue;
		}

			RenderPassContext context{};
			context.reads = &pass.reads;
			context.writes = &pass.writes;
			context.passName = pass.name;
			context.token = currentView.token;
			context.view = currentView;
			context.extent = currentView.extent;
			context.extent.domain = pass.resolutionDomain;
			context.depth = depth;
			context.frameIndex = frameIndex;
			context.historyValid = historyValid;

			const auto start = pass.profilingEnabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
			if (pass.profilingEnabled && globals::profiler)
				globals::profiler->BeginPass(pass.name);
			std::exception_ptr fatalFailure;
			try {
				pass.execute(context);
				++record.invocationCount;
			} catch (const std::exception& e) {
				record.failed = true;
				record.failureReason = e.what();
				logger::error("[PIXL Scheduler] Pass '{}' failed: {}", pass.name, e.what());
				if (!pass.optional)
					fatalFailure = std::current_exception();
			} catch (...) {
				record.failed = true;
				record.failureReason = "unknown C++ exception";
				logger::error("[PIXL Scheduler] Pass '{}' failed with an unknown C++ exception", pass.name);
				if (!pass.optional)
					fatalFailure = std::current_exception();
			}
			if (pass.profilingEnabled && globals::profiler)
				globals::profiler->EndPass();
			if (pass.profilingEnabled)
				record.lastCpuMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
			if (fatalFailure)
				std::rethrow_exception(fatalFailure);
		}
		return true;
	}

	void RenderPassScheduler::BeginFrame(std::uint64_t nextFrameIndex, std::uint32_t nextWidth, std::uint32_t nextHeight, bool nextHistoryValid)
	{
		RenderExtent nextExtent{};
		nextExtent.backingWidth = nextWidth;
		nextExtent.backingHeight = nextHeight;
		nextExtent.active = { 0, 0, nextWidth, nextHeight };
		nextExtent.outputWidth = nextWidth;
		nextExtent.outputHeight = nextHeight;
		BeginFrame(nextFrameIndex, nextExtent, nextHistoryValid);
	}

	void RenderPassScheduler::BeginFrame(std::uint64_t nextFrameIndex, const RenderExtent& nextExtent, bool nextHistoryValid)
	{
		if (!nextExtent.Valid()) {
			logger::error("[PIXL Scheduler] Invalid render extent rejected for frame {}", nextFrameIndex);
			return;
		}
		if (resourceEpoch == 0)
			resourceEpoch = 1;
		const bool extentChanged = frameObserved && !nextExtent.SameAllocation(extent);
		if (extentChanged) {
			++resourceEpoch;
			GPUResourceServices::Get().OnResolutionChanged();
			LightTransportWorld::Get().Invalidate("render resolution changed");
			VisibilityContext::Get().Invalidate("render resolution changed");
			Notify(SchedulerEvent::ResolutionChanged);
		}
		if (!nextHistoryValid && (!frameObserved || historyValid))
			InvalidateHistory("global render continuity invalidated");
		const bool newFrame = !frameObserved || nextFrameIndex != frameIndex;
		frameIndex = nextFrameIndex;
		extent = nextExtent;
		historyValid = nextHistoryValid;
		frameObserved = true;
		if (newFrame || extentChanged) {
			nextViewSerial = 0;
			depthViews.clear();
			currentView = {};
		}
	}

	ViewContext RenderPassScheduler::BeginView(ViewType type, bool advancesMainTemporal)
	{
		return BeginView(type, extent, advancesMainTemporal);
	}

	ViewContext RenderPassScheduler::BeginView(ViewType type, const RenderExtent& viewExtent, bool advancesMainTemporal)
	{
		ViewContext view{};
		if (!frameObserved || !viewExtent.Valid())
			return view;
		// EarlyPrepass and Prepass are separate Skyrim hooks for the same main view.
		// Keep one publication token across them so early resources remain current.
		if (type == ViewType::MainWorld && currentView.type == type &&
			currentView.Valid() && currentView.token.frame == frameIndex &&
			currentView.token.resources.value == resourceEpoch &&
			currentView.extent.SameAllocation(viewExtent) &&
			currentView.extent.SameActiveRegion(viewExtent)) {
			currentView.advancesMainTemporal |= advancesMainTemporal;
			return currentView;
		}
		view.type = type;
		view.extent = viewExtent;
		view.advancesMainTemporal = type == ViewType::MainWorld && advancesMainTemporal;
		view.token = { frameIndex, { resourceEpoch }, type, ++nextViewSerial };
		SetViewContext(view);
		return currentView;
	}

	void RenderPassScheduler::SetViewContext(const ViewContext& view)
	{
		if (!view.Valid() || view.token.frame != frameIndex || view.token.resources.value != resourceEpoch ||
			(view.type == ViewType::MainWorld && !view.extent.SameAllocation(extent))) {
			logger::error("[PIXL Scheduler] Rejected stale or incompatible view context");
			return;
		}
		currentView = view;
		depthViews.clear();
	}

	void RenderPassScheduler::PublishDepth(DepthView depth)
	{
		if (!currentView.Valid() || !depth.ValidFor(currentView.token) || !depth.extent.SameAllocation(currentView.extent)) {
			logger::error("[PIXL Scheduler] Rejected stale or incompatible depth publication");
			return;
		}
		if (const auto found = std::ranges::find_if(depthViews, [&](const DepthView& existing) { return existing.epoch == depth.epoch; });
			found != depthViews.end()) {
			*found = std::move(depth);
		} else if (depthViews.size() < 8) {
			depthViews.push_back(std::move(depth));
		} else {
			logger::error("[PIXL Scheduler] Depth publication capacity exceeded");
		}
	}

	const DepthView* RenderPassScheduler::GetDepth(DepthEpoch epoch) const noexcept
	{
		for (const auto& depth : depthViews) {
			if ((epoch == DepthEpoch::None || depth.epoch == epoch) && depth.ValidFor(currentView.token, epoch))
				return &depth;
		}
		return nullptr;
	}

	void RenderPassScheduler::InvalidateHistory(std::string_view reason)
	{
		historyValid = false;
		++historyEpoch;
		historyReason.assign(reason);
		// TemporalContext receives typed continuity events from State. Do not turn
		// this legacy scheduler notification into an indiscriminate ModuleReset;
		// histories such as world-space caches deliberately survive camera cuts.
		LightTransportWorld::Get().Invalidate(reason);
		VisibilityContext::Get().Invalidate(reason);
		Notify(SchedulerEvent::HistoryInvalidated);
	}

	void RenderPassScheduler::NotifyResourcesRecreated()
	{
		++resourceEpoch;
		currentView = {};
		depthViews.clear();
		OpticalCompositeQueue::Get().Invalidate();
		GPUResourceServices::Get().OnResourcesRecreated(globals::d3d::device, globals::d3d::context);
		LightTransportWorld::Get().Invalidate("renderer resources recreated");
		TemporalContext::Get().Invalidate(TemporalInvalidationReason::DeviceReset, "renderer resources recreated");
		TemporalValidityGPU::Get().Invalidate();
		PixelAnnotations::Get().Invalidate();
		ReconstructionContext::Get().Invalidate();
		Notify(SchedulerEvent::ResourcesRecreated);
	}

	void RenderPassScheduler::Notify(SchedulerEvent event)
	{
		for (auto& record : passes) {
			if (!record.failed && record.desc.notify) {
				try {
					record.desc.notify(event);
				} catch (const std::exception& e) {
					logger::error("[PIXL Scheduler] Notification failed for '{}': {}", record.desc.name, e.what());
				}
			}
		}
	}

	std::vector<PassDiagnostics> RenderPassScheduler::GetDiagnostics() const
	{
		std::vector<PassDiagnostics> diagnostics;
		diagnostics.reserve(passes.size());
		for (const auto& record : passes) {
			const auto& pass = record.desc;
			diagnostics.push_back({ pass.id,
				pass.name,
				pass.owner ? pass.owner->GetShortName() : "Renderer",
				pass.phase,
				pass.executionPoint,
				pass.reads,
				pass.writes,
				pass.temporal,
				pass.optional,
				pass.enabled,
				record.failed,
				pass.profilingEnabled,
				record.lastCpuMs,
				record.invocationCount,
				record.failureReason });
		}
		return diagnostics;
	}

	PassId RenderPassScheduler::MakePassId(std::string_view name, PassExecutionPoint point) noexcept
	{
		std::uint64_t hash = kFnvOffset;
		for (const auto c : name) {
			hash ^= static_cast<unsigned char>(c);
			hash *= kFnvPrime;
		}
		hash ^= static_cast<std::uint8_t>(point);
		hash *= kFnvPrime;
		return hash == 0 ? 1 : hash;
	}

	std::string_view RenderPassScheduler::ToString(RenderPhase phase) noexcept
	{
		switch (phase) {
		case RenderPhase::FrameBegin: return "Frame Begin";
		case RenderPhase::SceneAcquire: return "Scene Acquire";
		case RenderPhase::DepthPreparation: return "Depth Preparation";
		case RenderPhase::TerrainPreparation: return "Terrain Preparation";
		case RenderPhase::LightingPreparation: return "Lighting Preparation";
		case RenderPhase::IndirectLighting: return "Indirect Lighting";
		case RenderPhase::Atmosphere: return "Atmosphere";
		case RenderPhase::Water: return "Water";
		case RenderPhase::Transparency: return "Transparency";
		case RenderPhase::Reconstruction: return "Reconstruction";
		case RenderPhase::Presentation: return "Presentation";
		case RenderPhase::FrameEnd: return "Frame End";
		}
		return "Unknown";
	}

	std::string_view RenderPassScheduler::ToString(PassExecutionPoint point) noexcept
	{
		switch (point) {
		case PassExecutionPoint::ReflectionsPrepass: return "Reflections Prepass";
		case PassExecutionPoint::EarlyPrepass: return "Early Prepass";
		case PassExecutionPoint::Prepass: return "Prepass";
		}
		return "Unknown";
	}

	std::string_view RenderPassScheduler::ToString(RenderResource resource) noexcept
	{
		switch (resource) {
		case RenderResource::SharedFrameData: return "Shared Frame Data";
		case RenderResource::SceneColor: return "Scene Color";
		case RenderResource::SceneDepth: return "Scene Depth";
		case RenderResource::GBuffer: return "GBuffer";
		case RenderResource::DirectionalShadows: return "Directional Shadows";
		case RenderResource::ReflectionScene: return "Reflection Scene";
		case RenderResource::ModuleOwnedResources: return "Module-Owned Resources";
		case RenderResource::TemporalHistory: return "Temporal History";
		}
		return "Unknown";
	}
}
