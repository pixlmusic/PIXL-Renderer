// PIXL Renderer - terrain LOD blend-ring stability fix.
// Based on the LOD blend-ring behavior documented in Community Shaders PR #2894.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "LODBlendRingFix.h"

#include "Globals.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
	// Keep these thresholds aligned with the terrain LOD blend band in Lighting.hlsl.
	constexpr float kBandOuterRadius = 9625.59961f;
	constexpr float kBandInnerRadius = kBandOuterRadius - 1.0f / 0.000375600968f;
	constexpr float kHalfCellSize = 2048.0f;
	constexpr float kSlideSeconds = 0.25f;

	constexpr float LeanSlack(std::uint32_t gridsToLoad)
	{
		return std::max(0.0f, static_cast<float>(gridsToLoad) * kHalfCellSize - kBandOuterRadius);
	}

	// In the default 5x5 exterior grid, LOD blending skips the plus-shaped inner
	// cells, so the leaned band must remain outside those cells.
	static_assert(kBandInnerRadius - LeanSlack(5) > 3.0f * kHalfCellSize);

	float2 ToFloat2(const RE::NiPoint2& point)
	{
		return { point.x, point.y };
	}

	bool IsFinite(const float2& point)
	{
		return std::isfinite(point.x) && std::isfinite(point.y);
	}
}

LODBlendRingFix& LODBlendRingFix::GetInstance()
{
	static LODBlendRingFix instance;
	return instance;
}

void LODBlendRingFix::Install()
{
	// TES raises this flag while it updates exterior cells and defers LOD blend
	// reflagging until the transition is complete.
	blendInProgress = reinterpret_cast<bool*>(REL::RelocationID(527641, 414593).address());
	if (!blendInProgress)
		logger::error("[Engine Fixes] Could not resolve Skyrim's LOD blend transition flag");
}

void LODBlendRingFix::EndSlide()
{
	if (blendInProgress)
		*blendInProgress = false;
	sliding = false;
	slideProgress = 0.0f;
}

void LODBlendRingFix::Update()
{
	auto* tes = globals::game::tes;
	auto* player = globals::game::player;
	auto* state = globals::game::smState;
	if (!blendInProgress || !tes || !player || !state || tes->interiorCell ||
		!tes->GetRuntimeData2().worldSpace) {
		if (sliding)
			EndSlide();
		hasCenter = false;
		return;
	}

	const float2 engineCenter = ToFloat2(state->gridArrayCenter);
	const auto& playerPosition = player->GetPosition();
	const float2 playerXY{ playerPosition.x, playerPosition.y };
	if (!IsFinite(engineCenter) || !IsFinite(playerXY)) {
		if (sliding)
			EndSlide();
		hasCenter = false;
		return;
	}

	if (!hasCenter || engineCenter != center) {
		gridCenter = engineCenter;
		// On loads, teleports and multi-cell jumps Skyrim writes old == new. Those
		// transitions should snap to the new valid ring rather than animate across
		// unloaded terrain.
		sliding = hasCenter && ToFloat2(state->oldGridArrayCenter) != engineCenter;
		if (sliding) {
			slideOrigin = center;
			slideProgress = 0.0f;
		} else {
			EndSlide();
		}
		hasCenter = true;
	}

	if (!gridsToLoad)
		gridsToLoad = RE::GetINISetting("uGridsToLoad:General");
	const float slack = gridsToLoad ? LeanSlack(gridsToLoad->data.u) : 0.0f;
	float2 lean = playerXY - gridCenter;
	lean.Clamp(float2(-slack), float2(slack));
	const float2 desired = gridCenter + lean;

	if (sliding) {
		const double rawDelta = RE::GetSecondsSinceLastFrame();
		const float frameDelta = std::isfinite(rawDelta)
			? std::clamp(static_cast<float>(rawDelta), 0.0f, 0.10f)
			: 0.0f;
		slideProgress = std::min(1.0f, slideProgress + frameDelta / kSlideSeconds);
		center = float2::SmoothStep(slideOrigin, desired, slideProgress);
		if (slideProgress >= 1.0f)
			EndSlide();
	} else {
		center = desired;
	}

	// The blend ring is visual renderer state only. Keep Skyrim's cell grid,
	// player/camera transforms and world positions untouched.
	state->oldGridArrayCenter = state->gridArrayCenter = RE::NiPoint2(center.x, center.y);
	// Hold the engine's game-time interpolation at t=0 while this fix owns center.
	state->gridArrayLerpStart = std::numeric_limits<float>::max();
}
