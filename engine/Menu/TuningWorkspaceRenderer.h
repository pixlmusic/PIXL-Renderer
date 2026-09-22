#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <variant>
#include <vector>

struct RenderModule;

/**
 * @brief Renders PIXL's centered system browser and selected settings canvas.
 */
class TuningWorkspaceRenderer
{
public:
	enum class TunerInteractionMode
	{
		Closed,
		LiveUI,
		InspectLocked,
		InspectMoving
	};
	/** @brief Describes a built-in (non-feature) menu page with a name and draw callback. */
	struct BuiltInMenu
	{
		std::string name;
		std::function<void()> func;
	};

	/** @brief Represents a collapsible category header in the feature list. */
	struct CategoryHeader
	{
		std::string name;
	};

	/** @brief Non-selectable shader-area label nested under PIXL Renderer. */
	struct SubcategoryHeader
	{
		std::string name;
	};

	/** @brief One of PIXL's four user-facing renderer areas. */
	struct CategoryPage
	{
		std::string name;
		std::vector<RenderModule*> features;
	};

	/** @brief Variant type representing any entry in the menu list. */
	using MenuFuncInfo = std::variant<BuiltInMenu, std::string, CategoryHeader, SubcategoryHeader, CategoryPage, RenderModule*>;

	/**
	 * @brief Renders the full single-canvas system browser and settings panel.
	 *
	 * Builds the menu list from built-in pages and loaded features, handles
	 * pending feature selection requests, then draws the left-column navigation
	 * and right-column settings content.
	 *
	 * @param footerHeight Height reserved for the footer area below the list.
	 * @param selectedMenu Index of the currently selected menu item (updated on selection change).
	 * @param featureSearch Current search filter string (updated by the search input).
	 * @param pendingFeatureSelection Name of a feature to auto-select (cleared after processing).
	 * @param categoryExpansionStates Map of category name to expanded/collapsed state.
	 * @param drawGeneralSettings Callback that renders the General settings page content.
	 * @param drawAdvancedSettings Callback that renders the Advanced settings page content.
	 */
	static void RenderFeatureList(
		float footerHeight,
		size_t& selectedMenu,
		std::string& featureSearch,
		std::string& pendingFeatureSelection,
		std::map<std::string, bool>& categoryExpansionStates,
		const std::function<void()>& drawGeneralSettings,
		const std::function<void()>& drawAdvancedSettings);

	// PIXL Director live photo-mode controls / HUD.
	[[nodiscard]] static bool IsDirectorPhotoModeActive();
	[[nodiscard]] static bool IsDirectorCameraTransitionPending();
	/** Current PIXL tuner ownership state. The tuner remains open while inspecting. */
	[[nodiscard]] static TunerInteractionMode GetTunerInteractionMode();
	/** True only while native free-camera navigation is actively being driven. */
	[[nodiscard]] static bool IsDirectorInspectionMoving();
	/** True while a Photo Finish transaction owns and freezes the Director camera. */
	[[nodiscard]] static bool IsDirectorPhotoCaptureLocked();
	/**
	 * @brief Returns whether Director can safely take ownership of gameplay now.
	 *
	 * This is the single eligibility gate used by both the UI and the runtime
	 * hotkey entry point.  When supplied, @p reason receives a short user-facing
	 * explanation for a rejected activation.
	 */
	[[nodiscard]] static bool IsDirectorPhotoModeAvailable(std::string* reason = nullptr);
	/** Enters Director through its authoritative eligibility gate, or returns to its live HUD when already active. */
	static bool OpenDirectorPhotoMode();
	/** Requests safe teardown when the tuner closes during inspection. */
	static void CloseTunerInspection();
	/** Advances camera teardown even when the tuner and Director HUD are hidden. */
	static void UpdateTunerInspection();
	static void InitializeCameraCompatibility(bool requestInterface);
	static bool HandleDirectorKeyboardInput(std::uint32_t virtualKey);
	/** Routes key transitions used by the tuner ownership state machine. */
	static bool HandleTunerKeyboardInput(std::uint32_t virtualKey, bool pressed);
	static bool HandleDirectorGamepadInput(std::uint32_t gamepadKeyCode);
	static void RenderDirectorPhotoModeOverlay();
	/** Draws the user-facing hotkey module under the PIXL Renderer group. */
	static void DrawHotkeysSettings();

private:
	struct ListMenuVisitor
	{
		size_t listId;
		size_t& selectedMenuRef;
		std::map<std::string, bool>& categoryExpansionStates;

		void operator()(const BuiltInMenu& menu);
		void operator()(const std::string& label);
		void operator()(const CategoryHeader& header);
		void operator()(const SubcategoryHeader& header);
		void operator()(const CategoryPage& page);
		void operator()(RenderModule* feat);
	};

	struct DrawMenuVisitor
	{
		explicit DrawMenuVisitor(std::string& pendingFeatureSelectionRef) :
			pendingFeatureSelection(pendingFeatureSelectionRef) {}

		void operator()(const BuiltInMenu& menu);
		void operator()(const std::string&);
		void operator()(const CategoryHeader&);
		void operator()(const SubcategoryHeader&);
		void operator()(const CategoryPage& page);
		void operator()(RenderModule* feat);

	private:
		std::string& pendingFeatureSelection;

		// Helper methods for RenderModule rendering
		void RenderFeatureHeader(RenderModule* feat, bool isDisabled, bool isLoaded, bool sceneControlled);
		void RenderFeatureSettings(RenderModule* feat, bool isDisabled, bool isLoaded, bool hasFailedMessage, bool sceneControlled);
		void RenderSystemModal(RenderModule* feat, bool& open, ImVec2 rowAnchor);
		void RenderCategoryFeature(RenderModule* feat, bool forceOpen);
		void RenderCompactFeature(RenderModule* feat);
		static void RenderRestoreDefaultsButton(RenderModule* feat, bool isDisabled, bool isLoaded);
		void RenderReactiveConstraintWarningDialog();
	};

	static std::vector<MenuFuncInfo> BuildMenuList(
		const std::string& featureSearch,
		std::map<std::string, bool>& categoryExpansionStates,
		const std::function<void()>& drawGeneralSettings,
		const std::function<void()>& drawAdvancedSettings);

	static void HandlePendingFeatureSelection(
		std::string& pendingFeatureSelection,
		const std::vector<MenuFuncInfo>& menuList,
		size_t& selectedMenu);

	static void RenderLeftColumn(
		const std::vector<MenuFuncInfo>& menuList,
		size_t& selectedMenu,
		std::string& featureSearch,
		std::map<std::string, bool>& categoryExpansionStates,
		std::string& selectedFeatureName);

	static void RenderRightColumn(
		const std::vector<MenuFuncInfo>& menuList,
		size_t selectedMenu,
		std::string& pendingFeatureSelection,
		const std::string& selectedFeatureName);
};
