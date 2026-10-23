// PIXL Ocean Dynamics gameplay sampling. CPU consumers only see completed FFT
// readbacks, never a D3D resource or a render-thread pointer.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "OceanDynamics.h"

#include "Globals.h"
#include "Modules/TerrainOcclusion.h"
#include "Modules/Waterbody.h"
#include "OceanDynamicsShore.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>

namespace
{
	constexpr std::uint32_t kFirstGameplayCascade = 3;
	constexpr std::array<float, 1> kExtent{ 32768.0f };
	constexpr float kPi = 3.14159265358979323846f;

	float Smoothstep(float a, float b, float x) noexcept
	{
		const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

	float Wrap(float x, float period) noexcept
	{
		const float value = std::fmod(x, period);
		return value < 0.0f ? value + period : value;
	}

	struct Wave
	{
		float x{}, y{}, z{};
	};

	Wave SampleField(const std::array<float, OceanDynamics::Resolution * OceanDynamics::Resolution * 4>& texels,
		float x, float y) noexcept
	{
		Wave result{};
		for (std::uint32_t cascade = 0; cascade < 1; ++cascade) {
			const float px = Wrap((x / kExtent[cascade] + 0.5f) * OceanDynamics::Resolution - 0.5f,
				static_cast<float>(OceanDynamics::Resolution));
			const float py = Wrap((y / kExtent[cascade] + 0.5f) * OceanDynamics::Resolution - 0.5f,
				static_cast<float>(OceanDynamics::Resolution));
			const auto x0 = static_cast<std::uint32_t>(px);
			const auto y0 = static_cast<std::uint32_t>(py);
			const auto x1 = (x0 + 1) % OceanDynamics::Resolution;
			const auto y1 = (y0 + 1) % OceanDynamics::Resolution;
			const float fx = px - x0, fy = py - y0;
			auto at = [&](std::uint32_t sx, std::uint32_t sy, std::uint32_t channel) {
				const auto index = (((cascade * OceanDynamics::Resolution + sy) *
					OceanDynamics::Resolution + sx) * 4 + channel);
				return texels[index];
			};
			auto channel = [&](std::uint32_t c) {
				return std::lerp(std::lerp(at(x0, y0, c), at(x1, y0, c), fx),
					std::lerp(at(x0, y1, c), at(x1, y1, c), fx), fy);
			};
			const float cascadeWeight = 1.0f; // Match the native Water VS long swell.
			result.x += channel(0) * cascadeWeight;
			result.y += channel(1) * cascadeWeight;
			result.z += channel(2) * cascadeWeight;
		}
		result.x = std::clamp(result.x, -35.0f, 35.0f);
		result.y = std::clamp(result.y, -35.0f, 35.0f);
		result.z = std::clamp(result.z, -100.0f, 100.0f);
		return result;
	}

	struct Shore
	{
		float depth{};
		float strength{};
		float directionX{}, directionY{}, slope{};
		float slopeX{}, slopeY{};
		bool valid{};
	};

	Shore SampleShore(const TerrainOcclusion::OceanHeightfield* map,
		float x, float y, float seaLevel) noexcept
	{
		if (!map)
			return {};
		const float floor = map->Sample(x, y);
		if (!std::isfinite(floor))
			return {};
		const auto& bounds = map->metadata;
		const float stepX = std::max(std::abs(bounds.pos1.x - bounds.pos0.x) / map->width, 1.0f);
		const float stepY = std::max(std::abs(bounds.pos1.y - bounds.pos0.y) / map->height, 1.0f);
		const float dx = (map->Sample(x + stepX, y) - map->Sample(x - stepX, y)) / (2.0f * stepX);
		const float dy = (map->Sample(x, y + stepY) - map->Sample(x, y - stepY)) / (2.0f * stepY);
		const float slope = std::isfinite(dx) && std::isfinite(dy) ? std::hypot(dx, dy) : 0.0f;
		const float depth = seaLevel - floor;
		const float wet = Smoothstep(-8.0f, 55.0f, depth);
		const float surf = Smoothstep(4.0f, 36.0f, depth) *
			(1.0f - Smoothstep(210.0f, 560.0f, depth));
		const float strength = surf * wet;
		return { depth, strength, slope > 1e-5f ? dx / slope : 0.0f,
			slope > 1e-5f ? dy / slope : 0.0f, slope, dx, dy, true };
	}

	Wave SampleQueryWave(const std::array<float, OceanDynamics::Resolution * OceanDynamics::Resolution * 4>& field,
		float x, float y) noexcept
	{
		// Keep gameplay queries on the same continuous world-space FFT phase as
		// the visible mesh; terrain limits height without shifting wave crests.
		return SampleField(field, x, y);
	}

	bool SampleCageRadii(const OceanDynamics::WaveCageField* field, float x, float y,
		OceanShore::CageRadii& radii) noexcept
	{
		if (!field || field->radii.size() != static_cast<std::size_t>(
			OceanDynamics::WaveCageField::Resolution) * OceanDynamics::WaveCageField::Resolution)
			return false;
		const float spanX = field->bounds[2] - field->bounds[0];
		const float spanY = field->bounds[3] - field->bounds[1];
		if (std::abs(spanX) < 1.0f || std::abs(spanY) < 1.0f)
			return false;
		const float u = (x - field->bounds[0]) / spanX;
		const float v = (y - field->bounds[1]) / spanY;
		if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)
			return false;
		const float px = std::clamp(u * OceanDynamics::WaveCageField::Resolution - 0.5f,
			0.0f, static_cast<float>(OceanDynamics::WaveCageField::Resolution - 1));
		const float py = std::clamp(v * OceanDynamics::WaveCageField::Resolution - 0.5f,
			0.0f, static_cast<float>(OceanDynamics::WaveCageField::Resolution - 1));
		const auto x0 = static_cast<std::uint32_t>(px);
		const auto y0 = static_cast<std::uint32_t>(py);
		const auto x1 = std::min(x0 + 1, OceanDynamics::WaveCageField::Resolution - 1);
		const auto y1 = std::min(y0 + 1, OceanDynamics::WaveCageField::Resolution - 1);
		const float fx = px - x0, fy = py - y0;
		const auto at = [&](std::uint32_t ix, std::uint32_t iy) -> const std::array<float, 4>& {
			return field->radii[static_cast<std::size_t>(iy) * OceanDynamics::WaveCageField::Resolution + ix];
		};
		const auto& a = at(x0, y0);
		const auto& b = at(x1, y0);
		const auto& c = at(x0, y1);
		const auto& d = at(x1, y1);
		const std::array<float, 3> result{
			std::lerp(std::lerp(a[0], b[0], fx), std::lerp(c[0], d[0], fx), fy),
			std::lerp(std::lerp(a[1], b[1], fx), std::lerp(c[1], d[1], fx), fy),
			std::lerp(std::lerp(a[2], b[2], fx), std::lerp(c[2], d[2], fx), fy)
		};
		const float valid = std::lerp(std::lerp(a[3], b[3], fx), std::lerp(c[3], d[3], fx), fy);
		if (valid < 0.5f)
			return false;
		radii = { result[0], result[1], result[2] };
		return true;
	}

	bool SampleCageFrame(const TerrainOcclusion::OceanHeightfield* terrain, float x, float y,
		float seaLevel, OceanShore::TerrainFrame& frame) noexcept
	{
		if (!terrain || terrain->width == 0 || terrain->height == 0)
			return false;
		const auto& bounds = terrain->metadata;
		const float step = std::max({ 64.0f,
			std::abs(bounds.pos1.x - bounds.pos0.x) / terrain->width * 2.0f,
			std::abs(bounds.pos1.y - bounds.pos0.y) / terrain->height * 2.0f });
		const float floor = terrain->Sample(x, y);
		const float left = terrain->Sample(x - step, y);
		const float right = terrain->Sample(x + step, y);
		const float down = terrain->Sample(x, y - step);
		const float up = terrain->Sample(x, y + step);
		if (!std::isfinite(floor))
			return false;
		const float slopeX = std::isfinite(left) && std::isfinite(right)
			? (right - left) / (2.0f * step) : 0.0f;
		const float slopeY = std::isfinite(down) && std::isfinite(up)
			? (up - down) / (2.0f * step) : 0.0f;
		frame = OceanShore::BuildTerrainFrame(slopeX, slopeY, seaLevel - floor);
		return true;
	}

	Wave Transform(Wave wave, const Shore& shore, float swellPeriod, float shoalingStrength,
		const TerrainOcclusion::OceanHeightfield* terrain,
		const OceanDynamics::WaveCageField* cageField, float x, float y,
		float seaLevel, float cageBlend) noexcept
	{
		const float wet = shore.valid ? Smoothstep(-24.0f, 48.0f, shore.depth) : 1.0f;
		const float depth = shore.valid ? shore.depth : 50000.0f;
		const float height = OceanShore::TransformHeight(wave.z, depth,
			swellPeriod, shoalingStrength, wet);
		Wave result{ wave.x * wet, wave.y * wet, height };
		OceanShore::CageRadii radii{};
		OceanShore::TerrainFrame frame{};
		if (shore.valid && SampleCageRadii(cageField, x, y, radii) &&
			SampleCageFrame(terrain, x, y, seaLevel, frame)) {
			const float shoaling = OceanShore::ShoalingScale(depth, swellPeriod, shoalingStrength);
			const auto mapped = OceanShore::TransformCage(
				{ wave.x * shoaling, wave.y * shoaling, wave.z * shoaling }, frame, radii);
			const float blend = std::clamp(cageBlend, 0.0f, 1.0f);
			result.x = std::lerp(result.x, mapped.x * wet, blend);
			result.y = std::lerp(result.y, mapped.y * wet, blend);
			result.z = std::lerp(result.z, mapped.z * wet, blend);
		}
		// Same shallow coverage constraint as OceanBeachCoverage in HLSL.
		const float positiveDepth = std::max(depth, 0.0f);
		const float orbital = Smoothstep(128.0f, 512.0f, positiveDepth);
		result.x *= orbital;
		result.y *= orbital;
		const float contactHeight = 0.16f * std::max(positiveDepth - 96.0f, 0.0f) *
			Smoothstep(96.0f, 384.0f, positiveDepth);
		result.z = std::clamp(result.z, -contactHeight, contactHeight);
		return result;
	}
}

void OceanDynamics::QueueGameplayReadback(ID3D11DeviceContext* context) noexcept
{
	if (!context || !displacement || !gameplayReadback[0])
		return;
	// Read the long-swell cascade used by native geometry, at 10 Hz.
	// A staging slot is mapped only after a later simulation has run. DO_NOT_WAIT
	// prevents GPU/CPU synchronization spikes on the renderer thread.
	if (++gameplayReadbackCadence % 3 != 0)
		return;
	for (std::uint32_t slot = 0; slot < gameplayReadback.size(); ++slot) {
		if (!gameplayReadbackPending[slot])
			continue;
		D3D11_MAPPED_SUBRESOURCE mapped[1]{};
		std::uint32_t mappedCount = 0;
		for (; mappedCount < 1; ++mappedCount) {
			const auto hr = context->Map(gameplayReadback[slot].get(), mappedCount,
				D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped[mappedCount]);
			if (FAILED(hr))
				break;
		}
		if (mappedCount == 1) {
			auto field = std::make_unique<QueryField>();
			field->simulationSeconds = gameplayReadbackTimes[slot];
			// Include queued GPU time in the public age, not only time since Map.
			field->readyAt = gameplayReadbackSubmittedAt[slot];
			for (std::uint32_t slice = 0; slice < 1; ++slice)
				for (std::uint32_t row = 0; row < Resolution; ++row)
					std::memcpy(field->texels.data() +
						(static_cast<std::size_t>(slice) * Resolution * Resolution + row * Resolution) * 4,
						static_cast<const std::byte*>(mapped[slice].pData) + row * mapped[slice].RowPitch,
						Resolution * 4 * sizeof(float));
			auto history = std::make_shared<QueryHistory>();
			history->current = std::move(*field);
			if (const auto previous = gameplayHistory.load(std::memory_order_acquire);
				previous && previous->generation == queryGeneration) {
				history->previous = previous->current;
				history->hasPrevious = true;
			}
			history->generation = queryGeneration;
			gameplayHistory.store(std::move(history), std::memory_order_release);
			gameplayReadbackPending[slot] = false;
		}
		for (std::uint32_t slice = 0; slice < mappedCount; ++slice)
			context->Unmap(gameplayReadback[slot].get(), slice);
	}
	const auto slot = gameplayReadbackSlot++ % gameplayReadback.size();
	if (gameplayReadbackPending[slot])
		return;
	for (std::uint32_t slice = 0; slice < 1; ++slice)
		context->CopySubresourceRegion(gameplayReadback[slot].get(), slice, 0, 0, 0,
			displacement->resource.get(), kFirstGameplayCascade + slice, nullptr);
	gameplayReadbackTimes[slot] = simulationTime;
	gameplayReadbackSubmittedAt[slot] = std::chrono::steady_clock::now();
	gameplayReadbackPending[slot] = true;
}

std::uint32_t OceanDynamics::QueryBatch(const PIXL_OceanRequestV1* requests,
	PIXL_OceanSampleV1* samples, std::uint32_t count) const noexcept
{
	if (!requests || !samples || count == 0 || count > 64)
		return 0;
	std::fill_n(samples, count, PIXL_OceanSampleV1{});
	const auto history = gameplayHistory.load(std::memory_order_acquire);
	if (!history || !loaded || !settings.EnableSimulation || !settings.EnableGeometryDisplacement)
		return 0;
	const auto age = std::chrono::duration<float>(
		std::chrono::steady_clock::now() - history->current.readyAt).count();
	if (age > 0.5f)
		return 0;
	const auto* tes = RE::TES::GetSingleton();
	const auto* world = tes ? tes->GetRuntimeData2().worldSpace : nullptr;
	if (!world || (tes && tes->interiorCell))
		return 0;
	const auto map = globals::pipeline::terrainOcclusion.oceanHeightfield.load(std::memory_order_acquire);
	const auto* terrain = map && map->worldspaceFormID == world->GetFormID() ? map.get() : nullptr;
	LocalTerrainData localTerrain{};
	std::vector<LocalTerrainData> tiles;
	{
		std::lock_guard lock(localTerrainMutex);
		if (pendingLocalTerrain.generation != 0 &&
			pendingLocalTerrain.metadata.worldspaceFormID == world->GetFormID())
			localTerrain = pendingLocalTerrain;
		tiles=localTerrainTiles;
	}
	auto terrainAt = [&](float x, float y, float& height) noexcept {
		for(const auto& tile:tiles) {
			const auto& meta=tile.metadata;
			if(meta.worldspaceFormID!=world->GetFormID() || meta.spacing<=1.0f) continue;
			const float gx = (x - meta.originX) / meta.spacing;
			const float gy = (y - meta.originY) / meta.spacing;
			if (gx >= 0.0f && gy >= 0.0f && gx <= 32.0f && gy <= 32.0f) {
				const auto ix = std::min(static_cast<std::uint32_t>(gx), 31u);
				const auto iy = std::min(static_cast<std::uint32_t>(gy), 31u);
				const float fx = std::clamp(gx - ix, 0.0f, 1.0f);
				const float fy = std::clamp(gy - iy, 0.0f, 1.0f);
				const auto at = [&](std::uint32_t px, std::uint32_t py) {
					return tile.heights[py * 33u + px];
				};
				const float h00 = at(ix, iy), h10 = at(ix + 1u, iy);
				const float h01 = at(ix, iy + 1u), h11 = at(ix + 1u, iy + 1u);
				if (std::isfinite(h00) && std::isfinite(h10) &&
					std::isfinite(h01) && std::isfinite(h11) &&
					h00 > -30000.0f && h10 > -30000.0f &&
					h01 > -30000.0f && h11 > -30000.0f) {
					height = std::lerp(std::lerp(h00, h10, fx), std::lerp(h01, h11, fx), fy);
					return true;
				}
			}
		}
		if (terrain) {
			height = terrain->Sample(x, y);
			return std::isfinite(height);
		}
		return false;
	};
	auto sampleShore = [&](float x, float y, float seaLevel) noexcept {
		float floor{};
		if (!terrainAt(x, y, floor))
			return Shore{};
		const float step = localTerrain.generation != 0 ? localTerrain.metadata.spacing : 128.0f;
		float left{}, right{}, down{}, up{};
		const bool gradients = terrainAt(x - step, y, left) && terrainAt(x + step, y, right) &&
			terrainAt(x, y - step, down) && terrainAt(x, y + step, up);
		const float dx = gradients ? (right - left) / (2.0f * step) : 0.0f;
		const float dy = gradients ? (up - down) / (2.0f * step) : 0.0f;
		const float slope = std::hypot(dx, dy);
		const float depth = seaLevel - floor;
		const float wet = Smoothstep(-8.0f, 55.0f, depth);
		const float surf = Smoothstep(4.0f, 36.0f, depth) *
			(1.0f - Smoothstep(210.0f, 560.0f, depth));
		return Shore{ depth, surf * wet, slope > 1e-5f ? dx / slope : 0.0f,
			slope > 1e-5f ? dy / slope : 0.0f, slope, dx, dy, true };
	};
	static std::atomic_bool bathymetryQueryLogged = false;
	if (!bathymetryQueryLogged.exchange(true, std::memory_order_relaxed)) {
		const float firstFloor = terrain ? terrain->Sample(requests[0].worldX, requests[0].worldY) :
			std::numeric_limits<float>::quiet_NaN();
		if (map) {
			logger::info("[PIXL Ocean Dynamics] Gameplay bathymetry query: world {:08X}, map world {:08X} '{}', sample ({:.0f},{:.0f}) floor {:.1f}, bounds X {:.0f}..{:.0f} Y {:.0f}..{:.0f}, matched {}",
				world->GetFormID(), map->worldspaceFormID, map->metadata.worldspace,
				requests[0].worldX, requests[0].worldY, firstFloor,
				map->metadata.pos0.x, map->metadata.pos1.x,
				map->metadata.pos0.y, map->metadata.pos1.y, terrain != nullptr);
		} else {
			logger::warn("[PIXL Ocean Dynamics] Gameplay bathymetry query has no loaded terrain heightfield for world {:08X}",
				world->GetFormID());
		}
	}
	std::uint32_t validCount = 0;
	for (std::uint32_t index = 0; index < count; ++index) {
		const auto& request = requests[index];
		auto& sample = samples[index];
		if (request.worldspaceFormID != world->GetFormID() ||
			!std::isfinite(request.worldX) || !std::isfinite(request.worldY))
			continue;
		float seaLevel{};
		if (!globals::pipeline::waterbody.GetOpenOceanHeight(
			world, request.worldX, request.worldY, seaLevel))
			continue;
		float baseX = request.worldX, baseY = request.worldY;
		const float direction = settings.SwellDirectionDegrees * (kPi / 180.0f);
		const float directionX = std::cos(direction), directionY = std::sin(direction);
		const float shoalingStrength = std::clamp(settings.ShoalingStrength, 0.0f, 1.5f);
		const auto cageOwner = waveCageField.load(std::memory_order_acquire);
		const auto* cage = cageOwner && terrain && cageOwner->worldspaceFormID == world->GetFormID() &&
			std::abs(cageOwner->seaLevel - std::round(seaLevel / 8.0f) * 8.0f) <= 1.0f
			? cageOwner.get() : nullptr;
		const float activeCageBlend = cage ? waveCageBlend.load(std::memory_order_relaxed) : 0.0f;
		auto surfaceAt = [&](const QueryField& field, float x, float y) {
			const auto shore = sampleShore(x, y, seaLevel);
			const auto wave = SampleQueryWave(field.texels, x, y);
			return Transform(wave, shore, settings.SwellPeriod, shoalingStrength,
				terrain, cage, x, y, seaLevel, activeCageBlend);
		};
		const auto shore = sampleShore(baseX, baseY, seaLevel);
		const auto wave = SampleQueryWave(history->current.texels, baseX, baseY);
		const auto surfaceOffset = Transform(wave, shore, settings.SwellPeriod, shoalingStrength,
			terrain, cage, baseX, baseY, seaLevel, activeCageBlend);
		sample.validityFlags = PIXL_OCEAN_VALID_SURFACE;
		sample.worldspaceFormID = request.worldspaceFormID;
		sample.generation = history->generation;
		sample.simulationSeconds = history->current.simulationSeconds;
		sample.ageSeconds = age;
		sample.waterHeight = seaLevel + surfaceOffset.z;
		const float derivativeStep = 48.0f;
		const auto left = surfaceAt(history->current, baseX - derivativeStep, baseY);
		const auto right = surfaceAt(history->current, baseX + derivativeStep, baseY);
		const auto down = surfaceAt(history->current, baseX, baseY - derivativeStep);
		const auto up = surfaceAt(history->current, baseX, baseY + derivativeStep);
		const float inverseDiameter = 1.0f / (2.0f * derivativeStep);
		const OceanShore::Direction3 tangentX{
			1.0f + (right.x - left.x) * inverseDiameter,
			(right.y - left.y) * inverseDiameter,
			(right.z - left.z) * inverseDiameter };
		const OceanShore::Direction3 tangentY{
			(up.x - down.x) * inverseDiameter,
			1.0f + (up.y - down.y) * inverseDiameter,
			(up.z - down.z) * inverseDiameter };
		auto surfaceNormal = OceanShore::SafeNormalize(OceanShore::Cross(tangentX, tangentY),
			{ 0.0f, 0.0f, 1.0f });
		if (surfaceNormal.z < 0.0f)
			surfaceNormal = OceanShore::Scale(surfaceNormal, -1.0f);
		sample.normal[0] = surfaceNormal.x;
		sample.normal[1] = surfaceNormal.y;
		sample.normal[2] = surfaceNormal.z;
		const float surfaceSlope = std::abs(surfaceNormal.z) > 1.0e-4f
			? std::hypot(surfaceNormal.x / surfaceNormal.z, surfaceNormal.y / surfaceNormal.z)
			: 10.0f;
		if (history->hasPrevious) {
			const float dt = static_cast<float>(history->current.simulationSeconds -
				history->previous.simulationSeconds);
			if (dt > 1e-3f && dt < 1.0f) {
				const auto previous = surfaceAt(history->previous, baseX, baseY);
				sample.velocity[0] = std::clamp((surfaceOffset.x - previous.x) / dt, -800.0f, 800.0f);
				sample.velocity[1] = std::clamp((surfaceOffset.y - previous.y) / dt, -800.0f, 800.0f);
				sample.velocity[2] = std::clamp((surfaceOffset.z - previous.z) / dt, -800.0f, 800.0f);
			}
		}
		const OceanShore::Direction2 waveDirection{ directionX, directionY };
		sample.waveDirection[0] = waveDirection.x;
		sample.waveDirection[1] = waveDirection.y;
		if (shore.valid) {
			sample.validityFlags |= PIXL_OCEAN_VALID_DEPTH | PIXL_OCEAN_VALID_BREAKER;
			sample.waterDepth = shore.depth;
			sample.shoreDirection[0] = shore.directionX;
			sample.shoreDirection[1] = shore.directionY;
			const float breaking = OceanShore::BreakerActivation(shore.depth, surfaceOffset.z,
				surfaceSlope, shore.slope, settings.BreakerStrength) * shore.strength;
			const float deepSurf = OceanShore::DeepSurfBreaker(shore.depth, surfaceOffset.z,
				surfaceSlope, settings.BreakerStrength);
			const float spilling = 1.0f - Smoothstep(0.018f, 0.045f, shore.slope);
			const float surging = Smoothstep(0.075f, 0.15f, shore.slope);
			const float plunging = std::clamp(1.0f - spilling - surging, 0.0f, 1.0f);
			const float profile = spilling * 0.45f + plunging + surging * 0.68f;
			const float lip = std::clamp(breaking * profile, 0.0f, 1.0f);
			const float forwardCurl = lip * (0.20f + plunging * 0.38f + surging * 0.16f);
			sample.breakerStrength = std::max(lip, deepSurf);
			const float deepMomentum = deepSurf * (12.0f + deepSurf * 18.0f);
			sample.breakerMomentum[0] = shore.directionX * lip * (18.0f + forwardCurl * 24.0f) +
				waveDirection.x * deepMomentum;
			sample.breakerMomentum[1] = shore.directionY * lip * (18.0f + forwardCurl * 24.0f) +
				waveDirection.y * deepMomentum;
			sample.foam = std::max(lip * settings.FoamStrength,
				deepSurf * settings.FoamStrength * 0.62f);
		}
		if (!std::isfinite(sample.waterHeight) ||
			!std::isfinite(sample.normal[2]) || !std::isfinite(sample.velocity[2])) {
			sample = {};
			continue;
		}
		++validCount;
	}
	return validCount;
}

extern "C" __declspec(dllexport) const PIXL_OceanAPI_V1* PIXL_GetOceanAPI_V1() noexcept
{
	static constexpr PIXL_OceanAPI_V1 api{
		1, sizeof(PIXL_OceanRequestV1), sizeof(PIXL_OceanSampleV1), 64,
		[](const PIXL_OceanRequestV1* requests, PIXL_OceanSampleV1* samples,
			std::uint32_t count) noexcept -> std::uint32_t {
			return globals::pipeline::oceanDynamics.QueryBatch(requests, samples, count);
		}
	};
	return &api;
}

extern "C" __declspec(dllexport) const PIXL_OceanAPI_V2* PIXL_GetOceanAPI_V2() noexcept
{
	static constexpr PIXL_OceanAPI_V2 api{
		2, sizeof(PIXL_OceanRequestV1), sizeof(PIXL_OceanSampleV1), 64,
		33u * 33u, {},
		[](const PIXL_OceanRequestV1* requests, PIXL_OceanSampleV1* samples,
			std::uint32_t count) noexcept -> std::uint32_t {
			return globals::pipeline::oceanDynamics.QueryBatch(requests, samples, count);
		},
		[](const PIXL_OceanTerrainPatchV1* patch, const float* heights,
			std::uint32_t count) noexcept -> bool {
			return globals::pipeline::oceanDynamics.SubmitTerrainPatch(patch, heights, count);
		}
	};
	return &api;
}
