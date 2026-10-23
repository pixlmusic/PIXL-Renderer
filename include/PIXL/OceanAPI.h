#pragma once

// Plain C ABI shared by PIXL Renderer and optional gameplay plugins.
// Coordinates, heights and velocities are Skyrim world units (Z is up).
// QueryBatch is called on Skyrim's main thread. It never calls D3D Map.
#include <cstdint>

enum PIXL_OceanValidityV1 : std::uint32_t
{
	PIXL_OCEAN_VALID_SURFACE = 1u << 0,
	PIXL_OCEAN_VALID_DEPTH = 1u << 1,
	PIXL_OCEAN_VALID_BREAKER = 1u << 2,
	PIXL_OCEAN_STALE = 1u << 3
};

struct PIXL_OceanRequestV1
{
	std::uint32_t worldspaceFormID;
	std::uint32_t reserved;
	float worldX;
	float worldY;
	float worldZ;
	float sampleRadius;
};

struct PIXL_OceanSampleV1
{
	std::uint32_t validityFlags;
	std::uint32_t worldspaceFormID;
	std::uint64_t generation;
	double simulationSeconds;
	float ageSeconds;
	float waterHeight;
	float normal[3];
	float velocity[3];
	float waveDirection[2];
	float waterDepth;
	float breakerStrength;
	float breakerMomentum[2];
	float shoreDirection[2];
	float foam;
};

struct PIXL_OceanAPI_V1
{
	std::uint32_t version;
	std::uint32_t requestSize;
	std::uint32_t sampleSize;
	std::uint32_t maxBatchSize;
	// Returns the number of samples with PIXL_OCEAN_VALID_SURFACE.
	std::uint32_t (*QueryBatch)(const PIXL_OceanRequestV1*, PIXL_OceanSampleV1*, std::uint32_t) noexcept;
};

// Optional, higher-resolution loaded-cell terrain feed for gameplay queries.
// The producer calls on Skyrim's game thread and the renderer copies the
// samples immediately. This cell-local source is not used to shape rendered
// water, because joining it to the coarse world map creates a moving cell box.
// Samples are row-major, Z-up, Skyrim world units; NaN means no sample.
struct PIXL_OceanTerrainPatchV1
{
	std::uint32_t version;
	std::uint32_t worldspaceFormID;
	std::uint32_t resolution;
	std::uint32_t reserved;
	float originX;
	float originY;
	float spacing;
	float sampleTimeSeconds;
};

struct PIXL_OceanAPI_V2
{
	std::uint32_t version;
	std::uint32_t requestSize;
	std::uint32_t sampleSize;
	std::uint32_t maxBatchSize;
	std::uint32_t terrainPatchSize;
	std::uint32_t reserved[3]{};
	std::uint32_t (*QueryBatch)(const PIXL_OceanRequestV1*, PIXL_OceanSampleV1*, std::uint32_t) noexcept;
	bool (*SubmitTerrainPatch)(const PIXL_OceanTerrainPatchV1*, const float*, std::uint32_t) noexcept;
};

static_assert(sizeof(PIXL_OceanRequestV1) == 24);
static_assert(sizeof(PIXL_OceanSampleV1) == 96);
static_assert(sizeof(PIXL_OceanTerrainPatchV1) == 32);

extern "C" __declspec(dllexport) const PIXL_OceanAPI_V1* PIXL_GetOceanAPI_V1() noexcept;
extern "C" __declspec(dllexport) const PIXL_OceanAPI_V2* PIXL_GetOceanAPI_V2() noexcept;
