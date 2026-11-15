#include "SceneVoxelizer.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace PIXL::HybridGIInternal
{
	std::uint32_t PackSceneVoxelMaterial(const std::array<float, 3>& color, bool emissive)
	{
		float rgb[3] = { color[0], color[1], color[2] };
		for (auto& channel : rgb)
			channel = std::isfinite(channel) ? std::max(channel, 0.0f) : 0.0f;
		if (emissive) {
			const float maximum = std::max({ rgb[0], rgb[1], rgb[2] });
			const float scale = 1.0f / (1.0f + maximum);
			for (auto& channel : rgb)
				channel *= scale;
		}
		const auto r = static_cast<std::uint32_t>(std::clamp(std::lround(rgb[0] * 255.0f), 0l, 255l));
		const auto g = static_cast<std::uint32_t>(std::clamp(std::lround(rgb[1] * 255.0f), 0l, 255l));
		const auto b = static_cast<std::uint32_t>(std::clamp(std::lround(rgb[2] * 127.0f), 0l, 127l));
		return (emissive ? 0x80000000u : 0u) | (r << 15u) | (g << 7u) | b;
	}

	namespace
	{
		using Vec3 = std::array<float, 3>;

		Vec3 Sub(const Vec3& a, const Vec3& b)
		{
			return { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
		}

		Vec3 Cross(const Vec3& a, const Vec3& b)
		{
			return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
		}

		float Dot(const Vec3& a, const Vec3& b)
		{
			return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
		}

		bool Finite(const Vec3& v)
		{
			return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
		}

		bool TriangleIntersectsBox(const SceneTriangle& triangle, const Vec3& boxCenter, float halfSize)
		{
			// Separating-axis test for a triangle and axis-aligned voxel. Axes are
			// the box normals, triangle normal, and each triangle-edge x box-axis.
			const Vec3 vertices[] = {
				Sub(triangle.a, boxCenter), Sub(triangle.b, boxCenter), Sub(triangle.c, boxCenter)
			};
			constexpr float epsilon = 1e-4f;
			const auto separated = [&](const Vec3& axis) {
				const float axisLength2 = Dot(axis, axis);
				if (axisLength2 < 1e-16f)
					return false;
				const float p0 = Dot(vertices[0], axis);
				const float p1 = Dot(vertices[1], axis);
				const float p2 = Dot(vertices[2], axis);
				const float minimum = std::min({ p0, p1, p2 });
				const float maximum = std::max({ p0, p1, p2 });
				const float radius = halfSize * (std::abs(axis[0]) + std::abs(axis[1]) + std::abs(axis[2]));
				return minimum > radius + epsilon || maximum < -radius - epsilon;
			};

			if (separated({ 1.0f, 0.0f, 0.0f }) || separated({ 0.0f, 1.0f, 0.0f }) || separated({ 0.0f, 0.0f, 1.0f }))
				return false;
			const Vec3 edges[] = { Sub(vertices[1], vertices[0]), Sub(vertices[2], vertices[1]), Sub(vertices[0], vertices[2]) };
			if (separated(Cross(edges[0], edges[1])))
				return false;
			for (const auto& edge : edges) {
				if (separated(Cross(edge, { 1.0f, 0.0f, 0.0f })) ||
					separated(Cross(edge, { 0.0f, 1.0f, 0.0f })) ||
					separated(Cross(edge, { 0.0f, 0.0f, 1.0f })))
					return false;
			}
			return true;
		}
	}

	SceneVoxelGrid VoxelizeSceneTriangles(
		std::span<const SceneTriangle> triangles, const std::array<float, 3>& cameraWorld,
		std::uint32_t maxTriangles)
	{
		SceneVoxelGrid grid;
		constexpr auto dim = SceneVoxelGrid::kDimension;
		constexpr float extent = static_cast<float>(dim) * SceneVoxelGrid::kVoxelSize;
		constexpr float halfExtent = extent * 0.5f;
		grid.occupancy.assign(static_cast<std::size_t>(dim) * dim * dim, 0u);
		if (!Finite(cameraWorld))
			return grid;

		// Match HTrace's nearest-voxel camera snapping. Flooring incorrectly moved
		// the volume by a full cell for tiny negative movement at lattice boundaries.
		for (std::size_t axis = 0; axis < 3; ++axis) {
			const float snapped = std::round(cameraWorld[axis] / SceneVoxelGrid::kVoxelSize) * SceneVoxelGrid::kVoxelSize;
			grid.worldMin[axis] = snapped - halfExtent;
		}

		constexpr std::size_t maxCandidateTests = 250'000;
		std::size_t candidateTests = 0;
		const auto count = std::min<std::size_t>(triangles.size(), maxTriangles);
		for (std::size_t i = 0; i < count && candidateTests < maxCandidateTests; ++i) {
			const auto& t = triangles[i];
			if (!Finite(t.a) || !Finite(t.b) || !Finite(t.c))
				continue;
			const Vec3 e0 = Sub(t.b, t.a), e1 = Sub(t.c, t.a);
			const Vec3 normal = Cross(e0, e1);
			const float area2 = Dot(normal, normal);
			if (!std::isfinite(area2) || area2 <= 1e-8f)
				continue;

			const Vec3 minimum{
				std::min({ t.a[0], t.b[0], t.c[0] }),
				std::min({ t.a[1], t.b[1], t.c[1] }),
				std::min({ t.a[2], t.b[2], t.c[2] })
			};
			const Vec3 maximum{
				std::max({ t.a[0], t.b[0], t.c[0] }),
				std::max({ t.a[1], t.b[1], t.c[1] }),
				std::max({ t.a[2], t.b[2], t.c[2] })
			};
			int lo[3], hi[3];
			bool outside = false;
			for (int axis = 0; axis < 3; ++axis) {
				const float gridMax = grid.worldMin[axis] + extent;
				if (maximum[axis] < grid.worldMin[axis] || minimum[axis] >= gridMax) {
					outside = true;
					break;
				}
				const float minCell = std::floor((minimum[axis] - grid.worldMin[axis]) / SceneVoxelGrid::kVoxelSize);
				const float maxCell = std::floor((maximum[axis] - grid.worldMin[axis]) / SceneVoxelGrid::kVoxelSize);
				lo[axis] = minCell <= 0.0f ? 0 : minCell >= static_cast<float>(dim) ? static_cast<int>(dim) - 1 : static_cast<int>(minCell);
				hi[axis] = maxCell <= 0.0f ? 0 : maxCell >= static_cast<float>(dim) ? static_cast<int>(dim) - 1 : static_cast<int>(maxCell);
			}
			if (outside)
				continue;
			const std::size_t candidateVolume = static_cast<std::size_t>(hi[0] - lo[0] + 1) *
				static_cast<std::size_t>(hi[1] - lo[1] + 1) * static_cast<std::size_t>(hi[2] - lo[2] + 1);
			if (candidateVolume > maxCandidateTests - candidateTests)
				continue;  // skip whole triangles rather than create partial false occluders

			bool marked = false;
			for (int z = lo[2]; z <= hi[2] && candidateTests < maxCandidateTests; ++z) {
				for (int y = lo[1]; y <= hi[1] && candidateTests < maxCandidateTests; ++y) {
					for (int x = lo[0]; x <= hi[0] && candidateTests < maxCandidateTests; ++x) {
						++candidateTests;
						const Vec3 center{
							grid.worldMin[0] + (static_cast<float>(x) + 0.5f) * SceneVoxelGrid::kVoxelSize,
							grid.worldMin[1] + (static_cast<float>(y) + 0.5f) * SceneVoxelGrid::kVoxelSize,
							grid.worldMin[2] + (static_cast<float>(z) + 0.5f) * SceneVoxelGrid::kVoxelSize
						};
						if (TriangleIntersectsBox(t, center, SceneVoxelGrid::kVoxelSize * 0.5f)) {
							grid.occupancy[(static_cast<std::size_t>(z) * dim + static_cast<std::size_t>(y)) * dim + static_cast<std::size_t>(x)] = 1u;
							marked = true;
						}
					}
				}
			}
			if (marked)
				++grid.voxelizedTriangles;
		}
		return grid;
	}

	SceneVoxelScroll ComputeSceneVoxelScroll(
		const std::array<float, 3>& previousWorldMin, const std::array<float, 3>& newWorldMin,
		float voxelSize, std::uint32_t dimension)
	{
		SceneVoxelScroll result;
		if (!Finite(previousWorldMin) || !Finite(newWorldMin) || !std::isfinite(voxelSize) || voxelSize <= 0.0f || dimension == 0)
			return result;

		for (std::size_t axis = 0; axis < 3; ++axis) {
			const float cells = (newWorldMin[axis] - previousWorldMin[axis]) / voxelSize;
			const float nearest = std::round(cells);
			if (!std::isfinite(cells) || std::abs(cells - nearest) > 1e-4f || std::abs(nearest) >= static_cast<float>(dimension))
				return {};
			result.sourceOffset[axis] = static_cast<std::int32_t>(nearest);
		}
		result.valid = true;
		return result;
	}

	bool IsSceneVoxelNewlyExposed(
		const std::array<std::int32_t, 3>& destinationCell,
		const std::array<std::int32_t, 3>& sourceOffset, std::uint32_t dimension)
	{
		if (dimension == 0)
			return false;
		for (std::size_t axis = 0; axis < 3; ++axis) {
			const auto source = static_cast<std::int64_t>(destinationCell[axis]) + sourceOffset[axis];
			if (source < 0 || source >= static_cast<std::int64_t>(dimension))
				return true;
		}
		return false;
	}

	std::vector<std::size_t> SelectSceneGeometryWithinBudget(
		std::span<const SceneGeometryBudgetCandidate> candidates,
		std::size_t maximumObjects, std::size_t maximumTriangles)
	{
		std::vector<std::size_t> byDistance;
		byDistance.reserve(candidates.size());
		for (std::size_t i = 0; i < candidates.size(); ++i) {
			const auto& candidate = candidates[i];
			if (candidate.identity != 0 && std::isfinite(candidate.distance) && candidate.distance >= 0.0f &&
				candidate.triangleCount > 0 && candidate.triangleCount <= maximumTriangles)
				byDistance.push_back(i);
		}
		std::sort(byDistance.begin(), byDistance.end(), [&](std::size_t a, std::size_t b) {
			const auto& left = candidates[a];
			const auto& right = candidates[b];
			if (left.distance != right.distance)
				return left.distance < right.distance;
			return left.identity < right.identity;
		});

		std::vector<std::size_t> admitted;
		admitted.reserve(std::min(maximumObjects, byDistance.size()));
		std::size_t remainingTriangles = maximumTriangles;
		for (const auto index : byDistance) {
			if (admitted.size() >= maximumObjects)
				break;
			const auto& candidate = candidates[index];
			if (candidate.triangleCount > remainingTriangles)
				continue;
			// A repeated identity is one geometry instance for this frame. Keep the
			// nearest record so duplicate traversal paths cannot spend the budget twice.
			if (std::any_of(admitted.begin(), admitted.end(), [&](std::size_t prior) {
					return candidates[prior].identity == candidate.identity;
				}))
				continue;
			admitted.push_back(index);
			remainingTriangles -= candidate.triangleCount;
		}
		std::sort(admitted.begin(), admitted.end(), [&](std::size_t a, std::size_t b) {
			return candidates[a].identity < candidates[b].identity;
		});
		return admitted;
	}
}
