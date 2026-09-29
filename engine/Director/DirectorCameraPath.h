#pragma once

// PIXL Renderer - Director camera-path data model.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional permissions are described in the repository EXCEPTIONS.md.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace DirectorCameraPath
{
	// Version 2 adds the explicit path-facing rotation option.  Version 1 paths
	// remain accepted so previously saved routes continue to load unchanged.
	inline constexpr std::uint32_t kSchemaVersion = 2;

	struct Vec3
	{
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
	};

	struct Quaternion
	{
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
		float w = 1.0f;
	};

	enum class Easing : std::uint8_t
	{
		Linear,
		EaseIn,
		EaseOut,
		EaseInOut
	};

	struct Point
	{
		std::uint32_t id = 0;
		Vec3 worldPosition{};
		Quaternion worldRotation{};
		bool useLookAt = false;
		// Uses the spline tangent for smooth pitch/yaw.  This is deliberately
		// independent from look-at so an author can choose either shot framing.
		bool followPathDirection = false;
		Vec3 lookAtPosition{};
		float fieldOfView = 75.0f;
		float speed = 240.0f;
		float holdDuration = 0.0f;
		Easing positionEasing = Easing::Linear;
		Easing rotationEasing = Easing::EaseInOut;
		float rollDegrees = 0.0f;
		bool autoBank = false;
		float bankStrength = 1.0f;
		std::optional<float> exposure{};
		std::optional<float> focusDistance{};
	};

	struct Pose
	{
		Vec3 worldPosition{};
		Quaternion worldRotation{};
		float fieldOfView = 75.0f;
		float bankDegrees = 0.0f;
		bool valid = false;
	};

	/**
	 * Native Director path evaluator. Rebuild() performs all adaptive arc-length
	 * work; Evaluate*() are allocation-free and safe for per-frame preview.
	 */
	class Path
	{
	public:
		std::string name = "Untitled path";
		// Zero identifies a route saved before cell-aware Director paths existed.
		std::uint32_t cellFormID = 0;
		bool closed = false;
		std::optional<float> explicitDuration{};
		std::vector<Point> points{};

		[[nodiscard]] bool Rebuild(std::string* error = nullptr);
		[[nodiscard]] bool IsValid() const noexcept;
		[[nodiscard]] float GetLength() const noexcept;
		[[nodiscard]] float GetDuration() const noexcept;
		// Point arrival times are rebuilt with the path and consumed only by the
		// authoring timeline. The caller owns the output storage so preview frames
		// do not allocate while drawing point markers.
		void GetPointTimelineTimes(std::vector<float>& output) const;
		// Editor-only route samples. Call this after Rebuild() and retain the
		// output in UI state; playback never allocates this data per frame.
		void GetPreviewSamples(std::vector<Vec3>& output, std::uint32_t samplesPerSegment = 24) const;
		[[nodiscard]] Pose EvaluateNormalized(float normalizedDistance) const noexcept;
		[[nodiscard]] Pose EvaluateTime(float seconds, bool loop) const noexcept;
		[[nodiscard]] nlohmann::json ToJson() const;
		[[nodiscard]] static std::optional<Path> FromJson(const nlohmann::json& document, std::string* error = nullptr);

	private:
		struct ArcSample
		{
			float parameter = 0.0f;
			float distance = 0.0f;
		};

		struct Segment
		{
			std::vector<ArcSample> samples{};
			float length = 0.0f;
			float duration = 0.0f;
		};

		std::vector<Segment> segments{};
		// Computed at rebuild time. Tight, short corners have a safe traversal
		// speed without changing the user's authored speed or saved path format.
		std::vector<float> effectiveSpeeds{};
		float totalLength = 0.0f;
		float totalDuration = 0.0f;

		[[nodiscard]] Vec3 EvaluatePosition(std::size_t segment, float parameter) const noexcept;
		[[nodiscard]] Vec3 EvaluateTangent(std::size_t segment, float parameter) const noexcept;
		[[nodiscard]] Pose EvaluateSegment(std::size_t segment, float parameter) const noexcept;
		[[nodiscard]] float ParameterAtSegmentDistance(const Segment& segment, float distance) const noexcept;
		[[nodiscard]] std::size_t SegmentCount() const noexcept;
	};

	/** Cheap non-runtime coverage for degenerate points, arc-length motion and look-at stability. */
	[[nodiscard]] bool RunDeterministicSelfTest(std::string* failure = nullptr);
}
