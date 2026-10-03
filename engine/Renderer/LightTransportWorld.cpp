// PIXL Renderer - shared frame-scoped spatial-lighting publication service.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later

#include "LightTransportWorld.h"

namespace PIXL::Renderer
{
	LightTransportWorld& LightTransportWorld::Get()
	{
		static LightTransportWorld instance;
		return instance;
	}

	void LightTransportWorld::BeginFrame(std::uint64_t nextFrame)
	{
		std::scoped_lock lock(mutex);
		if (frame == nextFrame)
			return;
		frame = nextFrame;
		localLights = {};
		for (auto& probe : probes)
			probe = {};
		invalidationReason.clear();
	}

	void LightTransportWorld::Invalidate(std::string_view reason)
	{
		std::scoped_lock lock(mutex);
		++epoch;
		localLights = {};
		for (auto& probe : probes)
			probe = {};
		invalidationReason.assign(reason);
	}

	void LightTransportWorld::PublishLocalLights(
		ID3D11ShaderResourceView* lights,
		ID3D11ShaderResourceView* lightIndices,
		ID3D11ShaderResourceView* lightGrid,
		std::uint32_t emitterStart,
		std::uint32_t emitterCount,
		std::uint32_t totalLightCount)
	{
		std::scoped_lock lock(mutex);
		localLights = {};
		if (!lights || !lightIndices || !lightGrid)
			return;
		localLights.lights.copy_from(lights);
		localLights.lightIndices.copy_from(lightIndices);
		localLights.lightGrid.copy_from(lightGrid);
		localLights.emitterStart = emitterStart;
		localLights.emitterCount = emitterCount;
		localLights.totalLightCount = totalLightCount;
		localLights.frame = frame;
		localLights.epoch = epoch;
	}

	LocalLightingView LightTransportWorld::AcquireLocalLights() const
	{
		std::scoped_lock lock(mutex);
		return localLights;
	}

	void LightTransportWorld::PublishProbe(
		ProbeKind kind,
		ID3D11ShaderResourceView* resource,
		std::uint32_t width,
		std::uint32_t height,
		std::uint32_t depth)
	{
		const auto index = static_cast<std::size_t>(kind);
		if (index >= probes.size())
			return;
		std::scoped_lock lock(mutex);
		auto& probe = probes[index];
		probe = {};
		if (!resource)
			return;
		probe.resource.copy_from(resource);
		probe.width = width;
		probe.height = height;
		probe.depth = depth;
		probe.frame = frame;
		probe.epoch = epoch;
	}

	ProbeView LightTransportWorld::AcquireProbe(ProbeKind kind) const
	{
		const auto index = static_cast<std::size_t>(kind);
		if (index >= probes.size())
			return {};
		std::scoped_lock lock(mutex);
		return probes[index];
	}

	LightTransportDiagnostics LightTransportWorld::GetDiagnostics() const
	{
		std::scoped_lock lock(mutex);
		std::uint32_t validProbeCount = 0;
		for (const auto& probe : probes) {
			if (probe.resource && probe.frame == frame)
				++validProbeCount;
		}
		return {
			.frame = frame,
			.epoch = epoch,
			.localLightCount = localLights.frame == frame ? localLights.totalLightCount : 0u,
			.localEmitterCount = localLights.frame == frame ? localLights.emitterCount : 0u,
			.validProbeCount = validProbeCount,
			.invalidationReason = invalidationReason
		};
	}

	std::string_view LightTransportWorld::ToString(ProbeKind kind)
	{
		switch (kind) {
		case ProbeKind::AmbientEnvironmentSH: return "Ambient environment SH";
		case ProbeKind::AmbientSkySH: return "Ambient sky SH";
		case ProbeKind::SkyVisibility: return "Sky visibility";
		case ProbeKind::WorldIrradiance: return "World irradiance";
		default: return "Unknown";
		}
	}
}
