#include "Director/DirectorCameraPath.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string_view>

#include <nlohmann/json.hpp>

namespace DirectorCameraPath
{
	namespace
	{
		constexpr float kEpsilon = 1.0e-4f;
		constexpr float kArcLengthTolerance = 0.025f;
		constexpr std::uint32_t kMaximumSubdivisionDepth = 12;

		[[nodiscard]] bool IsFinite(float value) noexcept
		{
			return std::isfinite(value);
		}

		[[nodiscard]] bool IsFinite(Vec3 value) noexcept
		{
			return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z);
		}

		[[nodiscard]] float ClampFinite(float value, float minimum, float maximum, float fallback) noexcept
		{
			return IsFinite(value) ? std::clamp(value, minimum, maximum) : fallback;
		}

		[[nodiscard]] Vec3 Add(Vec3 a, Vec3 b) noexcept { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
		[[nodiscard]] Vec3 Subtract(Vec3 a, Vec3 b) noexcept { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
		[[nodiscard]] Vec3 Scale(Vec3 value, float factor) noexcept { return { value.x * factor, value.y * factor, value.z * factor }; }
		[[nodiscard]] float Dot(Vec3 a, Vec3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
		[[nodiscard]] Vec3 Cross(Vec3 a, Vec3 b) noexcept
		{
			return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
		}
		[[nodiscard]] float Length(Vec3 value) noexcept { return std::sqrt(std::max(Dot(value, value), 0.0f)); }
		[[nodiscard]] float Distance(Vec3 a, Vec3 b) noexcept { return Length(Subtract(a, b)); }
		[[nodiscard]] Vec3 Normalize(Vec3 value, Vec3 fallback = { 0.0f, 1.0f, 0.0f }) noexcept
		{
			const float length = Length(value);
			return length > kEpsilon && IsFinite(length) ? Scale(value, 1.0f / length) : fallback;
		}
		[[nodiscard]] Vec3 Lerp(Vec3 a, Vec3 b, float amount) noexcept { return Add(a, Scale(Subtract(b, a), amount)); }

		[[nodiscard]] Quaternion Normalize(Quaternion value) noexcept
		{
			const float lengthSquared = value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w;
			if (!IsFinite(lengthSquared) || lengthSquared <= kEpsilon * kEpsilon)
				return {};
			const float inverseLength = 1.0f / std::sqrt(lengthSquared);
			return { value.x * inverseLength, value.y * inverseLength, value.z * inverseLength, value.w * inverseLength };
		}

		[[nodiscard]] Quaternion Multiply(Quaternion a, Quaternion b) noexcept
		{
			return Normalize({
				a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
				a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
				a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
				a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
			});
		}

		[[nodiscard]] Quaternion Slerp(Quaternion first, Quaternion second, float amount) noexcept
		{
			first = Normalize(first);
			second = Normalize(second);
			float cosine = first.x * second.x + first.y * second.y + first.z * second.z + first.w * second.w;
			if (cosine < 0.0f) {
				second = { -second.x, -second.y, -second.z, -second.w };
				cosine = -cosine;
			}
			if (cosine > 0.9995f) {
				return Normalize({
					first.x + (second.x - first.x) * amount,
					first.y + (second.y - first.y) * amount,
					first.z + (second.z - first.z) * amount,
					first.w + (second.w - first.w) * amount
				});
			}
			const float angle = std::acos(std::clamp(cosine, -1.0f, 1.0f));
			const float sine = std::sin(angle);
			if (std::abs(sine) <= kEpsilon)
				return first;
			const float firstWeight = std::sin((1.0f - amount) * angle) / sine;
			const float secondWeight = std::sin(amount * angle) / sine;
			return Normalize({ first.x * firstWeight + second.x * secondWeight, first.y * firstWeight + second.y * secondWeight,
				first.z * firstWeight + second.z * secondWeight, first.w * firstWeight + second.w * secondWeight });
		}

		[[nodiscard]] Quaternion AxisAngle(Vec3 axis, float radians) noexcept
		{
			axis = Normalize(axis, { 0.0f, 1.0f, 0.0f });
			const float half = radians * 0.5f;
			const float sine = std::sin(half);
			return Normalize({ axis.x * sine, axis.y * sine, axis.z * sine, std::cos(half) });
		}

		[[nodiscard]] Quaternion LookRotation(Vec3 forward, Quaternion fallback, float rollDegrees) noexcept
		{
			forward = Normalize(forward, { 0.0f, 1.0f, 0.0f });
			Vec3 up{ 0.0f, 0.0f, 1.0f };
			if (std::abs(Dot(forward, up)) > 0.995f)
				up = { 0.0f, 1.0f, 0.0f };
			const Vec3 right = Normalize(Cross(up, forward), { 1.0f, 0.0f, 0.0f });
			up = Normalize(Cross(forward, right), { 0.0f, 0.0f, 1.0f });
			const float trace = right.x + forward.y + up.z;
			Quaternion result{};
		if (trace > 0.0f) {
			const float scale = std::sqrt(trace + 1.0f) * 2.0f;
			if (scale <= kEpsilon)
				return Normalize(fallback);
			result = { (forward.z - up.y) / scale, (up.x - right.z) / scale, (right.y - forward.x) / scale, 0.25f * scale };
		} else if (right.x > forward.y && right.x > up.z) {
			const float scale = std::sqrt(std::max(1.0f + right.x - forward.y - up.z, 0.0f)) * 2.0f;
			if (scale <= kEpsilon)
				return Normalize(fallback);
			result = { 0.25f * scale, (right.y + forward.x) / scale, (up.x + right.z) / scale, (forward.z - up.y) / scale };
		} else if (forward.y > up.z) {
			const float scale = std::sqrt(std::max(1.0f + forward.y - right.x - up.z, 0.0f)) * 2.0f;
			if (scale <= kEpsilon)
				return Normalize(fallback);
			result = { (right.y + forward.x) / scale, 0.25f * scale, (forward.z + up.y) / scale, (up.x - right.z) / scale };
		} else {
			const float scale = std::sqrt(std::max(1.0f + up.z - right.x - forward.y, 0.0f)) * 2.0f;
			if (scale <= kEpsilon)
				return Normalize(fallback);
			result = { (up.x + right.z) / scale, (forward.z + up.y) / scale, 0.25f * scale, (right.y - forward.x) / scale };
			}
			result = Normalize(result);
			if (!IsFinite(result.w))
				result = Normalize(fallback);
			return Multiply(result, AxisAngle(forward, rollDegrees * std::numbers::pi_v<float> / 180.0f));
		}

		[[nodiscard]] float Ease(Easing easing, float value) noexcept
		{
			value = std::clamp(value, 0.0f, 1.0f);
			switch (easing) {
			case Easing::EaseIn:
				return value * value;
			case Easing::EaseOut:
				return 1.0f - (1.0f - value) * (1.0f - value);
			case Easing::EaseInOut:
				return value < 0.5f ? 2.0f * value * value : 1.0f - std::pow(-2.0f * value + 2.0f, 2.0f) * 0.5f;
			case Easing::Linear:
			default:
				return value;
			}
		}

		[[nodiscard]] float KnotStep(Vec3 first, Vec3 second) noexcept
		{
			return std::sqrt(std::max(Distance(first, second), kEpsilon));
		}

		[[nodiscard]] Vec3 BlendKnot(Vec3 first, Vec3 second, float firstTime, float secondTime, float time) noexcept
		{
			const float interval = secondTime - firstTime;
			if (interval <= kEpsilon)
				return first;
			return Add(Scale(first, (secondTime - time) / interval), Scale(second, (time - firstTime) / interval));
		}

		[[nodiscard]] Vec3 CentripetalCatmullRom(Vec3 previous, Vec3 start, Vec3 end, Vec3 next, float parameter) noexcept
		{
			const float t0 = 0.0f;
			const float t1 = t0 + KnotStep(previous, start);
			const float t2 = t1 + KnotStep(start, end);
			const float t3 = t2 + KnotStep(end, next);
			const float t = t1 + (t2 - t1) * std::clamp(parameter, 0.0f, 1.0f);
			const Vec3 a1 = BlendKnot(previous, start, t0, t1, t);
			const Vec3 a2 = BlendKnot(start, end, t1, t2, t);
			const Vec3 a3 = BlendKnot(end, next, t2, t3, t);
			const Vec3 b1 = BlendKnot(a1, a2, t0, t2, t);
			const Vec3 b2 = BlendKnot(a2, a3, t1, t3, t);
			return BlendKnot(b1, b2, t1, t2, t);
		}

		[[nodiscard]] const char* EasingName(Easing easing) noexcept
		{
			switch (easing) {
			case Easing::EaseIn: return "easeIn";
			case Easing::EaseOut: return "easeOut";
			case Easing::EaseInOut: return "easeInOut";
			case Easing::Linear:
			default: return "linear";
			}
		}

		[[nodiscard]] Easing ParseEasing(const nlohmann::json& value) noexcept
		{
			if (!value.is_string())
				return Easing::EaseInOut;
			const std::string text = value.get<std::string>();
			if (text == "linear") return Easing::Linear;
			if (text == "easeIn") return Easing::EaseIn;
			if (text == "easeOut") return Easing::EaseOut;
			return Easing::EaseInOut;
		}

		[[nodiscard]] nlohmann::json Encode(Vec3 value)
		{
			return nlohmann::json::array({ value.x, value.y, value.z });
		}

		[[nodiscard]] nlohmann::json Encode(Quaternion value)
		{
			return nlohmann::json::array({ value.x, value.y, value.z, value.w });
		}

		[[nodiscard]] Vec3 DecodeVec3(const nlohmann::json& value)
		{
			if (!value.is_array() || value.size() != 3)
				throw std::runtime_error("expected a 3-component vector");
			return { value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>() };
		}

		[[nodiscard]] Quaternion DecodeQuaternion(const nlohmann::json& value)
		{
			if (!value.is_array() || value.size() != 4)
				throw std::runtime_error("expected a 4-component quaternion");
			return Normalize({ value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>(), value.at(3).get<float>() });
		}
	}

	std::size_t Path::SegmentCount() const noexcept
	{
		if (points.size() < 2)
			return 0;
		return closed ? points.size() : points.size() - 1;
	}

	Vec3 Path::EvaluatePosition(std::size_t segment, float parameter) const noexcept
	{
		const std::size_t count = points.size();
		if (count == 0)
			return {};
		if (count == 1)
			return points.front().worldPosition;
		const std::size_t start = segment % count;
		const std::size_t end = closed ? (start + 1) % count : std::min(start + 1, count - 1);
		if (count == 2)
			return Lerp(points[start].worldPosition, points[end].worldPosition, std::clamp(parameter, 0.0f, 1.0f));
		const std::size_t previous = closed ? (start + count - 1) % count : (start == 0 ? 0 : start - 1);
		const std::size_t next = closed ? (end + 1) % count : std::min(end + 1, count - 1);
		return CentripetalCatmullRom(points[previous].worldPosition, points[start].worldPosition, points[end].worldPosition, points[next].worldPosition, parameter);
	}

	Vec3 Path::EvaluateTangent(std::size_t segment, float parameter) const noexcept
	{
		const float delta = 0.001f;
		const Vec3 before = EvaluatePosition(segment, std::max(0.0f, parameter - delta));
		const Vec3 after = EvaluatePosition(segment, std::min(1.0f, parameter + delta));
		return Normalize(Subtract(after, before));
	}

	bool Path::Rebuild(std::string* error)
	{
		if (error)
			error->clear();
		segments.clear();
		effectiveSpeeds.clear();
		totalLength = 0.0f;
		totalDuration = 0.0f;
		for (const auto& point : points) {
			if (!IsFinite(point.worldPosition)) {
				if (error) *error = "A camera point has a non-finite position.";
				return false;
			}
		}
		const std::size_t count = SegmentCount();
		if (count == 0)
			return true;
		effectiveSpeeds.reserve(points.size());
		for (const Point& point : points)
			effectiveSpeeds.push_back(ClampFinite(point.speed, 1.0f, 100000.0f, 240.0f));
		for (std::size_t index = 0; index < points.size(); ++index) {
			if (!closed && (index == 0 || index + 1 == points.size()))
				continue;
			const std::size_t previous = (index + points.size() - 1) % points.size();
			const std::size_t next = (index + 1) % points.size();
			const Vec3 incoming = Subtract(points[index].worldPosition, points[previous].worldPosition);
			const Vec3 outgoing = Subtract(points[next].worldPosition, points[index].worldPosition);
			const float beforeLength = Length(incoming);
			const float afterLength = Length(outgoing);
			if (beforeLength <= kEpsilon || afterLength <= kEpsilon)
				continue;
			const float bend = std::acos(std::clamp(Dot(Scale(incoming, 1.0f / beforeLength),
				Scale(outgoing, 1.0f / afterLength)), -1.0f, 1.0f));
			if (bend > 0.35f) {
				// A short ninety-degree turn cannot be traversed smoothly at the
				// same speed as a long straight. Bound angular travel at the POI;
				// linear interpolation below eases into and out of the corner.
				const float cornerSpeed = std::max(1.0f, std::min(beforeLength, afterLength) * 4.0f / bend);
				effectiveSpeeds[index] = std::min(effectiveSpeeds[index], cornerSpeed);
			}
		}
		segments.resize(count);
		for (std::size_t segmentIndex = 0; segmentIndex < count; ++segmentIndex) {
			auto& segment = segments[segmentIndex];
			segment.samples.push_back({ 0.0f, 0.0f });
			float distance = 0.0f;
			const auto subdivide = [&](auto&& self, float firstParameter, Vec3 first, float lastParameter, Vec3 last, std::uint32_t depth) -> void {
				const float midpointParameter = (firstParameter + lastParameter) * 0.5f;
				const Vec3 midpoint = EvaluatePosition(segmentIndex, midpointParameter);
				const float chord = Distance(first, last);
				const float polyline = Distance(first, midpoint) + Distance(midpoint, last);
				const float localTolerance = std::min(kArcLengthTolerance, std::max(chord * 0.0025f, 0.0005f));
				// Small corners previously got only the two end samples: a fixed
				// world-unit tolerance hid curvature and produced uneven motion.
				if (depth < kMaximumSubdivisionDepth && (depth < 4 || polyline - chord > localTolerance)) {
					self(self, firstParameter, first, midpointParameter, midpoint, depth + 1);
					self(self, midpointParameter, midpoint, lastParameter, last, depth + 1);
					return;
				}
				distance += std::max(polyline, 0.0f);
				segment.samples.push_back({ lastParameter, distance });
			};
			const Vec3 first = EvaluatePosition(segmentIndex, 0.0f);
			const Vec3 last = EvaluatePosition(segmentIndex, 1.0f);
			subdivide(subdivide, 0.0f, first, 1.0f, last, 0);
			segment.length = distance;
			const std::size_t end = closed ? (segmentIndex + 1) % points.size() : segmentIndex + 1;
			const float startSpeed = effectiveSpeeds[segmentIndex];
			const float endSpeed = effectiveSpeeds[end];
			// Speed varies linearly with distance. Integrating 1 / speed gives
			// continuous velocity at adjacent POIs and an exact segment duration.
			const float speedDifference = endSpeed - startSpeed;
			segment.duration = segment.length * (std::abs(speedDifference) > 0.001f
				? std::log(endSpeed / startSpeed) / speedDifference
				: 1.0f / startSpeed);
			totalLength += segment.length;
			totalDuration += segment.duration + std::max(ClampFinite(points[end].holdDuration, 0.0f, 600.0f, 0.0f), 0.0f);
		}
		if (explicitDuration.has_value())
			totalDuration = std::max(*explicitDuration, 0.0f);
		return true;
	}

	bool Path::IsValid() const noexcept { return points.size() >= 2 && segments.size() == SegmentCount() && totalLength > kEpsilon; }
	float Path::GetLength() const noexcept { return totalLength; }
	float Path::GetDuration() const noexcept { return totalDuration; }

	void Path::GetPointTimelineTimes(std::vector<float>& output) const
	{
		output.clear();
		output.resize(points.size(), 0.0f);
		if (!IsValid())
			return;

		float elapsed = 0.0f;
		for (std::size_t index = 0; index < segments.size(); ++index) {
			output[index] = elapsed;
			const Segment& segment = segments[index];
			elapsed += std::max(segment.duration, 0.0f);
			const std::size_t end = closed ? (index + 1) % points.size() : index + 1;
			// The first marker remains at zero for a loop; every other marker marks
			// arrival before that point's optional hold duration.
			if (end != 0 || !closed)
				output[end] = elapsed;
			elapsed += std::max(points[end].holdDuration, 0.0f);
		}
		if (explicitDuration.has_value() && elapsed > kEpsilon) {
			const float scale = totalDuration / elapsed;
			for (float& marker : output)
				marker *= scale;
		}
	}

	void Path::GetPreviewSamples(std::vector<Vec3>& output, std::uint32_t samplesPerSegment) const
	{
		output.clear();
		const std::size_t count = SegmentCount();
		if (count == 0)
			return;

		// Keep route preview storage bounded even if a malformed path contains a
		// very large number of points.  This is authoring data, never runtime
		// camera data.
		const std::uint32_t maximumSamples = 512;
		samplesPerSegment = std::clamp(samplesPerSegment, 4u, 64u);
		samplesPerSegment = std::min(samplesPerSegment,
			std::max(4u, maximumSamples / static_cast<std::uint32_t>(count)));
		output.reserve(std::min<std::size_t>(maximumSamples, count * (samplesPerSegment + 1)));
		for (std::size_t segment = 0; segment < count; ++segment) {
			for (std::uint32_t sample = 0; sample <= samplesPerSegment; ++sample) {
				if (segment > 0 && sample == 0)
					continue;
				output.push_back(EvaluatePosition(segment, static_cast<float>(sample) / static_cast<float>(samplesPerSegment)));
			}
		}
	}

	float Path::ParameterAtSegmentDistance(const Segment& segment, float distance) const noexcept
	{
		if (segment.samples.size() < 2 || segment.length <= kEpsilon)
			return 0.0f;
		distance = std::clamp(distance, 0.0f, segment.length);
		const auto upper = std::lower_bound(segment.samples.begin(), segment.samples.end(), distance, [](const ArcSample& sample, float target) { return sample.distance < target; });
		if (upper == segment.samples.begin())
			return upper->parameter;
		if (upper == segment.samples.end())
			return segment.samples.back().parameter;
		const ArcSample& after = *upper;
		const ArcSample& before = *(upper - 1);
		const float span = after.distance - before.distance;
		return span > kEpsilon ? before.parameter + (after.parameter - before.parameter) * ((distance - before.distance) / span) : before.parameter;
	}

	Pose Path::EvaluateSegment(std::size_t segment, float parameter) const noexcept
	{
		Pose pose{};
		if (segment >= SegmentCount())
			return pose;
		const std::size_t end = closed ? (segment + 1) % points.size() : segment + 1;
		const Point& startPoint = points[segment];
		const Point& endPoint = points[end];
		const float positionParameter = Ease(startPoint.positionEasing, parameter);
		const float rotationParameter = Ease(startPoint.rotationEasing, parameter);
		pose.worldPosition = EvaluatePosition(segment, positionParameter);
		const float authoredRoll = startPoint.rollDegrees + (endPoint.rollDegrees - startPoint.rollDegrees) * rotationParameter;
		if (startPoint.useLookAt || endPoint.useLookAt) {
			const Vec3 target = Lerp(startPoint.lookAtPosition, endPoint.lookAtPosition, rotationParameter);
			const Vec3 direction = Subtract(target, pose.worldPosition);
			pose.worldRotation = LookRotation(direction, Slerp(startPoint.worldRotation, endPoint.worldRotation, rotationParameter), authoredRoll);
		} else if (startPoint.followPathDirection || endPoint.followPathDirection) {
			// Skyrim's free camera is pitch/yaw based.  Following the centripetal
			// spline tangent therefore supplies the useful part of a cinematic auto
			// tilt: smooth pitch and yaw through each fly-by without inventing an
			// unsupported camera roll axis.
			pose.worldRotation = LookRotation(EvaluateTangent(segment, positionParameter),
				Slerp(startPoint.worldRotation, endPoint.worldRotation, rotationParameter), authoredRoll);
		} else {
			pose.worldRotation = Slerp(startPoint.worldRotation, endPoint.worldRotation, rotationParameter);
		}
		pose.fieldOfView = ClampFinite(startPoint.fieldOfView + (endPoint.fieldOfView - startPoint.fieldOfView) * rotationParameter, 20.0f, 110.0f, 75.0f);
		float automaticBank = 0.0f;
		if (startPoint.autoBank || endPoint.autoBank) {
			// Use a small symmetric tangent interval. This is evaluated from the
			// already-built spline, so no per-frame path search or allocation occurs.
			const float interval = 0.015f;
			const Vec3 previousTangent = EvaluateTangent(segment, std::max(0.0f, parameter - interval));
			const Vec3 nextTangent = EvaluateTangent(segment, std::min(1.0f, parameter + interval));
			const float turn = std::atan2(
				Dot(Cross(previousTangent, nextTangent), Vec3{ 0.0f, 0.0f, 1.0f }),
				std::clamp(Dot(previousTangent, nextTangent), -1.0f, 1.0f));
			const float speed = std::max((startPoint.speed + endPoint.speed) * 0.5f, 1.0f);
			const float strength = std::clamp((startPoint.bankStrength + endPoint.bankStrength) * 0.5f, 0.0f, 4.0f);
			// Keep the result artistic and safe. A curve can become very tight when
			// two authored points are nearly coincident, so no raw curvature value
			// may directly become camera roll.
			automaticBank = std::clamp(-turn * speed * strength * 0.35f, -35.0f, 35.0f);
			pose.worldRotation = Multiply(pose.worldRotation, AxisAngle(EvaluateTangent(segment, parameter), automaticBank * std::numbers::pi_v<float> / 180.0f));
		}
		pose.bankDegrees = authoredRoll + automaticBank;
		pose.valid = IsFinite(pose.worldPosition) && IsFinite(pose.fieldOfView);
		return pose;
	}

	Pose Path::EvaluateNormalized(float normalizedDistance) const noexcept
	{
		if (points.empty()) return {};
		if (points.size() == 1) return { points.front().worldPosition, Normalize(points.front().worldRotation), ClampFinite(points.front().fieldOfView, 20.0f, 110.0f, 75.0f), points.front().rollDegrees, true };
		if (!IsValid()) return {};
		float remaining = std::clamp(normalizedDistance, 0.0f, 1.0f) * totalLength;
		for (std::size_t index = 0; index < segments.size(); ++index) {
			if (remaining <= segments[index].length || index + 1 == segments.size())
				return EvaluateSegment(index, ParameterAtSegmentDistance(segments[index], remaining));
			remaining -= segments[index].length;
		}
		return {};
	}

	Pose Path::EvaluateTime(float seconds, bool loop) const noexcept
	{
		if (totalDuration <= kEpsilon)
			return EvaluateNormalized(0.0f);
		if (!IsFinite(seconds)) return {};
		if (loop) {
			seconds = std::fmod(std::max(seconds, 0.0f), totalDuration);
		} else {
			seconds = std::clamp(seconds, 0.0f, totalDuration);
		}
		if (explicitDuration.has_value()) {
			float authoredDuration = 0.0f;
			for (std::size_t index = 0; index < segments.size(); ++index) {
				const std::size_t end = closed ? (index + 1) % points.size() : index + 1;
				authoredDuration += segments[index].duration + std::max(points[end].holdDuration, 0.0f);
			}
			seconds *= authoredDuration / totalDuration;
		}
		for (std::size_t index = 0; index < segments.size(); ++index) {
			const Segment& segment = segments[index];
			if (segment.duration > kEpsilon && seconds <= segment.duration) {
				const float startSpeed = effectiveSpeeds[index];
				const std::size_t end = closed ? (index + 1) % points.size() : index + 1;
				const float endSpeed = effectiveSpeeds[end];
				const float difference = endSpeed - startSpeed;
				const float distanceFraction = segment.length > kEpsilon && std::abs(difference) > 0.001f
					? startSpeed * std::expm1(difference * seconds / segment.length) / difference
					: seconds / std::max(segment.duration, kEpsilon);
				return EvaluateSegment(index, ParameterAtSegmentDistance(segment,
					segment.length * std::clamp(distanceFraction, 0.0f, 1.0f)));
			}
			// Repeated positions create a zero-length segment. Consume it and
			// continue instead of pinning every later time to that POI.
			seconds -= segment.duration;
			const std::size_t end = closed ? (index + 1) % points.size() : index + 1;
			const float hold = std::max(points[end].holdDuration, 0.0f);
			if (seconds <= hold)
				return EvaluateSegment(index, 1.0f);
			seconds -= hold;
		}
		return EvaluateNormalized(1.0f);
	}

	nlohmann::json Path::ToJson() const
	{
		nlohmann::json document{ { "schema", "PIXL.DirectorCameraPath" }, { "version", kSchemaVersion }, { "name", name }, { "closed", closed }, { "points", nlohmann::json::array() } };
		if (explicitDuration.has_value()) document["duration"] = *explicitDuration;
		for (const Point& point : points) {
			nlohmann::json encoded{ { "id", point.id }, { "position", Encode(point.worldPosition) }, { "rotation", Encode(Normalize(point.worldRotation)) },
				{ "lookAt", point.useLookAt }, { "followPath", point.followPathDirection }, { "target", Encode(point.lookAtPosition) }, { "fov", point.fieldOfView }, { "speed", point.speed }, { "hold", point.holdDuration },
				{ "positionEasing", EasingName(point.positionEasing) }, { "rotationEasing", EasingName(point.rotationEasing) }, { "roll", point.rollDegrees }, { "autoBank", point.autoBank }, { "bankStrength", point.bankStrength } };
			if (point.exposure.has_value()) encoded["exposure"] = *point.exposure;
			if (point.focusDistance.has_value()) encoded["focusDistance"] = *point.focusDistance;
			document["points"].push_back(std::move(encoded));
		}
		return document;
	}

	std::optional<Path> Path::FromJson(const nlohmann::json& document, std::string* error)
	{
		if (error) error->clear();
		try {
			const auto version = document.value("version", 0u);
			if (!document.is_object() || document.value("schema", "") != "PIXL.DirectorCameraPath" || (version != 1u && version != kSchemaVersion))
				throw std::runtime_error("unsupported Director camera-path schema");
			Path path{};
			path.name = document.value("name", path.name);
			path.closed = document.value("closed", false);
			if (document.contains("duration")) path.explicitDuration = document.at("duration").get<float>();
			const auto& entries = document.at("points");
			if (!entries.is_array()) throw std::runtime_error("points must be an array");
			for (const auto& entry : entries) {
				Point point{};
				point.id = entry.value("id", 0u);
				point.worldPosition = DecodeVec3(entry.at("position"));
				point.worldRotation = DecodeQuaternion(entry.at("rotation"));
				point.useLookAt = entry.value("lookAt", false);
				point.followPathDirection = entry.value("followPath", false);
				point.lookAtPosition = entry.contains("target") ? DecodeVec3(entry.at("target")) : point.worldPosition;
				point.fieldOfView = ClampFinite(entry.value("fov", 75.0f), 20.0f, 110.0f, 75.0f);
				point.speed = ClampFinite(entry.value("speed", 240.0f), 1.0f, 100000.0f, 240.0f);
				point.holdDuration = ClampFinite(entry.value("hold", 0.0f), 0.0f, 600.0f, 0.0f);
				point.positionEasing = ParseEasing(entry.value("positionEasing", nlohmann::json("easeInOut")));
				point.rotationEasing = ParseEasing(entry.value("rotationEasing", nlohmann::json("easeInOut")));
				point.rollDegrees = ClampFinite(entry.value("roll", 0.0f), -180.0f, 180.0f, 0.0f);
				point.autoBank = entry.value("autoBank", false);
				point.bankStrength = ClampFinite(entry.value("bankStrength", 1.0f), 0.0f, 4.0f, 1.0f);
				if (entry.contains("exposure")) point.exposure = entry.at("exposure").get<float>();
				if (entry.contains("focusDistance")) point.focusDistance = entry.at("focusDistance").get<float>();
				path.points.push_back(std::move(point));
			}
			std::string rebuildError;
			if (!path.Rebuild(&rebuildError)) throw std::runtime_error(rebuildError);
			return path;
		} catch (const std::exception& exception) {
			if (error) *error = exception.what();
			return std::nullopt;
		}
	}

	bool RunDeterministicSelfTest(std::string* failure)
	{
		if (failure) failure->clear();
		Path path{};
		path.points = {
			{ 1, { 0.0f, 0.0f, 0.0f } },
			{ 2, { 100.0f, 0.0f, 0.0f } },
			{ 3, { 100.0f, 100.0f, 20.0f } },
			{ 4, { 200.0f, 160.0f, 30.0f } }
		};
		path.points[2].useLookAt = true;
		path.points[2].lookAtPosition = { 100.0f, 220.0f, 25.0f };
		std::string error;
		if (!path.Rebuild(&error) || !path.IsValid() || path.GetLength() <= 0.0f) {
			if (failure) *failure = "normal path failed to rebuild: " + error;
			return false;
		}
		const Pose middle = path.EvaluateNormalized(0.5f);
		if (!middle.valid || !IsFinite(middle.worldPosition) || !IsFinite(middle.worldRotation.w)) {
			if (failure) *failure = "normal path produced an invalid pose";
			return false;
		}
		std::vector<Vec3> preview;
		path.GetPreviewSamples(preview);
		if (preview.size() < 2 || !IsFinite(preview.front()) || !IsFinite(preview.back())) {
			if (failure) *failure = "route preview samples were invalid";
			return false;
		}
		path.points[1].followPathDirection = true;
		const Pose tangentFacing = path.EvaluateNormalized(0.33f);
		if (!tangentFacing.valid || !IsFinite(tangentFacing.worldRotation.w)) {
			if (failure) *failure = "path-facing rotation was invalid";
			return false;
		}
		Path duplicates{};
		duplicates.points = { { 1, { 0.0f, 0.0f, 0.0f } }, { 2, { 0.0f, 0.0f, 0.0f } }, { 3, { 1.0f, 0.0f, 0.0f } } };
		if (!duplicates.Rebuild(&error) || !duplicates.EvaluateTime(0.1f, false).valid ||
			duplicates.EvaluateTime(0.1f, false).worldPosition.x < 0.9f) {
			if (failure) *failure = "duplicate-point path failed safely: " + error;
			return false;
		}
		const auto decoded = Path::FromJson(path.ToJson(), &error);
		if (!decoded.has_value() || !decoded->IsValid()) {
			if (failure) *failure = "round-trip JSON failed: " + error;
			return false;
		}
		auto versionOne = path.ToJson();
		versionOne["version"] = 1;
		for (auto& point : versionOne["points"])
			point.erase("followPath");
		const auto legacy = Path::FromJson(versionOne, &error);
		if (!legacy.has_value() || legacy->points[1].followPathDirection) {
			if (failure) *failure = "version 1 path migration failed";
			return false;
		}
		Path speedRamp{};
		speedRamp.points = { { 1, { 0.0f, 0.0f, 0.0f } }, { 2, { 100.0f, 0.0f, 0.0f } } };
		speedRamp.points[0].speed = 50.0f;
		speedRamp.points[1].speed = 200.0f;
		if (!speedRamp.Rebuild(&error) || !speedRamp.IsValid()) {
			if (failure) *failure = "speed-ramp path failed to rebuild: " + error;
			return false;
		}
		const float halfwayDistance = speedRamp.EvaluateTime(speedRamp.GetDuration() * 0.5f, false).worldPosition.x;
		if (!(halfwayDistance > 25.0f && halfwayDistance < 45.0f)) {
			if (failure) *failure = "per-point speed did not accelerate smoothly";
			return false;
		}
		speedRamp.explicitDuration = 8.0f;
		if (!speedRamp.Rebuild(&error)) {
			if (failure) *failure = "fixed-duration speed ramp failed to rebuild: " + error;
			return false;
		}
		std::vector<float> markerTimes;
		speedRamp.GetPointTimelineTimes(markerTimes);
		if (markerTimes.size() != 2 || std::abs(markerTimes.back() - 8.0f) > 0.01f ||
			std::abs(speedRamp.EvaluateTime(4.0f, false).worldPosition.x - halfwayDistance) > 0.1f) {
			if (failure) *failure = "fixed route duration lost point speed or timeline alignment";
			return false;
		}
		Path shortTurn{};
		shortTurn.points = {
			{ 1, { 0.0f, 0.0f, 0.0f } }, { 2, { 6.0f, 0.0f, 0.0f } },
			{ 3, { 6.0f, 6.0f, 0.0f } }, { 4, { 12.0f, 6.0f, 0.0f } }
		};
		if (!shortTurn.Rebuild(&error) || !shortTurn.IsValid() || shortTurn.GetDuration() < 0.25f) {
			if (failure) *failure = "short turn did not gain enough smooth traversal time";
			return false;
		}
		Vec3 previous = shortTurn.EvaluateTime(0.0f, false).worldPosition;
		for (int frame = 1; frame <= 120; ++frame) {
			const Pose sample = shortTurn.EvaluateTime(frame / 120.0f, false);
			if (!sample.valid || !IsFinite(sample.worldPosition) || Distance(previous, sample.worldPosition) > 2.5f) {
				if (failure) *failure = "short turn produced a discontinuous frame step";
				return false;
			}
			previous = sample.worldPosition;
		}
		return true;
	}
}
