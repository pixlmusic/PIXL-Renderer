// PIXL Renderer - weather-driven spectral ocean simulation on Skyrim water geometry.
// Native DX11 implementation using published spectral-ocean methods. The
// existing Skyrim water meshes remain the coverage and coastline authority.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "OceanDynamics.h"
#include "OceanDynamicsShore.h"
#include "PIXL/SurfingAPI.h"

#include "Buffer.h"
#include "Globals.h"
#include "State.h"
#include "Modules/TerrainOcclusion.h"
#include "Modules/Waterbody.h"
#include "Utils/D3D.h"
#include "WeatherManager.h"
#include "Util.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <cmath>
#include <numeric>
#include <imgui_stdlib.h>
#include <Windows.h>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(OceanDynamics::Settings,
	EnableSimulation, EnableGeometryDisplacement,
	GeometryQuality,
	WindSpeed, WindDirectionDegrees,
	SwellHeight, SwellPeriod, SwellDirectionDegrees, CrossSwellDirectionDegrees,
	CrossSwellStrength, DeepRogueWaveStrength, Choppiness, ShoalingStrength, BreakerStrength,
	FoamStrength, FoamLifetime, CrestTranslucency,
	EnableSpray, EnableBreakingWaves, SprayDensity)

namespace
{
	constexpr float kPi = 3.14159265358979323846f;
	constexpr std::array<float, OceanDynamics::CascadeCount> kCascadeExtent{ 256.0f, 1024.0f, 4096.0f, 32768.0f };

	std::unique_ptr<Texture2D> CreateArrayTexture(std::uint32_t size, const char* name)
	{
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = size;
		desc.Height = size;
		desc.MipLevels = 1;
		desc.ArraySize = OceanDynamics::CascadeCount;
		desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		auto texture = std::make_unique<Texture2D>(desc, name);

		D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
		srv.Format = desc.Format;
		srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
		srv.Texture2DArray.MipLevels = 1;
		srv.Texture2DArray.ArraySize = OceanDynamics::CascadeCount;
		texture->CreateSRV(srv);

		D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
		uav.Format = desc.Format;
		uav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
		uav.Texture2DArray.ArraySize = OceanDynamics::CascadeCount;
		texture->CreateUAV(uav);
		return texture;
	}

	float FiniteClamp(float value, float fallback, float low, float high)
	{
		return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
	}

	struct CageFitNode
	{
		OceanShore::TerrainFrame frame{};
		std::array<float, 4> radii{};
		bool valid{};
	};

	struct SymmetricMatrix3
	{
		float xx{}, xy{}, xz{}, yy{}, yz{}, zz{};
	};

	void AddOuterProduct(SymmetricMatrix3& matrix, OceanShore::Direction3 axis, float weight)
	{
		matrix.xx += weight * axis.x * axis.x;
		matrix.xy += weight * axis.x * axis.y;
		matrix.xz += weight * axis.x * axis.z;
		matrix.yy += weight * axis.y * axis.y;
		matrix.yz += weight * axis.y * axis.z;
		matrix.zz += weight * axis.z * axis.z;
	}

	OceanShore::Direction3 ApplyMatrix(const SymmetricMatrix3& matrix,
		OceanShore::Direction3 value)
	{
		return {
			matrix.xx * value.x + matrix.xy * value.y + matrix.xz * value.z,
			matrix.xy * value.x + matrix.yy * value.y + matrix.yz * value.z,
			matrix.xz * value.x + matrix.yz * value.y + matrix.zz * value.z
		};
	}

	bool GetTerrainFrame(const TerrainOcclusion::OceanHeightfield& terrain,
		float x, float y, float seaLevel, float frameStep,
		OceanShore::TerrainFrame& frame, float& floor, float& slopeX, float& slopeY)
	{
		floor = terrain.Sample(x, y);
		if (!std::isfinite(floor))
			return false;
		const float left = terrain.Sample(x - frameStep, y);
		const float right = terrain.Sample(x + frameStep, y);
		const float down = terrain.Sample(x, y - frameStep);
		const float up = terrain.Sample(x, y + frameStep);
		if (std::isfinite(left) && std::isfinite(right) &&
			std::isfinite(down) && std::isfinite(up)) {
			slopeX = (right - left) / (2.0f * frameStep);
			slopeY = (up - down) / (2.0f * frameStep);
		} else {
			slopeX = slopeY = 0.0f;
		}
		frame = OceanShore::BuildTerrainFrame(slopeX, slopeY, seaLevel - floor);
		return true;
	}

	bool CageFits(const TerrainOcclusion::OceanHeightfield& terrain,
		float centerX, float centerY, float seaLevel,
		const OceanShore::TerrainFrame& frame, const std::array<float, 3>& radii,
		float allowedPenetration)
	{
		constexpr float goldenAngle = 2.39996322972865332f;
		constexpr std::uint32_t directionCount = 10;
		for (std::uint32_t index = 0; index < directionCount; ++index) {
			const float z = 1.0f - 2.0f * (static_cast<float>(index) + 0.5f) / directionCount;
			const float radial = std::sqrt(std::max(1.0f - z * z, 0.0f));
			const float angle = goldenAngle * static_cast<float>(index);
			const float u = radial * std::cos(angle);
			const float v = radial * std::sin(angle);
			const auto offset = OceanShore::Add(
				OceanShore::Add(OceanShore::Scale(frame.shore, radii[0] * u),
					OceanShore::Scale(frame.alongShore, radii[1] * v)),
				OceanShore::Scale(frame.normal, radii[2] * z));
			const float floor = terrain.Sample(centerX + offset.x, centerY + offset.y);
			if (std::isfinite(floor) && seaLevel + offset.z < floor - allowedPenetration)
				return false;
		}
		return true;
	}

	std::shared_ptr<const OceanDynamics::WaveCageField> BuildWaveCageField(
		std::shared_ptr<const TerrainOcclusion::OceanHeightfield> terrain, float seaLevel)
	{
		if (!terrain || !terrain->width || !terrain->height || !std::isfinite(seaLevel))
			return {};
		constexpr float maximumRadius = 16.0f * 70.0f;
		constexpr float penetrationAllowance = 0.1f * 70.0f;
		constexpr float maximumFitDepth = 8000.0f;
		constexpr std::uint32_t resolution = OceanDynamics::WaveCageField::Resolution;
		const float spanX = terrain->metadata.pos1.x - terrain->metadata.pos0.x;
		const float spanY = terrain->metadata.pos1.y - terrain->metadata.pos0.y;
		if (!std::isfinite(spanX) || !std::isfinite(spanY) ||
			std::abs(spanX) < 1.0f || std::abs(spanY) < 1.0f)
			return {};

		auto field = std::make_shared<OceanDynamics::WaveCageField>();
		field->worldspace = terrain->metadata.worldspace;
		field->worldspaceFormID = terrain->worldspaceFormID;
		field->seaLevel = seaLevel;
		field->bounds[0] = terrain->metadata.pos0.x;
		field->bounds[1] = terrain->metadata.pos0.y;
		field->bounds[2] = terrain->metadata.pos1.x;
		field->bounds[3] = terrain->metadata.pos1.y;
		field->radii.resize(static_cast<std::size_t>(resolution) * resolution);
		std::vector<CageFitNode> nodes(field->radii.size());
		std::vector<std::array<float, 4>> next(field->radii.size());
		const float frameStep = std::max({ 64.0f,
			std::abs(spanX) / static_cast<float>(terrain->width) * 2.0f,
			std::abs(spanY) / static_cast<float>(terrain->height) * 2.0f });
		const float cellX = spanX / resolution;
		const float cellY = spanY / resolution;

		for (std::uint32_t y = 0; y < resolution; ++y) {
			for (std::uint32_t x = 0; x < resolution; ++x) {
				const auto index = static_cast<std::size_t>(y) * resolution + x;
				const float worldX = terrain->metadata.pos0.x + (static_cast<float>(x) + 0.5f) * cellX;
				const float worldY = terrain->metadata.pos0.y + (static_cast<float>(y) + 0.5f) * cellY;
				float floor{}, slopeX{}, slopeY{};
				auto& node = nodes[index];
				if (!GetTerrainFrame(*terrain, worldX, worldY, seaLevel, frameStep,
					node.frame, floor, slopeX, slopeY))
					continue;
				node.valid = true;

				const float depth = seaLevel - floor;
				auto& cage = node.radii;
				if (depth <= 0.0f) {
					// The paper also constructs cages on the dry side with the roles
					// of air and terrain reversed. Tangential support keeps the field
					// smooth across the waterline; the geometry clip removes dry faces.
					cage = { maximumRadius, maximumRadius, 0.0f, 0.0f };
					continue;
				}
				if (depth > maximumFitDepth) {
					cage = { maximumRadius, maximumRadius, maximumRadius, 1.0f };
					continue;
				}

				std::array<float, 3> fitted{};
				for (std::uint32_t axis = 0; axis < 3; ++axis) {
					float low = 0.0f;
					float high = maximumRadius;
					const float allowed = axis < 2 ? penetrationAllowance : 0.0f;
					// Eight bisections resolve the fitted radius to about 4.4 game units
					// over the 16 m cage range, keeping the zero-clearance shoreline band
					// smooth at the paper's 10 cm tangential penetration tolerance.
					for (std::uint32_t iteration = 0; iteration < 8; ++iteration) {
						const float candidate = 0.5f * (low + high);
						auto testRadii = fitted;
						testRadii[axis] = candidate;
						if (CageFits(*terrain, worldX, worldY, seaLevel,
							node.frame, testRadii, allowed))
							low = candidate;
						else
							high = candidate;
					}
					fitted[axis] = low;
				}
				cage[0] = fitted[0];
				cage[1] = fitted[1];
				cage[2] = depth <= 8.0f ? 0.0f : fitted[2];
				const float curvature = std::hypot(
					(terrain->Sample(worldX + frameStep, worldY) - 2.0f * floor + terrain->Sample(worldX - frameStep, worldY)) /
						(frameStep * frameStep),
					(terrain->Sample(worldX, worldY + frameStep) - 2.0f * floor + terrain->Sample(worldX, worldY - frameStep)) /
						(frameStep * frameStep));
				cage[3] = std::isfinite(curvature)
					? std::clamp(std::exp(-curvature * maximumRadius * 0.25f), 0.0f, 1.0f)
					: 0.0f;
			}
		}
		for (std::size_t index = 0; index < nodes.size(); ++index)
			field->radii[index] = { nodes[index].radii[0], nodes[index].radii[1],
				nodes[index].radii[2], nodes[index].valid ? 1.0f : 0.0f };

		// This matrix-space Jacobi update is the Appendix B least-squares solve
		// in closed form: averaging M=A diag(r) A^T over a uniform sphere and
		// projecting it back onto the local axes gives the same 3x3 minimizer.
		for (std::uint32_t iteration = 0; iteration < 100; ++iteration) {
			for (std::uint32_t y = 0; y < resolution; ++y) {
				for (std::uint32_t x = 0; x < resolution; ++x) {
					const auto index = static_cast<std::size_t>(y) * resolution + x;
					const auto& base = nodes[index].radii;
					if (!nodes[index].valid || base[3] > 0.999f) {
						next[index] = field->radii[index];
						continue;
					}

					SymmetricMatrix3 average{};
					float neighborCount = 0.0f;
					const std::array<std::pair<std::uint32_t, std::uint32_t>, 4> neighbors{{
						{ x > 0 ? x - 1 : x, y }, { x + 1 < resolution ? x + 1 : x, y },
						{ x, y > 0 ? y - 1 : y }, { x, y + 1 < resolution ? y + 1 : y }
					}};
					for (const auto [nx, ny] : neighbors) {
						const auto neighbor = static_cast<std::size_t>(ny) * resolution + nx;
						const auto& n = nodes[neighbor];
						const auto& r = field->radii[neighbor];
						if (!n.valid)
							continue;
						AddOuterProduct(average, n.frame.shore, r[0]);
						AddOuterProduct(average, n.frame.alongShore, r[1]);
						AddOuterProduct(average, n.frame.normal, r[2]);
						neighborCount += 1.0f;
					}
					if (neighborCount <= 0.0f) {
						next[index] = base;
						continue;
					}
					const float inverseNeighbors = 1.0f / neighborCount;
					average.xx *= inverseNeighbors; average.xy *= inverseNeighbors;
					average.xz *= inverseNeighbors; average.yy *= inverseNeighbors;
					average.yz *= inverseNeighbors; average.zz *= inverseNeighbors;
					const auto solveRadius = [&](OceanShore::Direction3 axis, float original) {
						const float target = std::max(OceanShore::Dot(axis, ApplyMatrix(average, axis)), 0.0f);
						return std::clamp((1.0f - base[3]) * target + base[3] * original,
							0.0f, original);
					};
						next[index] = {
						solveRadius(nodes[index].frame.shore, base[0]),
						solveRadius(nodes[index].frame.alongShore, base[1]),
						solveRadius(nodes[index].frame.normal, base[2]),
							1.0f
					};
				}
			}
			for (std::size_t index = 0; index < field->radii.size(); ++index)
				field->radii[index] = next[index];
		}
		return field;
	}

	struct OceanDrawState
	{
		D3D11_PRIMITIVE_TOPOLOGY topology{ D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED };
		ID3D11HullShader* hull{};
		ID3D11DomainShader* domain{};
		ID3D11GeometryShader* geometry{};
		ID3D11Buffer* vertexCageConstants{};
		ID3D11Buffer* domainCageConstants{};
		ID3D11Buffer* geometryBathymetry{};
		ID3D11Buffer* geometryPerFrame{};
		ID3D11Buffer* geometryCageConstants{};
		ID3D11Buffer* constants7{};
		ID3D11Buffer* constants12{};
		ID3D11ShaderResourceView* vertexCageTexture{};
		ID3D11ShaderResourceView* domainCageTexture{};
		ID3D11ShaderResourceView* geometryTextures[5]{};
		ID3D11ShaderResourceView* textures[3]{};
		ID3D11ShaderResourceView* localTerrainTexture{};
		ID3D11SamplerState* sampler{};
		ID3D11Buffer* hullConstants7{};
		ID3D11Buffer* hullConstants12{};
		ID3D11ShaderResourceView* hullTextures[3]{};
		ID3D11ShaderResourceView* hullLocalTerrainTexture{};
		ID3D11SamplerState* hullSampler{};
		ID3D11SamplerState* geometrySampler{};
		bool active{};
	};
	thread_local OceanDrawState oceanDrawState;

	void ReleaseOceanDrawState(OceanDrawState& state)
	{
		if (state.hull) state.hull->Release();
		if (state.domain) state.domain->Release();
		if (state.geometry) state.geometry->Release();
		if (state.vertexCageConstants) state.vertexCageConstants->Release();
		if (state.domainCageConstants) state.domainCageConstants->Release();
		if (state.geometryBathymetry) state.geometryBathymetry->Release();
		if (state.geometryPerFrame) state.geometryPerFrame->Release();
		if (state.geometryCageConstants) state.geometryCageConstants->Release();
		if (state.constants7) state.constants7->Release();
		if (state.constants12) state.constants12->Release();
		if (state.vertexCageTexture) state.vertexCageTexture->Release();
		if (state.domainCageTexture) state.domainCageTexture->Release();
		for (auto* texture : state.geometryTextures)
			if (texture) texture->Release();
		for (auto* texture : state.textures)
			if (texture) texture->Release();
		if (state.localTerrainTexture) state.localTerrainTexture->Release();
		if (state.sampler) state.sampler->Release();
		if (state.hullConstants7) state.hullConstants7->Release();
		if (state.hullConstants12) state.hullConstants12->Release();
		for (auto* texture : state.hullTextures)
			if (texture) texture->Release();
		if (state.hullLocalTerrainTexture) state.hullLocalTerrainTexture->Release();
		if (state.hullSampler) state.hullSampler->Release();
		if (state.geometrySampler) state.geometrySampler->Release();
		state = {};
	}

// CameraPosAdjust is the engine render origin, not the camera's absolute world
// position. Reconstruct from the cached inverse view, matching State's safe
// fallback without dereferencing PlayerCamera from the water-setup worker thread.
RE::NiPoint3 GetAbsoluteCameraPosition() noexcept
{
	const auto& origin = globals::game::frameBufferCached.GetCameraPosAdjust();
	RE::NiPoint3 position{ origin.x, origin.y, origin.z };
	const auto inverseView = globals::game::frameBufferCached.GetCameraViewInverse().Transpose();
	position.x += inverseView._41;
	position.y += inverseView._42;
	position.z += inverseView._43;
	return position;
}
}

std::pair<std::string, std::vector<std::string>> OceanDynamics::GetModuleSummary()
{
	return { "Weather-driven spectral waves for automatically classified open-ocean water.",
		{ "Waterbody identifies broad, same-level ocean regions from connected Skyrim water cells; no world bounds are entered by hand.",
			"The existing Skyrim water meshes keep ownership of surface coverage; adaptive tessellation adds wave detail without replacing coastline geometry.",
			"A single FFT field blends rolling shore response, crossed medium-depth swells and longer deep-water wave groups.",
			"Lakes and flowmapped rivers retain their existing water path." } };
}

bool OceanDynamics::IsActive() const noexcept
{
	return loaded && settings.EnableSimulation != 0 && resourcesReady && displacement && displacement->srv;
}

ID3D11ShaderResourceView* OceanDynamics::GetDisplacementSRV() const noexcept
{
	return IsActive() ? displacement->srv.get() : nullptr;
}

ID3D11ShaderResourceView* OceanDynamics::GetPreviousDisplacementSRV() const noexcept
{
	if (!IsActive())
		return nullptr;
	return previousDisplacementValid && previousRenderedDisplacement ?
		previousRenderedDisplacement->srv.get() : displacement->srv.get();
}

void OceanDynamics::RequestWaveCageField(float seaLevel) noexcept
{
	if (!std::isfinite(seaLevel))
		return;
	// Waterbody surfaces use a stable level per connected water component. The
	// small quantization avoids rebuilding the global field for float noise.
	requestedWaveCageSeaLevel.store(std::round(seaLevel / 8.0f) * 8.0f,
		std::memory_order_relaxed);
}

void OceanDynamics::UpdateWaveCageField() noexcept
{
	const float seaLevel = requestedWaveCageSeaLevel.load(std::memory_order_relaxed);
	if (!std::isfinite(seaLevel) || !globals::pipeline::terrainOcclusion.loaded)
		return;
	const auto terrain = globals::pipeline::terrainOcclusion.oceanHeightfield.load(std::memory_order_acquire);
	if (!terrain || terrain->worldspaceFormID == 0)
		return;

	auto active = waveCageField.load(std::memory_order_acquire);
	const auto matches = [seaLevel, &terrain](const std::shared_ptr<const WaveCageField>& field) {
		return field && field->worldspaceFormID == terrain->worldspaceFormID &&
			std::abs(field->seaLevel - seaLevel) <= 1.0f;
	};
	if (waveCageBuild.valid()) {
		if (waveCageBuild.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
			return;
		try {
			auto completed = waveCageBuild.get();
			if (matches(completed) && globals::d3d::device && globals::d3d::context) {
				if (!waveCageTexture || waveCageTexture->desc.Width != WaveCageField::Resolution ||
					waveCageTexture->desc.Height != WaveCageField::Resolution) {
					D3D11_TEXTURE2D_DESC desc{};
					desc.Width = WaveCageField::Resolution;
					desc.Height = WaveCageField::Resolution;
					desc.MipLevels = 1;
					desc.ArraySize = 1;
					desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
					desc.SampleDesc.Count = 1;
					desc.Usage = D3D11_USAGE_DEFAULT;
					desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
					waveCageTexture = std::make_unique<Texture2D>(desc, "OceanDynamics::WaveCage");
					D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
					srv.Format = desc.Format;
					srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
					srv.Texture2D.MipLevels = 1;
					waveCageTexture->CreateSRV(srv);
				}
				auto* context = globals::d3d::context;
				// t123 is shared by unrelated PIXL passes (for example WindowLife).
				// Temporarily unbind it to avoid a read/write hazard during upload, then
				// restore each stage's prior view instead of clobbering renderer state.
				winrt::com_ptr<ID3D11ShaderResourceView> previousVS, previousHS, previousDS,
					previousGS, previousPS, previousCS;
				context->VSGetShaderResources(123, 1, previousVS.put());
				context->HSGetShaderResources(123, 1, previousHS.put());
				context->DSGetShaderResources(123, 1, previousDS.put());
				context->GSGetShaderResources(123, 1, previousGS.put());
				context->PSGetShaderResources(123, 1, previousPS.put());
				context->CSGetShaderResources(123, 1, previousCS.put());
				ID3D11ShaderResourceView* empty = nullptr;
				context->VSSetShaderResources(123, 1, &empty);
				context->HSSetShaderResources(123, 1, &empty);
				context->DSSetShaderResources(123, 1, &empty);
				context->GSSetShaderResources(123, 1, &empty);
				context->PSSetShaderResources(123, 1, &empty);
				context->CSSetShaderResources(123, 1, &empty);
				context->UpdateSubresource(waveCageTexture->resource.get(), 0, nullptr,
					completed->radii.data(), WaveCageField::Resolution * sizeof(std::array<float, 4>), 0);
				ID3D11ShaderResourceView* restore = previousVS.get();
				context->VSSetShaderResources(123, 1, &restore);
				restore = previousHS.get();
				context->HSSetShaderResources(123, 1, &restore);
				restore = previousDS.get();
				context->DSSetShaderResources(123, 1, &restore);
				restore = previousGS.get();
				context->GSSetShaderResources(123, 1, &restore);
				restore = previousPS.get();
				context->PSSetShaderResources(123, 1, &restore);
				restore = previousCS.get();
				context->CSSetShaderResources(123, 1, &restore);
				waveCageField.store(completed, std::memory_order_release);
				waveCageBlend.store(0.0f, std::memory_order_relaxed);
				static std::atomic_bool cageBuildLogged = false;
				if (!cageBuildLogged.exchange(true, std::memory_order_relaxed))
					logger::info("[PIXL Ocean Dynamics] Built {}x{} terrain-fitted wave cage (8-step radius fit, 100 constrained smoothing iterations) for {}",
						WaveCageField::Resolution, WaveCageField::Resolution, completed->worldspace);
			}
		} catch (const std::exception& error) {
			logger::warn("[PIXL Ocean Dynamics] Terrain wave-cage build failed; using safe shore fallback: {}", error.what());
		} catch (...) {
			logger::warn("[PIXL Ocean Dynamics] Terrain wave-cage build failed; using safe shore fallback");
		}
	}

	active = waveCageField.load(std::memory_order_acquire);
	if (matches(active) && waveCageTexture && waveCageTexture->srv) {
		const float dt = globals::game::deltaTime
			? std::clamp(*globals::game::deltaTime, 0.0f, 0.1f) : 1.0f / 60.0f;
		const float blend = waveCageBlend.load(std::memory_order_relaxed);
		waveCageBlend.store(std::min(blend + dt * 1.5f, 1.0f), std::memory_order_relaxed);
		return;
	}

	if (waveCageBuild.valid())
		return;
	try {
		waveCageBuild = std::async(std::launch::async,
			[terrain, seaLevel] { return BuildWaveCageField(terrain, seaLevel); });
	} catch (const std::exception& error) {
		logger::warn("[PIXL Ocean Dynamics] Could not schedule terrain wave-cage build: {}", error.what());
	} catch (...) {
		logger::warn("[PIXL Ocean Dynamics] Could not schedule terrain wave-cage build");
	}
}

void OceanDynamics::BindWaterGeometryResources(const RE::BSGeometry* geometry, bool openOceanTile) noexcept
{
	if (!globals::d3d::context)
		return;
	if (geometry && openOceanTile)
		openOceanTileAvailable.store(true, std::memory_order_relaxed);
	// The original Skyrim mesh owns the visible surface. OceanDynamics supplies
	// its restrained broad swell and the matching FFT/bathymetry fields to the
	// existing Water shader and WaterOptics; lakes and rivers remain untouched.
	ID3D11ShaderResourceView* heightmapSRV = nullptr;
	std::string heightmapWorldspace;
	// Detailed terrain is retained in a world-cell atlas. Its outer confidence
	// field converges to the static world floor without a cell-shaped render mask.
	if (geometry && openOceanTile) {
		auto& terrain = globals::pipeline::terrainOcclusion;
		if (terrain.IsHeightMapReady() && terrain.cachedHeightmap && terrain.texHeightMap && terrain.texHeightMap->srv) {
			heightmapWorldspace = terrain.cachedHeightmap->worldspace;
			heightmapSRV = terrain.texHeightMap->srv.get();
		}
	}
	if (geometry && openOceanTile && heightmapSRV &&
		!geometryBindingLogged.exchange(true, std::memory_order_relaxed))
		logger::info("[PIXL Ocean Dynamics] Bound {} shoreline heightmap to classified ocean water", heightmapWorldspace);
	const bool oceanSurfaceBound = geometry && openOceanTile && IsActive() &&
		settings.EnableGeometryDisplacement && previousDisplacementValid;
	if (oceanSurfaceBound && globals::pipeline::waterbody.loaded && globals::state) {
		auto* mutableGeometry = const_cast<RE::BSGeometry*>(geometry);
		auto* rendererData = mutableGeometry->GetGeometryRuntimeData().rendererData;
		auto* vertexBuffer = rendererData
			? reinterpret_cast<ID3D11Buffer*>(rendererData->vertexBuffer) : nullptr;
		const auto* tes = RE::TES::GetSingleton();
		const auto* world = tes ? tes->GetRuntimeData2().worldSpace : nullptr;
		const auto& position = geometry->world.translate;
		float seaLevel{};
		if (vertexBuffer && world && std::isfinite(position.x) && std::isfinite(position.y) &&
			globals::pipeline::waterbody.GetOpenOceanHeight(world, position.x, position.y, seaLevel)) {
			RequestWaveCageField(seaLevel);
			winrt::com_ptr<ID3D11Buffer> boundVertexBuffer;
			UINT boundStride{}, boundOffset{};
			globals::d3d::context->IAGetVertexBuffers(0, 1, boundVertexBuffer.put(), &boundStride, &boundOffset);
			std::lock_guard lock(pendingOceanDrawMutex);
			const auto frame = static_cast<std::uint64_t>(globals::state->frameCount);
			std::erase_if(pendingOceanDraws, [frame](const PendingOceanDraw& draw) {
				// Waterbody queues immediately before its draw, but the engine can
				// advance the renderer frame counter between setup and submission.
				// Keep only that one-frame handoff window; older packets can otherwise
				// replace a later native water draw after the camera/cell has changed.
				return frame > draw.frame + 1;
			});
			const auto existing = std::find_if(pendingOceanDraws.begin(), pendingOceanDraws.end(),
				[vertexBuffer](const PendingOceanDraw& draw) {
					return draw.vertexBuffer == vertexBuffer || draw.rendererVertexBuffer == vertexBuffer;
				});
			const PendingOceanDraw packet{ geometry,
				boundVertexBuffer ? boundVertexBuffer.get() : vertexBuffer,
				vertexBuffer, boundStride, seaLevel, frame, GetCurrentThreadId() };
			if (existing != pendingOceanDraws.end())
				*existing = packet;
			else {
				if (pendingOceanDraws.size() >= 64)
					pendingOceanDraws.pop_front();
				pendingOceanDraws.push_back(packet);
			}
			static std::atomic_bool queuedPacketLogged = false;
			if (!queuedPacketLogged.exchange(true, std::memory_order_relaxed))
				logger::info("[PIXL Ocean Dynamics] Queued classified water geometry {}: renderer VB {}, IA VB {}, stride {}, frame {}, setup thread {}",
					static_cast<const void*>(geometry), static_cast<const void*>(vertexBuffer),
					static_cast<const void*>(boundVertexBuffer.get()), boundStride, frame, GetCurrentThreadId());
		} else if (oceanSurfaceBound) {
			static std::atomic_bool packetRejectedLogged = false;
			if (!packetRejectedLogged.exchange(true, std::memory_order_relaxed))
				logger::warn("[PIXL Ocean Dynamics] Classified ocean surface had no draw packet: geometry {} rendererData {} renderer VB {} world {} XY ({:.1f},{:.1f}) sea-level lookup {}",
					static_cast<const void*>(geometry),
					static_cast<const void*>(rendererData),
					static_cast<const void*>(vertexBuffer),
					static_cast<const void*>(world), position.x, position.y,
					world && vertexBuffer && std::isfinite(position.x) && std::isfinite(position.y)
						? globals::pipeline::waterbody.GetOpenOceanHeight(world, position.x, position.y, seaLevel)
						: false);
		}
	}
	static std::atomic_bool nativeVertexDisplacementLogged = false;
	if (oceanSurfaceBound && !nativeVertexDisplacementLogged.exchange(true, std::memory_order_relaxed))
		logger::info("[PIXL Ocean Dynamics] Native Skyrim water mesh bound to broad FFT swell and WaterOptics wave normals ({}x{} FFT)",
			Resolution, Resolution);
	ID3D11ShaderResourceView* surfaceSRV = oceanSurfaceBound ? displacement->srv.get() : nullptr;
	ID3D11ShaderResourceView* previousSurfaceSRV = oceanSurfaceBound ? GetPreviousDisplacementSRV() : nullptr;
	globals::d3d::context->PSSetShaderResources(120, 1, &surfaceSRV);
	globals::d3d::context->PSSetShaderResources(121, 1, &previousSurfaceSRV);
	globals::d3d::context->PSSetShaderResources(122, 1, &heightmapSRV);
	ID3D11ShaderResourceView* localTerrainSRV = oceanSurfaceBound && localTerrainAtlas && localAtlasBounds[3]>0.5f
		? localTerrainAtlas->srv.get() : nullptr;
	globals::d3d::context->PSSetShaderResources(124, 1, &localTerrainSRV);
	PIXL_SurfingTelemetryV2 surfTelemetry{};
	surfTelemetry.base.structSize = sizeof(surfTelemetry);
	using GetSurfTelemetry = std::uint32_t (__cdecl *)(PIXL_SurfingTelemetryV2*);
	const auto surfingModule = GetModuleHandleW(L"PIXLSurfing.dll");
	const auto getSurfTelemetry = surfingModule ? reinterpret_cast<GetSurfTelemetry>(
		GetProcAddress(surfingModule, "PIXL_Surfing_GetTelemetry_V2")) : nullptr;
	const bool hasSurfTelemetry = getSurfTelemetry && getSurfTelemetry(&surfTelemetry) != 0;
	const auto* surfTES = RE::TES::GetSingleton();
	const auto* surfWorld = surfTES ? surfTES->GetRuntimeData2().worldSpace : nullptr;
	const bool surfBoardActive = oceanSurfaceBound && surfWorld && hasSurfTelemetry &&
		surfTelemetry.base.worldspaceFormID == surfWorld->GetFormID() &&
		(surfTelemetry.base.flags & (1u << 3)) != 0 &&
		(surfTelemetry.base.state == 3 || surfTelemetry.base.state == 4 ||
			surfTelemetry.base.state == 5 || surfTelemetry.base.state == 6 ||
			surfTelemetry.base.state == 10);
	const float currentBathymetryTime = static_cast<float>(simulationTime);
	const bool timingChanged = !std::isfinite(bathymetrySimulationTime) ||
		std::abs(currentBathymetryTime - bathymetrySimulationTime) >= 0.02f;
	if (bathymetryCB && (bathymetryUploadPending || timingChanged ||
		heightmapWorldspace != bathymetryWorldspace || heightmapSRV != bathymetryMapSRV ||
		oceanSurfaceBound != oceanSurfaceWasBound)) {
		BathymetryConstants bathymetry{};
		if (heightmapSRV) {
			const auto& metadata = *globals::pipeline::terrainOcclusion.cachedHeightmap;
			bathymetry.worldBounds[0] = metadata.pos0.x;
			bathymetry.worldBounds[1] = metadata.pos0.y;
			bathymetry.worldBounds[2] = metadata.pos1.x;
			bathymetry.worldBounds[3] = metadata.pos1.y;
			bathymetry.heightEncoding[0] = metadata.pos0.z;
			bathymetry.heightEncoding[1] = metadata.pos1.z;
			bathymetry.heightEncoding[2] = metadata.zRange.x;
			bathymetry.heightEncoding[3] = metadata.zRange.y;
			bathymetry.shoreSettings[0] = 1.0f;
			bathymetry.shoreSettings[1] = 256.0f;
			bathymetry.shoreSettings[2] = settings.FoamStrength;
		}
		bathymetry.shoreSettings[3] = oceanSurfaceBound ? 1.0f : 0.0f;
		static constexpr float waveDetail[4]{ 0.42f, 0.60f, 0.82f, 1.0f };
		bathymetry.geometrySettings[0] = waveDetail[std::min(settings.GeometryQuality, 3u)];
		bathymetry.geometrySettings[1] = FiniteClamp(settings.CrestTranslucency, 1.0f, 0.0f, 1.0f);
		bathymetry.geometrySettings[2] = FiniteClamp(settings.SwellPeriod, 8.5f, 2.0f, 24.0f);
		// Pixel shoreline coverage must use the undisplaced water plane. Using
		// the displaced fragment Z makes clipping and foam pulse with every crest.
		float pixelSeaLevel = geometry ? geometry->world.translate.z : 0.0f;
		if (oceanSurfaceBound && surfWorld && geometry) {
			const auto& oceanPosition = geometry->world.translate;
			globals::pipeline::waterbody.GetOpenOceanHeight(surfWorld,
				oceanPosition.x, oceanPosition.y, pixelSeaLevel);
		}
		bathymetry.geometrySettings[3] = pixelSeaLevel;
		const float swellDirection = FiniteClamp(settings.SwellDirectionDegrees, 76.0f, 0.0f, 360.0f) * (kPi / 180.0f);
		bathymetry.phaseSettings[0] = std::cos(swellDirection);
		bathymetry.phaseSettings[1] = std::sin(swellDirection);
		bathymetry.phaseSettings[2] = FiniteClamp(settings.ShoalingStrength, 1.5f, 0.0f, 1.5f);
		bathymetry.phaseSettings[3] = FiniteClamp(settings.BreakerStrength, 2.0f, 0.0f, 2.0f);
		bathymetry.timingSettings[0] = currentBathymetryTime;
		bathymetry.timingSettings[1] = std::max(0.0f, currentBathymetryTime - 1.0f / 30.0f);
		if (localTerrainSRV)
			std::copy(localAtlasBounds.begin(),localAtlasBounds.end(),bathymetry.localTerrainPatch);
		if (surfBoardActive) {
			bathymetry.surfBoardPosition[0] = surfTelemetry.base.boardPosition[0];
			bathymetry.surfBoardPosition[1] = surfTelemetry.base.boardPosition[1];
			bathymetry.surfBoardPosition[2] = surfTelemetry.base.boardPosition[2];
			bathymetry.surfBoardPosition[3] = 1.0f;
			bathymetry.surfBoardVelocity[0] = surfTelemetry.base.boardVelocity[0];
			bathymetry.surfBoardVelocity[1] = surfTelemetry.base.boardVelocity[1];
			bathymetry.surfBoardVelocity[2] = std::hypot(surfTelemetry.base.boardVelocity[0],
				surfTelemetry.base.boardVelocity[1]);
		}
		bathymetryCB->Update(&bathymetry, sizeof(bathymetry));
		bathymetrySimulationTime = currentBathymetryTime;
		bathymetryWorldspace = std::move(heightmapWorldspace);
		bathymetryMapSRV = heightmapSRV;
		oceanSurfaceWasBound = oceanSurfaceBound;
		bathymetryUploadPending = false;
	}
	if (bathymetryCB) {
		ID3D11Buffer* cb = bathymetryCB->CB();
		globals::d3d::context->VSSetConstantBuffers(7, 1, &cb);
		globals::d3d::context->PSSetConstantBuffers(7, 1, &cb);
	}
	ID3D11ShaderResourceView* vertexSRVs[]{
		oceanSurfaceBound ? displacement->srv.get() : nullptr,
		oceanSurfaceBound ? GetPreviousDisplacementSRV() : nullptr,
		heightmapSRV
	};
	globals::d3d::context->VSSetShaderResources(120, 3, vertexSRVs);
	ID3D11ShaderResourceView* noWaveCage = nullptr;
	globals::d3d::context->VSSetShaderResources(123, 1, &noWaveCage);
	globals::d3d::context->VSSetShaderResources(124, 1, &localTerrainSRV);
	if (bathymetryCB) {
		ID3D11Buffer* cb = bathymetryCB->CB();
		globals::d3d::context->VSSetConstantBuffers(7, 1, &cb);
	}
	if (waveCageCB) {
		const WaveCageConstants disabledCage{ static_cast<float>(WaveCageField::Resolution), 0.0f, 0.0f, 16.0f * 70.0f };
		waveCageCB->Update(&disabledCage, sizeof(disabledCage));
		ID3D11Buffer* cageCB = waveCageCB->CB();
		globals::d3d::context->VSSetConstantBuffers(13, 1, &cageCB);
	}
	if (displacementSampler) {
		auto* sampler = displacementSampler.get();
		globals::d3d::context->VSSetSamplers(15, 1, &sampler);
		globals::d3d::context->PSSetSamplers(15, 1, &sampler);
	} else {
		ID3D11SamplerState* emptySampler = nullptr;
		globals::d3d::context->VSSetSamplers(15, 1, &emptySampler);
	}
}

OceanDynamics::WaterTessShaders* OceanDynamics::GetWaterTessShaders(
	std::uint32_t descriptor, bool counterClockwise)
{
	const std::uint64_t key = (static_cast<std::uint64_t>(descriptor & 0x7fffu) << 1u) |
		(counterClockwise ? 1u : 0u);
	if (auto found = waterTessShaders.find(key); found != waterTessShaders.end())
		return found->second.hull && found->second.domain && found->second.shoreline ? &found->second : nullptr;

	const auto technique = (descriptor >> 11u) & 0xfu;
	if (technique > 10u || technique == 9u)
		return nullptr;
	std::vector<std::pair<const char*, const char*>> defines{
		{ "OCEAN_TESSELLATION", "1" }, { "OCEAN_DYNAMICS", "1" },
		{ "FOG", "1" }
	};
	if (globals::pipeline::waterbody.loaded) defines.emplace_back("WATERBODY", "1");
	if (counterClockwise) defines.emplace_back("OCEAN_OUTPUT_CCW", "1");
	if (descriptor & (1u << 0u)) defines.emplace_back("VC", "1");
	if (descriptor & (1u << 1u)) defines.emplace_back("NORMAL_TEXCOORD", "1");
	if (descriptor & (1u << 3u)) defines.emplace_back("REFRACTIONS", "1");
	if (descriptor & (1u << 6u)) defines.emplace_back("WADING", "1");
	if (descriptor & (1u << 7u)) defines.emplace_back("VERTEX_ALPHA_DEPTH", "1");
	if (descriptor & (1u << 9u)) defines.emplace_back("FLOWMAP", "1");
	if (descriptor & (1u << 10u)) defines.emplace_back("BLEND_NORMALS", "1");
	static constexpr std::array<const char*, 8> lightCounts{ "0", "1", "2", "3", "4", "5", "6", "7" };
	if (technique == 8u)
		defines.emplace_back("UNDERWATER", "1");
	else if (technique == 10u)
		defines.emplace_back("STENCIL", "1");
	else if (technique < 8u) {
		defines.emplace_back("SPECULAR", "1");
		defines.emplace_back("NUM_SPECULAR_LIGHTS", lightCounts[technique]);
	}
	const wchar_t* source = L"Data\\PIXL-Shaders\\Water.hlsl";
	WaterTessShaders shaders;
	shaders.hull.attach(static_cast<ID3D11HullShader*>(
		Util::CompileShader(source, defines, "hs_5_0", "OceanHull")));
	shaders.domain.attach(static_cast<ID3D11DomainShader*>(
		Util::CompileShader(source, defines, "ds_5_0", "OceanDomain")));
	shaders.shoreline.attach(static_cast<ID3D11GeometryShader*>(
		Util::CompileShader(source, defines, "gs_5_0", "OceanShoreline")));
	if (!shaders.hull || !shaders.domain || !shaders.shoreline) {
		logger::error("[PIXL Ocean Dynamics] Water tessellation shader failed for descriptor 0x{:x}", descriptor);
		waterTessShaders.emplace(key, WaterTessShaders{});
		return nullptr;
	}
	return &waterTessShaders.emplace(key, std::move(shaders)).first->second;
}

bool OceanDynamics::BeginWaterDraw(ID3D11DeviceContext* context, bool indexedDraw) noexcept
{
	static std::atomic_bool drawBridgeLogged = false;
	if (context && !drawBridgeLogged.exchange(true, std::memory_order_relaxed))
		logger::info("[PIXL Ocean Dynamics] D3D water draw bridge active on thread {}", GetCurrentThreadId());
	if (!context || oceanDrawState.active || !IsActive() || settings.EnableGeometryDisplacement == 0 ||
		!previousDisplacementValid || !bathymetryCB || !waveCageCB || !globals::game::perFrame.get() ||
		!*globals::game::perFrame.get() || !displacement || !previousDisplacement || !globals::state ||
		globals::state->activeReflections || !indexedDraw)
		return false;

	// Water setup can enqueue several classified tiles before the immediate draw
	// hook. Match the exact IA buffer and consume one packet; stale packets expire
	// after the next frame so an old coastline tile cannot replace a later draw.
	winrt::com_ptr<ID3D11Buffer> candidateVertexBuffer;
	UINT candidateStride{}, candidateOffset{};
	context->IAGetVertexBuffers(0, 1, candidateVertexBuffer.put(), &candidateStride, &candidateOffset);
	if (!candidateVertexBuffer)
		return false;
	PendingOceanDraw packet{};
	{
		std::lock_guard lock(pendingOceanDrawMutex);
		const auto frame = static_cast<std::uint64_t>(globals::state->frameCount);
		std::erase_if(pendingOceanDraws, [frame](const PendingOceanDraw& draw) {
			return frame > draw.frame + 1;
		});
		const auto found = std::find_if(pendingOceanDraws.begin(), pendingOceanDraws.end(),
			[&candidateVertexBuffer](const PendingOceanDraw& draw) {
				return draw.vertexBuffer == candidateVertexBuffer.get();
			});
		if (found == pendingOceanDraws.end())
			return false;
		packet = *found;
		pendingOceanDraws.erase(found);
	}
	if (candidateStride != packet.vertexStride) {
		static std::atomic_bool strideMismatchLogged = false;
		if (!strideMismatchLogged.exchange(true, std::memory_order_relaxed))
			logger::warn("[PIXL Ocean Dynamics] Matched water buffer changed IA stride (queued {}, current {}); preserving native draw",
				packet.vertexStride, candidateStride);
		return false;
	}
	if (!packet.geometry)
		return false;

	D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
	context->IAGetPrimitiveTopology(&topology);
	if (topology != D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST) {
		static std::atomic_bool topologyMismatchLogged = false;
		if (!topologyMismatchLogged.exchange(true, std::memory_order_relaxed))
			logger::warn("[PIXL Ocean Dynamics] Classified ocean draw used topology {} instead of triangle-list; preserving native draw",
				static_cast<std::uint32_t>(topology));
		return false;
	}

	const auto drawTechnique = (globals::state->currentVertexDescriptor >> 11u) & 0xfu;
	if (drawTechnique < 8u) {
		sprayDrawEligibleFrame = static_cast<std::uint64_t>(globals::state->frameCount);
		// The camera may already be on dry land while the classified ocean draw is
		// visible. Use this tile's valid connected-ocean level for beach spray.
		UpdateSpray(context, packet.seaLevel);
	}

	auto& state = oceanDrawState;
	context->IAGetPrimitiveTopology(&state.topology);
	context->HSGetShader(&state.hull, nullptr, nullptr);
	context->DSGetShader(&state.domain, nullptr, nullptr);
	context->GSGetShader(&state.geometry, nullptr, nullptr);
	if (state.hull || state.domain || state.geometry) {
		static std::atomic_bool ownershipLogged = false;
		if (!ownershipLogged.exchange(true, std::memory_order_relaxed))
			logger::warn("[PIXL Ocean Dynamics] Existing tessellation stages own this water draw; preserving native water");
		ReleaseOceanDrawState(state);
		return false;
	}

	D3D11_RASTERIZER_DESC rasterizerDesc{};
	winrt::com_ptr<ID3D11RasterizerState> activeRasterizer;
	context->RSGetState(activeRasterizer.put());
	if (activeRasterizer)
		activeRasterizer->GetDesc(&rasterizerDesc);
	const auto descriptor = globals::state->currentVertexDescriptor;
	const auto waterTechnique = (descriptor >> 11u) & 0xfu;
	const auto* waterSystem = RE::TESWaterSystem::GetSingleton();
	const auto* playerCamera = RE::PlayerCamera::GetSingleton();
	const bool cameraSubmerged = waterSystem && waterSystem->playerUnderwater &&
		playerCamera && playerCamera->cameraRoot &&
		std::isfinite(waterSystem->underwaterHeight) &&
		playerCamera->cameraRoot->world.translate.z < waterSystem->underwaterHeight + 64.0f;
	if (waterTechnique == 8u || cameraSubmerged) {
		ReleaseOceanDrawState(state);
		return false;
	}

	WaterTessShaders* shaders = nullptr;
	try {
		shaders = GetWaterTessShaders(descriptor,
			rasterizerDesc.FrontCounterClockwise != FALSE);
	} catch (const std::exception& error) {
		logger::error("[PIXL Ocean Dynamics] Water tessellation unavailable: {}", error.what());
	}
	if (!shaders) {
		ReleaseOceanDrawState(state);
		return false;
	}

	context->DSGetConstantBuffers(7, 1, &state.constants7);
	context->DSGetConstantBuffers(12, 1, &state.constants12);
	context->DSGetConstantBuffers(13, 1, &state.domainCageConstants);
	context->DSGetShaderResources(120, 3, state.textures);
	context->DSGetShaderResources(123, 1, &state.domainCageTexture);
	context->DSGetShaderResources(124, 1, &state.localTerrainTexture);
	context->DSGetSamplers(15, 1, &state.sampler);
	context->VSGetConstantBuffers(13, 1, &state.vertexCageConstants);
	context->VSGetShaderResources(123, 1, &state.vertexCageTexture);
	context->GSGetConstantBuffers(7, 1, &state.geometryBathymetry);
	context->GSGetConstantBuffers(12, 1, &state.geometryPerFrame);
	context->GSGetConstantBuffers(13, 1, &state.geometryCageConstants);
	context->GSGetShaderResources(120, 5, state.geometryTextures);
	context->GSGetSamplers(15, 1, &state.geometrySampler);
	context->HSGetConstantBuffers(7, 1, &state.hullConstants7);
	context->HSGetConstantBuffers(12, 1, &state.hullConstants12);
	context->HSGetShaderResources(120, 3, state.hullTextures);
	context->HSGetShaderResources(124, 1, &state.hullLocalTerrainTexture);
	context->HSGetSamplers(15, 1, &state.hullSampler);
	ID3D11Buffer* bathymetry = bathymetryCB->CB();
	ID3D11Buffer* perFrameCB = *globals::game::perFrame;
	const auto cageField = waveCageField.load(std::memory_order_acquire);
	const auto* currentWorld = RE::TES::GetSingleton()
		? RE::TES::GetSingleton()->GetRuntimeData2().worldSpace : nullptr;
	const float quantizedSeaLevel = std::round(packet.seaLevel / 8.0f) * 8.0f;
	const bool cageValid = cageField && waveCageTexture && waveCageTexture->srv &&
		currentWorld && cageField->worldspaceFormID == currentWorld->GetFormID() &&
		std::abs(cageField->seaLevel - quantizedSeaLevel) <= 1.0f;
	WaveCageConstants cageConstants{ static_cast<float>(WaveCageField::Resolution),
		cageValid ? 1.0f : 0.0f, cageValid ? waveCageBlend.load(std::memory_order_relaxed) : 0.0f, 16.0f * 70.0f };
	waveCageCB->Update(&cageConstants, sizeof(cageConstants));
	ID3D11Buffer* cageCB = waveCageCB->CB();
	ID3D11ShaderResourceView* cageSRV = cageValid ? waveCageTexture->srv.get() : nullptr;
	ID3D11ShaderResourceView* localTerrain = localTerrainAtlas && localAtlasBounds[3]>0.5f
		? localTerrainAtlas->srv.get() : nullptr;
	ID3D11ShaderResourceView* textures[]{ displacement->srv.get(),
		previousDisplacement->srv.get(), bathymetryMapSRV, cageSRV, localTerrain };
	ID3D11SamplerState* sampler = displacementSampler.get();
	context->VSSetConstantBuffers(13, 1, &cageCB);
	context->VSSetShaderResources(123, 1, &cageSRV);
	context->DSSetConstantBuffers(7, 1, &bathymetry);
	context->DSSetConstantBuffers(12, 1, &perFrameCB);
	context->DSSetConstantBuffers(13, 1, &cageCB);
	context->DSSetShaderResources(120, 3, textures);
	context->DSSetShaderResources(123, 1, &cageSRV);
	context->DSSetShaderResources(124, 1, &localTerrain);
	context->DSSetSamplers(15, 1, &sampler);
	context->GSSetConstantBuffers(7, 1, &bathymetry);
	context->GSSetConstantBuffers(12, 1, &perFrameCB);
	context->GSSetConstantBuffers(13, 1, &cageCB);
	context->GSSetShaderResources(120, 5, textures);
	context->GSSetSamplers(15, 1, &sampler);
	context->HSSetConstantBuffers(7, 1, &bathymetry);
	context->HSSetConstantBuffers(12, 1, &perFrameCB);
	context->HSSetShaderResources(120, 3, textures);
	context->HSSetShaderResources(124, 1, &localTerrain);
	context->HSSetSamplers(15, 1, &sampler);
	context->HSSetShader(shaders->hull.get(), nullptr, 0);
	context->DSSetShader(shaders->domain.get(), nullptr, 0);
	context->GSSetShader(shaders->shoreline.get(), nullptr, 0);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
	state.active = true;
	static std::atomic_bool firstDrawLogged = false;
	if (!firstDrawLogged.exchange(true, std::memory_order_relaxed))
		logger::info("[PIXL Ocean Dynamics] Adaptive tessellation reached classified Skyrim water geometry {} (descriptor 0x{:x}, setup thread {}, draw thread {}); original mesh coverage retained",
			static_cast<const void*>(packet.geometry), descriptor, packet.setupThread, GetCurrentThreadId());
	return true;
}
bool OceanDynamics::HasOceanSprayDrawPending() const noexcept
{
	return globals::state && sprayDrawEligibleFrame == globals::state->frameCount;
}

void OceanDynamics::EndWaterDraw(ID3D11DeviceContext* context) noexcept
{
	auto& state = oceanDrawState;
	if (!state.active || !context)
		return;
	context->IASetPrimitiveTopology(state.topology);
	context->HSSetShader(state.hull, nullptr, 0);
	context->DSSetShader(state.domain, nullptr, 0);
	context->GSSetShader(state.geometry, nullptr, 0);
	context->VSSetConstantBuffers(13, 1, &state.vertexCageConstants);
	context->VSSetShaderResources(123, 1, &state.vertexCageTexture);
	context->DSSetConstantBuffers(7, 1, &state.constants7);
	context->DSSetConstantBuffers(12, 1, &state.constants12);
	context->DSSetConstantBuffers(13, 1, &state.domainCageConstants);
	context->DSSetShaderResources(120, 3, state.textures);
	context->DSSetShaderResources(123, 1, &state.domainCageTexture);
	context->DSSetShaderResources(124, 1, &state.localTerrainTexture);
	context->DSSetSamplers(15, 1, &state.sampler);
	context->GSSetConstantBuffers(7, 1, &state.geometryBathymetry);
	context->GSSetConstantBuffers(12, 1, &state.geometryPerFrame);
	context->GSSetConstantBuffers(13, 1, &state.geometryCageConstants);
	context->GSSetShaderResources(120, 5, state.geometryTextures);
	context->GSSetSamplers(15, 1, &state.geometrySampler);
	context->HSSetConstantBuffers(7, 1, &state.hullConstants7);
	context->HSSetConstantBuffers(12, 1, &state.hullConstants12);
	context->HSSetShaderResources(120, 3, state.hullTextures);
	context->HSSetShaderResources(124, 1, &state.hullLocalTerrainTexture);
	context->HSSetSamplers(15, 1, &state.hullSampler);
	ReleaseOceanDrawState(state);
}

bool OceanDynamics::CreateResources()
{
	if (!globals::d3d::device || !globals::d3d::context)
		return false;
	try {
		spectrumInitial = CreateArrayTexture(Resolution, "OceanDynamics::InitialSpectrum");
		for (std::size_t index = 0; index < spectrumFields.size(); ++index) {
			spectrumFields[index] = CreateArrayTexture(Resolution,
				index == 0 ? "OceanDynamics::SpectrumA" : "OceanDynamics::SpectrumB");
			fftScratch[index] = CreateArrayTexture(Resolution,
				index == 0 ? "OceanDynamics::FFTScratchA" : "OceanDynamics::FFTScratchB");
		}
		displacement = CreateArrayTexture(Resolution, "OceanDynamics::Displacement");
		previousDisplacement = CreateArrayTexture(Resolution, "OceanDynamics::PreviousDisplacement");
		previousRenderedDisplacement = CreateArrayTexture(Resolution, "OceanDynamics::PreviousRenderedDisplacement");
		D3D11_TEXTURE2D_DESC readback{};
		readback.Width = Resolution;
		readback.Height = Resolution;
		readback.MipLevels = 1;
		readback.ArraySize = 1;
		readback.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		readback.SampleDesc.Count = 1;
		readback.Usage = D3D11_USAGE_STAGING;
		readback.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		for (auto& slot : gameplayReadback)
			winrt::check_hresult(globals::d3d::device->CreateTexture2D(&readback, nullptr, slot.put()));
		simulationCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<SimulationConstants>(), "OceanDynamics::SimulationCB");
		fftCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<FFTConstants>(), "OceanDynamics::FFTConstants");
		bathymetryCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<BathymetryConstants>(), "OceanDynamics::BathymetryCB");
		waveCageCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<WaveCageConstants>(), "OceanDynamics::WaveCageCB");
		const BathymetryConstants emptyBathymetry{};
		bathymetryCB->Update(&emptyBathymetry, sizeof(emptyBathymetry));
		const WaveCageConstants emptyCage{};
		waveCageCB->Update(&emptyCage, sizeof(emptyCage));
		D3D11_SAMPLER_DESC sampler{};
		sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		sampler.MaxLOD = D3D11_FLOAT32_MAX;
		winrt::check_hresult(globals::d3d::device->CreateSamplerState(&sampler, displacementSampler.put()));
		resourcesReady = EnsureShaders();
	} catch (const std::exception& error) {
		logger::error("[PIXL Ocean Dynamics] GPU initialization failed: {}", error.what());
		ReleaseResources();
		return false;
	}
	if (resourcesReady) {
		initializationPending = true;
		failureLogged = false;
		logger::info("[PIXL Ocean Dynamics] Allocated 4 x {}^2 spectral cascades", Resolution);
	}
	return resourcesReady;
}

bool OceanDynamics::SubmitTerrainPatch(const PIXL_OceanTerrainPatchV1* patch,
	const float* heights, std::uint32_t count) noexcept
{
	if (!patch || !heights || patch->version != 1 ||
		patch->resolution != LocalTerrainResolution || count != LocalTerrainSampleCount ||
		patch->worldspaceFormID == 0 || !std::isfinite(patch->originX) ||
		!std::isfinite(patch->originY) || !std::isfinite(patch->spacing) ||
		patch->spacing < 16.0f || patch->spacing > 512.0f)
		return false;
	LocalTerrainData next{};
	next.metadata = *patch;
	std::copy_n(heights, LocalTerrainSampleCount, next.heights.begin());
	for (auto& height : next.heights) {
		if (!std::isfinite(height))
			height = -32768.0f;
		else if (height < -20000.0f || height > 30000.0f)
			return false;
	}
	std::lock_guard lock(localTerrainMutex);
	// Streaming can temporarily hide some land queries. A partial refresh must
	// never replace already known terrain with the coarse global fallback.
	const auto previous = std::find_if(localTerrainTiles.begin(), localTerrainTiles.end(),
		[&next](const LocalTerrainData& tile) {
			return tile.metadata.worldspaceFormID == next.metadata.worldspaceFormID &&
				tile.metadata.originX == next.metadata.originX && tile.metadata.originY == next.metadata.originY &&
				tile.metadata.spacing == next.metadata.spacing;
		});
	if (previous != localTerrainTiles.end()) {
		for (std::size_t i=0; i<next.heights.size(); ++i)
			if (next.heights[i] < -30000.0f) next.heights[i] = previous->heights[i];
		if (next.heights == previous->heights)
			return true; // Already uploaded; do not rebuild a stable coastline for a timestamp-only refresh.
	}
	if (std::count_if(next.heights.begin(),next.heights.end(),[](float h){return h > -30000.0f;}) <= 16)
		return false;
	next.generation = pendingLocalTerrain.generation + 1;
	pendingLocalTerrain = next;
	if (std::count_if(next.heights.begin(),next.heights.end(),[](float h){return h > -30000.0f;}) > 16) {
		std::erase_if(localTerrainTiles,[&next](const LocalTerrainData& tile) {
			return tile.metadata.worldspaceFormID != next.metadata.worldspaceFormID ||
				(tile.metadata.originX == next.metadata.originX && tile.metadata.originY == next.metadata.originY);
		});
		localTerrainTiles.push_back(std::move(next));
		if (localTerrainTiles.size() > 25) localTerrainTiles.erase(localTerrainTiles.begin());
	}
	return true;
}

void OceanDynamics::UpdateLocalTerrainAtlas() noexcept
{
	if (localAtlasInvalidated.exchange(false,std::memory_order_acq_rel)) {
		localAtlasBounds[3]=0;
		localAtlasWorld=0;
		localAtlasRevision=0;
		localAtlasHeights.clear();
		localAtlasTarget.clear();
	}
	if (localAtlasFailed || settings.EnableSimulation==0 || settings.EnableGeometryDisplacement==0) return;
	// Keep a world-cell-aligned atlas, retaining neighboring cells when the
	// player crosses a boundary. Missing samples blend into the global floor
	// through a distance field, rather than exposing a moving square patch.
	constexpr int side=161;
	constexpr float spacing=128.0f;
	const auto terrain=globals::pipeline::terrainOcclusion.oceanHeightfield.load(std::memory_order_acquire);
	if (!terrain || !globals::d3d::device || !globals::d3d::context) return;
	const auto camera=GetAbsoluteCameraPosition();
	const float originX=(std::floor(camera.x/4096.0f)-2.0f)*4096.0f;
	const float originY=(std::floor(camera.y/4096.0f)-2.0f)*4096.0f;
	std::vector<LocalTerrainData> tiles;
	std::uint64_t revision{};
	{
		std::lock_guard lock(localTerrainMutex);
		revision=pendingLocalTerrain.generation;
		if (revision!=localAtlasRevision || originX!=localAtlasBounds[0] || originY!=localAtlasBounds[1] || localAtlasWorld!=terrain->worldspaceFormID)
			tiles=localTerrainTiles;
	}
	try {
		const bool moved=localAtlasBounds[0]!=originX || localAtlasBounds[1]!=originY || localAtlasWorld!=terrain->worldspaceFormID;
		if (!localTerrainAtlas || revision!=localAtlasRevision || moved) {
			std::vector<float> raw(side*side,-32768.0f), global(side*side), distance(side*side,0.0f);
			for (int y=0;y<side;++y) for(int x=0;x<side;++x) {
				const float h=terrain->Sample(originX+x*spacing,originY+y*spacing);
				global[y*side+x]=std::isfinite(h)?h:-32768.0f;
			}
			for (const auto& tile:tiles) {
				if (tile.metadata.worldspaceFormID!=terrain->worldspaceFormID || std::abs(tile.metadata.spacing-spacing)>0.1f) continue;
				const int ox=static_cast<int>(std::round((tile.metadata.originX-originX)/spacing));
				const int oy=static_cast<int>(std::round((tile.metadata.originY-originY)/spacing));
				for(int y=0;y<33;++y) for(int x=0;x<33;++x) {
					const int ax=ox+x,ay=oy+y;
					if(ax>=0&&ay>=0&&ax<side&&ay<side&&tile.heights[y*33+x]>-30000.0f)
						raw[ay*side+ax]=tile.heights[y*33+x];
				}
			}
			for(int y=0;y<side;++y) for(int x=0;x<side;++x) {
				const int i=y*side+x;
				distance[i]=raw[i]>-30000.0f ? static_cast<float>(std::min({x,y,side-1-x,side-1-y,6})) : 0.0f;
				if(x>0) distance[i]=std::min(distance[i],distance[i-1]+1);
				if(y>0) distance[i]=std::min(distance[i],distance[i-side]+1);
			}
			for(int y=side-1;y>=0;--y) for(int x=side-1;x>=0;--x) {
				const int i=y*side+x;
				if(x+1<side) distance[i]=std::min(distance[i],distance[i+1]+1);
				if(y+1<side) distance[i]=std::min(distance[i],distance[i+side]+1);
			}
			localAtlasTarget.resize(side*side);
			for(int i=0;i<side*side;++i) {
				const float t=std::clamp(distance[i]/6.0f,0.0f,1.0f);
				localAtlasTarget[i]=raw[i]>-30000.0f ? std::lerp(global[i],raw[i],t*t*(3-2*t)) : global[i];
			}
			if (moved || localAtlasHeights.empty()) {
				auto old=std::move(localAtlasHeights);
				localAtlasHeights=global;
				if(!old.empty() && localAtlasWorld==terrain->worldspaceFormID) {
					const int dx=static_cast<int>(std::round((originX-localAtlasBounds[0])/spacing));
					const int dy=static_cast<int>(std::round((originY-localAtlasBounds[1])/spacing));
					for(int y=0;y<side;++y) for(int x=0;x<side;++x)
						if(x+dx>=0&&x+dx<side&&y+dy>=0&&y+dy<side) localAtlasHeights[y*side+x]=old[(y+dy)*side+x+dx];
				}
			}
			localAtlasBounds={originX,originY,spacing,tiles.empty()?0.0f:1.0f};
			localAtlasWorld=terrain->worldspaceFormID;
			localAtlasRevision=revision;
			localAtlasUpdating=true;
			bathymetryUploadPending=true;
			if(!localTerrainAtlas) {
				D3D11_TEXTURE2D_DESC desc{};
				desc.Width=desc.Height=side;
				desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
				desc.Format=DXGI_FORMAT_R32_FLOAT;
				desc.Usage=D3D11_USAGE_DEFAULT;
				desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
				localTerrainAtlas=std::make_unique<Texture2D>(desc,"OceanDynamics::WorldTerrainAtlas");
				D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
				srv.Format=desc.Format; srv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D; srv.Texture2D.MipLevels=1;
				localTerrainAtlas->CreateSRV(srv);
			}
		}
		if(!localAtlasHeights.empty() && localAtlasUpdating) {
			const float dt=globals::game::deltaTime?std::clamp(*globals::game::deltaTime,0.0f,0.05f):1.0f/60.0f;
			const float blend=1-std::exp(-dt*3.0f);
			bool changed=false;
			for(std::size_t i=0;i<localAtlasHeights.size();++i) if(std::abs(localAtlasHeights[i]-localAtlasTarget[i])>0.02f) {
				localAtlasHeights[i]=std::lerp(localAtlasHeights[i],localAtlasTarget[i],blend); changed=true;
			}
			if(changed || moved) globals::d3d::context->UpdateSubresource(localTerrainAtlas->resource.get(),0,nullptr,localAtlasHeights.data(),side*sizeof(float),0);
			localAtlasUpdating=changed;
		}
	} catch(const std::exception& error) {
		localAtlasBounds[3]=0;
		localAtlasFailed=true;
		logger::warn("[PIXL Ocean Dynamics] Detailed terrain atlas unavailable: {}",error.what());
	}
}

bool OceanDynamics::EnsureShaders()
{
	if (spectrumInitCS && spectrumUpdateCS && fftCS && packDisplacementCS)
		return true;
	const wchar_t* shader = L"Data\\PIXL-Shaders\\OceanDynamics\\OceanDynamics.hlsl";
	spectrumInitCS.attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(shader, {}, "cs_5_0", "SpectrumInit")));
	spectrumUpdateCS.attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(shader, {}, "cs_5_0", "SpectrumUpdate")));
	fftCS.attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(shader, {}, "cs_5_0", "ComputeFFT")));
	packDisplacementCS.attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(shader, {}, "cs_5_0", "PackDisplacement")));
	if (!spectrumInitCS || !spectrumUpdateCS || !fftCS || !packDisplacementCS) {
		logger::error("[PIXL Ocean Dynamics] A required compute kernel failed to compile; ocean simulation remains disabled");
		return false;
	}
	return true;
}

bool OceanDynamics::EnsureSprayResources() noexcept
{
	if (sprayParticles && sprayParticlesSRV && sprayParticlesUAV && sprayUpdateCS &&
		sprayVertexShader && sprayPixelShader && sprayConstants && sprayBlendState && sprayDepthState && sprayRasterizer)
		return true;
	if (!globals::d3d::device)
		return false;
	try {
		if (!sprayParticles) {
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = sizeof(SprayParticleGPU) * SprayParticleCount;
			desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = sizeof(SprayParticleGPU);
			std::vector<SprayParticleGPU> initial(SprayParticleCount);
			D3D11_SUBRESOURCE_DATA particleData{ initial.data(), 0, 0 };
			winrt::check_hresult(globals::d3d::device->CreateBuffer(&desc, &particleData, sprayParticles.put()));
			D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
			srv.Format = DXGI_FORMAT_UNKNOWN;
			srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
			srv.Buffer.NumElements = SprayParticleCount;
			winrt::check_hresult(globals::d3d::device->CreateShaderResourceView(sprayParticles.get(), &srv, sprayParticlesSRV.put()));
			D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
			uav.Format = DXGI_FORMAT_UNKNOWN;
			uav.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
			uav.Buffer.NumElements = SprayParticleCount;
			winrt::check_hresult(globals::d3d::device->CreateUnorderedAccessView(sprayParticles.get(), &uav, sprayParticlesUAV.put()));
		}
		if (!sprayConstants) {
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = 48;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			winrt::check_hresult(globals::d3d::device->CreateBuffer(&desc, nullptr, sprayConstants.put()));
		}
		if (!sprayBlendState) {
			D3D11_BLEND_DESC desc{};
			auto& target = desc.RenderTarget[0];
			target.BlendEnable = TRUE;
			target.SrcBlend = D3D11_BLEND_SRC_ALPHA;
			target.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
			target.BlendOp = D3D11_BLEND_OP_ADD;
			target.SrcBlendAlpha = D3D11_BLEND_ONE;
			target.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
			target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
			target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
			winrt::check_hresult(globals::d3d::device->CreateBlendState(&desc, sprayBlendState.put()));
		}
		if (!sprayDepthState) {
			D3D11_DEPTH_STENCIL_DESC desc{};
			desc.DepthEnable = TRUE;
			desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
			desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
			desc.StencilEnable = FALSE;
			winrt::check_hresult(globals::d3d::device->CreateDepthStencilState(&desc, sprayDepthState.put()));
		}
		if (!sprayRasterizer) {
			D3D11_RASTERIZER_DESC desc{};
			desc.FillMode = D3D11_FILL_SOLID;
			desc.CullMode = D3D11_CULL_NONE;
			desc.DepthClipEnable = TRUE;
			desc.MultisampleEnable = TRUE;
			winrt::check_hresult(globals::d3d::device->CreateRasterizerState(&desc, sprayRasterizer.put()));
		}
		const wchar_t* shaderPath = L"Data\\PIXL-Shaders\\OceanDynamics\\OceanSpray.hlsl";
		if (!sprayUpdateCS) sprayUpdateCS.attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(shaderPath, {}, "cs_5_0", "UpdateSpray")));
		if (!sprayVertexShader) sprayVertexShader.attach(static_cast<ID3D11VertexShader*>(Util::CompileShader(shaderPath, {}, "vs_5_0", "SprayVS")));
		if (!sprayPixelShader) sprayPixelShader.attach(static_cast<ID3D11PixelShader*>(Util::CompileShader(shaderPath, {}, "ps_5_0", "SprayPS")));
		return sprayUpdateCS && sprayVertexShader && sprayPixelShader;
	} catch (const std::exception& error) {
		logger::warn("[PIXL Ocean Dynamics] GPU spray unavailable: {}", error.what());
		return false;
	} catch (...) {
		return false;
	}
}

bool OceanDynamics::EnsureBreakerResources() noexcept
{
	if (breakerNodes && breakerUpdateCS && breakerVS && breakerPS)
		return true;
	if (breakerCompileFailed || !globals::d3d::device)
		return false;
	try {
		if (!breakerNodes) {
			constexpr auto count = BreakerAlongCount * BreakerCrossCount;
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = sizeof(BreakerNodeGPU) * count;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			desc.StructureByteStride = sizeof(BreakerNodeGPU);
			const std::vector<BreakerNodeGPU> initial(count);
			D3D11_SUBRESOURCE_DATA data{ initial.data(), 0, 0 };
			winrt::check_hresult(globals::d3d::device->CreateBuffer(&desc, &data, breakerNodes.put()));
			D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
			srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
			srv.Buffer.NumElements = count;
			winrt::check_hresult(globals::d3d::device->CreateShaderResourceView(breakerNodes.get(), &srv, breakerNodesSRV.put()));
			D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
			uav.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
			uav.Buffer.NumElements = count;
			winrt::check_hresult(globals::d3d::device->CreateUnorderedAccessView(breakerNodes.get(), &uav, breakerNodesUAV.put()));
		}
		const wchar_t* path = L"Data\\PIXL-Shaders\\OceanDynamics\\OceanBreakers.hlsl";
		if (!breakerUpdateCS) breakerUpdateCS.attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(path, {}, "cs_5_0", "UpdateBreakers")));
		if (!breakerVS) breakerVS.attach(static_cast<ID3D11VertexShader*>(Util::CompileShader(path, {}, "vs_5_0", "BreakerVS")));
		if (!breakerPS) breakerPS.attach(static_cast<ID3D11PixelShader*>(Util::CompileShader(path, {}, "ps_5_0", "BreakerPS")));
		breakerCompileFailed = !breakerUpdateCS || !breakerVS || !breakerPS;
		return !breakerCompileFailed;
	} catch (const std::exception& error) {
		logger::warn("[PIXL Ocean Dynamics] Breaking-wave sheets unavailable: {}", error.what());
		breakerCompileFailed = true;
		return false;
	}
}

void OceanDynamics::UpdateSpray(ID3D11DeviceContext* context, float seaLevel) noexcept
{
	if (!context || !settings.EnableSimulation || !globals::state ||
		sprayUpdateFrame == globals::state->frameCount ||
		(settings.EnableSpray == 0 && settings.EnableBreakingWaves == 0 && !sprayParticles) || !EnsureSprayResources())
		return;
	struct alignas(16) Constants
	{
		float deltaSeconds, timeSeconds, emissionScale, windSpeed;
		float cameraX, cameraY, seaLevel, cameraZ;
		float windX, windY, spawnRadius, reserved;
	} constants{};
	const auto camera = GetAbsoluteCameraPosition();
	const bool oceanAvailable = std::isfinite(seaLevel);
	constants.deltaSeconds = globals::game::deltaTime ? std::clamp(*globals::game::deltaTime, 0.0f, 0.05f) : 1.0f / 30.0f;
	constants.timeSeconds = static_cast<float>(simulationTime);
	constants.emissionScale = settings.EnableSpray != 0 && oceanAvailable
		? std::clamp(settings.SprayDensity * std::lerp(0.35f, 1.0f,
			std::clamp(weatherEnergySmoothed, 0.0f, 1.0f)), 0.0f, 2.0f) : 0.0f;
	constants.windSpeed = std::clamp(windSpeedSmoothed, 0.0f, 45.0f);
	constants.cameraX = camera.x;
	constants.cameraY = camera.y;
	constants.seaLevel = oceanAvailable ? seaLevel : camera.z;
	constants.cameraZ = camera.z;
	const float direction = FiniteClamp(settings.WindDirectionDegrees, 63.0f, 0.0f, 360.0f) * (kPi / 180.0f);
	constants.windX = std::cos(direction);
	constants.windY = std::sin(direction);
	// Concentrate the fixed particle budget around the visible surf zone instead
	// of scattering almost all candidate emitters across distant open water.
	// Keep the fixed candidate set dense enough to resolve the nearby breaker
	// line instead of spending most of it on distant open water.
	constants.spawnRadius = 1100.0f;
	context->UpdateSubresource(sprayConstants.get(), 0, nullptr, &constants, 0, 0);
	ID3D11Buffer* cb = sprayConstants.get();
	context->CSSetConstantBuffers(0, 1, &cb);
	const auto cageField = waveCageField.load(std::memory_order_acquire);
	const auto* currentWorld = RE::TES::GetSingleton()
		? RE::TES::GetSingleton()->GetRuntimeData2().worldSpace : nullptr;
	const bool cageValid = cageField && waveCageTexture && waveCageTexture->srv && currentWorld &&
		cageField->worldspaceFormID == currentWorld->GetFormID() &&
		std::abs(cageField->seaLevel - std::round(seaLevel / 8.0f) * 8.0f) <= 1.0f;
	const WaveCageConstants cageConstants{ static_cast<float>(WaveCageField::Resolution),
		cageValid ? 1.0f : 0.0f, cageValid ? waveCageBlend.load(std::memory_order_relaxed) : 0.0f, 16.0f * 70.0f };
	if (waveCageCB)
		waveCageCB->Update(&cageConstants, sizeof(cageConstants));
	ID3D11ShaderResourceView* srvs[]{ displacement->srv.get(), previousDisplacement->srv.get(),
		bathymetryMapSRV, localTerrainAtlas && localAtlasBounds[3]>0.5f ? localTerrainAtlas->srv.get() : nullptr,
		cageValid ? waveCageTexture->srv.get() : nullptr };
	context->CSSetShaderResources(0, 5, srvs);
	ID3D11Buffer* bathymetry = bathymetryCB ? bathymetryCB->CB() : nullptr;
	context->CSSetConstantBuffers(1, 1, &bathymetry);
	ID3D11Buffer* cageCB = waveCageCB ? waveCageCB->CB() : nullptr;
	context->CSSetConstantBuffers(2, 1, &cageCB);
	ID3D11SamplerState* sampler = displacementSampler.get();
	context->CSSetSamplers(0, 1, &sampler);
	if (settings.EnableBreakingWaves != 0 && EnsureBreakerResources()) {
		auto* output = breakerNodesUAV.get();
		const UINT keep = 0xffffffffu;
		context->CSSetUnorderedAccessViews(0, 1, &output, &keep);
		context->CSSetShader(breakerUpdateCS.get(), nullptr, 0);
		context->Dispatch((BreakerAlongCount * BreakerCrossCount + 63u) / 64u, 1, 1);
		ID3D11UnorderedAccessView* empty = nullptr;
		context->CSSetUnorderedAccessViews(0, 1, &empty, &keep);
	}
	ID3D11ShaderResourceView* lipSource = settings.EnableBreakingWaves != 0 ? breakerNodesSRV.get() : nullptr;
	context->CSSetShaderResources(6, 1, &lipSource);
	ID3D11UnorderedAccessView* uav = sprayParticlesUAV.get();
	const UINT keepCounter = 0xffffffffu;
	context->CSSetUnorderedAccessViews(0, 1, &uav, &keepCounter);
	context->CSSetShader(sprayUpdateCS.get(), nullptr, 0);
	context->Dispatch((SprayParticleCount + 63u) / 64u, 1, 1);
	ID3D11ShaderResourceView* noSRVs[5]{};
	ID3D11UnorderedAccessView* noUAV = nullptr;
	ID3D11Buffer* noCBs[3]{};
	ID3D11SamplerState* noSampler = nullptr;
	context->CSSetShaderResources(0, 5, noSRVs);
	context->CSSetShaderResources(6, 1, noSRVs);
	context->CSSetUnorderedAccessViews(0, 1, &noUAV, &keepCounter);
	context->CSSetConstantBuffers(0, 3, noCBs);
	context->CSSetSamplers(0, 1, &noSampler);
	context->CSSetShader(nullptr, nullptr, 0);
	sprayUpdateFrame = globals::state->frameCount;
	static std::atomic_bool sprayDispatchLogged = false;
	if (!sprayDispatchLogged.exchange(true, std::memory_order_relaxed))
		logger::info("[PIXL Ocean Dynamics] GPU crest-spray simulation dispatched ({} particles, classified sea level {:.1f}, emission {:.2f})",
			SprayParticleCount, constants.seaLevel, constants.emissionScale);
}

void OceanDynamics::DrawOceanSpray(ID3D11DeviceContext* context) noexcept
{
	if (!context || (settings.EnableSpray == 0 && settings.EnableBreakingWaves == 0) || !globals::state ||
		!IsActive() || sprayDrawFrame == globals::state->frameCount ||
		!HasOceanSprayDrawPending() ||
		!EnsureSprayResources() || !sprayParticlesSRV)
		return;
	if (((globals::state->currentVertexDescriptor >> 11u) & 0xfu) == 10u)
		return; // stencil water pass is not a color target for particles
	sprayDrawFrame = globals::state->frameCount;
	sprayDrawEligibleFrame = std::numeric_limits<std::uint64_t>::max();
	ID3D11VertexShader* previousVS = nullptr;
	ID3D11PixelShader* previousPS = nullptr;
	ID3D11GeometryShader* previousGS = nullptr;
	ID3D11HullShader* previousHS = nullptr;
	ID3D11DomainShader* previousDS = nullptr;
	ID3D11InputLayout* previousLayout = nullptr;
	ID3D11Buffer* previousFrame = nullptr;
	ID3D11ShaderResourceView* previousSRV = nullptr;
	ID3D11ShaderResourceView* previousNodes = nullptr;
	ID3D11Buffer* previousPSConstants[2]{};
	ID3D11Buffer* previousPSShared = nullptr;
	ID3D11Buffer* previousPSFrame = nullptr;
	ID3D11BlendState* previousBlend = nullptr;
	ID3D11DepthStencilState* previousDepth = nullptr;
	ID3D11RasterizerState* previousRasterizer = nullptr;
	D3D11_PRIMITIVE_TOPOLOGY previousTopology{};
	FLOAT blendFactor[4]{};
	UINT sampleMask{};
	UINT stencilRef{};
	context->VSGetShader(&previousVS, nullptr, nullptr);
	context->PSGetShader(&previousPS, nullptr, nullptr);
	context->GSGetShader(&previousGS, nullptr, nullptr);
	context->HSGetShader(&previousHS, nullptr, nullptr);
	context->DSGetShader(&previousDS, nullptr, nullptr);
	context->IAGetInputLayout(&previousLayout);
	context->IAGetPrimitiveTopology(&previousTopology);
	context->VSGetConstantBuffers(12, 1, &previousFrame);
	context->VSGetShaderResources(0, 1, &previousSRV);
	context->VSGetShaderResources(6, 1, &previousNodes);
	context->PSGetConstantBuffers(0, 2, previousPSConstants);
	context->PSGetConstantBuffers(5, 1, &previousPSShared);
	context->PSGetConstantBuffers(12, 1, &previousPSFrame);
	context->OMGetBlendState(&previousBlend, blendFactor, &sampleMask);
	context->OMGetDepthStencilState(&previousDepth, &stencilRef);
	context->RSGetState(&previousRasterizer);
	ID3D11Buffer* frame = *globals::game::perFrame;
	ID3D11ShaderResourceView* particleSRV = sprayParticlesSRV.get();
	context->IASetInputLayout(nullptr);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->VSSetShader(sprayVertexShader.get(), nullptr, 0);
	context->PSSetShader(sprayPixelShader.get(), nullptr, 0);
	context->GSSetShader(nullptr, nullptr, 0);
	context->HSSetShader(nullptr, nullptr, 0);
	context->DSSetShader(nullptr, nullptr, 0);
	context->VSSetConstantBuffers(12, 1, &frame);
	context->VSSetShaderResources(0, 1, &particleSRV);
	context->OMSetBlendState(sprayBlendState.get(), nullptr, 0xffffffffu);
	context->OMSetDepthStencilState(sprayDepthState.get(), 0);
	context->RSSetState(sprayRasterizer.get());
	if (settings.EnableBreakingWaves != 0 && breakerNodesSRV && breakerVS && breakerPS) {
		auto* nodes = breakerNodesSRV.get();
		ID3D11Buffer* constants[]{ sprayConstants.get(), bathymetryCB->CB() };
		auto* shared = globals::state->sharedDataCB ? globals::state->sharedDataCB->CB() : nullptr;
		context->VSSetShaderResources(6, 1, &nodes);
		context->VSSetShader(breakerVS.get(), nullptr, 0);
		context->PSSetShader(breakerPS.get(), nullptr, 0);
		context->PSSetConstantBuffers(0, 2, constants);
		context->PSSetConstantBuffers(5, 1, &shared);
		context->PSSetConstantBuffers(12, 1, &frame);
		context->DrawInstanced(16u * 6u, BreakerAlongCount * BreakerCrossCount, 0, 0);
		static std::atomic_bool sheetsLogged = false;
		if (!sheetsLogged.exchange(true))
			logger::info("[PIXL Ocean Dynamics] Connected overturning crest sheets reached the water color batch");
	}
	context->VSSetShader(sprayVertexShader.get(), nullptr, 0);
	context->PSSetShader(sprayPixelShader.get(), nullptr, 0);
	if (settings.EnableSpray != 0)
		context->DrawInstanced(6, SprayParticleCount, 0, 0);
	static std::atomic_bool sprayDrawLogged = false;
	if (!sprayDrawLogged.exchange(true, std::memory_order_relaxed))
		logger::info("[PIXL Ocean Dynamics] First GPU spray billboard draw reached the ocean color pass");
	ID3D11ShaderResourceView* noSRV = nullptr;
	context->VSSetShaderResources(0, 1, &noSRV);
	context->VSSetShader(previousVS, nullptr, 0);
	context->PSSetShader(previousPS, nullptr, 0);
	context->GSSetShader(previousGS, nullptr, 0);
	context->HSSetShader(previousHS, nullptr, 0);
	context->DSSetShader(previousDS, nullptr, 0);
	context->IASetInputLayout(previousLayout);
	context->IASetPrimitiveTopology(previousTopology);
	context->VSSetConstantBuffers(12, 1, &previousFrame);
	context->VSSetShaderResources(0, 1, &previousSRV);
	context->VSSetShaderResources(6, 1, &previousNodes);
	context->PSSetConstantBuffers(0, 2, previousPSConstants);
	context->PSSetConstantBuffers(5, 1, &previousPSShared);
	context->PSSetConstantBuffers(12, 1, &previousPSFrame);
	context->OMSetBlendState(previousBlend, blendFactor, sampleMask);
	context->OMSetDepthStencilState(previousDepth, stencilRef);
	context->RSSetState(previousRasterizer);
	if (previousVS) previousVS->Release();
	if (previousPS) previousPS->Release();
	if (previousGS) previousGS->Release();
	if (previousHS) previousHS->Release();
	if (previousDS) previousDS->Release();
	if (previousLayout) previousLayout->Release();
	if (previousFrame) previousFrame->Release();
	if (previousSRV) previousSRV->Release();
	if (previousNodes) previousNodes->Release();
	for (auto* buffer : previousPSConstants) if (buffer) buffer->Release();
	if (previousPSShared) previousPSShared->Release();
	if (previousPSFrame) previousPSFrame->Release();
	if (previousBlend) previousBlend->Release();
	if (previousDepth) previousDepth->Release();
	if (previousRasterizer) previousRasterizer->Release();
}

void OceanDynamics::ReleaseResources() noexcept
{
	resourcesReady = false;
	{
		std::lock_guard lock(pendingOceanDrawMutex);
		pendingOceanDraws.clear();
	}
	waterTessShaders.clear();
	initializationPending = true;
	displacement.reset();
	previousDisplacement.reset();
	previousRenderedDisplacement.reset();
	renderHistoryDirty = false;
	for (auto& slot : gameplayReadback) slot = nullptr;
	gameplayReadbackPending = {};
	gameplayReadbackSlot = 0;
	gameplayReadbackCadence = 0;
	++queryGeneration;
	gameplayHistory.store({}, std::memory_order_release);
	previousDisplacementValid = false;
	bathymetryUploadPending = true;
	openOceanTileAvailable.store(false, std::memory_order_relaxed);
	geometryBindingLogged.store(false, std::memory_order_relaxed);
	simulationDispatchLogged.store(false, std::memory_order_relaxed);
	oceanSurfaceWasBound = false;
	bathymetryWorldspace.clear();
	bathymetryMapSRV = nullptr;
	spectrumInitial.reset();
	for (auto& texture : spectrumFields) texture.reset();
	for (auto& texture : fftScratch) texture.reset();
	simulationCB.reset();
	fftCB.reset();
	bathymetryCB.reset();
	waveCageCB.reset();
	waveCageTexture.reset();
	localTerrainAtlas.reset();
	localAtlasHeights.clear();
	localAtlasTarget.clear();
	localAtlasBounds={};
	localAtlasRevision=0;
	localAtlasWorld=0;
	localAtlasFailed=false;
	localAtlasUpdating=false;
	waveCageField.store({}, std::memory_order_release);
	waveCageBlend.store(0.0f, std::memory_order_relaxed);
	displacementSampler = nullptr;
	sprayParticles = nullptr;
	sprayParticlesSRV = nullptr;
	sprayParticlesUAV = nullptr;
	sprayUpdateCS = nullptr;
	sprayVertexShader = nullptr;
	sprayPixelShader = nullptr;
	sprayConstants = nullptr;
	sprayBlendState = nullptr;
	sprayDepthState = nullptr;
	sprayRasterizer = nullptr;
	breakerNodes = nullptr;
	breakerNodesSRV = nullptr;
	breakerNodesUAV = nullptr;
	breakerUpdateCS = nullptr;
	breakerVS = nullptr;
	breakerPS = nullptr;
	breakerCompileFailed = false;
	sprayUpdateFrame = std::numeric_limits<std::uint64_t>::max();
	sprayDrawFrame = std::numeric_limits<std::uint64_t>::max();
	sprayDrawEligibleFrame = std::numeric_limits<std::uint64_t>::max();
}

void OceanDynamics::SetupResources()
{
	ReleaseResources();
	// Bathymetry is an OceanDynamics dependency even when optional terrain
	// shadow rendering is disabled in the user's module settings.
	globals::pipeline::terrainOcclusion.EnsureHeightmapCatalog();
	CreateResources();
}

void OceanDynamics::Reset()
{
	// Present ends a rendered frame, independently of the 30 Hz simulation.
	// Keep foam's previous simulation field separate from motion-vector history.
	if (renderHistoryDirty && displacement && previousRenderedDisplacement && globals::d3d::context) {
		auto* context = globals::d3d::context;
		ID3D11ShaderResourceView* empty = nullptr;
		context->VSSetShaderResources(121, 1, &empty);
		context->DSSetShaderResources(121, 1, &empty);
		context->PSSetShaderResources(121, 1, &empty);
		context->CopyResource(previousRenderedDisplacement->resource.get(), displacement->resource.get());
		renderHistoryDirty = false;
	}
}

void OceanDynamics::OnWorldspaceChanged() noexcept
{
	localAtlasInvalidated.store(true,std::memory_order_release);
	{
		std::lock_guard lock(pendingOceanDrawMutex);
		pendingOceanDraws.clear();
	}
	previousDisplacementValid = false;
	openOceanTileAvailable.store(false, std::memory_order_relaxed);
	geometryBindingLogged.store(false, std::memory_order_relaxed);
	oceanSurfaceWasBound = false;
	bathymetryUploadPending = true;
	bathymetryMapSRV = nullptr;
	bathymetryWorldspace.clear();
	simulationSecondsSinceDispatch = 0.0f;
	lastSimulationFrame = std::numeric_limits<std::uint64_t>::max();
	++queryGeneration;
	gameplayReadbackPending = {};
	gameplayHistory.store({}, std::memory_order_release);
}

void OceanDynamics::ClearShaderCache()
{
	breakerUpdateCS = nullptr;
	breakerVS = nullptr;
	breakerPS = nullptr;
	breakerCompileFailed = false;
	waterTessShaders.clear();
	spectrumInitCS = nullptr;
	spectrumUpdateCS = nullptr;
	fftCS = nullptr;
	packDisplacementCS = nullptr;
	sprayUpdateCS = nullptr;
	sprayVertexShader = nullptr;
	sprayPixelShader = nullptr;
	if (resourcesReady && !EnsureShaders()) {
		resourcesReady = false;
		if (!failureLogged) {
			logger::error("[PIXL Ocean Dynamics] Shader reload failed; using vanilla-compatible water fallback");
			failureLogged = true;
		}
	}
}

void OceanDynamics::RunSimulation(float deltaSeconds)
{
	if (!resourcesReady && !CreateResources())
		return;
	if (!EnsureShaders() || !globals::d3d::context)
		return;
	auto* context = globals::d3d::context;
	// The prior field supplies water motion vectors. Release read bindings before
	// copying and before the current field is written as a UAV.
	ID3D11ShaderResourceView* nullWaterSRVs[4]{};
	context->VSSetShaderResources(120, 3, nullWaterSRVs);
	context->DSSetShaderResources(120, 3, nullWaterSRVs);
	context->PSSetShaderResources(120, 4, nullWaterSRVs);
	if (previousDisplacementValid) {
		context->CopyResource(previousDisplacement->resource.get(), displacement->resource.get());
	}
	SimulationConstants constants{};
	constants.timeSeconds = static_cast<float>(simulationTime);
	constants.windSpeed = FiniteClamp(settings.WindSpeed, 8.0f, 0.0f, 45.0f);
	constants.windDirectionRadians = FiniteClamp(settings.WindDirectionDegrees, 63.0f, -3600.0f, 3600.0f) * (kPi / 180.0f);
	constants.swellHeight = FiniteClamp(settings.SwellHeight, 0.0f, 0.0f, 12.0f);
	constants.swellPeriod = FiniteClamp(settings.SwellPeriod, 11.7f, 2.0f, 24.0f);
	constants.swellDirectionRadians = FiniteClamp(settings.SwellDirectionDegrees, 76.0f, 0.0f, 360.0f) * (kPi / 180.0f);
	constants.crossSwellDirectionRadians = FiniteClamp(settings.CrossSwellDirectionDegrees, 135.0f, 0.0f, 360.0f) * (kPi / 180.0f);
	constants.crossSwellStrength = FiniteClamp(settings.CrossSwellStrength, 0.24f, 0.0f, 1.0f);
	constants.deepRogueWaveStrength = FiniteClamp(settings.DeepRogueWaveStrength, 1.0f, 0.0f, 1.0f);
	constants.choppiness = FiniteClamp(settings.Choppiness, 0.49f, 0.0f, 1.5f);
	constants.deltaSeconds = FiniteClamp(deltaSeconds, 1.0f / 30.0f, 0.0f, 0.1f);
	constants.foamLifetime = FiniteClamp(settings.FoamLifetime, 5.5f, 0.5f, 20.0f);
	constants.foamStrength = FiniteClamp(settings.FoamStrength, 0.80f, 0.0f, 2.0f);
	constants.resolution = static_cast<float>(Resolution);
	constants.inverseResolution = 1.0f / Resolution;
	std::copy(kCascadeExtent.begin(), kCascadeExtent.end(), constants.cascadeExtent);
	float targetWeatherEnergy = 1.0f;
	float targetWindSpeed = constants.windSpeed;
	if (auto* weather = WeatherManager::GetSingleton()) {
		const auto& state = weather->GetContext();
		targetWeatherEnergy = std::clamp(0.75f + state.windIntensity * 0.9f + state.storminess * 0.65f, 0.35f, 2.25f);
		targetWindSpeed = std::clamp(constants.windSpeed * (0.35f + state.windIntensity * 0.9f), 0.5f, 45.0f);
	}
	// WeatherManager already blends Skyrim's incoming/outgoing weather. This
	// second, short filter avoids a spectral energy pop when a weather override
	// or worldspace transition changes the semantic context in one frame.
	const float weatherBlend = 1.0f - std::exp(-std::clamp(deltaSeconds, 0.0f, 0.1f) * 0.45f);
	weatherEnergySmoothed = std::lerp(weatherEnergySmoothed, targetWeatherEnergy, weatherBlend);
	windSpeedSmoothed = std::lerp(windSpeedSmoothed, targetWindSpeed, weatherBlend);
	constants.weatherEnergy = std::clamp(weatherEnergySmoothed, 0.25f, 2.5f);
	constants.windSpeed = std::clamp(windSpeedSmoothed, 0.0f, 45.0f);
	simulationCB->Update(&constants, sizeof(constants));
	ID3D11Buffer* simulationBuffer = simulationCB->CB();
	context->CSSetConstantBuffers(0, 1, &simulationBuffer);
	ID3D11ShaderResourceView* noSRV[4]{};
	ID3D11UnorderedAccessView* noUAV[2]{};
	ID3D11Buffer* noBuffers[2]{};
	ID3D11UnorderedAccessView* uavs[2]{};
	UINT counts[2]{ 0xffffffffu, 0xffffffffu };

	if (initializationPending) {
		ID3D11UnorderedAccessView* initUav = spectrumInitial->uav.get();
		context->CSSetUnorderedAccessViews(0, 1, &initUav, counts);
		context->CSSetShader(spectrumInitCS.get(), nullptr, 0);
		context->Dispatch(Resolution / 8, Resolution / 8, CascadeCount);
		context->CSSetUnorderedAccessViews(0, 1, noUAV, counts);
		context->CSSetShader(nullptr, nullptr, 0);
		initializationPending = false;
	}

	ID3D11ShaderResourceView* initSrv = spectrumInitial->srv.get();
	uavs[0] = spectrumFields[0]->uav.get();
	uavs[1] = spectrumFields[1]->uav.get();
	context->CSSetShaderResources(0, 1, &initSrv);
	context->CSSetUnorderedAccessViews(0, 2, uavs, counts);
	context->CSSetShader(spectrumUpdateCS.get(), nullptr, 0);
	context->Dispatch(Resolution / 8, Resolution / 8, CascadeCount);
	context->CSSetShaderResources(0, 2, noSRV);
		context->CSSetUnorderedAccessViews(0, 2, noUAV, counts);
	context->CSSetShader(nullptr, nullptr, 0);

	for (std::uint32_t vertical = 0; vertical < 2; ++vertical) {
		FFTConstants fft{ Resolution, vertical, { 0u, 0u } };
		fftCB->Update(&fft, sizeof(fft));
		ID3D11Buffer* fftBuffer = fftCB->CB();
		context->CSSetConstantBuffers(1, 1, &fftBuffer);
		auto* sourceA = vertical == 0 ? spectrumFields[0].get() : fftScratch[0].get();
		auto* sourceB = vertical == 0 ? spectrumFields[1].get() : fftScratch[1].get();
		auto* destinationA = vertical == 0 ? fftScratch[0].get() : spectrumFields[0].get();
		auto* destinationB = vertical == 0 ? fftScratch[1].get() : spectrumFields[1].get();
		ID3D11ShaderResourceView* inputs[] = { sourceA->srv.get(), sourceB->srv.get() };
		ID3D11UnorderedAccessView* outputs[] = { destinationA->uav.get(), destinationB->uav.get() };
		context->CSSetShaderResources(0, 2, inputs);
		context->CSSetUnorderedAccessViews(0, 2, outputs, counts);
		context->CSSetShader(fftCS.get(), nullptr, 0);
		context->Dispatch(1, Resolution, CascadeCount);
		context->CSSetShaderResources(0, 2, noSRV);
		context->CSSetUnorderedAccessViews(0, 2, noUAV, counts);
		context->CSSetShader(nullptr, nullptr, 0);
	}
	FFTConstants finalNormalize{ Resolution, 0u, { 0u, 0u } };
	fftCB->Update(&finalNormalize, sizeof(finalNormalize));
	ID3D11Buffer* normalizeBuffer = fftCB->CB();
	context->CSSetConstantBuffers(1, 1, &normalizeBuffer);
	ID3D11ShaderResourceView* transformed[] = { spectrumFields[0]->srv.get(), spectrumFields[1]->srv.get() };
	ID3D11UnorderedAccessView* packed = displacement->uav.get();
	context->CSSetShaderResources(0, 2, transformed);
	context->CSSetUnorderedAccessViews(0, 1, &packed, counts);
	context->CSSetShader(packDisplacementCS.get(), nullptr, 0);
	context->Dispatch(Resolution / 8, Resolution / 8, CascadeCount);
	context->CSSetShaderResources(0, 2, noSRV);
	context->CSSetUnorderedAccessViews(0, 1, noUAV, counts);
	context->CSSetShader(nullptr, nullptr, 0);
	if (!previousDisplacementValid) {
		context->CopyResource(previousDisplacement->resource.get(), displacement->resource.get());
		context->CopyResource(previousRenderedDisplacement->resource.get(), displacement->resource.get());
		previousDisplacementValid = true;
	}
	renderHistoryDirty = true;
	context->CSSetConstantBuffers(0, 2, noBuffers);
	QueueGameplayReadback(context);
	if (!simulationDispatchLogged.exchange(true, std::memory_order_relaxed))
		logger::info("[PIXL Ocean Dynamics] First spectral simulation dispatch completed ({}^2 x {} cascades)", Resolution, CascadeCount);
}

void OceanDynamics::UpdateSimulationForCurrentFrame() noexcept
{
	if (!IsActive() || !globals::game::deltaTime || !globals::state)
		return;
	const auto frame = globals::state->frameCount;
	if (frame == lastSimulationFrame)
		return;
	lastSimulationFrame = frame;
	const float frameDelta = std::clamp(*globals::game::deltaTime, 0.0f, 0.1f);
	if (!(frameDelta > 0.0f))
		return;
	simulationTime += frameDelta;
	simulationSecondsSinceDispatch += frameDelta;
	// Bound the compute workload to 30 updates per second while keeping the
	// spectral phase clock tied to elapsed gameplay time.
	if (simulationSecondsSinceDispatch < (1.0f / 30.0f))
		return;
	const float elapsed = std::min(simulationSecondsSinceDispatch, 0.1f);
	simulationSecondsSinceDispatch = 0.0f;
	try {
		RunSimulation(elapsed);
	} catch (const std::exception& error) {
		if (!failureLogged) {
			logger::error("[PIXL Ocean Dynamics] Simulation update failed; displacement is paused: {}", error.what());
			failureLogged = true;
		}
	} catch (...) {
		if (!failureLogged) {
			logger::error("[PIXL Ocean Dynamics] Simulation update failed; displacement is paused");
			failureLogged = true;
		}
	}
}

void OceanDynamics::EarlyPrepass()
{
	if (!globals::pipeline::terrainOcclusion.loaded)
		globals::pipeline::terrainOcclusion.LoadHeightmap();
	UpdateWaveCageField();
	UpdateLocalTerrainAtlas();
	if (openOceanTileAvailable.exchange(false, std::memory_order_relaxed))
		UpdateSimulationForCurrentFrame();
}

void OceanDynamics::DrawSettings()
{
	bool changed = false;
	changed |= Util::UIntCheckbox("Enable ocean simulation", &settings.EnableSimulation);
	if (auto tooltip = Util::HoverTooltipWrapper()) ImGui::TextWrapped("Runs experimental GPU waves. PIXL automatically classifies broad, connected, non-flowmapped ocean water; lake and river tiles retain their existing rendering.");
	ImGui::BeginDisabled(settings.EnableSimulation == 0);
	changed |= Util::UIntCheckbox("Optional breaker sheets (experimental)", &settings.EnableBreakingWaves);
	if (auto tooltip = Util::HoverTooltipWrapper()) ImGui::TextWrapped("Adds a separate connected overturning sheet above steep crests. Off by default for the stable shoreline mode; rolling water and foam wash continue without it.");
	changed |= Util::UIntCheckbox("Animate ocean waves", &settings.EnableGeometryDisplacement);
	if (auto tooltip = Util::HoverTooltipWrapper()) ImGui::TextWrapped("Adds FFT displacement and adaptive tessellation to classified ocean water while preserving Skyrim's native coverage and shoreline meshes.");
	static constexpr const char* geometryQualities[]{ "Soft", "Balanced", "Detailed", "Strong" };
	int geometryQuality = static_cast<int>(std::min(settings.GeometryQuality, 3u));
	if (ImGui::Combo("Ocean Surface Detail", &geometryQuality, geometryQualities, 4)) {
		settings.GeometryQuality = static_cast<std::uint32_t>(std::clamp(geometryQuality, 0, 3));
		changed = true;
	}
	if (auto tooltip = Util::HoverTooltipWrapper()) ImGui::TextWrapped("Controls how finely the ocean mesh is tessellated. Higher settings refine close wave silhouettes; shoreline edges receive extra detail. Balanced caps edge factors at 32.");
	changed |= ImGui::SliderFloat("Local wind speed", &settings.WindSpeed, 0.0f, 45.0f, "%.1f m/s");
	changed |= ImGui::SliderFloat("Wind direction", &settings.WindDirectionDegrees, 0.0f, 360.0f, "%.0f deg");
	changed |= ImGui::SliderFloat("Independent swell height", &settings.SwellHeight, 0.0f, 12.0f, "%.2f m");
	changed |= ImGui::SliderFloat("Swell period", &settings.SwellPeriod, 2.0f, 24.0f, "%.1f s");
	changed |= ImGui::SliderFloat("Swell direction", &settings.SwellDirectionDegrees, 0.0f, 360.0f, "%.0f deg");
	changed |= ImGui::SliderFloat("Cross swell direction", &settings.CrossSwellDirectionDegrees, 0.0f, 360.0f, "%.0f deg");
	changed |= ImGui::SliderFloat("Cross swell strength", &settings.CrossSwellStrength, 0.0f, 1.0f, "%.2f");
	if (auto tooltip = Util::HoverTooltipWrapper()) ImGui::TextWrapped("Blends a second swell direction into the same ocean surface. Higher values create more crossing wave patterns at medium and deep water.");
	changed |= ImGui::SliderFloat("Deep rogue waves", &settings.DeepRogueWaveStrength, 0.0f, 1.0f, "%.2f");
	if (auto tooltip = Util::HoverTooltipWrapper()) ImGui::TextWrapped("Adds longer, larger wave groups in deep water. They blend into the medium swell and fade before the shoreline.");
	changed |= ImGui::SliderFloat("Wave choppiness", &settings.Choppiness, 0.0f, 1.5f, "%.2f");
	changed |= ImGui::SliderFloat("Shore wave shoaling", &settings.ShoalingStrength, 0.0f, 1.5f, "%.2f");
	if (auto tooltip = Util::HoverTooltipWrapper()) ImGui::TextWrapped("Strengthens and compresses the existing swell as finite-depth water slows it near shore. It does not add a separate wave pattern.");
	changed |= ImGui::SliderFloat("Shoreline wash and crest foam", &settings.BreakerStrength, 0.0f, 2.0f, "%.2f");
	if (auto tooltip = Util::HoverTooltipWrapper()) ImGui::TextWrapped("Controls the moving shallow-water foam crest and its soft retreat lace. The native terrain water edge remains pinned.");
	changed |= ImGui::SliderFloat("Whitewater strength", &settings.FoamStrength, 0.0f, 2.0f, "%.2f");
	changed |= Util::UIntCheckbox("Crest spray (experimental)", &settings.EnableSpray);
	if (auto tooltip = Util::HoverTooltipWrapper()) ImGui::TextWrapped("Emits GPU particles around strong breaking crests. Off by default in the stable shoreline mode; enable only when testing particle spray.");
	ImGui::BeginDisabled(settings.EnableSpray == 0);
	changed |= ImGui::SliderFloat("Crest spray density", &settings.SprayDensity, 0.0f, 1.5f, "%.2f");
	ImGui::EndDisabled();
	changed |= ImGui::SliderFloat("Light through wave crests", &settings.CrestTranslucency, 0.0f, 1.0f, "%.2f");
	if (auto tooltip = Util::HoverTooltipWrapper()) ImGui::TextWrapped("Adds a thin, backlit response to steep crests from the active sun or directional key light. Higher values make crest transmission easier to see; flat or thick water stays opaque.");
	ImGui::EndDisabled();
	if (changed) {
		bathymetryUploadPending = true;
		if (globals::state)
			globals::state->UpdateFeatureData(globals::state->inWorld);
	}
}

void OceanDynamics::LoadSettings(json& value)
{
	bathymetryUploadPending = true;
	if (value.is_object()) settings = value.get<Settings>();
	settings.EnableSimulation = settings.EnableSimulation ? 1u : 0u;
	settings.EnableGeometryDisplacement = settings.EnableGeometryDisplacement ? 1u : 0u;
	settings.GeometryQuality = std::min(settings.GeometryQuality, 3u);
	settings.WindSpeed = FiniteClamp(settings.WindSpeed, 8.0f, 0.0f, 45.0f);
	settings.WindDirectionDegrees = FiniteClamp(settings.WindDirectionDegrees, 63.0f, 0.0f, 360.0f);
	settings.SwellHeight = FiniteClamp(settings.SwellHeight, 0.0f, 0.0f, 12.0f);
	settings.SwellPeriod = FiniteClamp(settings.SwellPeriod, 11.7f, 2.0f, 24.0f);
	settings.SwellDirectionDegrees = FiniteClamp(settings.SwellDirectionDegrees, 76.0f, 0.0f, 360.0f);
	settings.CrossSwellDirectionDegrees = FiniteClamp(settings.CrossSwellDirectionDegrees, 135.0f, 0.0f, 360.0f);
	settings.CrossSwellStrength = FiniteClamp(settings.CrossSwellStrength, 0.24f, 0.0f, 1.0f);
	settings.DeepRogueWaveStrength = FiniteClamp(settings.DeepRogueWaveStrength, 1.0f, 0.0f, 1.0f);
	settings.Choppiness = FiniteClamp(settings.Choppiness, 0.49f, 0.0f, 1.5f);
	settings.ShoalingStrength = FiniteClamp(settings.ShoalingStrength, 1.5f, 0.0f, 1.5f);
	settings.BreakerStrength = FiniteClamp(settings.BreakerStrength, 2.0f, 0.0f, 2.0f);
	settings.FoamStrength = FiniteClamp(settings.FoamStrength, 0.80f, 0.0f, 2.0f);
	settings.FoamLifetime = FiniteClamp(settings.FoamLifetime, 5.5f, 0.5f, 20.0f);
	settings.CrestTranslucency = FiniteClamp(settings.CrestTranslucency, 1.0f, 0.0f, 1.0f);
	settings.EnableSpray = settings.EnableSpray ? 1u : 0u;
	settings.EnableBreakingWaves = settings.EnableBreakingWaves ? 1u : 0u;
	settings.SprayDensity = FiniteClamp(settings.SprayDensity, 1.5f, 0.0f, 1.5f);
}

void OceanDynamics::SaveSettings(json& value) { value = settings; }
void OceanDynamics::RestoreDefaultSettings()
{
	settings = {};
	bathymetryUploadPending = true;
}
