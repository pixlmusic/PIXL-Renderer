// PIXL Renderer - Foliage Optimizer
// Derived from Community Shaders 1.9.1 Grass Optimizations and substantially adapted for PIXL Renderer.
// Upstream and contributor copyrights remain with their respective authors.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/** @brief Byte stride of the per-instance records. */
constexpr uint32_t kGrassStride = 32;

/** @brief Mesh vertex stride in bytes, from nibble 0 of a packed vertexDesc (stored as bytes/4). */
constexpr uint32_t VertexStrideFromDesc(uint64_t descVal) { return static_cast<uint32_t>((4 * descVal) & 0x3C); }

/** @brief Maps grass shapes to their source .nif and caches the optional LOD meshes. Assigns integer IDs to each unique source .nif to avoid string lookups. */
class GrassMeshLibrary
{
public:
	/** @brief Which of the two mesh-swap distance bands an LOD mesh serves. */
	enum class LODTier : uint32_t
	{
		kMiddle = 0,
		kFar = 1,
		kCount = 2
	};

	/** @brief An LOD mesh, loaded from meshes\LOD\Grass\<source-mesh-stem>_LOD0.nif (middle) or _LOD1.nif (far). When `valid` is false, the full mesh is drawn then. */
	struct LODMesh
	{
		RE::NiPointer<RE::NiAVObject> keepAlive;  // owns the loaded model tree
		ID3D11Buffer* vertexBuffer = nullptr;
		ID3D11Buffer* indexBuffer = nullptr;
		uint32_t indexCount = 0;
		// The LOD .nif is authored separately, so its vertex format need not match the source mesh's.
		uint32_t meshStride = 0;
		uint64_t descVal = 0;
		bool attemptedLoad = false;
		bool valid = false;
	};

	/** @brief Records a shape's source model path, from the LoadGrassType hook. */
	void RecordModelPath(RE::BSMultiStreamInstanceTriShape* shape, const char* modelPath);

	/** @brief Resolves a shape to a source-mesh id (0 = unresolved) and caches the result. */
	uint32_t ResolveMeshId(RE::BSMultiStreamInstanceTriShape* shape);

	/** @brief Loads both LOD .nifs for this mesh id. */
	void EnsureLODMeshes(uint32_t meshId);

	/** @brief Returns the cached LOD mesh for a tier, or nullptr when none is loaded or it is unusable. The far tier falls back to the middle mesh. */
	const LODMesh* GetLODMesh(uint32_t meshId, LODTier tier) const;

	/** @brief Returns the current model-path generation for a shape pointer, or 0 when unknown. */
	uint64_t GetShapeGeneration(RE::BSMultiStreamInstanceTriShape* shape) const;

	/** @brief Removes a shape only when the delayed destruction still refers to the recorded generation. */
	void ForgetShape(RE::BSMultiStreamInstanceTriShape* shape, uint64_t expectedGeneration);

private:
	/** @brief Loads one tier's .nif into an entry, once. */
	static void LoadLODMesh(LODMesh& entry, const std::string& stem, LODTier tier);

	// Mesh identity uses the normalized full relative model path so two mods may safely
	// ship the same filename in different directories. IDs remain 1-based.
	std::vector<std::string> sourcePaths;
	// LOD lookup intentionally keeps the historical filename-stem convention.
	std::vector<std::string> lodStems;
	std::unordered_map<RE::BSMultiStreamInstanceTriShape*, uint32_t> idByShape;

	struct ShapePathRecord
	{
		std::string path;
		uint64_t generation = 0;
	};
	// Model-path publication crosses from LoadGrassType to the render thread. Keep the
	// resolved-id map under the same lock so allocator pointer reuse cannot return a stale ID.
	std::unordered_map<RE::BSMultiStreamInstanceTriShape*, ShapePathRecord> pathByShape;
	uint64_t nextPathGeneration = 1;
	mutable std::mutex stemMutex;

	// Parallel to stems, indexed by meshId - 1 then by LODTier. A deque to keep GetLODMesh's pointers valid after growth.
	std::deque<std::array<LODMesh, (size_t)LODTier::kCount>> lodMeshes;
};
