// PIXL Renderer - Foliage Optimizer
// Derived from Community Shaders 1.9.1 Grass Optimizations and substantially adapted for PIXL Renderer.
// Upstream and contributor copyrights remain with their respective authors.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "FoliageOptimizer.h"

#include "Renderer/HookRegistry.h"
#include "../FoliageDynamics.h"
#include "Menu/TuningWorkspaceRenderer.h"
#include "Renderer/VisibilityContext.h"
#include "State.h"

#define I18N_KEY_PREFIX "feature.foliage_optimizer."

namespace
{
	// Director/Photo camera ownership can move the active NiCamera without
	// changing the character-facing world-root camera immediately. Resolve the
	// same camera tree used by CameraSuite for capture focus and retain the
	// world-root camera as a safe gameplay fallback.
	RE::NiCamera* FindActiveCaptureCamera(RE::NiAVObject* object)
	{
		if (!object)
			return nullptr;
		if (auto* camera = netimmerse_cast<RE::NiCamera*>(object))
			return camera;
		const auto node = object->AsNode();
		if (!node)
			return nullptr;
		for (const auto& child : node->GetChildren()) {
			if (auto* camera = FindActiveCaptureCamera(child.get()))
				return camera;
		}
		return nullptr;
	}
}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	FoliageOptimizer::Settings,
	MinPixelSize,
	FullDetailPixelSize,
	MinDensity,
	MeshCostBias,
	CostBiasStartDistance,
	InvisibleFadeCull,
	RenderDistanceOverride,
	EdgeFadeStart,
	EnableOcclusionCulling,
	OcclusionBias,
	SimpleShadingPixelSize,
	CollisionDistance,
	EnableMeshLOD,
	EnableMidLOD,
	MidLODPixelSize,
	EnableFarLOD,
	FarLODPixelSize,
	MeshLODBandPixels,
	EnableDirectorBoost,
	PhotoDensityScale,
	PhotoRangeScale,
	PhotoFadeScale,
	VideoDensityScale,
	VideoRangeScale,
	VideoFadeScale,
	FrustumGuardBand,
	DirectorFrustumGuardBand)

void FoliageOptimizer::LoadSettings(json& o_json)
{
	settings = o_json;
}

void FoliageOptimizer::SaveSettings(json& o_json)
{
	o_json = settings;
}

void FoliageOptimizer::RestoreDefaultSettings()
{
	settings = {};
}

void FoliageOptimizer::DrawSettings()
{
	ImGui::SeparatorText(T(TKEY("culling"), "Culling & LOD"));

	ImGui::SliderFloat(T(TKEY("full_detail_pixel_size"), "Full-Detail Pixel Size"), &settings.FullDetailPixelSize, 4.0f, 128.0f, "%.1f px");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("full_detail_pixel_size_tooltip"),
							  "Instances whose on-screen radius is above this render at full density. Below it, density is increasingly thinned down to Minimum Density at Min Pixel Size. Increasing this setting improves performance by removing closer grass."));
	}

	ImGui::SliderFloat(T(TKEY("min_pixel_size"), "Min Pixel Size"), &settings.MinPixelSize, 1.0f, 32.0f, "%.1f px");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("min_pixel_size_tooltip"),
							  "Individual grass instances that visibly take up less space on the screen than this are dropped entirely. Higher values improve performance by removing far-away grass instances sooner."));
	}

	Util::PercentageSlider(T(TKEY("min_density"), "Minimum Density"), &settings.MinDensity);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("min_density_tooltip"),
							  "The percentage of grass that remains at the smallest (Min Pixel Size) LOD level before culling."));
	}

	Util::PercentageSlider(T(TKEY("mesh_cost_bias"), "Mesh Cost Bias"), &settings.MeshCostBias);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("mesh_cost_bias_tooltip"),
							  "Culls or removes grass meshes based on their complexity (performance impact). At 0, removal is identical between all grass types regardless of complexity. At 1, heavier and more complex meshes are culled 2-6x sooner than simple ones. Only applies beyond the Cost Bias Start Distance, so nearby grass is never thinned."));
	}

	ImGui::SliderFloat(T(TKEY("cost_bias_start_distance"), "Cost Bias Start Distance"), &settings.CostBiasStartDistance, 0.0f, 20000.0f, "%.0f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		std::vector<std::string> tooltipLines = {
			T(TKEY("cost_bias_start_distance_tooltip"),
				"Distance at which Mesh Cost Bias starts taking effect, ramping to full over the same distance again. Nearer than this, all grass types are treated identically no matter how complex. Zero applies the bias everywhere, including right in front of the player."),
			Util::Units::FormatDistance(settings.CostBiasStartDistance)
		};
		Util::DrawMultiLineTooltip(tooltipLines);
	}

	ImGui::SliderFloat(T(TKEY("render_distance_override"), "Grass Render Distance"), &settings.RenderDistanceOverride, 0.0f, 100000.0f, "%.0f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("render_distance_override_tooltip"),
							  "Max grass render distance in units. 0 = use the game's INI cap (fGrassStartFadeDistance + fGrassFadeRange). Any grass beyond the vanilla range or this range will be removed."));
	}

	Util::PercentageSlider(T(TKEY("edge_fade_start"), "Edge Fade Start"), &settings.EdgeFadeStart);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		std::vector<std::string> tooltipLines = {
			T(TKEY("edge_fade_start_tooltip"),
				"Percent of the grass render distance at which grass starts fading out. The default of 0.85 fades over the last 15%. A lower value results in a longer, smoother fade out, while a value of 1.0 disables the fade and grass pops out at the render distance."),
			Util::Units::FormatDistance(maxGrassDistance * settings.EdgeFadeStart)
		};
		Util::DrawMultiLineTooltip(tooltipLines);
	}

	ImGui::SliderFloat(T(TKEY("invisible_fade_cull"), "Invisible Fade Cull"), &settings.InvisibleFadeCull, 0.0f, 0.5f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("invisible_fade_cull_tooltip"),
							  "Skip drawing grass whose transparency is below this threshold. Grass with a fade value of zero is completely invisible and thus is removed early for performance reasons."));
	}

	ImGui::Checkbox(T(TKEY("occlusion_culling"), "Occlusion Culling"), &settings.EnableOcclusionCulling);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("occlusion_culling_tooltip"),
							  "Skips grass hidden behind rocks, buildings and NPCs. Depending on how much grass is not visible, this may cost more than its benefits. If you see grass flickering when moving, try disabling this. Terrain such as hills is not treated as an occluder when Terrain Blending is enabled, which reduces the benefit."));
	}

	ImGui::SliderFloat(T(TKEY("occlusion_bias"), "Occlusion Bias"), &settings.OcclusionBias, 0.0f, 0.05f, "%.4f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("occlusion_bias_tooltip"),
							  "How far behind an occluder grass must sit before Occlusion Culling removes it. Raise this if grass disappears around the edges of rocks and hills, lower it to reclaim more performance. Has no effect unless Occlusion Culling is enabled."));
	}

	ImGui::SliderFloat(T(TKEY("simple_shading_px"), "Simple Shading Below"), &settings.SimpleShadingPixelSize, 0.0f, 32.0f, "%.1f px");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("simple_shading_px_tooltip"),
							  "Grass instances smaller than this size on screen will skip barely visible detail including contact shadows, specular highlights, and other complex grass visual elements. Zero disables this feature."));
	}

	ImGui::SliderFloat(T(TKEY("collision_distance"), "Collision Distance"), &settings.CollisionDistance, 0.0f, 8192.0f, "%.0f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		std::vector<std::string> tooltipLines = {
			T(TKEY("collision_distance_tooltip"),
				"Grass beyond this distance skips any collision detection. Zero disables collision on all grass. Requires the Grass Collision feature."),
			Util::Units::FormatDistance(settings.CollisionDistance)
		};
		Util::DrawMultiLineTooltip(tooltipLines);
	}

	ImGui::SeparatorText(T(TKEY("mesh_lod"), "Mesh LOD"));

	ImGui::Checkbox(T(TKEY("enable_mesh_lod"), "Enable Mesh LOD"), &settings.EnableMeshLOD);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("enable_mesh_lod_tooltip"),
							  "Improves performance by swapping distant grass instances for a simpler LOD mesh, in two bands. Requires an LOD .nif per grass type at meshes\\LOD\\Grass\\<source-mesh-name>_LOD0.nif, plus an optional _LOD1.nif for the far band. Grass with no LOD mesh keeps its full mesh."));
	}

	ImGui::BeginDisabled(!settings.EnableMeshLOD);

	ImGui::Checkbox(T(TKEY("enable_mid_lod"), "Enable Middle LOD"), &settings.EnableMidLOD);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("enable_mid_lod_tooltip"),
							  "Swaps mid-distance grass to the _LOD0.nif mesh. With this off, grass stays on its full mesh until the far band takes over."));
	}

	ImGui::SliderFloat(T(TKEY("mid_lod_pixel_size"), "Middle LOD Pixel Size"), &settings.MidLODPixelSize, 1.0f, 64.0f, "%.1f px");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("mid_lod_pixel_size_tooltip"),
							  "Instances whose on-screen radius is below this but above the Far LOD Pixel Size swap to the _LOD0.nif mesh."));
	}

	ImGui::Checkbox(T(TKEY("enable_far_lod"), "Enable Far LOD"), &settings.EnableFarLOD);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("enable_far_lod_tooltip"),
							  "Swaps the most distant grass to the _LOD1.nif mesh. Grass types without that file reuse their _LOD0.nif, so the far band still gets its own brightness."));
	}

	ImGui::SliderFloat(T(TKEY("far_lod_pixel_size"), "Far LOD Pixel Size"), &settings.FarLODPixelSize, 1.0f, 64.0f, "%.1f px");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("far_lod_pixel_size_tooltip"),
							  "Instances whose on-screen radius is below this but above Min Pixel Size swap to the _LOD1.nif mesh. Values above the Middle LOD Pixel Size are clamped to it, since the far band is always the more distant of the two."));
	}

	ImGui::SliderFloat(T(TKEY("mesh_lod_band"), "Mesh LOD Transition Band"), &settings.MeshLODBandPixels, 0.0f, 16.0f, "%.1f px");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("mesh_lod_band_tooltip"),
							  "The range of on-screen sizes over which a random amount of meshes are swapped out before completely transitioning to the next LOD. Applies to both transitions. A wider range results in a smoother transition."));
	}

	ImGui::EndDisabled();

	ImGui::SeparatorText("Director / Capture");
	ImGui::Checkbox("Temporary Director Boost", &settings.EnableDirectorBoost);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextWrapped("Temporarily increases retained density, range and edge-fade coverage in Photo and Video Mode. Saved gameplay values are never overwritten.");
	}
	ImGui::BeginDisabled(!settings.EnableDirectorBoost);
	ImGui::SliderFloat("Photo Density", &settings.PhotoDensityScale, 1.0f, 2.0f, "%.2fx");
	ImGui::SliderFloat("Photo Range", &settings.PhotoRangeScale, 1.0f, 2.0f, "%.2fx");
	ImGui::SliderFloat("Photo Fade Coverage", &settings.PhotoFadeScale, 1.0f, 2.0f, "%.2fx");
	ImGui::SliderFloat("Video Density", &settings.VideoDensityScale, 1.0f, 1.5f, "%.2fx");
	ImGui::SliderFloat("Video Range", &settings.VideoRangeScale, 1.0f, 1.5f, "%.2fx");
	ImGui::SliderFloat("Video Fade Coverage", &settings.VideoFadeScale, 1.0f, 1.5f, "%.2fx");
	ImGui::EndDisabled();

	if (ImGui::CollapsingHeader("Advanced Stability")) {
		ImGui::SliderFloat("Frustum Guard Band", &settings.FrustumGuardBand, 32.0f, 512.0f, "%.0f units");
		ImGui::SliderFloat("Director Guard Band", &settings.DirectorFrustumGuardBand, 64.0f, 1024.0f, "%.0f units");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextWrapped("Keeps wind-bent cards just outside the geometric frustum alive, preventing edge popping during camera paths and wide-aperture captures.");
		}
	}
}

void FoliageOptimizer::PostPostLoad()
{
	logger::info("[PIXL] Foliage Optimizer waiting for graphics resource validation");
}

bool FoliageOptimizer::HasShaderDefine(RE::BSShader::Type shaderType)
{
	switch (shaderType) {
	case RE::BSShader::Type::Grass:
		return true;
	default:
		return false;
	}
}

void FoliageOptimizer::ComputeFrustumPlanes(RE::NiFrustumPlanes& out, const RE::NiFrustum& viewFrustum, const RE::NiTransform& transform)
{
	const __m128 fwd = _mm_set_ps(0.0f, transform.rotate.entry[2][0], transform.rotate.entry[1][0], transform.rotate.entry[0][0]);
	const __m128 col1 = _mm_set_ps(0.0f, transform.rotate.entry[2][1], transform.rotate.entry[1][1], transform.rotate.entry[0][1]);
	const __m128 col2 = _mm_set_ps(0.0f, transform.rotate.entry[2][2], transform.rotate.entry[1][2], transform.rotate.entry[0][2]);
	const __m128 trans = _mm_set_ps(0.0f, transform.translate.z, transform.translate.y, transform.translate.x);

	const __m128 nearPt = _mm_add_ps(trans, _mm_mul_ps(fwd, _mm_set1_ps(viewFrustum.fNear)));
	const __m128 farPt = _mm_add_ps(trans, _mm_mul_ps(fwd, _mm_set1_ps(viewFrustum.fFar)));

	auto MakePlane = [&](int idx, __m128 normal, __m128 point) {
		alignas(16) float n[4];
		_mm_store_ps(n, normal);
		out.cullingPlanes[idx].normal = { n[0], n[1], n[2] };
		out.cullingPlanes[idx].constant = _mm_cvtss_f32(_mm_dp_ps(normal, point, 0x71));
	};

	MakePlane(0, fwd, nearPt);
	const __m128 negFwd = _mm_xor_ps(fwd, _mm_set1_ps(-0.0f));
	MakePlane(1, negFwd, farPt);

	if (viewFrustum.bOrtho) {
		__m128 leftVec = col2;
		MakePlane(2, leftVec, _mm_add_ps(trans, _mm_mul_ps(leftVec, _mm_set1_ps(viewFrustum.fLeft))));
		__m128 rightVec = _mm_xor_ps(col2, _mm_set1_ps(-0.0f));
		MakePlane(3, rightVec, _mm_add_ps(trans, _mm_mul_ps(rightVec, _mm_set1_ps(viewFrustum.fRight))));
		__m128 upVec = col1;
		MakePlane(4, upVec, _mm_add_ps(trans, _mm_mul_ps(upVec, _mm_set1_ps(viewFrustum.fTop))));
		__m128 botVec = _mm_xor_ps(col1, _mm_set1_ps(-0.0f));
		MakePlane(5, botVec, _mm_add_ps(trans, _mm_mul_ps(botVec, _mm_set1_ps(viewFrustum.fBottom))));
	} else {
		// SetFrustrumPlanes: s = 1/sqrt(slopeÂ²+1); n = fwd*(Â±slope*s) + axis*(Â±s)
		auto sidePlane = [&](int idx, __m128 axis, float slope, float fwdSign, float axisSign) {
			const float s = 1.0f / std::sqrt(slope * slope + 1.0f);
			__m128 n = _mm_add_ps(
				_mm_mul_ps(fwd, _mm_set1_ps(fwdSign * slope * s)),
				_mm_mul_ps(axis, _mm_set1_ps(axisSign * s)));
			MakePlane(idx, n, trans);
		};

		sidePlane(2, col2, viewFrustum.fLeft, -1.0f, +1.0f);
		sidePlane(3, col2, viewFrustum.fRight, +1.0f, -1.0f);
		sidePlane(4, col1, viewFrustum.fTop, +1.0f, -1.0f);
		sidePlane(5, col1, viewFrustum.fBottom, -1.0f, +1.0f);
	}

	out.activePlanes = RE::NiFrustumPlanes::ActivePlane(0x3F);

	const float edgePadding = std::clamp(runtimeFrustumGuardBand, 0.0f, 2048.0f);
	out.cullingPlanes[2].constant -= edgePadding;
	out.cullingPlanes[3].constant -= edgePadding;
	out.cullingPlanes[4].constant -= edgePadding;
	out.cullingPlanes[5].constant -= edgePadding;
}

void FoliageOptimizer::UpdateGrass()
{
	auto& visibility = PIXL::Renderer::VisibilityContext::Get();
	std::scoped_lock blk(bucketStore.bucketMutex);
	auto* device = globals::d3d::device;
	auto* ctx = globals::d3d::context;

	if (!GetCullCS() || !ctx1 || !cullParamsCB) {
		// Without a cull dispatch, indirect args are invalid. Return to Skyrim's normal
		// grass path until an explicit shader-cache reset retries initialization.
		runtimeReady = false;
		for (auto& [key, b] : bucketStore.buckets)
			b.ResetCullState();
		bucketStore.DiscardPending();
		return;
	}

	// Get vanilla wind timer values
	timeAccum += globals::game::smState->timerValues[1];
	prevTimeBase = timeBase;
	timeBase = globals::game::smState->timerValues[4] * 0.0016666667f * 6.2831802f;

	const auto iniFloat = [](const char* name, float fallback) {
		auto* setting = RE::GetINISetting(name);
		return setting ? setting->GetFloat() : fallback;
	};
	if (fadeInTimeRcp == 0.0f) {
		const float t = iniFloat("fGrassFadeInTime:Grass", 0.0f);
		fadeInTimeRcp = t > 0.0f ? 1.0f / t : 1e6f;
	}
	if (vanillaMaxDistance == 0.0f) {
		grassStartFadeDistance = iniFloat("fGrassStartFadeDistance:Grass", 6000.0f);
		vanillaMaxDistance = grassStartFadeDistance + iniFloat("fGrassFadeRange:Grass", 2000.0f);
	}

	const bool directorVideo = settings.EnableDirectorBoost && TuningWorkspaceRenderer::IsDirectorVideoModeActive();
	const bool directorPhoto = settings.EnableDirectorBoost && !directorVideo && TuningWorkspaceRenderer::IsDirectorPhotoModeActive();
	const float densityScale = directorVideo ? settings.VideoDensityScale : (directorPhoto ? settings.PhotoDensityScale : 1.0f);
	const float rangeScale = directorVideo ? settings.VideoRangeScale : (directorPhoto ? settings.PhotoRangeScale : 1.0f);
	const float fadeScale = directorVideo ? settings.VideoFadeScale : (directorPhoto ? settings.PhotoFadeScale : 1.0f);
	runtimeFrustumGuardBand = (directorVideo || directorPhoto) ? settings.DirectorFrustumGuardBand : settings.FrustumGuardBand;

	maxGrassDistance = (settings.RenderDistanceOverride > 0.0f ? settings.RenderDistanceOverride : vanillaMaxDistance) *
		std::clamp(rangeScale, 1.0f, 2.0f);
	maxDistSq = maxGrassDistance * maxGrassDistance;

	bucketStore.BeginFrame({ settings.EnableMeshLOD, settings.EnableMidLOD, settings.EnableFarLOD, timeAccum });

	// Settings should have immediate VRAM semantics: disabled LOD tiers release their
	// scratch across all buckets, not only when a bucket happens to become visible again.
	for (auto& [key, bucket] : bucketStore.buckets) {
		if (!settings.EnableMeshLOD || !settings.EnableMidLOD)
			bucket.lodBins[(size_t)GrassMeshLibrary::LODTier::kMiddle].Release();
		if (!settings.EnableMeshLOD || !settings.EnableFarLOD)
			bucket.lodBins[(size_t)GrassMeshLibrary::LODTier::kFar].Release();
	}

	globals::profiler->BeginPass("FoliageOptimizer::ApplyPending");
	bucketStore.RefreshComplexGrass(globals::pipeline::foliageDynamics.settings.ComplexGrassThreshold, ctx);
	bucketStore.ApplyPending(device, ctx);
	globals::profiler->EndPass();

	RE::NiCamera* cam = RE::Main::WorldRootCamera();
	if (directorVideo || directorPhoto) {
		if (auto* playerCamera = RE::PlayerCamera::GetSingleton()) {
			if (auto* captureCamera = FindActiveCaptureCamera(playerCamera->cameraRoot.get()))
				cam = captureCamera;
		}
	}
	if (!cam) {
		// Leaving last frame's flags up would let the draw path re-issue its indirect draws.
		for (auto& [key, b] : bucketStore.buckets)
			b.ResetCullState();
		return;
	}

	// A capture camera is a separate NiCamera in CameraSuite. If ownership has
	// changed, or the camera has teleported, the previous frame's occlusion
	// result is spatially unrelated and can hide otherwise visible grass. Give
	// frustum culling two settling frames while the new depth/Hi-Z view catches
	// up. The guard band still preserves edge vegetation during normal motion.
	const bool directorCamera = directorVideo || directorPhoto;
	const auto delta = cam->world.translate - lastCullCameraPosition;
	const float cameraJumpSq = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
	const bool cameraChanged = !haveCullCameraState ||
		lastCullCamera != cam ||
		lastCullWasDirector != directorCamera ||
		cameraJumpSq > (768.0f * 768.0f);
	if (cameraChanged) {
		lastCullCamera = cam;
		lastCullCameraPosition = cam->world.translate;
		lastCullWasDirector = directorCamera;
		haveCullCameraState = true;
		cameraTransitionFrames = 2;
		visibility.Invalidate("foliage camera ownership changed");
	}
	// Track the previous frame rather than the last jump anchor. Otherwise normal travel
	// eventually accumulates past the teleport threshold and spuriously disables Hi-Z.
	lastCullCameraPosition = cam->world.translate;

	RE::NiFrustumPlanes frustum{};
	ComputeFrustumPlanes(frustum, cam->GetRuntimeData2().viewFrustum, cam->world);
	const RE::NiPoint3 camPos = cam->world.translate;
	const __m128 camPosV = _mm_setr_ps(camPos.x, camPos.y, camPos.z, 0.0f);
	FrustumSoA frustumSoA;
	BuildFrustumSoA(frustumSoA, frustum);

	const bool allowHiZ = settings.EnableOcclusionCulling && cameraTransitionFrames == 0;
	if (allowHiZ)
		visibility.Build(device, ctx, globals::state ? globals::state->frameCount : 0u);
	else
		visibility.Invalidate("foliage occlusion temporarily disabled");
	if (cameraTransitionFrames > 0)
		--cameraTransitionFrames;

	{
		CullParamsCB cp{};
		for (int i = 0; i < 6; ++i) {
			cp.frustumPlanes[i][0] = frustum.cullingPlanes[i].normal.x;
			cp.frustumPlanes[i][1] = frustum.cullingPlanes[i].normal.y;
			cp.frustumPlanes[i][2] = frustum.cullingPlanes[i].normal.z;
			cp.frustumPlanes[i][3] = frustum.cullingPlanes[i].constant;
		}

		cp.minPixelSize = settings.MinPixelSize;
		cp.fullDetailPixelSize = settings.FullDetailPixelSize;
		cp.lodMinKeep = std::clamp(settings.MinDensity * densityScale, 0.0f, 1.0f);
		cp.lodFadeBand = 0.15f;

		const auto& vf = cam->GetRuntimeData2().viewFrustum;
		const auto [screenW, screenH] = globals::game::renderer->GetScreenSize();
		cp.meshCostBias = settings.MeshCostBias;
		cp.projScale = screenH / (2.0f * std::abs(vf.fTop));
		cp.maxDistSq = maxDistSq;
		const float fadeCoverage = (1.0f - std::clamp(settings.EdgeFadeStart, 0.0f, 1.0f)) *
			std::clamp(fadeScale, 1.0f, 2.0f);
		cp.edgeFadeStart = std::clamp(1.0f - fadeCoverage, 0.0f, 1.0f);

		cp.alphaParam1 = std::min(grassStartFadeDistance, maxGrassDistance);
		cp.alphaParam2 = std::max(maxGrassDistance - cp.alphaParam1, 1.0f);
		cp.fadeNow = timeAccum;
		cp.fadeInTimeRcp = fadeInTimeRcp;

		const float collisionDist = std::max(0.0f, settings.CollisionDistance);
		cp.invisibleFadeCull = settings.InvisibleFadeCull;
		cp.simpleShadingPixelSize = std::max(0.0f, settings.SimpleShadingPixelSize);
		cp.collisionDistSq = collisionDist * collisionDist;
		cp.midLODPixelSize = settings.MidLODPixelSize;
		cp.farLODPixelSize = settings.EnableMidLOD ? std::min(settings.FarLODPixelSize, settings.MidLODPixelSize) : settings.FarLODPixelSize;

		cp.meshLODBandPx = std::max(0.0f, settings.MeshLODBandPixels);
		cp.hiZEnabled = visibility.IsValid() ? 1.0f : 0.0f;
		cp.hiZSizeX = (float)visibility.GetWidth();
		cp.hiZSizeY = (float)visibility.GetHeight();

		cp.hiZTexelPixels = visibility.GetTexelPixels();
		cp.hiZMipCount = (float)visibility.GetMipCount();
		cp.occlusionBias = std::max(0.0f, settings.OcclusionBias);
		cp.costBiasStartDist = std::max(0.0f, settings.CostBiasStartDistance);

		cullParamsCB->Update(cp);
	}

	uint32_t visibleBuckets = 0;
	sliceTableCPU.clear();

	// Measures the CPU time spent frustum culling bucket slices
	globals::profiler->BeginPass("FoliageOptimizer::SliceCull");
	for (auto& [key, b] : bucketStore.buckets) {
		b.ResetCullState();
		if (!b.totalInstances || !b.instanceSRV)
			continue;

		if (!b.coarseValid)
			bucketStore.UpdateCoarseBounds(b);

		CullBucketSlices(b, frustumSoA, camPosV);

		if (b.cullState != GrassBucket::CullState::Candidate)
			continue;

		for (uint32_t tier = 0; tier < (uint32_t)GrassMeshLibrary::LODTier::kCount; ++tier)
			b.lodBins[tier].active = bucketStore.EnsureLODBin(b, (GrassMeshLibrary::LODTier)tier, device);
		++visibleBuckets;
	}
	globals::profiler->EndPass();

	// Measures the slice table upload, the per-bucket constants, and the cull dispatch per visible bucket.
	globals::profiler->BeginPass("FoliageOptimizer::InstanceCull");
	UploadCullState(device, ctx, visibleBuckets);
	globals::profiler->EndPass();
}

void FoliageOptimizer::MergeSlicesIntoRuns(GrassBucket& b)
{
	const auto cellOf = [](const RE::NiPoint3& origin) {
		constexpr float kCellSize = 4096.0f;
		const auto cellX = (int32_t)std::floor(origin.x / kCellSize);
		const auto cellY = (int32_t)std::floor(origin.y / kCellSize);
		return ((uint64_t)(uint32_t)cellX << 32) | (uint32_t)cellY;
	};

	const auto continuesRun = [&cellOf](const BucketSlice& slice, const GrassBucket::SliceRun& run, uint64_t cell) {
		return slice.bufferOffset != UINT32_MAX && slice.count != 0 &&
		       slice.bufferOffset == run.firstSliceOffset + run.instanceCount &&
		       cellOf(slice.origin) == cell;
	};

	b.sliceRuns.clear();
	const uint32_t sliceCount = (uint32_t)b.slices.size();

	for (uint32_t first = 0; first < sliceCount;) {
		if (b.slices[first].bufferOffset == UINT32_MAX || b.slices[first].count == 0)
		{
			++first;
			continue;
		}

		GrassBucket::SliceRun run;
		run.firstSliceOffset = b.slices[first].bufferOffset;
		run.instanceCount = b.slices[first].count;
		__m128 lo = _mm_load_ps(b.sliceBounds[first].lo);
		__m128 hi = _mm_load_ps(b.sliceBounds[first].hi);
		const uint64_t cell = cellOf(b.slices[first].origin);

		uint32_t next = first + 1;
		for (; next < sliceCount && continuesRun(b.slices[next], run, cell); ++next) {
			lo = _mm_min_ps(lo, _mm_load_ps(b.sliceBounds[next].lo));
			hi = _mm_max_ps(hi, _mm_load_ps(b.sliceBounds[next].hi));
			run.instanceCount += b.slices[next].count;
		}

		_mm_store_ps(run.bounds.lo, lo);
		_mm_store_ps(run.bounds.hi, hi);
		b.sliceRuns.push_back(run);
		first = next;
	}

	b.clustersValid = true;
}

void FoliageOptimizer::CullBucketSlices(GrassBucket& b, const FrustumSoA& frustumSoA, __m128 camPosV)
{
	b.sliceTableOffset = (uint32_t)sliceTableCPU.size();
	b.sliceTableCount = 0;
	b.visibleInstances = 0;
	b.cullState = GrassBucket::CullState::Unavailable;
	for (GrassBucket::LODBin& bin : b.lodBins)
		bin.active = false;

	if (b.sliceBounds.size() != b.slices.size())
		return;

	const __m128 bucketLo = _mm_setr_ps(b.coarseMin.x, b.coarseMin.y, b.coarseMin.z, 0.0f);
	const __m128 bucketHi = _mm_setr_ps(b.coarseMax.x, b.coarseMax.y, b.coarseMax.z, 0.0f);
	if (!AabbVisible(frustumSoA, bucketLo, bucketHi)) {
		b.cullState = GrassBucket::CullState::Invisible;
		b.queueOptimizationSafe.store(true, std::memory_order_release);
		return;
	}

	if (!b.clustersValid)
		MergeSlicesIntoRuns(b);

	const __m128 pad = _mm_set1_ps(b.modelRadius + 64.0f);
	for (const GrassBucket::SliceRun& run : b.sliceRuns) {
		const __m128 lo = _mm_sub_ps(_mm_load_ps(run.bounds.lo), pad);
		const __m128 hi = _mm_add_ps(_mm_load_ps(run.bounds.hi), pad);

		const __m128 beyond = _mm_max_ps(_mm_max_ps(_mm_sub_ps(lo, camPosV), _mm_sub_ps(camPosV, hi)), _mm_setzero_ps());
		auto distanceSq = _mm_cvtss_f32(_mm_dp_ps(beyond, beyond, 0x71));

		const bool withinRenderDistance = distanceSq <= maxDistSq;
		if (!withinRenderDistance || !AabbVisible(frustumSoA, lo, hi))
			continue;

		sliceTableCPU.emplace_back(run.firstSliceOffset, b.visibleInstances);
		++b.sliceTableCount;
		b.visibleInstances += run.instanceCount;
	}

	if (b.sliceTableCount != 0) {
		b.cullState = GrassBucket::CullState::Candidate;
	} else {
		b.cullState = GrassBucket::CullState::Invisible;
		b.queueOptimizationSafe.store(true, std::memory_order_release);
		sliceTableCPU.resize(b.sliceTableOffset);
	}
}

void FoliageOptimizer::UploadCullState(ID3D11Device* device, ID3D11DeviceContext* ctx, uint32_t visibleBuckets)
{
	// One map fills every visible bucket's slot â€” replaces a Map/Unmap per bucket.
	bool cullStateUploaded = false;
	if (visibleBuckets && EnsureCullBucketCapacity(visibleBuckets, device)) {
		D3D11_MAPPED_SUBRESOURCE m{};
		if (SUCCEEDED(ctx->Map(cullBucketCB->CB(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
			auto* bytes = static_cast<uint8_t*>(m.pData);
			uint32_t slot = 0;
			for (auto& [key, b] : bucketStore.buckets) {
				if (b.cullState != GrassBucket::CullState::Candidate)
					continue;
				b.cullSlot = slot;
				auto* cb = reinterpret_cast<CullBucketCB*>(bytes + (size_t)slot * kSlotBytes);
				cb->instanceCount = b.visibleInstances;
				cb->sliceTableOffset = b.sliceTableOffset;
				cb->sliceCount = b.sliceTableCount;
				cb->wavePeriod = b.wavePeriod;
				cb->timeBase = timeBase;
				cb->prevTimeBase = prevTimeBase;
				cb->boundCenter[0] = b.boundCenter.x;
				cb->boundCenter[1] = b.boundCenter.y;
				cb->boundCenter[2] = b.boundCenter.z;
				cb->modelRadius = b.modelRadius;
				cb->distScale = b.distScale;
				cb->minPixelScale = b.minPixelScale;
				cb->isComplex = b.isComplex ? 1.0f : 0.0f;
				cb->midLODEnabled = b.lodBins[(size_t)GrassMeshLibrary::LODTier::kMiddle].active ? 1.0f : 0.0f;
				cb->farLODEnabled = b.lodBins[(size_t)GrassMeshLibrary::LODTier::kFar].active ? 1.0f : 0.0f;
				++slot;
			}
			ctx->Unmap(cullBucketCB->CB(), 0);
			cullStateUploaded = true;
		}
	}

	// If the cull state failed to upload, skip all buckets to prevent the CS from running using garbage or out-of-date data.
	if (visibleBuckets && !cullStateUploaded) {
		for (auto& [key, b] : bucketStore.buckets)
			if (b.cullState == GrassBucket::CullState::Candidate) {
				b.cullState = GrassBucket::CullState::Unavailable;
				b.queueOptimizationSafe.store(false, std::memory_order_release);
			}
	}

	ID3D11Buffer* paramsCB = cullParamsCB->CB();
	ctx->CSSetConstantBuffers(0, 1, &paramsCB);
	ID3D11Buffer* frameBuffers[1]{ *globals::game::perFrame.get() };
	ctx->CSSetConstantBuffers(12, 1, frameBuffers);

	bool sliceTableUploaded = sliceTableCPU.empty();
	if (!sliceTableCPU.empty()) {
		if (sliceTableCPU.size() > sliceTableCapacity) {
			sliceTable.reset();
			sliceTableCapacity = 0;

			uint32_t cap = 256;
			while (cap < sliceTableCPU.size())
				cap *= 2;

			D3D11_BUFFER_DESC bd{};
			bd.ByteWidth = cap * 2 * sizeof(uint32_t);
			bd.Usage = D3D11_USAGE_DYNAMIC;
			bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			bd.StructureByteStride = 2 * sizeof(uint32_t);
			try {
				sliceTable = std::make_unique<Buffer>(bd, nullptr, "FoliageOptimizer::SliceTable");
				D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
				sv.Format = DXGI_FORMAT_UNKNOWN;
				sv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
				sv.Buffer.NumElements = cap;
				sliceTable->CreateSRV(sv);
				sliceTableCapacity = cap;
			} catch (...) {
				logger::error("[GRASS OPTIMIZATIONS] slice table create failed elements={}", cap);
				sliceTable.reset();
			}
		}

		if (sliceTable && sliceTable->srv) {
			D3D11_MAPPED_SUBRESOURCE m{};
			if (SUCCEEDED(ctx->Map(sliceTable->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
				std::memcpy(m.pData, sliceTableCPU.data(), sliceTableCPU.size() * 2 * sizeof(uint32_t));
				ctx->Unmap(sliceTable->resource.get(), 0);
				sliceTableUploaded = true;
			}
		}
	}

	if (!sliceTableUploaded) {
		for (auto& [key, b] : bucketStore.buckets)
			if (b.cullState == GrassBucket::CullState::Candidate) {
				b.cullState = GrassBucket::CullState::Unavailable;
				b.queueOptimizationSafe.store(false, std::memory_order_release);
			}
	}

	ctx->CSSetShader(cullCS, nullptr, 0);

	for (auto& [key, b] : bucketStore.buckets)
		if (b.cullState == GrassBucket::CullState::Candidate)
			CullBucket(b, ctx);

	ID3D11UnorderedAccessView* nullUAVs[4 + 2 * (size_t)GrassMeshLibrary::LODTier::kCount] = {};
	ctx->CSSetUnorderedAccessViews(0, (UINT)std::size(nullUAVs), nullUAVs, nullptr);
	ID3D11ShaderResourceView* nullSRVs[4] = {};
	ctx->CSSetShaderResources(0, 4, nullSRVs);
	ctx->CSSetShader(nullptr, nullptr, 0);
}

void FoliageOptimizer::BuildFrustumSoA(FrustumSoA& out, const RE::NiFrustumPlanes& f)
{
	static constexpr RE::NiFrustumPlanes::ActivePlane kBits[RE::NiFrustumPlanes::Planes::kTotal] = {
		RE::NiFrustumPlanes::ActivePlane::kNear, RE::NiFrustumPlanes::ActivePlane::kFar,
		RE::NiFrustumPlanes::ActivePlane::kLeft, RE::NiFrustumPlanes::ActivePlane::kRight,
		RE::NiFrustumPlanes::ActivePlane::kTop, RE::NiFrustumPlanes::ActivePlane::kBottom
	};

	// Pad unused and inactive slots with an always-pass plane (zero normal, constant -1), keeping the per-slice test branch-free.
	alignas(16) float nx[8], ny[8], nz[8], d[8];
	for (uint32_t i = 0; i < 8; ++i) {
		nx[i] = ny[i] = nz[i] = 0.0f;
		d[i] = -1.0f;
	}

	for (uint32_t i = 0; i < 6; ++i) {
		if (!f.activePlanes.any(kBits[i]))
			continue;
		const auto& pl = f.cullingPlanes[i];
		nx[i] = pl.normal.x;
		ny[i] = pl.normal.y;
		nz[i] = pl.normal.z;
		d[i] = pl.constant;
	}

	for (uint32_t g = 0; g < 2; ++g) {
		out.nx[g] = _mm_load_ps(nx + g * 4);
		out.ny[g] = _mm_load_ps(ny + g * 4);
		out.nz[g] = _mm_load_ps(nz + g * 4);
		out.d[g] = _mm_load_ps(d + g * 4);
	}
}

bool FoliageOptimizer::AabbVisible(const FrustumSoA& f, __m128 lo, __m128 hi)
{
	const __m128 lx = _mm_shuffle_ps(lo, lo, _MM_SHUFFLE(0, 0, 0, 0));
	const __m128 ly = _mm_shuffle_ps(lo, lo, _MM_SHUFFLE(1, 1, 1, 1));
	const __m128 lz = _mm_shuffle_ps(lo, lo, _MM_SHUFFLE(2, 2, 2, 2));
	const __m128 hx = _mm_shuffle_ps(hi, hi, _MM_SHUFFLE(0, 0, 0, 0));
	const __m128 hy = _mm_shuffle_ps(hi, hi, _MM_SHUFFLE(1, 1, 1, 1));
	const __m128 hz = _mm_shuffle_ps(hi, hi, _MM_SHUFFLE(2, 2, 2, 2));

	for (uint32_t g = 0; g < 2; ++g) {
		// Positive vertex: the box corner furthest along each plane normal.
		// blendv keys off the normal's sign bit.
		const __m128 px = _mm_blendv_ps(hx, lx, f.nx[g]);
		const __m128 py = _mm_blendv_ps(hy, ly, f.ny[g]);
		const __m128 pz = _mm_blendv_ps(hz, lz, f.nz[g]);

		const __m128 dot = _mm_add_ps(
			_mm_add_ps(_mm_mul_ps(f.nx[g], px), _mm_mul_ps(f.ny[g], py)),
			_mm_mul_ps(f.nz[g], pz));

		// dot(n, p) - constant < 0 â†’ outside
		if (_mm_movemask_ps(_mm_cmplt_ps(_mm_sub_ps(dot, f.d[g]), _mm_setzero_ps())))
			return false;
	}
	return true;
}

void FoliageOptimizer::SetupResources()
{
	auto& hookRegistry = PIXL::Renderer::HookRegistry::Get();
	hookRegistry.Declare({
		.name = "FoliageOptimizer.RuntimeHooks",
		.owner = "Foliage Optimizer",
		.relocation = "Grass instance lifecycle, setup, stream and indirect-draw hook set",
		.featureImpact = "GPU grass capture, culling and indirect rendering",
		.required = false });

	runtimeReady = false;
	cullParamsCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<CullParamsCB>(), "FoliageOptimizer::CullParamsCB");
	bucketStore.SetupResources();

	ctx1 = nullptr;
	if (FAILED(globals::d3d::context->QueryInterface(__uuidof(ID3D11DeviceContext1), ctx1.put_void())) || !ctx1) {
		logger::error("[GRASS OPTIMIZATIONS] ID3D11DeviceContext1 unavailable â€” feature disabled");
		ctx1 = nullptr;
	}

	runtimeReady = !!ctx1 && GetCullCS();
	if (!runtimeReady) {
		hookRegistry.SetStatus(
			"FoliageOptimizer.RuntimeHooks",
			PIXL::Renderer::HookStatus::Disabled,
			"Required DX11.1 context or culling shader is unavailable; vanilla grass retained");
		logger::error("[PIXL] Foliage Optimizer disabled: required DX11 resources are unavailable");
		return;
	}

	// Runtime hook profiles are intentionally split. AE/validated runtimes may install
	// the complete GID stream chain. SE has a reduced core profile available through
	// Hooks::Install(false), but it remains quarantined until the 1.5.97 ABI is verified
	// in-game; enabling an unverified vtable/draw hook set is not a safe fallback.
	if (REL::Module::IsSE()) {
		constexpr bool kSECoreHookProfileValidated = false;
		if (!kSECoreHookProfileValidated) {
			runtimeReady = false;
			hookRegistry.SetStatus(
				"FoliageOptimizer.RuntimeHooks",
				PIXL::Renderer::HookStatus::Unsupported,
				"SE core grass hook profile is isolated but not runtime-validated; vanilla grass retained");
			logger::warn("[PIXL] Foliage Optimizer SE hook profile remains quarantined for stability; vanilla grass retained");
			return;
		}
	}

	if (!hooksInstalled) {
		const bool installGIDHooks = !REL::Module::IsSE();
		hookRegistry.SetStatus(
			"FoliageOptimizer.RuntimeHooks",
			PIXL::Renderer::HookStatus::Validated,
			"Supported runtime selected; installing the complete grass hook set");
		Hooks::Install(installGIDHooks);
		hooksInstalled = true;
		hookRegistry.SetStatus(
			"FoliageOptimizer.RuntimeHooks",
			PIXL::Renderer::HookStatus::Installed,
			"Grass lifecycle, stream capture and indirect-draw hooks installed");
	}
}

namespace
{
	static size_t GIDGroupBytes(const PIXLGrassGroupHeader* header)
	{
		if (!header || !header->numShortsPerInstance)
			return 0;
		return static_cast<size_t>(header->groupInstanceCount) * header->numShortsPerInstance * sizeof(std::uint16_t);
	}

	thread_local PIXLGrassGroupHeader tl_lastFileGroupHeader{};
	thread_local std::vector<std::uint16_t> tl_lastFileInstanceData;
	thread_local bool tl_haveFileGroup = false;
}

void FoliageOptimizer::ClearShaderCache()
{
	auto release = [](ID3D11ComputeShader*& shader) {
		if (shader)
			shader->Release();
		shader = nullptr;
	};
	release(cullCS);
	cullCompileAttempted = false;
	PIXL::Renderer::VisibilityContext::Get().ClearShaderCache();
	bucketStore.ClearShaderCache();
}

ID3D11ComputeShader* FoliageOptimizer::GetCullCS()
{
	if (!cullCS && !cullCompileAttempted) {
		cullCompileAttempted = true;
		cullCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\FoliageOptimizer\\GrassCullingCS.hlsl", {}, "cs_5_0"));
		if (!cullCS)
			logger::error("[GRASS OPTIMIZATIONS] cull CS load failed â€” feature disabled");
	}
	return cullCS;
}

void FoliageOptimizer::CullBucket(GrassBucket& b, ID3D11DeviceContext* ctx)
{
	static_assert(
		(size_t)GrassMeshLibrary::LODTier::kMiddle == 0 &&
		(size_t)GrassMeshLibrary::LODTier::kFar == 1 &&
		(size_t)GrassMeshLibrary::LODTier::kCount == 2,
		"GrassCullingCS LOD counter offsets must match LODTier");

	if (b.cullSlot == UINT32_MAX || !b.gpuResident.load(std::memory_order_acquire) ||
		!b.argsUAV || !b.lodCounterUAV || !b.lodCounterBuf || !b.instanceSRV || !b.originSRV ||
		!b.compactedUAV || !b.extrasUAV) {
		b.cullState = GrassBucket::CullState::Unavailable;
		b.queueOptimizationSafe.store(false, std::memory_order_release);
		return;
	}

	// Clearing the args view allows the instance count to be directly reset to zero for the draw.
	const UINT zeros[4] = { 0, 0, 0, 0 };
	ctx->ClearUnorderedAccessViewUint(b.argsUAV, zeros);
	ctx->ClearUnorderedAccessViewUint(b.lodCounterUAV, zeros);

	// Main outputs, two outputs per LOD tier, then the shared LOD counter, matching u0-u7 in GrassCullingCS.
	ID3D11UnorderedAccessView* uavs[4 + 2 * (size_t)GrassMeshLibrary::LODTier::kCount] = { b.compactedUAV, b.extrasUAV, b.argsUAV };
	for (size_t tier = 0; tier < (size_t)GrassMeshLibrary::LODTier::kCount; ++tier) {
		const GrassBucket::LODBin& bin = b.lodBins[tier];
		uavs[3 + tier * 2 + 0] = bin.active ? bin.compactedUAV : nullptr;
		uavs[3 + tier * 2 + 1] = bin.active ? bin.extrasUAV : nullptr;
	}
	uavs[std::size(uavs) - 1] = b.lodCounterUAV;
	ctx->CSSetUnorderedAccessViews(0, (UINT)std::size(uavs), uavs, nullptr);

	ID3D11ShaderResourceView* sliceTableSRV = sliceTable ? sliceTable->srv.get() : nullptr;
	ID3D11ShaderResourceView* srvs[4] = { b.instanceSRV, b.originSRV,
		PIXL::Renderer::VisibilityContext::Get().GetDepthPyramidSRV(), sliceTableSRV };
	ctx->CSSetShaderResources(0, 4, srvs);

	ID3D11Buffer* bucketCB = cullBucketCB->CB();
	UINT first = b.cullSlot * 16;
	UINT num = 16;
	ctx1->CSSetConstantBuffers1(1, 1, &bucketCB, &first, &num);

	// A Candidate becomes Ready only after every resource required for the GPU cull
	// is bound and the dispatch is actually submitted. Any failure stays vanilla-safe.
	if (b.visibleInstances && b.sliceTableCount && sliceTableSRV) {
		ctx->Dispatch((b.visibleInstances + 63) / 64, 1, 1);
		b.cullState = GrassBucket::CullState::Ready;
		b.queueOptimizationSafe.store(true, std::memory_order_release);
	} else {
		b.cullState = GrassBucket::CullState::Unavailable;
		b.queueOptimizationSafe.store(false, std::memory_order_release);
	}

	// The LOD counter UAV must be unbound before its values can be copied into the draw arguments.
	ID3D11UnorderedAccessView* nullUAVs[std::size(uavs)] = {};
	ctx->CSSetUnorderedAccessViews(0, (UINT)std::size(nullUAVs), nullUAVs, nullptr);
	for (size_t tier = 0; tier < (size_t)GrassMeshLibrary::LODTier::kCount; ++tier) {
		const GrassBucket::LODBin& bin = b.lodBins[tier];
		if (!bin.active || !bin.argsBuf)
			continue;
		const UINT countOffset = (UINT)(tier * sizeof(uint32_t));
		const D3D11_BOX countBox{ countOffset, 0, 0, countOffset + sizeof(uint32_t), 1, 1 };
		ctx->CopySubresourceRegion(bin.argsBuf, 0, instanceCountOffset, 0, 0, b.lodCounterBuf, 0, &countBox);
	}
}

bool FoliageOptimizer::EnsureCullBucketCapacity(uint32_t slots, [[maybe_unused]] ID3D11Device* device)
{
	if (cullBucketCB && cullBucketCBSlots >= slots)
		return true;

	uint32_t cap = cullBucketCBSlots ? cullBucketCBSlots : 64;
	while (cap < slots)
		cap *= 2;

	cullBucketCB.reset();
	cullBucketCBSlots = 0;

	try {
		cullBucketCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc(cap * kSlotBytes), "FoliageOptimizer::CullBucketCB");
	} catch (...) {
		logger::error("[GRASS OPTIMIZATIONS] cull bucket CB create failed slots={}", cap);
		return false;
	}
	cullBucketCBSlots = cap;
	return true;
}

void FoliageOptimizer::Hooks::BSMultiStreamInstanceTriShape_dtor::thunk(RE::BSMultiStreamInstanceTriShape* shape)
{
	globals::pipeline::foliageOptimizer.bucketStore.StageRemoval(shape);
	func(shape);
}

void FoliageOptimizer::Hooks::BSMultiStreamInstanceTriShape_OnVisible::thunk(RE::BSMultiStreamInstanceTriShape* This, RE::NiCullingProcess* process, std::int32_t alphaGroupIndex)
{
	if (!globals::pipeline::foliageOptimizer.runtimeReady) {
		func(This, process, alphaGroupIndex);
		return;
	}
	auto prop = This->GetGeometryRuntimeData().shaderProperty;
	if (prop && skyrim_cast<RE::BSGrassShaderProperty*>(prop.get())) {
		auto& self = globals::pipeline::foliageOptimizer;

		// Only queue one representative shape per frame for each bucket to skip redundant setup.
		if (!self.bucketStore.ClaimQueueSlot(This, globals::game::graphicsState->frameCount))
			return;

		// Skips redundant and costly frustum checks since they are now handled by the coarse slice cull and CS.
		auto& shape = *This;
		process->AppendVirtual(shape, alphaGroupIndex);
		return;
	}

	func(This, process, alphaGroupIndex);
}

void FoliageOptimizer::Hooks::DoneAddingInstances::thunk(RE::BSMultiStreamInstanceTriShape* shape,
	RE::BSTArray<std::uint32_t>& a_instances)
{
	auto& self = globals::pipeline::foliageOptimizer;

	auto& rt = shape->GetMultiStreamTrishapeRuntimeData();
	auto prop = shape->GetGeometryRuntimeData().shaderProperty;
	if (rt.groupAlloc && prop && skyrim_cast<RE::BSGrassShaderProperty*>(prop.get())) {
		if (auto* tex = prop->GetBaseTexture()) {
			const uint64_t descVal = *reinterpret_cast<uint64_t*>(&shape->GetGeometryRuntimeData().vertexDesc);
			self.bucketStore.StageCapture(shape, rt.groupAlloc, rt.instanceCount,
				2u * rt.instanceSize, descVal, tex);
		}
	}
	func(shape, a_instances);
}

void FoliageOptimizer::Hooks::BSGrassShader_SetupGeometry::thunk(RE::BSShader* This, RE::BSRenderPass* a2, std::uint32_t flags)
{
	auto& self = globals::pipeline::foliageOptimizer;

	const auto frame = globals::game::graphicsState->frameCount;
	if (self.lastFrame != frame) {
		self.UpdateGrass();
		self.lastFrame = frame;
	}

	func(This, a2, flags);
}

std::uint32_t FoliageOptimizer::Hooks::AddGroupGIDBuffer::thunk(RE::BSMultiStreamInstanceTriShape* shape, PIXLGrassGroupHeader* header, std::uint16_t* data)
{
	globals::pipeline::foliageOptimizer.bucketStore.CaptureGIDGroup(shape, header, data, GIDGroupBytes(header));
	return func(shape, header, data);
}

std::uint32_t FoliageOptimizer::Hooks::AddQueuedGroupGIDBuffer::thunk(RE::BSMultiStreamInstanceTriShape* shape, PIXLGrassGroupHeader* header, std::uint16_t* data, RE::BSTArray<std::uint32_t>& queued)
{
	globals::pipeline::foliageOptimizer.bucketStore.CaptureGIDGroup(shape, header, data, GIDGroupBytes(header));
	return func(shape, header, data, queued);
}

void FoliageOptimizer::Hooks::ReadGroupHeaderStreamTraits::thunk(RE::BSStreamHeader* streamHeader, PIXLGrassGroupHeader* groupHeader, uint32_t size)
{
	func(streamHeader, groupHeader, size);
	std::memcpy(&tl_lastFileGroupHeader, groupHeader, std::min<uint32_t>(size, sizeof(tl_lastFileGroupHeader)));
}

void FoliageOptimizer::Hooks::ReadInstanceGroupStreamTraits::thunk(RE::BSStreamHeader* streamHeader, uint16_t* instanceData, uint32_t size)
{
	func(streamHeader, instanceData, size);
	tl_lastFileInstanceData.resize((size + sizeof(uint16_t) - 1) / sizeof(uint16_t));
	std::memcpy(tl_lastFileInstanceData.data(), instanceData, size);
	tl_haveFileGroup = true;
}

void FoliageOptimizer::Hooks::AddGroupQueuedGIDFile::thunk(RE::BSMultiStreamInstanceTriShape* shape, RE::BSStream* stream, RE::BSTArray<std::uint32_t>& queued)
{
	tl_haveFileGroup = false;
	func(shape, stream, queued);
	if (tl_haveFileGroup) {
		globals::pipeline::foliageOptimizer.bucketStore.CaptureGIDGroup(
			shape, &tl_lastFileGroupHeader, tl_lastFileInstanceData.data(), tl_lastFileInstanceData.size() * sizeof(std::uint16_t));
		tl_haveFileGroup = false;
	}
}

void FoliageOptimizer::Hooks::AddGroupGIDFile::thunk(RE::BSMultiStreamInstanceTriShape* shape, RE::BSStream* stream)
{
	tl_haveFileGroup = false;
	func(shape, stream);
	if (tl_haveFileGroup) {
		globals::pipeline::foliageOptimizer.bucketStore.CaptureGIDGroup(
			shape, &tl_lastFileGroupHeader, tl_lastFileInstanceData.data(), tl_lastFileInstanceData.size() * sizeof(std::uint16_t));
		tl_haveFileGroup = false;
	}
}

RE::BSMultiStreamInstanceTriShape* FoliageOptimizer::Hooks::LoadGrassType::thunk(RE::BGSGrassManager* grassManager, RE::GrassParam* a_param, uint32_t CellXDivided, uint32_t CellYDivided, uint64_t* typeKey, RE::BSFixedString* modelPath)
{
	auto* shape = func(grassManager, a_param, CellXDivided, CellYDivided, typeKey, modelPath);

	if (shape && modelPath)
		globals::pipeline::foliageOptimizer.bucketStore.meshLibrary.RecordModelPath(shape, modelPath->c_str());

	return shape;
}

void FoliageOptimizer::Hooks::DrawInstanceTriShape::thunk(RE::BSRenderPass* pass, RE::BSMultiStreamInstanceTriShape* geometry)
{
	auto& self = globals::pipeline::foliageOptimizer;
	auto* ctx = globals::d3d::context;
	auto drawVanilla = [&]() {
		if (ctx) {
			ID3D11ShaderResourceView* nullExtras = nullptr;
			ctx->VSSetShaderResources(2, 1, &nullExtras);
		}
		func(pass, geometry);
	};
	if (!self.runtimeReady || !ctx) {
		drawVanilla();
		return;
	}

	auto shaderProperty = geometry->GetGeometryRuntimeData().shaderProperty;
	auto* grassProperty = shaderProperty ? skyrim_cast<RE::BSGrassShaderProperty*>(shaderProperty.get()) : nullptr;
	if (!grassProperty) {
		drawVanilla();
		return;
	}
	RE::NiSourceTexture* diffuseTexture = shaderProperty->GetBaseTexture();
	if (!diffuseTexture) {
		drawVanilla();
		return;
	}

	const uint64_t descVal = *reinterpret_cast<uint64_t*>(&geometry->GetGeometryRuntimeData().vertexDesc);
	const uint32_t frame = globals::game::graphicsState->frameCount;

	uint32_t descriptor = 0;
	if (globals::game::currentPixelShader && *globals::game::currentPixelShader)
		descriptor = (*globals::game::currentPixelShader)->id;
	const uint64_t passKey = (static_cast<uint64_t>(pass->passEnum) << 32) | descriptor;

	GrassBucket* b = nullptr;
	{
		std::scoped_lock lk(self.bucketStore.bucketMutex);

		const uint32_t meshId = self.bucketStore.meshLibrary.ResolveMeshId(geometry);
		const uint32_t triCount = meshId ? 0u : (uint32_t)geometry->GetTrishapeRuntimeData().triangleCount;
		auto* material = grassProperty->material;
		auto it = self.bucketStore.buckets.find({ meshId, material, meshId ? nullptr : diffuseTexture, triCount, meshId ? 0u : descVal });
		if (it != self.bucketStore.buckets.end() && it->second.totalInstances && it->second.gpuResident.load(std::memory_order_acquire))
			b = &it->second;
		if (b && b->drawnFrame == frame && b->drawnPassKey == passKey)
			return;
	}
	if (!b) {
		drawVanilla();
		return;
	}

	if (b->cullState == GrassBucket::CullState::Invisible)
		return;
	if (b->cullState != GrassBucket::CullState::Ready) {
		drawVanilla();
		return;
	}

	auto fallbackBucket = [&]() {
		b->cullState = GrassBucket::CullState::Unavailable;
		b->queueOptimizationSafe.store(false, std::memory_order_release);
		drawVanilla();
	};

	auto* rendererData = geometry->GetGeometryRuntimeData().rendererData;
	if (!rendererData) {
		fallbackBucket();
		return;
	}
	auto* meshVB = reinterpret_cast<ID3D11Buffer*>(rendererData->vertexBuffer);
	auto* indexB = reinterpret_cast<ID3D11Buffer*>(rendererData->indexBuffer);
	if (!meshVB || !indexB || !b->compactedBuf || !b->extrasSRV || !b->argsBuf) {
		fallbackBucket();
		return;
	}

	UINT mainStride = VertexStrideFromDesc(descVal);
	if (!mainStride) {
		fallbackBucket();
		return;
	}

	std::array<const GrassMeshLibrary::LODMesh*, (size_t)GrassMeshLibrary::LODTier::kCount> lodMeshes{};
	bool lodPreflightFailed = false;
	{
		std::scoped_lock lk(self.bucketStore.bucketMutex);
		for (uint32_t tier = 0; tier < (uint32_t)GrassMeshLibrary::LODTier::kCount; ++tier) {
			GrassBucket::LODBin& bin = b->lodBins[tier];
			if (!bin.active)
				continue;
			lodMeshes[tier] = self.bucketStore.meshLibrary.GetLODMesh(b->meshId, (GrassMeshLibrary::LODTier)tier);
			const auto* lod = lodMeshes[tier];
			if (!lod || !lod->vertexBuffer || !lod->indexBuffer || !lod->meshStride ||
				!bin.compactedBuf || !bin.extrasSRV || !bin.argsBuf) {
				lodPreflightFailed = true;
				break;
			}
		}
	}
	if (lodPreflightFailed) {
		fallbackBucket();
		return;
	}

	// Claim the bucket/pass only after the optimized path is known to be drawable.
	// If validation fails above, every source shape retains its normal vanilla draw.
	{
		std::scoped_lock lk(self.bucketStore.bucketMutex);
		if (b->drawnFrame == frame && b->drawnPassKey == passKey)
			return;
		b->drawnFrame = frame;
		b->drawnPassKey = passKey;
	}

	if (!b->argsIndexCountWritten) {
		const uint32_t indexCount = 3u * geometry->GetTrishapeRuntimeData().triangleCount;
		const D3D11_BOX argBox{ argsByteOffset, 0, 0, argsByteOffset + sizeof(uint32_t), 1, 1 };
		ctx->UpdateSubresource(b->argsBuf, 0, &argBox, &indexCount, 0, 0);
		b->argsIndexCountWritten = true;
	}

	// Replicate vanilla state setup
	auto& shadowState = globals::game::shadowState->GetRuntimeData();
	if (shadowState.vertexDesc != descVal) {
		shadowState.vertexDesc = descVal;
		shadowState.stateUpdateFlags.set(RE::BSGraphics::ShaderFlags::DIRTY_VERTEX_DESC);
	}
	if (shadowState.topology != D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST) {
		shadowState.topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		shadowState.stateUpdateFlags.set(RE::BSGraphics::ShaderFlags::DIRTY_PRIMITIVE_TOPO);
	}
	static REL::Relocation<void (*)(uint32_t)> SetDirtyStates{ REL::RelocationID(75580, 77386) };
	SetDirtyStates(0);

	ctx->IASetIndexBuffer(indexB, DXGI_FORMAT_R16_UINT, 0);

	ID3D11Buffer* vbs[2] = { meshVB, nullptr };
	UINT strides[2] = { mainStride, kGrassStride };
	UINT offsets[2] = { 0, 0 };

	vbs[1] = b->compactedBuf;
	ctx->IASetVertexBuffers(0, 2, vbs, strides, offsets);
	ctx->VSSetShaderResources(2, 1, &b->extrasSRV);
	ctx->DrawIndexedInstancedIndirect(b->argsBuf, argsByteOffset);

	for (uint32_t tier = 0; tier < (uint32_t)GrassMeshLibrary::LODTier::kCount; ++tier) {
		GrassBucket::LODBin& bin = b->lodBins[tier];
		if (!bin.active)
			continue;

		const GrassMeshLibrary::LODMesh* lod = lodMeshes[tier];
		if (!lod || !lod->vertexBuffer || !lod->indexBuffer)
			continue;

		if (!bin.argsIndexCountWritten) {
			const D3D11_BOX argBox{ argsByteOffset, 0, 0, argsByteOffset + sizeof(uint32_t), 1, 1 };
			ctx->UpdateSubresource(bin.argsBuf, 0, &argBox, &lod->indexCount, 0, 0);
			bin.argsIndexCountWritten = true;
		}

		if (shadowState.vertexDesc != lod->descVal) {
			shadowState.vertexDesc = lod->descVal;
			shadowState.stateUpdateFlags.set(RE::BSGraphics::ShaderFlags::DIRTY_VERTEX_DESC);
			SetDirtyStates(0);
		}

		ctx->IASetIndexBuffer(lod->indexBuffer, DXGI_FORMAT_R16_UINT, 0);

		vbs[0] = lod->vertexBuffer;
		vbs[1] = bin.compactedBuf;
		strides[0] = lod->meshStride;
		ctx->IASetVertexBuffers(0, 2, vbs, strides, offsets);
		ctx->VSSetShaderResources(2, 1, &bin.extrasSRV);
		ctx->DrawIndexedInstancedIndirect(bin.argsBuf, argsByteOffset);
	}
}
