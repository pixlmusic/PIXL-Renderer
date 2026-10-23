#include "TerrainOcclusion.h"

#include <DirectXTex.h>
#include <pystring/pystring.h>
#include <limits>
#include <cmath>

#include "I18n/I18n.h"
#include "State.h"
#include "Util.h"

#define I18N_KEY_PREFIX "feature.terrain_occlusion."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	TerrainOcclusion::Settings,
	EnableTerrainShadow)

float TerrainOcclusion::OceanHeightfield::Sample(float worldX, float worldY) const noexcept
{
	if (!std::isfinite(worldX) || !std::isfinite(worldY))
		return std::numeric_limits<float>::quiet_NaN();
	if (!width || !height || encodedHeight.size() != static_cast<std::size_t>(width) * height)
		return std::numeric_limits<float>::quiet_NaN();
	const float spanX = metadata.pos1.x - metadata.pos0.x;
	const float spanY = metadata.pos1.y - metadata.pos0.y;
	if (!std::isfinite(spanX) || !std::isfinite(spanY) || std::abs(spanX) < 1.0f || std::abs(spanY) < 1.0f)
		return std::numeric_limits<float>::quiet_NaN();
	const float u = (worldX - metadata.pos0.x) / spanX;
	const float v = (worldY - metadata.pos0.y) / spanY;
	if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)
		return std::numeric_limits<float>::quiet_NaN();
	const float px = std::clamp(u * width - 0.5f, 0.0f, static_cast<float>(width - 1));
	const float py = std::clamp(v * height - 0.5f, 0.0f, static_cast<float>(height - 1));
	const auto x0 = static_cast<std::uint32_t>(px);
	const auto y0 = static_cast<std::uint32_t>(py);
	const auto x1 = std::min(x0 + 1, width - 1);
	const auto y1 = std::min(y0 + 1, height - 1);
	const float fx = px - x0;
	const float fy = py - y0;
	auto at = [&](std::uint32_t x, std::uint32_t y) { return encodedHeight[static_cast<std::size_t>(y) * width + x]; };
	const float encoded = std::lerp(std::lerp(at(x0, y0), at(x1, y0), fx),
		std::lerp(at(x0, y1), at(x1, y1), fx), fy);
	return std::clamp(std::lerp(metadata.pos0.z, metadata.pos1.z, encoded),
		metadata.zRange.x, metadata.zRange.y);
}

void TerrainOcclusion::LoadSettings(json& o_json)
{
	settings = o_json;
}

void TerrainOcclusion::SaveSettings(json& o_json)
{
	o_json = settings;
}

void TerrainOcclusion::DrawSettings()
{
	ImGui::Checkbox(T(TKEY("enable_terrain_shadow"), "Enable Terrain Shadow"), &settings.EnableTerrainShadow);

	if (globals::state->IsDeveloperMode() && ImGui::CollapsingHeader(T(TKEY("debug"), "Debug"))) {
		std::string curr_worldspace = "N/A";
		std::string curr_worldspace_name = "N/A";
		auto tes = RE::TES::GetSingleton();
		if (tes) {
			auto worldspace = tes->GetRuntimeData2().worldSpace;
			if (worldspace) {
				curr_worldspace = worldspace->GetFormEditorID();
				curr_worldspace_name = worldspace->GetName();
			}
		}
		ImGui::Text("Current worldspace: %s (%s)", curr_worldspace.c_str(), curr_worldspace_name.c_str());
		ImGui::Text("Has height map: %s", heightmaps.contains(curr_worldspace) ? "yes" : "no");

		ImGui::Separator();

		ImGui::BulletText("shadowUpdateCBData");
		ImGui::Indent();
		{
			ImGui::Text("LightPxDir: (%g, %g)", shadowUpdateCBData.LightPxDir.x, shadowUpdateCBData.LightPxDir.y);
			ImGui::Text("LightDeltaZ: (%g, %g)", shadowUpdateCBData.LightDeltaZ.x, shadowUpdateCBData.LightDeltaZ.y);
			ImGui::Text("StartPxCoord: %u", shadowUpdateCBData.StartPxCoord);
			ImGui::Text("PxSize: (%g, %g)", shadowUpdateCBData.PxSize.x, shadowUpdateCBData.PxSize.y);
		}
		ImGui::Unindent();

		if (ImGui::TreeNode(T(TKEY("buffer_viewer"), "Buffer Viewer"))) {
			static float debugRescale = .1f;
			ImGui::SliderFloat("View Resize", &debugRescale, 0.f, 1.f);

			if (texShadowHeight) {
				BUFFER_VIEWER_NODE_BULLET(texShadowHeight, debugRescale)
			}
			ImGui::TreePop();
		}
	}
}

void TerrainOcclusion::ClearShaderCache()
{
	shadowUpdateProgram = nullptr;

	CompileComputeShaders();
}

void TerrainOcclusion::ParseHeightmapPath(std::filesystem::path p, bool xlodgen_style)
{
	auto filename = p.filename();
	if (filename.extension() != ".dds")
		return;
	logger::debug("Found dds: {}", filename.string());

	auto splitstr = pystring::split(filename.stem().string(), ".");
	// Shipped xLODGen maps also live in textures/heightmaps. The filename,
	// not its installation directory, determines which encoding to parse.
	if (splitstr.size() >= 3 && splitstr[1] == "Terrain" && splitstr[2] == "HeightMap")
		xlodgen_style = true;
	else if (splitstr.size() >= 2 && splitstr[1] == "HeightMap")
		xlodgen_style = false;
	if (splitstr.size() != (xlodgen_style ? 9 : 10)) {
		logger::debug("{} has incorrect number ({}) of fields", filename.string(), splitstr.size());
		return;
	}

	bool middle_check = xlodgen_style ? ((splitstr[1] == "Terrain") && (splitstr[2] == "HeightMap")) : (splitstr[1] == "HeightMap");
	if (middle_check) {
		HeightMapMetadata metadata;
		try {
			if (xlodgen_style) {
				metadata.worldspace = splitstr[0];
				metadata.pos0.x = std::stoi(splitstr[3]) * 4096.f;
				metadata.pos1.y = std::stoi(splitstr[4]) * 4096.f;
				metadata.pos1.x = (std::stoi(splitstr[5]) + 1) * 4096.f;
				metadata.pos0.y = (std::stoi(splitstr[6]) + 1) * 4096.f;
				metadata.pos0.z = -32767 * 8.f;
				metadata.pos1.z = 32767 * 8.f;
				metadata.zRange.x = std::stoi(splitstr[7]) * 8.f;
				metadata.zRange.y = std::stoi(splitstr[8]) * 8.f;
			} else {
				metadata.worldspace = splitstr[0];
				metadata.pos0.x = std::stoi(splitstr[2]) * 4096.f;
				metadata.pos1.y = std::stoi(splitstr[3]) * 4096.f;
				metadata.pos1.x = (std::stoi(splitstr[4]) + 1) * 4096.f;
				metadata.pos0.y = (std::stoi(splitstr[5]) + 1) * 4096.f;
				metadata.pos0.z = std::stoi(splitstr[6]) * 8.f;
				metadata.pos1.z = std::stoi(splitstr[7]) * 8.f;
				metadata.zRange.x = std::stoi(splitstr[8]) * 8.f;
				metadata.zRange.y = std::stoi(splitstr[9]) * 8.f;
			}
		} catch (std::exception& e) {
			logger::debug("Failed to parse {}. Error: {}", filename.string(), e.what());
			return;
		}

		metadata.dir = p.parent_path().wstring();
		metadata.filename = filename.string();

		if (heightmaps.contains(metadata.worldspace))
			logger::warn("{} has more than one height maps!", metadata.worldspace);
		heightmaps[metadata.worldspace] = metadata;

		logger::info("{} loaded.", filename.string());
	} else
		logger::debug("{} has unknown type ({})", filename.string(), splitstr[1]);
}

void TerrainOcclusion::EnsureHeightmapCatalog()
{
	if (heightmapCatalogLoaded)
		return;
	heightmapCatalogLoaded = true;
	logger::debug("Listing xLODGen height maps...");
	{
		std::filesystem::path texture_dir{ L"Data\\textures\\Terrain\\" };
		std::error_code ec;
		for (auto const& dir_entry : std::filesystem::directory_iterator{ texture_dir, ec }) {
			auto dir_path = dir_entry.path();
			if (!std::filesystem::is_directory(dir_path))
				continue;

			for (auto const& sub_dir_entry : std::filesystem::directory_iterator{ dir_path })
				ParseHeightmapPath(sub_dir_entry.path(), true);
		}
	}

	logger::debug("Listing height maps...");
	{
		std::filesystem::path texture_dir{ L"Data\\textures\\heightmaps\\" };
		std::error_code ec;
		for (auto const& dir_entry : std::filesystem::directory_iterator{ texture_dir, ec })
			ParseHeightmapPath(dir_entry.path(), false);
	}

	}

void TerrainOcclusion::SetupResources()
{
	EnsureHeightmapCatalog();
	logger::debug("Creating constant buffers...");
	{
		shadowUpdateCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<ShadowUpdateCB>(), "TerrainOcclusion::UpdateCB");
	}

	CompileComputeShaders();
}

void TerrainOcclusion::CompileComputeShaders()
{
	logger::debug("Compiling shaders...");
	{
		auto program_ptr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\PIXL-Shaders\\TerrainOcclusion\\ShadowUpdate.cs.hlsl", {}, "cs_5_0"));
		if (program_ptr)
			shadowUpdateProgram.attach(program_ptr);
	}
}

bool TerrainOcclusion::IsHeightMapReady()
{
	if (auto tes = RE::TES::GetSingleton())
		if (auto worldspace = tes->GetRuntimeData2().worldSpace)
			return cachedHeightmap && cachedHeightmap->worldspace == worldspace->GetFormEditorID();
	return false;
}

TerrainOcclusion::PerFrame TerrainOcclusion::GetCommonBufferData()
{
	bool isHeightmapReady = IsHeightMapReady();

	PerFrame data = {
		.EnableTerrainShadow = settings.EnableTerrainShadow && isHeightmapReady,
	};

	if (isHeightmapReady) {
		auto invScale = cachedHeightmap->pos1 - cachedHeightmap->pos0;
		if (!std::isfinite(invScale.x) || !std::isfinite(invScale.y) || !std::isfinite(invScale.z) ||
			std::abs(invScale.x) < 1.0f || std::abs(invScale.y) < 1.0f || std::abs(invScale.z) < 1.0f) {
			data.EnableTerrainShadow = 0;
			return data;
		}
		data.Scale = float3(1.f, 1.f, 1.f) / invScale;
		data.Offset = -cachedHeightmap->pos0 * float2{ data.Scale.x, data.Scale.y };
		data.ZRange = cachedHeightmap->zRange;
	}

	return data;
}

void TerrainOcclusion::LoadHeightmap()
{
	auto tes = globals::game::tes;
	if (!tes)
		return;

	auto worldspace = tes->GetRuntimeData2().worldSpace;
	while (worldspace && worldspace->parentWorld && worldspace->parentUseFlags.any(RE::TESWorldSpace::ParentUseFlag::kUseLandData))
		worldspace = worldspace->parentWorld;

	if (!worldspace)
		return;

	std::string worldspace_name = worldspace->GetFormEditorID();
	if (!heightmaps.contains(worldspace_name))  // no height map for that, but we don't remove cache
		return;

	if (cachedHeightmap && cachedHeightmap->worldspace == worldspace_name)  // already cached
		return;

	auto device = globals::d3d::device;

	logger::debug("Loading height map...");
	{
		auto& target_heightmap = heightmaps[worldspace_name];

		DirectX::ScratchImage image;
		try {
			std::filesystem::path path{ target_heightmap.dir };
			path /= target_heightmap.filename;

			DX::ThrowIfFailed(LoadFromDDSFile(path.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image));
		} catch (const DX::com_exception& e) {
			logger::error("{}", e.what());
			return;
		}

		// The same authored heightmap used by the water domain shader is kept
		// as a compact CPU snapshot for batched, nonblocking gameplay queries.
		std::shared_ptr<OceanHeightfield> heightfield;
		if (const auto* source = image.GetImage(0, 0, 0)) {
			DirectX::ScratchImage converted;
			const DirectX::Image* level = nullptr;
			HRESULT cpuDecodeResult = DirectX::Convert(*source, DXGI_FORMAT_R32_FLOAT,
				DirectX::TEX_FILTER_DEFAULT, 0.0f, converted);
			if (SUCCEEDED(cpuDecodeResult))
				level = converted.GetImage(0, 0, 0);
			// BC4 terrain heightmaps are common in xLODGen output. DirectXTex's
			// generic Convert path does not reliably expand BC4 to scalar float;
			// decode the channel explicitly before publishing the CPU query field.
			DirectX::ScratchImage decompressed;
			DirectX::ScratchImage decodedFloat;
			if (!level && DirectX::IsCompressed(source->format)) {
				cpuDecodeResult = DirectX::Decompress(*source, DXGI_FORMAT_R8G8B8A8_UNORM,
					decompressed);
				if (SUCCEEDED(cpuDecodeResult)) {
					const auto* rgba = decompressed.GetImage(0, 0, 0);
					if (rgba && rgba->width && rgba->height) {
						if (SUCCEEDED(DirectX::Convert(*rgba, DXGI_FORMAT_R32_FLOAT,
							DirectX::TEX_FILTER_DEFAULT, 0.0f, decodedFloat)))
							level = decodedFloat.GetImage(0, 0, 0);
						else
							cpuDecodeResult = E_FAIL;
					}
				}
			}
			if (level && level->width && level->height) {
				heightfield = std::make_shared<OceanHeightfield>();
				heightfield->metadata = target_heightmap;
				heightfield->worldspaceFormID = worldspace->GetFormID();
				heightfield->width = static_cast<std::uint32_t>(std::min<std::size_t>(level->width, 2048));
				heightfield->height = static_cast<std::uint32_t>(std::min<std::size_t>(level->height, 2048));
				heightfield->encodedHeight.resize(static_cast<std::size_t>(heightfield->width) * heightfield->height);
				for (std::size_t y = 0; y < heightfield->height; ++y) {
					const std::size_t sourceY = std::min(level->height - 1,
						(y * level->height + heightfield->height / 2) / heightfield->height);
					const auto* sourceRow = reinterpret_cast<const float*>(level->pixels + sourceY * level->rowPitch);
					for (std::size_t x = 0; x < heightfield->width; ++x) {
						const std::size_t sourceX = std::min(level->width - 1,
							(x * level->width + heightfield->width / 2) / heightfield->width);
						const float encoded = sourceRow[sourceX];
						heightfield->encodedHeight[y * heightfield->width + x] =
							std::isfinite(encoded) ? std::clamp(encoded, 0.0f, 1.0f) : 0.0f;
					}
				}
			} else {
				logger::error("[PIXL Ocean Dynamics] CPU terrain heightmap decode failed for '{}' (format {}, HRESULT 0x{:08X}); GPU terrain shadows remain available, but shoreline gameplay queries are disabled",
				target_heightmap.filename, static_cast<unsigned>(source->format),
				static_cast<std::uint32_t>(cpuDecodeResult));
			}
		}

		ID3D11Resource* pResource = nullptr;
		try {
			DX::ThrowIfFailed(CreateTexture(device,
				image.GetImages(), image.GetImageCount(),
				image.GetMetadata(), &pResource));
		} catch (const DX::com_exception& e) {
			logger::error("{}", e.what());
			return;
		}

		texHeightMap = std::make_unique<Texture2D>(reinterpret_cast<ID3D11Texture2D*>(pResource), "TerrainOcclusion::HeightMap");

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texHeightMap->desc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = 1 }
		};
		texHeightMap->CreateSRV(srvDesc);

		cachedHeightmap = &heightmaps[worldspace_name];
		if (!heightfield) {
			// Do not leave an older world's CPU map active when a new GPU map was
			// loaded successfully but its CPU representation could not be decoded.
			oceanHeightfield.store({}, std::memory_order_release);
		} else {
			logger::info("[PIXL Ocean Dynamics] CPU terrain query heightfield ready for '{}' ({}x{}, world {:08X})",
				target_heightmap.filename, heightfield->width, heightfield->height,
				heightfield->worldspaceFormID);
			oceanHeightfield.store(std::move(heightfield), std::memory_order_release);
		}
	}

	shadowUpdateIdx = 0;
	needPrecompute = true;
}

void TerrainOcclusion::Precompute()
{
	if (!cachedHeightmap)
		return;

	logger::info("Creating shadow texture...");
	{
		if (texShadowHeight) {
			auto context = globals::d3d::context;

			std::array<ID3D11ShaderResourceView*, 1> srvs = { nullptr };
			context->PSSetShaderResources(60, (uint)srvs.size(), srvs.data());
			context->CSSetShaderResources(60, (uint)srvs.size(), srvs.data());
		}

		D3D11_TEXTURE2D_DESC texDesc = {
			.Width = texHeightMap->desc.Width,
			.Height = texHeightMap->desc.Height,
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = DXGI_FORMAT_R16G16_UNORM,
			.SampleDesc = { .Count = 1 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS
		};
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = 1 }
		};
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		texShadowHeight = std::make_unique<Texture2D>(texDesc, "TerrainOcclusion::ShadowHeight");
		texShadowHeight->CreateSRV(srvDesc);
		texShadowHeight->CreateUAV(uavDesc);
	}
#undef I18N_KEY_PREFIX

	needPrecompute = false;
}

void TerrainOcclusion::UpdateShadow()
{
	ZoneScoped;

	if (!IsHeightMapReady())
		return;

	// don't forget to change NTHREADS in shader!
	constexpr uint updateLength = 128u;
	constexpr uint logUpdateLength = std::bit_width(128u) - 1;  // integer log2, https://stackoverflow.com/questions/994593/how-to-do-an-integer-log2-in-c

	auto context = globals::d3d::context;
	if (!context || !texHeightMap || !texShadowHeight || !texShadowHeight->uav ||
		!shadowUpdateCB || !shadowUpdateProgram || !cachedHeightmap)
		return;
	const float3 mapSpan = cachedHeightmap->pos1 - cachedHeightmap->pos0;
	const float zSpan = cachedHeightmap->zRange.y - cachedHeightmap->zRange.x;
	if (!std::isfinite(mapSpan.x) || !std::isfinite(mapSpan.y) || !std::isfinite(zSpan) ||
		std::abs(mapSpan.x) < 1.0f || std::abs(mapSpan.y) < 1.0f || std::abs(zSpan) < 1.0f)
		return;

	if (texShadowHeight) {
		std::array<ID3D11ShaderResourceView*, 1> srvs = { nullptr };
		context->PSSetShaderResources(60, (uint)srvs.size(), srvs.data());
		context->CSSetShaderResources(60, (uint)srvs.size(), srvs.data());
	}

	const auto accumulatorAddress = globals::game::currentAccumulator.get();
	if (!accumulatorAddress || !*accumulatorAddress)
		return;
	auto accumulator = *accumulatorAddress;
	auto shadowSceneNode = accumulator->GetRuntimeData().activeShadowSceneNode;
	if (!shadowSceneNode || !shadowSceneNode->GetRuntimeData().sunLight)
		return;
	auto sunLight = skyrim_cast<RE::NiDirectionalLight*>(shadowSceneNode->GetRuntimeData().sunLight->light.get());
	if (!sunLight)
		return;
	TracyD3D11Zone(globals::state->tracyCtx, "Terrain Occlusion - Update Shadows");

	/* ---- UPDATE CB ---- */
	uint width = texHeightMap->desc.Width;
	uint height = texHeightMap->desc.Height;

	// only update direction at the start of each cycle
	static uint edgePxCoord;
	static int signDir;
	static uint maxUpdates;
	if (shadowUpdateIdx == 0) {
		auto direction = sunLight->GetWorldDirection();
		float3 dirLightDir = { direction.x, direction.y, direction.z };
		if (dirLightDir.z > 0)
			dirLightDir = -dirLightDir;

		// in UV
		float3 invScale = cachedHeightmap->pos1 - cachedHeightmap->pos0;
		invScale.z = cachedHeightmap->zRange.y - cachedHeightmap->zRange.x;
		float3 dirLightPxDir = dirLightDir / invScale;
		dirLightPxDir.x *= width;
		dirLightPxDir.y *= height;
		const float planarDirection = std::max(std::abs(dirLightPxDir.x), std::abs(dirLightPxDir.y));
		if (!std::isfinite(planarDirection) || !std::isfinite(dirLightDir.z) || planarDirection < 1.0e-6f) {
			// At the zenith there is no terrain horizon to cast a long shadow.
			const float clearHeight[4]{};
			context->ClearUnorderedAccessViewFloat(texShadowHeight->uav.get(), clearHeight);
			shadowUpdateIdx = 0;
			return;
		}

		float stepMult;
		if (abs(dirLightPxDir.x) >= abs(dirLightPxDir.y)) {
			stepMult = 1.f / abs(dirLightPxDir.x);
			edgePxCoord = dirLightPxDir.x > 0 ? 0 : (width - 1);
			signDir = dirLightPxDir.x > 0 ? 1 : -1;
			maxUpdates = (width + updateLength - 1) >> logUpdateLength;
		} else {
			stepMult = 1.f / abs(dirLightPxDir.y);
			edgePxCoord = dirLightPxDir.y > 0 ? 0 : height - 1;
			signDir = dirLightPxDir.y > 0 ? 1 : -1;
			maxUpdates = (height + updateLength - 1) >> logUpdateLength;
		}
		dirLightPxDir *= stepMult;

		shadowUpdateCBData.LightPxDir = { dirLightPxDir.x, dirLightPxDir.y };

		// soft shadow angles
		float lenUV = float2{ dirLightDir.x, dirLightDir.y }.Length();
		float dirLightAngle = atan2(-dirLightDir.z, lenUV);
		float shadowSofteningRadiusAngle = RE::NI_PI / 180.f;
		float upperAngle = std::max(0.f, dirLightAngle - shadowSofteningRadiusAngle);
		float lowerAngle = std::min(RE::NI_HALF_PI - 1e-2f, dirLightAngle + shadowSofteningRadiusAngle);

		shadowUpdateCBData.LightDeltaZ = -(lenUV / invScale.z * stepMult) * float2{ std::tan(upperAngle), std::tan(lowerAngle) };
	}

	shadowUpdateCBData.StartPxCoord = edgePxCoord + signDir * shadowUpdateIdx * updateLength;
	shadowUpdateCBData.PxSize = { 1.f / texHeightMap->desc.Width, 1.f / texHeightMap->desc.Height };

	shadowUpdateCBData.PosRange = { cachedHeightmap->pos0.z, cachedHeightmap->pos1.z };
	shadowUpdateCBData.ZRange = cachedHeightmap->zRange;

	shadowUpdateCB->Update(shadowUpdateCBData);

	shadowUpdateIdx = (shadowUpdateIdx + 1) % maxUpdates;

	/* ---- BACKUP ---- */
	struct ShaderState
	{
		ID3D11ShaderResourceView* srvs[1] = { nullptr };
		ID3D11ComputeShader* shader = nullptr;
		ID3D11UnorderedAccessView* uavs[1] = { nullptr };
		ID3D11Buffer* buffer = nullptr;
	} old, newer;

	/* ---- DISPATCH ---- */

	newer.srvs[0] = texHeightMap->srv.get();
	newer.uavs[0] = texShadowHeight->uav.get();
	newer.buffer = shadowUpdateCB->CB();

	context->CSSetShaderResources(0, ARRAYSIZE(newer.srvs), newer.srvs);
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(newer.uavs), newer.uavs, nullptr);
	context->CSSetConstantBuffers(0, 1, &newer.buffer);
	context->CSSetShader(shadowUpdateProgram.get(), nullptr, 0);
	globals::profiler->BeginPass("TerrainOcclusion::ShadowUpdate");
	context->Dispatch(abs(shadowUpdateCBData.LightPxDir.x) >= abs(shadowUpdateCBData.LightPxDir.y) ? height : width, 1, 1);
	globals::profiler->EndPass();

	/* ---- RESTORE ---- */
	context->CSSetShaderResources(0, ARRAYSIZE(old.srvs), old.srvs);
	context->CSSetShader(old.shader, nullptr, 0);
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(old.uavs), old.uavs, nullptr);
	context->CSSetConstantBuffers(0, 1, &old.buffer);
}

void TerrainOcclusion::ReflectionsPrepass()
{
	if (texShadowHeight) {
		auto context = globals::d3d::context;

		std::array<ID3D11ShaderResourceView*, 1> srvs = { texShadowHeight->srv.get() };
		context->PSSetShaderResources(60, (uint)srvs.size(), srvs.data());
		context->CSSetShaderResources(60, (uint)srvs.size(), srvs.data());
	}
}

void TerrainOcclusion::EarlyPrepass()
{
	LoadHeightmap();

	if (!settings.EnableTerrainShadow)
		return;

	if (needPrecompute)
		Precompute();

	UpdateShadow();

	if (texShadowHeight) {
		auto context = globals::d3d::context;

		std::array<ID3D11ShaderResourceView*, 1> srvs = { texShadowHeight->srv.get() };
		context->PSSetShaderResources(60, (uint)srvs.size(), srvs.data());
		context->CSSetShaderResources(60, (uint)srvs.size(), srvs.data());
	}
}
