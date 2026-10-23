#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace OceanGeometry
{
	struct QuadtreePatch
	{
		float centerX{};
		float centerY{};
		float size{};
		std::uint32_t level{};
	};

	struct LodTile
	{
		float centerX{};
		float centerY{};
		float size{};
		std::uint32_t level{};
	};

	// Absolute world-space center. Vertices are converted through NiAVObject's
	// absolute world transform; Skyrim's water shader performs its camera/origin
	// adjustment afterward. Keep this center camera-following and world anchored.
	inline std::array<float, 3> ClipmapCenter(float cameraX, float cameraY, float seaLevel) noexcept
	{
		// Snap to the smallest Crest-style tile scale so the LOD grid remains fixed
		// in world space while the camera moves inside a tile.
		constexpr float snap = 512.0f;
		return { std::round(cameraX / snap) * snap, std::round(cameraY / snap) * snap, seaLevel };
	}

	// Port of Crest OceanBuilder's concentric 4x4 tile layout. The finest level
	// owns the center 4x4 tiles; each coarser level adds a 4x4 ring with its
	// covered 2x2 interior omitted. The renderer tessellates shared boundaries
	// according to projected edge length so 2:1 LOD edges receive matching
	// fractional-even subdivision. This replaces the former adaptive leaf quadtree
	// with Crest's stable, camera-snapped ocean tile topology.
	inline bool BuildLodTileRings(float centerX, float centerY, float baseTileSize,
		std::uint32_t lodCount, std::vector<LodTile>& output)
	{
		output.clear();
		if (!std::isfinite(centerX) || !std::isfinite(centerY) ||
			!std::isfinite(baseTileSize) || baseTileSize <= 0.0f ||
			lodCount == 0 || lodCount > 12)
			return false;

		constexpr std::array<float, 4> tileOffsets{ -1.5f, -0.5f, 0.5f, 1.5f };
		output.reserve(16u + 12u * (lodCount - 1u));
		for (std::uint32_t lod = 0; lod < lodCount; ++lod) {
			const float size = baseTileSize * std::ldexp(1.0f, static_cast<int>(lod));
			if (!std::isfinite(size)) {
				output.clear();
				return false;
			}
			for (std::size_t y = 0; y < tileOffsets.size(); ++y) {
				for (std::size_t x = 0; x < tileOffsets.size(); ++x) {
					// Coarse levels omit their central 2x2 tiles. This leaves a
					// continuous 12-tile ring around the next finer LOD.
					if (lod > 0 && x >= 1 && x <= 2 && y >= 1 && y <= 2)
						continue;
					output.push_back({ centerX + tileOffsets[x] * size,
						centerY + tileOffsets[y] * size, size, lod });
				}
			}
		}
		return output.size() == 16u + 12u * (lodCount - 1u);
	}

	// Build a camera-centered, square quadtree surface. All patches use the same
	// small local grid; the water HS stage then chooses matching shared-edge
	// factors from projected edge length. This keeps the authored topology dense
	// near the view and amortizes geometry over successively larger far patches.
	// The bounded leaf count is deliberate: on pathological camera/precision
	// states the caller falls back to Skyrim's source mesh instead of allocating
	// an unbounded transient vertex buffer.
	inline bool BuildQuadtree(float centerX, float centerY, float rootSize,
		float minimumPatchSize, std::uint32_t maxLeaves,
		std::vector<QuadtreePatch>& output)
	{
		output.clear();
		if (!std::isfinite(centerX) || !std::isfinite(centerY) ||
			!std::isfinite(rootSize) || !std::isfinite(minimumPatchSize) ||
			rootSize <= 0.0f || minimumPatchSize <= 0.0f || maxLeaves == 0)
			return false;

		const float cameraX = centerX;
		const float cameraY = centerY;
		bool overflow = false;
		auto subdivide = [&](auto&& self, float x, float y, float size, std::uint32_t level) -> void {
			if (overflow) return;
			const float half = size * 0.5f;
			const float dx = std::max(std::abs(cameraX - x) - half, 0.0f);
			const float dy = std::max(std::abs(cameraY - y) - half, 0.0f);
			const bool nearCamera = dx * dx + dy * dy < size * size * 1.44f;
			if (size > minimumPatchSize && nearCamera) {
				const float child = size * 0.5f;
				self(self, x - child * 0.5f, y - child * 0.5f, child, level + 1);
				self(self, x + child * 0.5f, y - child * 0.5f, child, level + 1);
				self(self, x - child * 0.5f, y + child * 0.5f, child, level + 1);
				self(self, x + child * 0.5f, y + child * 0.5f, child, level + 1);
				return;
			}
			if (output.size() >= maxLeaves) {
				overflow = true;
				return;
			}
			output.push_back({ x, y, size, level });
		};

		subdivide(subdivide, centerX, centerY, rootSize, 0);
		if (overflow || output.empty()) {
			output.clear();
			return false;
		}
		return true;
	}
}
