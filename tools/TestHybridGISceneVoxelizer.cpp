#include "Modules/HybridGI/SceneVoxelizer.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

int main()
{
	using namespace PIXL::HybridGIInternal;
	if ((PackSceneVoxelMaterial({ 0.7f, 0.4f, 0.2f }, false) & 0x80000000u) != 0u ||
		(PackSceneVoxelMaterial({ 4.0f, 1.5f, 0.5f }, true) & 0x80000000u) == 0u) {
		std::cerr << "HybridGI diffuse/emissive material marker check failed\n";
		return 1;
	}

	const std::array<float, 3> camera{ 0.0f, 0.0f, 0.0f };
	const SceneTriangle plane{
		{ -48.0f, -48.0f, 0.0f }, { 48.0f, -48.0f, 0.0f }, { 0.0f, 48.0f, 0.0f }
	};
	const auto grid = VoxelizeSceneTriangles(std::span{ &plane, 1 }, camera);
	assert(grid.voxelizedTriangles == 1);
	assert(grid.occupancy.size() == std::size_t(SceneVoxelGrid::kDimension) * SceneVoxelGrid::kDimension * SceneVoxelGrid::kDimension);
	assert(std::count(grid.occupancy.begin(), grid.occupancy.end(), 1u) > 8);
	assert(grid.worldMin[0] == -1024.0f && grid.worldMin[1] == -1024.0f && grid.worldMin[2] == -1024.0f);

	const SceneTriangle outside{
		{ 4000.0f, 4000.0f, 4000.0f }, { 4100.0f, 4000.0f, 4000.0f }, { 4000.0f, 4100.0f, 4000.0f }
	};
	const auto outsideGrid = VoxelizeSceneTriangles(std::span{ &outside, 1 }, camera);
	assert(outsideGrid.voxelizedTriangles == 0);
	assert(std::count(outsideGrid.occupancy.begin(), outsideGrid.occupancy.end(), 1u) == 0);

	// The bounds of this triangle overlap a voxel, but the actual triangle does
	// not. Conservative SAT voxelization must avoid the old AABB/barycentric gaps.
	const SceneTriangle diagonal{
		{ -80.0f, -80.0f, 0.0f }, { 80.0f, 80.0f, 0.0f }, { 80.0f, 79.0f, 0.0f }
	};
	const auto diagonalGrid = VoxelizeSceneTriangles(std::span{ &diagonal, 1 }, camera);
	assert(diagonalGrid.voxelizedTriangles == 1);
	assert(std::count(diagonalGrid.occupancy.begin(), diagonalGrid.occupancy.end(), 1u) > 0);
	const auto subVoxelMove = VoxelizeSceneTriangles(std::span{ &plane, 1 }, { 0.25f, -0.25f, 0.49f });
	assert(subVoxelMove.worldMin == grid.worldMin);
	assert(subVoxelMove.occupancy == grid.occupancy);
	const auto belowSnapBoundary = VoxelizeSceneTriangles(std::span{ &plane, 1 }, { 3.99f, 0.0f, 0.0f });
	const auto aboveSnapBoundary = VoxelizeSceneTriangles(std::span{ &plane, 1 }, { 4.01f, 0.0f, 0.0f });
	assert(belowSnapBoundary.worldMin == grid.worldMin);
	assert(aboveSnapBoundary.worldMin[0] == grid.worldMin[0] + SceneVoxelGrid::kVoxelSize);

	const auto scroll = ComputeSceneVoxelScroll(grid.worldMin,
		{ grid.worldMin[0] + 8.0f, grid.worldMin[1] - 8.0f, grid.worldMin[2] },
		SceneVoxelGrid::kVoxelSize, SceneVoxelGrid::kDimension);
	const std::array<std::int32_t, 3> expectedScroll{ 1, -1, 0 };
	assert(scroll.valid && scroll.sourceOffset == expectedScroll);
	assert(IsSceneVoxelNewlyExposed({ static_cast<std::int32_t>(SceneVoxelGrid::kDimension - 1), 20, 20 }, scroll.sourceOffset, SceneVoxelGrid::kDimension));
	assert(IsSceneVoxelNewlyExposed({ 20, 0, 20 }, scroll.sourceOffset, SceneVoxelGrid::kDimension));
	assert(!IsSceneVoxelNewlyExposed({ 20, 20, 20 }, scroll.sourceOffset, SceneVoxelGrid::kDimension));
	assert(!ComputeSceneVoxelScroll(grid.worldMin, { grid.worldMin[0] + 1.0f, grid.worldMin[1], grid.worldMin[2] },
		SceneVoxelGrid::kVoxelSize, SceneVoxelGrid::kDimension).valid);
	assert(!ComputeSceneVoxelScroll(grid.worldMin, { grid.worldMin[0] + SceneVoxelGrid::kDimension * SceneVoxelGrid::kVoxelSize, grid.worldMin[1], grid.worldMin[2] },
		SceneVoxelGrid::kVoxelSize, SceneVoxelGrid::kDimension).valid);

	// Proximity gets first claim on the geometry triangle budget; pointer identity
	// only determines the stable output order after admission.
	const std::array geometryCandidates{
		SceneGeometryBudgetCandidate{ 900, 20.0f, 7000 },
		SceneGeometryBudgetCandidate{ 100, 10.0f, 7000 },
		SceneGeometryBudgetCandidate{ 50, 5.0f, 2000 },
		SceneGeometryBudgetCandidate{ 25, 1.0f, 0 },
		SceneGeometryBudgetCandidate{ 50, 8.0f, 2000 },  // duplicate instance token
		SceneGeometryBudgetCandidate{ 75, std::numeric_limits<float>::infinity(), 1 }
	};
	const auto admittedGeometry = SelectSceneGeometryWithinBudget(geometryCandidates, 8, 10000);
	assert(admittedGeometry.size() == 2);
	assert(geometryCandidates[admittedGeometry[0]].identity == 50);
	assert(geometryCandidates[admittedGeometry[1]].identity == 100);
	const auto cappedGeometry = SelectSceneGeometryWithinBudget(geometryCandidates, 1, 8192);
	assert(cappedGeometry.size() == 1 && geometryCandidates[cappedGeometry[0]].identity == 50);

	const SceneTriangle invalid{
		{ 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f }
	};
	const std::array many{ plane, invalid };
	const auto capped = VoxelizeSceneTriangles(many, camera, 1);
	assert(capped.voxelizedTriangles == 1);
	assert(std::count(capped.occupancy.begin(), capped.occupancy.end(), 1u) > 0);
	const auto degenerate = VoxelizeSceneTriangles(std::span{ &invalid, 1 }, camera);
	assert(degenerate.voxelizedTriangles == 0);

	std::cout << "HybridGI scene voxelizer checks passed\n";
	return 0;
}
