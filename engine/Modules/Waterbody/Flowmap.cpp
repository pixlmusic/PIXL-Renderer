#include "Flowmap.h"

// Community Shaders Unified Water-derived file, retained and integrated by
// PIXL Renderer. SPDX-License-Identifier: GPL-3.0-or-later
// Additional permissions are described in the repository EXCEPTIONS.md.

#include <DDSTextureLoader.h>
#include <DirectXTex.h>
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <limits>
#include <string_view>
#include <span>
#include <d3d11.h>
#include <Windows.h>

namespace
{
	constexpr uint32_t FLOW_CELL_SIZE = 64;
	constexpr size_t FLOWMAP_MIP_LEVELS = 6;
	constexpr size_t kMaxSourceDDSBytes = 1024u * 1024u;  // 64x64 authored cell cap.
	constexpr uint64_t kMaxAtlasPixels = 128ull * 1024ull * 1024ull;
	constexpr std::wstring_view FLOWMAP_CACHE_PREFIX = L"Tamriel-Flowmap-v2";
	constexpr std::wstring_view LEGACY_FLOWMAP_CACHE_PREFIX = L"Tamriel-Flowmap";

	struct FlowPixel
	{
		uint8_t b;
		uint8_t g;
		uint8_t r;
		uint8_t a;
	};

	static_assert(sizeof(FlowPixel) == 4);

	bool IsEmptyFlowSample(const FlowPixel& sample)
	{
		return sample.r == 0 && sample.g == 0 && sample.b == 0 && sample.a == 0;
	}

	bool HasFlowmapCachePrefix(const std::filesystem::path& path, std::wstring_view prefix)
	{
		if (path.extension() != L".dds")
			return false;

		const auto name = path.filename().wstring();
		return name.size() > prefix.size() && name.starts_with(prefix) && name[prefix.size()] == L'.';
	}

	bool IsPIXLFlowmapCache(const std::filesystem::path& path)
	{
		return HasFlowmapCachePrefix(path, FLOWMAP_CACHE_PREFIX) ||
			HasFlowmapCachePrefix(path, LEGACY_FLOWMAP_CACHE_PREFIX);
	}

	// Vector-aware filtering: opposite currents must cancel rather than retain
	// their scalar speed and hallucinate strong flow in distant mips.
	FlowPixel BlendFlowSamples(std::span<const FlowPixel* const> samples, bool includeEmptyCoverage)
	{
		if (samples.empty())
			return {};

		float vectorX = 0.f, vectorY = 0.f, alpha = 0.f;
		size_t authoredCount = 0;
		for (const FlowPixel* sample : samples) {
			if (!sample)
				continue;
			alpha += sample->a;
			if (IsEmptyFlowSample(*sample))
				continue;

			const float x = static_cast<float>(sample->r) * (2.f / 255.f) - 1.f;
			const float y = static_cast<float>(sample->g) * (2.f / 255.f) - 1.f;
			const float length2 = x * x + y * y;
			const float strength = std::sqrt(std::max(1.01f - static_cast<float>(sample->b) / 255.f, 0.f));
			if (length2 > 1.e-8f) {
				const float weight = strength / std::sqrt(length2);
				vectorX += x * weight;
				vectorY += y * weight;
			}
			++authoredCount;
		}

		if (!authoredCount)
			return {};

		const size_t divisor = includeEmptyCoverage ? samples.size() : authoredCount;
		const float invCount = 1.f / static_cast<float>(divisor);
		vectorX *= invCount;
		vectorY *= invCount;
		const float speed = std::min(std::sqrt(vectorX * vectorX + vectorY * vectorY), 1.f);
		if (speed > 1.e-6f) {
			vectorX /= speed;
			vectorY /= speed;
		} else {
			vectorX = vectorY = 0.f;
		}

		const auto encode = [](float value) -> uint8_t {
			return static_cast<uint8_t>(std::clamp(std::lround(value * 255.f), 0l, 255l));
		};
		FlowPixel result{};
		result.r = encode(vectorX * 0.5f + 0.5f);
		result.g = encode(vectorY * 0.5f + 0.5f);
		result.b = encode(std::clamp(1.01f - speed * speed, 0.f, 1.f));
		result.a = encode(std::clamp(alpha * invCount / 255.f, 0.f, 1.f));
		return result;
	}

	void ReconcileFlowmapBoundaries(DirectX::ScratchImage& atlas, uint32_t cellsWide, uint32_t cellsHigh)
	{
		auto* image = atlas.GetImage(0, 0, 0);
		if (!image)
			return;

		auto pixelAt = [&](size_t x, size_t y) -> FlowPixel& {
			return reinterpret_cast<FlowPixel*>(image->pixels + y * image->rowPitch)[x];
		};

		for (uint32_t cellX = 1; cellX < cellsWide; ++cellX) {
			const size_t rightX = static_cast<size_t>(cellX) * FLOW_CELL_SIZE;
			for (size_t y = 0; y < image->height; ++y) {
				auto& left = pixelAt(rightX - 1, y);
				auto& right = pixelAt(rightX, y);
				if (IsEmptyFlowSample(left) || IsEmptyFlowSample(right))
					continue;
				const std::array<const FlowPixel*, 2> pair{ &left, &right };
				left = right = BlendFlowSamples(pair, false);
			}
		}

		for (uint32_t cellY = 1; cellY < cellsHigh; ++cellY) {
			const size_t bottomY = static_cast<size_t>(cellY) * FLOW_CELL_SIZE;
			for (size_t x = 0; x < image->width; ++x) {
				auto& top = pixelAt(x, bottomY - 1);
				auto& bottom = pixelAt(x, bottomY);
				if (IsEmptyFlowSample(top) || IsEmptyFlowSample(bottom))
					continue;
				const std::array<const FlowPixel*, 2> pair{ &top, &bottom };
				top = bottom = BlendFlowSamples(pair, false);
			}
		}
	}

	bool GenerateVectorAwareMips(DirectX::ScratchImage& atlas)
	{
		const auto& metadata = atlas.GetMetadata();
		for (size_t mip = 1; mip < metadata.mipLevels; ++mip) {
			const auto* source = atlas.GetImage(mip - 1, 0, 0);
			auto* destination = atlas.GetImage(mip, 0, 0);
			if (!source || !destination)
				return false;

			for (size_t y = 0; y < destination->height; ++y) {
				auto* destinationRow = reinterpret_cast<FlowPixel*>(destination->pixels + y * destination->rowPitch);
				for (size_t x = 0; x < destination->width; ++x) {
					std::array<const FlowPixel*, 4> footprint{};
					for (size_t sampleY = 0; sampleY < 2; ++sampleY) {
						const size_t sourceY = std::min(y * 2 + sampleY, source->height - 1);
						const auto* sourceRow = reinterpret_cast<const FlowPixel*>(source->pixels + sourceY * source->rowPitch);
						for (size_t sampleX = 0; sampleX < 2; ++sampleX) {
							const size_t sourceX = std::min(x * 2 + sampleX, source->width - 1);
							footprint[sampleY * 2 + sampleX] = &sourceRow[sourceX];
						}
					}
					destinationRow[x] = BlendFlowSamples(footprint, true);
				}
			}
		}
		return true;
	}
}

bool Flowmap::TryGetFlowmap(RE::NiPointer<RE::NiSourceTexture>& outFlowmapTex) const
{
	if (!flowmapTex || !flowmapTex->rendererTexture || !flowmapTex->rendererTexture->texture || !flowmapTex->rendererTexture->resourceView) {
		outFlowmapTex = nullptr;
		return false;
	}
	outFlowmapTex = flowmapTex;
	return true;
}

void Flowmap::Reset()
{
	flowmapTex = nullptr;
	width = 0;
	height = 0;
	invWidth = 0.0f;
	invHeight = 0.0f;
	offsetX = 0;
	offsetY = 0;
	loadedCacheFile.clear();
	preferredCacheFile.clear();
}

bool Flowmap::LoadOrGenerateFlowmap(bool useMips)
{
	// Keep last-good GPU texture until a replacement is validated.
	if (!LoadFlowmap()) {
		logger::info("[Waterbody] [Flowmap] Could not load flowmap - regenerating...");
		return RegenerateAndLoadFlowmap(useMips);
	}

	return true;
}

bool Flowmap::RegenerateAndLoadFlowmap(bool useMips)
{
	// Never discard valid cache files before replacement generation succeeds.
	if (!GenerateFlowmap(useMips)) {
		logger::error("[Waterbody] [Flowmap] Failed to generate flowmap; last-good cache preserved");
		return false;
	}
	if (!LoadFlowmap()) {
		logger::error("[Waterbody] [Flowmap] Failed to load regenerated flowmap");
		return false;
	}

	// Only after loading a valid replacement may we remove stale PIXL caches.
	namespace fs = std::filesystem;
	std::error_code ec;
	const fs::path dir = Util::PathHelpers::GetDataPath() / "textures" / "water" / "flowmaps";
	fs::directory_iterator end;
	for (fs::directory_iterator it(dir, ec); !ec && it != end; it.increment(ec)) {
		const auto& entry = *it;
		if (!entry.is_regular_file(ec) || ec)
			continue;
		const auto& path = entry.path();
		if (!IsPIXLFlowmapCache(path) || path == loadedCacheFile)
			continue;
		std::error_code removeError;
		fs::remove(path, removeError);
		if (removeError)
			logger::warn("[Waterbody] [Flowmap] Could not prune stale cache '{}': {}", path.string(), removeError.message());
	}
	logger::debug("[Waterbody] [Flowmap] Flowmap regenerated and loaded");
	return true;
}

bool Flowmap::LoadFlowmap()
{
	namespace fs = std::filesystem;
	const fs::path dir = Util::PathHelpers::GetDataPath() / "textures" / "water" / "flowmaps";
	std::error_code ec;
	if (!fs::is_directory(dir, ec) || ec)
		return false;

	struct Candidate
	{
		fs::path path;
		fs::file_time_type modified;
		int32_t width{}, height{}, offsetX{}, offsetY{};
	};
	std::vector<Candidate> candidates;
	const auto parseInt = [](std::string_view value, int32_t& output) {
		const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), output);
		return error == std::errc{} && end == value.data() + value.size();
	};

	fs::directory_iterator end;
	for (fs::directory_iterator it(dir, ec); !ec && it != end; it.increment(ec)) {
		const auto& entry = *it;
		if (!entry.is_regular_file(ec) || ec ||
			!HasFlowmapCachePrefix(entry.path(), FLOWMAP_CACHE_PREFIX))
			continue;

		const std::string stem = entry.path().stem().string();
		std::array<std::string_view, 5> parts{};
		size_t begin = 0;
		bool valid = true;
		for (size_t n = 0; n < parts.size(); ++n) {
			const size_t separator = stem.find('.', begin);
			if ((n == 4 && separator != std::string::npos) ||
				(n != 4 && separator == std::string::npos)) {
				valid = false;
				break;
			}
			parts[n] = std::string_view(stem).substr(begin,
				separator == std::string::npos ? std::string::npos : separator - begin);
			begin = separator == std::string::npos ? stem.size() : separator + 1;
		}
		if (!valid || parts[0] != "Tamriel-Flowmap-v2")
			continue;

		Candidate candidate{};
		candidate.path = entry.path();
		if (!parseInt(parts[1], candidate.width) || !parseInt(parts[2], candidate.height) ||
			!parseInt(parts[3], candidate.offsetX) || !parseInt(parts[4], candidate.offsetY) ||
			candidate.width <= 0 || candidate.height <= 0 ||
			static_cast<int64_t>(candidate.width) * FLOW_CELL_SIZE > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
			static_cast<int64_t>(candidate.height) * FLOW_CELL_SIZE > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
			static_cast<uint64_t>(candidate.width) * candidate.height * FLOW_CELL_SIZE * FLOW_CELL_SIZE > kMaxAtlasPixels)
			continue;
		candidate.modified = entry.last_write_time(ec);
		if (ec) {
			ec.clear();
			continue;
		}
		candidates.push_back(std::move(candidate));
	}
	// Deterministically prefer the newest successfully published cache; stale
	// atlas dimensions elsewhere in this directory no longer win iterator order.
	std::sort(candidates.begin(), candidates.end(), [&](const Candidate& a, const Candidate& b) {
		if (a.path == b.path)
			return false;
		if (a.path == preferredCacheFile)
			return true;
		if (b.path == preferredCacheFile)
			return false;
		return a.modified == b.modified ? a.path.native() > b.path.native() : a.modified > b.modified;
	});

	for (const auto& candidate : candidates) {
		const auto relative = std::format(R"(textures\water\flowmaps\{})", candidate.path.filename().string());
		RE::NiPointer<RE::NiTexture> tex;
		RE::BSShaderManager::GetTexture(relative.c_str(), true, tex, false);
		if (!tex || tex->GetRTTI() != globals::rtti::NiSourceTextureRTTI.get())
			continue;
		auto* source = static_cast<RE::NiSourceTexture*>(tex.get());
		if (!source || !source->rendererTexture || !source->rendererTexture->texture ||
			!source->rendererTexture->resourceView)
			continue;

		// Commit only once both the DDS name and the game's GPU texture are valid.
		flowmapTex = RE::NiPointer(source);
		width = candidate.width;
		height = candidate.height;
		offsetX = candidate.offsetX;
		offsetY = candidate.offsetY;
		invWidth = 1.f / static_cast<float>(width);
		invHeight = 1.f / static_cast<float>(height);
		loadedCacheFile = candidate.path;
		logger::debug("[Waterbody] [Flowmap] Loaded {} ({}x{} cells)",
			candidate.path.filename().string(), width, height);
		return true;
	}
	logger::debug("[Waterbody] [Flowmap] No valid flowmap cache found");
	return false;
}

bool Flowmap::GenerateFlowmap(bool useMips)
{
	const auto t0 = std::chrono::steady_clock::now();

	const auto tamriel = RE::TESForm::LookupByEditorID<RE::TESWorldSpace>("Tamriel");
	if (!tamriel) {
		logger::error("[Waterbody] [Flowmap] Failed to load Tamriel WorldSpace");
		return false;
	}

	int32_t worldMinX, worldMinY, worldMaxX, worldMaxY;
	Util::WorldToCell(tamriel->minimumCoords, worldMinX, worldMinY);
	Util::WorldToCell(tamriel->maximumCoords, worldMaxX, worldMaxY);
	// TESWorldSpace maximum coordinates are exclusive; convert to inclusive cells.
	if (worldMaxX <= worldMinX || worldMaxY <= worldMinY) {
		logger::error("[Waterbody] [Flowmap] Invalid worldspace bounds");
		return false;
	}
	worldMaxX -= 1;
	worldMaxY -= 1;
	if (worldMinX > worldMaxX || worldMinY > worldMaxY ||
		static_cast<int64_t>(worldMaxX) - worldMinX > 1024 ||
		static_cast<int64_t>(worldMaxY) - worldMinY > 1024) {
		logger::error("[Waterbody] [Flowmap] Invalid worldspace extents");
		return false;
	}

	struct FlowCell
	{
		int32_t x;
		int32_t y;
		DirectX::ScratchImage image;
	};

	int32_t mapMinX = INT_MAX;
	int32_t mapMinY = INT_MAX;
	int32_t mapMaxX = INT_MIN;
	int32_t mapMaxY = INT_MIN;

	auto cells = std::vector<FlowCell>();
	cells.reserve(1024);

	{
		for (int64_t y = worldMinY; y <= worldMaxY; ++y) {
			for (int64_t x = worldMinX; x <= worldMaxX; ++x) {
				auto path = std::format(R"(Textures\Water\skyrim.esm\flow.{}.{}.dds)", x, y);
				auto stream = RE::BSResourceNiBinaryStream(path);

				if (!stream.good())
					continue;

				const auto size = stream.stream->totalSize;
				if (!size || size > kMaxSourceDDSBytes)
					continue;
				std::vector<uint8_t> buffer(static_cast<size_t>(size));
				stream.read(buffer.data(), size);
				if (!stream.good()) {
					logger::warn("[Waterbody] [Flowmap] Truncated DDS source at {},{}", x, y);
					continue;
				}

				DirectX::TexMetadata meta{};
				// Validate DDS dimensions before letting DirectXTex allocate its image
				// chain (a corrupt source could advertise an enormous decompression).
				auto hr = DirectX::GetMetadataFromDDSMemory(buffer.data(), size,
					DirectX::DDS_FLAGS_NONE, meta);
				if (FAILED(hr) || meta.width != FLOW_CELL_SIZE || meta.height != FLOW_CELL_SIZE ||
					meta.arraySize != 1 || meta.dimension != DirectX::TEX_DIMENSION_TEXTURE2D)
					continue;

				DirectX::ScratchImage src;
				hr = DirectX::LoadFromDDSMemory(buffer.data(), size, DirectX::DDS_FLAGS_NONE, &meta, src);
				if (FAILED(hr)) {
					logger::warn("[Waterbody] [Flowmap] Flow texture at {},{} failed to load", x, y);
					continue;
				}

				DirectX::ScratchImage conv;
				if (DirectX::IsCompressed(meta.format)) {
					hr = DirectX::Decompress(src.GetImages(), src.GetImageCount(), src.GetMetadata(), DXGI_FORMAT_B8G8R8A8_UNORM, conv);
					if (FAILED(hr)) {
						logger::warn("[Waterbody] [Flowmap] Flow texture at {},{} failed to decompress", x, y);
						continue;
					}
				} else if (meta.format != DXGI_FORMAT_B8G8R8A8_UNORM) {
					hr = DirectX::Convert(src.GetImages(), src.GetImageCount(), src.GetMetadata(), DXGI_FORMAT_B8G8R8A8_UNORM, DirectX::TEX_FILTER_DEFAULT, 0.0f, conv);
					if (FAILED(hr)) {
						logger::warn("[Waterbody] [Flowmap] Flow texture at {},{} failed to convert to the correct format", x, y);
						continue;
					}
				} else {
					conv = std::move(src);
				}

				const auto* baseImage = conv.GetImage(0, 0, 0);
				if (!baseImage || baseImage->width != FLOW_CELL_SIZE || baseImage->height != FLOW_CELL_SIZE || baseImage->format != DXGI_FORMAT_B8G8R8A8_UNORM) {
					logger::warn("[Waterbody] [Flowmap] Flow texture at {},{} is invalid", x, y);
					continue;
				}

				mapMinX = std::min(mapMinX, static_cast<int32_t>(x));
				mapMinY = std::min(mapMinY, static_cast<int32_t>(y));
				mapMaxX = std::max(mapMaxX, static_cast<int32_t>(x));
				mapMaxY = std::max(mapMaxY, static_cast<int32_t>(y));

				cells.emplace_back(FlowCell{ static_cast<int32_t>(x), static_cast<int32_t>(y), std::move(conv) });
			}
		}
	}

	if (cells.empty()) {
		logger::error("[Waterbody] [Flowmap] No source flow textures found");
		return false;
	}

	const int64_t width64 = static_cast<int64_t>(mapMaxX) - mapMinX + 1;
	const int64_t height64 = static_cast<int64_t>(mapMaxY) - mapMinY + 1;
	if (width64 <= 0 || height64 <= 0 ||
		width64 * FLOW_CELL_SIZE > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
		height64 * FLOW_CELL_SIZE > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
		static_cast<uint64_t>(width64) * height64 * FLOW_CELL_SIZE * FLOW_CELL_SIZE > kMaxAtlasPixels ||
		mapMinX == std::numeric_limits<int32_t>::min() || mapMinY == std::numeric_limits<int32_t>::min()) {
		logger::error("[Waterbody] [Flowmap] Atlas dimensions too large ({}x{} cells)", width64, height64);
		return false;
	}
	const int32_t atlasWidth = static_cast<int32_t>(width64);
	const int32_t atlasHeight = static_cast<int32_t>(height64);
	const int32_t atlasOffsetX = -mapMinX;
	const int32_t atlasOffsetY = -mapMinY;

	logger::debug("[Waterbody] [Flowmap] Loaded {} flow textures, creating a {}x{} flow map...", cells.size(), atlasWidth, atlasHeight);

	const size_t pixelWidth = static_cast<size_t>(atlasWidth) * FLOW_CELL_SIZE;
	const size_t pixelHeight = static_cast<size_t>(atlasHeight) * FLOW_CELL_SIZE;
	const size_t mipLevels = useMips ? FLOWMAP_MIP_LEVELS : 1;
	DirectX::ScratchImage flowmap;
	if (FAILED(flowmap.Initialize2D(DXGI_FORMAT_B8G8R8A8_UNORM, pixelWidth, pixelHeight, 1, mipLevels))) {
		logger::error("[Waterbody] [Flowmap] Failed to allocate CPU atlas");
		return false;
	}
	std::memset(flowmap.GetPixels(), 0, flowmap.GetPixelsSize());

	auto* destination = flowmap.GetImage(0, 0, 0);
	for (const auto& [x, y, sourceImage] : cells) {
		const auto* source = sourceImage.GetImage(0, 0, 0);
		const size_t cellX = static_cast<size_t>(x + atlasOffsetX);
		const size_t cellY = static_cast<size_t>(y + atlasOffsetY);
		const size_t destinationX = cellX * FLOW_CELL_SIZE;
		const size_t destinationY = pixelHeight - (cellY + 1) * FLOW_CELL_SIZE;

		for (size_t row = 0; row < FLOW_CELL_SIZE; ++row) {
			auto* destinationRow = destination->pixels + (destinationY + row) * destination->rowPitch + destinationX * sizeof(FlowPixel);
			const auto* sourceRow = source->pixels + row * source->rowPitch;
			std::memcpy(destinationRow, sourceRow, FLOW_CELL_SIZE * sizeof(FlowPixel));
		}
	}

	ReconcileFlowmapBoundaries(flowmap, static_cast<uint32_t>(atlasWidth), static_cast<uint32_t>(atlasHeight));
	if (useMips && !GenerateVectorAwareMips(flowmap)) {
		logger::error("[Waterbody] [Flowmap] Failed to generate vector-aware mip chain");
		return false;
	}

	const auto filename = std::format(L"{}.{}.{}.{}.{}.dds", FLOWMAP_CACHE_PREFIX, atlasWidth, atlasHeight, atlasOffsetX, atlasOffsetY);
	const auto path = Util::PathHelpers::GetDataPath() / "textures" / "water" / "flowmaps" / filename;
	auto temporaryPath = path;
	temporaryPath += L".tmp";
	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);
	if (ec) {
		logger::error("[Waterbody] [Flowmap] Could not create cache directory: {}", ec.message());
		return false;
	}
	std::filesystem::remove(temporaryPath, ec);
	const auto hr = DirectX::SaveToDDSFile(flowmap.GetImages(), flowmap.GetImageCount(), flowmap.GetMetadata(), DirectX::DDS_FLAGS_NONE, temporaryPath.c_str());
	if (FAILED(hr)) {
		logger::error("[Waterbody] [Flowmap] Failed to save flowmap to {}: hr={:08X}", temporaryPath.string(), static_cast<uint32_t>(hr));
		return false;
	}

	if (!::MoveFileExW(temporaryPath.c_str(), path.c_str(),
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
		logger::error("[Waterbody] [Flowmap] Failed to publish {} (Win32 {})", path.string(), ::GetLastError());
		std::filesystem::remove(temporaryPath, ec);
		return false;
	}

	preferredCacheFile = path;
	const auto t1 = std::chrono::steady_clock::now();
	const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
	logger::info("[Waterbody] [Flowmap] Generated vector-aware CPU atlas in {} ms", ms);

	return true;
}
