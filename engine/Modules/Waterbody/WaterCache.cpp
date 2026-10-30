#include "WaterCache.h"

#include <BS_thread_pool.hpp>
#include <cmath>
#include <limits>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <string_view>
#include <bit>
#include <unordered_set>
#include <Windows.h>
#include <type_traits>

#include "Utils/WinApi.h"

namespace
{
	// Far beyond normal terrain heights, but below sentinel/garbage values
	constexpr float kMaxValidCellHeight = 50000.0f;

	bool IsValidCellHeight(const float height)
	{
		// Cell records may use sentinel or garbage heights for absent water data
		return std::isfinite(height) &&
		       height != FLT_MIN &&
		       height != FLT_MAX &&
		       std::fabs(height) < kMaxValidCellHeight;
	}

	// Vanilla worldspace inheritance is shallow, but guard malformed mod cycles.
	template <class T>
	T* ResolveWaterParent(T* worldSpace)
	{
		for (unsigned depth = 0; worldSpace && worldSpace->parentWorld &&
			worldSpace->parentUseFlags.all(RE::TESWorldSpace::ParentUseFlag::kUseWaterData); ++depth) {
			if (depth == 64 || worldSpace->parentWorld == worldSpace)
				return nullptr;
			worldSpace = worldSpace->parentWorld;
		}
		return worldSpace;
	}

	// Exact negative floor division, including INT32_MIN (without x-(lod-1) overflow).
	constexpr int32_t FloorDiv(const int32_t value, const int32_t divisor) noexcept
	{
		const int32_t quotient = value / divisor;
		return quotient - (value % divisor < 0 ? 1 : 0);
	}

	bool IsSafeCacheKey(std::string_view key)
	{
		return !key.empty() && key.find("..") == std::string_view::npos &&
			std::none_of(key.begin(), key.end(), [](unsigned char c) {
				return c < 32 || c == '/' || c == '\\' || c == ':' || c == '*' ||
					c == '?' || c == '"' || c == '<' || c == '>' || c == '|';
			});
	}

	RE::TESWaterForm* LookupWaterForm(const RE::FormID formID)
	{
		if (!formID)
			return nullptr;

		auto* form = RE::TESWaterForm::LookupByID<RE::TESWaterForm>(formID);
		if (!form || form->formType != RE::FormType::Water)
			return nullptr;

		return form;
	}
}

WaterCache::~WaterCache()
{
	// The monitor is declared before several objects it accesses; relying on
	// default reverse member destruction would destroy those objects first.
	if (async.monitor.joinable()) {
		async.monitor.request_stop();
		async.monitor.join();
	}
	if (async.pool)
		async.pool->wait();
}

bool WaterCache::SetCurrentWorldSpace(const RE::TESWorldSpace* worldSpace)
{
	std::scoped_lock lock(currentCacheMutex);
	return SetCurrentWorldSpaceLocked(worldSpace);
}

bool WaterCache::SetCurrentWorldSpaceLocked(const RE::TESWorldSpace* worldSpace)
{
	if (!worldSpace) {
		currentCache.reset();
		currentWorldSpace.clear();
		currentWorldSpaceFormID = 0;
		return false;
	}

	worldSpace = ResolveWaterParent(worldSpace);
	if (!worldSpace)
		return false;
	if (currentCache && currentWorldSpaceFormID == worldSpace->GetFormID())
		return true;

	const char* editorID = worldSpace->GetFormEditorID();
	if (!editorID || !*editorID)
		return false;
	const std::string newWorldSpace{ editorID };
	if (!IsSafeCacheKey(newWorldSpace))
		return false;
	if (currentCache && currentWorldSpace == newWorldSpace) {
		currentWorldSpaceFormID = worldSpace->GetFormID();
		return true;
	}

	const auto snap = std::atomic_load_explicit(&cacheMap, std::memory_order_acquire);
	if (!snap) {
		logger::error("[Waterbody] [Cache] Failed to get cache snapshot");
		currentCache.reset();
		currentWorldSpace.clear();
		currentWorldSpaceFormID = 0;
		return false;
	}

	const auto it = snap->find(newWorldSpace);
	if (it == snap->end()) {
		logger::error("[Waterbody] [Cache] Failed to get runtime cache for {}", newWorldSpace);
		currentCache.reset();
		currentWorldSpace.clear();
		currentWorldSpaceFormID = 0;
		return false;
	}

	currentCache = it->second;
	currentWorldSpace = newWorldSpace;
	currentWorldSpaceFormID = worldSpace->GetFormID();
	logger::debug("[Waterbody] [Cache] Runtime cache for {} activated", currentWorldSpace);

	return true;
}

WaterCache::InstructionResult WaterCache::GetInstructions(const RE::TESWorldSpace* worldSpace, const uint32_t lodLevel, const uint32_t x, const uint32_t y)
{
	std::scoped_lock lock(currentCacheMutex);

	if (!SetCurrentWorldSpaceLocked(worldSpace) || !currentCache)
		return {};

	// Held alongside the pointer so a concurrent LoadCaches can't free it mid-use.
	InstructionResult result;
	result.cache = currentCache;
	// Original API carries signed cell coordinates in uint32_t fields.
	// bit_cast makes the negative-cell contract explicit and portable on x64.
	result.instructions = currentCache->GetInstructions(static_cast<int32_t>(lodLevel),
		std::bit_cast<int32_t>(x), std::bit_cast<int32_t>(y));
	return result;
}

bool WaterCache::IsOpenOceanCell(const RE::TESWorldSpace* worldSpace, const int32_t x, const int32_t y)
{
	// Query the unmerged LOD4 records so local water geometry can use the same
	// procedural cell classification as PIXL's replacement water tiles.
	const auto result = GetInstructions(worldSpace, 4, static_cast<uint32_t>(x), static_cast<uint32_t>(y));
	if (!result.instructions)
		return false;

	for (const auto& instruction : *result.instructions) {
		const auto right = static_cast<int64_t>(instruction.x) + instruction.size;
		const auto top = static_cast<int64_t>(instruction.y) + instruction.size;
		if (x >= instruction.x && x < right && y >= instruction.y && y < top)
			return (instruction.flags & Instruction::kOpenOcean) != 0;
	}
	return false;
}

bool WaterCache::GetOpenOceanCellHeight(const RE::TESWorldSpace* worldSpace,
	const int32_t x, const int32_t y, float& height)
{
	const auto result = GetInstructions(worldSpace, 4, static_cast<uint32_t>(x), static_cast<uint32_t>(y));
	if (!result.instructions)
		return false;
	for (const auto& instruction : *result.instructions) {
		const auto right = static_cast<int64_t>(instruction.x) + instruction.size;
		const auto top = static_cast<int64_t>(instruction.y) + instruction.size;
		if (x >= instruction.x && x < right && y >= instruction.y && y < top &&
			(instruction.flags & Instruction::kOpenOcean) != 0 && IsValidCellHeight(instruction.waterHeight)) {
			height = instruction.waterHeight;
			return true;
		}
	}
	return false;
}

std::vector<WaterCache::Instruction>* WaterCache::RuntimeCache::GetInstructions(const int32_t lodLevel, const int32_t x, const int32_t y)
{
	// Only actual renderer LODs are legal; countr_zero(6), for example,
	// silently selected the LOD4 bucket in the original implementation.
	if (lodLevel != 4 && lodLevel != 8 && lodLevel != 16 && lodLevel != 32)
		return nullptr;

	const auto lodIndex = std::countr_zero(static_cast<uint32_t>(lodLevel)) - 2u;
	if (lodIndex >= instructions.size())
		return nullptr;

	auto& lodInstructions = instructions[lodIndex];
	const auto& bounds = header.bounds;
	int32_t lodCellX, lodCellY, lodMinX, lodMinY, lodMaxX, lodMaxY;
	GetLODCoords(lodLevel, x, y, lodCellX, lodCellY);
	GetLODCoords(lodLevel, bounds.minX, bounds.minY, lodMinX, lodMinY);
	GetLODCoords(lodLevel, bounds.maxX, bounds.maxY, lodMaxX, lodMaxY);
	if (lodCellX < lodMinX || lodCellY < lodMinY || lodCellX > lodMaxX || lodCellY > lodMaxY)
		return nullptr;

	const auto lodWidth = static_cast<size_t>(static_cast<int64_t>(lodMaxX) - lodMinX + 1);
	const auto index = static_cast<size_t>(static_cast<int64_t>(lodCellY) - lodMinY) * lodWidth +
		static_cast<size_t>(static_cast<int64_t>(lodCellX) - lodMinX);
	return index < lodInstructions.size() ? &lodInstructions[index] : nullptr;
}

void WaterCache::GenerateTamrielPrecache()
{
	// This function is strictly for developer use for building an offline pre-cache using sheson's SSE Terrain Tamriel Full Extend
	// which adds back missing terrain mesh data
	const auto tamriel = RE::TESForm::LookupByEditorID<RE::TESWorldSpace>("Tamriel");
	if (!tamriel) {
		logger::error("[Waterbody] [Cache] Failed to load Tamriel WorldSpace");
		return;
	}

	PreCache cache;
	WorldSpaceHeader& hdr = cache.header;
	hdr.bounds.minX = -96;
	hdr.bounds.minY = -96;
	hdr.bounds.maxX = 95;
	hdr.bounds.maxY = 96;
	hdr.width = cache.header.bounds.maxX - cache.header.bounds.minX + 1;
	hdr.height = cache.header.bounds.maxY - cache.header.bounds.minY + 1;
	hdr.dataCount = hdr.width * hdr.height;

	cache.heights.resize(hdr.dataCount);

	BuildPreCache(tamriel, cache);

	TryWriteCacheToFile("Tamriel_precache.wpc", hdr, cache.heights);
}

bool WaterCache::LoadOrGenerateCaches()
{
	if (!LoadCaches()) {
		logger::info("[Waterbody] [Cache] Could not load caches - regenerating...");
		return RegenerateCaches();
	}

	return true;
}

bool WaterCache::RegenerateCaches()
{
	logger::info("[Waterbody] [Cache] Regenerating caches (preserving last-good files)...");

	// Generate each replacement atomically. Deleting all good caches before a
	// potentially failing regeneration caused worlds to lose water on next boot.
	// Stale files not referenced by loaded worldspaces are harmless.
	if (async.running.load(std::memory_order_acquire)) {
		logger::warn("[Waterbody] [Cache] Regeneration already running");
		return false;
	}
	return GenerateCaches();
}

bool WaterCache::LoadCaches()
{
	const auto t0 = std::chrono::steady_clock::now();

	auto worldSpaces = GetValidWorldSpaces();

	auto newCacheMap = std::make_shared<CacheMap>();
	uint32_t unavailableCount = 0;

	for (auto& worldSpace : worldSpaces) {
		const auto editorID = worldSpace ? worldSpace->GetFormEditorID() : nullptr;
		if (!worldSpace || !editorID || !*editorID) {
			logger::warn("[Waterbody] [Cache] WorldSpace has no EditorID - skipping");
			continue;
		}

		std::string key{ editorID };

		if (!IsSafeCacheKey(key) || newCacheMap->contains(key))
			continue;

		DiskCache diskCache;
		const auto fileName = std::format("{}_ocean2_cache.wc", key);
		if (!TryReadCacheFromFile(fileName, diskCache.header, diskCache.instructions)) {
			logger::info("[Waterbody] [Cache] Could not locate disk cache for {}", key);
			unavailableCount++;
			continue;
		}

		logger::debug("[Waterbody] [Cache] Loaded cache for {} - Bounds {},{}  {},{} - Instructions {}", editorID, diskCache.header.bounds.minX, diskCache.header.bounds.minY, diskCache.header.bounds.maxX, diskCache.header.bounds.maxY, diskCache.header.dataCount);

		auto newCache = std::make_unique<RuntimeCache>();
		if (!TryBuildRuntimeCache(diskCache, *newCache)) {
			logger::warn("[Waterbody] [Cache] Failed to build runtime cache for {}", key);
			unavailableCount++;
			continue;
		}

		newCacheMap->emplace(std::move(key), std::move(newCache));
	}

	if (unavailableCount) {
		logger::info("[Waterbody] [Cache] Loaded {} / {} worldspace caches ({} unavailable)", newCacheMap->size(), worldSpaces.size(), unavailableCount);
	}

	if (newCacheMap->empty()) {
		return false;
	}

	// Commit map + active cache together. Readers can never observe an active
	// worldspace from one snapshot and its stale cache from another.
	{
		std::scoped_lock lock(currentCacheMutex);
		if (!currentWorldSpace.empty()) {
			if (const auto it = newCacheMap->find(currentWorldSpace); it != newCacheMap->end()) {
				currentCache = it->second;
			} else {
				currentCache.reset();
				currentWorldSpace.clear();
				currentWorldSpaceFormID = 0;
			}
		}
		std::atomic_store_explicit(&cacheMap,
			std::static_pointer_cast<const CacheMap>(newCacheMap), std::memory_order_release);
	}

	const auto t1 = std::chrono::steady_clock::now();
	const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
	logger::info("[Waterbody] [Cache] Caches loaded in {} ms", ms);

	return true;
}

bool WaterCache::GenerateCaches()
{
	// The gate covers the whole enqueue/monitor lifecycle, not just construction
	// of the worker pool. A second invocation must not replace a running pool.
	if (async.running.exchange(true, std::memory_order_acq_rel)) {
		logger::warn("[Waterbody] [Cache] Build already running");
		return false;
	}

	try {
		const auto worldSpaces = GetValidWorldSpaces();
		if (worldSpaces.empty()) {
			async.running.store(false, std::memory_order_release);
			return false;
		}

		std::error_code ec;
		std::filesystem::create_directories(Util::PathHelpers::GetWaterbodyCachePath(), ec);
		if (ec) {
			logger::error("[Waterbody] [Cache] Cannot create cache directory: {}", ec.message());
			async.running.store(false, std::memory_order_release);
			return false;
		}

		// A completed jthread is still joinable. Join it before reassigning the
		// pool or monitor to avoid racing the previous run's last instructions.
		if (async.monitor.joinable())
			async.monitor.join();

		const unsigned hw = std::max(1u, Util::GetPerformanceCoreCount());
		const unsigned threads = std::max(1u, hw > 4 ? hw - 4 : (hw * 3) / 4);
		async.pool = std::make_unique<BS::thread_pool<>>(threads);
		async.failed.store(false, std::memory_order_relaxed);
		buildProgress.Start(static_cast<uint32_t>(worldSpaces.size()));

		logger::info("[Waterbody] [Cache] Building {} worldspaces on {} threads",
			worldSpaces.size(), threads);

		for (auto* worldSpace : worldSpaces) {
			const char* editorID = worldSpace ? worldSpace->GetFormEditorID() : nullptr;
			if (!editorID || !IsSafeCacheKey(editorID)) {
				buildProgress.Done(false);
				async.failed.store(true, std::memory_order_relaxed);
				continue;
			}
			// Own the editor ID. Capturing the game's char* could dangle if
			// plugin/WorldSpace metadata changes during an asynchronous pass.
			const std::string worldName{ editorID };
			async.pool->detach_task([this, worldSpace, worldName] {
				bool success = false;
				try {
					DiskCache cache{};
					const auto fileName = std::format("{}_ocean2_cache.wc", worldName);
					success = BuildDiskCache(worldSpace, cache) &&
						TryWriteCacheToFile(fileName, cache.header, cache.instructions);
				} catch (const std::exception& e) {
					logger::error("[Waterbody] [Cache] {} worker exception: {}", worldName, e.what());
				} catch (...) {
					logger::error("[Waterbody] [Cache] {} worker exception", worldName);
				}
				if (!success) {
					async.failed.store(true, std::memory_order_relaxed);
					logger::error("[Waterbody] [Cache] {} generation failed", worldName);
				}
				buildProgress.Done(success);
			});
		}

		async.monitor = std::jthread([this](const std::stop_token& st) {
			using namespace std::chrono_literals;
			while (!st.stop_requested() && buildProgress.DoneCount() < buildProgress.total.load())
				std::this_thread::sleep_for(50ms);

			if (async.pool)
				async.pool->wait();
			buildProgress.Stop();
			if (st.stop_requested()) {
				async.running.store(false, std::memory_order_release);
				return;  // Renderer shutdown: do not call game forms or reload caches.
			}
			logger::info("[Waterbody] [Cache] Build completed in {} ms ({} / {} succeeded, {} failed)",
				buildProgress.ElapsedMs(), buildProgress.completed.load(), buildProgress.total.load(),
				buildProgress.failed.load());
			try {
				if (!LoadCaches())
					async.failed.store(true, std::memory_order_relaxed);
			} catch (const std::exception& e) {
				logger::error("[Waterbody] [Cache] Reload failed: {}", e.what());
				async.failed.store(true, std::memory_order_relaxed);
			} catch (...) {
				logger::error("[Waterbody] [Cache] Reload failed");
				async.failed.store(true, std::memory_order_relaxed);
			}
			async.running.store(false, std::memory_order_release);
		});
	} catch (const std::exception& e) {
		logger::error("[Waterbody] [Cache] Failed to start workers: {}", e.what());
		if (async.pool)
			async.pool->wait();
		buildProgress.Stop();
		async.failed.store(true, std::memory_order_relaxed);
		async.running.store(false, std::memory_order_release);
		return false;
	}
	return true;
}

void WaterCache::BuildPreCache(RE::TESWorldSpace* worldSpace, PreCache& cache)
{
	const auto& hdr = cache.header;
	const auto& [minX, minY, maxX, maxY] = hdr.bounds;
	auto& heights = cache.heights;

	const auto files = worldSpace->sourceFiles.array;

	for (auto y = minY; y <= maxY; ++y) {
		for (auto x = minX; x <= maxX; ++x) {
			const int32_t idx = (y - minY) * hdr.width + x - minX;
			RE::FormID rawFormID;
			float landHeight, waterHeight;

			TryGetCellData(worldSpace, files, x, y, rawFormID, waterHeight, landHeight, false);
			heights[idx] = { landHeight, waterHeight };
		}
	}
}

bool WaterCache::BuildDiskCache(RE::TESWorldSpace* worldSpace, DiskCache& diskCache)
{
	const auto t0 = std::chrono::steady_clock::now();

	diskCache.header = {};
	diskCache.instructions = {};
	auto& hdr = diskCache.header;
	auto& [minX, minY, maxX, maxY] = diskCache.header.bounds;

	const auto editorID = worldSpace->editorID;

	bool hasPrecache = false;
	PreCache preCache;

	const auto wsMin = worldSpace->minimumCoords;
	const auto wsMax = worldSpace->maximumCoords;
	const auto validWorldCoord = [](float value) {
		return std::isfinite(value) && value != FLT_MAX && value != FLT_MIN &&
			std::fabs(value) < 1.0e8f;
	};
	if (!validWorldCoord(wsMin.x) || !validWorldCoord(wsMin.y) ||
		!validWorldCoord(wsMax.x) || !validWorldCoord(wsMax.y) ||
		wsMax.x <= wsMin.x || wsMax.y <= wsMin.y) {
		logger::warn("[Waterbody] [Cache] {}: Invalid worldspace extents", editorID.c_str());
		return false;
	}

	if (editorID == "Tamriel") {
		if (TryReadCacheFromFile("Tamriel_precache.wpc", preCache.header, preCache.heights)) {
			diskCache.header = preCache.header;
			hasPrecache = true;
		} else {
			logger::warn("[Waterbody] [Cache] Tamriel: Failed to load precache, falling back to generation");
			Util::WorldToCell(wsMin, minX, minY);
			Util::WorldToCell(wsMax, maxX, maxY);
			maxX -= 1;
			maxY -= 1;
			hdr.width = maxX - minX + 1;
			hdr.height = maxY - minY + 1;
			hdr.dataCount = 0;
		}
	} else {
		Util::WorldToCell(wsMin, minX, minY);
		Util::WorldToCell(wsMax, maxX, maxY);
		maxX -= 1;
		maxY -= 1;
		hdr.width = maxX - minX + 1;
		hdr.height = maxY - minY + 1;
		hdr.dataCount = 0;
	}

	bool invalidWS = wsMin.x == FLT_MIN || wsMin.y == FLT_MIN || wsMax.x == FLT_MAX || wsMax.y == FLT_MAX || wsMin.x == FLT_MAX || wsMin.y == FLT_MAX || wsMax.x == FLT_MIN || wsMax.y == FLT_MIN;
	invalidWS = invalidWS || hdr.width <= 0 || hdr.height <= 0 || hdr.width > 512 || hdr.height > 512;

	if (invalidWS) {
		logger::warn("[Waterbody] [Cache] {}: Invalid WorldSpace - skipping", editorID.c_str());
		diskCache = {};
		return false;
	}

	logger::debug("[Waterbody] [Cache] {}: Building disk cache...", editorID.c_str());

	const auto files = worldSpace->sourceFiles.array;

	auto cellData = std::vector<CellData>(hdr.width * hdr.height);

	int32_t waterCellCount = 0;
	int32_t precacheFallbackCount = 0;
	int32_t skippedMissingCellDataCount = 0;
	int32_t skippedInvalidHeightCount = 0;
	int32_t skippedUnresolvedFormCount = 0;
	int32_t firstUnresolvedFormX = 0;
	int32_t firstUnresolvedFormY = 0;
	RE::FormID firstUnresolvedFormID = 0;

	for (auto y = minY; y <= maxY; ++y) {
		for (auto x = minX; x <= maxX; ++x) {
			const int32_t idx = (y - minY) * hdr.width + x - minX;

			RE::FormID formID = 0;
			float landHeight = FLT_MAX;
			float waterHeight = FLT_MAX;

			if (!TryGetCellData(worldSpace, files, x, y, formID, waterHeight, landHeight, true)) {
				if (!hasPrecache) {
					skippedMissingCellDataCount++;
					cellData[idx] = {};
					continue;
				}

				const auto precacheIdx = static_cast<size_t>(idx);
				if (precacheIdx >= preCache.heights.size()) {
					// Corrupt precache files can have headers that outlive their payload
					skippedMissingCellDataCount++;
					cellData[idx] = {};
					continue;
				}

				const auto [land, water] = preCache.heights[precacheIdx];
				landHeight = land;
				waterHeight = water;
				precacheFallbackCount++;
			}

			const bool validHeights = IsValidCellHeight(waterHeight) && IsValidCellHeight(landHeight);

			if (validHeights && waterHeight > landHeight) {
				if (!formID && worldSpace->worldWater)
					formID = worldSpace->worldWater->formID;  // Use default world water if no water form set
			} else {
				if (!validHeights)
					skippedInvalidHeightCount++;
				// Discard invalid tiles before they can generate floating water planes
				formID = 0;
			}

			const RE::FormID requestedFormID = formID;
			RE::TESWaterForm* form = LookupWaterForm(formID);

			if (!form && requestedFormID && worldSpace->worldWater && requestedFormID != worldSpace->worldWater->formID) {
				formID = worldSpace->worldWater->formID;
				form = LookupWaterForm(formID);
			}

			if (requestedFormID && !form) {
				if (!skippedUnresolvedFormCount) {
					firstUnresolvedFormX = x;
					firstUnresolvedFormY = y;
					firstUnresolvedFormID = requestedFormID;
				}
				skippedUnresolvedFormCount++;
				cellData[idx] = {};
				continue;
			}

			if (form)
				waterCellCount++;

			cellData[idx] = { { landHeight, waterHeight }, formID, form, false };
		}
	}
	ClassifyOpenOceanCells(hdr, cellData);
	// The water-placement cache now stores the procedural ocean classification.
	// Force old layouts to regenerate rather than interpreting them as current.
	hdr.label = Util::FCC("WTC2");

	if (skippedUnresolvedFormCount) {
		logger::warn("[Waterbody] [Cache] {}: Skipped {} cells due to unresolvable water forms (first at {},{} form {:08X})",
			editorID.c_str(), skippedUnresolvedFormCount, firstUnresolvedFormX, firstUnresolvedFormY, firstUnresolvedFormID);
	}

	if (precacheFallbackCount || skippedMissingCellDataCount || skippedInvalidHeightCount) {
		logger::debug("[Waterbody] [Cache] {}: {} cells used precache fallback, {} cells skipped due to missing data, {} cells skipped due to invalid heights",
			editorID.c_str(), precacheFallbackCount, skippedMissingCellDataCount, skippedInvalidHeightCount);
	}

	logger::debug("[Waterbody] [Cache] {}: Generating instructions for {} water cells...", editorID.c_str(), waterCellCount);

	int32_t instructionCount;
	GenerateInstructions(4, diskCache, cellData, instructionCount);
	logger::debug("[Waterbody] [Cache] {}: LOD4 - {} instructions generated", editorID.c_str(), instructionCount);

	GenerateInstructions(8, diskCache, cellData, instructionCount);
	logger::debug("[Waterbody] [Cache] {}: LOD8 - {} instructions generated", editorID.c_str(), instructionCount);

	GenerateInstructions(16, diskCache, cellData, instructionCount);
	logger::debug("[Waterbody] [Cache] {}: LOD16 - {} instructions generated", editorID.c_str(), instructionCount);

	GenerateInstructions(32, diskCache, cellData, instructionCount);
	logger::debug("[Waterbody] [Cache] {}: LOD32 - {} instructions generated", editorID.c_str(), instructionCount);

	diskCache.header.dataCount = static_cast<int32_t>(diskCache.instructions.size());

	const auto t1 = std::chrono::steady_clock::now();
	const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
	logger::debug("[Waterbody] [Cache] {}: Cache generated in {} ms", worldSpace->GetFormEditorID(), ms);
	return true;
}

void WaterCache::ClassifyOpenOceanCells(const WorldSpaceHeader& header, std::vector<CellData>& cellData)
{
	// Classify broad, same-level water connected to the worldspace's outer edge.
	// A local 5x5 water-coverage test identifies the open-water core; only a
	// two-cell fringe is grown from that core, so narrow rivers connected to the
	// sea do not inherit the ocean classification all the way inland.
	constexpr float kWaterLevelTolerance = 32.0f;
	constexpr size_t kMinimumOceanCells = 64;
	constexpr int32_t kMinimumOceanSpan = 8;
	constexpr int32_t kCoreCoverage = 20;
	constexpr int32_t kShoreFringeCells = 2;
	const int32_t width = header.width;
	const int32_t height = header.height;
	if (width <= 0 || height <= 0 || cellData.size() != static_cast<size_t>(width) * height)
		return;

	std::vector<uint8_t> visited(cellData.size(), 0);
	std::vector<uint32_t> componentLabels(cellData.size(), 0);
	std::vector<int32_t> oceanDistance(cellData.size(), -1);
	std::vector<int32_t> component;
	std::vector<int32_t> pending;
	uint32_t nextComponentLabel = 1;
	for (int32_t start = 0; start < static_cast<int32_t>(cellData.size()); ++start) {
		const auto& seed = cellData[start];
		if (visited[start] || !seed.form || !IsValidCellHeight(seed.heights.water))
			continue;

		component.clear();
		pending.clear();
		pending.push_back(start);
		visited[start] = 1;
		bool reachesWorldspaceEdge = false;
		int32_t minX = width, minY = height, maxX = -1, maxY = -1;
		while (!pending.empty()) {
			const int32_t index = pending.back();
			pending.pop_back();
			const int32_t x = index % width;
			const int32_t y = index / width;
			component.push_back(index);
			minX = std::min(minX, x);
			minY = std::min(minY, y);
			maxX = std::max(maxX, x);
			maxY = std::max(maxY, y);
			reachesWorldspaceEdge |= x == 0 || y == 0 || x == width - 1 || y == height - 1;

			const int32_t neighbours[] = { index - 1, index + 1, index - width, index + width };
			for (size_t direction = 0; direction < std::size(neighbours); ++direction) {
				if ((direction == 0 && x == 0) || (direction == 1 && x == width - 1) ||
					(direction == 2 && y == 0) || (direction == 3 && y == height - 1))
					continue;
				const int32_t neighbourIndex = neighbours[direction];
				if (visited[neighbourIndex])
					continue;
				const auto& neighbour = cellData[neighbourIndex];
				if (!neighbour.form || !IsValidCellHeight(neighbour.heights.water) ||
					std::fabs(neighbour.heights.water - seed.heights.water) > kWaterLevelTolerance)
					continue;
				visited[neighbourIndex] = 1;
				pending.push_back(neighbourIndex);
			}
		}

		if (!reachesWorldspaceEdge || component.size() < kMinimumOceanCells ||
			maxX - minX + 1 < kMinimumOceanSpan || maxY - minY + 1 < kMinimumOceanSpan)
			continue;

		const uint32_t componentLabel = nextComponentLabel++;
		for (const int32_t index : component)
			componentLabels[index] = componentLabel;

		pending.clear();
		for (const int32_t index : component) {
			const int32_t x = index % width;
			const int32_t y = index / width;
			int32_t coveredCells = 0;
			for (int32_t oy = -2; oy <= 2; ++oy) {
				for (int32_t ox = -2; ox <= 2; ++ox) {
					const int32_t sx = x + ox;
					const int32_t sy = y + oy;
					if (sx >= 0 && sx < width && sy >= 0 && sy < height &&
						componentLabels[sy * width + sx] == componentLabel)
						++coveredCells;
				}
			}
			if (coveredCells >= kCoreCoverage) {
				oceanDistance[index] = 0;
				cellData[index].openOcean = true;
				pending.push_back(index);
			}
		}

		while (!pending.empty()) {
			const int32_t index = pending.back();
			pending.pop_back();
			const int32_t distance = oceanDistance[index];
			if (distance >= kShoreFringeCells)
				continue;
			const int32_t x = index % width;
			const int32_t y = index / width;
			const int32_t neighbours[] = { index - 1, index + 1, index - width, index + width };
			for (size_t direction = 0; direction < std::size(neighbours); ++direction) {
				if ((direction == 0 && x == 0) || (direction == 1 && x == width - 1) ||
					(direction == 2 && y == 0) || (direction == 3 && y == height - 1))
					continue;
				const int32_t neighbour = neighbours[direction];
				if (componentLabels[neighbour] != componentLabel || oceanDistance[neighbour] >= 0)
					continue;
				oceanDistance[neighbour] = distance + 1;
				cellData[neighbour].openOcean = true;
				pending.push_back(neighbour);
			}
		}
	}
}

void WaterCache::GenerateInstructions(const int32_t lodLevel, DiskCache& diskCache, const std::vector<CellData>& cellData, int32_t& instructionCount)
{
	instructionCount = 0;
	const auto& hdr = diskCache.header;
	const auto& [minX, minY, maxX, maxY] = hdr.bounds;
	int32_t lodMinX, lodMinY, lodMaxX, lodMaxY;
	GetLODCoords(lodLevel, minX, minY, lodMinX, lodMinY);
	GetLODCoords(lodLevel, maxX, maxY, lodMaxX, lodMaxY);

	auto& instructions = diskCache.instructions;
	instructions.reserve(lodLevel == 4 ? cellData.size() : cellData.size() / 5);
	std::vector<bool> processed(lodLevel * lodLevel);

	// Loop over all lod chunks
	for (int32_t lodY = lodMinY; lodY <= lodMaxY; ++lodY) {
		for (int32_t lodX = lodMinX; lodX <= lodMaxX; ++lodX) {
			// Chunk base coordinates
			const int32_t baseX = lodX * lodLevel - minX;
			const int32_t baseY = lodY * lodLevel - minY;

			int32_t cellsRemaining = lodLevel * lodLevel;
			std::ranges::fill(processed, false);

			// Loop over all cells within the chunk
			for (int32_t localY = 0; localY < lodLevel; ++localY) {
				for (int32_t localX = 0; localX < lodLevel; ++localX) {
					const size_t localIndex = localY * lodLevel + localX;
					// Skip any processed cells
					if (processed[localIndex])
						continue;

					// Array relative cell coordinates
					const int32_t cellX = baseX + localX;
					const int32_t cellY = baseY + localY;

					// Skip any cells outside the map
					if (cellX < 0 || cellX >= hdr.width || cellY < 0 || cellY >= hdr.height) {
						processed[localIndex] = true;
						cellsRemaining--;
						continue;
					}

					const int32_t cellIndex = cellY * hdr.width + cellX;
					const auto& [heights, formID, form, openOcean] = cellData[cellIndex];

					// Skip any cells without water
					if (!form) {
						processed[localIndex] = true;
						cellsRemaining--;
						continue;
					}

					// Neighbouring cells must match target values to merge
					const float targetHeight = heights.water;
					const auto targetType = formID;
					const uint8_t targetFlags = form->flags.underlying();
					const bool targetOpenOcean = openOcean;

					int32_t size = 1;
					int32_t maxSize = 1;
					if (lodLevel > 4) {
						// For LOD 4 we don't want combined meshes, they need to be toggled off as real cells load
						// For others, size can only grow to what's left in the chunk or the maximum map dimensions
						maxSize = std::min(lodLevel - localX, lodLevel - localY);
						maxSize = std::min(maxSize, hdr.width - cellX);
						maxSize = std::min(maxSize, hdr.height - cellY);
					}

					// Try to grow to the largest square possible starting at localX/localY
					for (int32_t candidateSize = 2; candidateSize <= maxSize; ++candidateSize) {
						bool failed = false;

						// Check new top row
						const int32_t newRowY = localY + candidateSize - 1;
						for (int32_t dx = 0; dx < candidateSize; ++dx) {
							const int32_t rx = baseX + (localX + dx);
							const int32_t ry = baseY + newRowY;
							const int32_t idx = ry * hdr.width + rx;
							const int32_t cLocalIdx = newRowY * lodLevel + localX + dx;
							const auto& [cHeights, cFormID, cForm, cOpenOcean] = cellData[idx];
							if (processed[cLocalIdx] || !cForm || cHeights.water != targetHeight || cFormID != targetType || cForm->flags.underlying() != targetFlags || cOpenOcean != targetOpenOcean) {
								failed = true;
								break;
							}
						}

						if (failed)
							break;

						// Check new right column, excluding the corner already checked above
						const int32_t newColX = localX + candidateSize - 1;
						for (int32_t dy = 0; dy < candidateSize - 1; ++dy) {
							const int32_t rx = baseX + newColX;
							const int32_t ry = baseY + (localY + dy);
							const int32_t idx = ry * hdr.width + rx;
							const int32_t cLocalIdx = (localY + dy) * lodLevel + newColX;
							const auto& [cHeights, cFormID, cForm, cOpenOcean] = cellData[idx];
							if (processed[cLocalIdx] || !cForm || cHeights.water != targetHeight || cFormID != targetType || cForm->flags.underlying() != targetFlags || cOpenOcean != targetOpenOcean) {
								failed = true;
								break;
							}
						}

						if (failed)
							break;

						// Success for this candidate size
						size = candidateSize;
					}

					// Mark all cells within this group as processed
					for (int32_t yy = localY; yy < localY + size; ++yy) {
						for (int32_t xx = localX; xx < localX + size; ++xx) {
							processed[yy * lodLevel + xx] = true;
						}
					}
					cellsRemaining -= size * size;
					instructionCount++;

					Instruction instruction;
					instruction.lodLevel = lodLevel;
					instruction.form.id = formID;
					// Convert back to game cell coords
					instruction.x = cellX + minX;
					instruction.y = cellY + minY;
					instruction.size = size;
					instruction.waterHeight = targetHeight;
					instruction.flags = targetOpenOcean ? Instruction::kOpenOcean : 0u;

					// Add new instruction to list
					instructions.push_back(instruction);

					if (cellsRemaining <= 0)
						break;
				}

				if (cellsRemaining <= 0)
					break;
			}
		}
	}
}

bool WaterCache::TryBuildRuntimeCache(const DiskCache& diskCache, RuntimeCache& cache)
{
	// For LOD4, 8, 16 and 32
	cache.instructions = std::vector<std::vector<std::vector<Instruction>>>(4);

	const auto& hdr = diskCache.header;
	const auto& [minX, minY, maxX, maxY] = hdr.bounds;

	cache.header = hdr;

	int32_t diskReadIndex = 0;
	int32_t skippedInvalidInstructionCount = 0;
	int32_t skippedUnresolvedFormCount = 0;

	for (int32_t lodLevelIdx = 0; lodLevelIdx < 4; ++lodLevelIdx) {
		auto& lodInstructions = cache.instructions[lodLevelIdx];

		const int32_t lodLevel = 1 << (lodLevelIdx + 2);
		int32_t lodMinX, lodMinY, lodMaxX, lodMaxY;
		GetLODCoords(lodLevel, minX, minY, lodMinX, lodMinY);
		GetLODCoords(lodLevel, maxX, maxY, lodMaxX, lodMaxY);
		const int32_t lodWidth = lodMaxX - lodMinX + 1;
		const int32_t lodHeight = lodMaxY - lodMinY + 1;

		if (lodWidth <= 0 || lodHeight <= 0 ||
			static_cast<int64_t>(lodWidth) * lodHeight >
				static_cast<int64_t>(hdr.width) * hdr.height)
			return false;
		lodInstructions.resize(static_cast<size_t>(lodWidth) * lodHeight);

		while (diskReadIndex < diskCache.instructions.size()) {
			auto instruction = diskCache.instructions[diskReadIndex];
			if (instruction.lodLevel != static_cast<uint32_t>(lodLevel))
				break;

			// Malformed, mixed-LOD, or malicious cache records must not enter
			// the runtime renderer's per-chunk placement vectors.
			if (instruction.size == 0 || instruction.size > static_cast<uint32_t>(lodLevel) ||
				(instruction.flags & ~Instruction::kOpenOcean) != 0 ||
				static_cast<int64_t>(instruction.x) < minX || static_cast<int64_t>(instruction.y) < minY ||
				static_cast<int64_t>(instruction.x) + instruction.size - 1 > maxX ||
				static_cast<int64_t>(instruction.y) + instruction.size - 1 > maxY ||
				FloorDiv(static_cast<int32_t>(static_cast<int64_t>(instruction.x) + instruction.size - 1), lodLevel) !=
					FloorDiv(instruction.x, lodLevel) ||
				FloorDiv(static_cast<int32_t>(static_cast<int64_t>(instruction.y) + instruction.size - 1), lodLevel) !=
					FloorDiv(instruction.y, lodLevel) ||
				!IsValidCellHeight(instruction.waterHeight)) {
				// Keep old or malformed disk caches from restoring bad water tiles
				skippedInvalidInstructionCount++;
				diskReadIndex++;
				continue;
			}

			int32_t lodX, lodY;
			GetLODCoords(lodLevel, instruction.x, instruction.y, lodX, lodY);
			lodX -= lodMinX;
			lodY -= lodMinY;

			if (lodX < 0 || lodY < 0 || lodX >= lodWidth || lodY >= lodHeight) {
				logger::warn("[Waterbody] [Cache] Skipping out-of-bounds LOD{} instruction at {},{}", lodLevel, instruction.x, instruction.y);
				diskReadIndex++;
				continue;
			}

			const RE::FormID serializedFormID = instruction.form.id;
			instruction.form.ptr = LookupWaterForm(serializedFormID);
			if (!instruction.form.ptr) {
				if (!skippedUnresolvedFormCount) {
					logger::warn("[Waterbody] [Cache] Failed to load WaterForm {:08X} at LOD{} cell {},{} - skipping instruction",
						serializedFormID, lodLevel, instruction.x, instruction.y);
				}
				skippedUnresolvedFormCount++;
				diskReadIndex++;
				continue;
			}

			if (!instruction.form.ptr->IsInitialized()) {
				logger::warn("[Waterbody] [Cache] WaterForm {:08X} is not initialized", serializedFormID);
				instruction.form.ptr->InitItemImpl();
			}

			lodInstructions[static_cast<size_t>(lodY) * lodWidth + lodX].push_back(instruction);

			diskReadIndex++;
		}
	}

	// The on-disk records are grouped by 4/8/16/32. Do not accept a
	// truncated/misordered tail masquerading as a valid partial cache.
	if (diskReadIndex != static_cast<int32_t>(diskCache.instructions.size()))
		return false;

	if (skippedInvalidInstructionCount) {
		logger::debug("[Waterbody] [Cache] Skipped {} cached instructions with invalid water heights", skippedInvalidInstructionCount);
	}

	if (skippedUnresolvedFormCount > 1) {
		logger::warn("[Waterbody] [Cache] Skipped {} cached instructions with unresolvable water forms", skippedUnresolvedFormCount);
	}

	return true;
}

std::vector<RE::TESWorldSpace*> WaterCache::GetValidWorldSpaces()
{
	std::vector<RE::TESWorldSpace*> validWorldSpaces;
	auto* dataHandler = RE::TESDataHandler::GetSingleton();
	if (!dataHandler)
		return validWorldSpaces;
	const auto worldSpaces = dataHandler->GetFormArray<RE::TESWorldSpace>();
	std::unordered_set<RE::TESWorldSpace*> seen;
	std::unordered_set<std::string> seenKeys;
	seen.reserve(worldSpaces.size());
	seenKeys.reserve(worldSpaces.size());
	for (auto* worldSpace : worldSpaces) {
		worldSpace = ResolveWaterParent(worldSpace);
		if (!worldSpace || !worldSpace->worldWater || !seen.insert(worldSpace).second)
			continue;
		const char* key = worldSpace->GetFormEditorID();
		if (key && IsSafeCacheKey(key) && seenKeys.emplace(key).second)
			validWorldSpaces.push_back(worldSpace);
	}
	return validWorldSpaces;
}

void WaterCache::GetLODCoords(const int32_t lodLevel, const int32_t x, const int32_t y, int32_t& outX, int32_t& outY)
{
	if (lodLevel <= 0) {
		outX = outY = 0;
		return;
	}
	outX = FloorDiv(x, lodLevel);
	outY = FloorDiv(y, lodLevel);
}

bool WaterCache::TryGetCellData(RE::TESWorldSpace* worldSpace, RE::TESFileArray* files, const int32_t x, const int32_t y, RE::FormID& outFormID, float& outWaterHeight, float& outLandHeight, bool resolveFormID)
{
	outFormID = 0;
	outWaterHeight = FLT_MAX;
	outLandHeight = FLT_MAX;

	// Some custom worlds have no contributing TESFile records. Indexing
	// data[-1] in that case previously crashed during cache generation.
	if (!worldSpace || !files || files->empty())
		return false;
	const auto size = static_cast<int32_t>(files->size());
	const auto arrayData = files->data();
	if (!arrayData)
		return false;

	int32_t fileIndex = size - 1;
	auto duplicateAt = [&](int32_t index) -> RE::TESFile* {
		return index >= 0 && arrayData[index] ? arrayData[index]->Duplicate() : nullptr;
	};
	RE::TESFile* file = duplicateAt(fileIndex);
	bool foundWaterData = false;
	bool foundLandData = false;

	// Search through the files in reverse load order to find the cell and read the water height and waterForm FormID
	do {
		if (file && file->SeekCell(worldSpace, x, y)) {
			ReadWaterData(file, outWaterHeight, outFormID);
			foundWaterData = true;
			break;
		}
		file = duplicateAt(--fileIndex);
	} while (fileIndex >= 0);

	if (resolveFormID && outFormID && file)
		outFormID = file->GetRuntimeFormID(outFormID);

	// Continue searching from the previous file to find the original record for the cell, this always has the landscape data - extract land height
	do {
		if (file && file->SeekCell(worldSpace, x, y) && file->SeekLandscapeForCurrentCell()) {
			ReadMinLandHeightData(file, outLandHeight);
			foundLandData = true;
			break;
		}
		file = duplicateAt(--fileIndex);
	} while (fileIndex >= 0);

	if (!foundWaterData || !foundLandData) {
		outFormID = 0;
		outWaterHeight = FLT_MIN;
		outLandHeight = 0;
		return false;
	}

	if (outWaterHeight == FLT_MAX)
		outWaterHeight = worldSpace->defaultWaterHeight;
	if (outLandHeight == FLT_MAX)
		outLandHeight = worldSpace->defaultLandHeight;

	return foundWaterData && foundLandData;
}

void WaterCache::ReadWaterData(RE::TESFile* file, float& waterHeight, RE::FormID& formID)
{
	if (!file->SeekNextSubrecordType(Util::FCC("XCLW")))
		return;

	file->ReadData(&waterHeight, 4);
	if (file->isBigEndian)
		waterHeight = std::bit_cast<float>(_byteswap_ulong(std::bit_cast<uint32_t>(waterHeight)));

	if (!file->SeekNextSubrecordType(Util::FCC("XCWT")))
		return;

	file->ReadData(&formID, 4);
	if (file->isBigEndian)
		formID = _byteswap_ulong(formID);
}

void WaterCache::ReadMinLandHeightData(RE::TESFile* file, float& minHeight)
{
	if (!file->SeekNextSubrecordType(Util::FCC("VHGT")))
		return;

	struct VHGTData
	{
		float offset;
		int8_t deltas[1089];
		byte padding[3];
	};
	VHGTData data{};

	file->ReadData(&data, 1096);

	float offset = data.offset;
	if (file->isBigEndian)
		offset = std::bit_cast<float>(_byteswap_ulong(std::bit_cast<std::uint32_t>(offset)));

	for (size_t y = 0; y < 33; y++) {
		float rowOffset = 0;
		offset += static_cast<float>(data.deltas[y * 33]);

		for (size_t x = 0; x < 33; x++) {
			const size_t index = y * 33 + x;

			if (x != 0)
				rowOffset += static_cast<float>(data.deltas[index]);

			const float height = (rowOffset + offset) * 8.0f;
			minHeight = std::min(height, minHeight);
		}
	}
}

template <typename T>
bool WaterCache::TryWriteCacheToFile(const std::string& name, const WorldSpaceHeader& header, const std::vector<T>& vec)
{
	namespace fs = std::filesystem;
	const fs::path path = Util::PathHelpers::GetWaterbodyCachePath() / name;

	std::error_code ec;
	fs::create_directories(path.parent_path(), ec);
	if (ec)
		return false;
	fs::path temporary = path;
	temporary += L".tmp";
	std::ofstream ofs(temporary, std::ios::binary | std::ios::trunc);
	if (!ofs) {
		logger::error("[Waterbody] [Cache] Failed to open '{}' for writing", temporary.string());
		return false;
	}

	auto cacheHeader = header;
	if constexpr (std::is_same_v<T, Instruction>)
		cacheHeader.label = Util::FCC("WTC2");
	else
		cacheHeader.label = Util::FCC("WTCH");
	if (vec.size() > static_cast<size_t>(std::numeric_limits<int32_t>::max()) ||
		cacheHeader.dataCount != static_cast<int32_t>(vec.size()))
		return false;

	ofs.write(reinterpret_cast<const char*>(&cacheHeader), sizeof(cacheHeader));
	if (!vec.empty())
		ofs.write(reinterpret_cast<const char*>(vec.data()), vec.size() * sizeof(T));
	ofs.flush();
	const bool written = ofs.good();
	ofs.close();
	if (!written) {
		logger::error("[Waterbody] [Cache] Failed to write cache '{}'", temporary.string());
		fs::remove(temporary, ec);
		return false;
	}
	// Atomic replacement on Windows: preserve last-good .wc on a failed write.
	if (!::MoveFileExW(temporary.c_str(), path.c_str(),
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
		logger::error("[Waterbody] [Cache] Failed to publish '{}' (Win32 {})", path.string(), ::GetLastError());
		fs::remove(temporary, ec);
		return false;
	}
	return true;
}

template <typename T>
bool WaterCache::TryReadCacheFromFile(const std::string& name, WorldSpaceHeader& header, std::vector<T>& vec)
{
	namespace fs = std::filesystem;
	const fs::path path = Util::PathHelpers::GetWaterbodyCachePath() / name;
	if (!fs::exists(path))
		return false;

	std::ifstream ifs(path, std::ios::binary);
	if (!ifs) {
		logger::error("[Waterbody] [Cache] Failed to open '{}' for reading", path.string());
		return false;
	}

	ifs.read(reinterpret_cast<char*>(&header), sizeof(header));
	constexpr uint32_t expectedLabel = std::is_same_v<T, Instruction> ? Util::FCC("WTC2") : Util::FCC("WTCH");
	if (!ifs || header.label != expectedLabel) {
		logger::error("[Waterbody] [Cache] Invalid or corrupt header for '{}'", path.string());
		return false;
	}
	// Match the dimensions accepted by generation, and guard the signed LOD
	// coordinate arithmetic (up to level 32) before trusting cached bounds.
	const auto& bounds = header.bounds;
	const std::int64_t spanX = static_cast<std::int64_t>(bounds.maxX) - bounds.minX + 1;
	const std::int64_t spanY = static_cast<std::int64_t>(bounds.maxY) - bounds.minY + 1;
	if (header.width <= 0 || header.height <= 0 || header.width > 512 || header.height > 512 ||
		spanX != header.width || spanY != header.height ||
		bounds.minX < std::numeric_limits<std::int32_t>::min() + 31 ||
		bounds.minY < std::numeric_limits<std::int32_t>::min() + 31 ||
		bounds.maxX > std::numeric_limits<std::int32_t>::max() - 31 ||
		bounds.maxY > std::numeric_limits<std::int32_t>::max() - 31 ||
		header.dataCount < 0 || header.dataCount > 4 * header.width * header.height) {
		logger::error("[Waterbody] [Cache] Invalid dimensions/count for '{}'", path.string());
		return false;
	}
	if constexpr (std::is_same_v<T, Heights>) {
		if (header.dataCount != header.width * header.height) {
			logger::error("[Waterbody] [Cache] Incomplete height grid for '{}'", path.string());
			return false;
		}
	}

	// Validate the declared allocation against this open file before resizing.
	// A truncated cache or negative signed count must fail into regeneration.
	const auto payloadStart = ifs.tellg();
	ifs.seekg(0, std::ios::end);
	const auto payloadEnd = ifs.tellg();
	if (!ifs || payloadStart < std::streampos(0) || payloadEnd < payloadStart ||
		header.dataCount < 0 ||
		static_cast<std::uint64_t>(header.dataCount) > static_cast<std::uint64_t>(payloadEnd - payloadStart) / sizeof(T) ||
		static_cast<std::uint64_t>(header.dataCount) > vec.max_size()) {
		logger::error("[Waterbody] [Cache] Invalid or truncated payload for '{}'", path.string());
		return false;
	}
	ifs.seekg(payloadStart);
	if (!ifs)
		return false;

	vec.resize(header.dataCount);
	if (!vec.empty()) {
		ifs.read(reinterpret_cast<char*>(vec.data()), vec.size() * sizeof(T));
		if (!ifs.good()) {
			logger::error("[Waterbody] [Cache] Failed to read payload for '{}'", path.string());
			return false;
		}
	}
	return true;
}
