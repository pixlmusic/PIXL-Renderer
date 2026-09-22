// Headless tests use the shipping widgets and registry, not copies of them.
#include "Menu/Controls.h"
#include "Menu/ExtensionCallback.h"
#include <cassert>
#include <iostream>
#include <stdexcept>

namespace PIXLUI
{
	float testScale = 1.0f;
	float Ref(float value) { return value * testScale; }
}

int main()
{
	using namespace PIXLUI::Extensions;
	Registry registry;
	assert(registry.Register({}) == 0);
	assert(registry.Register({ "invalid/id", "Invalid", {}, 0, [] {} }) == 0);
	const auto a = registry.Register({ "one", "First", "Tools", 5, [] {} });
	const auto b = registry.Register({ "two", "Second", "Tools", 1, [] {} });
	assert(a && b && a != b);
	assert(registry.Register({ "one", "Duplicate", {}, 0, [] {} }) == 0);
	auto snapshot = registry.Snapshot();
	assert(snapshot == registry.Snapshot());  // No per-frame vector allocation.
	assert(snapshot->front()->handle == b);
	assert(registry.Unregister(b));
	assert(!snapshot->front()->registered && snapshot->size() == 2);
	assert(!registry.Unregister(b));
	assert(registry.Snapshot()->size() == 1);
	const auto replacement = registry.Register({ "two", "Replacement", {}, 0, [] {} });
	assert(replacement != b);
	registry.Shutdown();
	assert(registry.Snapshot()->empty());
	assert(!snapshot->back()->registered);
	assert(registry.Register({ "late", "Shutdown", {}, 0, [] {} }) == 0);

	ImGui::CreateContext();
	auto& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.DisplaySize = ImVec2(3840, 2160);
	io.DeltaTime = 1.0f / 60.0f;
	io.Fonts->AddFontDefault();
	unsigned char* pixels = nullptr;
	int textureWidth = 0, textureHeight = 0;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &textureWidth, &textureHeight);
	const char* choices[] = { "Low", "Medium", "High", "100% quality" };
	int frames = 0;
	for (float scale : { 1.0f, 1.3333f, 2.0f }) {
		PIXLUI::testScale = scale;
		for (float width : { 180.0f, 260.0f, 390.0f, 560.0f, 720.0f }) {
			ImGui::NewFrame();
			ImGui::SetNextWindowPos(ImVec2(30, 30));
			ImGui::SetNextWindowSize(ImVec2(width * scale, 900 * scale));
			ImGui::Begin("Test", nullptr, ImGuiWindowFlags_NoSavedSettings);
			ImGui::SetWindowFontScale(scale);
			const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
			float value = 0.5f;
			int tier = 3;
			int choice = 1;
			const float rowStart = ImGui::GetCursorScreenPos().y;
			const float estimatedHeight = PIXLUI::ControlRow::Height("Exposure compensation for bright skies", ImGui::GetContentRegionAvail().x);
			ImGui::BeginDisabled();
			assert(!PIXLUI::SliderFloatField("Exposure compensation for bright skies", &value, 0, 1));
			if (ImGui::GetCursorScreenPos().y - rowStart > estimatedHeight + 1.0f)
				std::cerr << "Height scale=" << scale << " width=" << width << " estimated=" << estimatedHeight << " actual=" << ImGui::GetCursorScreenPos().y - rowStart << '\n';
			assert(ImGui::GetCursorScreenPos().y - rowStart <= estimatedHeight + 1.0f);
			if (ImGui::GetItemRectMax().x > right + 1.0f)
				std::cerr << "Overflow scale=" << scale << " width=" << width << " right=" << right << " actual=" << ImGui::GetItemRectMax().x << '\n';
			assert(ImGui::GetItemRectMax().x <= right + 1.0f);
			assert(!PIXLUI::SliderIntField("Quality", &tier, 0, 3, choices));
			assert(ImGui::GetItemRectMax().x <= right + 1.0f);
			assert(!PIXLUI::CycleSelector("Colour preset", &choice, choices, 4));
			assert(ImGui::GetItemRectMax().x <= right + 1.0f);
			ImGui::EndDisabled();
			assert(value == 0.5f && tier == 3 && choice == 1);
			assert(ImGui::GetCurrentContext()->ErrorCountCurrentFrame == 0);
			ImGui::End();
			ImGui::Render();
			++frames;
		}
	}

	ImGui::NewFrame();
	ImGui::Begin("Isolation");
	ImGui::BeginChild("Host");
	const auto style = ImGui::GetStyle();
	const auto* window = ImGui::GetCurrentWindow();
	Entry extension{ 1, { "broken", "Broken", {}, 0, [] {} } };
	assert(!InvokeIsolated(extension, [] {
		ImGui::BeginChild("Unbalanced");
		ImGui::PushID("Forgotten");
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0, 1, 1));
		ImGui::BeginDisabled();
		throw std::runtime_error("Provider failure");
	}));
	assert(extension.faulted);
	assert(window == ImGui::GetCurrentWindow());
	assert(ImGui::GetStyle().Alpha == style.Alpha);
	assert(ImGui::GetStyle().Colors[ImGuiCol_Text].y == style.Colors[ImGuiCol_Text].y);
	assert(!InvokeIsolated(extension, [] { assert(false); }));
	Entry working{ 2, { "working", "Working", {}, 0, [] {} } };
	assert(InvokeIsolated(working, [] { ImGui::TextUnformatted("Renderer still usable"); }));
	ImGui::EndChild();
	ImGui::End();
	ImGui::Render();
	ImGui::DestroyContext();
	std::cout << "PASS: registry/lifetime, " << frames << " scaled layouts, disabled controls, callback exception/stack isolation\n";
}
