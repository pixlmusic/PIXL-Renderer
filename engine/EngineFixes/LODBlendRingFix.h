// PIXL Renderer - terrain LOD blend-ring stability fix.
// Based on the LOD blend-ring behavior documented in Community Shaders PR #2894.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "EngineFix.h"

/**
 * @brief Keeps Skyrim's terrain LOD blend ring stable during cell-grid shifts.
 *
 * The engine advances this ring using game time. Owning its center on the frame
 * clock keeps the blend transition moving in paused-camera workflows and avoids
 * the terrain transition lagging behind the loaded exterior grid.
 */
struct LODBlendRingFix : EngineFix
{
	std::string GetName() override { return "LOD Blend Ring Fix"; }
	void Install() override;

	static LODBlendRingFix& GetInstance();

	/** @brief Updates the ring once per rendered frame on Skyrim's main thread. */
	void Update();

private:
	void EndSlide();

	bool* blendInProgress{};
	RE::Setting* gridsToLoad{};
	float2 center{};
	float2 gridCenter{};
	float2 slideOrigin{};
	float slideProgress{};
	bool sliding{};
	bool hasCenter{};
};
