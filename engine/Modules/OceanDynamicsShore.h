// Small CPU-side reference math for the finite-depth FFT cage transfer.
// Keep the matching HLSL helper in OceanWaveBands.hlsli in sync.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>

namespace OceanShore
{
	inline constexpr float kGravity = 9.81f * 70.0f;
	inline constexpr float kPi = 3.14159265358979323846f;

	inline float DeepWaterRatio(float depth, float period) noexcept
	{
		if (!std::isfinite(depth) || !std::isfinite(period) || depth <= 0.0f)
			return 0.035f;
		const float safePeriod = std::clamp(period, 2.0f, 24.0f);
		const float omega = 2.0f * kPi / safePeriod;
		const float k0 = (omega * omega) / kGravity;
		const float kh = k0 * std::clamp(depth, 0.0f, 50000.0f);
		return std::sqrt(std::clamp(std::tanh(kh), 0.001225f, 1.0f));
	}

	inline float ShoreBlend(float depth) noexcept
	{
		if (!std::isfinite(depth))
			return 0.0f;
		const float t = std::clamp((std::max(depth, 0.0f) - 200.0f) / 1400.0f, 0.0f, 1.0f);
		return 1.0f - t * t * (3.0f - 2.0f * t);
	}

	inline float DepthCageRadius(float depth) noexcept
	{
		if (!std::isfinite(depth))
			return 0.0f;
		return std::min(std::max(depth, 0.0f) * 0.10f, 16.0f);
	}

	inline float DepthCageBlend(float shoreAmount, float depth) noexcept
	{
		if (!std::isfinite(depth))
			return 0.0f;
		const float positiveDepth = std::max(depth, 0.0f);
		const float t = std::clamp((positiveDepth - 1200.0f) / 3600.0f, 0.0f, 1.0f);
		const float finiteDepth = 1.0f - t * t * (3.0f - 2.0f * t);
		const float safeShoreAmount = std::clamp(std::isfinite(shoreAmount) ? shoreAmount : 0.0f, 0.0f, 1.0f);
		return std::clamp(std::max(safeShoreAmount, finiteDepth), 0.0f, 1.0f);
	}

	struct Direction2
	{
		float x{}, y{};
	};

	struct Direction3
	{
		float x{}, y{}, z{};
	};

	struct TerrainFrame
	{
		Direction3 shore{};
		Direction3 alongShore{};
		Direction3 normal{ 0.0f, 0.0f, 1.0f };
	};

	inline Direction3 Add(Direction3 a, Direction3 b) noexcept
	{
		return { a.x + b.x, a.y + b.y, a.z + b.z };
	}

	inline Direction3 Scale(Direction3 v, float scale) noexcept
	{
		return { v.x * scale, v.y * scale, v.z * scale };
	}

	inline float Dot(Direction3 a, Direction3 b) noexcept
	{
		return a.x * b.x + a.y * b.y + a.z * b.z;
	}

	inline Direction3 Cross(Direction3 a, Direction3 b) noexcept
	{
		return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
			a.x * b.y - a.y * b.x };
	}

	inline Direction3 SafeNormalize(Direction3 v, Direction3 fallback) noexcept
	{
		const float lengthSq = Dot(v, v);
		if (!std::isfinite(lengthSq) || lengthSq <= 1.0e-12f)
			return fallback;
		return Scale(v, 1.0f / std::sqrt(lengthSq));
	}

	inline TerrainFrame BuildTerrainFrame(float terrainSlopeX, float terrainSlopeY,
		float depth) noexcept
	{
		TerrainFrame frame{};
		frame.normal = SafeNormalize({ -terrainSlopeX, -terrainSlopeY, 1.0f },
			{ 0.0f, 0.0f, 1.0f });
		const float shoreSign = depth >= 0.0f ? 1.0f : -1.0f;
		Direction3 shoreTangent{ terrainSlopeX * shoreSign, terrainSlopeY * shoreSign,
			(terrainSlopeX * terrainSlopeX + terrainSlopeY * terrainSlopeY) * shoreSign };
		if (Dot(shoreTangent, shoreTangent) <= 1.0e-10f)
			shoreTangent = { 1.0f, 0.0f, 0.0f };
		frame.shore = SafeNormalize(shoreTangent, { 1.0f, 0.0f, 0.0f });
		frame.alongShore = SafeNormalize(Cross(frame.normal, frame.shore),
			{ 0.0f, 1.0f, 0.0f });
		// Re-orthogonalize to avoid accumulating finite-difference noise at flat
		// and nearly-flat bathymetry transitions.
		frame.shore = SafeNormalize(Cross(frame.alongShore, frame.normal), frame.shore);
		return frame;
	}

	struct CageRadii
	{
		float shore{};
		float alongShore{};
		float normal{};
	};

	inline Direction3 TransformCage(Direction3 displacement, const TerrainFrame& frame,
		CageRadii radii) noexcept
	{
		const float referenceRadius = std::max({ radii.shore, radii.alongShore, radii.normal });
		const float lengthSq = Dot(displacement, displacement);
		if (!std::isfinite(referenceRadius) || referenceRadius <= 1.0e-3f ||
			!std::isfinite(lengthSq) || lengthSq <= 1.0e-12f)
			return {};

		const float length = std::sqrt(lengthSq);
		// This is the paper's radial tanh soft clip with beta chosen from the
		// local cage scale. It is linear for small motions and bounded by the
		// ellipsoid, while remaining stable in Skyrim's 70-units-per-meter scale.
		const Direction3 normalized = Scale(displacement,
			std::tanh(length / referenceRadius) / length);
		return Add(Add(Scale(frame.shore, radii.shore * Dot(frame.shore, normalized)),
			Scale(frame.alongShore, radii.alongShore * Dot(frame.alongShore, normalized))),
			Scale(frame.normal, radii.normal * Dot(frame.normal, normalized)));
	}

	inline float ShoalingScale(float depth, float period, float intensity) noexcept
	{
		const float shallow = ShoreBlend(depth);
		const float ratio = DeepWaterRatio(depth, period);
		const float energyScale = std::clamp(1.0f / std::sqrt(std::max(ratio, 0.05f)), 1.0f, 1.25f);
		return 1.0f + (energyScale - 1.0f) * std::clamp(shallow *
			std::clamp(std::isfinite(intensity) ? intensity : 0.0f, 0.0f, 1.5f), 0.0f, 1.0f);
	}

	inline float TransformHeight(float height, float depth, float period,
		float intensity, float wetWeight) noexcept
	{
		if (!std::isfinite(height))
			return 0.0f;
		const float shallow = std::clamp(ShoreBlend(depth) *
			std::clamp(std::isfinite(intensity) ? intensity : 0.0f, 0.0f, 1.5f), 0.0f, 1.0f);
		const float shoaled = height * ShoalingScale(depth, period, intensity);
		const float bound = DepthCageRadius(depth);
		const float limited = bound > 1.0e-3f ? bound * std::tanh(shoaled / bound) : 0.0f;
		const float limitBlend = DepthCageBlend(shallow, depth);
		const float transformed = std::lerp(shoaled, limited, limitBlend);
		return transformed * std::clamp(std::isfinite(wetWeight) ? wetWeight : 0.0f, 0.0f, 1.0f);
	}

	inline float BreakerActivation(float depth, float height, float surfaceSlope,
		float terrainSlope, float intensity) noexcept
	{
		if (!std::isfinite(depth) || !std::isfinite(height) ||
			!std::isfinite(surfaceSlope) || !std::isfinite(terrainSlope))
			return 0.0f;
		const auto smooth = [](float a, float b, float x) {
			const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
		};
		const float crest = smooth(-1.5f, 8.0f, height);
		// Keep this threshold in sync with the long-swell finite-difference
		// gradient used by GetOceanShoreFoam in Water.hlsl.
		const float steep = smooth(0.0015f, 0.018f, surfaceSlope);
		const float shallow = 1.0f - smooth(180.0f, 900.0f, depth);
		// xLODGen bathymetry spans the whole world, so its gradients are much
		// softer than local terrain. A lower threshold keeps broad sandy shelves
		// eligible for moving breakers without making deep water foam.
		const float coast = smooth(0.00035f, 0.018f, terrainSlope);
		return std::clamp(crest * steep * shallow * coast *
			std::clamp(std::isfinite(intensity) ? intensity : 0.0f, 0.0f, 2.0f), 0.0f, 1.0f);
	}

	// Deep surfing events use the same long-swell FFT surface as the renderer.
	// This bounded crest-compression proxy supplies a breaker cue to Surfing
	// without inventing a second wave or changing the offshore surface height.
	inline float DeepSurfBreaker(float depth, float height, float surfaceSlope,
		float intensity) noexcept
	{
		if (!std::isfinite(depth) || !std::isfinite(height) ||
			!std::isfinite(surfaceSlope) || depth <= 0.0f)
			return 0.0f;
		const auto smooth = [](float a, float b, float x) {
			const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
		};
		const float deep = smooth(900.0f, 2400.0f, depth);
		const float crest = smooth(10.0f, 42.0f, height);
		const float compression = smooth(0.006f, 0.048f, std::abs(surfaceSlope));
		return std::clamp(deep * crest * compression *
			std::clamp(std::isfinite(intensity) ? intensity : 0.0f, 0.0f, 2.0f), 0.0f, 0.72f);
	}
}
