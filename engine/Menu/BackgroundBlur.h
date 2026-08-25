#pragma once

#include <d3d11.h>
#include <mutex>
#include <winrt/base.h>

struct ImVec2;

namespace BackgroundBlur
{
	/**
	 * @brief Initializes blur shaders and GPU resources
	 * @return True if initialization succeeded
	 */
	bool Initialize();

	/**
	 * @brief Renders background blur behind all visible ImGui windows
	 * This is the main entry point - call after ImGui::Render() but before ImGui_ImplDX11_RenderDrawData()
	 */
	void RenderBackgroundBlur();

	/**
	 * @brief Cleans up all blur resources
	 */
	void Cleanup();

	void SetEnabled(bool enable);

	struct LivePreviewFrame
	{
		ID3D11ShaderResourceView* srv = nullptr;
		UINT width = 0;
		UINT height = 0;

		explicit operator bool() const
		{
			return
				srv != nullptr &&
				width > 0 &&
				height > 0;
		}
	};

	/**
	 * @brief Captures the current game presentation target before ImGui is
	 * rendered into it and returns a stable SRV for the Camera + Post FX page.
	 *
	 * The copy is throttled to 30 Hz and only occurs while the caller asks for
	 * the preview, so there is no gameplay cost while the page is closed.
	 */
	LivePreviewFrame CaptureLivePreview(bool effectsEnabled = true);

}  // namespace BackgroundBlur
