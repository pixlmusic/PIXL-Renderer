#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <imgui.h>

#include "Fonts.h"
#include "ThemeManager.h"
#include "../Util.h"

// PIXL Renderer presentation-only UI primitives.
//
// These helpers draw the metallic/cyan authoring workspace without changing
// any rendering, shader, persistence, or module behavior.
namespace PIXLUI
{
	struct Colors
	{
		static constexpr ImU32 Window       = IM_COL32(7, 9, 11, 248);
		static constexpr ImU32 Inset        = IM_COL32(10, 13, 16, 252);
		static constexpr ImU32 InsetRaised  = IM_COL32(16, 20, 24, 252);
		static constexpr ImU32 Rail         = IM_COL32(8, 11, 14, 252);

		static constexpr ImU32 SteelLight   = IM_COL32(151, 164, 170, 255);
		static constexpr ImU32 SteelMid     = IM_COL32(91, 103, 109, 255);
		static constexpr ImU32 SteelDark    = IM_COL32(42, 49, 54, 255);
		static constexpr ImU32 SteelDeep    = IM_COL32(20, 25, 29, 255);
		static constexpr ImU32 SteelHighlight = IM_COL32(215, 222, 224, 54);
		static constexpr ImU32 SteelShadow  = IM_COL32(0, 0, 0, 155);

		static constexpr ImU32 Border       = IM_COL32(94, 108, 114, 210);
		static constexpr ImU32 BorderBright = IM_COL32(154, 168, 173, 200);
		static constexpr ImU32 BorderSoft   = IM_COL32(52, 63, 68, 190);

		static constexpr ImU32 Cyan         = IM_COL32(70, 201, 207, 255);
		static constexpr ImU32 CyanBright   = IM_COL32(133, 236, 239, 255);
		static constexpr ImU32 CyanSoft     = IM_COL32(59, 165, 171, 210);
		static constexpr ImU32 CyanGhost    = IM_COL32(55, 190, 196, 28);
		static constexpr ImU32 CyanGlow     = IM_COL32(62, 211, 216, 42);

		static constexpr ImU32 Text         = IM_COL32(232, 233, 228, 255);
		static constexpr ImU32 TextMuted    = IM_COL32(166, 171, 170, 255);
		static constexpr ImU32 TextDim      = IM_COL32(107, 115, 116, 255);
		static constexpr ImU32 TextDark     = IM_COL32(14, 17, 19, 255);

		static constexpr ImU32 Success      = IM_COL32(115, 190, 154, 255);
		static constexpr ImU32 Warning      = IM_COL32(201, 163, 91, 255);
		static constexpr ImU32 Danger       = IM_COL32(199, 92, 98, 255);

		static constexpr ImU32 RefSteelTop      = SteelLight;
		static constexpr ImU32 RefSteelMid      = SteelMid;
		static constexpr ImU32 RefSteelBottom   = SteelDark;
		static constexpr ImU32 RefSteelEdge     = BorderBright;
		static constexpr ImU32 RefSteelDarkEdge = SteelDeep;
		static constexpr ImU32 RefInset          = Inset;
		static constexpr ImU32 RefInset2         = InsetRaised;
		static constexpr ImU32 RefCyan           = Cyan;
		static constexpr ImU32 RefCyanHot        = CyanBright;
		static constexpr ImU32 RefCyanGlow       = CyanGlow;
	};

	struct Layout
	{
		// Reference coordinate system used by the compact tuner.
		static constexpr float ReferenceWidth = 1287.0f;
		static constexpr float ReferenceHeight = 832.0f;
		static constexpr float ViewportHeightRatio = 0.90f;
		static constexpr float ViewportWidthSafety = 0.96f;

		static constexpr float HeaderHeight = 74.0f;
		static constexpr float HeaderLeftInset = 78.0f;
		static constexpr float HeaderRightInset = 17.0f;
		static constexpr float HeaderButtonHeight = 41.0f;
		static constexpr float HeaderRestoreWidth = 124.0f;
		static constexpr float HeaderSaveWidth = 130.0f;
		static constexpr float HeaderButtonGap = 10.0f;

		static constexpr float HeaderToToolbarGap = 13.0f;
		static constexpr float ToolbarHeight = 31.0f;
		static constexpr float ToolbarToWorkspaceGap = 9.0f;
		static constexpr float ReturnButtonWidth = 159.0f;
		static constexpr float ReturnButtonHeight = 28.0f;

		static constexpr float SidebarWidth = 246.0f;
		static constexpr float WorkspaceGap = 8.0f;
		static constexpr float ChassisInset = 7.0f;
		static constexpr float SidebarInnerInset = 10.0f;
		static constexpr float ContentInnerInset = 9.0f;

		static constexpr float ContentTitleHeight = 99.0f;
		static constexpr float NavigationHeight = 26.0f;
		static constexpr float NavigationGap = 4.0f;

		// A4 premium chassis dimensions in reference-image pixels.
		static constexpr float HeaderChamfer = 11.0f;
		static constexpr float SidebarChamfer = 9.0f;
		static constexpr float ContentChamfer = 11.0f;
		static constexpr float ButtonChamfer = 5.0f;
		static constexpr float OuterBezel = 5.0f;
		static constexpr float InnerBezel = 3.0f;
		static constexpr float CyanSeamInset = 4.0f;
		static constexpr float ShadowOffset = 4.0f;

		// Shared tuner geometry keeps panel and header gaps consistent.
		static constexpr float TunePanelGap = 12.0f;
		static constexpr float TuneRailX = 15.0f;
		static constexpr float TuneRailWidth = 56.0f;
		static constexpr float TuneHeaderWidth = TuneRailWidth + TunePanelGap +
			213.0f + TunePanelGap + 560.0f;
		static constexpr float TuneHeaderHeight = 55.0f;
		static constexpr float TuneCommandWidth = 400.0f;
		static constexpr float TuneCommandHeight = 33.0f;
		static constexpr float TuneSidebarFrameWidth = 213.0f;
		static constexpr float TuneSidebarFrameHeight = 810.0f;
		static constexpr float TuneContentFrameWidth = 560.0f;
		static constexpr float TuneContentFrameHeight = 620.0f;
		static constexpr float TuneTitleHeight = 82.0f;
		static constexpr float TuneNavWidth = 210.0f;
		static constexpr float TuneNavHeight = 30.0f;
		static constexpr float TuneSearchWidth = 175.0f;
		static constexpr float TuneSearchHeight = 33.0f;
		static constexpr float TuneSectionWidth = 445.0f;
		static constexpr float TuneSectionHeight = 27.0f;
		static constexpr float TuneFeatureWidth = 440.0f;
		static constexpr float TuneFeatureHeight = 49.0f;
		static constexpr float TuneFeatureGap = 7.0f;

		// Coordinates are relative to the fixed root and scaled by gReferenceScale.
		static constexpr float TuneHeaderX = TuneRailX + TuneRailWidth + TunePanelGap;
		static constexpr float TuneHeaderY = 18.0f;
		static constexpr float TuneCommandX = 20.0f;
		static constexpr float TuneCommandY = 95.0f;
		static constexpr float TuneSidebarX = TuneHeaderX;
		static constexpr float TuneSidebarY = TuneHeaderY + TuneHeaderHeight + TunePanelGap;
		static constexpr float TuneContentX = TuneSidebarX + TuneSidebarFrameWidth + TunePanelGap;
		static constexpr float TuneContentY = TuneSidebarY;

		static constexpr float TuneSidebarTitleX = 13.0f;
		static constexpr float TuneSidebarTitleY = 6.0f;
		static constexpr float TuneSidebarSearchX = 19.0f;
		static constexpr float TuneSidebarSearchY = 48.0f;
		static constexpr float TuneSidebarNavX = 19.0f;
		static constexpr float TuneSidebarNavY = 93.0f;

		static constexpr float TuneSectionInsetX = 10.0f;
		static constexpr float TuneFeatureInsetX = 12.0f;
		static constexpr float TuneFirstSectionRowGap = 5.0f;
		static constexpr float TuneLaterSectionRowGap = 5.0f;
	};

	inline float gReferenceScale = 1.0f;

	inline void SetReferenceScale(float scale)
	{
		gReferenceScale = std::max(0.5f, scale);
	}

	inline float Ref(float value)
	{
		return value * gReferenceScale;
	}

	enum class ChromeStyle : std::uint8_t
	{
		Dark,
		Raised,
		Header,
		Sidebar,
		Content,
		Toolbar
	};


	inline float Scale(float value)
	{
		return value * Util::GetUIScale();
	}

	inline ImVec4 ToVec4(ImU32 color)
	{
		return ImGui::ColorConvertU32ToFloat4(color);
	}

	inline ImU32 MixColor(
		ImU32 from,
		ImU32 to,
		float t)
	{
		t = std::clamp(t, 0.0f, 1.0f);
		ImVec4 a =
			ImGui::ColorConvertU32ToFloat4(from);
		const ImVec4 b =
			ImGui::ColorConvertU32ToFloat4(to);

		a.x += (b.x - a.x) * t;
		a.y += (b.y - a.y) * t;
		a.z += (b.z - a.z) * t;
		a.w += (b.w - a.w) * t;

		return ImGui::ColorConvertFloat4ToU32(a);
	}

	inline ImU32 ScaleAlpha(
		ImU32 color,
		float amount)
	{
		ImVec4 value =
			ImGui::ColorConvertU32ToFloat4(color);
		value.w *=
			std::clamp(
				amount,
				0.0f,
				1.0f);
		return ImGui::ColorConvertFloat4ToU32(value);
	}

	inline float Animate01(
		const char* key,
		bool target,
		float response = 18.0f)
	{
		const ImGuiID id =
			ImGui::GetID(key);
		ImGuiStorage* storage =
			ImGui::GetStateStorage();

		float current =
			storage->GetFloat(
				id,
				target ? 1.0f : 0.0f);

		const float dt =
			std::clamp(
				ImGui::GetIO().DeltaTime,
				0.0f,
				0.050f);
		const float blend =
			1.0f -
			std::exp(
				-response * dt);

		current +=
			((target ? 1.0f : 0.0f) -
			 current) *
			blend;

		if (std::abs(
				current -
				(target ? 1.0f : 0.0f)) <
			0.001f) {
			current =
				target ? 1.0f : 0.0f;
		}

		storage->SetFloat(
			id,
			current);
		return current;
	}

	inline void SetAnimationValue(
		const char* key,
		float value)
	{
		ImGui::GetStateStorage()->SetFloat(
			ImGui::GetID(key),
			std::clamp(
				value,
				0.0f,
				1.0f));
	}

	// All legacy module DrawSettings() implementations pass through this scope.
	// It deliberately changes presentation only: no module values, callbacks,
	// persistence, renderer state or shader paths are touched.
	//
	// This is the compatibility layer that removes the visual "stock CS" feel
	// without rewriting dozens of mature module settings functions.
	struct EngineeringStyleScope
	{
		int styleVarCount = 0;
		int colorCount = 0;

		EngineeringStyleScope()
		{
			auto pushVar =
				[this](
					ImGuiStyleVar index,
					float value) {
					ImGui::PushStyleVar(
						index,
						value);
					++styleVarCount;
				};

			auto pushVar2 =
				[this](
					ImGuiStyleVar index,
					ImVec2 value) {
					ImGui::PushStyleVar(
						index,
						value);
					++styleVarCount;
				};

			auto pushColor =
				[this](
					ImGuiCol index,
					ImU32 value) {
					ImGui::PushStyleColor(
						index,
						ToVec4(value));
					++colorCount;
				};

			pushVar2(
				ImGuiStyleVar_FramePadding,
				ImVec2(
					Ref(7.0f),
					Ref(4.0f)));
			pushVar(
				ImGuiStyleVar_FrameRounding,
				Ref(2.0f));
			pushVar(
				ImGuiStyleVar_FrameBorderSize,
				Ref(1.0f));
			pushVar2(
				ImGuiStyleVar_ItemSpacing,
				ImVec2(
					Ref(8.0f),
					Ref(6.0f)));
			pushVar2(
				ImGuiStyleVar_ItemInnerSpacing,
				ImVec2(
					Ref(7.0f),
					Ref(5.0f)));
			pushVar(
				ImGuiStyleVar_IndentSpacing,
				Ref(17.0f));
			pushVar(
				ImGuiStyleVar_ScrollbarSize,
				Ref(9.0f));
			pushVar(
				ImGuiStyleVar_ScrollbarRounding,
				Ref(1.0f));
			pushVar(
				ImGuiStyleVar_GrabMinSize,
				Ref(9.0f));
			pushVar(
				ImGuiStyleVar_GrabRounding,
				Ref(1.0f));

			// PIXL chapter rail: intentionally flat, compact and technical.
			// This removes the stock blue ImGui / legacy renderer tab look
			// from General, Advanced and legacy module sub-pages.
			pushVar(
				ImGuiStyleVar_TabRounding,
				Ref(2.0f));
			pushVar(
				ImGuiStyleVar_TabBorderSize,
				0.0f);
			pushVar(
				ImGuiStyleVar_TabBarBorderSize,
				Ref(1.0f));
			pushVar(
				ImGuiStyleVar_TabBarOverlineSize,
				Ref(2.0f));
			pushVar2(
				ImGuiStyleVar_CellPadding,
				ImVec2(
					Ref(8.0f),
					Ref(5.0f)));

			pushColor(
				ImGuiCol_FrameBg,
				IM_COL32(10, 14, 17, 248));
			pushColor(
				ImGuiCol_FrameBgHovered,
				IM_COL32(16, 23, 27, 252));
			pushColor(
				ImGuiCol_FrameBgActive,
				IM_COL32(19, 29, 33, 255));

			pushColor(
				ImGuiCol_Button,
				IM_COL32(14, 19, 23, 250));
			pushColor(
				ImGuiCol_ButtonHovered,
				IM_COL32(23, 31, 35, 255));
			pushColor(
				ImGuiCol_ButtonActive,
				IM_COL32(10, 14, 17, 255));

			pushColor(
				ImGuiCol_Header,
				IM_COL32(14, 20, 24, 238));
			pushColor(
				ImGuiCol_HeaderHovered,
				IM_COL32(22, 31, 35, 250));
			pushColor(
				ImGuiCol_HeaderActive,
				IM_COL32(17, 26, 30, 255));

			pushColor(
				ImGuiCol_CheckMark,
				Colors::CyanBright);
			pushColor(
				ImGuiCol_SliderGrab,
				Colors::CyanSoft);
			pushColor(
				ImGuiCol_SliderGrabActive,
				Colors::CyanBright);

			pushColor(
				ImGuiCol_Border,
				Colors::BorderSoft);
			pushColor(
				ImGuiCol_Separator,
				Colors::BorderSoft);
			pushColor(
				ImGuiCol_SeparatorHovered,
				Colors::CyanSoft);
			pushColor(
				ImGuiCol_SeparatorActive,
				Colors::Cyan);

			pushColor(
				ImGuiCol_PopupBg,
				IM_COL32(9, 12, 15, 252));

			pushColor(
				ImGuiCol_ScrollbarBg,
				IM_COL32(7, 10, 12, 180));
			pushColor(
				ImGuiCol_ScrollbarGrab,
				IM_COL32(52, 67, 72, 220));
			pushColor(
				ImGuiCol_ScrollbarGrabHovered,
				Colors::CyanSoft);
			pushColor(
				ImGuiCol_ScrollbarGrabActive,
				Colors::Cyan);

			pushColor(
				ImGuiCol_Tab,
				IM_COL32(11, 15, 18, 245));
			pushColor(
				ImGuiCol_TabHovered,
				IM_COL32(20, 29, 33, 252));
			pushColor(
				ImGuiCol_TabSelected,
				IM_COL32(18, 27, 31, 255));
			pushColor(
				ImGuiCol_TabSelectedOverline,
				Colors::CyanBright);
			pushColor(
				ImGuiCol_TabDimmed,
				IM_COL32(9, 12, 15, 235));
			pushColor(
				ImGuiCol_TabDimmedSelected,
				IM_COL32(15, 21, 25, 245));
			pushColor(
				ImGuiCol_TabDimmedSelectedOverline,
				Colors::CyanSoft);

			// ImGui::ProgressBar uses PlotHistogram. Replace the inherited
			// legacy renderer blue bars with the PIXL cyan signal language.
			pushColor(
				ImGuiCol_PlotHistogram,
				Colors::CyanSoft);
			pushColor(
				ImGuiCol_PlotHistogramHovered,
				Colors::CyanBright);

			pushColor(
				ImGuiCol_TextSelectedBg,
				Colors::CyanGhost);
			pushColor(
				ImGuiCol_NavCursor,
				Colors::CyanSoft);
		}

		~EngineeringStyleScope()
		{
			if (colorCount > 0)
				ImGui::PopStyleColor(
					colorCount);
			if (styleVarCount > 0)
				ImGui::PopStyleVar(
					styleVarCount);
		}

		EngineeringStyleScope(
			const EngineeringStyleScope&) = delete;
		EngineeringStyleScope& operator=(
			const EngineeringStyleScope&) = delete;
	};


	// Dedicated performance-instrumentation skin.
	// This scope is intentionally stronger than EngineeringStyleScope because the
	// profiler is a PIXL diagnostic product surface rather than a legacy module page.
	struct ProfilerStyleScope
	{
		int styleVarCount = 0;
		int colorCount = 0;

		ProfilerStyleScope()
		{
			auto pushVar =
				[this](
					ImGuiStyleVar index,
					float value) {
					ImGui::PushStyleVar(
						index,
						value);
					++styleVarCount;
				};

			auto pushVar2 =
				[this](
					ImGuiStyleVar index,
					ImVec2 value) {
					ImGui::PushStyleVar(
						index,
						value);
					++styleVarCount;
				};

			auto pushColor =
				[this](
					ImGuiCol index,
					ImU32 value) {
					ImGui::PushStyleColor(
						index,
						ToVec4(value));
					++colorCount;
				};

			pushVar2(
				ImGuiStyleVar_WindowPadding,
				ImVec2(
					Ref(10.0f),
					Ref(9.0f)));
			pushVar(
				ImGuiStyleVar_WindowRounding,
				Ref(2.0f));
			pushVar(
				ImGuiStyleVar_WindowBorderSize,
				Ref(1.0f));
			pushVar2(
				ImGuiStyleVar_FramePadding,
				ImVec2(
					Ref(7.0f),
					Ref(4.0f)));
			pushVar(
				ImGuiStyleVar_FrameRounding,
				Ref(1.0f));
			pushVar(
				ImGuiStyleVar_FrameBorderSize,
				Ref(1.0f));
			pushVar2(
				ImGuiStyleVar_ItemSpacing,
				ImVec2(
					Ref(7.0f),
					Ref(5.0f)));
			pushVar2(
				ImGuiStyleVar_CellPadding,
				ImVec2(
					Ref(8.0f),
					Ref(5.0f)));
			pushVar(
				ImGuiStyleVar_ScrollbarSize,
				Ref(8.0f));
			pushVar(
				ImGuiStyleVar_ScrollbarRounding,
				Ref(1.0f));

			pushColor(
				ImGuiCol_WindowBg,
				IM_COL32(7, 10, 12, 248));
			pushColor(
				ImGuiCol_ChildBg,
				IM_COL32(8, 11, 14, 238));
			pushColor(
				ImGuiCol_Border,
				Colors::BorderBright);

			pushColor(
				ImGuiCol_FrameBg,
				IM_COL32(10, 15, 18, 248));
			pushColor(
				ImGuiCol_FrameBgHovered,
				IM_COL32(18, 27, 31, 252));
			pushColor(
				ImGuiCol_FrameBgActive,
				IM_COL32(20, 31, 36, 255));

			pushColor(
				ImGuiCol_Header,
				IM_COL32(15, 23, 27, 242));
			pushColor(
				ImGuiCol_HeaderHovered,
				IM_COL32(21, 33, 38, 252));
			pushColor(
				ImGuiCol_HeaderActive,
				IM_COL32(17, 29, 34, 255));

			// Tables are intentionally dark and low-contrast so timing data,
			// rather than stock ImGui header chrome, carries the hierarchy.
			pushColor(
				ImGuiCol_TableHeaderBg,
				IM_COL32(18, 30, 35, 255));
			pushColor(
				ImGuiCol_TableBorderStrong,
				Colors::BorderBright);
			pushColor(
				ImGuiCol_TableBorderLight,
				Colors::BorderSoft);
			pushColor(
				ImGuiCol_TableRowBg,
				IM_COL32(7, 10, 12, 232));
			pushColor(
				ImGuiCol_TableRowBgAlt,
				IM_COL32(10, 14, 17, 232));

			// PIXL signal bars: cyan/ice instead of the old red or stock blue.
			pushColor(
				ImGuiCol_PlotHistogram,
				Colors::CyanSoft);
			pushColor(
				ImGuiCol_PlotHistogramHovered,
				Colors::CyanBright);
			pushColor(
				ImGuiCol_PlotLines,
				Colors::CyanSoft);
			pushColor(
				ImGuiCol_PlotLinesHovered,
				Colors::CyanBright);

			pushColor(
				ImGuiCol_CheckMark,
				Colors::CyanBright);
			pushColor(
				ImGuiCol_SliderGrab,
				Colors::CyanSoft);
			pushColor(
				ImGuiCol_SliderGrabActive,
				Colors::CyanBright);

			pushColor(
				ImGuiCol_Separator,
				Colors::BorderSoft);
			pushColor(
				ImGuiCol_ScrollbarBg,
				IM_COL32(5, 8, 10, 190));
			pushColor(
				ImGuiCol_ScrollbarGrab,
				IM_COL32(46, 64, 70, 230));
			pushColor(
				ImGuiCol_ScrollbarGrabHovered,
				Colors::CyanSoft);
			pushColor(
				ImGuiCol_ScrollbarGrabActive,
				Colors::Cyan);

			pushColor(
				ImGuiCol_Text,
				Colors::Text);
			pushColor(
				ImGuiCol_TextDisabled,
				Colors::TextDim);
		}

		~ProfilerStyleScope()
		{
			if (colorCount > 0)
				ImGui::PopStyleColor(
					colorCount);
			if (styleVarCount > 0)
				ImGui::PopStyleVar(
					styleVarCount);
		}

		ProfilerStyleScope(
			const ProfilerStyleScope&) = delete;
		ProfilerStyleScope& operator=(
			const ProfilerStyleScope&) = delete;
	};

	inline ImU32 LerpColor(ImU32 a, ImU32 b, float t)
	{
		const ImVec4 ca = ImGui::ColorConvertU32ToFloat4(a);
		const ImVec4 cb = ImGui::ColorConvertU32ToFloat4(b);
		const ImVec4 c(
			ca.x + (cb.x - ca.x) * t,
			ca.y + (cb.y - ca.y) * t,
			ca.z + (cb.z - ca.z) * t,
			ca.w + (cb.w - ca.w) * t);
		return ImGui::ColorConvertFloat4ToU32(c);
	}

	inline void DrawVerticalGradient(
		ImDrawList* draw,
		ImVec2 min,
		ImVec2 max,
		ImU32 top,
		ImU32 bottom,
		int strips);

	inline void BuildChamferPath(
		ImDrawList* draw,
		ImVec2 min,
		ImVec2 max,
		float chamfer)
	{
		chamfer = std::max(
			0.0f,
			std::min(
				chamfer,
				std::min(
					(max.x - min.x) * 0.25f,
					(max.y - min.y) * 0.45f)));

		draw->PathLineTo(ImVec2(min.x + chamfer, min.y));
		draw->PathLineTo(ImVec2(max.x - chamfer, min.y));
		draw->PathLineTo(ImVec2(max.x, min.y + chamfer));
		draw->PathLineTo(ImVec2(max.x, max.y - chamfer));
		draw->PathLineTo(ImVec2(max.x - chamfer, max.y));
		draw->PathLineTo(ImVec2(min.x + chamfer, max.y));
		draw->PathLineTo(ImVec2(min.x, max.y - chamfer));
		draw->PathLineTo(ImVec2(min.x, min.y + chamfer));
	}

	inline void FillChamfered(
		ImDrawList* draw,
		ImVec2 min,
		ImVec2 max,
		float chamfer,
		ImU32 color)
	{
		BuildChamferPath(draw, min, max, chamfer);
		draw->PathFillConvex(color);
	}

	inline void StrokeChamfered(
		ImDrawList* draw,
		ImVec2 min,
		ImVec2 max,
		float chamfer,
		ImU32 color,
		float thickness)
	{
		BuildChamferPath(draw, min, max, chamfer);
		draw->PathStroke(color, ImDrawFlags_Closed, thickness);
	}

	inline void DrawChamferGlow(
		ImDrawList* draw,
		ImVec2 min,
		ImVec2 max,
		float chamfer,
		bool strong)
	{
		const int passes = strong ? 4 : 3;
		for (int i = passes; i >= 1; --i) {
			const float inset =
				Ref(1.5f + static_cast<float>(i) * 1.25f);
			StrokeChamfered(
				draw,
				ImVec2(min.x + inset, min.y + inset),
				ImVec2(max.x - inset, max.y - inset),
				std::max(0.0f, chamfer - inset),
				IM_COL32(
					72,
					236,
					241,
					strong ? (13 + i * 10) : (9 + i * 7)),
				Ref(1.0f));
		}
	}

	inline void DrawReferenceBrush(
		ImDrawList* draw,
		ImVec2 min,
		ImVec2 max,
		bool lightFace)
	{
		const float step = std::max(Ref(2.0f), 2.0f);
		int index = 0;
		for (float y = min.y + step;
			 y < max.y - step;
			 y += step, ++index) {
			const ImU32 c = lightFace
				? ((index & 1)
					? IM_COL32(236, 247, 249, 8)
					: IM_COL32(17, 34, 40, 8))
				: ((index & 1)
					? IM_COL32(195, 220, 225, 5)
					: IM_COL32(2, 9, 12, 9));

			draw->AddLine(
				ImVec2(min.x + Ref(4.0f), y),
				ImVec2(max.x - Ref(4.0f), y),
				c,
				1.0f);
		}
	}

	inline void DrawReferenceSteelFace(
		ImDrawList* draw,
		ImVec2 min,
		ImVec2 max,
		bool lightFace)
	{
		const ImU32 top = lightFace
			? Colors::RefSteelTop
			: IM_COL32(73, 99, 111, 255);
		const ImU32 bottom = lightFace
			? Colors::RefSteelBottom
			: IM_COL32(31, 49, 57, 255);

		DrawVerticalGradient(
			draw,
			min,
			max,
			top,
			bottom,
			36);

		const float midY =
			min.y + (max.y - min.y) * 0.46f;
		draw->AddRectFilled(
			ImVec2(min.x, midY - Ref(8.0f)),
			ImVec2(max.x, midY + Ref(8.0f)),
			lightFace
				? IM_COL32(177, 202, 211, 20)
				: IM_COL32(116, 148, 158, 11));

		DrawReferenceBrush(
			draw,
			min,
			max,
			lightFace);
	}

	inline void DrawVerticalGradient(
		ImDrawList* draw,
		ImVec2 min,
		ImVec2 max,
		ImU32 top,
		ImU32 bottom,
		int strips = 20)
	{
		if (!draw || max.x <= min.x || max.y <= min.y)
			return;

		strips = std::max(strips, 2);
		const float height = max.y - min.y;
		for (int i = 0; i < strips; ++i) {
			const float t0 = static_cast<float>(i) / static_cast<float>(strips);
			const float t1 = static_cast<float>(i + 1) / static_cast<float>(strips);
			draw->AddRectFilled(
				ImVec2(min.x, min.y + height * t0),
				ImVec2(max.x, min.y + height * t1 + 1.0f),
				LerpColor(top, bottom, (t0 + t1) * 0.5f));
		}
	}

	inline void DrawBrushedLines(ImDrawList* draw, ImVec2 min, ImVec2 max, bool bright)
	{
		if (!draw)
			return;

		const float step = std::max(2.0f, Scale(4.0f));
		int line = 0;
		for (float y = min.y + step; y < max.y - step; y += step, ++line) {
			const ImU32 c = bright
				? ((line & 1) ? IM_COL32(230, 242, 244, 6) : IM_COL32(4, 11, 13, 7))
				: ((line & 1) ? IM_COL32(190, 212, 216, 4) : IM_COL32(2, 8, 10, 6));
			draw->AddLine(ImVec2(min.x + Scale(3.0f), y), ImVec2(max.x - Scale(3.0f), y), c, 1.0f);
		}
	}

	inline void DrawGlowRect(ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding, bool strong)
	{
		if (!draw || max.x <= min.x || max.y <= min.y)
			return;

		// A2: never expand outside the owning rectangle. The target uses a crisp
		// cyan inner seam with a restrained halo, not repeated external frames.
		const int passes = strong ? 3 : 2;
		for (int i = passes; i >= 1; --i) {
			const float inset =
				Scale(1.15f + static_cast<float>(i) * 1.25f);
			const ImU32 c = strong
				? IM_COL32(63, 232, 237, 18 + i * 10)
				: IM_COL32(63, 219, 225, 12 + i * 7);
			draw->AddRect(
				ImVec2(min.x + inset, min.y + inset),
				ImVec2(max.x - inset, max.y - inset),
				c,
				std::max(0.0f, rounding - inset),
				0,
				Scale(1.0f));
		}
	}

	inline void DrawTechCorners(ImDrawList* draw, ImVec2 min, ImVec2 max, ImU32 color)
	{
		if (!draw)
			return;

		const float c = Scale(10.0f);
		const float t = Scale(1.25f);

		// top-left
		draw->AddLine(ImVec2(min.x + c, min.y), ImVec2(min.x, min.y + c), color, t);
		// top-right
		draw->AddLine(ImVec2(max.x - c, min.y), ImVec2(max.x, min.y + c), color, t);
		// bottom-left
		draw->AddLine(ImVec2(min.x, max.y - c), ImVec2(min.x + c, max.y), color, t);
		// bottom-right
		draw->AddLine(ImVec2(max.x, max.y - c), ImVec2(max.x - c, max.y), color, t);
	}

	inline void DrawChrome(ImVec2 min, ImVec2 max, ChromeStyle style, bool accent = false)
	{
		ImDrawList* draw = ImGui::GetWindowDrawList();
		if (!draw || max.x <= min.x || max.y <= min.y)
			return;

		ImU32 face = Colors::Inset;
		ImU32 edge = Colors::BorderSoft;
		switch (style) {
		case ChromeStyle::Header:
			face = IM_COL32(16, 20, 24, 252);
			edge = Colors::BorderBright;
			break;
		case ChromeStyle::Sidebar:
			face = IM_COL32(9, 12, 15, 250);
			edge = IM_COL32(79, 91, 97, 210);
			break;
		case ChromeStyle::Content:
			face = IM_COL32(12, 15, 18, 250);
			edge = IM_COL32(88, 101, 107, 215);
			break;
		case ChromeStyle::Raised:
			face = IM_COL32(24, 29, 33, 250);
			edge = Colors::Border;
			break;
		case ChromeStyle::Toolbar:
			face = IM_COL32(11, 14, 17, 248);
			edge = Colors::BorderSoft;
			break;
		default:
			break;
		}

		const float notch = Ref(
			style == ChromeStyle::Header || style == ChromeStyle::Content
				? 7.0f : 4.0f);

		FillChamfered(
			draw,
			ImVec2(min.x + Ref(2.0f), min.y + Ref(3.0f)),
			ImVec2(max.x + Ref(1.0f), max.y + Ref(1.0f)),
			notch,
			IM_COL32(0, 0, 0, 115));
		FillChamfered(draw, min, max, notch, face);
		StrokeChamfered(draw, min, max, notch, edge, Ref(1.0f));

		draw->AddLine(
			ImVec2(min.x + notch + Ref(3.0f), min.y + Ref(1.0f)),
			ImVec2(max.x - notch - Ref(3.0f), min.y + Ref(1.0f)),
			IM_COL32(220, 225, 225, 34),
			Ref(1.0f));

		if (accent) {
			draw->AddLine(
				ImVec2(min.x + Ref(4.0f), min.y + Ref(4.0f)),
				ImVec2(min.x + Ref(4.0f), max.y - Ref(4.0f)),
				Colors::Cyan,
				Ref(2.0f));
			draw->AddLine(
				ImVec2(min.x + Ref(4.0f), min.y + Ref(4.0f)),
				ImVec2(min.x + Ref(54.0f), min.y + Ref(4.0f)),
				Colors::CyanSoft,
				Ref(1.0f));
		}
	}


	inline void DrawWindowShell(ImVec2 min, ImVec2 max)
	{
		ImDrawList* draw = ImGui::GetWindowDrawList();
		if (!draw || max.x <= min.x || max.y <= min.y)
			return;

		draw->AddRectFilled(min, max, Colors::Window);
		draw->AddRect(
			ImVec2(min.x + Ref(1.0f), min.y + Ref(1.0f)),
			ImVec2(max.x - Ref(1.0f), max.y - Ref(1.0f)),
			IM_COL32(102, 115, 121, 155),
			Ref(3.0f), 0, Ref(1.0f));
	}


	class ChromeScope
	{
	public:
		ChromeScope(
			const char* id,
			ImVec2 size,
			ChromeStyle style,
			bool accent = false,
			ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar,
			float padding = ThemeManager::Constants::PRODUCT_PANEL_PADDING) :
			style_(style), accent_(accent)
		{
			(void)padding;
			const float lockedPadding =
				(style == ChromeStyle::Sidebar || style == ChromeStyle::Content)
					? 0.0f : 8.0f;
			ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, Ref(2.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Ref(lockedPadding), Ref(lockedPadding)));
			ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
			visible_ = ImGui::BeginChild(id, size, ImGuiChildFlags_None, flags);
			if (visible_) {
				const ImVec2 min = ImGui::GetWindowPos();
				const ImVec2 max(min.x + ImGui::GetWindowSize().x, min.y + ImGui::GetWindowSize().y);
				DrawChrome(min, max, style_, accent_);
			}
		}

		~ChromeScope()
		{
			ImGui::EndChild();
			ImGui::PopStyleColor();
			ImGui::PopStyleVar(3);
		}

		ChromeScope(const ChromeScope&) = delete;
		ChromeScope& operator=(const ChromeScope&) = delete;
		explicit operator bool() const { return visible_; }

	private:
		bool visible_ = false;
		ChromeStyle style_ = ChromeStyle::Dark;
		bool accent_ = false;
	};

	// Backward-compatible wrapper used by existing advanced cards.
	class PanelScope
	{
	public:
		PanelScope(
			const char* id,
			ImVec2 size = ImVec2(0, 0),
			bool accent = false,
			ImGuiChildFlags childFlags = ImGuiChildFlags_None,
			ImGuiWindowFlags windowFlags = ImGuiWindowFlags_None,
			bool raised = false) :
			accent_(accent), raised_(raised)
		{
			ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, Scale(ThemeManager::Constants::PRODUCT_PANEL_ROUNDING));
			ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
			ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
			visible_ = ImGui::BeginChild(id, size, childFlags, windowFlags);
			if (visible_) {
				const ImVec2 min = ImGui::GetWindowPos();
				const ImVec2 max(min.x + ImGui::GetWindowSize().x, min.y + ImGui::GetWindowSize().y);
				DrawChrome(
					min,
					max,
					accent_ ? ChromeStyle::Content : (raised_ ? ChromeStyle::Raised : ChromeStyle::Dark),
					accent_);
			}
		}

		~PanelScope()
		{
			ImGui::EndChild();
			ImGui::PopStyleColor();
			ImGui::PopStyleVar(2);
		}

		PanelScope(const PanelScope&) = delete;
		PanelScope& operator=(const PanelScope&) = delete;
		explicit operator bool() const { return visible_; }

	private:
		bool visible_ = false;
		bool accent_ = false;
		bool raised_ = false;
	};

	inline void PageTitle(const char* title, const char* subtitle = nullptr)
	{
		const ImVec2 start = ImGui::GetCursorScreenPos();
		const float width = ImGui::GetContentRegionAvail().x;
		const float height = Ref(Layout::TuneTitleHeight);
		ImDrawList* draw = ImGui::GetWindowDrawList();

		draw->AddLine(
			ImVec2(start.x, start.y + height - Ref(1.0f)),
			ImVec2(start.x + width, start.y + height - Ref(1.0f)),
			Colors::BorderSoft, Ref(1.0f));
		draw->AddLine(
			ImVec2(start.x, start.y + height - Ref(1.0f)),
			ImVec2(start.x + Ref(96.0f), start.y + height - Ref(1.0f)),
			Colors::CyanSoft, Ref(2.0f));

		ImGui::SetCursorScreenPos(
			ImVec2(start.x + Ref(13.0f), start.y + Ref(10.0f)));
		{
			MenuFonts::FontRoleGuard titleFont(Menu::FontRole::Title);
			ImGui::SetWindowFontScale(1.42f);
			ImGui::TextColored(ToVec4(Colors::Text), "%s", title);
			ImGui::SetWindowFontScale(1.0f);
		}

		if (subtitle && subtitle[0] != '\0') {
			ImGui::SetCursorScreenPos(
				ImVec2(start.x + Ref(14.0f), start.y + Ref(49.0f)));
			MenuFonts::FontRoleGuard subtext(Menu::FontRole::Subtext);
			ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(Colors::TextMuted));
			ImGui::PushTextWrapPos(start.x + width - Ref(22.0f));
			ImGui::TextUnformatted(subtitle);
			ImGui::PopTextWrapPos();
			ImGui::PopStyleColor();
		}

		ImGui::SetCursorScreenPos(start);
		ImGui::Dummy(ImVec2(width, height));
	}


	inline void SectionBanner(const char* label)
	{
		ImGui::PushID(label);
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float h = Ref(Layout::TuneSectionHeight);
		const float w =
			std::min(ImGui::GetContentRegionAvail().x, Ref(Layout::TuneSectionWidth));

		ImGui::InvisibleButton("##section", ImVec2(w, h));
		ImDrawList* draw = ImGui::GetWindowDrawList();
		const float cy = p.y + h * 0.5f;
		const float r = Ref(4.0f);
		const ImVec2 diamond(p.x + Ref(8.0f), cy);

		draw->AddQuadFilled(
			ImVec2(diamond.x, diamond.y - r),
			ImVec2(diamond.x + r, diamond.y),
			ImVec2(diamond.x, diamond.y + r),
			ImVec2(diamond.x - r, diamond.y),
			Colors::CyanSoft);

		const ImVec2 textSize = ImGui::CalcTextSize(label);
		draw->AddText(
			ImVec2(p.x + Ref(20.0f), cy - textSize.y * 0.5f),
			Colors::TextMuted, label);

		draw->AddLine(
			ImVec2(p.x + Ref(28.0f) + textSize.x, cy),
			ImVec2(p.x + w, cy),
			Colors::BorderSoft, Ref(1.0f));
		ImGui::PopID();
	}


	inline void SectionLabel(const char* label)
	{
		MenuFonts::FontRoleGuard heading(Menu::FontRole::Subheading);
		ImGui::TextColored(ToVec4(Colors::CyanBright), "%s", label);
		const ImVec2 p = ImGui::GetCursorScreenPos();
		ImGui::GetWindowDrawList()->AddLine(
			p,
			ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y),
			Colors::BorderSoft,
			Scale(1.0f));
		ImGui::Dummy(ImVec2(0, Scale(5.0f)));
	}

	inline void RailDivider(const char* label)
	{
		const float width = ImGui::GetContentRegionAvail().x;
		const ImVec2 start = ImGui::GetCursorScreenPos();
		const float h = Ref(25.0f);
		ImGui::Dummy(ImVec2(width, h));

		ImDrawList* draw = ImGui::GetWindowDrawList();
		const ImVec2 textSize = ImGui::CalcTextSize(label);
		const float centerX = start.x + width * 0.5f;
		const float textY = start.y + (h - textSize.y) * 0.5f;
		const float gap = Ref(10.0f);
		const float lineY = start.y + h * 0.5f;

		draw->AddLine(
			ImVec2(start.x + Ref(6.0f), lineY),
			ImVec2(centerX - textSize.x * 0.5f - gap, lineY),
			Colors::BorderBright,
			Ref(1.0f));
		draw->AddLine(
			ImVec2(centerX + textSize.x * 0.5f + gap, lineY),
			ImVec2(start.x + width - Ref(6.0f), lineY),
			Colors::BorderBright,
			Ref(1.0f));
		draw->AddText(
			ImVec2(centerX - textSize.x * 0.5f, textY),
			Colors::TextMuted,
			label);
	}

	inline bool NavItem(const char* id, const char* label, bool selected, float height = 0.0f)
	{
		ImGui::PushID(id);

		const float baseWidth =
			std::min(
				ImGui::GetContentRegionAvail().x,
				Ref(Layout::TuneNavWidth));
		const float h =
			height > 0.0f
				? height
				: Ref(Layout::TuneNavHeight);

		const ImVec2 base =
			ImGui::GetCursorScreenPos();

		ImGui::SetCursorScreenPos(base);
		const bool pressed =
			ImGui::InvisibleButton(
				"##nav",
				ImVec2(
					baseWidth,
					h));
		const bool hovered =
			ImGui::IsItemHovered();

		const float selectT =
			Animate01(
				"##selectedMotion",
				selected,
				19.0f);
		const float hoverT =
			Animate01(
				"##hoverMotion",
				hovered,
				24.0f);

		// FINAL TUNER NAV GEOMETRY
		// Selection is communicated through the cyan rune, surface transition
		// and left seam only. All categories share one immutable baseline so
		// the active row never protrudes or remains visually offset.
		const ImVec2 p =
			base;
		const float w =
			baseWidth;

		ImDrawList* draw =
			ImGui::GetWindowDrawList();

		const float surfaceT =
			std::max(
				selectT,
				hoverT * 0.72f);

		if (surfaceT > 0.01f) {
			FillChamfered(
				draw,
				p,
				ImVec2(
					p.x + w,
					p.y + h),
				Ref(3.0f),
				MixColor(
					IM_COL32(11, 15, 18, 150),
					IM_COL32(20, 27, 31, 245),
					surfaceT));
		}

		const float cy =
			p.y + h * 0.5f;
		const float r =
			Ref(4.0f);
		const ImVec2 rune(
			p.x + Ref(12.0f),
			cy);

		draw->AddQuad(
			ImVec2(rune.x, rune.y - r),
			ImVec2(rune.x + r, rune.y),
			ImVec2(rune.x, rune.y + r),
			ImVec2(rune.x - r, rune.y),
			MixColor(
				Colors::BorderSoft,
				Colors::BorderBright,
				std::max(
					hoverT,
					selectT)),
			Ref(1.0f));

		if (selectT > 0.01f) {
			draw->AddQuadFilled(
				ImVec2(rune.x, rune.y - r),
				ImVec2(rune.x + r, rune.y),
				ImVec2(rune.x, rune.y + r),
				ImVec2(rune.x - r, rune.y),
				ScaleAlpha(
					Colors::Cyan,
					selectT));
		}

		const ImVec2 textSize =
			ImGui::CalcTextSize(label);
		const float textNudge =
			Ref(1.5f) *
			selectT;

		draw->AddText(
			ImVec2(
				p.x +
					Ref(25.0f) +
					textNudge,
				cy -
					textSize.y * 0.5f),
			MixColor(
				Colors::TextMuted,
				Colors::Text,
				std::max(
					selectT,
					hoverT * 0.55f)),
			label);

		if (selectT > 0.01f) {
			draw->AddLine(
				ImVec2(
					p.x + Ref(1.0f),
					p.y + Ref(4.0f)),
				ImVec2(
					p.x + Ref(1.0f),
					p.y + h - Ref(4.0f)),
				ScaleAlpha(
					Colors::Cyan,
					selectT),
				Ref(2.0f));
		}

		ImGui::PopID();
		return pressed;
	}

	inline bool Toggle(const char* id, bool* value);

	struct FeatureRowResult
	{
		bool openPressed = false;
		bool togglePressed = false;
		bool rowHovered = false;
		bool toggleHovered = false;
	};

	inline FeatureRowResult FeatureRow(
		const char* id,
		const char* label,
		bool* enabled,
		bool open = false)
	{
		FeatureRowResult result{};
		ImGui::PushID(id);

		const ImVec2 start =
			ImGui::GetCursorScreenPos();
		const float width =
			std::min(
				ImGui::GetContentRegionAvail().x,
				Ref(Layout::TuneFeatureWidth));
		const float height =
			Ref(Layout::TuneFeatureHeight);
		const float gap =
			Ref(Layout::TuneFeatureGap);
		const float latchWidth =
			Ref(60.0f);
		const float latchHeight =
			Ref(26.0f);
		const float latchX =
			width -
			latchWidth -
			Ref(8.0f);
		const float latchY =
			(height -
			 latchHeight) *
			0.5f;

		ImGui::SetCursorScreenPos(start);
		result.openPressed =
			ImGui::InvisibleButton(
				"##OpenFeature",
				ImVec2(
					width -
						latchWidth -
						Ref(14.0f),
					height));
		result.rowHovered =
			ImGui::IsItemHovered();

		const float openT =
			Animate01(
				"##openMotion",
				open,
				20.0f);
		const float hoverT =
			Animate01(
				"##rowHoverMotion",
				result.rowHovered,
				24.0f);

		ImDrawList* draw =
			ImGui::GetWindowDrawList();
		const ImVec2 rowMax(
			start.x + width,
			start.y + height);

		const ImU32 idleFace =
			IM_COL32(
				13,
				18,
				21,
				245);
		const ImU32 hoverFace =
			IM_COL32(
				18,
				24,
				28,
				248);
		const ImU32 openFace =
			IM_COL32(
				20,
				27,
				31,
				252);

		const ImU32 face =
			MixColor(
				MixColor(
					idleFace,
					hoverFace,
					hoverT),
				openFace,
				openT);

		FillChamfered(
			draw,
			start,
			rowMax,
			Ref(4.0f),
			face);

		StrokeChamfered(
			draw,
			start,
			rowMax,
			Ref(4.0f),
			MixColor(
				Colors::BorderSoft,
				Colors::BorderBright,
				std::max(
					openT,
					hoverT * 0.55f)),
			Ref(1.0f));

		if (openT > 0.01f) {
			draw->AddLine(
				ImVec2(
					start.x +
						Ref(2.0f),
					start.y +
						Ref(5.0f)),
				ImVec2(
					start.x +
						Ref(2.0f),
					rowMax.y -
						Ref(5.0f)),
				ScaleAlpha(
					Colors::Cyan,
					openT),
				Ref(2.0f));
		}

		const float cy =
			start.y +
			height * 0.5f;
		const float gx =
			start.x +
			Ref(18.0f);
		const ImU32 gate =
			MixColor(
				Colors::TextMuted,
				Colors::CyanBright,
				std::max(
					openT,
					hoverT * 0.45f));

		// A restrained gate motion: the chevron moves 2px right as the
		// engineering surface opens.
		const float gateOffset =
			Ref(2.0f) *
			openT;

		draw->AddLine(
			ImVec2(
				gx -
					Ref(3.0f) +
					gateOffset,
				cy -
					Ref(5.0f)),
			ImVec2(
				gx +
					Ref(2.0f) +
					gateOffset,
				cy),
			gate,
			Ref(1.5f));
		draw->AddLine(
			ImVec2(
				gx +
					Ref(2.0f) +
					gateOffset,
				cy),
			ImVec2(
				gx -
					Ref(3.0f) +
					gateOffset,
				cy +
					Ref(5.0f)),
			gate,
			Ref(1.5f));

		const ImVec2 textSize =
			ImGui::CalcTextSize(label);
		draw->AddText(
			ImVec2(
				start.x +
					Ref(34.0f),
				cy -
					textSize.y *
						0.5f),
			MixColor(
				Colors::TextMuted,
				Colors::Text,
				std::max(
					openT,
					hoverT * 0.55f)),
			label);

		ImGui::SetCursorScreenPos(
			ImVec2(
				start.x + latchX,
				start.y + latchY));
		result.togglePressed =
			Toggle(
				"##FeatureLatch",
				enabled);
		result.toggleHovered =
			ImGui::IsItemHovered();

		ImGui::SetCursorScreenPos(
			ImVec2(
				start.x,
				start.y + height));
		ImGui::Dummy(
			ImVec2(
				width,
				gap));

		ImGui::PopID();
		return result;
	}


	inline bool Toggle(const char* id, bool* value)
	{
		ImGui::PushID(id);

		const float w =
			Ref(60.0f);
		const float h =
			Ref(26.0f);
		const ImVec2 p =
			ImGui::GetCursorScreenPos();

		const bool pressed =
			ImGui::InvisibleButton(
				"##sigilSwitch",
				ImVec2(
					w,
					h));

		if (pressed)
			*value = !*value;

		const bool hovered =
			ImGui::IsItemHovered();
		const bool active =
			ImGui::IsItemActive();

		const float onT =
			Animate01(
				"##stateMotion",
				*value,
				22.0f);
		const float hoverT =
			Animate01(
				"##toggleHoverMotion",
				hovered,
				25.0f);

		ImDrawList* draw =
			ImGui::GetWindowDrawList();

		const ImU32 idleFace =
			IM_COL32(
				17,
				22,
				26,
				255);
		const ImU32 hoverFace =
			IM_COL32(
				24,
				30,
				34,
				255);
		const ImU32 activeFace =
			IM_COL32(
				10,
				13,
				16,
				255);

		FillChamfered(
			draw,
			p,
			ImVec2(
				p.x + w,
				p.y + h),
			Ref(4.0f),
			active
				? activeFace
				: MixColor(
					idleFace,
					hoverFace,
					hoverT));

		StrokeChamfered(
			draw,
			p,
			ImVec2(
				p.x + w,
				p.y + h),
			Ref(4.0f),
			MixColor(
				Colors::Border,
				Colors::CyanSoft,
				std::max(
					onT,
					hoverT * 0.35f)),
			Ref(1.0f));

		const float cy =
			p.y +
			h * 0.5f;
		const float r =
			Ref(5.0f);
		const ImVec2 rune(
			p.x +
				Ref(13.0f),
			cy);

		draw->AddQuad(
			ImVec2(
				rune.x,
				rune.y - r),
			ImVec2(
				rune.x + r,
				rune.y),
			ImVec2(
				rune.x,
				rune.y + r),
			ImVec2(
				rune.x - r,
				rune.y),
			MixColor(
				Colors::TextDim,
				Colors::CyanBright,
				std::max(
					onT,
					hoverT * 0.35f)),
			Ref(1.0f));

		if (onT > 0.01f) {
			draw->AddQuadFilled(
				ImVec2(
					rune.x,
					rune.y - r),
				ImVec2(
					rune.x + r,
					rune.y),
				ImVec2(
					rune.x,
					rune.y + r),
				ImVec2(
					rune.x - r,
					rune.y),
				ScaleAlpha(
					Colors::Cyan,
					onT));
		}

		const char* state =
			*value ? "ON" : "OFF";
		const ImVec2 textSize =
			ImGui::CalcTextSize(state);

		draw->AddText(
			ImVec2(
				p.x +
					Ref(27.0f),
				cy -
					textSize.y *
						0.5f),
			MixColor(
				Colors::TextDim,
				Colors::Text,
				std::max(
					onT,
					hoverT * 0.45f)),
			state);

		ImGui::PopID();
		return pressed;
	}

	inline bool ActionButton(const char* label, ImVec2 size = ImVec2(0, 0), bool primary = false)
	{
		const ImVec2 textSize =
			ImGui::CalcTextSize(label);

		if (size.x <= 0.0f)
			size.x =
				textSize.x +
				Ref(
					primary
						? 28.0f
						: 24.0f);

		if (size.y <= 0.0f)
			size.y =
				Ref(30.0f);

		const ImVec2 p =
			ImGui::GetCursorScreenPos();
		const bool pressed =
			ImGui::InvisibleButton(
				label,
				size);
		const bool hovered =
			ImGui::IsItemHovered();
		const bool active =
			ImGui::IsItemActive();

		const float hoverT =
			Animate01(
				"##buttonHoverMotion",
				hovered,
				24.0f);

		ImDrawList* draw =
			ImGui::GetWindowDrawList();

		const ImU32 face =
			active
				? IM_COL32(
					11,
					14,
					17,
					255)
				: MixColor(
					IM_COL32(
						18,
						23,
						27,
						255),
					IM_COL32(
						27,
						33,
						37,
						255),
					hoverT);

		FillChamfered(
			draw,
			p,
			ImVec2(
				p.x + size.x,
				p.y + size.y),
			Ref(4.0f),
			face);

		const float accentT =
			std::max(
				primary ? 0.72f : 0.0f,
				hoverT);

		StrokeChamfered(
			draw,
			p,
			ImVec2(
				p.x + size.x,
				p.y + size.y),
			Ref(4.0f),
			MixColor(
				Colors::Border,
				Colors::CyanSoft,
				accentT),
			Ref(1.0f));

		if (primary ||
			hoverT > 0.01f) {
			draw->AddLine(
				ImVec2(
					p.x + Ref(4.0f),
					p.y + Ref(5.0f)),
				ImVec2(
					p.x + Ref(4.0f),
					p.y + size.y - Ref(5.0f)),
				ScaleAlpha(
					Colors::Cyan,
					std::max(
						primary
							? 1.0f
							: 0.0f,
						hoverT * 0.72f)),
				Ref(2.0f));
		}

		draw->AddText(
			ImVec2(
				p.x +
					(size.x -
					 textSize.x) *
						0.5f,
				p.y +
					(size.y -
					 textSize.y) *
						0.5f),
			MixColor(
				Colors::TextMuted,
				Colors::Text,
				std::max(
					primary
						? 1.0f
						: 0.0f,
					hoverT)),
			label);

		return pressed;
	}


	inline bool PageButton(
		const char* id,
		const char* label,
		bool selected,
		ImVec2 size)
	{
		ImGui::PushID(id);

		const ImVec2 p =
			ImGui::GetCursorScreenPos();
		const bool pressed =
			ImGui::InvisibleButton(
				"##page",
				size);
		const bool hovered =
			ImGui::IsItemHovered();

		const float selectT =
			Animate01(
				"##pageSelectedMotion",
				selected,
				18.0f);
		const float hoverT =
			Animate01(
				"##pageHoverMotion",
				hovered,
				24.0f);

		ImDrawList* draw =
			ImGui::GetWindowDrawList();

		const float surfaceT =
			std::max(
				selectT,
				hoverT * 0.72f);

		if (surfaceT > 0.01f) {
			FillChamfered(
				draw,
				p,
				ImVec2(
					p.x + size.x,
					p.y + size.y),
				Ref(4.0f),
				MixColor(
					IM_COL32(
						12,
						16,
						20,
						160),
					IM_COL32(
						20,
						27,
						31,
						248),
					surfaceT));
		}

		const float cy =
			p.y +
			size.y * 0.5f;
		const float rune =
			Ref(4.0f);
		const float runeX =
			p.x +
			Ref(15.0f);

		draw->AddQuad(
			ImVec2(
				runeX,
				cy - rune),
			ImVec2(
				runeX + rune,
				cy),
			ImVec2(
				runeX,
				cy + rune),
			ImVec2(
				runeX - rune,
				cy),
			MixColor(
				Colors::BorderSoft,
				Colors::CyanBright,
				std::max(
					selectT,
					hoverT * 0.55f)),
			Ref(1.0f));

		if (selectT > 0.01f) {
			draw->AddQuadFilled(
				ImVec2(
					runeX,
					cy - rune),
				ImVec2(
					runeX + rune,
					cy),
				ImVec2(
					runeX,
					cy + rune),
				ImVec2(
					runeX - rune,
					cy),
				ScaleAlpha(
					Colors::Cyan,
					selectT));
		}

		const ImVec2 textSize =
			ImGui::CalcTextSize(label);
		draw->AddText(
			ImVec2(
				p.x +
					Ref(29.0f),
				cy -
					textSize.y *
						0.5f),
			MixColor(
				Colors::TextMuted,
				Colors::Text,
				std::max(
					selectT,
					hoverT * 0.50f)),
			label);

		const float underlineT =
			std::max(
				selectT,
				hoverT * 0.22f);

		if (underlineT > 0.01f) {
			const float fullWidth =
				size.x -
				Ref(16.0f);
			const float animatedWidth =
				fullWidth *
				underlineT;

			draw->AddLine(
				ImVec2(
					p.x +
						Ref(8.0f),
					p.y +
						size.y -
						Ref(2.0f)),
				ImVec2(
					p.x +
						Ref(8.0f) +
						animatedWidth,
					p.y +
						size.y -
						Ref(2.0f)),
				ScaleAlpha(
					Colors::CyanSoft,
					std::min(
						1.0f,
						0.35f +
							underlineT)),
				Ref(2.0f));
		}

		ImGui::PopID();
		return pressed;
	}

	inline float LinearToDisplayT(
		float value,
		float minValue,
		float maxValue,
		bool logarithmic)
	{
		if (maxValue <= minValue)
			return 0.0f;

		if (logarithmic && minValue > 0.0f && maxValue > 0.0f && value > 0.0f) {
			const float lo = std::log(minValue);
			const float hi = std::log(maxValue);
			return std::clamp((std::log(value) - lo) / (hi - lo), 0.0f, 1.0f);
		}

		return std::clamp(
			(value - minValue) / (maxValue - minValue),
			0.0f,
			1.0f);
	}

	inline float DisplayTToLinear(
		float t,
		float minValue,
		float maxValue,
		bool logarithmic)
	{
		t = std::clamp(t, 0.0f, 1.0f);

		if (logarithmic && minValue > 0.0f && maxValue > 0.0f) {
			const float lo = std::log(minValue);
			const float hi = std::log(maxValue);
			return std::exp(lo + (hi - lo) * t);
		}

		return minValue + (maxValue - minValue) * t;
	}

	// Custom tracks use InvisibleButton rather than ImGui's native slider path.
	// Publish only this frame's activity so the tuner preview can include them.
	inline int activeSliderDragFrame = -1;

	inline bool SliderFloatField(
		const char* label,
		float* value,
		float minValue,
		float maxValue,
		const char* format = "%.2f",
		bool logarithmic = false)
	{
		ImGui::PushID(label);

		const ImVec2 start = ImGui::GetCursorScreenPos();
		const float width = std::min(ImGui::GetContentRegionAvail().x, Ref(720.0f));
		const float height = Ref(30.0f);
		const float labelWidth =
			std::clamp(width * 0.34f, Ref(120.0f), Ref(205.0f));
		const float valueWidth = Ref(72.0f);
		const float trackMinX = start.x + labelWidth;
		const float trackMaxX = start.x + width - valueWidth;
		const float trackWidth = std::max(Ref(70.0f), trackMaxX - trackMinX);
		const float cy = start.y + height * 0.5f;

		const ImVec2 trackButtonMin(
			trackMinX,
			start.y + Ref(5.0f));
		const ImVec2 trackButtonSize(
			trackWidth,
			height - Ref(10.0f));

		ImGui::SetCursorScreenPos(trackButtonMin);
		ImGui::InvisibleButton(
			"##track",
			trackButtonSize);

		const bool hovered = ImGui::IsItemHovered();
		const bool active = ImGui::IsItemActive();
		const float hoverT =
			Animate01(
				"##sliderHoverMotion",
				hovered || active,
				24.0f);
		if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
			activeSliderDragFrame = ImGui::GetFrameCount();
		bool changed = false;

		if (active && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
			const float mouseX = ImGui::GetIO().MousePos.x;
			const float t =
				std::clamp(
					(mouseX - trackMinX) / trackWidth,
					0.0f,
					1.0f);
			const float newValue =
				DisplayTToLinear(
					t,
					minValue,
					maxValue,
					logarithmic);

			if (std::abs(newValue - *value) > 0.000001f) {
				*value = newValue;
				changed = true;
			}
		}

		ImDrawList* draw = ImGui::GetWindowDrawList();

		const ImVec2 labelSize = ImGui::CalcTextSize(label);
		draw->AddText(
			ImVec2(
				start.x,
				cy - labelSize.y * 0.5f),
			hovered ? Colors::Text : Colors::TextMuted,
			label);

		const float railY = cy;
		draw->AddLine(
			ImVec2(trackMinX, railY),
			ImVec2(trackMinX + trackWidth, railY),
			MixColor(
				IM_COL32(51, 61, 66, 235),
				IM_COL32(63, 84, 89, 245),
				hoverT),
			Ref(4.0f));

		const float t =
			LinearToDisplayT(
				*value,
				minValue,
				maxValue,
				logarithmic);
		const float markerX =
			trackMinX + trackWidth * t;

		draw->AddLine(
			ImVec2(trackMinX, railY),
			ImVec2(markerX, railY),
			hovered || active ? Colors::Cyan : Colors::CyanSoft,
			Ref(3.0f));

		const float r = Ref(5.0f);
		draw->AddQuadFilled(
			ImVec2(markerX, railY - r),
			ImVec2(markerX + r, railY),
			ImVec2(markerX, railY + r),
			ImVec2(markerX - r, railY),
			active ? Colors::CyanBright : Colors::Text);

		char buffer[64]{};
		std::snprintf(
			buffer,
			sizeof(buffer),
			format,
			*value);

		const ImVec2 valueSize = ImGui::CalcTextSize(buffer);
		draw->AddText(
			ImVec2(
				start.x + width - valueSize.x,
				cy - valueSize.y * 0.5f),
			Colors::Text,
			buffer);

		ImGui::SetCursorScreenPos(start);
		ImGui::Dummy(ImVec2(width, height));

		ImGui::PopID();
		return changed;
	}

	inline bool SliderIntField(
		const char* label,
		int* value,
		int minValue,
		int maxValue,
		const char* const* valueNames = nullptr,
		bool* outHovered = nullptr)
	{
		ImGui::PushID(label);

		*value = std::clamp(*value, minValue, maxValue);

		const ImVec2 start = ImGui::GetCursorScreenPos();
		const float width = std::min(ImGui::GetContentRegionAvail().x, Ref(720.0f));
		const float height = Ref(30.0f);
		const float labelWidth =
			std::clamp(width * 0.34f, Ref(120.0f), Ref(205.0f));
		const float valueWidth = Ref(78.0f);
		const float trackMinX = start.x + labelWidth;
		const float trackMaxX = start.x + width - valueWidth;
		const float trackWidth = std::max(Ref(90.0f), trackMaxX - trackMinX);
		const float cy = start.y + height * 0.5f;
		const int steps = std::max(1, maxValue - minValue);

		ImGui::SetCursorScreenPos(
			ImVec2(
				trackMinX,
				start.y + Ref(5.0f)));
		ImGui::InvisibleButton(
			"##track",
			ImVec2(
				trackWidth,
				height - Ref(10.0f)));

		const bool hovered = ImGui::IsItemHovered();
		const bool active = ImGui::IsItemActive();

		if (outHovered)
			*outHovered = hovered || active;
		if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
			activeSliderDragFrame = ImGui::GetFrameCount();

		bool changed = false;

		if (active &&
			ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
			const float mouseT =
				std::clamp(
					(ImGui::GetIO().MousePos.x - trackMinX) /
						trackWidth,
					0.0f,
					1.0f);

			const int snapped =
				std::clamp(
					minValue +
						static_cast<int>(
							std::lround(
								mouseT *
								static_cast<float>(steps))),
					minValue,
					maxValue);

			if (snapped != *value) {
				*value = snapped;
				changed = true;
			}
		}

		ImDrawList* draw = ImGui::GetWindowDrawList();

		const ImVec2 labelSize =
			ImGui::CalcTextSize(label);
		draw->AddText(
			ImVec2(
				start.x,
				cy - labelSize.y * 0.5f),
			hovered
				? Colors::Text
				: Colors::TextMuted,
			label);

		draw->AddLine(
			ImVec2(trackMinX, cy),
			ImVec2(
				trackMinX + trackWidth,
				cy),
			IM_COL32(51, 61, 66, 235),
			Ref(3.0f));

		for (int i = 0; i <= steps; ++i) {
			const float detentX =
				trackMinX +
				trackWidth *
					(static_cast<float>(i) /
					 static_cast<float>(steps));

			const float d = Ref(2.5f);
			draw->AddQuadFilled(
				ImVec2(detentX, cy - d),
				ImVec2(detentX + d, cy),
				ImVec2(detentX, cy + d),
				ImVec2(detentX - d, cy),
				Colors::BorderBright);
		}

		const float t =
			static_cast<float>(*value - minValue) /
			static_cast<float>(steps);
		const float markerX =
			trackMinX + trackWidth * t;

		draw->AddLine(
			ImVec2(trackMinX, cy),
			ImVec2(markerX, cy),
			hovered || active
				? Colors::Cyan
				: Colors::CyanSoft,
			Ref(3.0f));

		const float r = Ref(5.0f);
		draw->AddQuadFilled(
			ImVec2(markerX, cy - r),
			ImVec2(markerX + r, cy),
			ImVec2(markerX, cy + r),
			ImVec2(markerX - r, cy),
			active
				? Colors::CyanBright
				: Colors::Text);

		char numeric[16]{};
		const char* display = nullptr;
		if (valueNames) {
			display =
				valueNames[
					std::clamp(
						*value - minValue,
						0,
						steps)];
		} else {
			std::snprintf(
				numeric,
				sizeof(numeric),
				"%d",
				*value);
			display = numeric;
		}

		const ImVec2 valueSize =
			ImGui::CalcTextSize(display);
		draw->AddText(
			ImVec2(
				start.x + width - valueSize.x,
				cy - valueSize.y * 0.5f),
			Colors::Text,
			display);

		ImGui::SetCursorScreenPos(start);
		ImGui::Dummy(
			ImVec2(
				width,
				height));

		ImGui::PopID();
		return changed;
	}

	inline bool CycleSelector(
		const char* label,
		int* value,
		const char* const* labels,
		int count)
	{
		if (!labels || count <= 0)
			return false;

		*value =
			std::clamp(
				*value,
				0,
				count - 1);

		ImGui::PushID(label);

		const ImVec2 start = ImGui::GetCursorScreenPos();
		const float width = std::min(ImGui::GetContentRegionAvail().x, Ref(720.0f));
		const float height = Ref(30.0f);
		const float labelWidth =
			std::clamp(width * 0.34f, Ref(120.0f), Ref(205.0f));
		const float selectorX = start.x + labelWidth;
		const float selectorWidth =
			std::max(Ref(140.0f), width - labelWidth);
		const float arrowWidth = Ref(34.0f);

		const ImVec2 labelSize = ImGui::CalcTextSize(label);
		ImGui::GetWindowDrawList()->AddText(
			ImVec2(
				start.x,
				start.y + (height - labelSize.y) * 0.5f),
			Colors::TextMuted,
			label);

		bool changed = false;

		ImGui::SetCursorScreenPos(
			ImVec2(selectorX, start.y + Ref(3.0f)));
		if (ImGui::InvisibleButton(
				"##previous",
				ImVec2(
					arrowWidth,
					height - Ref(6.0f)))) {
			*value =
				(*value + count - 1) % count;
			changed = true;
		}

		ImGui::SetCursorScreenPos(
			ImVec2(
				selectorX + selectorWidth - arrowWidth,
				start.y + Ref(3.0f)));
		if (ImGui::InvisibleButton(
				"##next",
				ImVec2(
					arrowWidth,
					height - Ref(6.0f)))) {
			*value =
				(*value + 1) % count;
			changed = true;
		}

		ImDrawList* draw = ImGui::GetWindowDrawList();

		FillChamfered(
			draw,
			ImVec2(selectorX, start.y + Ref(3.0f)),
			ImVec2(
				selectorX + selectorWidth,
				start.y + height - Ref(3.0f)),
			Ref(4.0f),
			IM_COL32(14, 18, 22, 245));

		StrokeChamfered(
			draw,
			ImVec2(selectorX, start.y + Ref(3.0f)),
			ImVec2(
				selectorX + selectorWidth,
				start.y + height - Ref(3.0f)),
			Ref(4.0f),
			Colors::BorderSoft,
			Ref(1.0f));

		const float cy =
			start.y + height * 0.5f;
		const ImU32 arrowColor =
			Colors::TextMuted;

		draw->AddLine(
			ImVec2(selectorX + Ref(20.0f), cy - Ref(4.0f)),
			ImVec2(selectorX + Ref(15.0f), cy),
			arrowColor,
			Ref(1.5f));
		draw->AddLine(
			ImVec2(selectorX + Ref(15.0f), cy),
			ImVec2(selectorX + Ref(20.0f), cy + Ref(4.0f)),
			arrowColor,
			Ref(1.5f));

		const float rx =
			selectorX + selectorWidth - Ref(18.0f);
		draw->AddLine(
			ImVec2(rx - Ref(3.0f), cy - Ref(4.0f)),
			ImVec2(rx + Ref(2.0f), cy),
			arrowColor,
			Ref(1.5f));
		draw->AddLine(
			ImVec2(rx + Ref(2.0f), cy),
			ImVec2(rx - Ref(3.0f), cy + Ref(4.0f)),
			arrowColor,
			Ref(1.5f));

		const ImVec2 currentSize =
			ImGui::CalcTextSize(labels[*value]);
		draw->AddText(
			ImVec2(
				selectorX +
					(selectorWidth - currentSize.x) * 0.5f,
				cy - currentSize.y * 0.5f),
			Colors::Text,
			labels[*value]);

		ImGui::SetCursorScreenPos(start);
		ImGui::Dummy(ImVec2(width, height));

		ImGui::PopID();
		return changed;
	}

	inline bool LabeledToggle(
		const char* label,
		bool* value)
	{
		ImGui::PushID(label);

		const ImVec2 start = ImGui::GetCursorScreenPos();
		const float width = ImGui::GetContentRegionAvail().x;
		const float height = Ref(28.0f);
		const float latchWidth = Ref(60.0f);
		const float rowGap = Ref(8.0f);

		// Make the complete label side of the row interactive.  This gives every
		// normal module toggle a comfortable mouse/controller target while keeping
		// a distinct non-overlapping latch ID on the right.
		ImGui::SetCursorScreenPos(start);
		const bool labelPressed =
			ImGui::InvisibleButton(
				"##labelHit",
				ImVec2(
					std::max(1.0f, width - latchWidth - rowGap),
					height));
		if (labelPressed)
			*value = !*value;

		const ImVec2 labelSize =
			ImGui::CalcTextSize(label);
		ImGui::GetWindowDrawList()->AddText(
			ImVec2(
				start.x,
				start.y + (height - labelSize.y) * 0.5f),
			Colors::TextMuted,
			label);

		ImGui::SetCursorScreenPos(
			ImVec2(
				start.x + width - latchWidth,
				start.y + Ref(1.0f)));

		const bool latchChanged =
			Toggle("##value", value);

		ImGui::SetCursorScreenPos(start);
		ImGui::Dummy(ImVec2(width, height));

		ImGui::PopID();
		return labelPressed || latchChanged;
	}


	inline void StatusPill(const char* text, ImU32 color = Colors::Cyan)
	{
		const ImVec2 textSize = ImGui::CalcTextSize(text);
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const ImVec2 size(textSize.x + Scale(14.0f), textSize.y + Scale(7.0f));
		ImGui::Dummy(size);
		ImDrawList* draw = ImGui::GetWindowDrawList();
		draw->AddRectFilled(
			p,
			ImVec2(p.x + size.x, p.y + size.y),
			IM_COL32(10, 23, 27, 230),
			size.y * 0.5f);
		draw->AddRect(
			p,
			ImVec2(p.x + size.x, p.y + size.y),
			color,
			size.y * 0.5f,
			0,
			Scale(1.0f));
		draw->AddText(
			ImVec2(p.x + Scale(7.0f), p.y + Scale(3.0f)),
			color,
			text);
	}
}
