#include "PCH.h"

#include "IconLoader.h"

#include "Globals.h"
#include "Menu.h"
#include "Utils/D3D.h"
#include "Utils/FileSystem.h"

#include <chrono>
#include <filesystem>
#include <stb_image.h>
#include <thread>

namespace Util
{
	bool LoadTextureFromFile(ID3D11Device* device, const char* filename, ID3D11ShaderResourceView** out_srv, ImVec2& out_size, bool loadAsTintableMask)
	{
		if (out_srv)
			*out_srv = nullptr;
		out_size = {};
		if (!device || !filename || filename[0] == '\0' || !out_srv) {
			return false;
		}

		int image_width = 0;
		int image_height = 0;
		int image_components = 0;
		constexpr int kMaxInterfaceTextureDimension = 4096;
		if (!stbi_info(filename, &image_width, &image_height, &image_components) ||
			image_width <= 0 || image_height <= 0 ||
			image_width > kMaxInterfaceTextureDimension || image_height > kMaxInterfaceTextureDimension) {
			logger::warn("Rejected invalid or oversized interface texture: {}", filename);
			return false;
		}

		unsigned char* image_data = stbi_load(filename, &image_width, &image_height, nullptr, 4);
		if (image_data == nullptr) {
			return false;
		}
		// The file may have changed between the metadata probe and decode.
		if (image_width <= 0 || image_height <= 0 ||
			image_width > kMaxInterfaceTextureDimension || image_height > kMaxInterfaceTextureDimension) {
			stbi_image_free(image_data);
			return false;
		}

		// The supplied PIXL mark is an opaque black shape with authored alpha.
		// Converting RGB to white at upload time preserves that exact silhouette
		// while allowing ImGui to tint it for dark, light and pulsing compiler use.
		if (loadAsTintableMask) {
			const size_t pixelCount = static_cast<size_t>(image_width) * static_cast<size_t>(image_height);
			for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
				auto* rgba = image_data + pixel * 4;
				rgba[0] = rgba[1] = rgba[2] = 255;
			}
		}

		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = image_width;
		desc.Height = image_height;
		desc.MipLevels = 0;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

		ID3D11Texture2D* pTexture = nullptr;
		HRESULT hr = device->CreateTexture2D(&desc, nullptr, &pTexture);
		if (FAILED(hr) || !pTexture) {
			stbi_image_free(image_data);
			return false;
		}
		Util::SetResourceName(pTexture, "IconLoader::%s", filename);

		ID3D11DeviceContext* context = nullptr;
		device->GetImmediateContext(&context);
		if (!context) {
			pTexture->Release();
			stbi_image_free(image_data);
			return false;
		}
		context->UpdateSubresource(pTexture, 0, nullptr, image_data, desc.Width * 4, 0);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = static_cast<UINT>(-1);
		srvDesc.Texture2D.MostDetailedMip = 0;

		hr = device->CreateShaderResourceView(pTexture, &srvDesc, out_srv);
		if (FAILED(hr)) {
			pTexture->Release();
			stbi_image_free(image_data);
			if (context)
				context->Release();
			return false;
		}
		Util::SetResourceName(*out_srv, "IconLoader::%s SRV", filename);

		context->GenerateMips(*out_srv);
		context->Release();

		pTexture->Release();
		stbi_image_free(image_data);

		out_size = ImVec2(static_cast<float>(image_width), static_cast<float>(image_height));
		return true;
	}
}

namespace Util::IconLoader
{
	struct IconDefinition
	{
		std::string filename;
		ID3D11ShaderResourceView** texture;
		ImVec2* size;
		bool tintableMask = false;
	};

	std::vector<IconDefinition> GetIconDefinitions(Menu* menu)
	{
		return {
			{ "Brand\\PIXL-Mark.png", &menu->uiIcons.logo.texture, &menu->uiIcons.logo.size, true }
		};
	}

	void LoadThemeSpecificIcons(Menu* menu, ID3D11Device* device, const std::vector<IconDefinition>& iconDefs)
	{
		const auto& selectedTheme = menu->GetSettings().SelectedThemePreset;
		if (selectedTheme.empty()) {
			return;
		}

		const auto safeTheme = Util::FileHelpers::SanitizeFileName(selectedTheme);
		if (safeTheme.empty()) {
			logger::warn("LoadThemeSpecificIcons: Ignoring invalid theme name");
			return;
		}
		std::filesystem::path themeIconsPath = Util::PathHelpers::GetThemesPath() / safeTheme;
		if (!std::filesystem::exists(themeIconsPath) || !std::filesystem::is_directory(themeIconsPath)) {
			logger::debug("LoadThemeSpecificIcons: Theme folder does not exist: {}", themeIconsPath.string());
			return;
		}

		logger::info("LoadThemeSpecificIcons: Checking for custom icons in theme '{}' at path: {}", selectedTheme, themeIconsPath.string());

		ID3D11DeviceContext* context = globals::d3d::context;
		if (context)
			context->Flush();

		int iconsOverridden = 0;

		for (const auto& iconDef : iconDefs) {
			std::filesystem::path iconPath = themeIconsPath / std::filesystem::path(iconDef.filename).filename();

			logger::trace("LoadThemeSpecificIcons: Checking for icon: {}", iconPath.string());

			if (std::filesystem::exists(iconPath)) {
				winrt::com_ptr<ID3D11ShaderResourceView> replacement;
				ImVec2 replacementSize{};
				if (Util::LoadTextureFromFile(device, iconPath.string().c_str(), replacement.put(), replacementSize, iconDef.tintableMask)) {
					// A broken optional override must not discard the working base icon.
					if (*iconDef.texture)
						(*iconDef.texture)->Release();
					*iconDef.texture = replacement.detach();
					*iconDef.size = replacementSize;
					logger::debug("LoadThemeSpecificIcons: Loaded custom icon: {}", iconPath.filename().string());
					iconsOverridden++;
				}
			}
		}

		if (iconsOverridden > 0) {
			logger::info("LoadThemeSpecificIcons: Loaded {} custom icon(s) from theme '{}'", iconsOverridden, selectedTheme);
		}
	}

	bool InitializeMenuIcons(Menu* menu)
	{
		if (!menu) {
			logger::warn("InitializeMenuIcons: Menu pointer is null");
			return false;
		}

		ID3D11Device* device = globals::d3d::device;
		ID3D11DeviceContext* context = globals::d3d::context;
		if (!device || !context) {
			logger::warn("InitializeMenuIcons: D3D device or context is null");
			return false;
		}

		// Flush and wait for GPU idle before releasing textures
		context->Flush();
		winrt::com_ptr<ID3D11Query> eventQuery;
		D3D11_QUERY_DESC queryDesc = { D3D11_QUERY_EVENT, 0 };
		if (SUCCEEDED(device->CreateQuery(&queryDesc, eventQuery.put()))) {
			context->End(eventQuery.get());
			BOOL queryData = FALSE;
			for (int i = 0; i < 1000 && context->GetData(eventQuery.get(), &queryData, sizeof(BOOL), 0) != S_OK; i++) {
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		}

		std::string basePath = Util::PathHelpers::GetIconsPath().string() + "\\";
		logger::info("InitializeMenuIcons: Loading icons from base path: {}", basePath);

		auto iconDefs = GetIconDefinitions(menu);

		// Release all existing textures using the same definitions list (avoids stale hardcoded list)
		for (const auto& iconDef : iconDefs) {
			if (*iconDef.texture) {
				(*iconDef.texture)->Release();
				*iconDef.texture = nullptr;
			}
		}
		// Also release search icon (not in iconDefs)
		if (menu->uiIcons.search.texture) {
			menu->uiIcons.search.texture->Release();
			menu->uiIcons.search.texture = nullptr;
		}

		bool anyIconLoaded = false;
		int iconsLoaded = 0;

		for (const auto& iconDef : iconDefs) {
			std::string fullPath = basePath + iconDef.filename;
			if (Util::LoadTextureFromFile(device, fullPath.c_str(), iconDef.texture, *iconDef.size, iconDef.tintableMask)) {
				iconsLoaded++;
				anyIconLoaded = true;
			} else {
				// If monochrome icon failed to load, try fallback to colored version
				if (fullPath.find("Monochrome") != std::string::npos) {
					std::string fallbackPath = fullPath;
					size_t pos = fallbackPath.find("\\Monochrome");
					if (pos != std::string::npos) {
						fallbackPath.erase(pos, 11);  // Remove "\Monochrome"
					}
					if (Util::LoadTextureFromFile(device, fallbackPath.c_str(), iconDef.texture, *iconDef.size, iconDef.tintableMask)) {
						iconsLoaded++;
						anyIconLoaded = true;
					} else {
						logger::warn("InitializeMenuIcons: Failed to load icon from: {} (and fallback: {})", fullPath, fallbackPath);
					}
				} else {
					logger::warn("InitializeMenuIcons: Failed to load icon from: {}", fullPath);
				}
			}
		}

		logger::info("InitializeMenuIcons: Loaded {}/{} icons successfully", iconsLoaded, iconDefs.size());

		LoadThemeSpecificIcons(menu, device, iconDefs);

		// A valid theme override can supply the icon when the base asset is absent.
		return anyIconLoaded || std::any_of(iconDefs.begin(), iconDefs.end(), [](const auto& icon) { return *icon.texture != nullptr; });
	}
}
