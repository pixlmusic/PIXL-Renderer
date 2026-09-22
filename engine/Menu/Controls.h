#pragma once

#include <algorithm>
#include <cstddef>
#include <imgui.h>

namespace PIXLUI
{
	float Ref(float value);

	// Publish only this frame's drag activity for the tuner's scene preview.
	inline int activeSliderDragFrame = -1;

	// Native slider behaviour supplies keyboard navigation and Ctrl+click exact
	// entry. The shared row reflows below its label instead of drawing through it
	// when a public-page column is too narrow.
	class ControlRow
	{
	public:
		static bool IsStacked(const char* label, float width)
		{
			const float labelWidth = std::min(Ref(205.0f), width * 0.42f);
			return width < Ref(390.0f) ||
			       ImGui::CalcTextSize(label).x > labelWidth - ImGui::GetStyle().ItemSpacing.x;
		}
		static float Height(const char* label, float width)
		{
			const auto& style = ImGui::GetStyle();
			return ImGui::GetFrameHeightWithSpacing() + (IsStacked(label, width) ?
																ImGui::CalcTextSize(label, nullptr, false, std::max(1.0f, width)).y + 2.0f * style.FramePadding.y + style.ItemSpacing.y :
																0.0f);
		}
		explicit ControlRow(const char* label)
		{
			ImGui::PushID(label);
			ImGui::BeginGroup();
			const ImVec2 start = ImGui::GetCursorScreenPos();
			const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
			const float labelWidth = std::min(Ref(205.0f), width * 0.42f);
			const bool stacked = IsStacked(label, width);
			ImGui::AlignTextToFramePadding();
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + (stacked ? width : labelWidth));
			ImGui::TextUnformatted(label);
			ImGui::PopTextWrapPos();
			if (!stacked)
				ImGui::SetCursorScreenPos(ImVec2(start.x + labelWidth, start.y));
			ImGui::SetNextItemWidth(stacked ? width : std::max(1.0f, width - labelWidth));
		}
		~ControlRow()
		{
			ImGui::EndGroup();
			ImGui::PopID();
		}
		ControlRow(const ControlRow&) = delete;
		ControlRow& operator=(const ControlRow&) = delete;
	};

	inline bool SliderFloatField(
		const char* label, float* value, float minValue, float maxValue,
		const char* format = "%.2f", bool logarithmic = false)
	{
		ControlRow row(label);
		const auto flags = ImGuiSliderFlags_AlwaysClamp |
		                   (logarithmic ? ImGuiSliderFlags_Logarithmic : ImGuiSliderFlags_None);
		const bool changed = ImGui::SliderFloat("##value", value, minValue, maxValue, format, flags);
		if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
			activeSliderDragFrame = ImGui::GetFrameCount();
		return changed;
	}

	inline bool SliderIntField(
		const char* label, int* value, int minValue, int maxValue,
		const char* const* valueNames = nullptr, bool* outHovered = nullptr)
	{
		ControlRow row(label);
		// Names are display strings, not printf formats.
		char display[128]{};
		if (valueNames) {
			const char* name = valueNames[std::clamp(*value, minValue, maxValue) - minValue];
			std::size_t n = 0;
			for (; *name && n + 2 < sizeof(display); ++name) {
				if (*name == '%')
					display[n++] = '%';
				display[n++] = *name;
			}
		}
		const bool changed = ImGui::SliderInt("##value", value, minValue, maxValue,
			valueNames ? display : "%d", ImGuiSliderFlags_AlwaysClamp);
		if (outHovered)
			*outHovered = ImGui::IsItemHovered() || ImGui::IsItemActive();
		if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
			activeSliderDragFrame = ImGui::GetFrameCount();
		return changed;
	}

	inline bool CycleSelector(
		const char* label, int* value, const char* const* labels, int count)
	{
		if (!labels || count <= 0)
			return false;
		ControlRow row(label);
		// Direct selection makes large preset lists discoverable and avoids
		// narrow arrow hit targets. Merely drawing never rewrites the setting.
		return ImGui::Combo("##value", value, labels, count);
	}
}
