#include "Flowmap.h"

#include <DDSTextureLoader.h>
#include <DirectXTex.h>
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>

namespace
{
	constexpr uint32_t FLOW_CELL_SIZE = 64;
	constexpr size_t FLOWMAP_MIP_LEVELS = 6;
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

	bool IsPIXLFlowmapCache(const std::filesystem::path& path)
	{
		if (path.extension() != L".dds")
			return false;

		const auto name = path.filename().wstring();
		return name.starts_with(FLOWMAP_CACHE_PREFIX) || name.starts_with(LEGACY_FLOWMAP_CACHE_PREFIX);
	}

	FlowPixel BlendFlowSamples(std::span<const FlowPixel* const> samples, bool includeEmptyCoverage)
	{
		float directionX = 0.0f;
		float directionY = 0.0f;
		float decodedStrength = 0.0f;
		float alpha = 0.0f;
		uint32_t authoredCount = 0;
		const FlowPixel* strongest = nullptr;

		for (const auto* sample : samples) {
			alpha += sample->a;
			if (IsEmptyFlowSample(*sample))
				continue;

			const float x = static_cast<float>(sample->r) * (2.0f / 255.0f) - 1.0f;
			const float y = static_cast<float>(sample->g) * (2.0f / 255.0f) - 1.0f;
			const float strength = std::sqrt(std::max(1.01f - static_cast<float>(sample->b) / 255.0f, 0.0f));
			const float lengthSquared = x * x + y * y;
			if (lengthSquared > 1.0e-8f) {
				const float inverseLength = 1.0f / std::sqrt(lengthSquared);
				directionX += x * inverseLength * strength;
				directionY += y * inverseLength * strength;
			}

			decodedStrength += strength;
			++authoredCount;
			if (!strongest || sample->a > strongest->a)
				strongest = sample;
		}

		if (authoredCount == 0)
			return {};

		float directionLengthSquared = directionX * directionX + directionY * directionY;
		if (directionLengthSquared <= 1.0e-8f) {
			directionX = static_cast<float>(strongest->r) * (2.0f / 255.0f) - 1.0f;
			directionY = static_cast<float>(strongest->g) * (2.0f / 255.0f) - 1.0f;
			directionLengthSquared = directionX * directionX + directionY * directionY;
		}

		if (directionLengthSquared > 1.0e-8f) {
			const float inverseLength = 1.0f / std::sqrt(directionLengthSquared);
			directionX *= inverseLength;
			directionY *= inverseLength;
		}

		auto encodeDirection = [](float value) {
			return static_cast<uint8_t>(std::clamp(std::lround((value * 0.5f + 0.5f) * 255.0f), 0l, 255l));
		};

		FlowPixel result{};
		result.r = encodeDirection(directionX);
		result.g = encodeDirection(directionY);
		const auto coverageDivisor = includeEmptyCoverage ? samples.size() : authoredCount;
		const float averageStrength = decodedStrength / static_cast<float>(coverageDivisor);
		const float encodedInverseStrength = std::clamp(1.01f - averageStrength * averageStrength, 0.0f, 1.0f);
		result.b = static_cast<uint8_t>(std::clamp(std::lround(encodedInverseStrength * 255.0f), 0l, 255l));
		result.a = static_cast<uint8_t>(std::clamp(std::lround(alpha / static_cast<float>(coverageDivisor)), 0l, 255l));
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
	if (!flowmapTex || !flowmapTex->rendererTexture || !flowmapTex->rendererTexture->texture || !flowmapTex->rendererTexture->resourceView)
		return false;

	outFlowmapTex = this->flowmapTex;
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
}

bool Flowmap::LoadOrGenerateFlowmap(bool useMips)
{
	Reset();

	if (!LoadFlowmap()) {
		logger::info("[Waterbody] [Flowmap] Could not load flowmap - regenerating...");
		return RegenerateAndLoadFlowmap(useMips);
	}

	return true;
}

bool Flowmap::RegenerateAndLoadFlowmap(bool useMips)
{
	Reset();

	namespace fs = std::filesystem;
	const fs::path dir = Util::PathHelpers::GetDataPath() / "textures" / "water" / "flowmaps";

	std::error_code ec;
	fs::create_directories(dir, ec);

	if (!fs::exists(dir))
		return false;

	for (const auto& entry : fs::directory_iterator(dir, ec)) {
		if (ec)
			break;
		if (!entry.is_regular_file())
			continue;

		const auto& path = entry.path();
		if (!IsPIXLFlowmapCache(path))
			continue;

		std::error_code rec;
		fs::remove(path, rec);
		if (rec)
			logger::warn("[Waterbody] [Flowmap] Failed to remove '{}': {}", path.string(), rec.message());
	}

	if (!GenerateFlowmap(useMips)) {
		logger::error("[Waterbody] [Flowmap] Failed to generate flowmap");
		return false;
	}

	if (!LoadFlowmap()) {
		logger::error("[Waterbody] [Flowmap] Failed to load flowmap after generation");
		Reset();
		return false;
	}

	logger::debug("[Waterbody] [Flowmap] Flowmap regenerated and loaded");
	return true;
}

bool Flowmap::LoadFlowmap()
{
	namespace fs = std::filesystem;

	const fs::path dir = Util::PathHelpers::GetDataPath() / "textures" / "water" / "flowmaps";

	fs::directory_entry file;

	if (fs::exists(dir) && fs::is_directory(dir)) {
		for (const auto& entry : fs::directory_iterator(dir)) {
			if (!entry.is_regular_file()) {
				continue;
			}

			const std::wstring name = entry.path().filename().wstring();
			if (name.starts_with(FLOWMAP_CACHE_PREFIX)) {
				file = entry;
				break;
			}
		}
	}

	if (file.path().empty()) {
		logger::debug("[Waterbody] [Flowmap] No flowmap found");
		return false;
	}

	std::vector<std::string> tokens;
	std::istringstream iss(file.path().filename().stem().string());
	std::string token;

	while (std::getline(iss, token, '.')) {
		tokens.push_back(token);
	}

	if (tokens.size() != 5) {
		logger::error("[Waterbody] [Flowmap] Invalid file name");
		return false;
	}

	auto path = std::format(R"(textures\water\flowmaps\{})", file.path().filename().string().c_str());
	RE::NiPointer<RE::NiTexture> tex;
	RE::BSShaderManager::GetTexture(path.c_str(), true, tex, false);

	if (!tex || tex->GetRTTI() != globals::rtti::NiSourceTextureRTTI.get()) {
		logger::error("[Waterbody] [Flowmap] Failed to load flowmap from {}", path);
		return false;
	}

	const auto sourceTex = static_cast<RE::NiSourceTexture*>(tex.get());

	if (!sourceTex || !sourceTex->rendererTexture || !sourceTex->rendererTexture->texture) {
		logger::error("[Waterbody] [Flowmap] Flowmap invalid", path);
		return false;
	}

	flowmapTex = RE::NiPointer(sourceTex);

	auto parse_int = [&](const std::string& str, int32_t& out) -> bool {
		int temp;
		auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), temp);
		if (ec != std::errc{} || ptr != str.data() + str.size()) {
			logger::error("[Waterbody] [Flowmap] Failed to parse '{}' from filename", str);
			return false;
		}
		out = temp;
		return true;
	};

	if (!parse_int(tokens[1], width) || !parse_int(tokens[2], height) || !parse_int(tokens[3], offsetX) || !parse_int(tokens[4], offsetY)) {
		return false;
	}
	if (tokens[0] != "Tamriel-Flowmap-v2" || width <= 0 || height <= 0) {
		logger::error("[Waterbody] [Flowmap] Invalid cache identity or dimensions in {}", file.path().filename().string());
		Reset();
		return false;
	}

	invWidth = 1.0f / static_cast<float>(width);
	invHeight = 1.0f / static_cast<float>(height);

	logger::debug("[Waterbody] [Flowmap] Flowmap loaded");
	return true;
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
	worldMaxX -= 1;
	worldMaxY -= 1;

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
		for (auto y = worldMinY; y < worldMaxY; ++y) {
			for (auto x = worldMinX; x < worldMaxX; ++x) {
				auto path = std::format(R"(Textures\Water\skyrim.esm\flow.{}.{}.dds)", x, y);
				auto stream = RE::BSResourceNiBinaryStream(path);

				if (!stream.good())
					continue;

				const auto size = stream.stream->totalSize;
				std::vector<uint8_t> buffer(size);
				stream.read(buffer.data(), size);

				DirectX::TexMetadata meta{};
				DirectX::ScratchImage src;
				auto hr = DirectX::LoadFromDDSMemory(buffer.data(), size, DirectX::DDS_FLAGS_NONE, &meta, src);
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

				mapMinX = std::min(mapMinX, x);
				mapMinY = std::min(mapMinY, y);
				mapMaxX = std::max(mapMaxX, x);
				mapMaxY = std::max(mapMaxY, y);

				cells.emplace_back(FlowCell{ x, y, std::move(conv) });
			}
		}
	}

	if (cells.empty()) {
		logger::error("[Waterbody] [Flowmap] No source flow textures found");
		return false;
	}

	const auto width = mapMaxX - mapMinX + 1;
	const auto height = mapMaxY - mapMinY + 1;
	const auto offsetX = -mapMinX;
	const auto offsetY = -mapMinY;

	logger::debug("[Waterbody] [Flowmap] Loaded {} flow textures, creating a {}x{} flow map...", cells.size(), width, height);

	const size_t pixelWidth = static_cast<size_t>(width) * FLOW_CELL_SIZE;
	const size_t pixelHeight = static_cast<size_t>(height) * FLOW_CELL_SIZE;
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
		const size_t cellX = static_cast<size_t>(x + offsetX);
		const size_t cellY = static_cast<size_t>(y + offsetY);
		const size_t destinationX = cellX * FLOW_CELL_SIZE;
		const size_t destinationY = pixelHeight - (cellY + 1) * FLOW_CELL_SIZE;

		for (size_t row = 0; row < FLOW_CELL_SIZE; ++row) {
			auto* destinationRow = destination->pixels + (destinationY + row) * destination->rowPitch + destinationX * sizeof(FlowPixel);
			const auto* sourceRow = source->pixels + row * source->rowPitch;
			std::memcpy(destinationRow, sourceRow, FLOW_CELL_SIZE * sizeof(FlowPixel));
		}
	}

	ReconcileFlowmapBoundaries(flowmap, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
	if (useMips && !GenerateVectorAwareMips(flowmap)) {
		logger::error("[Waterbody] [Flowmap] Failed to generate vector-aware mip chain");
		return false;
	}

	const auto filename = std::format(L"{}.{}.{}.{}.{}.dds", FLOWMAP_CACHE_PREFIX, width, height, offsetX, offsetY);
	const auto path = Util::PathHelpers::GetDataPath() / "textures" / "water" / "flowmaps" / filename;
	auto temporaryPath = path;
	temporaryPath += L".tmp";
	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);
	std::filesystem::remove(temporaryPath, ec);
	const auto hr = DirectX::SaveToDDSFile(flowmap.GetImages(), flowmap.GetImageCount(), flowmap.GetMetadata(), DirectX::DDS_FLAGS_NONE, temporaryPath.c_str());
	if (FAILED(hr)) {
		logger::error("[Waterbody] [Flowmap] Failed to save flowmap to {}: hr={:08X}", temporaryPath.string(), static_cast<uint32_t>(hr));
		return false;
	}

	std::filesystem::remove(path, ec);
	ec.clear();
	std::filesystem::rename(temporaryPath, path, ec);
	if (ec) {
		logger::error("[Waterbody] [Flowmap] Failed to publish {}: {}", path.string(), ec.message());
		std::filesystem::remove(temporaryPath, ec);
		return false;
	}

	const auto t1 = std::chrono::steady_clock::now();
	const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
	logger::info("[Waterbody] [Flowmap] Generated vector-aware CPU atlas in {} ms", ms);

	return true;
}
