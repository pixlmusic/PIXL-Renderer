#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace PIXL::HybridGIInternal
{
	// Owned world-space triangles copied while Skyrim's render pass is valid.
	// No engine geometry or resource pointers are retained by this type.
	struct SceneTriangle
	{
		std::array<float, 3> a{}, b{}, c{};
		std::array<float, 2> uvA{}, uvB{}, uvC{};
		std::uint32_t packedMaterial{};
		std::uint32_t textureIndex{ UINT32_MAX };
	};

	// The GPU triangle carries its bounded dominant-axis raster footprint. Keeping
	// this count in the uploaded record avoids shader-compiler range expansion and
	// makes the per-triangle voxel work explicit and inspectable.
	struct GpuSceneTriangle
	{
		std::array<float, 3> a{}, b{}, c{};
		std::uint32_t projectedCellCount{};
		std::uint32_t packedMaterial{};
		std::array<float, 2> uvA{}, uvB{}, uvC{};
		std::uint32_t textureIndex{ UINT32_MAX };
	};

	struct SceneVoxelGrid
	{
		static constexpr std::uint32_t kDimension = 256;
		// Skyrim world units. A 10-unit pitch gives this camera-centered field a
		// 2560-unit (~36.6 m) window while retaining the fixed 256^3 GPU footprint.
		static constexpr float kVoxelSize = 10.0f;
		std::array<float, 3> worldMin{};
		std::vector<std::uint32_t> occupancy;
		std::uint32_t voxelizedTriangles{};
	};

	struct SceneVoxelScroll
	{
		std::array<std::int32_t, 3> sourceOffset{};
		bool valid{};
	};

	struct SceneGeometryBudgetCandidate
	{
		std::uintptr_t identity{};
		float distance{};
		std::uint32_t triangleCount{};
	};

	static_assert(sizeof(SceneTriangle) == sizeof(float) * 15 + sizeof(std::uint32_t) * 2);
	static_assert(sizeof(GpuSceneTriangle) == sizeof(float) * 15 + sizeof(std::uint32_t) * 3);
	static constexpr std::uint32_t kSceneMaterialTextureSlots = 16;
	static constexpr std::uint32_t kMaxGpuSceneTriangles = 16384;

	[[nodiscard]] std::uint32_t PackSceneVoxelMaterial(const std::array<float, 3>& color, bool emissive);

	// Conservative triangle/cell overlap voxelization. Work is bounded by triangle
	// count and candidate voxel tests; a cell is marked only when the triangle
	// intersects its AABB (within a small floating-point tolerance).
	[[nodiscard]] SceneVoxelGrid VoxelizeSceneTriangles(
		std::span<const SceneTriangle> triangles, const std::array<float, 3>& cameraWorld,
		std::uint32_t maxTriangles = kMaxGpuSceneTriangles);

	// A valid result maps destination cells in the new snapped window to source
	// cells in the previous one. Large or non-lattice moves require a full rebuild.
	[[nodiscard]] SceneVoxelScroll ComputeSceneVoxelScroll(
		const std::array<float, 3>& previousWorldMin, const std::array<float, 3>& newWorldMin,
		float voxelSize, std::uint32_t dimension);
	[[nodiscard]] bool IsSceneVoxelNewlyExposed(
		const std::array<std::int32_t, 3>& destinationCell,
		const std::array<std::int32_t, 3>& sourceOffset, std::uint32_t dimension);

	// Chooses nearby geometry before applying the triangle budget, then returns
	// admitted source indices in stable identity order for reusable snapshots.
	[[nodiscard]] std::vector<std::size_t> SelectSceneGeometryWithinBudget(
		std::span<const SceneGeometryBudgetCandidate> candidates,
		std::size_t maximumObjects, std::size_t maximumTriangles);
}
