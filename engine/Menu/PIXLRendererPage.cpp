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
#include <shellapi.h>

#include "RenderModule.h"
#include "Modules/CameraSuite.h"
#include "Renderer/ExternalPostProcessing.h"
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
#include "Menu/LaunchExperienceRenderer.h"
#include "Renderer/QualityProfiles.h"
#include "ShaderCache.h"
#include "State.h"
#include "MaterialForge.h"
#include "Util.h"

namespace
{
	void WrappedTintedText(ImU32 color, const char* text)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, PIXLUI::ToVec4(color));
		ImGui::TextWrapped("%s", text);
		ImGui::PopStyleColor();
	}

	constexpr std::array<const char*, 4> kProfileNames{
		"LOW",
		"MEDIUM",
		"HIGH",
		"CINEMATIC"
	};

	constexpr std::array<const char*, 4> kDetailNames{
		"LOW",
		"MEDIUM",
		"HIGH",
		"CINEMATIC"
	};

	constexpr std::array<const char*, 4> kProfileDescriptions{
		"The PIXL baseline with core lighting, materials and atmosphere intact, tuned aggressively for performance.",
		"A balanced version of every major PIXL system with reduced rays, froxels, geometry and distant foliage cost.",
		"The former Cinematic presentation: the complete release look and recommended target for powerful gameplay systems.",
		"An intentionally extreme capture/high-end tier with up to three-times ray, bokeh and surface budgets."
	};

	using QualityGroup =
		PIXLRenderer::QualityProfiles::Group;

	struct QualityPreviewSelection
	{
		const char* assetKey = "Profile";
		const char* title = "HIGH";
		const char* description = kProfileDescriptions[2];
		int tier = 2;
	};

	struct QualityPreviewTexture
	{
		winrt::com_ptr<ID3D11ShaderResourceView> srv;
		ImVec2 size{};
	};

	std::unordered_map<std::string, QualityPreviewTexture>
		g_qualityPreviewTextures;
	ID3D11Device* g_qualityPreviewDevice = nullptr;  // Identity only; SRVs own the resources.

	const char* QualityTierName(int tier)
	{
		return kDetailNames[
			std::clamp(
				tier,
				0,
				3)];
	}

	const char* QualityPreviewAssetTierName(int tier)
	{
		// Preserve the existing authored top-tier artwork. The public tier was
		// renamed from Ultra to Cinematic in the v2 quality contract, but preview
		// assets remain *_ULTRA.png until replacement captures are authored.
		return std::clamp(tier, 0, 3) == 3 ? "ULTRA" : QualityTierName(tier);
	}

	QualityPreviewTexture* GetQualityPreviewTexture(
		const char* assetKey,
		int tier);

	void DrawQualityImageTooltip(
		const char* assetKey,
		int tier,
		const char* description)
	{
		if (!ImGui::IsItemHovered())
			return;

		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(PIXLUI::Ref(520.0f));
		if (description)
			ImGui::TextWrapped("%s", description);

		if (auto* texture = GetQualityPreviewTexture(assetKey, tier);
			texture && texture->srv && texture->size.x > 0.0f && texture->size.y > 0.0f) {
			ImGui::Dummy(ImVec2(0.0f, PIXLUI::Ref(4.0f)));
			const float width = PIXLUI::Ref(500.0f);
			const float height = width * texture->size.y / texture->size.x;
			ImGui::Image(texture->srv.get(), ImVec2(width, height));
		}
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
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
		if (g_qualityPreviewDevice != globals::d3d::device) {
			g_qualityPreviewTextures.clear();
			g_qualityPreviewDevice = globals::d3d::device;
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
					QualityPreviewAssetTierName(tier),
					QualityPreviewAssetTierName(tier) +
						std::char_traits<char>::length(
							QualityPreviewAssetTierName(tier))) +
				L".png");

		// Cache misses as well as successes. Missing optional art must not cause
		// filesystem probes or repeated image decoding on every visible frame.
		auto [cached, inserted] = g_qualityPreviewTextures.try_emplace(cacheKey);
		static_cast<void>(inserted);
		std::error_code error;
		if (!std::filesystem::is_regular_file(path, error) || error)
			return &cached->second;

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

		cached->second = std::move(texture);
		return &cached->second;
	}

	bool NeuralRuntimeFilePresent()
	{
		std::error_code error;
		const std::filesystem::path path = L"Data/Shaders/ImageReconstruction/Streamline/nvngx_dlssnr.dll";
		return std::filesystem::is_regular_file(path, error) && !error &&
			std::filesystem::file_size(path, error) > 0 && !error;
	}

	void DrawNeuralSetupCard(bool canEnable, bool* quickSetupConfirmed = nullptr)
	{
		const auto* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowSize(ImVec2(std::min(PIXLUI::Ref(880.0f), viewport->WorkSize.x - 32.0f),
			std::min(PIXLUI::Ref(820.0f), viewport->WorkSize.y - 32.0f)), ImGuiCond_Appearing);
		ImGui::SetNextWindowPos(viewport->GetWorkCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		bool open = true;
		if (!ImGui::BeginPopupModal("NEURAL RENDERING | SETUP", &open, ImGuiWindowFlags_NoSavedSettings))
			return;
		static bool checked = false;
		static bool found = false;
		static bool browserFailed = false;
		static bool saved = false;
		if (ImGui::IsWindowAppearing()) {
			checked = found = saved = browserFailed = false;
		}
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextColored(PIXLUI::ToVec4(PIXLUI::Colors::CyanBright), "YOUR OPTIONAL NEURAL RENDERING SETUP");
		ImGui::TextWrapped("Four steps, then return here. PIXL does not download or install third-party DLLs.");
		if (PIXLUI::ActionButton("OPEN RENODX DISCORD", ImVec2(PIXLUI::Ref(230.0f), PIXLUI::Ref(32.0f)), true)) {
			// Fixed HTTPS destination; never execute user-supplied URLs or commands.
			browserFailed = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open",
				L"https://discord.gg/renodx", nullptr, nullptr, SW_SHOWNORMAL)) <= 32;
		}
		if (browserFailed) {
			ImGui::TextWrapped("Could not open your browser. Visit https://discord.gg/renodx manually.");
		}
		ImGui::TextColored(PIXLUI::ToVec4(PIXLUI::Colors::Warning), "EXPERIMENTAL THIRD-PARTY SOFTWARE");
		ImGui::TextWrapped("The community build is not an NVIDIA-approved PIXL download. Obtain and use it only with the necessary permissions. These reference screenshots may change as Discord is updated.");
		const float footer = PIXLUI::Ref(260.0f);
		if (ImGui::BeginChild("##NRSetupSteps", ImVec2(0, std::max(PIXLUI::Ref(110.0f), ImGui::GetContentRegionAvail().y - footer)), ImGuiChildFlags_None)) {
			const char* titles[] = { "01  OPEN THE FORUM", "02  PINNED / SHORTFUSE ONLY", "03  DOWNLOAD THE DLL", "04  COPY INTO SKYRIM" };
			const char* instructions[] = {
				"Open dlss5-forum, then the Patched DLSS-NR thread shown below.",
				"Use ONLY the Pinned Messages tab. Find ShortFuse's pinned Patched DLSS-NR post (highlighted). Use ONLY that version, not replies, reposts or other builds.",
				"Download nvngx_dlssnr.dll attached to that pinned ShortFuse post. PIXL does not fetch the file or verify its publisher.",
				"If supplied as an archive, extract it first. Copy nvngx_dlssnr.dll into your Skyrim Special Edition installation at the path below. Do not overwrite other Streamline files."
			};
			const int columns = ImGui::GetContentRegionAvail().x >= PIXLUI::Ref(650.0f) ? 2 : 1;
			if (ImGui::BeginTable("##NRSetupCards", columns, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV)) {
				for (int i = 0; i < 4; ++i) {
					ImGui::TableNextColumn();
					ImGui::PushID(i);
					ImGui::TextColored(PIXLUI::ToVec4(i == 1 ? PIXLUI::Colors::CyanBright : PIXLUI::Colors::CyanSoft), "%s", titles[i]);
					ImGui::TextWrapped("%s", instructions[i]);
					if (auto* image = GetQualityPreviewTexture("NRSetup", i); image && image->srv && image->size.x > 0 && image->size.y > 0) {
						const float scale = std::min(ImGui::GetContentRegionAvail().x / image->size.x, PIXLUI::Ref(250.0f) / image->size.y);
						const ImVec2 size(image->size.x * scale, image->size.y * scale);
						const ImVec2 origin = ImGui::GetCursorScreenPos();
						ImGui::Image(image->srv.get(), size);
						if (i == 1) {
							ImGui::GetWindowDrawList()->AddRect(ImVec2(origin.x + size.x * 0.035f, origin.y + size.y * 0.59f),
								ImVec2(origin.x + size.x * 0.985f, origin.y + size.y * 0.99f), PIXLUI::Colors::CyanBright, 3.0f, 0, 2.0f);
						}
						if (ImGui::IsItemHovered()) {
							ImGui::BeginTooltip();
							const float zoom = std::min(1.0f, std::min((viewport->WorkSize.x - 64.0f) / image->size.x, (viewport->WorkSize.y - 64.0f) / image->size.y));
							ImGui::Image(image->srv.get(), ImVec2(image->size.x * zoom, image->size.y * zoom));
							ImGui::EndTooltip();
						}
					} else {
						ImGui::TextDisabled("Reference image unavailable; follow the instructions above.");
					}
					ImGui::Spacing();
					ImGui::PopID();
				}
				ImGui::EndTable();
			}
		}
		ImGui::EndChild();
		ImGui::Separator();
		ImGui::TextWrapped("Skyrim Special Edition / Data / Shaders / ImageReconstruction / Streamline / nvngx_dlssnr.dll");
		if (ImGui::Button("I'VE COPIED IT - CHECK FILE")) {
			found = NeuralRuntimeFilePresent();
			checked = true;
			saved = false;
		}
		if (checked) {
			ImGui::PushStyleColor(ImGuiCol_Text, PIXLUI::ToVec4(found ? PIXLUI::Colors::Success : PIXLUI::Colors::Danger));
			ImGui::TextWrapped("%s", found ? "File found. Presence only: compatibility, authenticity and licensing are not verified." : "File not found at the destination above. Check the folder, filename and extraction, then retry.");
			ImGui::PopStyleColor();
		}
		if (checked && found && canEnable)
			ImGui::TextColored(PIXLUI::ToVec4(PIXLUI::Colors::CyanBright), "READY | Confirm below, then restart after saving settings.");
		ImGui::BeginDisabled(!checked || !found || !canEnable);
		if (ImGui::Button(quickSetupConfirmed ? "CONFIRM NR SETUP & RETURN" : "SAVE NR ENABLED FOR NEXT LAUNCH")) {
			// Recheck after returning from the browser; never load/execute the DLL
			// merely to inspect it, and do not rely on an earlier presence result.
			found = NeuralRuntimeFilePresent();
			if (found) {
				if (quickSetupConfirmed) {
					*quickSetupConfirmed = true;
					ImGui::CloseCurrentPopup();
				} else {
					globals::pipeline::imageReconstruction.settings.neuralRenderingEnabled = true;
					globals::state->Save();
					saved = true;
				}
			}
		}
		ImGui::EndDisabled();
		if (!canEnable)
			ImGui::TextWrapped("Select DLSS on a PIXL-supported NVIDIA adapter before enabling NR.");
		ImGui::TextWrapped("%s", quickSetupConfirmed ? "After checking the file, confirm and return to Quick Setup. Save & Continue applies your graphics choices together, then offers the required restart." : saved ? "Saved. Save your game, fully exit Skyrim, then launch it again. The DX11/DX12 sidecar is initialized at startup; reloading a save is not enough." : "After copying, return here to check the file and save NR enabled. A full game restart is required; PIXL will not quit the game for you.");
		if (ImGui::Button("CLOSE"))
			ImGui::CloseCurrentPopup();
		ImGui::PopTextWrapPos();
		ImGui::EndPopup();
	}

	void DrawQualityPreview(
		const QualityPreviewSelection& preview)
	{
		PIXLUI::ChromeScope panel(
			"##PIXLQualityPreview",
			ImVec2(
				0,
				PIXLUI::Ref(365.0f)),
			PIXLUI::ChromeStyle::Raised,
			true,
			ImGuiWindowFlags_NoScrollbar |
				ImGuiWindowFlags_NoScrollWithMouse,
			12.0f);

		if (!panel)
			return;

		// Keep the profile summary in one compact band so the image can use
		// the same vertical space as the quality controls beside it.
		const ImVec2 headerStart =
			ImGui::GetCursorScreenPos();
		const float headerWidth =
			ImGui::GetContentRegionAvail().x;
		const float headerHeight =
			PIXLUI::Ref(26.0f);
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
		if (std::string_view(preview.assetKey) == "Profile") {
			std::snprintf(context, sizeof(context), "%s", preview.title);
		} else {
			std::snprintf(
				context,
				sizeof(context),
				"%s / %s",
				preview.title,
				QualityTierName(preview.tier));
		}
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

		const float summaryX = headerStart.x + headerPadX +
			headingSize.x + PIXLUI::Ref(7.0f) + contextSize.x + PIXLUI::Ref(14.0f);
		const float summaryWidth = headerStart.x + headerWidth - headerPadX - summaryX;
		if (summaryWidth > PIXLUI::Ref(56.0f)) {
			const char* summary = preview.description;
			const size_t length = std::char_traits<char>::length(summary);
			const float ellipsisWidth = ImGui::CalcTextSize("...").x;
			size_t visible = length;
			if (ImGui::CalcTextSize(summary).x > summaryWidth) {
				size_t low = 0;
				size_t high = length;
				while (low < high) {
					const size_t middle = (low + high + 1) / 2;
					if (ImGui::CalcTextSize(summary, summary + middle).x + ellipsisWidth <= summaryWidth)
						low = middle;
					else
						high = middle - 1;
				}
				visible = low;
			}
			draw->AddText(ImVec2(summaryX, textY), PIXLUI::Colors::TextMuted,
				summary, summary + visible);
			if (visible < length)
				draw->AddText(ImVec2(summaryX + ImGui::CalcTextSize(summary, summary + visible).x,
					textY), PIXLUI::Colors::TextMuted, "...");
		}
		ImGui::SetCursorScreenPos(headerStart);
		ImGui::InvisibleButton("##QualityPreviewSummary", ImVec2(headerWidth, headerHeight));
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", preview.description);
		ImGui::Dummy(ImVec2(0, PIXLUI::Ref(3.0f)));

		const ImVec2 imageStart =
			ImGui::GetCursorScreenPos();
		const ImVec2 available =
			ImGui::GetContentRegionAvail();

		const ImVec2 imageArea(
			available.x,
			std::max(PIXLUI::Ref(260.0f), available.y));

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
			ImVec2 drawSize = imageArea;
			if (imageArea.x / imageArea.y > sourceAspect)
				drawSize.x = imageArea.y * sourceAspect;
			else
				drawSize.y = imageArea.x / sourceAspect;
			ImGui::SetCursorScreenPos(ImVec2(
				imageStart.x + (imageArea.x - drawSize.x) * 0.5f,
				imageStart.y + (imageArea.y - drawSize.y) * 0.5f));
			ImGui::Image(texture->srv.get(), drawSize);
			ImGui::SetCursorScreenPos(imageStart);
			ImGui::Dummy(imageArea);
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

	void FlushDeferredStateSave(bool force = false)
	{
		if (!g_deferredStateSave)
			return;

		// Slider values and renderer constant data remain live every frame.
		// Only the JSON/disk commit waits for the user's interaction to end.
		if (!force && (ImGui::IsMouseDown(
				ImGuiMouseButton_Left) ||
			ImGui::IsAnyItemActive())) {
			return;
		}

		if (globals::state) {
			globals::state->Save();
			g_deferredStateSave = false;
		}
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
		int count,
		int* hoveredIndex = nullptr)
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

			if (hoveredIndex && ImGui::IsItemHovered())
				*hoveredIndex = i;
		}

		ImGui::PopID();
		return changed;
	}

	bool DrawQualitySlider(
		const char* label,
		int& value,
		const char* description)
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

		if (description && hovered)
			ImGui::SetTooltip("%s", description);

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
				"Water-reflection ray count, hit refinement, reach and edge stability. Caustic colour remains user-authored." },
			Row{
				"Terrain & Vegetation",
				"TerrainVegetation",
				&settings.TerrainVegetationQuality,
				QualityGroup::TerrainVegetation,
				"Snow/mud tessellation and history plus grass density, projected-size LOD, mesh LOD and collision range." },
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
				"Exposure metering, local adaptation, physical DOF bokeh and motion-blur sample budgets; the authored camera look is unchanged." }
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
				1.08f);

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

			int hoveredProfile = -1;
			if (DrawChoiceButtons(
					"GlobalQuality",
					profile,
					kProfileNames.data(),
					4,
					&hoveredProfile)) {
				settings.RendererQuality =
					profile;

				PIXLRenderer::QualityProfiles::
					ApplyGlobal(
						profile);
			}

			const int previewProfile = std::clamp(
				hoveredProfile >= 0 ? hoveredProfile : settings.RendererQuality,
				0,
				3);
			preview.assetKey = "Profile";
			preview.title = kProfileNames[previewProfile];
			preview.description = kProfileDescriptions[previewProfile];
			preview.tier = previewProfile;

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
						"RESTORE PROFILE",
						ImVec2(
							PIXLUI::Ref(145.0f),
							PIXLUI::Ref(27.0f)),
						false)) {
					PIXLRenderer::QualityProfiles::
						ApplyGlobal(
							settings.RendererQuality);
				}
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip(
						"Reapply the %s profile to all system details and its renderer settings. This replaces custom adjustments.",
						kProfileNames[profile]);
				}
			}
			ImGui::TextWrapped("Advanced changes remain as CUSTOM until their selected contract is reapplied. A global profile replaces all seven workload groups, but never artistic strength or colour controls.");

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
						row.help)) {
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
							"Advanced workload values differ from the selected %s contract.",
							QualityTierName(*row.value));
					}
					ImGui::SameLine(0.0f, PIXLUI::Ref(7.0f));
					if (ImGui::SmallButton("REAPPLY")) {
						PIXLRenderer::QualityProfiles::Apply(row.group, *row.value);
						QueueDeferredStateSave();
					}
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("Restore only this system to its selected %s workload contract.", QualityTierName(*row.value));
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

		static bool showAdvancedReconstruction = false;

		SectionHeading("IMAGE RECONSTRUCTION");

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

		// Keep the active delivery path visible before the detailed controls. This
		// is deliberately compact: it explains the current state without turning
		// the user-facing page into the engineering tuner.
		{
			PIXLUI::PanelScope reconstructionSummary("##ReconstructionSummary", ImVec2(0, PIXLUI::Ref(50.0f)), true);
			if (reconstructionSummary && ImGui::BeginTable("##ReconstructionSummaryTable", 2,
				ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
				ImGui::TableNextColumn();
				ImGui::TextColored(PIXLUI::ToVec4(PIXLUI::Colors::CyanBright), "%s", methods[methodValue]);
				ImGui::TextColored(PIXLUI::ToVec4(PIXLUI::Colors::TextDim), "ACTIVE RECONSTRUCTION");
				ImGui::TableNextColumn();
				ImGui::TextColored(PIXLUI::ToVec4(PIXLUI::Colors::TextMuted), "RESTART AFTER PATH CHANGE");
				ImGui::TextColored(PIXLUI::ToVec4(PIXLUI::Colors::TextDim), "Current delivery remains stable while you browse.");
				ImGui::EndTable();
			}
		}

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

		const bool dlssSelected =
			*method == static_cast<uint>(ImageReconstruction::UpscaleMethod::kDLSS);
		if (dlssSelected) {
			const char* dlssPresets[] = { "DEFAULT", "PRESET J", "PRESET K", "PRESET F" };
			int preset = settings.presetDLSS == 1u ? 1 :
				settings.presetDLSS == 2u ? 2 : settings.presetDLSS == 5u ? 3 : 0;
			if (CycleControl(
					"DLSS model",
					&preset,
					dlssPresets,
					static_cast<int>(std::size(dlssPresets)),
					"Selects a model preset supported by the installed NVIDIA Streamline runtime. Changing it requires a restart.")) {
				constexpr uint storedPresets[]{ 0u, 1u, 2u, 5u };
				settings.presetDLSS = storedPresets[std::clamp(preset, 0, 3)];
				changed = restartNeeded = true;
			}

			changed |= ToggleControl(
				"DLSS sharpening",
				&settings.sharpnessEnabledDLSS,
				"Optional restrained RCAS sharpening after DLSS. Leave disabled unless the selected reconstruction looks soft.");
			if (settings.sharpnessEnabledDLSS)
				changed |= SliderControl("DLSS sharpness", &settings.sharpnessDLSS, 0.0f, 1.0f, "%.2f", false);
		} else if (*method == static_cast<uint>(ImageReconstruction::UpscaleMethod::kFSR)) {
			changed |= SliderControl(
				"FSR sharpness",
				&settings.sharpnessFSR,
				0.0f,
				1.0f,
				"%.2f",
				false);
		}

		// Neural reconstruction and frame delivery are independent runtime paths.
		// Keep their normal controls side-by-side; advanced controls still expand
		// in their owning column only when explicitly requested.
		if (ImGui::BeginTable("##PIXLReconstructionDeliveryGrid", 2,
			ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
			ImGui::TableNextColumn();
			ImGui::PushID("NeuralRenderingColumn");
		SectionHeading("NEURAL RENDERING");
		const bool nrHardwareSupported =
			imageReconstruction.streamline.neuralRenderingSupportedOnCurrentAdapter;
		const bool nrSessionProvisioned =
			imageReconstruction.d3d12SwapChainActive &&
			imageReconstruction.neuralRenderingProvisionedAtBoot;
		const bool nrControlAvailable = nrHardwareSupported && dlssSelected;
		PIXLUI::StatusPill(
			nrControlAvailable ? "DLSS READY" : "RTX 30+ / DLSS REQUIRED",
			nrControlAvailable ? PIXLUI::Colors::Success : PIXLUI::Colors::Warning);

		ImGui::BeginDisabled(!nrControlAvailable);
		if (ToggleControl(
				"Neural Rendering",
				&settings.neuralRenderingEnabled)) {
			if (settings.neuralRenderingEnabled && !NeuralRuntimeFilePresent()) {
				settings.neuralRenderingEnabled = false;
				ImGui::OpenPopup("NEURAL RENDERING | SETUP");
			}
			imageReconstruction.pendingNeuralRenderingReset.store(true, std::memory_order_release);
			changed = true;
			if (!nrSessionProvisioned)
				restartNeeded = true;
		}
		DrawQualityImageTooltip(
			"Neural",
			3,
			"DLSS Neural Rendering adds the Ultra+ finish shown here. It requires NVIDIA RTX 30-series or newer hardware and an active DLSS session. Ctrl+N toggles it during gameplay; the first sidecar activation may require one restart.");
		ImGui::EndDisabled();

		if (PIXLUI::ActionButton("NR SETUP GUIDE", ImVec2(PIXLUI::Ref(180.0f), PIXLUI::Ref(30.0f)), false))
			ImGui::OpenPopup("NEURAL RENDERING | SETUP");
		DrawNeuralSetupCard(nrControlAvailable);

		if (!nrHardwareSupported) {
			WrappedTintedText(PIXLUI::Colors::Warning,
				"NR REQUIRES AN NVIDIA RTX 30-SERIES GPU OR NEWER");
			WrappedTintedText(PIXLUI::Colors::TextDim,
				"AMD, Intel and RTX 20-series adapters use PIXL's TAA/FSR/DLSS paths without Neural Rendering.");
		} else if (!dlssSelected) {
			WrappedTintedText(PIXLUI::Colors::TextDim,
				"Select DLSS and restart once to provision the PIXL DX12 sidecar. NR can then be toggled live.");
		} else if (!nrSessionProvisioned) {
			WrappedTintedText(PIXLUI::Colors::Warning,
				"RESTART ONCE TO PROVISION THE DLSS NEURAL SIDECAR");
		} else {
			WrappedTintedText(PIXLUI::Colors::Success,
				"NEURAL SIDECAR READY - REAL-TIME AND PHOTO TOGGLES ARE LIVE");
		}
		WrappedTintedText(PIXLUI::Colors::TextDim,
			"SHIFT+N QUICK TOGGLE  |  DLSS SR > NR COMPOSITE > FRAME DELIVERY > UI. Rebind in Hotkeys.");
		Tooltip(
			"The installed Feature 18 contract consumes display-resolution, post-DLSS colour plus render-resolution depth and motion guides. A pre-DLSS mode would instead receive Skyrim's linear HDR render target, require a hard DX12-to-DX11 hand-back every frame, and invalidate the model's validated colour/extent contract. PIXL therefore keeps the stable gameplay order. Photo Finish accumulates synchronized completed neural frames and performs its larger offline output reconstruction afterward.");

		ImGui::BeginDisabled(!nrControlAvailable);
		const char* neuralPresets[] = { "NATURAL", "BALANCED", "DETAIL", "STRONG", "CUSTOM" };
		int neuralPreset = static_cast<int>(std::min(settings.neuralRenderingPreset, 4u));
		if (CycleControl(
				"NR look",
				&neuralPreset,
				neuralPresets,
				static_cast<int>(std::size(neuralPresets)),
				"Natural is the restrained release-safe starting point. Strong is intentionally experimental.")) {
			imageReconstruction.ApplyNeuralRenderingPreset(static_cast<uint>(neuralPreset));
			changed = true;
		}
		if (SliderControl("NR intensity", &settings.neuralRenderingIntensity, 0.0f, 2.0f, "%.2f", false)) {
			settings.neuralRenderingPreset = 4u;
			imageReconstruction.pendingNeuralRenderingReset.store(true, std::memory_order_release);
			changed = true;
		}
		ImGui::EndDisabled();

		ToggleControl(
			"Advanced image controls",
			&showAdvancedReconstruction,
			"Shows model conditioning, frame-generation compatibility, and latency controls. Normal users can leave these at their defaults.");
		if (showAdvancedReconstruction) {
			ImGui::PushID("AdvancedImageControls");
			ImGui::Indent(PIXLUI::Ref(14.0f));
			ImGui::BeginDisabled(!nrControlAvailable);
			const char* nrContracts[] = {
				"FOLLOW DLSS", "DLAA", "QUALITY", "BALANCED", "PERFORMANCE", "ULTRA PERFORMANCE", "ULTRA QUALITY"
			};
			int nrContract = static_cast<int>(std::min(settings.neuralRenderingQualityMode, 6u));
			if (CycleControl("NR quality contract", &nrContract, nrContracts,
				static_cast<int>(std::size(nrContracts)),
				"Selects the model quality contract at Feature 18 creation. This is not a second output-resolution control.")) {
				settings.neuralRenderingQualityMode = static_cast<uint>(nrContract);
				changed = restartNeeded = true;
			}
			const char* nrOutputs[] = { "RUNTIME DEFAULT", "PRESET 1", "PRESET 2", "PRESET 3" };
			int nrOutput = static_cast<int>(std::min(settings.neuralRenderingOutputPreset, 3u));
			if (CycleControl("NR output preset", &nrOutput, nrOutputs,
				static_cast<int>(std::size(nrOutputs)),
				"Private runtime presets are numbered because NVIDIA does not publish stable semantic names for them.")) {
				settings.neuralRenderingOutputPreset = static_cast<uint>(nrOutput);
				changed = restartNeeded = true;
			}
			const auto markNeuralCustom = [&]() {
				settings.neuralRenderingPreset = 4u;
				imageReconstruction.pendingNeuralRenderingReset.store(true, std::memory_order_release);
				changed = true;
			};
			if (SliderControl("Local tone", &settings.neuralRenderingLocalTone, 0.0f, 2.0f, "%.2f", false)) markNeuralCustom();
			if (SliderControl("Local structure", &settings.neuralRenderingLocalStructure, 0.0f, 2.0f, "%.2f", false)) markNeuralCustom();
			if (SliderControl("Skin structure", &settings.neuralRenderingSkinStructure, 0.0f, 2.0f, "%.2f", false)) markNeuralCustom();
			int nrStyle = static_cast<int>(std::min(settings.neuralRenderingStyle, 3u));
			const char* styles[] = { "0", "1", "2", "3" };
			if (CycleControl("Model style", &nrStyle, styles, 4, "Selects the installed model's bounded style hint.")) {
				settings.neuralRenderingStyle = static_cast<uint>(nrStyle);
				markNeuralCustom();
			}
			changed |= ToggleControl("Automatic character mask", &settings.neuralRenderingAutoMask,
				"Uses the installed model's learned character mask to concentrate skin structure on detected people. It is not a terrain mask.");
			changed |= ToggleControl("UI correction", &settings.neuralRenderingUICorrection,
				"Keeps HUD and menu composition from being interpreted as world detail.");
			ImGui::TextColored(PIXLUI::ToVec4(PIXLUI::Colors::TextDim),
				"MODEL PRECISION: NVIDIA RUNTIME AUTOMATIC (NO SAFE APPLICATION INT4 CONTROL)");
			ImGui::EndDisabled();
			ImGui::Unindent(PIXLUI::Ref(14.0f));
			ImGui::PopID();
		}

		ImGui::PopID();
		ImGui::TableNextColumn();
		ImGui::PushID("FrameDeliveryColumn");
		SectionHeading("FRAME DELIVERY");

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

		if (imageReconstruction.DrawFrameGenerationBackendSelector()) {
			changed = restartNeeded = true;
		}
		bool frameGeneration =
			settings.frameGenerationMode != 0;
		const bool frameGenerationBackendAvailable =
			imageReconstruction.IsSelectedFrameGenerationBackendSelectable();
		ImGui::BeginDisabled(!frameGenerationBackendAvailable && !frameGeneration);
		if (ToggleControl(
				"Frame generation",
				&frameGeneration,
				"Generates intermediate frames through PIXL's compatibility swapchain. Requires a restart after changing.")) {
			settings.frameGenerationMode = frameGeneration ? 1u : 0u;
			if (frameGeneration)
				settings.frameGenerationForceEnable = 1;
			changed = restartNeeded = true;
		}
		ImGui::EndDisabled();
		if (!frameGenerationBackendAvailable)
			WrappedTintedText(PIXLUI::Colors::Warning,
				imageReconstruction.UsesDLSSGFrameGeneration()
					? "DLSS FRAME GENERATION: SUPPORTED RTX HARDWARE OR THE RTX 30 PROXY IS REQUIRED"
					: "FSR FRAME GENERATION RUNTIME IS NOT INSTALLED");
		if (imageReconstruction.UsesDLSSGFrameGeneration()) {
			const char* multipliers[] = { "2x (1 generated frame)", "3x (2 generated frames)", "4x (3 generated frames)" };
			int multiplier = static_cast<int>(std::clamp(settings.dlssgGeneratedFrames, 1u, 3u)) - 1;
			if (ImGui::Combo("DLSS-G frame multiplier", &multiplier, multipliers, _countof(multipliers))) {
				settings.dlssgGeneratedFrames = static_cast<uint>(multiplier + 1);
				changed = true;
			}
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::TextWrapped("Total output frames per rendered frame. Higher multipliers increase GPU work and do not improve input response. The active runtime determines its own limit; optional SM86 installs may also use MaxGeneratedFrames in dlssg_sm86.ini.");
			if (imageReconstruction.HasDLSSGModule())
				ImGui::Text("Runtime limit: %ux", imageReconstruction.streamlineDX12.dlssgMaxFramesToGenerate + 1u);
		}
		if (showAdvancedReconstruction) {
			bool forceLowRefresh = settings.frameGenerationForceEnable != 0u;
			if (ToggleControl(
					"Allow FG below 120 Hz",
					&forceLowRefresh,
					"Overrides PIXL's conservative high-refresh check. Generated frames are less useful on low-refresh displays.")) {
				settings.frameGenerationForceEnable = forceLowRefresh ? 1u : 0u;
				changed = restartNeeded = true;
			}
			changed |= ToggleControl(
				"Allow FG in menus",
				&settings.frameGenerationAllowInMenus,
				"Keeps generation active over menus. Off avoids UI interpolation artifacts and is recommended.");

			SectionHeading("LATENCY");
			const bool dlssgReflex = imageReconstruction.dx12SwapChain.presenter == DX12SwapChain::Presenter::kDLSSG;
			const auto& reflexRuntime = dlssgReflex ? imageReconstruction.streamlineDX12 : imageReconstruction.streamline;
			const bool reflexAvailable = reflexRuntime.IsReflexAvailable() &&
				(!imageReconstruction.IsFrameGenerationDx12PathActive() || dlssgReflex);
			if (dlssgReflex)
				ImGui::TextWrapped("DLSS-G automatically enables Reflex during generation. Boost and the limiter below use its DX12 runtime. The limiter targets rendered frames, not generated output.");
			ImGui::BeginDisabled(!reflexAvailable);
			changed |= ToggleControl(
				"NVIDIA Reflex",
				&settings.reflexLowLatencyMode,
				"Reduces the render queue on supported NVIDIA hardware. DLSS-G requires it during generation; this switch also keeps it on when generation is paused.");
			ImGui::BeginDisabled(!settings.reflexLowLatencyMode && !dlssgReflex);
			changed |= ToggleControl("Reflex boost", &settings.reflexLowLatencyBoost,
				"Requests a more aggressive low-latency power state at additional power cost.");
			changed |= ToggleControl("Marker optimization", &settings.reflexUseMarkersToOptimize,
				"Uses PIXL frame markers for tighter Reflex timing when PCL is available.");
			changed |= ToggleControl("Reflex FPS limiter", &settings.reflexUseFPSLimit,
				"Uses NVIDIA's latency-aware limiter instead of an external cap.");
			if (settings.reflexUseFPSLimit)
				changed |= SliderControl("Reflex target FPS", &settings.reflexFPSLimit, 20.0f, 240.0f, "%.0f FPS", false);
			ImGui::EndDisabled();
			ImGui::EndDisabled();
			if (!reflexAvailable)
				ImGui::TextColored(PIXLUI::ToVec4(PIXLUI::Colors::TextDim),
					"REFLEX IS UNAVAILABLE ON THIS RUNTIME (FSR3 OWNS ITS OWN PACING)");
		}

		const auto frameGenerationState =
			imageReconstruction.GetFrameGenerationState();
		const bool frameGenerationPendingRestart =
			frameGenerationState == ImageReconstruction::FrameGenerationState::RestartRequired;

		if (restartNeeded || frameGenerationPendingRestart) {
			WrappedTintedText(PIXLUI::Colors::Warning,
				"RESTART SKYRIM TO APPLY DISPLAY PATH CHANGES");
		} else if (frameGenerationState == ImageReconstruction::FrameGenerationState::Unavailable &&
			imageReconstruction.UsesDLSSGFrameGeneration()) {
			WrappedTintedText(PIXLUI::Colors::Warning,
				"DLSS FRAME GENERATION COULD NOT START. CHECK BORDERLESS MODE, GPU DRIVER, HARDWARE-ACCELERATED GPU SCHEDULING AND PIXL LOG.");
		} else {
			const char* statusText = "DISPLAY PATH READY";
			auto statusColour = PIXLUI::Colors::TextDim;
			switch (frameGenerationState) {
			case ImageReconstruction::FrameGenerationState::Active:
				statusText = "FRAME GENERATION ON - GENERATING FRAMES";
				statusColour = PIXLUI::Colors::Success;
				break;
			case ImageReconstruction::FrameGenerationState::TemporarilySuspended:
				statusText = "FRAME GENERATION ON - PAUSED WHILE MENU IS OPEN";
				statusColour = PIXLUI::Colors::Warning;
				break;
			case ImageReconstruction::FrameGenerationState::Starting:
				statusText = "FRAME GENERATION ON - PATH READY";
				statusColour = PIXLUI::Colors::Success;
				break;
			case ImageReconstruction::FrameGenerationState::Unavailable:
				statusText = "FRAME GENERATION OFF - REQUIREMENTS NOT MET";
				statusColour = PIXLUI::Colors::Danger;
				break;
			case ImageReconstruction::FrameGenerationState::RuntimeFault:
				statusText = "FRAME GENERATION OFF - BACKEND ERROR";
				statusColour = PIXLUI::Colors::Danger;
				break;
			case ImageReconstruction::FrameGenerationState::Off:
			default:
				statusText = "FRAME GENERATION OFF";
				break;
			}
			WrappedTintedText(statusColour, statusText);
		}

		ImGui::PopID();
		ImGui::EndTable();
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
		const bool gameScene = globals::state && globals::state->inWorld;

		PIXLUI::ChromeScope panel(
			"##PIXLCameraLivePreview",
			ImVec2(
				0,
				PIXLUI::Ref(gameScene ? 390.0f : 84.0f)),
			PIXLUI::ChromeStyle::Content,
			false,
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
			PIXLUI::Ref(28.0f);

		ImDrawList* draw =
			ImGui::GetWindowDrawList();

		const char* title =
			"LIVE GAME PREVIEW";
		const float toggleWidth = PIXLUI::Ref(60.0f);
		const float toggleHeight = PIXLUI::Ref(24.0f);
		const float labelGap = PIXLUI::Ref(5.0f);
		const float groupGap = PIXLUI::Ref(10.0f);
		const char* previewLabel = "PREVIEW";
		const char* effectsLabel = "FX";
		const float controlsWidth =
			ImGui::CalcTextSize(previewLabel).x + labelGap + toggleWidth +
			groupGap + ImGui::CalcTextSize(effectsLabel).x + labelGap + toggleWidth;
		const float controlX = headerStart.x +
			std::max(0.0f, headerWidth - controlsWidth - PIXLUI::Ref(8.0f));
		if (headerWidth > controlsWidth + ImGui::CalcTextSize(title).x + PIXLUI::Ref(24.0f)) {
			const ImVec2 titleSize = ImGui::CalcTextSize(title);
			draw->AddText(ImVec2(headerStart.x + PIXLUI::Ref(9.0f),
				headerStart.y + (headerHeight - titleSize.y) * 0.5f),
				PIXLUI::Colors::TextMuted, title);
		}
		const float controlY = headerStart.y +
			(headerHeight - toggleHeight) * 0.5f;
		float nextX = controlX;
		draw->AddText(ImVec2(nextX, controlY +
			(toggleHeight - ImGui::GetTextLineHeight()) * 0.5f),
			PIXLUI::Colors::TextDim, previewLabel);
		nextX += ImGui::CalcTextSize(previewLabel).x + labelGap;
		ImGui::SetCursorScreenPos(ImVec2(nextX, controlY));
		PIXLUI::Toggle("##LiveCameraPreviewEnabled", &livePreviewEnabled);
		if (ImGui::IsItemHovered()) {
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::TextWrapped("Shows the rendered game from CameraSuite's clean scene buffer. PIXL menu UI is excluded.");
		}
		nextX += toggleWidth + groupGap;
		draw->AddText(ImVec2(nextX, controlY +
			(toggleHeight - ImGui::GetTextLineHeight()) * 0.5f),
			PIXLUI::Colors::TextDim, effectsLabel);
		nextX += ImGui::CalcTextSize(effectsLabel).x + labelGap;
		ImGui::SetCursorScreenPos(ImVec2(nextX, controlY));
		PIXLUI::Toggle("##LiveCameraPreviewEffects", &livePreviewEffects);
		if (ImGui::IsItemHovered()) {
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::TextWrapped("Preview comparison only. OFF neutralizes PIXL camera finishing in the preview and does not save or alter gameplay settings.");
		}

		ImGui::SetCursorScreenPos(
			headerStart);
		ImGui::Dummy(ImVec2(headerWidth, headerHeight));

		const ImVec2 imageStart =
			ImGui::GetCursorScreenPos();
		const ImVec2 available =
			ImGui::GetContentRegionAvail();
		const ImVec2 imageArea(
			available.x,
			std::max(PIXLUI::Ref(40.0f), available.y));

		BackgroundBlur::LivePreviewFrame
			preview{};

		if (livePreviewEnabled && gameScene) {
			preview =
				BackgroundBlur::
					CaptureLivePreview(
						livePreviewEffects);
		}

		if (livePreviewEnabled && preview && preview.width > 0 && preview.height > 0) {
			const float sourceAspect =
				static_cast<float>(
					preview.width) /
				static_cast<float>(
					preview.height);
			const float areaAspect =
				std::max(imageArea.x, 1.0f) /
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
			ImGui::SetCursorScreenPos(imageStart);
			ImGui::Dummy(imageArea);
		} else {
			draw->AddRectFilled(imageStart,
				ImVec2(imageStart.x + imageArea.x, imageStart.y + imageArea.y),
				IM_COL32(8, 11, 14, 230));

			const char* message =
				!livePreviewEnabled
					? "LIVE PREVIEW OFF"
					: globals::state && !globals::state->inWorld
						? "PREVIEW AVAILABLE IN GAME"
						: "CLEAN SCENE PREVIEW INITIALIZING";
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

	void DrawColourPresetControls()
	{
		auto& camera = globals::pipeline::cameraSuite;
		bool changed = false;

		SectionHeading("COMPLETE LOOK PRESETS");
		ImGui::TextWrapped("Start with a complete exposure, tone, bloom and colour treatment. Every value remains editable under Post Processing.");
		changed |= ExternalPostProcessing::DrawENBQuickStylePalette();
		ImGui::Dummy(ImVec2(0.0f, PIXLUI::Ref(10.0f)));

		SectionHeading("PIXL COLOUR GRADES");
		ImGui::TextColored(PIXLUI::ToVec4(PIXLUI::Colors::TextMuted),
			"SELECT A GRADE, THEN SET ITS STRENGTH. ORIGINAL IS A CLEAN BYPASS.");
		ImGui::Dummy(ImVec2(0.0f, PIXLUI::Ref(4.0f)));

		const char* looks[] = {
			"ORIGINAL", "NORDIC NEUTRAL", "SAGA", "DRAMATIC", "HEARTHFIRE", "BLEAK",
			"BLEACH", "WINTER", "SUNSET", "FANTASY GREEN", "NIGHTFALL", "CINEMATIC"
		};
		const char* lookDescriptions[] = {
			"Neutral output with no PIXL colour LUT.",
			"Clean cool-neutral grade designed for Skyrim's natural palette.",
			"Measured Nordic contrast with restrained colour separation.",
			"Deeper contrast and firmer highlights for strong compositions.",
			"Warm interior and firelight response without excessive saturation.",
			"Cool, desaturated northern atmosphere.",
			"High-key low-colour treatment for stylised scenes.",
			"Cold daylight and snow-biased colour separation.",
			"Warm dusk response for amber skies and firelight.",
			"Green-biased fantasy treatment for forests and alchemy scenes.",
			"Cool low-light treatment with protected highlights.",
			"Subtle filmic finishing intended for Photo and Director modes."
		};
		const int previousLook = std::clamp(static_cast<int>(camera.settings.lookPreset), 0,
			static_cast<int>(std::size(looks)) - 1);
		const float gradeGap = PIXLUI::Ref(7.0f);
		const int columns = ImGui::GetContentRegionAvail().x >= PIXLUI::Ref(780.0f) ? 4 : 3;
		const float gradeWidth = std::max(PIXLUI::Ref(118.0f),
			(ImGui::GetContentRegionAvail().x - gradeGap * static_cast<float>(columns - 1)) /
				static_cast<float>(columns));
		for (int look = 0; look < static_cast<int>(std::size(looks)); ++look) {
			if (look % columns != 0)
				ImGui::SameLine(0.0f, gradeGap);
			ImGui::PushID(look);
			if (PIXLUI::ActionButton(looks[look], ImVec2(gradeWidth, PIXLUI::Ref(38.0f)), look == previousLook)) {
				camera.settings.lookPreset = static_cast<uint>(look);
				if (look == 11 && previousLook != 11)
					camera.settings.lookOpacity = 0.07f;
				else if (look != 0 && previousLook == 0 && camera.settings.lookOpacity <= 0.001f)
					camera.settings.lookOpacity = 0.20f;
				camera.LoadLookTexture();
				changed = true;
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", lookDescriptions[look]);
			ImGui::PopID();
		}

		ImGui::Dummy(ImVec2(0.0f, PIXLUI::Ref(5.0f)));
		ImGui::BeginDisabled(camera.settings.lookPreset == 0);
		float lookPercent = std::clamp(camera.settings.lookOpacity * 100.0f, 0.0f, 100.0f);
		if (SliderControl("Colour grade strength", &lookPercent, 0.0f, 100.0f, "%.0f%%", false,
				"Blends the selected grade after physical-camera exposure and tone response.")) {
			camera.settings.lookOpacity = lookPercent * 0.01f;
			changed = true;
		}
		ImGui::EndDisabled();

		if (changed) {
			camera.UpdateHDRData();
			globals::state->UpdateFeatureData(globals::state->inWorld);
			QueueDeferredStateSave();
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

		// Keep primary controls beside the preview; the enclosing page owns
		// scrolling so individual control cards do not trap the mouse wheel.
		const ImVec2 originalSpacing =
			ImGui::GetStyle().ItemSpacing;

		ImGui::PushStyleVar(
			ImGuiStyleVar_ItemSpacing,
			ImVec2(
				originalSpacing.x,
				PIXLUI::Ref(2.0f)));

		if (ImGui::BeginTable(
				"##PIXLCameraWorkspace",
				2,
				ImGuiTableFlags_SizingStretchProp |
					ImGuiTableFlags_NoSavedSettings)) {
			ImGui::TableSetupColumn(
				"Camera",
				ImGuiTableColumnFlags_WidthStretch,
				0.80f);
			ImGui::TableSetupColumn(
				"Preview and effects",
				ImGuiTableColumnFlags_WidthStretch,
				1.20f);

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

			SectionHeading("BLOOM");
			changed |= ToggleControl("Bloom enabled", &camera.settings.enableBloom);
			ImGui::BeginDisabled(!camera.settings.enableBloom);
			changed |= SliderControl("Bloom strength", &camera.settings.bloomStrength,
				0.0f, 2.0f, "%.2f", false);
			changed |= SliderControl("Bloom threshold", &camera.settings.bloomThreshold,
				0.0f, 4.0f, "%.2f", false);
			changed |= SliderControl("Bloom radius", &camera.settings.bloomRadius,
				0.0f, 4.0f, "%.2f", false);
			ImGui::EndDisabled();

			ImGui::PopID();

			// -------------------------------------------------------------
			// CENTRE — live scene preview
			// -------------------------------------------------------------
			// Keep the scene comparison in the upper-right corner. The compact
			// setting columns stay adjacent for faster visual evaluation.
			ImGui::TableSetColumnIndex(1);
			ImGui::PushID(
				"PreviewDepthColumn");

			DrawLiveCameraPreview();

			ImGui::Dummy(
				ImVec2(
					0,
					PIXLUI::Ref(4.0f)));

			ImGui::PopID();

			// -------------------------------------------------------------
			// RIGHT — reflections / bloom / weather lens
			// -------------------------------------------------------------
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

			SectionHeading("DEPTH OF FIELD");
			if (camera.IsCinematicDoFLoaded()) {
				changed |= ToggleControl("Use Cinematic DoF", &camera.settings.preferCinematicDoF,
					"Hands depth of field to the installed plugin and prevents duplicate native blur. Focus remains in Cinematic DoF's own menu; disable this hand-off to use Skyrim DoF again.");
			}
			ImGui::BeginDisabled(camera.UsesCinematicDoF());
			changed |= ToggleControl("Skyrim depth of field",
				&camera.settings.enableSkyrimDepthOfField,
				"Uses Skyrim's native image-space depth of field. PIXL's experimental full-screen DOF path is disabled for this release.");
			ImGui::EndDisabled();

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

	void ApplyPublicDofPreset(int preset)
	{
		globals::pipeline::cameraSuite.ApplyDepthOfFieldPreset(
			static_cast<std::uint32_t>(std::clamp(preset, 0, 3)));
	}

	void DrawDepthOfFieldControls()
	{
		auto& camera = globals::pipeline::cameraSuite;
		auto& settings = camera.settings;
		bool changed = false;

		SectionHeading("AUTO-DOF");
		ImGui::TextColored(PIXLUI::ToVec4(PIXLUI::Colors::TextMuted),
			"PHYSICAL LENS  /  STABLE AUTOFOCUS  /  NATIVE PIXL DEPTH");
		ImGui::TextWrapped("Shape focus for gameplay, portraits and Director shots. These are the same lens parameters exposed by the Advanced CameraSuite page.");
		ImGui::Dummy(ImVec2(0.0f, PIXLUI::Ref(5.0f)));

		if (camera.IsCinematicDoFLoaded()) {
			changed |= ToggleControl("Use external Cinematic DoF", &settings.preferCinematicDoF,
				"Hands depth of field to Cinematic DoF Standalone. Turn this off to use PIXL's native physical lens and the controls below.");
		}
		ImGui::BeginDisabled(camera.UsesCinematicDoF());
		changed |= ToggleControl("PIXL Auto-DOF", &settings.enableEnhancedDepthOfField,
			"Runs PIXL's depth-aware near/far bokeh before UI composition. This is the recommended native path.");

		ImGui::BeginDisabled(!settings.enableEnhancedDepthOfField);
		SectionHeading("LENS PRESETS");
		const char* presetLabels[] = { "GAMEPLAY", "BALANCED", "PORTRAIT", "CINEMA" };
		const char* presetHelp[] = {
			"Restrained separation and a long focus transition for normal movement.",
			"A versatile physical-camera baseline for exploration and dialogue.",
			"A 50 mm shallow lens with stronger subject separation.",
			"A wide-aperture Director look with larger, bounded bokeh."
		};
		const float presetGap = PIXLUI::Ref(7.0f);
		const float availablePresetWidth = ImGui::GetContentRegionAvail().x;
		const int presetColumns = availablePresetWidth < PIXLUI::Ref(430.0f) ? 2 : 4;
		const float presetWidth = std::max(1.0f,
			(availablePresetWidth - presetGap * static_cast<float>(presetColumns - 1)) /
			static_cast<float>(presetColumns));
		for (int preset = 0; preset < 4; ++preset) {
			if (preset % presetColumns != 0)
				ImGui::SameLine(0.0f, presetGap);
			ImGui::PushID(preset);
			if (PIXLUI::ActionButton(presetLabels[preset], ImVec2(presetWidth, PIXLUI::Ref(38.0f)), false)) {
				ApplyPublicDofPreset(preset);
				changed = true;
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", presetHelp[preset]);
			ImGui::PopID();
		}

		ImGui::Dummy(ImVec2(0.0f, PIXLUI::Ref(4.0f)));
		if (ImGui::BeginTable("##PublicDofCore", 2,
				ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_BordersInnerV)) {
			ImGui::TableNextColumn();
			ImGui::PushID("DofFocus");
			SectionHeading("FOCUS");
			changed |= ToggleControl("Scene autofocus", &settings.dofAutoFocus,
				"Keeps the scene around the centre of your view in focus and shifts smoothly as you look around.");
			changed |= ToggleControl("Track dialogue / subjects", &settings.dofActorTracking,
				"Keeps speaking characters and tracked subjects in focus ahead of the background.");
			changed |= SliderControl("Focus speed", &settings.dofFocusSpeed, 0.25f, 20.0f, "%.2fx", true,
				"Sets how quickly focus follows a new subject. Lower is slower and softer; higher reacts faster.");
			changed |= SliderControl("Focus transition", &settings.dofFocusRange, 240.0f, 20000.0f, "%.0f units", true,
				"A wider range keeps more nearby objects sharp before they fade into blur.");
			if (!settings.dofAutoFocus)
				changed |= SliderControl("Manual focus", &settings.dofFocusDistance, 100.0f, 20000.0f, "%.0f units", true,
					"Sets the distance that stays sharp while automatic focus is off.");
			ImGui::PopID();

			ImGui::TableNextColumn();
			ImGui::PushID("DofLens");
			SectionHeading("PHYSICAL LENS");
			changed |= ToggleControl("Physical thin lens", &settings.dofPhysicalLens,
				"Uses camera lens settings to shape blur more like a real camera.");
			changed |= SliderControl("Focal length", &settings.dofFocalLengthMm, 18.0f, 200.0f, "%.0f mm", true,
				"Longer lenses make the background look closer and blur it more; shorter lenses keep more of the scene in view.");
			changed |= SliderControl("Aperture", &settings.dofFStop, 0.7f, 32.0f, "f/%.1f", true,
				"Lower values blur the background more; higher values keep more of the scene sharp.");
			changed |= SliderControl("Maximum bokeh", &settings.dofMaxBokehPixels, 4.0f, 96.0f, "%.0f px", true,
				"Caps the size of soft light shapes in the blur. Larger values look more dramatic and use more graphics power.");
			changed |= SliderControl("Overall presence", &settings.dofStrength, 0.0f, 1.0f, "%.2f", false,
				"Turns depth of field down or up without changing the lens settings.");
			ImGui::PopID();
			ImGui::EndTable();
		}

		SectionHeading("NEAR & DISTANCE SEPARATION");
		if (ImGui::BeginTable("##PublicDofSeparation", 2,
				ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_BordersInnerV)) {
			ImGui::TableNextColumn();
			changed |= SliderControl("Near blur", &settings.dofNearBlurIntensity, 0.0f, 2.0f, "%.2fx", false,
				"Adds blur to nearby objects that are out of focus. Keep it low in first person to keep hands and weapons clear.");
			changed |= SliderControl("Foreground coverage", &settings.dofForegroundCoverage, 0.0f, 1.5f, "%.2f", false,
				"Fills small gaps around blurred foreground edges. Too much can create a soft halo.");
			ImGui::TableNextColumn();
			changed |= SliderControl("Distance blur", &settings.dofFarBlurIntensity, 0.0f, 2.0f, "%.2fx", false,
				"Adds gentle blur to faraway scenery while autofocus keeps the subject sharp.");
			changed |= SliderControl("Distance blur start", &settings.dofFarBlurDistance, 500.0f, 50000.0f, "%.0f units", true,
				"Sets how far away scenery must be before this extra blur starts.");
			ImGui::EndTable();
		}

		if (ImGui::CollapsingHeader("ADVANCED BOKEH & EDGE CONTROL", ImGuiTreeNodeFlags_None)) {
			if (ImGui::BeginTable("##PublicDofAdvanced", 2,
					ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_BordersInnerV)) {
				ImGui::TableNextColumn();
				changed |= SliderControl("Highlight response", &settings.dofHighlightResponse, 0.0f, 1.0f, "%.2f", false,
					"Lets bright out-of-focus lights form more visible bokeh shapes.");
				changed |= SliderControl("Edge protection", &settings.dofFocusEdgeProtection, 0.0f, 2.0f, "%.2f", false,
					"Reduces blur leaking across sharp foreground edges.");
				changed |= SliderControl("Focus deadband", &settings.dofFocusDeadband, 0.0f, 0.25f, "%.3f", false,
					"Ignores tiny focus changes so the image does not keep breathing.");
				changed |= SliderControl("Sensor height", &settings.dofSensorHeightMm, 10.0f, 40.0f, "%.1f mm", false,
					"Sets how large the physical lens blur appears on screen.");
				ImGui::TableNextColumn();
				int blades = static_cast<int>(settings.dofApertureBlades);
				if (ImGui::SliderInt("Aperture blades", &blades, 3, 12)) {
					settings.dofApertureBlades = static_cast<uint>(blades);
					changed = true;
				}
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Sets how many sides appear in blurred highlights.");
				changed |= SliderControl("Blade curvature", &settings.dofBladeCurvature, 0.0f, 1.0f, "%.2f", false,
					"Rounds the bokeh from polygonal toward circular.");
				changed |= SliderControl("Cat-eye", &settings.dofCatEye, 0.0f, 1.0f, "%.2f", false,
					"Shapes blur near the screen edges like a real lens.");
				changed |= SliderControl("Anamorphic ratio", &settings.dofAnamorphicRatio, 0.5f, 2.0f, "%.2f", false,
					"Stretches bokeh horizontally or vertically.");
				if (ImGui::SliderAngle("Aperture rotation", &settings.dofApertureRotation, -180.0f, 180.0f))
					changed = true;
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Rotates the shape of out-of-focus highlights.");
				ImGui::EndTable();
			}
		}
		ImGui::EndDisabled();
		ImGui::EndDisabled();

		ImGui::BeginDisabled(settings.enableEnhancedDepthOfField || camera.UsesCinematicDoF());
		changed |= ToggleControl("Legacy Skyrim depth of field", &settings.enableSkyrimDepthOfField,
			"Fallback to Skyrim's authored image-space DOF when neither PIXL nor Cinematic DoF owns the effect.");
		ImGui::EndDisabled();

		if (changed) {
			camera.UpdateHDRData();
			QueueDeferredStateSave();
		}
	}

	void DrawPublicCameraEffects()
	{
		auto& camera = globals::pipeline::cameraSuite;
		auto& settings = camera.settings;
		bool changed = false;

		SectionHeading("PIXL POST-PROCESSING");
		ImGui::TextWrapped("Shape the finished image with exposure, colour, bloom, motion blur and weather optics. These effects are applied after the world is rendered; the game interface stays sharp.");
		ImGui::Dummy(ImVec2(0.0f, PIXLUI::Ref(7.0f)));

		if (ImGui::BeginTable("##PublicCameraEffects", 2,
				ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_BordersInnerV)) {
			ImGui::TableNextColumn();
			ImGui::PushID("PublicExposure");
			SectionHeading("LIGHT & COLOUR");
			changed |= ToggleControl("Physical camera", &settings.enablePhysicalCamera,
				"Applies PIXL's camera exposure and tone response to the finished scene.");
			changed |= ToggleControl("Automatic exposure", &settings.cameraAutoExposure,
				"Lets the image adapt as you move between bright and dark places.");
			changed |= SliderControl("Exposure", &settings.cameraExposureCompensationEV, -2.0f, 2.0f, "%+.2f EV", false,
				"Makes the whole scene brighter or darker. Small changes are usually best.");
			changed |= SliderControl("Camera influence", &settings.cameraInfluence, 0.0f, 1.0f, "%.2f", false,
				"Blends the physical camera response with Skyrim's original image.");
			changed |= SliderControl("Local exposure", &settings.cameraLocalExposure, 0.0f, 0.5f, "%.2f", false,
				"Lifts dark areas while protecting nearby bright details.");
			changed |= SliderControl("Highlight protection", &settings.cameraHighlightProtection, 0.0f, 1.0f, "%.2f", false,
				"Keeps bright skies, fires and reflections from losing detail.");
			changed |= SliderControl("Shadow detail", &settings.cameraShadowDetail, 0.0f, 0.5f, "%.2f", false,
				"Reveals more detail in dark areas without brightening the whole image.");
			changed |= SliderControl("Contrast", &settings.cameraContrast, 0.75f, 1.30f, "%.2f", false,
				"Sets the difference between light and dark parts of the image.");
			changed |= SliderControl("Colour strength", &settings.cameraSaturation, 0.70f, 1.25f, "%.2f", false,
				"Makes colours more vivid or more muted.");
			if (ImGui::CollapsingHeader("ADAPTATION CURVE")) {
				changed |= SliderControl("Bright to dark", &settings.cameraAdaptBrightToDark, 0.05f, 4.0f, "%.2f s", true,
					"How quickly your view adjusts after entering a darker place.");
				changed |= SliderControl("Dark to bright", &settings.cameraAdaptDarkToBright, 0.03f, 2.0f, "%.2f s", true,
					"How quickly your view adjusts after entering a brighter place.");
				changed |= SliderControl("Black toe", &settings.cameraToe, 0.0f, 0.5f, "%.2f", false,
					"Softens the deepest blacks so shadow detail is easier to see.");
				changed |= SliderControl("Highlight shoulder", &settings.cameraShoulder, 0.2f, 1.5f, "%.2f", false,
					"Softens the transition into the brightest parts of the image.");
			}
			ImGui::PopID();

			ImGui::TableNextColumn();
			ImGui::PushID("PublicOptics");
			SectionHeading("BLOOM & MOTION");
			changed |= ToggleControl("Bloom", &settings.enableBloom,
				"Adds a soft glow around bright lights and magical effects.");
			ImGui::BeginDisabled(!settings.enableBloom);
			changed |= SliderControl("Bloom strength", &settings.bloomStrength, 0.0f, 2.0f, "%.2f", false,
				"Controls how much glow bright areas create.");
			changed |= SliderControl("Bloom threshold", &settings.bloomThreshold, 0.0f, 4.0f, "%.2f", false,
				"Raises the brightness needed before a surface starts to glow.");
			changed |= SliderControl("Bloom radius", &settings.bloomRadius, 0.1f, 4.0f, "%.2fx", false,
				"Spreads the glow over a wider or tighter area.");
			ImGui::EndDisabled();
			changed |= ToggleControl("Motion blur", &settings.enableModernMotionBlur,
				"Adds a restrained directional blur during fast camera movement.");
			ImGui::BeginDisabled(!settings.enableModernMotionBlur);
			changed |= SliderControl("Motion blur strength", &settings.motionBlurStrength, 0.0f, 1.0f, "%.2f", false,
				"Sets how visible the movement blur is.");
			changed |= SliderControl("Shutter", &settings.motionBlurShutter, 0.10f, 1.0f, "%.2fx", false,
				"Longer shutter values add more blur to fast movement.");
			changed |= SliderControl("Motion limit", &settings.motionBlurMaxPixels, 4.0f, 48.0f, "%.0f px", false,
				"Caps blur length to help keep the image readable.");
			ImGui::EndDisabled();
			ImGui::PopID();

			ImGui::TableNextColumn();
			ImGui::PushID("PublicWeatherLens");
			SectionHeading("WEATHER & LENS EFFECTS");
			changed |= ToggleControl("Rain on the lens", &settings.enableStormglass,
				"Adds subtle, weather-aware droplets to the camera during rain.");
			ImGui::BeginDisabled(!settings.enableStormglass);
			changed |= SliderControl("Rain effect strength", &settings.stormglassStrength, 0.0f, 1.0f, "%.2f", false,
				"Controls how visible rain on the lens appears.");
			ImGui::EndDisabled();
			changed |= ToggleControl("Underwater lens", &settings.enableSubmergedOptics,
				"Adds optical distortion and colour response while submerged.");
			changed |= ToggleControl("Cold-weather lens", &settings.enableColdLens,
				"Adds restrained frost and cold-edge response in freezing conditions.");
			changed |= ToggleControl("Elemental impact lens", &settings.enableElementalDamageLens,
				"Adds brief camera effects when elemental damage hits the player.");
			ImGui::PopID();

			ImGui::TableNextColumn();
			ImGui::PushID("PublicMenuScene");
			SectionHeading("MENU SCENES");
			changed |= SliderControl("Menu scene brightness", &settings.menuSceneBrightness, 0.75f, 3.0f, "%.2fx", false,
				"Brightens or dims menu, loading and lockpicking scenes without changing gameplay exposure.");
			ImGui::PopID();
			ImGui::EndTable();
		}
		if (ImGui::CollapsingHeader("OTHER POST-PROCESSING PROVIDERS", ImGuiTreeNodeFlags_None))
			ExternalPostProcessing::DrawSettings();

		if (changed)
			camera.UpdateHDRData();
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
					&gi.EnableGI,
					"Adds light bounced from the world around you. Higher lighting quality improves detail and costs more graphics power.");

			changed |=
				ToggleControl(
					"Hybrid reflections",
					&gi.EnableExperimentalSpecularGI,
					"Adds reflections from nearby scene detail and the world light cache. It works alongside Skyrim's original reflections.");

			changed |=
				ToggleControl(
					"World light cache",
					&gi.EnableWorldCache,
					"Stores bounced light in the world so it can illuminate nearby views over time. It uses additional graphics memory.");

			changed |=
				ToggleControl(
					"Directional occlusion",
					&gi.EnableDirectionalOcclusion,
					"Adds soft contact shading where surfaces meet and in small creases. Higher quality can cost more graphics power.");

			bool contactShadows =
				pbr.EnableLocalContactShadows != 0u;

			if (ToggleControl(
					"Local contact shadows",
					&contactShadows,
					"Adds short, detailed shadows from nearby local lights, helping objects feel grounded.")) {
				pbr.EnableLocalContactShadows =
					contactShadows ? 1u : 0u;
				changed = true;
			}

			bool screenShadows =
				shadows.Enable != 0u;

			if (ToggleControl(
					"Sun & moon contact shadows",
					&screenShadows,
					"Adds fine depth-aware shadows from the active sun or moon. Local contact shadows separately handle nearby lights.")) {
				shadows.Enable =
					screenShadows ? 1u : 0u;
				changed = true;
			}

			changed |=
				ToggleControl(
					"Terrain shadows",
					&terrainOcclusion.EnableTerrainShadow,
					"Adds distance-aware shadows to large landscape features such as cliffs and hills.");

			ImGui::PopID();

			ImGui::TableNextColumn();
			ImGui::PushID("WaterFeatures");
			SectionHeading("WATER & REFLECTIONS");

			bool waterReflections =
				water.EnableEnhancedSSR != 0u;

			if (ToggleControl(
					"Water reflections",
					&waterReflections,
					"Adds screen-space reflections to water where the scene is visible on screen.")) {
				water.EnableEnhancedSSR =
					waterReflections ? 1u : 0u;
				changed = true;
			}

			bool materialSSR =
				cubemaps.EnabledSSR != 0u;

			if (ToggleControl(
					"Material reflections",
					&materialSSR,
					"Adds screen-space reflections to eligible shiny materials such as metal and wet surfaces.")) {
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
					&waterCaustics,
					"Projects moving light patterns from water onto nearby surfaces where supported.")) {
				water.EnableEnhancedCaustics =
					waterCaustics ? 1u : 0u;
				changed = true;
			}

			changed |=
				SliderControl(
					"Underwater caustics",
					&water.CausticsStrength,
					0.0f,
					2.0f,
					"%.2fx",
					false,
					"Sets caustic intensity on surfaces below the water.");

			changed |= SliderControl(
				"Light bounced onto walls",
				&water.ProjectedCausticsStrength,
				0.0f,
				3.0f,
				"%.2fx",
				false,
				"Brightens caustic light projected onto nearby walls and bridge undersides above the water.");

			changed |= SliderControl(
				"Caustic bounce reach",
				&water.ProjectedCausticsDistance,
				64.0f,
				800.0f,
				"%.0f units",
				false,
				"Extends projected caustic light higher and farther from the water surface.");

			ImGui::PopID();

			ImGui::TableNextColumn();
			ImGui::PushID("InteractionFeatures");
			SectionHeading("WORLD INTERACTION");

			changed |=
				ToggleControl(
					"Grass interaction",
					&grass.EnableGroundResponse,
					"Lets footsteps and nearby movement bend or disturb grass where the mesh supports it.");

			changed |=
				ToggleControl(
					"Ground deformation",
					&grass.EnableDeformableGround,
					"Lets suitable snow and mud surfaces keep marks from footsteps and impacts.");

			ImGui::BeginDisabled(
				!grass.EnableDeformableGround);

			changed |=
				ToggleControl(
					"Snow deformation",
					&grass.EnableSnowDeformation,
					"Adds persistent compression and tracks to compatible snow surfaces.");

			changed |=
				ToggleControl(
					"Mud deformation",
					&grass.EnableMudDeformation,
					"Adds softer, moisture-aware marks to compatible mud surfaces.");

			ImGui::EndDisabled();

			SectionHeading("FINISHING");

			changed |=
				SliderControl(
					"Indirect light",
					&gi.GIStrength,
					0.70f,
					1.30f,
					"%.2fx",
					false,
					"Fine-tunes the strength of indirect light bounced from the surrounding world.");

			changed |=
				SliderControl(
					"Reflection presence",
					&gi.ReflectionIntensity,
					0.70f,
					1.30f,
					"%.2fx",
					false,
					"Fine-tunes reflection brightness around the PIXL baseline. Very high values can look unnatural.");

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
		static int cameraWorkspace = 0;
		const float availableWidth = ImGui::GetContentRegionAvail().x;
		const float gap = PIXLUI::Ref(8.0f);
		SectionHeading("CAMERA & IMAGE FINISHING");
		ImGui::TextColored(PIXLUI::ToVec4(PIXLUI::Colors::TextMuted),
			"POST-PROCESSING  /  DEPTH OF FIELD  /  LOOKS  /  UPSCALING");
		ImGui::Dummy(ImVec2(0.0f, PIXLUI::Ref(5.0f)));

		const char* workspaceLabels[] = { "POST PROCESSING", "DEPTH OF FIELD", "LOOKS & PRESETS", "UPSCALING" };
		const int columns = availableWidth >= PIXLUI::Ref(700.0f) ? 4 : 2;
		const float tabWidth = std::max(1.0f,
			(availableWidth - gap * static_cast<float>(columns - 1)) / static_cast<float>(columns));
		for (int page = 0; page < 4; ++page) {
			if (page % columns != 0)
				ImGui::SameLine(0.0f, gap);
			if (PIXLUI::PageButton(workspaceLabels[page], workspaceLabels[page], cameraWorkspace == page,
					ImVec2(tabWidth, PIXLUI::Ref(38.0f)))) {
				cameraWorkspace = page;
				ImGui::SetScrollY(0.0f);
			}
		}
		ImGui::Dummy(ImVec2(0, PIXLUI::Ref(8.0f)));
		if (cameraWorkspace == 0) {
			DrawPublicCameraEffects();
		} else if (cameraWorkspace == 1) {
			DrawDepthOfFieldControls();
		} else if (cameraWorkspace == 2) {
			DrawColourPresetControls();
			ImGui::Dummy(ImVec2(0, PIXLUI::Ref(8.0f)));
		} else
			DrawPerformanceControls();
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
			TuningWorkspaceRenderer::ResetWorkspaceHistory();
			globals::state->Load();
		}

		ImGui::Dummy(
			ImVec2(
				0,
				PIXLUI::Ref(14.0f)));

		static bool showAdvancedSupport = false;
		if (PIXLUI::ActionButton(
				showAdvancedSupport ? "HIDE ADVANCED / SUPPORT" : "ADVANCED / SUPPORT",
				ImVec2(PIXLUI::Ref(210.0f), PIXLUI::Ref(34.0f)),
				false)) {
			showAdvancedSupport = !showAdvancedSupport;
		}

		if (showAdvancedSupport) {
			ImGui::Dummy(ImVec2(0, PIXLUI::Ref(8.0f)));
			SectionHeading("ENGINEERING");
			ImGui::TextColored(
				PIXLUI::ToVec4(PIXLUI::Colors::TextMuted),
				"Module diagnostics and shader maintenance. Most players never need these tools.");
			ImGui::Dummy(ImVec2(0, PIXLUI::Ref(8.0f)));

			if (PIXLUI::ActionButton(
					"OPEN PIXL TUNER",
					ImVec2(PIXLUI::Ref(190.0f), PIXLUI::Ref(36.0f)),
					false)) {
				globals::menu->GetSettings().AdvancedMode = true;
				globals::state->Save();
			}

			ImGui::SameLine(0.0f, PIXLUI::Ref(8.0f));
			if (PIXLUI::ActionButton(
					"REBUILD SHADERS",
					ImVec2(PIXLUI::Ref(175.0f), PIXLUI::Ref(36.0f)),
					false)) {
				Util::RequestClearShaderCacheConfirmation();
			}
			Tooltip("Clears PIXL's shader library and rebuilds it from the installed renderer source.");
		}
	}

}

void PIXLRendererPage::RenderNeuralSetupGuide(bool canEnable, bool* quickSetupConfirmed)
{
	DrawNeuralSetupCard(canEnable, quickSetupConfirmed);
}

void PIXLRendererPage::Render()
{
	auto& menuSettings = globals::menu->GetSettings();
	const auto storedPage = std::clamp(
		static_cast<int>(menuSettings.LastPublicPage),
		static_cast<int>(PublicPage::Quality),
		static_cast<int>(PublicPage::Renderer));
	PublicPage currentPage = static_cast<PublicPage>(storedPage);
	bool pageChanged = false;
	auto selectPage = [&](PublicPage page) {
		if (currentPage == page)
			return;
		currentPage = page;
		menuSettings.LastPublicPage = static_cast<uint>(page);
		pageChanged = true;
		QueueDeferredStateSave();
	};

	const float gap =
		PIXLUI::Ref(8.0f);
	const float pageWidth =
		std::max(
			PIXLUI::Ref(170.0f),
			(ImGui::GetContentRegionAvail().x -
				gap * 2.0f) /
				3.0f);
	const float entryWidth = (pageWidth - gap) * 0.5f;
	const bool compactEntryLabels = entryWidth <
		ImGui::CalcTextSize("ADVANCED TUNER").x + PIXLUI::Ref(24.0f);

	if (PIXLUI::ActionButton(compactEntryLabels ? "SETUP" : "QUICK SETUP", ImVec2(entryWidth, PIXLUI::Ref(30.0f)), false))
		LaunchExperienceRenderer::OpenQuickSetup();
	Util::AddTooltip("Reopen the guided setup card to change the quality profile or image reconstruction path. Changes are saved when you continue.");
	ImGui::SameLine(0.0f, gap);
	if (PIXLUI::ActionButton(compactEntryLabels ? "TUNER" : "ADVANCED TUNER", ImVec2(entryWidth, PIXLUI::Ref(30.0f)), false)) {
		menuSettings.AdvancedMode = true;
		globals::state->Save();
	}
	Util::AddTooltip("Open the complete PIXL tuner with every module and engineering control.");
	ImGui::Dummy(ImVec2(0.0f, PIXLUI::Ref(5.0f)));

	if (PIXLUI::PageButton(
			"Quality",
			"QUALITY",
			currentPage == PublicPage::Quality,
			ImVec2(
				pageWidth,
				PIXLUI::Ref(40.0f)))) {
		selectPage(PublicPage::Quality);
	}

	ImGui::SameLine(
		0.0f,
		gap);

	if (PIXLUI::PageButton(
			"Post-processing",
			"POST-PROCESSING",
			currentPage == PublicPage::Camera,
			ImVec2(
				pageWidth,
				PIXLUI::Ref(40.0f)))) {
		selectPage(PublicPage::Camera);
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
		selectPage(PublicPage::Renderer);
	}

	if (pageChanged)
		PIXLUI::SetAnimationValue("##PIXLPublicPageReveal", 0.0f);
	const float pageReveal = PIXLUI::Animate01("##PIXLPublicPageReveal", true, 13.0f);

	ImGui::Dummy(
		ImVec2(
			0,
			PIXLUI::Ref(10.0f)));

	ImGui::PushStyleColor(
		ImGuiCol_ChildBg,
		ImVec4(0, 0, 0, 0));

	const ImGuiWindowFlags publicPageFlags = ImGuiWindowFlags_None;
	// Shared compact rhythm across Quality, Camera and Renderer. Keep colours,
	// fonts and control hit targets; spend less space on repeated vertical padding.
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(PIXLUI::Ref(8.0f), PIXLUI::Ref(4.0f)));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(PIXLUI::Ref(7.0f), PIXLUI::Ref(3.0f)));
	ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.62f + 0.38f * pageReveal);
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (1.0f - pageReveal) * PIXLUI::Ref(6.0f));

	ImGui::PushID(static_cast<int>(currentPage));
	if (ImGui::BeginChild(
			"##PIXLPublicPage",
			ImVec2(0, 0),
			ImGuiChildFlags_None,
			publicPageFlags)) {
		// A tab change should reveal the page's primary card. In particular, the
		// Quality preview must never inherit a lower scroll position from a
		// previous visit and appear to be missing.
		if (pageChanged || ImGui::IsWindowAppearing())
			ImGui::SetScrollY(0.0f);
		switch (currentPage) {
		case PublicPage::Quality: {
			DrawQualityControls();

			ImGui::Dummy(
				ImVec2(
					0,
					PIXLUI::Ref(5.0f)));

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
	ImGui::PopID();
	ImGui::PopStyleVar(3);
	ImGui::PopStyleColor();

	FlushDeferredStateSave();
}

void PIXLRendererPage::FlushPendingEdits(bool force)
{
	FlushDeferredStateSave(force);
}
