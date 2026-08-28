#include "PIXLRendererPage.h"

#include "PIXLStyle.h"
#include "BackgroundBlur.h"

#include <algorithm>
#include <array>
#include <format>
#include <imgui.h>
#include <DirectXTex.h>
#include <filesystem>
#include <unordered_map>
#include <winrt/base.h>

#include "RenderModule.h"
#include "Modules/CameraSuite.h"
#include "Modules/WorldProbes.h"
#include "Modules/GroundResponse.h"
#include "Modules/HybridGI.h"
#include "Modules/ContactShadows.h"
#include "Modules/TerrainOcclusion.h"
#include "Modules/ImageReconstruction.h"
#include "Modules/WaterOptics.h"
#include "Fonts.h"
#include "Globals.h"
#include "Menu.h"
#include "Menu/TuningWorkspaceRenderer.h"
#include "Renderer/QualityProfiles.h"
#include "ShaderCache.h"
#include "State.h"
#include "MaterialForge.h"
#include "Util.h"

namespace
{
	constexpr std::array<const char*, 4> kProfileNames{
		"FAST",
		"BALANCED",
		"ENHANCED",
		"CINEMATIC"
	};

	constexpr std::array<const char*, 4> kDetailNames{
		"LOW",
		"MEDIUM",
		"HIGH",
		"ULTRA"
	};

	using QualityGroup =
		PIXLRenderer::QualityProfiles::Group;

	struct QualityPreviewSelection
	{
		const char* assetKey = "Lighting";
		const char* title = "LIGHTING";
		const char* description = "Global illumination, reflections and shadow detail.";
		int tier = 3;
	};

	struct QualityPreviewTexture
	{
		winrt::com_ptr<ID3D11ShaderResourceView> srv;
		ImVec2 size{};
	};

	std::unordered_map<std::string, QualityPreviewTexture>
		g_qualityPreviewTextures;

	const char* QualityTierName(int tier)
	{
		return kDetailNames[
			std::clamp(
				tier,
				0,
				3)];
	}

	QualityPreviewTexture* GetQualityPreviewTexture(
		const char* assetKey,
		int tier)
	{
		if (!globals::d3d::device ||
			!assetKey ||
			assetKey[0] == '\0') {
			return nullptr;
		}

		const std::string cacheKey =
			std::format(
				"{}_{}",
				assetKey,
				QualityTierName(tier));

		if (auto it =
				g_qualityPreviewTextures.find(
					cacheKey);
			it !=
				g_qualityPreviewTextures.end()) {
			return &it->second;
		}

		const auto path =
			std::filesystem::path(
				L"Data\\SKSE\\Plugins\\PIXL\\Interface\\QualityPreviews") /
			(
				std::wstring(
					assetKey,
					assetKey +
						std::char_traits<char>::length(
							assetKey)) +
				L"_" +
				std::wstring(
					QualityTierName(tier),
					QualityTierName(tier) +
						std::char_traits<char>::length(
							QualityTierName(tier))) +
				L".png");

		if (!std::filesystem::exists(path))
			return nullptr;

		DirectX::TexMetadata metadata{};
		DirectX::ScratchImage image;

		if (FAILED(
				DirectX::LoadFromWICFile(
					path.c_str(),
					DirectX::WIC_FLAGS_NONE,
					&metadata,
					image))) {
			return nullptr;
		}

		QualityPreviewTexture texture;
		if (FAILED(
				DirectX::CreateShaderResourceView(
					globals::d3d::device,
					image.GetImages(),
					image.GetImageCount(),
					metadata,
					texture.srv.put()))) {
			return nullptr;
		}

		texture.size =
			ImVec2(
				static_cast<float>(
					metadata.width),
				static_cast<float>(
					metadata.height));

		auto [it, inserted] =
			g_qualityPreviewTextures.emplace(
				cacheKey,
				std::move(texture));
		static_cast<void>(inserted);
		return &it->second;
	}

	void DrawQualityPreview(
		const QualityPreviewSelection& preview)
	{
		PIXLUI::ChromeScope panel(
			"##PIXLQualityPreview",
			ImVec2(
				0,
				PIXLUI::Ref(400.0f)),
			PIXLUI::ChromeStyle::Raised,
			true,
			ImGuiWindowFlags_NoScrollbar |
				ImGuiWindowFlags_NoScrollWithMouse,
			12.0f);

		if (!panel)
			return;

		// Fixed inner title band. The previous text started directly against
		// the chrome; this gives the preview a deliberate centred baseline.
		const ImVec2 headerStart =
			ImGui::GetCursorScreenPos();
		const float headerWidth =
			ImGui::GetContentRegionAvail().x;
		const float headerHeight =
			PIXLUI::Ref(28.0f);
		const float headerPadX =
			PIXLUI::Ref(6.0f);

		ImDrawList* draw =
			ImGui::GetWindowDrawList();

		const char* heading =
			"QUALITY PREVIEW";
		const ImVec2 headingSize =
			ImGui::CalcTextSize(
				heading);

		char context[96]{};
		std::snprintf(
			context,
			sizeof(context),
			"%s / %s",
			preview.title,
			QualityTierName(
				preview.tier));
		const ImVec2 contextSize =
			ImGui::CalcTextSize(
				context);

		const float textY =
			headerStart.y +
			(headerHeight -
			 headingSize.y) *
				0.5f;

		draw->AddText(
			ImVec2(
				headerStart.x +
					headerPadX,
				textY),
			PIXLUI::Colors::TextMuted,
			heading);

		draw->AddText(
			ImVec2(
				headerStart.x +
					headerPadX +
					headingSize.x +
					PIXLUI::Ref(7.0f),
				headerStart.y +
					(headerHeight -
					 contextSize.y) *
						0.5f),
			PIXLUI::Colors::CyanSoft,
			context);

		draw->AddLine(
			ImVec2(
				headerStart.x +
					headerPadX,
				headerStart.y +
					headerHeight),
			ImVec2(
				headerStart.x +
					headerWidth -
					headerPadX,
				headerStart.y +
					headerHeight),
			PIXLUI::Colors::BorderSoft,
			PIXLUI::Ref(1.0f));

		ImGui::SetCursorScreenPos(
			headerStart);
		ImGui::Dummy(
			ImVec2(
				headerWidth,
				headerHeight));

		ImGui::Dummy(
			ImVec2(
				0,
				PIXLUI::Ref(5.0f)));

		const ImVec2 imageStart =
			ImGui::GetCursorScreenPos();
		const ImVec2 available =
			ImGui::GetContentRegionAvail();

		const ImVec2 imageArea(
			available.x,
			std::max(
				PIXLUI::Ref(280.0f),
				available.y));

		if (auto* texture =
				GetQualityPreviewTexture(
					preview.assetKey,
					preview.tier);
			texture &&
			texture->srv &&
			texture->size.x > 0.0f &&
			texture->size.y > 0.0f) {
			const float sourceAspect =
				texture->size.x /
				texture->size.y;
			const float areaAspect =
				imageArea.x /
				imageArea.y;

			ImVec2 drawSize =
				imageArea;

			if (sourceAspect > areaAspect) {
				drawSize.y =
					imageArea.x /
					sourceAspect;
			} else {
				drawSize.x =
					imageArea.y *
					sourceAspect;
			}

			ImGui::SetCursorScreenPos(
				ImVec2(
					imageStart.x +
						(imageArea.x -
						 drawSize.x) *
							0.5f,
					imageStart.y +
						(imageArea.y -
						 drawSize.y) *
							0.5f));

			ImGui::Image(
				texture->srv.get(),
				drawSize);
		} else {
			PIXLUI::FillChamfered(
				draw,
				imageStart,
				ImVec2(
					imageStart.x +
						imageArea.x,
					imageStart.y +
						imageArea.y),
				PIXLUI::Ref(4.0f),
				IM_COL32(
					8,
					11,
					14,
					230));

			PIXLUI::StrokeChamfered(
				draw,
				imageStart,
				ImVec2(
					imageStart.x +
						imageArea.x,
					imageStart.y +
						imageArea.y),
				PIXLUI::Ref(4.0f),
				PIXLUI::Colors::BorderSoft,
				PIXLUI::Ref(1.0f));

			const char* placeholder =
				"PREVIEW IMAGE SLOT";
			const ImVec2 placeholderSize =
				ImGui::CalcTextSize(
					placeholder);

			draw->AddText(
				ImVec2(
					imageStart.x +
						(imageArea.x -
						 placeholderSize.x) *
							0.5f,
					imageStart.y +
						(imageArea.y -
						 placeholderSize.y) *
							0.5f),
				PIXLUI::Colors::TextDim,
				placeholder);

			ImGui::Dummy(
				imageArea);
		}
	}


	enum class PublicPage : int
	{
		Quality = 0,
		Camera = 1,
		Renderer = 2
	};

	bool g_deferredStateSave = false;

	void QueueDeferredStateSave()
	{
		g_deferredStateSave = true;
	}

	void FlushDeferredStateSave()
	{
		if (!g_deferredStateSave)
			return;

		// Slider values and renderer constant data remain live every frame.
		// Only the JSON/disk commit waits for the user's interaction to end.
		if (ImGui::IsMouseDown(
				ImGuiMouseButton_Left) ||
			ImGui::IsAnyItemActive()) {
			return;
		}

		globals::state->Save();
		g_deferredStateSave = false;
	}

	RenderModule* FindFeature(
		std::string_view shortName)
	{
		for (auto* feature :
			 RenderModule::GetModuleList()) {
			if (feature &&
				feature->GetShortName() ==
					shortName) {
				return feature;
			}
		}

		return nullptr;
	}

	void Tooltip(const char* text)
	{
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::PushTextWrapPos(
				ImGui::GetCursorPosX() +
				PIXLUI::Ref(330.0f));
			ImGui::TextUnformatted(text);
			ImGui::PopTextWrapPos();
		}
	}

	void SetPipelineForNextBoot(bool enabled)
	{
		for (const auto& placement : PIXLRendererPage::Placements) {
			if (FindFeature(placement.featureShortName)) {
				globals::state->SetFeatureDisabled(
					std::string(
						placement.featureShortName),
					!enabled);
			}
		}
	}

	std::pair<int, int> CountPipelineFeatures()
	{
		int loaded = 0;
		int installed = 0;

		for (const auto& placement : PIXLRendererPage::Placements) {
			if (auto* feature =
					FindFeature(
						placement.featureShortName)) {
				installed += feature->installed ? 1 : 0;
				loaded += feature->loaded ? 1 : 0;
			}
		}

		return { loaded, installed };
	}

	void SectionHeading(const char* title)
	{
		PIXLUI::SectionBanner(title);
		ImGui::Dummy(
			ImVec2(
				0,
				PIXLUI::Ref(2.0f)));
	}

	bool DrawChoiceButtons(
		const char* id,
		int& value,
		const char* const* labels,
		int count)
	{
		ImGui::PushID(id);

		const float gap =
			PIXLUI::Ref(7.0f);
		const float width =
			std::max(
				PIXLUI::Ref(70.0f),
				(ImGui::GetContentRegionAvail().x -
					gap *
						static_cast<float>(count - 1)) /
					static_cast<float>(count));

		bool changed = false;

		for (int i = 0; i < count; ++i) {
			if (i > 0)
				ImGui::SameLine(
					0.0f,
					gap);

			const bool selected =
				i == value;

			if (PIXLUI::ActionButton(
					labels[i],
					ImVec2(
						width,
						PIXLUI::Ref(36.0f)),
					selected)) {
				value = i;
				changed = true;
			}
		}

		ImGui::PopID();
		return changed;
	}

	bool DrawQualitySlider(
		const char* label,
		int& value,
		const char* assetKey,
		const char* description,
		QualityPreviewSelection& preview)
	{
		bool hovered = false;

		const bool changed =
			PIXLUI::SliderIntField(
				label,
				&value,
				0,
				3,
				kDetailNames.data(),
				&hovered);

		if (hovered || changed) {
			preview.assetKey =
				assetKey;
			preview.title =
				label;
			preview.description =
				description;
			preview.tier =
				value;
		}

		return changed;
	}

	bool ToggleControl(
		const char* label,
		bool* value,
		const char* help = nullptr)
	{
		const bool changed =
			PIXLUI::LabeledToggle(
				label,
				value);

		if (help)
			Tooltip(help);

		return changed;
	}

	bool SliderControl(
		const char* label,
		float* value,
		float minValue,
		float maxValue,
		const char* format,
		bool logarithmic = false,
		const char* help = nullptr)
	{
		const bool changed =
			PIXLUI::SliderFloatField(
				label,
				value,
				minValue,
				maxValue,
				format,
				logarithmic);

		if (help)
			Tooltip(help);

		return changed;
	}

	bool CycleControl(
		const char* label,
		int* value,
		const char* const* values,
		int count,
		const char* help = nullptr)
	{
		const bool changed =
			PIXLUI::CycleSelector(
				label,
				value,
				values,
				count);

		if (help)
			Tooltip(help);

		return changed;
	}

	void DrawPageIntro(
		const char* title,
		const char* subtitle)
	{
		ImGui::TextColored(
			PIXLUI::ToVec4(
				PIXLUI::Colors::Text),
			"%s",
			title);

		if (subtitle &&
			subtitle[0] != '\0') {
			ImGui::TextColored(
				PIXLUI::ToVec4(
					PIXLUI::Colors::TextMuted),
				"%s",
				subtitle);
		}

		ImGui::Dummy(
			ImVec2(
				0,
				PIXLUI::Ref(8.0f)));
	}


	void DrawQualityControls()
	{
		auto& settings =
			globals::menu->GetSettings();

		struct Row
		{
			const char* name;
			const char* assetKey;
			int* value;
			QualityGroup group;
			const char* help;
		};

		const std::array rows{
			Row{
				"Lighting",
				"Lighting",
				&settings.LightingQuality,
				QualityGroup::Lighting,
				"Indirect light, reflections, contact shadows and temporal stability." },
			Row{
				"Materials",
				"Materials",
				&settings.MaterialsQuality,
				QualityGroup::Materials,
				"Surface depth, material response, specular stability and BRDF quality." },
			Row{
				"Atmosphere",
				"Atmosphere",
				&settings.AtmosphereQuality,
				QualityGroup::Atmosphere,
				"Volumetric-fog resolution, depth precision and temporal sampling." },
			Row{
				"Water",
				"Water",
				&settings.WaterQuality,
				QualityGroup::Water,
				"Water-reflection trace budget, edge stability and caustic dispersion." },
			Row{
				"Terrain & Vegetation",
				"TerrainVegetation",
				&settings.TerrainVegetationQuality,
				QualityGroup::TerrainVegetation,
				"Raised snow/mud distance and tessellation quality; vegetation appearance stays user-authored." },
			Row{
				"Characters",
				"Characters",
				&settings.CharactersQuality,
				QualityGroup::Characters,
				"Skin diffusion samples, skin micro detail and hair self-shadow quality." },
			Row{
				"Camera",
				"Camera",
				&settings.CameraQuality,
				QualityGroup::Camera,
				"Depth of field, motion blur and lens-effect sampling quality; camera exposure stays user-authored." }
		};

		std::array<int, static_cast<size_t>(QualityGroup::Count)>
			detectedTiers{};
		bool custom = false;
		for (size_t i = 0; i < rows.size(); ++i) {
			detectedTiers[i] =
				PIXLRenderer::QualityProfiles::Detect(
					rows[i].group);
			custom = custom ||
				detectedTiers[i] != *rows[i].value ||
				*rows[i].value != settings.RendererQuality;
		}

		static QualityPreviewSelection preview{};

		for (const auto& row : rows) {
			if (std::string_view(
					preview.assetKey) ==
				row.assetKey) {
				preview.tier =
					*row.value;
				break;
			}
		}

		if (ImGui::BeginTable(
				"##PIXLQualityWorkspace",
				2,
				ImGuiTableFlags_SizingStretchProp |
					ImGuiTableFlags_NoSavedSettings)) {
			ImGui::TableSetupColumn(
				"Controls",
				ImGuiTableColumnFlags_WidthStretch,
				0.72f);
			ImGui::TableSetupColumn(
				"Preview",
				ImGuiTableColumnFlags_WidthStretch,
				1.28f);

			ImGui::TableNextColumn();
			ImGui::PushID(
				"QualityControls");

			const ImVec2 qualitySpacing =
				ImGui::GetStyle().ItemSpacing;
			ImGui::PushStyleVar(
				ImGuiStyleVar_ItemSpacing,
				ImVec2(
					qualitySpacing.x,
					PIXLUI::Ref(3.0f)));

			SectionHeading(
				"QUALITY PROFILE");

			int profile =
				std::clamp(
					settings.RendererQuality,
					0,
					3);

			if (DrawChoiceButtons(
					"GlobalQuality",
					profile,
					kProfileNames.data(),
					4)) {
				settings.RendererQuality =
					profile;

				PIXLRenderer::QualityProfiles::
					ApplyGlobal(
						profile);
			}

			ImGui::Dummy(
				ImVec2(
					0,
					PIXLUI::Ref(6.0f)));

			ImGui::TextColored(
				PIXLUI::ToVec4(
					PIXLUI::Colors::CyanSoft),
				"%s",
				kProfileNames[
					std::clamp(
						settings.RendererQuality,
						0,
						3)]);

			ImGui::SameLine();

			ImGui::TextDisabled(
				custom
					? "CUSTOM"
					: "COORDINATED");

			if (custom) {
				ImGui::SameLine(
					0.0f,
					PIXLUI::Ref(10.0f));

				if (PIXLUI::ActionButton(
						"MATCH PROFILE",
						ImVec2(
							PIXLUI::Ref(118.0f),
							PIXLUI::Ref(27.0f)),
						false)) {
					PIXLRenderer::QualityProfiles::
						ApplyGlobal(
							settings.RendererQuality);
				}
			}

			ImGui::Dummy(
				ImVec2(
					0,
					PIXLUI::Ref(5.0f)));

			SectionHeading(
				"SYSTEM DETAIL");

			for (size_t i = 0;
				 i < rows.size();
				 ++i) {
				const auto& row =
					rows[i];

				ImGui::PushID(
					static_cast<int>(i));

				if (DrawQualitySlider(
						row.name,
						*row.value,
						row.assetKey,
						row.help,
						preview)) {
					PIXLRenderer::QualityProfiles::
						Apply(
							row.group,
							*row.value);

					QueueDeferredStateSave();
				}

				if (detectedTiers[i] != *row.value) {
					ImGui::SameLine(
						0.0f,
						PIXLUI::Ref(7.0f));
					ImGui::TextColored(
						PIXLUI::ToVec4(
							PIXLUI::Colors::Warning),
						"CUSTOM");
					if (ImGui::IsItemHovered()) {
						ImGui::SetTooltip(
							"Advanced values differ from the selected %s contract. Move the tier or use MATCH PROFILE to reapply it.",
							QualityTierName(*row.value));
					}
				}

				ImGui::PopID();
			}

			ImGui::PopStyleVar();
			ImGui::PopID();

			ImGui::TableNextColumn();
			DrawQualityPreview(
				preview);

			ImGui::EndTable();
		}
	}

	void DrawPerformanceControls()
	{
		auto& imageReconstruction =
			globals::pipeline::imageReconstruction;
		auto& settings =
			imageReconstruction.settings;

		bool changed = false;
		bool restartNeeded = false;

		// Presentation-only disclosure state. Experimental renderer options are
		// intentionally hidden from the average-user path until requested.
		static bool showExperimentalDisplayOptions = false;

		SectionHeading("DISPLAY & PERFORMANCE");

		uint* method =
			imageReconstruction.streamline.featureDLSS
				? &settings.upscaleMethod
				: &settings.upscaleMethodNoDLSS;

		const char* methods[] = {
			"NATIVE",
			"TAA",
			"FSR 3.1",
			"DLSS"
		};

		const int methodCount =
			imageReconstruction.streamline.featureDLSS
				? 4
				: 3;

		int methodValue =
			std::clamp(
				static_cast<int>(*method),
				0,
				methodCount - 1);

		if (CycleControl(
				"Reconstruction",
				&methodValue,
				methods,
				methodCount,
				"Chooses how PIXL reconstructs the final image. Changing reconstruction technology is safest after a restart.")) {
			*method =
				static_cast<uint>(
					methodValue);
			changed = restartNeeded = true;
		}

		if (*method >=
			static_cast<uint>(
				ImageReconstruction::UpscaleMethod::kFSR)) {
			const char* qualities[] = {
				"NATIVE AA",
				"QUALITY",
				"BALANCED",
				"PERFORMANCE",
				"ULTRA PERFORMANCE"
			};

			int quality =
				static_cast<int>(
					settings.qualityMode);

			if (CycleControl(
					"Image quality",
					&quality,
					qualities,
					5,
					"Quality preserves the most detail. Balanced is the recommended performance option at 1440p and above.")) {
				settings.qualityMode =
					static_cast<uint>(
						quality);
				changed = restartNeeded = true;
			}
		}

		bool vsync = false;
		RE::Setting* presentSetting = nullptr;
		bool preferenceSetting = false;

		if (globals::game::iniPrefSettingCollection) {
			presentSetting =
				globals::game::iniPrefSettingCollection->GetSetting(
					"iVSyncPresentInterval:Display");
		}

		preferenceSetting =
			presentSetting != nullptr;

		if (!presentSetting) {
			presentSetting =
				RE::GetINISetting(
					"iVSyncPresentInterval:Display");
		}

		if (presentSetting) {
			vsync =
				presentSetting->GetInteger() != 0;
		} else if (globals::game::renderer) {
			vsync =
				globals::game::renderer
					->GetRuntimeData()
					.presentInterval != 0;
		}

		if (ToggleControl(
				"V-Sync",
				&vsync,
				"Synchronizes presentation to the display refresh. Leave it off when using VRR with an external frame cap.")) {
			if (presentSetting) {
				presentSetting->SetInteger(
					vsync ? 1 : 0);

				if (preferenceSetting &&
					globals::game::iniPrefSettingCollection) {
					globals::game::iniPrefSettingCollection
						->WriteSetting(
							presentSetting);
				}
			}

			if (globals::game::renderer) {
				globals::game::renderer
					->GetRuntimeData()
					.presentInterval =
						vsync ? 1u : 0u;
			}
		}

		bool limiter =
			settings.frameLimitMode != 0;

		if (ToggleControl(
				"Frame limiter",
				&limiter,
				"PIXL's low-jitter limiter works on both the native renderer and frame-generation path.")) {
			settings.frameLimitMode =
				limiter ? 1u : 0u;
			changed = true;
		}

		if (limiter) {
			changed |=
				SliderControl(
					"Target FPS",
					&settings.frameLimitFPS,
					30.0f,
					240.0f,
					"%.0f FPS",
					false);
		}

		if (ToggleControl(
				"Experimental options",
				&showExperimentalDisplayOptions,
				"Shows PIXL display-path features intended for experienced users. Frame generation and future experimental upscaling controls live here.")) {
			// UI disclosure only; no renderer state is changed here.
		}

		bool frameGeneration =
			settings.frameGenerationMode != 0;

		if (showExperimentalDisplayOptions) {
			ImGui::PushID(
				"ExperimentalDisplayOptions");

			ImGui::Indent(
				PIXLUI::Ref(14.0f));

			if (ToggleControl(
					"Frame generation",
					&frameGeneration,
					"Generates intermediate frames through PIXL's compatibility swapchain. Requires a restart after changing.")) {
				settings.frameGenerationMode =
					frameGeneration ? 1u : 0u;
				changed = restartNeeded = true;
			}

			// This disclosure is intentionally the future home for optional
			// upscaling experiments so the normal renderer page stays clean.
			ImGui::Unindent(
				PIXLUI::Ref(14.0f));

			ImGui::PopID();
		}

		const bool frameGenerationPendingRestart =
			frameGeneration !=
				imageReconstruction
					.IsFrameGenerationDx12PathActive();

		if (restartNeeded ||
			frameGenerationPendingRestart) {
			ImGui::TextColored(
				PIXLUI::ToVec4(
					PIXLUI::Colors::Warning),
				"RESTART SKYRIM TO APPLY DISPLAY PATH CHANGES");
		} else {
			ImGui::TextColored(
				PIXLUI::ToVec4(
					PIXLUI::Colors::TextDim),
				showExperimentalDisplayOptions &&
						frameGeneration
					? "FRAME GENERATION PATH READY"
					: "DISPLAY PATH READY");
		}

		if (changed)
			QueueDeferredStateSave();
	}

	void DrawLiveCameraPreview()
	{
		static bool livePreviewEnabled =
			true;
		static bool livePreviewEffects =
			true;

		PIXLUI::ChromeScope panel(
			"##PIXLCameraLivePreview",
			ImVec2(
				0,
				PIXLUI::Ref(320.0f)),
			PIXLUI::ChromeStyle::Raised,
			true,
			ImGuiWindowFlags_NoScrollbar |
				ImGuiWindowFlags_NoScrollWithMouse,
			12.0f);

		if (!panel)
			return;

		const ImVec2 headerStart =
			ImGui::GetCursorScreenPos();
		const float headerWidth =
			ImGui::GetContentRegionAvail().x;
		const float headerHeight =
			PIXLUI::Ref(32.0f);

		ImDrawList* draw =
			ImGui::GetWindowDrawList();

		const char* title =
			"LIVE GAME PREVIEW";
		const ImVec2 titleSize =
			ImGui::CalcTextSize(
				title);

		draw->AddText(
			ImVec2(
				headerStart.x +
					PIXLUI::Ref(7.0f),
				headerStart.y +
					(headerHeight -
					 titleSize.y) *
						0.5f),
			PIXLUI::Colors::TextMuted,
			title);

		const float toggleWidth =
			PIXLUI::Ref(60.0f);
		const float toggleHeight =
			PIXLUI::Ref(26.0f);
		const float labelGap =
			PIXLUI::Ref(5.0f);
		const float groupGap =
			PIXLUI::Ref(8.0f);

		const char* previewLabel =
			"PREVIEW";
		const char* effectsLabel =
			"FX";

		const float previewLabelWidth =
			ImGui::CalcTextSize(
				previewLabel).x;
		const float effectsLabelWidth =
			ImGui::CalcTextSize(
				effectsLabel).x;

		const float controlsWidth =
			previewLabelWidth +
			labelGap +
			toggleWidth +
			groupGap +
			effectsLabelWidth +
			labelGap +
			toggleWidth;

		float controlX =
			headerStart.x +
			headerWidth -
			controlsWidth -
			PIXLUI::Ref(6.0f);

		const float controlY =
			headerStart.y +
			(headerHeight -
			 toggleHeight) *
				0.5f;

		draw->AddText(
			ImVec2(
				controlX,
				headerStart.y +
					(headerHeight -
					 ImGui::GetTextLineHeight()) *
						0.5f),
			PIXLUI::Colors::TextDim,
			previewLabel);

		controlX +=
			previewLabelWidth +
			labelGap;

		ImGui::SetCursorScreenPos(
			ImVec2(
				controlX,
				controlY));

		PIXLUI::Toggle(
			"##LiveCameraPreviewEnabled",
			&livePreviewEnabled);

		if (ImGui::IsItemHovered()) {
			if (auto _tt =
					Util::HoverTooltipWrapper()) {
				ImGui::TextWrapped(
					"Shows the rendered game from CameraSuite's clean scene buffer. PIXL menu UI is excluded.");
			}
		}

		controlX +=
			toggleWidth +
			groupGap;

		draw->AddText(
			ImVec2(
				controlX,
				headerStart.y +
					(headerHeight -
					 ImGui::GetTextLineHeight()) *
						0.5f),
			PIXLUI::Colors::TextDim,
			effectsLabel);

		controlX +=
			effectsLabelWidth +
			labelGap;

		ImGui::SetCursorScreenPos(
			ImVec2(
				controlX,
				controlY));

		PIXLUI::Toggle(
			"##LiveCameraPreviewEffects",
			&livePreviewEffects);

		if (ImGui::IsItemHovered()) {
			if (auto _tt =
					Util::HoverTooltipWrapper()) {
				ImGui::TextWrapped(
					"Preview comparison only. OFF neutralizes PIXL camera finishing in the preview and does not save or alter gameplay settings.");
			}
		}

		draw->AddLine(
			ImVec2(
				headerStart.x +
					PIXLUI::Ref(6.0f),
				headerStart.y +
					headerHeight),
			ImVec2(
				headerStart.x +
					headerWidth -
					PIXLUI::Ref(6.0f),
				headerStart.y +
					headerHeight),
			PIXLUI::Colors::BorderSoft,
			PIXLUI::Ref(1.0f));

		ImGui::SetCursorScreenPos(
			headerStart);
		ImGui::Dummy(
			ImVec2(
				headerWidth,
				headerHeight));

		ImGui::Dummy(
			ImVec2(
				0,
				PIXLUI::Ref(4.0f)));

		const ImVec2 imageStart =
			ImGui::GetCursorScreenPos();
		const ImVec2 available =
			ImGui::GetContentRegionAvail();
		const ImVec2 imageArea(
			available.x,
			std::max(
				PIXLUI::Ref(240.0f),
				available.y));

		BackgroundBlur::LivePreviewFrame
			preview{};

		if (livePreviewEnabled) {
			preview =
				BackgroundBlur::
					CaptureLivePreview(
						livePreviewEffects);
		}

		if (livePreviewEnabled &&
			preview) {
			const float sourceAspect =
				static_cast<float>(
					preview.width) /
				static_cast<float>(
					preview.height);
			const float areaAspect =
				imageArea.x /
				imageArea.y;

			ImVec2 drawSize =
				imageArea;

			if (sourceAspect >
				areaAspect) {
				drawSize.y =
					imageArea.x /
						sourceAspect;
			} else {
				drawSize.x =
					imageArea.y *
						sourceAspect;
			}

			ImGui::SetCursorScreenPos(
				ImVec2(
					imageStart.x +
						(imageArea.x -
						 drawSize.x) *
							0.5f,
					imageStart.y +
						(imageArea.y -
						 drawSize.y) *
							0.5f));

			ImGui::Image(
				preview.srv,
				drawSize);
		} else {
			PIXLUI::FillChamfered(
				draw,
				imageStart,
				ImVec2(
					imageStart.x +
						imageArea.x,
					imageStart.y +
						imageArea.y),
				PIXLUI::Ref(4.0f),
				IM_COL32(
					8,
					11,
					14,
					230));

			PIXLUI::StrokeChamfered(
				draw,
				imageStart,
				ImVec2(
					imageStart.x +
						imageArea.x,
					imageStart.y +
						imageArea.y),
				PIXLUI::Ref(4.0f),
				PIXLUI::Colors::BorderSoft,
				PIXLUI::Ref(1.0f));

			const char* message =
				livePreviewEnabled
					? "CLEAN SCENE PREVIEW INITIALIZING"
					: "LIVE PREVIEW OFF";
			const ImVec2 messageSize =
				ImGui::CalcTextSize(
					message);

			draw->AddText(
				ImVec2(
					imageStart.x +
						(imageArea.x -
						 messageSize.x) *
							0.5f,
					imageStart.y +
						(imageArea.y -
						 messageSize.y) *
							0.5f),
				PIXLUI::Colors::TextDim,
				message);

			ImGui::Dummy(
				imageArea);
		}
	}


	void DrawFinishingControls()
	{
		auto& camera =
			globals::pipeline::cameraSuite;
		auto& gi =
			globals::pipeline::hybridGI.settings;
		auto& water =
			globals::pipeline::waterOptics.settings;
		auto& cubemaps =
			globals::pipeline::worldProbes.settings;

		bool changed = false;
		bool lightingChanged = false;

		// CAMERA + POST FX is a fixed no-scroll product page. Keep the mature
		// settings structurally identical, but place them around the permanent
		// live scene preview instead of stacking three unrelated columns.
		const ImVec2 originalSpacing =
			ImGui::GetStyle().ItemSpacing;

		ImGui::PushStyleVar(
			ImGuiStyleVar_ItemSpacing,
			ImVec2(
				originalSpacing.x,
				PIXLUI::Ref(2.0f)));

		if (ImGui::BeginTable(
				"##PIXLCameraWorkspace",
				3,
				ImGuiTableFlags_SizingStretchProp |
					ImGuiTableFlags_NoSavedSettings)) {
			ImGui::TableSetupColumn(
				"Camera",
				ImGuiTableColumnFlags_WidthStretch,
				0.94f);
			ImGui::TableSetupColumn(
				"Preview",
				ImGuiTableColumnFlags_WidthStretch,
				1.22f);
			ImGui::TableSetupColumn(
				"Effects",
				ImGuiTableColumnFlags_WidthStretch,
				0.94f);

			// -------------------------------------------------------------
			// LEFT — exposure + tonemap / LUT
			// -------------------------------------------------------------
			ImGui::TableNextColumn();
			ImGui::PushID(
				"ExposureTonemapColumn");

			SectionHeading(
				"EXPOSURE & ADAPTATION");

			changed |=
				ToggleControl(
					"Physical camera",
					&camera.settings
						.enablePhysicalCamera);

			changed |=
				SliderControl(
					"Camera influence",
					&camera.settings
						.cameraInfluence,
					0.0f,
					1.0f,
					"%.2f",
					false);

			Tooltip(
				"How strongly PIXL's photographic response modifies Skyrim's authored SDR image. Lower values remain closer to vanilla lighting and weather.");

			changed |=
				ToggleControl(
					"Automatic exposure",
					&camera.settings
						.cameraAutoExposure);

			changed |=
				SliderControl(
					"Exposure",
					&camera.settings
						.cameraExposureCompensationEV,
					-2.0f,
					2.0f,
					"%+.2f EV",
					false);

			changed |=
				SliderControl(
					"Bright to dark",
					&camera.settings
						.cameraAdaptBrightToDark,
					0.05f,
					4.0f,
					"%.2f s",
					true);

			changed |=
				SliderControl(
					"Dark to bright",
					&camera.settings
						.cameraAdaptDarkToBright,
					0.03f,
					2.0f,
					"%.2f s",
					true);

			changed |=
				SliderControl(
					"Local exposure",
					&camera.settings
						.cameraLocalExposure,
					0.0f,
					0.5f,
					"%.2f",
					false);

			SectionHeading(
				"TONEMAP & LUT");

			const char* looks[] = {
				"Original",
				"Nordic Neutral",
				"Saga",
				"Dramatic",
				"Hearthfire",
				"Bleak"
			};

			int look =
				static_cast<int>(
					camera.settings
						.lookPreset);

			if (CycleControl(
					"Colour grade",
					&look,
					looks,
					static_cast<int>(
						std::size(
							looks)))) {
				camera.settings.lookPreset =
					static_cast<uint>(
						look);
				camera.LoadLookTexture();
				changed = true;
			}

			ImGui::BeginDisabled(
				camera.settings.lookPreset ==
					0);

			float lookPercent =
				std::clamp(
					camera.settings
						.lookOpacity *
						100.0f,
					0.0f,
					100.0f);

			if (SliderControl(
					"LUT strength",
					&lookPercent,
					0.0f,
					100.0f,
					"%.0f%%",
					false)) {
				camera.settings.lookOpacity =
					lookPercent *
						0.01f;
				changed = true;
			}

			ImGui::EndDisabled();

			changed |=
				SliderControl(
					"Contrast",
					&camera.settings
						.cameraContrast,
					0.75f,
					1.25f,
					"%.2f",
					false);

			changed |=
				SliderControl(
					"Colour",
					&camera.settings
						.cameraSaturation,
					0.75f,
					1.25f,
					"%.2f",
					false);

			changed |=
				SliderControl(
					"Highlight protection",
					&camera.settings
						.cameraHighlightProtection,
					0.0f,
					1.0f,
					"%.2f",
					false);

			changed |=
				SliderControl(
					"Shadow detail",
					&camera.settings
						.cameraShadowDetail,
					0.0f,
					0.4f,
					"%.2f",
					false);

			changed |=
				SliderControl(
					"Black toe",
					&camera.settings
						.cameraToe,
					0.0f,
					0.5f,
					"%.2f",
					false);

			changed |=
				SliderControl(
					"Highlight shoulder",
					&camera.settings
						.cameraShoulder,
					0.2f,
					1.5f,
					"%.2f",
					false);

			changed |=
				SliderControl(
					"Menu model visibility",
					&camera.settings
						.menuSceneBrightness,
					0.75f,
					3.0f,
					"%.2fx",
					false);

			Tooltip(
				"Dedicated gain for main-menu, loading, and lockpicking models. Gameplay exposure is unaffected.");

			ImGui::PopID();

			// -------------------------------------------------------------
			// CENTRE — live scene preview + depth of field
			// -------------------------------------------------------------
			ImGui::TableNextColumn();
			ImGui::PushID(
				"PreviewDepthColumn");

			DrawLiveCameraPreview();

			ImGui::Dummy(
				ImVec2(
					0,
					PIXLUI::Ref(4.0f)));

			SectionHeading(
				"DEPTH OF FIELD");

			changed |=
				ToggleControl(
					"Depth of field enabled",
					&camera.settings
						.enableEnhancedDepthOfField);

			ImGui::BeginDisabled(
				!camera.settings
					.enableEnhancedDepthOfField);

			changed |=
				SliderControl(
					"DOF strength",
					&camera.settings
						.dofStrength,
					0.0f,
					1.0f,
					"%.2f",
					false);

			changed |=
				SliderControl(
					"Focus distance",
					&camera.settings
						.dofFocusDistance,
					100.0f,
					20000.0f,
					"%.0f units",
					true);

			changed |=
				SliderControl(
					"Focus range",
					&camera.settings
						.dofFocusRange,
					100.0f,
					20000.0f,
					"%.0f units",
					true);

			changed |=
				SliderControl(
					"Bokeh size",
					&camera.settings
						.dofBokehRadius,
					0.5f,
					2.0f,
					"%.2fx",
					false);

			changed |=
				SliderControl(
					"Highlight response",
					&camera.settings
						.dofHighlightResponse,
					0.0f,
					1.0f,
					"%.2f",
					false);

			changed |=
				SliderControl(
					"Focus edge protection",
					&camera.settings
						.dofFocusEdgeProtection,
					0.0f,
					2.0f,
					"%.2f",
					false);

			changed |=
				SliderControl(
					"Foreground coverage",
					&camera.settings
						.dofForegroundCoverage,
					0.0f,
					1.5f,
					"%.2f",
					false);

			ImGui::EndDisabled();
			ImGui::PopID();

			// -------------------------------------------------------------
			// RIGHT — reflections / bloom / weather lens
			// -------------------------------------------------------------
			ImGui::TableNextColumn();
			ImGui::PushID(
				"PostEffectsColumn");

			SectionHeading(
				"OCCLUSION & REFLECTIONS");

			lightingChanged |=
				ToggleControl(
					"Ambient occlusion",
					&gi.EnableDirectionalOcclusion);

			ImGui::BeginDisabled(
				!gi.EnableDirectionalOcclusion);

			lightingChanged |=
				SliderControl(
					"AO presence",
					&gi.AOPower,
					0.0f,
					2.0f,
					"%.2fx",
					false);

			lightingChanged |=
				SliderControl(
					"AO reach",
					&gi.AORadius,
					32.0f,
					512.0f,
					"%.0f units",
					true);

			ImGui::EndDisabled();

			lightingChanged |=
				ToggleControl(
					"Scene reflections",
					&gi.EnableExperimentalSpecularGI);

			ImGui::BeginDisabled(
				!gi.EnableExperimentalSpecularGI);

			lightingChanged |=
				SliderControl(
					"Reflection presence",
					&gi.ReflectionIntensity,
					0.0f,
					1.5f,
					"%.2fx",
					false);

			ImGui::EndDisabled();

			bool materialSSR =
				cubemaps.EnabledSSR !=
					0u;

			if (ToggleControl(
					"Material screen-space reflections",
					&materialSSR)) {
				cubemaps.EnabledSSR =
					materialSSR ? 1u : 0u;

				globals::pipeline::worldProbes
					.recompileFlag =
						true;

				lightingChanged = true;
			}

			bool waterSSR =
				water.EnableEnhancedSSR !=
					0u;

			if (ToggleControl(
					"Water screen-space reflections",
					&waterSSR)) {
				water.EnableEnhancedSSR =
					waterSSR ? 1u : 0u;

				lightingChanged = true;
			}

			SectionHeading(
				"BLOOM");

			changed |=
				ToggleControl(
					"Bloom enabled",
					&camera.settings
						.enableBloom);

			ImGui::BeginDisabled(
				!camera.settings
					.enableBloom);

			changed |=
				SliderControl(
					"Bloom strength",
					&camera.settings
						.bloomStrength,
					0.0f,
					2.0f,
					"%.2f",
					false);

			changed |=
				SliderControl(
					"Bloom threshold",
					&camera.settings
						.bloomThreshold,
					0.0f,
					4.0f,
					"%.2f",
					false);

			changed |=
				SliderControl(
					"Bloom radius",
					&camera.settings
						.bloomRadius,
					0.0f,
					4.0f,
					"%.2f",
					false);

			ImGui::EndDisabled();

			SectionHeading(
				"WEATHER & WATER LENS");

			changed |=
				ToggleControl(
					"Stormglass rain lens",
					&camera.settings
						.enableStormglass);

			Tooltip(
				"Procedural rain beads and moving trails respond to Skyrim's actual rainfall. The world refracts naturally while HUD text remains untouched.");

			ImGui::BeginDisabled(
				!camera.settings
					.enableStormglass);

			changed |=
				SliderControl(
					"Rain lens presence",
					&camera.settings
						.stormglassStrength,
					0.0f,
					1.0f,
					"%.2f",
					false);

			ImGui::EndDisabled();

			changed |=
				ToggleControl(
					"Submerged optics",
					&camera.settings
						.enableSubmergedOptics);

			Tooltip(
				"Adds a restrained water-type-aware underwater response and a draining wet-lens transition when you break the surface.");

			ImGui::PopID();

			ImGui::EndTable();
		}

		ImGui::PopStyleVar();

		changed |=
			lightingChanged;

		if (changed)
			camera.UpdateHDRData();

		if (lightingChanged) {
			globals::pipeline::hybridGI
				.queuedResetHistory =
					true;

			globals::state->
				UpdateFeatureData(
					globals::state
						->inWorld);
		}

		if (changed)
			QueueDeferredStateSave();
	}

	void DrawFeatureControls()
	{
		auto& gi =
			globals::pipeline::hybridGI.settings;
		auto& water =
			globals::pipeline::waterOptics.settings;
		auto& grass =
			globals::pipeline::groundResponse.settings;
		auto& shadows =
			globals::pipeline::contactShadows.bendSettings;
		auto& terrainOcclusion =
			globals::pipeline::terrainOcclusion.settings;
		auto& pbr =
			globals::pipeline::materialForge.settings;
		auto& cubemaps =
			globals::pipeline::worldProbes.settings;

		bool changed = false;

		if (ImGui::BeginTable(
				"##PIXLFeatureGrid",
				3,
				ImGuiTableFlags_SizingStretchSame |
					ImGuiTableFlags_NoSavedSettings)) {
			ImGui::TableNextColumn();
			ImGui::PushID("LightingFeatures");
			SectionHeading("LIGHT & SHADOW");

			changed |=
				ToggleControl(
					"Global illumination",
					&gi.EnableGI);

			changed |=
				ToggleControl(
					"Hybrid reflections",
					&gi.EnableExperimentalSpecularGI);

			changed |=
				ToggleControl(
					"Persistent world light",
					&gi.EnableWorldCache);

			changed |=
				ToggleControl(
					"Directional occlusion",
					&gi.EnableDirectionalOcclusion);

			bool contactShadows =
				pbr.EnableLocalContactShadows != 0u;

			if (ToggleControl(
					"Local contact shadows",
					&contactShadows)) {
				pbr.EnableLocalContactShadows =
					contactShadows ? 1u : 0u;
				changed = true;
			}

			bool screenShadows =
				shadows.Enable != 0u;

			if (ToggleControl(
					"Screen-space shadows",
					&screenShadows)) {
				shadows.Enable =
					screenShadows ? 1u : 0u;
				changed = true;
			}

			changed |=
				ToggleControl(
					"Terrain shadows",
					&terrainOcclusion.EnableTerrainShadow);

			ImGui::PopID();

			ImGui::TableNextColumn();
			ImGui::PushID("WaterFeatures");
			SectionHeading("WATER & REFLECTIONS");

			bool waterReflections =
				water.EnableEnhancedSSR != 0u;

			if (ToggleControl(
					"Water reflections",
					&waterReflections)) {
				water.EnableEnhancedSSR =
					waterReflections ? 1u : 0u;
				changed = true;
			}

			bool materialSSR =
				cubemaps.EnabledSSR != 0u;

			if (ToggleControl(
					"Material reflections",
					&materialSSR)) {
				cubemaps.EnabledSSR =
					materialSSR ? 1u : 0u;
				globals::pipeline::worldProbes
					.recompileFlag = true;
				changed = true;
			}

			bool waterCaustics =
				water.EnableEnhancedCaustics != 0u;

			if (ToggleControl(
					"Water caustics",
					&waterCaustics)) {
				water.EnableEnhancedCaustics =
					waterCaustics ? 1u : 0u;
				changed = true;
			}

			changed |=
				SliderControl(
					"Caustic presence",
					&water.CausticsStrength,
					0.0f,
					1.5f,
					"%.2fx");

			ImGui::PopID();

			ImGui::TableNextColumn();
			ImGui::PushID("InteractionFeatures");
			SectionHeading("WORLD INTERACTION");

			changed |=
				ToggleControl(
					"Grass interaction",
					&grass.EnableGroundResponse);

			changed |=
				ToggleControl(
					"Ground deformation",
					&grass.EnableDeformableGround);

			ImGui::BeginDisabled(
				!grass.EnableDeformableGround);

			changed |=
				ToggleControl(
					"Snow deformation",
					&grass.EnableSnowDeformation);

			changed |=
				ToggleControl(
					"Mud deformation",
					&grass.EnableMudDeformation);

			ImGui::EndDisabled();

			SectionHeading("FINISHING");

			changed |=
				SliderControl(
					"Indirect light",
					&gi.GIStrength,
					0.70f,
					1.30f,
					"%.2fx");

			changed |=
				SliderControl(
					"Reflection presence",
					&gi.ReflectionIntensity,
					0.70f,
					1.30f,
					"%.2fx");

			Tooltip(
				"These narrow-range controls are deliberate finishing offsets around the validated PIXL baseline.");

			ImGui::PopID();
			ImGui::EndTable();
		}

		if (changed) {
			globals::pipeline::hybridGI
				.recompileFlag = true;
			globals::pipeline::hybridGI
				.queuedResetHistory = true;
			globals::state->UpdateFeatureData(
				globals::state->inWorld);
			QueueDeferredStateSave();
		}
	}

	void DrawCameraControls()
	{
		std::string directorUnavailableReason;
		const bool directorActive =
			TuningWorkspaceRenderer::IsDirectorPhotoModeActive();
		const bool directorAvailable = directorActive ||
			TuningWorkspaceRenderer::IsDirectorPhotoModeAvailable(
				&directorUnavailableReason);

		SectionHeading("PIXL DIRECTOR");
		ImGui::TextColored(
			PIXLUI::ToVec4(PIXLUI::Colors::TextMuted),
			"Cinematic free camera, live shot controls and clean high-quality capture.");
		ImGui::SameLine();
		ImGui::BeginDisabled(!directorAvailable);
		if (PIXLUI::ActionButton(
				directorActive ? "RETURN TO DIRECTOR" : "OPEN PIXL DIRECTOR",
				ImVec2(PIXLUI::Ref(190.0f), PIXLUI::Ref(32.0f)),
				true)) {
			TuningWorkspaceRenderer::OpenDirectorPhotoMode();
		}
		ImGui::EndDisabled();
		if (!directorAvailable && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::TextWrapped("%s", directorUnavailableReason.c_str());
		}
		ImGui::Dummy(ImVec2(0, PIXLUI::Ref(5.0f)));
		DrawFinishingControls();
	}

	void DrawStatusPill(
		const char* label,
		const char* value,
		ImU32 color)
	{
		ImGui::BeginGroup();
		ImGui::TextColored(
			PIXLUI::ToVec4(color),
			"%s",
			value);
		ImGui::TextColored(
			PIXLUI::ToVec4(
				PIXLUI::Colors::TextDim),
			"%s",
			label);
		ImGui::EndGroup();
	}

	void DrawRendererSettings()
	{
		const auto [loaded, installed] =
			CountPipelineFeatures();

		auto* cache =
			globals::shaderCache;

		bool enabled =
			cache->IsEnabled();

		// FINAL PIXL RENDERER PAGE
		// The active top tab already names the page; start directly with the
		// system state instead of repeating a title/subtitle and pipeline label.
		SectionHeading("SYSTEM");

		if (ImGui::BeginTable(
				"##RendererStatus",
				3,
				ImGuiTableFlags_SizingStretchSame |
					ImGuiTableFlags_NoSavedSettings)) {
			ImGui::TableNextColumn();

			DrawStatusPill(
				"PIPELINE",
				enabled ? "ACTIVE" : "VANILLA",
				enabled
					? PIXLUI::Colors::Success
					: PIXLUI::Colors::TextDim);

			ImGui::TableNextColumn();

			const auto modules =
				std::format(
					"{} / {}",
					loaded,
					installed);

			DrawStatusPill(
				"SYSTEMS",
				modules.c_str(),
				loaded == installed
					? PIXLUI::Colors::Success
					: PIXLUI::Colors::Warning);

			ImGui::TableNextColumn();

			// Keep the master pipeline switch on the same baseline as READY /
			// PREPARING. This is the authoritative system on/off control.
			if (ImGui::BeginTable(
					"##ShaderStatusAndPipeline",
					2,
					ImGuiTableFlags_SizingStretchProp |
						ImGuiTableFlags_NoSavedSettings)) {
				ImGui::TableSetupColumn(
					"Status",
					ImGuiTableColumnFlags_WidthStretch,
					1.0f);
				ImGui::TableSetupColumn(
					"Pipeline",
					ImGuiTableColumnFlags_WidthFixed,
					PIXLUI::Ref(72.0f));

				ImGui::TableNextColumn();

				DrawStatusPill(
					"SHADERS",
					cache->IsCompiling()
						? "PREPARING"
						: "READY",
					cache->IsCompiling()
						? PIXLUI::Colors::Warning
						: PIXLUI::Colors::Success);

				ImGui::TableNextColumn();

				if (PIXLUI::Toggle(
						"##PIXLRenderingPipeline",
						&enabled)) {
					cache->SetEnabled(enabled);
					SetPipelineForNextBoot(enabled);
					globals::state->Save();
				}

				Tooltip(
					"Turns the complete PIXL rendering pipeline on or off for the next renderer session.");

				ImGui::EndTable();
			}

			ImGui::EndTable();
		}

		if (enabled &&
			loaded < installed) {
			ImGui::TextColored(
				PIXLUI::ToVec4(
					PIXLUI::Colors::Warning),
				"Restart Skyrim once to finish activating all PIXL systems.");
		}

		ImGui::Dummy(
			ImVec2(
				0,
				PIXLUI::Ref(5.0f)));

		DrawPerformanceControls();

		ImGui::Dummy(
			ImVec2(
				0,
				PIXLUI::Ref(12.0f)));

		SectionHeading("LOOK");

		if (PIXLUI::ActionButton(
				"SAVE LOOK",
				ImVec2(
					PIXLUI::Ref(150.0f),
					PIXLUI::Ref(34.0f)),
				true)) {
			globals::state->Save();
			globals::state->SaveTheme();
		}

		ImGui::SameLine(
			0.0f,
			PIXLUI::Ref(8.0f));

		if (PIXLUI::ActionButton(
				"RESTORE",
				ImVec2(
					PIXLUI::Ref(130.0f),
					PIXLUI::Ref(34.0f)),
				false)) {
			globals::state->Load();
		}

		ImGui::Dummy(
			ImVec2(
				0,
				PIXLUI::Ref(14.0f)));

		SectionHeading("ENGINEERING");

		ImGui::TextColored(
			PIXLUI::ToVec4(
				PIXLUI::Colors::TextMuted),
			"Module-level renderer controls, diagnostics and shader maintenance.");

		ImGui::Dummy(
			ImVec2(
				0,
				PIXLUI::Ref(8.0f)));

		if (PIXLUI::ActionButton(
				"OPEN PIXL TUNER",
				ImVec2(
					PIXLUI::Ref(190.0f),
					PIXLUI::Ref(36.0f)),
				false)) {
			globals::menu
				->GetSettings()
				.AdvancedMode = true;

			globals::state->Save();
		}

		ImGui::SameLine(
			0.0f,
			PIXLUI::Ref(8.0f));

		if (PIXLUI::ActionButton(
				"REBUILD SHADERS",
				ImVec2(
					PIXLUI::Ref(175.0f),
					PIXLUI::Ref(36.0f)),
				false)) {
			Util::RequestClearShaderCacheConfirmation();
		}

		Tooltip(
			"Clears PIXL's shader library and rebuilds it from the installed renderer source.");
	}

}

void PIXLRendererPage::Render()
{
	static PublicPage currentPage =
		PublicPage::Quality;

	const float gap =
		PIXLUI::Ref(8.0f);

	const float pageWidth =
		std::max(
			PIXLUI::Ref(170.0f),
			(ImGui::GetContentRegionAvail().x -
				gap * 2.0f) /
				3.0f);

	if (PIXLUI::PageButton(
			"Quality",
			"QUALITY",
			currentPage == PublicPage::Quality,
			ImVec2(
				pageWidth,
				PIXLUI::Ref(40.0f)))) {
		currentPage =
			PublicPage::Quality;
	}

	ImGui::SameLine(
		0.0f,
		gap);

	if (PIXLUI::PageButton(
			"Camera",
			"CAMERA + POST FX",
			currentPage == PublicPage::Camera,
			ImVec2(
				pageWidth,
				PIXLUI::Ref(40.0f)))) {
		currentPage =
			PublicPage::Camera;
	}

	ImGui::SameLine(
		0.0f,
		gap);

	if (PIXLUI::PageButton(
			"Renderer",
			"PIXL RENDERER",
			currentPage == PublicPage::Renderer,
			ImVec2(
				pageWidth,
				PIXLUI::Ref(40.0f)))) {
		currentPage =
			PublicPage::Renderer;
	}

	ImGui::Dummy(
		ImVec2(
			0,
			PIXLUI::Ref(10.0f)));

	ImGui::PushStyleColor(
		ImGuiCol_ChildBg,
		ImVec4(0, 0, 0, 0));

	const ImGuiWindowFlags publicPageFlags =
		currentPage == PublicPage::Renderer
			? ImGuiWindowFlags_None
			: ImGuiWindowFlags_NoScrollbar |
				ImGuiWindowFlags_NoScrollWithMouse;

	if (ImGui::BeginChild(
			"##PIXLPublicPage",
			ImVec2(0, 0),
			ImGuiChildFlags_None,
			publicPageFlags)) {
		switch (currentPage) {
		case PublicPage::Quality: {
			DrawPageIntro(
				"QUALITY",
				nullptr);

			DrawQualityControls();

			ImGui::Dummy(
				ImVec2(
					0,
					PIXLUI::Ref(2.0f)));

			SectionHeading("FEATURES");

			const ImVec2 featureSpacing =
				ImGui::GetStyle().ItemSpacing;
			ImGui::PushStyleVar(
				ImGuiStyleVar_ItemSpacing,
				ImVec2(
					featureSpacing.x,
					PIXLUI::Ref(1.0f)));

			DrawFeatureControls();

			ImGui::PopStyleVar();
			break;
		}

		case PublicPage::Camera:
			DrawCameraControls();
			break;

		case PublicPage::Renderer:
			DrawRendererSettings();
			break;
		}
	}

	ImGui::EndChild();
	ImGui::PopStyleColor();

	FlushDeferredStateSave();
}
