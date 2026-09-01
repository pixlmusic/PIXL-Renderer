// PIXL Pixel Capture
// Non-blocking screenshot tool. GPU copy runs on the
// render thread; encoding and disk I/O run on a dedicated worker thread so
// capture does not stall the frame.

#include "Modules/PixelCapture.h"

#include <PCH.h>

#include "Modules/CameraSuite.h"
#include "Modules/ImageReconstruction.h"
#include "Modules/HybridGI.h"
#include "Globals.h"
#include "I18n/I18n.h"
#include "Menu.h"
#include "Utils/FileSystem.h"

#define I18N_KEY_PREFIX "feature.pixel_capture."

#include <DirectXTex.h>
#pragma warning(push)
#pragma warning(disable: 4244)  // double->float conversion in third-party header
#include <sk_hdr_png.hpp>
#pragma warning(pop)

#include <cmath>
#include <format>
#include <functional>
#include <malloc.h>

namespace
{
	// Capture source for the current runtime. SRV is non-owning - the texture's
	// lifetime is owned by the slot or a caller-held com_ptr.
	struct CaptureSource
	{
		ID3D11Texture2D* texture = nullptr;
		ID3D11ShaderResourceView* srv = nullptr;
		// kFRAMEBUFFER's SRV aliases the swap-chain backbuffer, which ImGui's DX11
		// backend can't sample directly. When true, the preview path copies through
		// the SRV-readable cache instead.
		bool needsPreviewCache = false;
		const char* description = "(none)";
	};

	struct D3D11MultithreadGuard
	{
		winrt::com_ptr<REX::W32::ID3D11Multithread> multithread;

		explicit D3D11MultithreadGuard(ID3D11DeviceContext* context)
		{
			if (context && SUCCEEDED(context->QueryInterface(multithread.put()))) {
				multithread->SetMultithreadProtected(TRUE);
				multithread->Enter();
			}
		}

		~D3D11MultithreadGuard()
		{
			if (multithread) {
				multithread->Leave();
				multithread->SetMultithreadProtected(FALSE);
			}
		}
	};

	bool PopulateScratchImageFromStagingTexture(
		ID3D11DeviceContext* context,
		ID3D11Texture2D* stagingTexture,
		DXGI_FORMAT format,
		uint32_t width,
		uint32_t height,
		DirectX::ScratchImage& image)
	{
		D3D11MultithreadGuard guard(context);

		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (FAILED(context->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mapped))) {
			return false;
		}
		if (!mapped.pData || mapped.RowPitch == 0) {
			context->Unmap(stagingTexture, 0);
			return false;
		}

		const HRESULT initHr = image.Initialize2D(format, width, height, 1, 1);
		if (FAILED(initHr)) {
			context->Unmap(stagingTexture, 0);
			return false;
		}

		const auto* destImage = image.GetImage(0, 0, 0);
		if (!destImage) {
			context->Unmap(stagingTexture, 0);
			return false;
		}

		// Driver-mapped region can be smaller than height * mapped.RowPitch
		// (alignment quirks, partial mappings). Cap by mapped.DepthPitch and
		// clamp each row's copy to whichever of source/dest pitches is smaller -
		// stepping past either side hits unmapped memory and the worker crashes
		// inside rep movsb (see crash 2026-05-19).
		const size_t bytesPerRow = std::min<size_t>(destImage->rowPitch, mapped.RowPitch);
		const size_t mappedDepth = mapped.DepthPitch != 0 ? mapped.DepthPitch :
		                                                    mapped.RowPitch * destImage->height;
		const size_t maxRowsBySize = mapped.RowPitch > 0 ? (mappedDepth / mapped.RowPitch) : 0;
		const size_t rowsToCopy = std::min<size_t>(destImage->height, maxRowsBySize);

		auto* destPixels = image.GetPixels();
		const auto* srcPixels = static_cast<const uint8_t*>(mapped.pData);

		// Initialize2D leaves the pixel buffer uninitialized. If the mapped
		// region is short (rowsToCopy < height) or narrow (bytesPerRow <
		// destImage->rowPitch), the gaps would otherwise read back as
		// undefined memory and SaveToWICFile would encode garbage. Zero-fill
		// up front so any uncopied bytes encode as deterministic black.
		std::memset(destPixels, 0, image.GetPixelsSize());

		for (size_t row = 0; row < rowsToCopy; ++row) {
			memcpy(
				destPixels + row * destImage->rowPitch,
				srcPixels + row * mapped.RowPitch,
				bytesPerRow);
		}

		context->Unmap(stagingTexture, 0);
		return true;
	}

	void StripAlphaForBmp(DirectX::ScratchImage& image)
	{
		const DirectX::Image* firstImage = image.GetImage(0, 0, 0);
		if (!firstImage || firstImage->pixels == nullptr) {
			return;
		}

		const DXGI_FORMAT format = firstImage->format;
		if (format != DXGI_FORMAT_R8G8B8A8_UNORM &&
			format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB &&
			format != DXGI_FORMAT_B8G8R8A8_UNORM &&
			format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) {
			return;
		}

		auto* pixels = image.GetPixels();
		const size_t rowPitch = firstImage->rowPitch;
		for (size_t y = 0; y < firstImage->height; ++y) {
			uint8_t* row = pixels + y * rowPitch;
			for (size_t x = 0; x < firstImage->width; ++x) {
				row[x * 4 + 3] = 0xFF;
			}
		}
	}

	// Tonemaps a linear RGB ScratchImage in-place: Reinhard c/(1+c), then gamma-2.2.
	void TonemapHdrToSrgb(DirectX::ScratchImage& image)
	{
		using namespace DirectX;
		DirectX::ScratchImage tonemapped;
		const HRESULT hr = TransformImage(
			image.GetImages(),
			image.GetImageCount(),
			image.GetMetadata(),
			[](XMVECTOR* outPixels, const XMVECTOR* inPixels, size_t width, size_t /*y*/) {
				const XMVECTOR one = XMVectorSplatOne();
				const XMVECTOR invGamma = XMVectorReplicate(1.0f / 2.2f);
				for (size_t i = 0; i < width; ++i) {
					XMVECTOR c = XMVectorMax(inPixels[i], XMVectorZero());
					const XMVECTOR rgb = XMVectorDivide(c, XMVectorAdd(c, one));
					const XMVECTOR gammaCorrected = XMVectorPow(rgb, invGamma);
					outPixels[i] = XMVectorSelect(gammaCorrected, c, g_XMSelect1110);
				}
			},
			tonemapped);
		if (SUCCEEDED(hr)) {
			image = std::move(tonemapped);
		}
	}

	const DirectX::Image* PrepareBmpImage(DirectX::ScratchImage& sourceImage, DirectX::ScratchImage& convertedImage)
	{
		if (sourceImage.GetMetadata().format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
			TonemapHdrToSrgb(sourceImage);
		}

		if (SUCCEEDED(DirectX::Convert(
				sourceImage.GetImages(),
				sourceImage.GetImageCount(),
				sourceImage.GetMetadata(),
				DXGI_FORMAT_B8G8R8X8_UNORM,
				DirectX::TEX_FILTER_DEFAULT,
				0.0f,
				convertedImage))) {
			return convertedImage.GetImage(0, 0, 0);
		}

		return sourceImage.GetImage(0, 0, 0);
	}

	// Game-root-relative paths (e.g. "Screenshots") must be absolute for CF_HDROP / Discord.
	std::filesystem::path ResolveToAbsoluteGamePath(const std::filesystem::path& path)
	{
		if (path.is_absolute()) {
			return path;
		}
		wchar_t buffer[MAX_PATH]{};
		const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
		if (length > 0 && length < MAX_PATH) {
			return std::filesystem::path(buffer).parent_path() / path;
		}
		std::error_code ec;
		return std::filesystem::absolute(path, ec);
	}

	bool CopyFilePathToClipboardHDrop(const std::wstring& absolutePath)
	{
		if (absolutePath.empty()) {
			return false;
		}

		const size_t pathChars = absolutePath.size();
		const size_t bytes = sizeof(DROPFILES) + (pathChars + 2) * sizeof(wchar_t);
		HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
		if (!hMem) {
			return false;
		}

		auto* drop = static_cast<DROPFILES*>(GlobalLock(hMem));
		if (!drop) {
			GlobalFree(hMem);
			return false;
		}

		drop->pFiles = sizeof(DROPFILES);
		drop->fWide = TRUE;

		auto* files = reinterpret_cast<wchar_t*>(reinterpret_cast<BYTE*>(drop) + sizeof(DROPFILES));
		memcpy(files, absolutePath.c_str(), (pathChars + 1) * sizeof(wchar_t));

		GlobalUnlock(hMem);

		for (int attempt = 0; attempt < 8; ++attempt) {
			if (attempt > 0) {
				Sleep(1 << (attempt - 1));
			}
			if (!OpenClipboard(nullptr)) {
				continue;
			}
			EmptyClipboard();
			const bool placed = SetClipboardData(CF_HDROP, hMem) != nullptr;
			CloseClipboard();
			if (placed) {
				return true;
			}
		}

		GlobalFree(hMem);
		return false;
	}

	void RunOnMainThread(std::function<void()> fn)
	{
		if (auto* taskInterface = SKSE::GetTaskInterface()) {
			taskInterface->AddTask(std::move(fn));
		} else {
			fn();
		}
	}

	void CopySavedPathToClipboard(bool enabled, const std::filesystem::path& path)
	{
		if (!enabled || path.empty()) {
			return;
		}

		const auto absolutePath = ResolveToAbsoluteGamePath(path);
		std::error_code ec;
		if (!std::filesystem::exists(absolutePath, ec)) {
			logger::warn("Screenshot not found for clipboard: {}", absolutePath.string());
			return;
		}
		if (std::filesystem::file_size(absolutePath, ec) == 0) {
			logger::warn("Screenshot file is empty, skipping clipboard: {}", absolutePath.string());
			return;
		}

		if (!CopyFilePathToClipboardHDrop(absolutePath.wstring())) {
			logger::warn("Screenshot saved but clipboard copy failed.");
		}
	}

	// Resolves the slot's underlying texture, falling back to QueryInterface on
	// SRV/RTV when slot.texture is null (kFRAMEBUFFER on flat aliases the swap-
	// chain backbuffer that way). `holder` keeps the QI refcount alive across
	// the caller's use of the returned pointer.
	ID3D11Texture2D* ResolveSlotTexture(
		const RE::BSGraphics::RenderTargetData& slot,
		winrt::com_ptr<ID3D11Texture2D>& holder)
	{
		if (slot.texture) {
			return slot.texture;
		}
		auto resolveFromView = [&](ID3D11View* view) -> ID3D11Texture2D* {
			if (!view) {
				return nullptr;
			}
			winrt::com_ptr<ID3D11Resource> resource;
			view->GetResource(resource.put());
			if (!resource) {
				return nullptr;
			}
			if (FAILED(resource->QueryInterface(__uuidof(ID3D11Texture2D), holder.put_void()))) {
				return nullptr;
			}
			return holder.get();
		};
		if (auto* tex = resolveFromView(slot.SRV)) {
			return tex;
		}
		return resolveFromView(slot.RTV);
	}

	// Returns the texture that was presented to the display (post-ApplyHDR).
	ID3D11Texture2D* ResolveDisplayedBackBuffer(winrt::com_ptr<ID3D11Texture2D>& holder)
	{
		auto& imageReconstruction = globals::pipeline::imageReconstruction;
		if (imageReconstruction.d3d12SwapChainActive &&
			imageReconstruction.dx12SwapChain.swapChainBufferWrapped &&
			imageReconstruction.dx12SwapChain.swapChainBufferWrapped->resource11) {
			holder.copy_from(imageReconstruction.dx12SwapChain.swapChainBufferWrapped->resource11);
			return holder.get();
		}

		if (!globals::d3d::swapChain) {
			return nullptr;
		}

		winrt::com_ptr<ID3D11Texture2D> backBuffer;
		if (FAILED(globals::d3d::swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), backBuffer.put_void()))) {
			return nullptr;
		}
		holder = std::move(backBuffer);
		return holder.get();
	}

	bool IsFlatHdrScreenshotCapture()
	{
		return globals::pipeline::cameraSuite.loaded &&
		       globals::pipeline::cameraSuite.settings.enableHDR;
	}

	// Picks the capture source:
	//   HDR + CS menu open -> clean HDR composite (no UI, no menu blur).
	//   HDR enabled        -> swap-chain back buffer after ApplyHDR (PQ HDR10 / PQ float).
	//   otherwise          -> kFRAMEBUFFER (tonemapped UNORM).
	// forCapture: post-blur screenshot uses the snapshot; pre-blur preview uses hdrTexture.
	CaptureSource SelectCaptureSource(winrt::com_ptr<ID3D11Texture2D>& holder, bool forCapture)
	{
		CaptureSource src;
		auto* renderer = globals::game::renderer;
		if (!renderer) {
			return src;
		}

		if (IsFlatHdrScreenshotCapture()) {
			// Recompose from the clean scene with no UI buffer.
			auto& hdr = globals::pipeline::cameraSuite;
			if (Menu::GetSingleton()->IsEnabled && hdr.outputTexture && hdr.outputTexture->srv) {
				ID3D11ShaderResourceView* sceneSRV =
					(forCapture && hdr.IsCleanSceneCaptureFresh()) ? hdr.cleanSceneCapture->srv.get() :
																	 (hdr.hdrTexture ? hdr.hdrTexture->srv.get() : nullptr);
				if (sceneSRV) {
					if (ID3D11Texture2D* clean = hdr.ComposeCleanCapture(sceneSRV, /*sdrPreview=*/!forCapture)) {
						src.texture = clean;
						src.srv = hdr.outputTexture->srv.get();
						src.needsPreviewCache = false;
						src.description = "HDR clean composite (no UI, no menu blur)";
						return src;
					}
				}
			}

			src.texture = ResolveDisplayedBackBuffer(holder);
			src.needsPreviewCache = true;
			src.description = "Swap chain back buffer (HDR display composite)";
			return src;
		}

		auto& slot = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kFRAMEBUFFER];
		src.texture = ResolveSlotTexture(slot, holder);
		src.srv = slot.SRV;
		src.needsPreviewCache = true;
		src.description = "kFRAMEBUFFER";
		return src;
	}

	CaptureSource SelectPhotoFinishSource(
		winrt::com_ptr<ID3D11Texture2D>& holder,
		bool useNeuralSource)
	{
		if (useNeuralSource) {
			auto& swapChain = globals::pipeline::imageReconstruction.dx12SwapChain;
			if (auto* texture = swapChain.GetCompletedNeuralOutput()) {
				CaptureSource src;
				src.texture = texture;
				src.needsPreviewCache = false;
				src.description = "completed PIXL neural presentation output";
				return src;
			}
			// Capture setup needs dimensions/format before the first temporary
			// Photo NR frame is published. The resource is never sampled until the
			// serial synchronization below confirms a completed model evaluation.
			if (auto* texture = swapChain.GetProvisionedNeuralOutput()) {
				CaptureSource src;
				src.texture = texture;
				src.needsPreviewCache = false;
				src.description = "provisioned PIXL neural Photo Finish output";
				return src;
			}
		}

		return SelectCaptureSource(holder, /*forCapture=*/true);
	}

	// True when our hotkey is the single PrintScreen key vanilla binds. Anything
	// else (different key, chord, modifier) means the user wants both ours and
	// vanilla independently.
	bool HotkeyCollidesWithVanilla()
	{
		const auto& combo = Menu::GetSingleton()->GetSettings().ScreenshotKey;
		return combo.size() == 1 &&
		       combo[0].GetDevice() == InputDeviceType::Keyboard &&
		       combo[0].GetKey() == VK_SNAPSHOT;
	}

	// Forces BlendEnable=FALSE and opaque alpha for the preview Image draw.
	// Paired with ImDrawCallback_ResetRenderState queued by Subrect::DrawEditor.
	void OpaquePreviewBlendCallback(const ImDrawList*, const ImDrawCmd*)
	{
		const bool writeAlpha = IsFlatHdrScreenshotCapture() && Menu::GetSingleton()->IsEnabled;

		static winrt::com_ptr<ID3D11BlendState> rgbBlend;
		static winrt::com_ptr<ID3D11BlendState> rgbaBlend;
		auto& blend = writeAlpha ? rgbaBlend : rgbBlend;
		if (!blend) {
			D3D11_BLEND_DESC desc{};
			desc.RenderTarget[0].BlendEnable = FALSE;
			desc.RenderTarget[0].RenderTargetWriteMask =
				D3D11_COLOR_WRITE_ENABLE_RED |
				D3D11_COLOR_WRITE_ENABLE_GREEN |
				D3D11_COLOR_WRITE_ENABLE_BLUE |
				(writeAlpha ? D3D11_COLOR_WRITE_ENABLE_ALPHA : 0);
			globals::d3d::device->CreateBlendState(&desc, blend.put());
		}
		if (blend) {
			globals::d3d::context->OMSetBlendState(blend.get(), nullptr, 0xFFFFFFFF);
		}
	}

	std::filesystem::path BuildScreenshotPath(const std::string& screenshotPath, bool usePng)
	{
		SYSTEMTIME st;
		GetLocalTime(&st);
		char buf[80];
		const char* extension = usePng ? ".png" : ".bmp";
		snprintf(buf, sizeof(buf), "CS_%04d-%02d-%02d_%02d-%02d-%02d_%03d%s",
			st.wYear, st.wMonth, st.wDay,
			st.wHour, st.wMinute, st.wSecond,
			st.wMilliseconds,
			extension);
		return ResolveToAbsoluteGamePath(std::filesystem::path(screenshotPath) / buf);
	}

	struct HdrFormatInfo
	{
		DXGI_FORMAT dxgi;
		sk_hdr_png::format png;
		size_t bytesPerPixel;
	};

	constexpr HdrFormatInfo kHdrFormats[] = {
		{ DXGI_FORMAT_R10G10B10A2_UNORM, sk_hdr_png::format::r10g10b10a2_unorm, 4 },
		{ DXGI_FORMAT_R16G16B16A16_FLOAT, sk_hdr_png::format::r16g16b16a16_pq, 8 },
	};

	const HdrFormatInfo* LookupHdrFormat(DXGI_FORMAT format)
	{
		for (const auto& info : kHdrFormats) {
			if (info.dxgi == format) {
				return &info;
			}
		}
		return nullptr;
	}

	bool IsHdrCaptureFormat(DXGI_FORMAT format)
	{
		return LookupHdrFormat(format) != nullptr;
	}

	// sk_hdr_png requires 16-byte aligned pixel memory.
	bool CopyToAlignedPixelBuffer(
		const DirectX::Image& image,
		size_t bytesPerPixel,
		void*& outAligned,
		size_t& outByteSize)
	{
		if (bytesPerPixel == 0) {
			return false;
		}

		const size_t tightRowBytes = static_cast<size_t>(image.width) * bytesPerPixel;
		outByteSize = tightRowBytes * image.height;

		outAligned = _aligned_malloc(outByteSize, 16);
		if (!outAligned) {
			return false;
		}

		auto* dest = static_cast<uint8_t*>(outAligned);
		const auto* src = image.pixels;
		for (size_t row = 0; row < image.height; ++row) {
			memcpy(dest + row * tightRowBytes, src + row * image.rowPitch, tightRowBytes);
		}
		return true;
	}

	bool SaveHdrPng(
		const DirectX::ScratchImage& image,
		const std::filesystem::path& outputPath,
		int quantizationBits,
		DXGI_FORMAT format)
	{
		const DirectX::Image* firstImage = image.GetImage(0, 0, 0);
		const HdrFormatInfo* hdrInfo = firstImage ? LookupHdrFormat(format) : nullptr;
		if (!firstImage || !hdrInfo || firstImage->format != format) {
			return false;
		}

		void* alignedPixels = nullptr;
		size_t byteSize = 0;
		if (!CopyToAlignedPixelBuffer(*firstImage, hdrInfo->bytesPerPixel, alignedPixels, byteSize)) {
			return false;
		}

		const bool saved = sk_hdr_png::write_image_to_disk(
			outputPath.wstring().c_str(),
			static_cast<unsigned int>(firstImage->width),
			static_cast<unsigned int>(firstImage->height),
			alignedPixels,
			quantizationBits,
			hdrInfo->png,
			false);

		_aligned_free(alignedPixels);
		return saved;
	}

	bool SaveSdrScreenshot(
		DirectX::ScratchImage& image,
		const std::filesystem::path& outputPath,
		bool saveAsPng)
	{
		StripAlphaForBmp(image);
		DirectX::ScratchImage convertedImage;
		const DirectX::Image* saveImage = PrepareBmpImage(image, convertedImage);
		if (!saveImage) {
			return false;
		}

		const GUID& codec = saveAsPng ?
		                        DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG) :
		                        DirectX::GetWICCodec(DirectX::WIC_CODEC_BMP);
		return SUCCEEDED(DirectX::SaveToWICFile(
			*saveImage,
			DirectX::WIC_FLAGS_NONE,
			codec,
			outputPath.c_str()));
	}


	bool ConvertScratchToFloat(
		const DirectX::ScratchImage& source,
		DirectX::ScratchImage& output)
	{
		const DirectX::Image* src =
			source.GetImage(0, 0, 0);

		if (!src)
			return false;

		constexpr DXGI_FORMAT kWorkingFormat =
			DXGI_FORMAT_R32G32B32A32_FLOAT;

		if (src->format ==
			kWorkingFormat) {
			if (FAILED(
					output.Initialize2D(
						kWorkingFormat,
						src->width,
						src->height,
						1,
						1))) {
				return false;
			}

			const DirectX::Image* dst =
				output.GetImage(0, 0, 0);

			if (!dst)
				return false;

			for (size_t y = 0;
				 y < src->height;
				 ++y) {
				memcpy(
					output.GetPixels() +
						y * dst->rowPitch,
					src->pixels +
						y * src->rowPitch,
					std::min(
						dst->rowPitch,
						src->rowPitch));
			}

			return true;
		}

		return SUCCEEDED(
			DirectX::Convert(
				*src,
				kWorkingFormat,
				DirectX::
					TEX_FILTER_DEFAULT,
				0.0f,
				output));
	}

	bool BuildTemporalAverage(
		ID3D11DeviceContext* context,
		const std::vector<
			winrt::com_ptr<
				ID3D11Texture2D>>& samples,
		DXGI_FORMAT format,
		uint32_t width,
		uint32_t height,
		DirectX::ScratchImage& output)
	{
		if (!context ||
			samples.empty() ||
			width == 0 ||
			height == 0) {
			return false;
		}

		constexpr DXGI_FORMAT kWorkingFormat =
			DXGI_FORMAT_R32G32B32A32_FLOAT;

		if (FAILED(
				output.Initialize2D(
					kWorkingFormat,
					width,
					height,
					1,
					1))) {
			return false;
		}

		std::memset(
			output.GetPixels(),
			0,
			output.GetPixelsSize());

		const DirectX::Image* accumulatorImage =
			output.GetImage(0, 0, 0);

		if (!accumulatorImage)
			return false;

		for (const auto& staging :
			 samples) {
			if (!staging)
				return false;

			DirectX::ScratchImage nativeImage;

			if (!PopulateScratchImageFromStagingTexture(
					context,
					staging.get(),
					format,
					width,
					height,
					nativeImage)) {
				return false;
			}

			DirectX::ScratchImage floatImage;

			if (!ConvertScratchToFloat(
					nativeImage,
					floatImage)) {
				return false;
			}

			const DirectX::Image* src =
				floatImage.GetImage(
					0,
					0,
					0);

			if (!src ||
				src->width != width ||
				src->height != height) {
				return false;
			}

			for (size_t y = 0;
				 y < height;
				 ++y) {
				auto* dst =
					reinterpret_cast<float*>(
						output.GetPixels() +
						y *
							accumulatorImage->
								rowPitch);

				const auto* sourceRow =
					reinterpret_cast<
						const float*>(
							src->pixels +
							y *
								src->rowPitch);

				for (size_t x = 0;
					 x <
					 static_cast<size_t>(
						 width) *
						 4u;
					 ++x) {
					dst[x] +=
						sourceRow[x];
				}
			}
		}

		const float invCount =
			1.0f /
			static_cast<float>(
				samples.size());

		for (size_t y = 0;
			 y < height;
			 ++y) {
			auto* row =
				reinterpret_cast<float*>(
					output.GetPixels() +
					y *
						accumulatorImage->
							rowPitch);

			for (size_t x = 0;
				 x <
				 static_cast<size_t>(
					 width) *
					 4u;
				 ++x) {
				row[x] *=
					invCount;
			}
		}

		return true;
	}

	bool BuildJitterAwareSuperResolution(
		ID3D11DeviceContext* context,
		const std::vector<winrt::com_ptr<ID3D11Texture2D>>& samples,
		const std::vector<float2>& jitterOffsets,
		DXGI_FORMAT format,
		uint32_t width,
		uint32_t height,
		unsigned int outputScale,
		const DirectX::ScratchImage& nativeAverage,
		DirectX::ScratchImage& output)
	{
		// PIXL_PHOTO_SUPER_RESOLVE_V3
		// Each Photo Finish frame is rendered at a real Halton projection offset.
		// V2 only averaged those samples at identical integer coordinates and then
		// cubic-resized the average, throwing away the sub-pixel information. V3
		// splats each captured pixel into its jitter-corrected location on the final
		// output lattice. A low-weight cubic reconstruction acts only as a hole-fill
		// prior; genuine samples dominate wherever the jitter sequence provides data.
		if (!context || samples.empty() || width == 0 || height == 0)
			return false;

		outputScale = outputScale >= 4u ? 4u : outputScale >= 2u ? 2u : 1u;
		if (outputScale > 1u &&
			(samples.size() < 4u || jitterOffsets.size() < samples.size())) {
			return false;
		}
		const DirectX::Image* native = nativeAverage.GetImage(0, 0, 0);
		if (!native || native->format != DXGI_FORMAT_R32G32B32A32_FLOAT ||
			native->width != width || native->height != height)
			return false;

		if (outputScale == 1u) {
			if (FAILED(output.Initialize2D(native->format, native->width, native->height, 1, 1)))
				return false;
			const DirectX::Image* dst = output.GetImage(0, 0, 0);
			if (!dst) return false;
			for (size_t y = 0; y < native->height; ++y) {
				memcpy(output.GetPixels() + y * dst->rowPitch,
					native->pixels + y * native->rowPitch,
					std::min(dst->rowPitch, native->rowPitch));
			}
			return true;
		}

		const size_t outWidth = static_cast<size_t>(width) * outputScale;
		const size_t outHeight = static_cast<size_t>(height) * outputScale;
		if (FAILED(DirectX::Resize(*native, outWidth, outHeight,
			DirectX::TEX_FILTER_CUBIC, output)))
			return false;

		const DirectX::Image* outImage = output.GetImage(0, 0, 0);
		if (!outImage || outImage->format != DXGI_FORMAT_R32G32B32A32_FLOAT)
			return false;

		// Store resolve weight in alpha while accumulating. Screenshot encoders do
		// not rely on scene alpha; alpha is restored to 1.0 after normalization.
		constexpr float kPriorWeight = 0.20f;
		for (size_t y = 0; y < outHeight; ++y) {
			auto* row = reinterpret_cast<float*>(
				output.GetPixels() + y * outImage->rowPitch);
			for (size_t x = 0; x < outWidth; ++x) {
				const size_t base = x * 4u;
				row[base + 0u] *= kPriorWeight;
				row[base + 1u] *= kPriorWeight;
				row[base + 2u] *= kPriorWeight;
				row[base + 3u] = kPriorWeight;
			}
		}

		auto nativeLumaAt = [native](size_t x, size_t y) {
			const auto* row = reinterpret_cast<const float*>(
				native->pixels + y * native->rowPitch);
			const size_t base = x * 4u;
			return row[base + 0u] * 0.2126f +
				row[base + 1u] * 0.7152f +
				row[base + 2u] * 0.0722f;
		};

		const float scale = static_cast<float>(outputScale);
		for (size_t sampleIndex = 0; sampleIndex < samples.size(); ++sampleIndex) {
			const auto& staging = samples[sampleIndex];
			if (!staging)
				return false;

			DirectX::ScratchImage nativeImage;
			if (!PopulateScratchImageFromStagingTexture(
					context, staging.get(), format, width, height, nativeImage))
				return false;

			DirectX::ScratchImage floatImage;
			if (!ConvertScratchToFloat(nativeImage, floatImage))
				return false;

			const DirectX::Image* src = floatImage.GetImage(0, 0, 0);
			if (!src || src->width != width || src->height != height)
				return false;

			const float2 jitter = sampleIndex < jitterOffsets.size()
				? jitterOffsets[sampleIndex]
				: float2{ 0.0f, 0.0f };

			for (size_t y = 0; y < height; ++y) {
				const auto* srcRow = reinterpret_cast<const float*>(
					src->pixels + y * src->rowPitch);
				for (size_t x = 0; x < width; ++x) {
					const size_t srcBase = x * 4u;
					const float r = srcRow[srcBase + 0u];
					const float g = srcRow[srcBase + 1u];
					const float b = srcRow[srcBase + 2u];
					const float sampleLum = r * 0.2126f + g * 0.7152f + b * 0.0722f;
					const float meanLum = nativeLumaAt(x, y);
					const float normalizedDelta =
						std::abs(sampleLum - meanLum) /
						(0.035f + std::abs(meanLum) * 0.20f);
					const float temporalWeight = std::clamp(
						1.0f / (1.0f + normalizedDelta * normalizedDelta * 1.8f),
						0.12f,
						1.0f);

					// Positive projection jitter moves geometry toward negative screen X/Y;
					// map the captured pixel back by +jitter onto the unjittered lattice.
					const float fx =
						(static_cast<float>(x) + 0.5f + jitter.x) * scale - 0.5f;
					const float fy =
						(static_cast<float>(y) + 0.5f + jitter.y) * scale - 0.5f;
					const int x0 = static_cast<int>(std::floor(fx));
					const int y0 = static_cast<int>(std::floor(fy));
					const float tx = fx - static_cast<float>(x0);
					const float ty = fy - static_cast<float>(y0);

					const int px[2] = { x0, x0 + 1 };
					const int py[2] = { y0, y0 + 1 };
					const float wx[2] = { 1.0f - tx, tx };
					const float wy[2] = { 1.0f - ty, ty };
					for (int oy = 0; oy < 2; ++oy) {
						if (py[oy] < 0 || py[oy] >= static_cast<int>(outHeight))
							continue;
						auto* outRow = reinterpret_cast<float*>(
							output.GetPixels() + static_cast<size_t>(py[oy]) * outImage->rowPitch);
						for (int ox = 0; ox < 2; ++ox) {
							if (px[ox] < 0 || px[ox] >= static_cast<int>(outWidth))
								continue;
							const float w = wx[ox] * wy[oy] * temporalWeight;
							if (w <= 1.0e-6f)
								continue;
							const size_t dstBase = static_cast<size_t>(px[ox]) * 4u;
							outRow[dstBase + 0u] += r * w;
							outRow[dstBase + 1u] += g * w;
							outRow[dstBase + 2u] += b * w;
							outRow[dstBase + 3u] += w;
						}
					}
				}
			}
		}

		for (size_t y = 0; y < outHeight; ++y) {
			auto* row = reinterpret_cast<float*>(
				output.GetPixels() + y * outImage->rowPitch);
			for (size_t x = 0; x < outWidth; ++x) {
				const size_t base = x * 4u;
				const float weight = std::max(row[base + 3u], 1.0e-6f);
				row[base + 0u] = std::max(0.0f, row[base + 0u] / weight);
				row[base + 1u] = std::max(0.0f, row[base + 1u] / weight);
				row[base + 2u] = std::max(0.0f, row[base + 2u] / weight);
				row[base + 3u] = 1.0f;
			}
		}

		logger::info(
			"Photo Finish V3 jitter-aware resolve: {} samples -> {}x{} ({}x), {} jitter offsets consumed.",
			samples.size(), outWidth, outHeight, outputScale,
			std::min(samples.size(), jitterOffsets.size()));
		return true;
	}

	bool ApplyDirectionalMotionFinish(
		DirectX::ScratchImage& image,
		float strength,
		float angleDegrees)
	{
		strength =
			std::clamp(
				strength,
				0.0f,
				1.0f);

		if (strength <=
			0.0001f) {
			return true;
		}

		const DirectX::Image* src =
			image.GetImage(0, 0, 0);

		if (!src ||
			src->format !=
				DXGI_FORMAT_R32G32B32A32_FLOAT) {
			return false;
		}

		DirectX::ScratchImage finished;

		if (FAILED(
				finished.Initialize2D(
					src->format,
					src->width,
					src->height,
					1,
					1))) {
			return false;
		}

		const DirectX::Image* dstImage =
			finished.GetImage(0, 0, 0);

		if (!dstImage)
			return false;

		constexpr float kDegreesToRadians =
			0.017453292519943295769f;

		const float angle =
			angleDegrees *
			kDegreesToRadians;

		const float dirX =
			std::cos(angle);
		const float dirY =
			std::sin(angle);

		const int taps =
			std::clamp(
				3 +
					static_cast<int>(
						std::round(
							strength *
								12.0f)),
				3,
				15) |
			1;

		const float radius =
			1.0f +
			strength *
				11.0f;

		auto sampleChannel =
			[src](
				float x,
				float y,
				int channel) {
				x =
					std::clamp(
						x,
						0.0f,
						static_cast<float>(
							src->width -
								1));
				y =
					std::clamp(
						y,
						0.0f,
						static_cast<float>(
							src->height -
								1));

				const int x0 =
					static_cast<int>(
						std::floor(x));
				const int y0 =
					static_cast<int>(
						std::floor(y));
				const int x1 =
					std::min(
						x0 + 1,
						static_cast<int>(
							src->width -
								1));
				const int y1 =
					std::min(
						y0 + 1,
						static_cast<int>(
							src->height -
								1));

				const float tx =
					x -
					static_cast<float>(
						x0);
				const float ty =
					y -
					static_cast<float>(
						y0);

				auto valueAt =
					[src, channel](
						int px,
						int py) {
						const auto* row =
							reinterpret_cast<
								const float*>(
									src->pixels +
									static_cast<size_t>(
										py) *
										src->rowPitch);

						return row[
							static_cast<size_t>(
								px) *
								4u +
							static_cast<size_t>(
								channel)];
					};

				const float a =
					std::lerp(
						valueAt(
							x0,
							y0),
						valueAt(
							x1,
							y0),
						tx);

				const float b =
					std::lerp(
						valueAt(
							x0,
							y1),
						valueAt(
							x1,
							y1),
						tx);

				return std::lerp(
					a,
					b,
					ty);
			};

		const float blend =
			std::clamp(
				strength *
					0.88f,
				0.0f,
				0.88f);

		for (size_t y = 0;
			 y < src->height;
			 ++y) {
			const auto* centerRow =
				reinterpret_cast<
					const float*>(
						src->pixels +
						y *
							src->rowPitch);

			auto* dst =
				reinterpret_cast<float*>(
					finished.GetPixels() +
					y *
						dstImage->rowPitch);

			for (size_t x = 0;
				 x < src->width;
				 ++x) {
				float sum[3]{
					0.0f,
					0.0f,
					0.0f
				};
				float weightTotal =
					0.0f;

				for (int tap = 0;
					 tap < taps;
					 ++tap) {
					const float normalized =
						taps > 1
							? static_cast<float>(
								  tap) /
									  static_cast<float>(
										  taps -
										  1)
							: 0.5f;

					const float signedT =
						normalized *
							2.0f -
						1.0f;

					const float weight =
						1.0f -
						std::abs(
							signedT) *
							0.55f;

					const float sampleX =
						static_cast<float>(
							x) +
						dirX *
							signedT *
							radius;

					const float sampleY =
						static_cast<float>(
							y) +
						dirY *
							signedT *
							radius;

					for (int channel = 0;
						 channel < 3;
						 ++channel) {
						sum[channel] +=
							sampleChannel(
								sampleX,
								sampleY,
								channel) *
							weight;
					}

					weightTotal +=
						weight;
				}

				const size_t base =
					x * 4u;

				for (int channel = 0;
					 channel < 3;
					 ++channel) {
					const float blurred =
						weightTotal >
								0.0f
							? sum[channel] /
								  weightTotal
							: centerRow[
								  base +
								  channel];

					dst[
						base +
						channel] =
						std::max(
							0.0f,
							std::lerp(
								centerRow[
									base +
									channel],
								blurred,
								blend));
				}

				dst[
					base +
					3u] =
					centerRow[
						base +
						3u];
			}
		}

		image =
			std::move(
				finished);

		return true;
	}

	bool ApplyDetailReconstruction(
		DirectX::ScratchImage& image,
		float strength)
	{
		// PIXL_DETAIL_RECONSTRUCTION_V3
		// Luma-guided, multi-scale detail recovery. The previous implementation
		// sharpened RGB channels independently against a four-neighbour average,
		// which made the effect easy to miss at low settings and could create
		// coloured edge halos at high settings. V3 reconstructs luminance detail
		// across two spatial scales, damps strong silhouette edges, then applies a
		// bounded luminance gain back to RGB so chroma remains stable.
		strength = std::clamp(strength, 0.0f, 1.0f);
		if (strength <= 0.0001f)
			return true;

		const DirectX::Image* src = image.GetImage(0, 0, 0);
		if (!src || src->format != DXGI_FORMAT_R32G32B32A32_FLOAT)
			return false;

		DirectX::ScratchImage detailed;
		if (FAILED(detailed.Initialize2D(src->format, src->width, src->height, 1, 1)))
			return false;

		const DirectX::Image* dstImage = detailed.GetImage(0, 0, 0);
		if (!dstImage)
			return false;

		auto pixelAt = [src](int x, int y, int channel) {
			x = std::clamp(x, 0, static_cast<int>(src->width) - 1);
			y = std::clamp(y, 0, static_cast<int>(src->height) - 1);
			const auto* row = reinterpret_cast<const float*>(
				src->pixels + static_cast<size_t>(y) * src->rowPitch);
			return row[static_cast<size_t>(x) * 4u + static_cast<size_t>(channel)];
		};

		auto lumaAt = [&pixelAt](int x, int y) {
			const float r = pixelAt(x, y, 0);
			const float g = pixelAt(x, y, 1);
			const float b = pixelAt(x, y, 2);
			return r * 0.2126f + g * 0.7152f + b * 0.0722f;
		};

		const float gain = 0.55f + strength * 1.65f;
		const float midGain = 0.18f + strength * 0.42f;

		for (size_t y = 0; y < src->height; ++y) {
			const auto* centerRow = reinterpret_cast<const float*>(
				src->pixels + y * src->rowPitch);
			auto* dst = reinterpret_cast<float*>(
				detailed.GetPixels() + y * dstImage->rowPitch);

			for (size_t x = 0; x < src->width; ++x) {
				const int ix = static_cast<int>(x);
				const int iy = static_cast<int>(y);
				const size_t base = x * 4u;
				const float centerLum = lumaAt(ix, iy);

				// Compact Gaussian-like 3x3 footprint.
				const float fineBlur =
					(centerLum * 4.0f +
					 (lumaAt(ix - 1, iy) + lumaAt(ix + 1, iy) +
					  lumaAt(ix, iy - 1) + lumaAt(ix, iy + 1)) * 2.0f +
					 (lumaAt(ix - 1, iy - 1) + lumaAt(ix + 1, iy - 1) +
					  lumaAt(ix - 1, iy + 1) + lumaAt(ix + 1, iy + 1))) /
					16.0f;

				// Wider structure estimate catches texture that cubic reconstruction
				// otherwise smooths without turning the pass into simple sharpening.
				const float wideBlur =
					(centerLum * 2.0f +
					 lumaAt(ix - 2, iy) + lumaAt(ix + 2, iy) +
					 lumaAt(ix, iy - 2) + lumaAt(ix, iy + 2) +
					 0.5f * (lumaAt(ix - 2, iy - 2) + lumaAt(ix + 2, iy - 2) +
							 lumaAt(ix - 2, iy + 2) + lumaAt(ix + 2, iy + 2))) /
					8.0f;

				float localMin = centerLum;
				float localMax = centerLum;
				for (int oy = -1; oy <= 1; ++oy) {
					for (int ox = -1; ox <= 1; ++ox) {
						const float l = lumaAt(ix + ox, iy + oy);
						localMin = std::min(localMin, l);
						localMax = std::max(localMax, l);
					}
				}

				const float localContrast = std::max(localMax - localMin, 0.0f);
				const float gradient =
					std::abs(lumaAt(ix - 1, iy) - lumaAt(ix + 1, iy)) +
					std::abs(lumaAt(ix, iy - 1) - lumaAt(ix, iy + 1));
				const float edgeDamping = 1.0f / (1.0f + gradient * (2.2f + 2.8f * strength));
				const float textureConfidence = std::clamp(
					localContrast / (0.018f + std::abs(centerLum) * 0.16f), 0.0f, 1.0f);

				float detail =
					(centerLum - fineBlur) * gain +
					(centerLum - wideBlur) * midGain;
				detail *= std::lerp(0.42f, 1.0f, textureConfidence) *
					std::lerp(0.68f, 1.0f, edgeDamping);

				const float limiter =
					0.010f + localContrast * (0.30f + 0.22f * strength) +
					std::abs(centerLum) * 0.045f;
				detail = std::clamp(detail, -limiter, limiter);

				const float haloMargin = 0.012f + localContrast * 0.16f;
				const float targetLum = std::clamp(
					centerLum + detail,
					localMin - haloMargin,
					localMax + haloMargin);

				const float lumScale = centerLum > 1.0e-5f
					? std::clamp(targetLum / centerLum, 0.72f, 1.38f)
					: 1.0f;

				for (int channel = 0; channel < 3; ++channel) {
					const float center = centerRow[base + static_cast<size_t>(channel)];
					dst[base + static_cast<size_t>(channel)] =
						std::max(0.0f, center * lumScale);
				}
				dst[base + 3u] = centerRow[base + 3u];
			}
		}

		image = std::move(detailed);
		return true;
	}


	bool ConvertDepthScratchToFloat(
		const DirectX::ScratchImage& source,
		DirectX::ScratchImage& output)
	{
		const DirectX::Image* src =
			source.GetImage(0, 0, 0);

		if (!src)
			return false;

		return SUCCEEDED(
			DirectX::Convert(
				*src,
				DXGI_FORMAT_R32_FLOAT,
				DirectX::TEX_FILTER_DEFAULT,
				0.0f,
				output));
	}

	bool ApplyPhotoLensDepthOfField(
		DirectX::ScratchImage& image,
		const DirectX::ScratchImage& depthImage,
		float focusDistance,
		float focusRange,
		float strength,
		unsigned int quality,
		unsigned int apertureBlades,
		float highlightBoost,
		float edgeProtection,
		float foregroundCoverage,
		float catEye,
		float anamorphicRatio,
		float outputPixelScale)
	{
		// PIXL_PHOTO_LENS_V3
		// The photo-only lens pass runs after super-resolution, so aperture radius
		// must scale with the output lattice to preserve the same optical blur at
		// Native, 2x and 4x. V2 used a fixed final-pixel radius, which made DOF
		// progressively weaker as output resolution increased.
		strength = std::clamp(strength, 0.0f, 1.0f);
		if (strength <= 0.0001f)
			return true;

		const DirectX::Image* src = image.GetImage(0, 0, 0);
		const DirectX::Image* depth = depthImage.GetImage(0, 0, 0);
		if (!src || !depth ||
			src->format != DXGI_FORMAT_R32G32B32A32_FLOAT ||
			depth->format != DXGI_FORMAT_R32_FLOAT ||
			depth->width != src->width || depth->height != src->height)
			return false;

		focusDistance = std::clamp(focusDistance, 25.0f, 20000.0f);
		focusRange = std::clamp(focusRange, 100.0f, 20000.0f);
		edgeProtection = std::clamp(edgeProtection, 0.0f, 2.0f);
		foregroundCoverage = std::clamp(foregroundCoverage, 0.0f, 1.5f);
		catEye = std::clamp(catEye, 0.0f, 1.0f);
		anamorphicRatio = std::clamp(anamorphicRatio, 0.5f, 2.0f);
		outputPixelScale = std::clamp(outputPixelScale, 1.0f, 4.0f);
		apertureBlades = std::clamp(apertureBlades, 5u, 11u);
		highlightBoost = std::clamp(highlightBoost, 0.0f, 0.5f);

		const unsigned int clampedQuality = std::min(quality, 3u);
		const int tapTable[4] = { 12, 22, 36, 52 };
		const float radiusTable[4] = { 4.5f, 9.0f, 16.0f, 24.0f };
		const int maxTaps = tapTable[clampedQuality];
		const float maxRadius =
			radiusTable[clampedQuality] * strength * outputPixelScale;
		const float focusDeadZone = std::max(18.0f, focusRange * 0.07f);
		const float nearRange = std::max(80.0f, focusRange * 0.72f);
		const float farRange = std::max(100.0f, focusRange);

		auto depthAt = [depth](int x, int y) {
			x = std::clamp(x, 0, static_cast<int>(depth->width) - 1);
			y = std::clamp(y, 0, static_cast<int>(depth->height) - 1);
			const auto* row = reinterpret_cast<const float*>(
				depth->pixels + static_cast<size_t>(y) * depth->rowPitch);
			return row[x];
		};

		auto signedCoC = [=](float viewDepth) {
			if (!std::isfinite(viewDepth) || viewDepth <= 1.0e-4f)
				return 0.0f;
			// HybridGI WorkingDepth is already linear view-space depth. Very large
			// values are the sky/far clear and correctly behave as optical infinity.
			const float delta = viewDepth - focusDistance;
			const float absDelta = std::max(std::abs(delta) - focusDeadZone, 0.0f);
			if (absDelta <= 0.0f)
				return 0.0f;
			const float denom = delta < 0.0f ? nearRange : farRange;
			return std::clamp((delta < 0.0f ? -1.0f : 1.0f) * absDelta / denom, -1.0f, 1.0f);
		};

		auto sampleColor = [src](float x, float y, int channel) {
			x = std::clamp(x, 0.0f, static_cast<float>(src->width - 1u));
			y = std::clamp(y, 0.0f, static_cast<float>(src->height - 1u));
			const int x0 = static_cast<int>(std::floor(x));
			const int y0 = static_cast<int>(std::floor(y));
			const int x1 = std::min(x0 + 1, static_cast<int>(src->width) - 1);
			const int y1 = std::min(y0 + 1, static_cast<int>(src->height) - 1);
			const float tx = x - static_cast<float>(x0);
			const float ty = y - static_cast<float>(y0);
			auto valueAt = [src, channel](int px, int py) {
				const auto* row = reinterpret_cast<const float*>(
					src->pixels + static_cast<size_t>(py) * src->rowPitch);
				return row[static_cast<size_t>(px) * 4u + static_cast<size_t>(channel)];
			};
			const float a = std::lerp(valueAt(x0,y0), valueAt(x1,y0), tx);
			const float b = std::lerp(valueAt(x0,y1), valueAt(x1,y1), tx);
			return std::lerp(a,b,ty);
		};

		DirectX::ScratchImage finished;
		if (FAILED(finished.Initialize2D(src->format, src->width, src->height, 1, 1)))
			return false;
		const DirectX::Image* dstImage = finished.GetImage(0,0,0);
		if (!dstImage) return false;

		constexpr float kTwoPi = 6.28318530717958647692f;
		constexpr float kGoldenAngle = 2.39996322972865332223f;
		const float apertureX = std::sqrt(anamorphicRatio);
		const float apertureY = 1.0f / std::max(apertureX, 1.0e-4f);

		for (size_t y=0; y<src->height; ++y) {
			const auto* centerRow = reinterpret_cast<const float*>(src->pixels + y*src->rowPitch);
			auto* dst = reinterpret_cast<float*>(finished.GetPixels() + y*dstImage->rowPitch);
			for (size_t x=0; x<src->width; ++x) {
				const size_t base=x*4u;
				const float centerDepth=depthAt(static_cast<int>(x),static_cast<int>(y));
				const float centerCoC=signedCoC(centerDepth);
				const float cocAbs=std::abs(centerCoC);
				float radius=cocAbs*maxRadius;

				// A gather driven only by the receiver CoC cannot spread a defocused
				// foreground silhouette over a sharp/far receiver. Probe a compact ring
				// for nearby negative CoC and enlarge the gather where foreground bokeh
				// should optically cover the receiver.
				float nearbyForegroundCoC=0.0f;
				const float probeRadius=std::clamp(maxRadius*0.28f,1.5f*outputPixelScale,9.0f*outputPixelScale);
				for(int probe=0;probe<8;++probe) {
					const float probeAngle=static_cast<float>(probe)*(kTwoPi/8.0f);
					const int px=static_cast<int>(std::round(static_cast<float>(x)+std::cos(probeAngle)*probeRadius));
					const int py=static_cast<int>(std::round(static_cast<float>(y)+std::sin(probeAngle)*probeRadius));
					nearbyForegroundCoC=std::min(nearbyForegroundCoC,signedCoC(depthAt(px,py)));
				}
				const float foregroundRadius=std::abs(nearbyForegroundCoC)*maxRadius*foregroundCoverage*0.90f;
				radius=std::max(radius,foregroundRadius);
				if (radius<0.45f) {
					for(int c=0;c<4;++c) dst[base+c]=centerRow[base+c];
					continue;
				}
				const float coverageCoC=std::max(cocAbs,std::abs(nearbyForegroundCoC)*foregroundCoverage);
				const int taps=std::clamp(8+static_cast<int>(std::ceil(coverageCoC*maxTaps)),8,maxTaps);
				float sum[3]={centerRow[base],centerRow[base+1],centerRow[base+2]};
				float weightTotal=1.0f;
				const float2 screenVector={
					(static_cast<float>(x)+0.5f)/static_cast<float>(src->width)*2.0f-1.0f,
					(static_cast<float>(y)+0.5f)/static_cast<float>(src->height)*2.0f-1.0f};
				const float edgeMagnitude=std::sqrt(screenVector.x*screenVector.x+screenVector.y*screenVector.y);
				const float edge=std::clamp(edgeMagnitude,0.0f,1.0f);
				for(int tap=0;tap<taps;++tap) {
					const float nr=std::sqrt((static_cast<float>(tap)+0.5f)/static_cast<float>(taps));
					const float angle=static_cast<float>(tap)*kGoldenAngle;
					const float bladeSpan=kTwoPi/static_cast<float>(apertureBlades);
					float bladeAngle=std::fmod(angle,bladeSpan);
					if(bladeAngle>bladeSpan*0.5f) bladeAngle-=bladeSpan;
					const float polygonScale=std::cos(3.14159265358979323846f/static_cast<float>(apertureBlades))/
						std::max(0.35f,std::cos(bladeAngle));
					float ox=std::cos(angle)*radius*nr*polygonScale*apertureX;
					float oy=std::sin(angle)*radius*nr*polygonScale*apertureY;
					// Mechanical vignetting / cat-eye compression acts along the radial
					// sensor direction, not only the horizontal aperture axis.
					const float catScale=1.0f-catEye*edge*0.46f;
					if(edgeMagnitude>1.0e-4f) {
						const float rx=screenVector.x/edgeMagnitude;
						const float ry=screenVector.y/edgeMagnitude;
						float radial=ox*rx+oy*ry;
						const float tangent=-ox*ry+oy*rx;
						radial*=catScale;
						ox=radial*rx-tangent*ry;
						oy=radial*ry+tangent*rx;
					}
					const float sx=static_cast<float>(x)+ox;
					const float sy=static_cast<float>(y)+oy;
					const float sampleDepth=depthAt(static_cast<int>(std::round(sx)),static_cast<int>(std::round(sy)));
					const float sampleCoC=signedCoC(sampleDepth);
					const float depthGap=std::abs(sampleDepth-centerDepth)/std::max(focusRange,100.0f);
					const bool sameLayer=(centerCoC*sampleCoC)>=0.0f;
					float layerWeight=std::exp2(-depthGap*(5.0f+18.0f*edgeProtection));
					if(!sameLayer) {
						// Near defocus can occlude a farther receiver; background is not
						// allowed to leak forward across a foreground silhouette.
						if(sampleCoC<0.0f && centerCoC>=0.0f)
							layerWeight*=std::clamp(0.18f+0.55f*foregroundCoverage*std::abs(sampleCoC),0.0f,0.82f);
						else
							layerWeight*=0.018f;
					}
					if(sampleCoC<0.0f && foregroundRadius>cocAbs*maxRadius)
						layerWeight*=1.0f+0.35f*foregroundCoverage*std::abs(sampleCoC);
					float rgb[3];
					for(int c=0;c<3;++c) rgb[c]=sampleColor(sx,sy,c);
					const float lum=rgb[0]*0.2126f+rgb[1]*0.7152f+rgb[2]*0.0722f;
					const float highlight=1.0f+std::clamp(std::max(lum-1.0f,0.0f)*highlightBoost,0.0f,0.85f);
					const float radial=1.0f-nr*0.30f;
					const float w=std::max(layerWeight*highlight*radial,1.0e-5f);
					for(int c=0;c<3;++c) sum[c]+=rgb[c]*w;
					weightTotal+=w;
				}
				for(int c=0;c<3;++c) dst[base+c]=std::max(0.0f,sum[c]/std::max(weightTotal,1.0e-5f));
				dst[base+3]=centerRow[base+3];
			}
		}
		image=std::move(finished);
		return true;
	}

	bool BuildPhotoFinishImage(
		ID3D11DeviceContext* context,
		const std::vector<winrt::com_ptr<ID3D11Texture2D>>& samples,
		const std::vector<float2>& jitterOffsets,
		ID3D11Texture2D* depthStagingTexture,
		DXGI_FORMAT depthFormat,
		DXGI_FORMAT sourceFormat,
		uint32_t width,
		uint32_t height,
		unsigned int outputScale,
		float detailStrength,
		bool lensDofEnabled,
		unsigned int lensDofQuality,
		float lensDofStrength,
		unsigned int lensDofApertureBlades,
		float lensDofHighlightBoost,
		float lensDofFocusDistance,
		float lensDofFocusRange,
		float lensDofEdgeProtection,
		float lensDofForegroundCoverage,
		float lensDofCatEye,
		float lensDofAnamorphicRatio,
		bool motionEnabled,
		float motionStrength,
		float motionAngleDegrees,
		const std::function<void(PixelCapture::PhotoFinishStage,float)>& reportProgress,
		DirectX::ScratchImage& output)
	{
		DirectX::ScratchImage working;
		if(reportProgress) reportProgress(PixelCapture::PhotoFinishStage::Resolving,0.30f);
		if(!BuildTemporalAverage(context,samples,sourceFormat,width,height,working)) return false;

		if(motionEnabled && reportProgress) reportProgress(PixelCapture::PhotoFinishStage::MotionFinish,0.48f);
		if(motionEnabled && !ApplyDirectionalMotionFinish(working,motionStrength,motionAngleDegrees)) return false;

		if(reportProgress) reportProgress(PixelCapture::PhotoFinishStage::DetailRecovery,0.58f);
		if(!ApplyDetailReconstruction(working,detailStrength)) return false;

		outputScale = outputScale>=4 ? 4u : outputScale>=2 ? 2u : 1u;
		DirectX::ScratchImage finalWorking;
		if(outputScale>1u) {
			if(reportProgress) reportProgress(PixelCapture::PhotoFinishStage::Upscaling,0.68f);
			if(!BuildJitterAwareSuperResolution(
				context,samples,jitterOffsets,sourceFormat,width,height,outputScale,working,finalWorking)) {
				logger::warn("Photo Finish V3 jitter-aware reconstruction failed; falling back to cubic resize.");
				const DirectX::Image* native=working.GetImage(0,0,0);
				if(!native) return false;
				if(FAILED(DirectX::Resize(*native,static_cast<size_t>(width)*outputScale,
					static_cast<size_t>(height)*outputScale,DirectX::TEX_FILTER_CUBIC,finalWorking))) return false;
			}
			// Restore local micro-contrast created by the jitter solve. Keep this
			// bounded and before lens blur so defocused areas are never re-sharpened.
			if(!ApplyDetailReconstruction(finalWorking,std::clamp(detailStrength*0.40f,0.0f,0.40f))) return false;
		} else {
			finalWorking=std::move(working);
		}

		if(lensDofEnabled && depthStagingTexture) {
			if(reportProgress) reportProgress(PixelCapture::PhotoFinishStage::LensDepthOfField,0.79f);
			DirectX::ScratchImage depthNative,depthFloat,depthFinal;
			if(PopulateScratchImageFromStagingTexture(context,depthStagingTexture,depthFormat,width,height,depthNative) &&
				ConvertDepthScratchToFloat(depthNative,depthFloat)) {
				const DirectX::ScratchImage* lensDepth=&depthFloat;
				if(outputScale>1u) {
					const DirectX::Image* d=depthFloat.GetImage(0,0,0);
					if(d && SUCCEEDED(DirectX::Resize(*d,static_cast<size_t>(width)*outputScale,
						static_cast<size_t>(height)*outputScale,DirectX::TEX_FILTER_POINT,depthFinal))) lensDepth=&depthFinal;
				}
				if(!ApplyPhotoLensDepthOfField(finalWorking,*lensDepth,lensDofFocusDistance,lensDofFocusRange,
					lensDofStrength,lensDofQuality,lensDofApertureBlades,lensDofHighlightBoost,
					lensDofEdgeProtection,lensDofForegroundCoverage,lensDofCatEye,lensDofAnamorphicRatio,
					static_cast<float>(outputScale)))
					logger::warn("PIXL Photo Lens DOF skipped: offline lens resolve failed.");
			} else {
				logger::warn("PIXL Photo Lens DOF skipped: linear WorkingDepth staging could not be read.");
			}
		}

		const DirectX::Image* finalImage=finalWorking.GetImage(0,0,0);
		if(!finalImage) return false;
		return SUCCEEDED(DirectX::Convert(*finalImage,sourceFormat,DirectX::TEX_FILTER_DEFAULT,0.0f,output));
	}

	unsigned int ClampPhotoFinishScaleForSize(
		unsigned int requestedScale,
		uint32_t width,
		uint32_t height)
	{
		unsigned int scale =
			requestedScale >= 4
				? 4u
				: requestedScale >= 2
					? 2u
					: 1u;

		// Keep worst-case captures sane on 4K+ displays. 48 MP still allows
		// 1920x1200 -> 7680x4800 while preventing 4x 4K from allocating an
		// enormous intermediate image inside a live game process.
		constexpr uint64_t kMaxOutputPixels =
			48ull *
			1000ull *
			1000ull;

		while (scale > 1u) {
			const uint64_t outputPixels =
				static_cast<uint64_t>(
					width) *
				static_cast<uint64_t>(
					height) *
				static_cast<uint64_t>(
					scale) *
				static_cast<uint64_t>(
					scale);

			if (outputPixels <=
				kMaxOutputPixels) {
				break;
			}

			scale =
				scale >= 4u
					? 2u
					: 1u;
		}

		return scale;
	}

	bool SaveScreenshotToDisk(
		DirectX::ScratchImage& image,
		const std::filesystem::path& outputPath,
		DXGI_FORMAT format,
		int hdrPngBitDepth,
		bool saveAsHdrPng,
		bool saveAsSdrPng)
	{
		if (saveAsHdrPng) {
			return SaveHdrPng(image, outputPath, hdrPngBitDepth, format);
		}
		return SaveSdrScreenshot(image, outputPath, saveAsSdrPng);
	}

}

PixelCapture::~PixelCapture()
{
	StopWorkerThread();
}

bool PixelCapture::IsInMenu() const
{
	return true;
}

void PixelCapture::PostPostLoad()
{
}

void PixelCapture::LoadSettings(json& a_json)
{
	if (a_json.contains("ScreenshotPath"))
		screenshotPath = a_json["ScreenshotPath"];
	if (a_json.contains("ApplyCropToScreenshot"))
		applyCropToScreenshot = a_json["ApplyCropToScreenshot"];
	if (a_json.contains("HdrPngBitDepth"))
		hdrPngBitDepth = std::clamp<unsigned int>(a_json["HdrPngBitDepth"], 7u, 16u);
	if (a_json.contains("SdrUsePng"))
		sdrUsePng = a_json["SdrUsePng"];
	if (a_json.contains("CopyToClipboard"))
		copyToClipboard = a_json["CopyToClipboard"];

	if (a_json.contains("PhotoFinishEnabled"))
		photoFinishEnabled = a_json["PhotoFinishEnabled"];

	if (a_json.contains("PhotoFinishQualityPreset"))
		photoFinishQualityPreset =
			std::clamp<unsigned int>(
				a_json["PhotoFinishQualityPreset"],
				0u,
				3u);

	if (a_json.contains("PhotoFinishScale")) {
		const unsigned int requested =
			a_json["PhotoFinishScale"];
		photoFinishScale =
			requested >= 4u ? 4u :
			requested >= 2u ? 2u : 1u;
	}

	if (a_json.contains("PhotoFinishTemporalSamples")) {
		const unsigned int requested =
			a_json["PhotoFinishTemporalSamples"];
		photoFinishTemporalSamples =
			requested >= 24u ? 24u :
			requested >= 16u ? 16u :
			requested >= 8u ? 8u :
			requested >= 4u ? 4u : 1u;
	}

	if (a_json.contains("PhotoFinishDetailStrength"))
		photoFinishDetailStrength =
			std::clamp<float>(
				a_json["PhotoFinishDetailStrength"],
				0.0f,
				1.0f);
	if (a_json.contains("PhotoFinishNeuralEnabled"))
		photoFinishNeuralEnabled = a_json["PhotoFinishNeuralEnabled"];
	if (a_json.contains("PhotoFinishRenderScaleMode"))
		photoFinishRenderScaleMode =
			std::clamp<unsigned int>(a_json["PhotoFinishRenderScaleMode"], 0u, 2u);
	if (a_json.contains("PhotoFinishLightingWarmupFrames")) {
		const unsigned int requested = a_json["PhotoFinishLightingWarmupFrames"];
		photoFinishLightingWarmupFrames =
			requested >= 16u ? 16u : requested >= 8u ? 8u : requested >= 4u ? 4u : 0u;
	}
	if (a_json.contains("PhotoFinishNeuralFeedbackSteps"))
		photoFinishNeuralFeedbackSteps =
			std::clamp<unsigned int>(a_json["PhotoFinishNeuralFeedbackSteps"], 1u, 3u);

	// Keep the legacy fields readable, but never reactivate the experimental
	// depth resolve from an older configuration.
	photoLensDofEnabled = false;
	if (a_json.contains("PhotoLensDofQuality"))
		photoLensDofQuality =
			std::clamp<unsigned int>(
				a_json["PhotoLensDofQuality"],
				0u,
				3u);
	if (a_json.contains("PhotoLensDofStrength"))
		photoLensDofStrength =
			std::clamp<float>(
				a_json["PhotoLensDofStrength"],
				0.0f,
				1.0f);
	if (a_json.contains("PhotoLensDofApertureBlades"))
		photoLensDofApertureBlades =
			std::clamp<unsigned int>(
				a_json["PhotoLensDofApertureBlades"],
				5u,
				11u);
	if (a_json.contains("PhotoLensDofHighlightBoost"))
		photoLensDofHighlightBoost =
			std::clamp<float>(
				a_json["PhotoLensDofHighlightBoost"],
				0.0f,
				0.5f);

	if (a_json.contains("PhotoFinishMotionEnabled"))
		photoFinishMotionEnabled =
			a_json["PhotoFinishMotionEnabled"];

	if (a_json.contains("PhotoFinishMotionStrength"))
		photoFinishMotionStrength =
			std::clamp<float>(
				a_json["PhotoFinishMotionStrength"],
				0.0f,
				1.0f);

	if (a_json.contains("PhotoFinishMotionAngleDegrees"))
		photoFinishMotionAngleDegrees =
			std::clamp<float>(
				a_json["PhotoFinishMotionAngleDegrees"],
				-180.0f,
				180.0f);

	if (auto it = a_json.find("DirectorPhotoPresets");
		it != a_json.end() && it->is_array()) {
		const std::size_t count = std::min(it->size(), directorPhotoPresets.size());
		for (std::size_t index = 0; index < count; ++index) {
			const auto& source = (*it)[index];
			if (!source.is_object())
				continue;
			auto& preset = directorPhotoPresets[index];
			preset.valid = source.value("Valid", false);
			preset.lookPreset = std::clamp(source.value("LookPreset", 0u), 0u, 5u);
			preset.lookOpacity = std::clamp(source.value("LookOpacity", 0.35f), 0.0f, 1.0f);
			preset.exposure = std::clamp(source.value("Exposure", 0.0f), -2.0f, 2.0f);
			preset.contrast = std::clamp(source.value("Contrast", 1.0f), 0.75f, 1.25f);
			preset.saturation = std::clamp(source.value("Saturation", 1.0f), 0.75f, 1.25f);
			preset.highlightProtection = std::clamp(source.value("HighlightProtection", 0.0f), 0.0f, 1.0f);
			preset.shadowDetail = std::clamp(source.value("ShadowDetail", 0.0f), 0.0f, 0.4f);
			preset.bloomEnabled = source.value("BloomEnabled", false);
			preset.bloomStrength = std::clamp(source.value("BloomStrength", 0.0f), 0.0f, 2.0f);
			preset.fieldOfView = std::clamp(source.value("FieldOfView", 75.0f), 20.0f, 110.0f);
			preset.motionEnabled = source.value("MotionEnabled", false);
			preset.motionStrength = std::clamp(source.value("MotionStrength", 0.18f), 0.0f, 1.0f);
			preset.motionAngleDegrees = std::clamp(source.value("MotionAngleDegrees", 0.0f), -180.0f, 180.0f);
			preset.neuralPhotoEnabled = source.value("NeuralPhotoEnabled", false);
		}
	}

	subrect.LoadSettings(a_json);
}

void PixelCapture::SaveSettings(json& a_json)
{
	a_json["ScreenshotPath"] = screenshotPath;
	a_json["ApplyCropToScreenshot"] = applyCropToScreenshot;
	a_json["HdrPngBitDepth"] = hdrPngBitDepth;
	a_json["SdrUsePng"] = sdrUsePng;
	a_json["CopyToClipboard"] = copyToClipboard;
	a_json["PhotoFinishEnabled"] = photoFinishEnabled;
	a_json["PhotoFinishQualityPreset"] =
		photoFinishQualityPreset;
	a_json["PhotoFinishScale"] = photoFinishScale;
	a_json["PhotoFinishTemporalSamples"] =
		photoFinishTemporalSamples;
	a_json["PhotoFinishDetailStrength"] =
		photoFinishDetailStrength;
	a_json["PhotoFinishNeuralEnabled"] = photoFinishNeuralEnabled;
	a_json["PhotoFinishRenderScaleMode"] = photoFinishRenderScaleMode;
	a_json["PhotoFinishLightingWarmupFrames"] = photoFinishLightingWarmupFrames;
	a_json["PhotoFinishNeuralFeedbackSteps"] = photoFinishNeuralFeedbackSteps;
	a_json["PhotoLensDofEnabled"] =
		photoLensDofEnabled;
	a_json["PhotoLensDofQuality"] =
		photoLensDofQuality;
	a_json["PhotoLensDofStrength"] =
		photoLensDofStrength;
	a_json["PhotoLensDofApertureBlades"] =
		photoLensDofApertureBlades;
	a_json["PhotoLensDofHighlightBoost"] =
		photoLensDofHighlightBoost;
	a_json["PhotoFinishMotionEnabled"] =
		photoFinishMotionEnabled;
	a_json["PhotoFinishMotionStrength"] =
		photoFinishMotionStrength;
	a_json["PhotoFinishMotionAngleDegrees"] =
		photoFinishMotionAngleDegrees;
	a_json["DirectorPhotoPresets"] = json::array();
	for (const auto& preset : directorPhotoPresets) {
		a_json["DirectorPhotoPresets"].push_back({
			{ "Valid", preset.valid },
			{ "LookPreset", preset.lookPreset },
			{ "LookOpacity", preset.lookOpacity },
			{ "Exposure", preset.exposure },
			{ "Contrast", preset.contrast },
			{ "Saturation", preset.saturation },
			{ "HighlightProtection", preset.highlightProtection },
			{ "ShadowDetail", preset.shadowDetail },
			{ "BloomEnabled", preset.bloomEnabled },
			{ "BloomStrength", preset.bloomStrength },
			{ "FieldOfView", preset.fieldOfView },
			{ "MotionEnabled", preset.motionEnabled },
			{ "MotionStrength", preset.motionStrength },
			{ "MotionAngleDegrees", preset.motionAngleDegrees },
			{ "NeuralPhotoEnabled", preset.neuralPhotoEnabled }
		});
	}
	subrect.SaveSettings(a_json);
}

void PixelCapture::DrawSettings()
{
	ImGui::TextWrapped("%s", T(TKEY("async_note"), "Capture and save run asynchronously without stalling the game."));

	const bool hdrCaptureAvailable = globals::pipeline::cameraSuite.loaded &&
	                                 globals::pipeline::cameraSuite.settings.enableHDR;

	if (hdrCaptureAvailable) {
		ImGui::TextWrapped("%s",
			T(TKEY("hdr_note"),
				"HDR enabled: saves the displayed frame as PNG with HDR10 metadata (48 bpp RGB, cICP/cLLi). "
				"Use an HDR-aware viewer such as Windows Photos (HDR on) or Special K SKIF."));
		ImGui::SliderInt(
			T(TKEY("hdr_bit_depth"), "HDR PNG bit depth"),
			reinterpret_cast<int*>(&hdrPngBitDepth),
			7,
			16,
			"%d-bit",
			ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(
				"%s", T(TKEY("hdr_bit_depth_tooltip"),
						  "Quantization for the 48 bpp RGB PNG payload. 11-bit is a good default; "
						  "higher values increase file size with diminishing returns."));

	} else {
		ImGui::TextWrapped("%s",
			T(TKEY("sdr_note"),
				"Enable Camera Suite to capture HDR PNG screenshots with HDR10 metadata. "
				"SDR captures use the lossless format selected below."));
	}

	if (ImGui::Button(T(TKEY("take_screenshot"), "Take Screenshot Now"))) {
		captureRequested = true;
	}
	ImGui::SameLine();
	ImGui::Checkbox(T(TKEY("apply_crop"), "Apply crop"), &applyCropToScreenshot);

	ImGui::SeparatorText(T(TKEY("output"), "Output"));

	ImGui::Checkbox("Copy saved file to clipboard", &copyToClipboard);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Places the saved screenshot on the clipboard as a file (paste in Explorer or attach in chat apps).");

	if (!hdrCaptureAvailable) {
		int sdrFormat = sdrUsePng ? 1 : 0;
		ImGui::RadioButton("BMP (lossless)", &sdrFormat, 0);
		ImGui::SameLine();
		ImGui::RadioButton("PNG (lossless)", &sdrFormat, 1);
		sdrUsePng = sdrFormat != 0;
	}

	char buf[260];
	strncpy_s(buf, sizeof(buf), screenshotPath.c_str(), _TRUNCATE);
	ImGui::PushItemWidth(-FLT_MIN - 120.0f);
	if (ImGui::InputText("##ScreenshotFolder", buf, sizeof(buf))) {
		screenshotPath = buf;
	}
	ImGui::PopItemWidth();
	ImGui::SameLine();
	const bool canOpen = !screenshotPath.empty();
	ImGui::BeginDisabled(!canOpen);
	if (ImGui::Button(T(TKEY("open"), "Open"))) {
		std::error_code ec;
		std::filesystem::create_directories(screenshotPath, ec);
		ShellExecuteA(nullptr, "open", screenshotPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::Text("%s", T(TKEY("folder"), "Folder"));
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("folder_tooltip"),
							  "Relative paths resolve against the Skyrim install dir.\n"
							  "Absolute paths (e.g. D:\\Captures) save there directly."));
	}

	auto& menuSettings = Menu::GetSingleton()->GetSettings();
	Util::InputComboWidget(
		T(TKEY("hotkey"), "Hotkey"),
		menuSettings.ScreenshotKey,
		Menu::GetSingleton()->settingScreenshotKey,
		"Change##PixelCapture");

	if (HotkeyCollidesWithVanilla()) {
		Util::Text::WrappedWarning(
			T(TKEY("hotkey_collision"),
				"This hotkey collides with vanilla PrintScreen; both saves will fire. "
				"Set bAllowScreenShot=0 in Skyrim.ini to suppress vanilla, or pick a different hotkey above."));
	}

	ImGui::SeparatorText(T(TKEY("crop"), "Crop"));

	// Preview reflects what Capture() would save.
	winrt::com_ptr<ID3D11Texture2D> previewTextureKeepAlive;
	const auto src = SelectCaptureSource(previewTextureKeepAlive, /*forCapture=*/false);

	ID3D11ShaderResourceView* previewView = src.srv;
	if (src.texture && (src.needsPreviewCache || !previewView)) {
		EnsurePreviewCache(src.texture);
		if (previewCacheSRV && previewCacheTexture) {
			globals::d3d::context->CopySubresourceRegion(
				previewCacheTexture.get(), 0, 0, 0, 0, src.texture, 0, nullptr);
			previewView = previewCacheSRV.get();
		}
	}

	subrect.DrawEditor(previewView, src.texture, 1.0f, 0.0f, OpaquePreviewBlendCallback);
}

void PixelCapture::EnsurePreviewCache(ID3D11Texture2D* sourceTexture)
{
	if (!sourceTexture) {
		return;
	}
	D3D11_TEXTURE2D_DESC srcDesc{};
	sourceTexture->GetDesc(&srcDesc);

	// Reuse the cache when the source dimensions/format haven't changed.
	if (previewCacheTexture) {
		D3D11_TEXTURE2D_DESC cacheDesc{};
		previewCacheTexture->GetDesc(&cacheDesc);
		if (cacheDesc.Width == srcDesc.Width &&
			cacheDesc.Height == srcDesc.Height &&
			cacheDesc.Format == srcDesc.Format) {
			return;
		}
		previewCacheSRV = nullptr;
		previewCacheTexture = nullptr;
	}

	// SRV-readable copy. Match source format for CopySubresourceRegion compatibility.
	D3D11_TEXTURE2D_DESC cacheDesc = srcDesc;
	cacheDesc.MipLevels = 1;
	cacheDesc.ArraySize = 1;
	cacheDesc.SampleDesc.Count = 1;
	cacheDesc.SampleDesc.Quality = 0;
	cacheDesc.Usage = D3D11_USAGE_DEFAULT;
	cacheDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	cacheDesc.CPUAccessFlags = 0;
	cacheDesc.MiscFlags = 0;

	if (FAILED(globals::d3d::device->CreateTexture2D(&cacheDesc, nullptr, previewCacheTexture.put()))) {
		previewCacheTexture = nullptr;
		return;
	}
	if (FAILED(globals::d3d::device->CreateShaderResourceView(
			previewCacheTexture.get(), nullptr, previewCacheSRV.put()))) {
		previewCacheSRV = nullptr;
		previewCacheTexture = nullptr;
	}
}

void PixelCapture::Reset()
{
}

void PixelCapture::ProcessCaptureRequest()
{
	std::optional<CustomCaptureRequest> customRequest;
	{
		std::lock_guard lock(captureRequestMutex);
		customRequest.swap(customCaptureRequest);
	}

	if (customRequest.has_value()) {
		CaptureImpl(
			customRequest->outputPath,
			customRequest->notify);
	}

	if (photoFinishRequested.exchange(
			false,
			std::memory_order_acq_rel) &&
		!photoFinishBurst.has_value()) {
		StartPhotoFinishCapture();
	}

	if (directorCaptureRequested.exchange(
			false,
			std::memory_order_acq_rel) &&
		!photoFinishBurst.has_value()) {
		Capture();
		return;
	}

	// Photo Finish samples exactly one presented frame per call so temporal
	// accumulation represents consecutive renderer output rather than duplicate
	// copies of one backbuffer.
	if (photoFinishBurst.has_value()) {
		CapturePhotoFinishSample();
		return;
	}

	if (captureRequested.exchange(false)) {
		// Normal screenshot requests are ignored while Director owns capture.
		auto* menu =
			Menu::GetSingleton();

		if (!menu ||
			!menu->ShouldLockGameInputForDirector()) {
			Capture();
		}
	}
}

void PixelCapture::RequestCaptureToPath(std::filesystem::path outputPath, bool notify)
{
	std::lock_guard lock(captureRequestMutex);
	customCaptureRequest = CustomCaptureRequest{ std::move(outputPath), notify };
}

std::uint32_t PixelCapture::GetPendingCaptureCount() const
{
	return pendingCaptureCount.load(std::memory_order_acquire);
}

void PixelCapture::RequestPhotoFinishCapture()
{
	// The photo renderer is transactional: never queue another capture while a
	// request, GPU sampling burst, CPU resolve, encode, or save is in flight.
	if (IsPhotoFinishBusy())
		return;

	if (!photoFinishEnabled) {
		directorCaptureRequested.store(
			true,
			std::memory_order_release);
		return;
	}

	// Collapse rapid button presses into one expensive reconstruction job.
	if (!photoFinishBurst.has_value()) {
		photoFinishRequested.store(
			true,
			std::memory_order_release);
	}
}

bool PixelCapture::IsPhotoFinishSampling() const
{
	return
		photoFinishSamplesTarget.load(
			std::memory_order_acquire) >
		0u;
}

bool PixelCapture::IsPhotoFinishBusy() const
{
	return
		captureRequested.load(std::memory_order_acquire) ||
		photoFinishRequested.load(std::memory_order_acquire) ||
		directorCaptureRequested.load(std::memory_order_acquire) ||
		photoFinishBurst.has_value() ||
		pendingCaptureCount.load(std::memory_order_acquire) > 0u ||
		GetPhotoFinishStage() != PhotoFinishStage::Idle;
}

std::uint32_t PixelCapture::GetPhotoFinishSamplesCaptured() const
{
	return
		photoFinishSamplesCaptured.load(
			std::memory_order_acquire);
}

std::uint32_t PixelCapture::GetPhotoFinishSamplesTarget() const
{
	return
		photoFinishSamplesTarget.load(
			std::memory_order_acquire);
}

PixelCapture::PhotoFinishStage PixelCapture::GetPhotoFinishStage() const
{
	return static_cast<PhotoFinishStage>(
		photoFinishStage.load(
			std::memory_order_acquire));
}

float PixelCapture::GetPhotoFinishProgress() const
{
	return std::clamp(
		photoFinishProgress.load(
			std::memory_order_acquire),
		0.0f,
		1.0f);
}

const char* PixelCapture::GetPhotoFinishStageLabel() const
{
	switch (GetPhotoFinishStage()) {
	case PhotoFinishStage::ConvergingLighting:
		return "CONVERGING LIGHTING + POST EFFECTS";
	case PhotoFinishStage::Accumulating:
		return "ACCUMULATING FINAL TEMPORAL DETAIL";
	case PhotoFinishStage::Resolving:
		return "RESOLVING MULTI-FRAME IMAGE";
	case PhotoFinishStage::LensDepthOfField:
		return "PHOTO LENS / DEPTH-AWARE BOKEH";
	case PhotoFinishStage::MotionFinish:
		return "PHOTOGRAPHIC SHUTTER FINISH";
	case PhotoFinishStage::DetailRecovery:
		return "RECOVERING MICRO DETAIL";
	case PhotoFinishStage::Upscaling:
		return "HIGH-RES RECONSTRUCTION";
	case PhotoFinishStage::Saving:
		return "ENCODING FINAL PHOTO";
	default:
		return "";
	}
}

bool PixelCapture::IsPhotoFinishProcessing() const
{
	const auto stage =
		GetPhotoFinishStage();
	return stage != PhotoFinishStage::Idle &&
		stage != PhotoFinishStage::ConvergingLighting &&
		stage != PhotoFinishStage::Accumulating;
}

void PixelCapture::SetPhotoFinishStage(
	PhotoFinishStage stage,
	float progress)
{
	photoFinishProgress.store(
		std::clamp(progress, 0.0f, 1.0f),
		std::memory_order_release);
	photoFinishStage.store(
		static_cast<std::uint32_t>(stage),
		std::memory_order_release);
}

void PixelCapture::StartPhotoFinishCapture()
{
	auto device =
		globals::d3d::device;
	auto context =
		globals::d3d::context;

	if (!device ||
		!context)
		return;

	winrt::com_ptr<ID3D11Texture2D>
		sourceTextureKeepAlive;

	auto& reconstruction = globals::pipeline::imageReconstruction;
	const bool neuralSourceAvailable = reconstruction.CanUsePhotoNeuralRendering() &&
		reconstruction.dx12SwapChain.GetProvisionedNeuralOutput();
	const bool useNeuralSource = photoFinishNeuralEnabled && neuralSourceAvailable;
	if (photoFinishNeuralEnabled && !useNeuralSource) {
		logger::warn(
			"Photo Finish Neural Reconstruction was requested but the PIXL DLSS sidecar/model is unavailable; using the universal Photo Finish path.");
	}

	const auto src =
		SelectPhotoFinishSource(
			sourceTextureKeepAlive,
			useNeuralSource);

	if (!src.texture) {
		logger::error(
			"Photo Finish failed to acquire capture source ({}).",
			src.description);
		return;
	}

	D3D11_TEXTURE2D_DESC
		srcDesc{};

	src.texture->GetDesc(
		&srcDesc);

	PhotoFinishBurst burst;
	burst.useNeuralSource = useNeuralSource;
	burst.warmupFramesTotal =
		photoFinishLightingWarmupFrames >= 16u ? 16u :
		photoFinishLightingWarmupFrames >= 8u ? 8u :
		photoFinishLightingWarmupFrames >= 4u ? 4u : 0u;
	burst.warmupFramesRemaining = burst.warmupFramesTotal;
	if (useNeuralSource && burst.warmupFramesTotal < 4u) {
		burst.warmupFramesTotal = 4u;
		burst.warmupFramesRemaining = 4u;
	}
	burst.neuralFeedbackSteps = useNeuralSource
		? std::clamp(photoFinishNeuralFeedbackSteps, 1u, 3u)
		: 1u;
	burst.minimumRenderScale = photoFinishRenderScaleMode >= 2u
		? 1.0f
		: photoFinishRenderScaleMode >= 1u
			? 0.85f
			: 0.0f;
	if (useNeuralSource)
		burst.minimumRenderScale = 1.0f;
	burst.lastNeuralFrameSerial =
		reconstruction.dx12SwapChain.GetCompletedNeuralFrameSerial();
	burst.format =
		srcDesc.Format;
	burst.sourceWidth =
		srcDesc.Width;
	burst.sourceHeight =
		srcDesc.Height;
	burst.copyWidth =
		srcDesc.Width;
	burst.copyHeight =
		srcDesc.Height;

	if (applyCropToScreenshot) {
		const auto region =
			subrect.GetPixelRegion(
				srcDesc.Width,
				srcDesc.Height);

		burst.copyX =
			region.x;
		burst.copyY =
			region.y;
		burst.copyWidth =
			region.w;
		burst.copyHeight =
			region.h;
	}

	unsigned int requestedSamples =
		photoFinishTemporalSamples >= 24u
			? 24u
			: photoFinishTemporalSamples >= 16u
				? 16u
				: photoFinishTemporalSamples >= 8u
					? 8u
					: photoFinishTemporalSamples >= 4u
						? 4u
						: 1u;

	// Feature 18 is temporally reconstructed. Additional quality tiers mean more
	// independent, correctly guided DLAA+NR frames—not recursive re-evaluation of
	// the previous neural image, which the model turns into cumulative blur.
	const unsigned int neuralConvergenceFloor = useNeuralSource
		? 8u * std::clamp(burst.neuralFeedbackSteps, 1u, 3u)
		: 1u;
	if (useNeuralSource && requestedSamples < neuralConvergenceFloor) {
		logger::info(
			"Photo Finish neural convergence raised from {} to {} fresh guided frame(s).",
			requestedSamples,
			neuralConvergenceFloor);
		requestedSamples = neuralConvergenceFloor;
	}

	const std::uint64_t bytesPerPixel =
		srcDesc.Format ==
			DXGI_FORMAT_R16G16B16A16_FLOAT
			? 8ull
			: 4ull;

	const std::uint64_t bytesPerSample =
		std::max<std::uint64_t>(
			1ull,
			static_cast<std::uint64_t>(
				burst.copyWidth) *
			static_cast<std::uint64_t>(
				burst.copyHeight) *
			bytesPerPixel);

	// Photo capture is allowed to be expensive, but do not let a high-res 24
	// sample burst allocate unbounded staging memory inside Skyrim.
	constexpr std::uint64_t kPhotoFinishSampleBudget =
		320ull * 1024ull * 1024ull;

	const unsigned int memorySafeSamples =
		static_cast<unsigned int>(
			std::clamp<std::uint64_t>(
				kPhotoFinishSampleBudget /
					bytesPerSample,
				1ull,
				24ull));

	burst.targetSamples =
		std::max(
			1u,
			std::min(
				requestedSamples,
				memorySafeSamples));

	if (burst.targetSamples !=
		requestedSamples) {
		logger::warn(
			"Photo Finish accumulation reduced from {} to {} samples for capture-memory safety.",
			requestedSamples,
			burst.targetSamples);
	}

	burst.outputScale =
		ClampPhotoFinishScaleForSize(
			photoFinishScale,
			burst.copyWidth,
			burst.copyHeight);

	if (burst.outputScale !=
		photoFinishScale) {
		logger::warn(
			"Photo Finish {}x output reduced to {}x for {}x{} capture memory safety.",
			photoFinishScale,
			burst.outputScale,
			burst.copyWidth,
			burst.copyHeight);
	}

	burst.detailStrength =
		std::clamp(
			photoFinishDetailStrength,
			0.0f,
			1.0f);

	burst.photoLensDofEnabled = false;
	burst.photoLensDofQuality =
		std::min(
			photoLensDofQuality,
			3u);
	burst.photoLensDofStrength =
		std::clamp(
			photoLensDofStrength,
			0.0f,
			1.0f);
	burst.photoLensDofApertureBlades =
		std::clamp(
			photoLensDofApertureBlades,
			5u,
			11u);
	burst.photoLensDofHighlightBoost =
		std::clamp(
			photoLensDofHighlightBoost,
			0.0f,
			0.5f);

	{
		auto& cameraSuite = globals::pipeline::cameraSuite;
		std::lock_guard<std::mutex> lock(cameraSuite.settingsMutex);
		burst.photoLensDofFocusDistance = std::clamp(cameraSuite.settings.dofFocusDistance, 25.0f, 20000.0f);
		burst.photoLensDofFocusRange = std::clamp(cameraSuite.settings.dofFocusRange, 100.0f, 20000.0f);
		burst.photoLensDofEdgeProtection = std::clamp(cameraSuite.settings.dofFocusEdgeProtection, 0.0f, 2.0f);
		burst.photoLensDofForegroundCoverage = std::clamp(cameraSuite.settings.dofForegroundCoverage, 0.0f, 1.5f);
		burst.photoLensDofCatEye = std::clamp(cameraSuite.settings.dofCatEye, 0.0f, 1.0f);
		burst.photoLensDofAnamorphicRatio = std::clamp(cameraSuite.settings.dofAnamorphicRatio, 0.5f, 2.0f);
	}

	burst.motionEnabled =
		photoFinishMotionEnabled;

	burst.motionStrength =
		std::clamp(
			photoFinishMotionStrength,
			0.0f,
			1.0f);

	burst.motionAngleDegrees =
		std::clamp(
			photoFinishMotionAngleDegrees,
			-180.0f,
			180.0f);

	const bool flatHdrCapture =
		IsFlatHdrScreenshotCapture();

	if (flatHdrCapture &&
		!IsHdrCaptureFormat(
			srcDesc.Format)) {
		logger::error(
			"Unsupported HDR Photo Finish format: {}",
			static_cast<uint32_t>(
				srcDesc.Format));
		return;
	}

	burst.saveAsHdrPng =
		flatHdrCapture &&
		IsHdrCaptureFormat(
			srcDesc.Format);

	burst.saveAsSdrPng =
		!burst.saveAsHdrPng &&
		sdrUsePng;

	burst.hdrPngBitDepth =
		static_cast<int>(
			hdrPngBitDepth);

	burst.outputPath =
		BuildScreenshotPath(
			screenshotPath,
			burst.saveAsHdrPng ||
				burst.saveAsSdrPng);

	burst.copyToClipboard =
		copyToClipboard;
	burst.notify =
		true;

	burst.samples.reserve(
		burst.targetSamples);
	burst.jitterOffsets.reserve(
		burst.targetSamples);

	auto& photoReconstruction = globals::pipeline::imageReconstruction;
	photoReconstruction.BeginPhotoCaptureRenderOverride(burst.minimumRenderScale, burst.useNeuralSource);
	if (burst.warmupFramesRemaining == 0u) {
		photoReconstruction.BeginPhotoCaptureJitter(burst.targetSamples);
		photoReconstruction.SetPhotoCaptureJitterSample(0u);
	}

	photoFinishSamplesCaptured.store(
		0u,
		std::memory_order_release);

	photoFinishSamplesTarget.store(
		burst.targetSamples,
		std::memory_order_release);

	photoFinishBurst =
		std::move(
			burst);

	SetPhotoFinishStage(
		photoFinishBurst->warmupFramesRemaining > 0u
			? PhotoFinishStage::ConvergingLighting
			: PhotoFinishStage::Accumulating,
		0.02f);

	logger::info(
		"Photo Finish transaction started: {} lighting/post warmup frame(s), {} actual sample(s), {:.0f}% minimum internal render scale, {}x final output, neural convergence tier {}, detail {:.2f}, motion {}, projection jitter {}, neural source {}.",
		photoFinishBurst->warmupFramesTotal,
		photoFinishSamplesTarget.load(
			std::memory_order_acquire),
		photoFinishBurst->minimumRenderScale > 0.0f
			? photoFinishBurst->minimumRenderScale * 100.0f
			: reconstruction.resolutionScale.x * 100.0f,
		photoFinishBurst->
			outputScale,
		photoFinishBurst->neuralFeedbackSteps,
		photoFinishBurst->
			detailStrength,
		photoFinishBurst->
			motionEnabled
				? "on"
				: "off",
		photoReconstruction.IsPhotoCaptureJitterActive() ? "on" : "off",
		photoFinishBurst->useNeuralSource ? "on" : "off");
}

void PixelCapture::CapturePhotoFinishSample()
{
	if (!photoFinishBurst.has_value())
		return;

	auto device =
		globals::d3d::device;
	auto context =
		globals::d3d::context;

	if (!device ||
		!context) {
		globals::pipeline::imageReconstruction.EndPhotoCaptureJitter();
		globals::pipeline::imageReconstruction.EndPhotoCaptureRenderOverride();
		photoFinishBurst.reset();
		photoFinishSamplesTarget =
			0u;
		SetPhotoFinishStage(
			PhotoFinishStage::Idle,
			0.0f);
		return;
	}

	auto& burst =
		*photoFinishBurst;

	if (burst.warmupFramesRemaining > 0u) {
		--burst.warmupFramesRemaining;
		const auto completedWarmup =
			burst.warmupFramesTotal - burst.warmupFramesRemaining;
		SetPhotoFinishStage(
			PhotoFinishStage::ConvergingLighting,
			0.02f + 0.10f *
				(static_cast<float>(completedWarmup) /
				 static_cast<float>(std::max(burst.warmupFramesTotal, 1u))));

		if (burst.warmupFramesRemaining == 0u) {
			auto& photoReconstruction = globals::pipeline::imageReconstruction;
			photoReconstruction.BeginPhotoCaptureJitter(burst.targetSamples);
			photoReconstruction.SetPhotoCaptureJitterSample(0u);
			burst.awaitingFirstJitteredFrame = true;
			SetPhotoFinishStage(PhotoFinishStage::Accumulating, 0.12f);
		}
		return;
	}

	if (burst.awaitingFirstJitteredFrame) {
		burst.awaitingFirstJitteredFrame = false;
		// The override was armed after the frame that triggered this request.
		// Wait for Main_UpdateJitter to render sample 0 with the deterministic
		// Director Halton offset before copying any color/depth data.
		if (burst.useNeuralSource) {
			burst.lastNeuralFrameSerial =
				globals::pipeline::imageReconstruction.dx12SwapChain.GetCompletedNeuralFrameSerial();
			burst.awaitingNeuralFrame = true;
		}
		return;
	}

	if (burst.useNeuralSource) {
		auto& swapChain = globals::pipeline::imageReconstruction.dx12SwapChain;
		const auto serial = swapChain.GetCompletedNeuralFrameSerial();
		if (serial <= burst.lastNeuralFrameSerial || !swapChain.GetCompletedNeuralOutput()) {
			if (++burst.neuralWaitFrames < 180u)
				return;
			logger::warn(
				"Photo Finish timed out waiting for a fresh neural frame; continuing with the universal capture source.");
			burst.useNeuralSource = false;
			burst.awaitingNeuralFrame = false;
		} else if (burst.awaitingNeuralFrame) {
			// This published frame was rendered before the next deterministic jitter
			// was armed. Use it only as a synchronization marker; the following
			// neural frame contains the requested projection phase.
			burst.lastNeuralFrameSerial = serial;
			burst.neuralWaitFrames = 0;
			burst.awaitingNeuralFrame = false;
			return;
		} else {
			burst.lastNeuralFrameSerial = serial;
			burst.neuralWaitFrames = 0;
		}
	}

	winrt::com_ptr<ID3D11Texture2D>
		sourceTextureKeepAlive;

	const auto src =
		SelectPhotoFinishSource(
			sourceTextureKeepAlive,
			burst.useNeuralSource);

	if (!src.texture) {
		logger::error(
			"Photo Finish sample failed to acquire capture source.");
		globals::pipeline::imageReconstruction.EndPhotoCaptureJitter();
		globals::pipeline::imageReconstruction.EndPhotoCaptureRenderOverride();
		photoFinishBurst.reset();
		photoFinishSamplesTarget =
			0u;
		SetPhotoFinishStage(
			PhotoFinishStage::Idle,
			0.0f);
		return;
	}

	D3D11_TEXTURE2D_DESC
		srcDesc{};

	src.texture->GetDesc(
		&srcDesc);

	if (srcDesc.Width !=
			burst.sourceWidth ||
		srcDesc.Height !=
			burst.sourceHeight ||
		srcDesc.Format !=
			burst.format) {
		logger::error(
			"Photo Finish source changed during temporal sampling; capture cancelled.");
		globals::pipeline::imageReconstruction.EndPhotoCaptureJitter();
		globals::pipeline::imageReconstruction.EndPhotoCaptureRenderOverride();
		photoFinishBurst.reset();
		photoFinishSamplesTarget =
			0u;
		SetPhotoFinishStage(
			PhotoFinishStage::Idle,
			0.0f);
		return;
	}

	D3D11_TEXTURE2D_DESC
		stagingDesc =
			srcDesc;

	stagingDesc.Width =
		burst.copyWidth;
	stagingDesc.Height =
		burst.copyHeight;
	stagingDesc.MipLevels = 1;
	stagingDesc.ArraySize = 1;
	stagingDesc.SampleDesc.Count = 1;
	stagingDesc.SampleDesc.Quality = 0;
	stagingDesc.Usage =
		D3D11_USAGE_STAGING;
	stagingDesc.BindFlags = 0;
	stagingDesc.CPUAccessFlags =
		D3D11_CPU_ACCESS_READ;
	stagingDesc.MiscFlags = 0;

	winrt::com_ptr<ID3D11Texture2D>
		stagingTexture;

	if (FAILED(
			device->CreateTexture2D(
				&stagingDesc,
				nullptr,
				stagingTexture.put()))) {
		logger::error(
			"Photo Finish failed to create staging sample.");
		globals::pipeline::imageReconstruction.EndPhotoCaptureJitter();
		globals::pipeline::imageReconstruction.EndPhotoCaptureRenderOverride();
		photoFinishBurst.reset();
		photoFinishSamplesTarget =
			0u;
		SetPhotoFinishStage(
			PhotoFinishStage::Idle,
			0.0f);
		return;
	}

	D3D11_BOX sourceRegion{};
	sourceRegion.left =
		burst.copyX;
	sourceRegion.top =
		burst.copyY;
	sourceRegion.front = 0;
	sourceRegion.right =
		burst.copyX +
		burst.copyWidth;
	sourceRegion.bottom =
		burst.copyY +
		burst.copyHeight;
	sourceRegion.back = 1;

	context->CopySubresourceRegion(
		stagingTexture.get(),
		0,
		0,
		0,
		0,
		src.texture,
		0,
		&sourceRegion);


	if (burst.samples.empty() &&
		burst.photoLensDofEnabled) {
		auto& hybridGI =
			globals::pipeline::hybridGI;

		if (hybridGI.loaded &&
			hybridGI.texWorkingDepth &&
			hybridGI.texWorkingDepth->resource) {
			winrt::com_ptr<ID3D11Texture2D>
				depthTexture;

			if (SUCCEEDED(
					hybridGI.texWorkingDepth->resource->
						QueryInterface(
							depthTexture.put())) &&
				depthTexture) {
				D3D11_TEXTURE2D_DESC depthDesc{};
				depthTexture->GetDesc(&depthDesc);

				if (burst.copyX + burst.copyWidth <= depthDesc.Width &&
					burst.copyY + burst.copyHeight <= depthDesc.Height) {
					D3D11_TEXTURE2D_DESC depthStagingDesc =
						depthDesc;
					depthStagingDesc.Width = burst.copyWidth;
					depthStagingDesc.Height = burst.copyHeight;
					depthStagingDesc.MipLevels = 1;
					depthStagingDesc.ArraySize = 1;
					depthStagingDesc.SampleDesc.Count = 1;
					depthStagingDesc.SampleDesc.Quality = 0;
					depthStagingDesc.Usage = D3D11_USAGE_STAGING;
					depthStagingDesc.BindFlags = 0;
					depthStagingDesc.CPUAccessFlags =
						D3D11_CPU_ACCESS_READ;
					depthStagingDesc.MiscFlags = 0;

					winrt::com_ptr<ID3D11Texture2D>
						depthStaging;
					if (SUCCEEDED(
							device->CreateTexture2D(
								&depthStagingDesc,
								nullptr,
								depthStaging.put()))) {
						D3D11_BOX depthRegion{};
						depthRegion.left = burst.copyX;
						depthRegion.top = burst.copyY;
						depthRegion.front = 0;
						depthRegion.right =
							burst.copyX + burst.copyWidth;
						depthRegion.bottom =
							burst.copyY + burst.copyHeight;
						depthRegion.back = 1;

						context->CopySubresourceRegion(
							depthStaging.get(),
							0,
							0,
							0,
							0,
							depthTexture.get(),
							0,
							&depthRegion);

						burst.depthStagingTexture =
							std::move(depthStaging);
						burst.depthFormat =
							depthDesc.Format;
					}
				}
			}
		}

		if (!burst.depthStagingTexture) {
			logger::warn(
				"PIXL Lens DOF depth unavailable; final photo will keep preview DOF only.");
		}
	}

	burst.jitterOffsets.push_back(
		globals::pipeline::imageReconstruction.jitter);

	burst.samples.push_back(
		std::move(
			stagingTexture));

	const auto captured =
		static_cast<std::uint32_t>(
			burst.samples.size());

	photoFinishSamplesCaptured.store(
		captured,
		std::memory_order_release);

	SetPhotoFinishStage(
		PhotoFinishStage::Accumulating,
		0.05f +
			0.20f *
				(static_cast<float>(captured) /
				 static_cast<float>(
					 std::max(burst.targetSamples, 1u))));

	if (captured <
		burst.targetSamples) {
		globals::pipeline::imageReconstruction.SetPhotoCaptureJitterSample(captured);
		if (burst.useNeuralSource)
			burst.awaitingNeuralFrame = true;
		return;
	}

	// Sampling is complete; release the projection override before the normal
	// viewport resumes and report how much genuinely distinct sample coverage ran.
	globals::pipeline::imageReconstruction.EndPhotoCaptureJitter();
	globals::pipeline::imageReconstruction.EndPhotoCaptureRenderOverride();
	std::size_t uniqueJitters = 0u;
	std::vector<float2> uniqueOffsets;
	for (const auto& offset : burst.jitterOffsets) {
		const bool seen = std::ranges::any_of(uniqueOffsets, [&offset](const float2& existing) {
			return std::abs(existing.x - offset.x) < 1.0e-5f &&
				std::abs(existing.y - offset.y) < 1.0e-5f;
		});
		if (!seen) uniqueOffsets.push_back(offset);
	}
	uniqueJitters = uniqueOffsets.size();
	logger::info(
		"Photo Finish accumulation complete: {}/{} frames, {} unique projection jitters.",
		captured, burst.targetSamples, uniqueJitters);

	EnsureWorkerThread();

	PendingScreenshot screenshot;
	screenshot.photoFinish =
		true;
	screenshot.photoFinishSamples =
		std::move(
			burst.samples);
	screenshot.photoFinishJitterOffsets =
		std::move(
			burst.jitterOffsets);
	screenshot.photoFinishScale =
		burst.outputScale;
	screenshot.photoFinishDetailStrength =
		burst.detailStrength;
	screenshot.photoDepthStagingTexture =
		std::move(
			burst.depthStagingTexture);
	screenshot.photoDepthFormat =
		burst.depthFormat;
	screenshot.photoLensDofEnabled =
		burst.photoLensDofEnabled;
	screenshot.photoLensDofQuality =
		burst.photoLensDofQuality;
	screenshot.photoLensDofStrength =
		burst.photoLensDofStrength;
	screenshot.photoLensDofApertureBlades =
		burst.photoLensDofApertureBlades;
	screenshot.photoLensDofHighlightBoost =
		burst.photoLensDofHighlightBoost;
	screenshot.photoLensDofFocusDistance = burst.photoLensDofFocusDistance;
	screenshot.photoLensDofFocusRange = burst.photoLensDofFocusRange;
	screenshot.photoLensDofEdgeProtection = burst.photoLensDofEdgeProtection;
	screenshot.photoLensDofForegroundCoverage = burst.photoLensDofForegroundCoverage;
	screenshot.photoLensDofCatEye = burst.photoLensDofCatEye;
	screenshot.photoLensDofAnamorphicRatio = burst.photoLensDofAnamorphicRatio;
	screenshot.photoFinishMotionEnabled =
		burst.motionEnabled;
	screenshot.photoFinishMotionStrength =
		burst.motionStrength;
	screenshot.photoFinishMotionAngleDegrees =
		burst.motionAngleDegrees;
	screenshot.format =
		burst.format;
	screenshot.width =
		burst.copyWidth;
	screenshot.height =
		burst.copyHeight;
	screenshot.saveAsHdrPng =
		burst.saveAsHdrPng;
	screenshot.saveAsSdrPng =
		burst.saveAsSdrPng;
	screenshot.hdrPngBitDepth =
		burst.hdrPngBitDepth;
	screenshot.outputPath =
		burst.outputPath;
	screenshot.copyToClipboard =
		burst.copyToClipboard;
	screenshot.notify =
		burst.notify;

	SetPhotoFinishStage(
		PhotoFinishStage::Resolving,
		0.28f);

	EnqueueScreenshot(
		std::move(
			screenshot));

	photoFinishBurst.reset();

	photoFinishSamplesTarget.store(
		0u,
		std::memory_order_release);
}

void PixelCapture::EnsureWorkerThread()
{
	if (screenshotWorker.joinable()) {
		return;
	}

	screenshotWorkerRunning = true;
	screenshotWorker = std::thread(&PixelCapture::ScreenshotWorkerLoop, this);
}

void PixelCapture::StopWorkerThread()
{
	{
		std::lock_guard<std::mutex> lock(screenshotQueueMutex);
		screenshotWorkerRunning = false;
	}
	screenshotQueueCV.notify_all();

	if (screenshotWorker.joinable()) {
		screenshotWorker.join();
	}
}

void PixelCapture::EnqueueScreenshot(PendingScreenshot&& screenshot)
{
	pendingCaptureCount.fetch_add(1, std::memory_order_release);
	{
		std::lock_guard<std::mutex> lock(screenshotQueueMutex);
		screenshotQueue.push(std::move(screenshot));
	}
	screenshotQueueCV.notify_one();
}

void PixelCapture::ScreenshotWorkerLoop()
{
	CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	auto* context = globals::d3d::context;
	while (true) {
		PendingScreenshot screenshot;
		{
			std::unique_lock<std::mutex> lock(screenshotQueueMutex);
			screenshotQueueCV.wait(lock, [this] {
				return !screenshotQueue.empty() || !screenshotWorkerRunning;
			});

			if (!screenshotWorkerRunning && screenshotQueue.empty()) {
				break;
			}

			screenshot = std::move(screenshotQueue.front());
			screenshotQueue.pop();
		}

		DirectX::ScratchImage image;

		bool imageReady = false;

		if (screenshot.photoFinish) {
			const auto reportProgress =
				[this](
					PhotoFinishStage stage,
					float progress) {
					SetPhotoFinishStage(
						stage,
						progress);
				};

			imageReady =
				BuildPhotoFinishImage(
					context,
					screenshot.photoFinishSamples,
					screenshot.photoFinishJitterOffsets,
					screenshot.photoDepthStagingTexture.get(),
					screenshot.photoDepthFormat,
					screenshot.format,
					screenshot.width,
					screenshot.height,
					screenshot.photoFinishScale,
					screenshot.photoFinishDetailStrength,
					screenshot.photoLensDofEnabled,
					screenshot.photoLensDofQuality,
					screenshot.photoLensDofStrength,
					screenshot.photoLensDofApertureBlades,
					screenshot.photoLensDofHighlightBoost,
					screenshot.photoLensDofFocusDistance,
					screenshot.photoLensDofFocusRange,
					screenshot.photoLensDofEdgeProtection,
					screenshot.photoLensDofForegroundCoverage,
					screenshot.photoLensDofCatEye,
					screenshot.photoLensDofAnamorphicRatio,
					screenshot.photoFinishMotionEnabled,
					screenshot.photoFinishMotionStrength,
					screenshot.photoFinishMotionAngleDegrees,
					reportProgress,
					image);

			if (!imageReady) {
				logger::error(
					"Photo Finish reconstruction failed.");
			}
		} else {
			imageReady =
				PopulateScratchImageFromStagingTexture(
					context,
					screenshot.stagingTexture.get(),
					screenshot.format,
					screenshot.width,
					screenshot.height,
					image);

			if (!imageReady) {
				logger::error(
					"Failed to map screenshot staging texture.");
			}
		}

		if (!imageReady) {
			if (screenshot.photoFinish)
				SetPhotoFinishStage(
					PhotoFinishStage::Idle,
					0.0f);

			pendingCaptureCount.fetch_sub(
				1,
				std::memory_order_release);
			continue;
		}

		if (screenshot.photoFinish)
			SetPhotoFinishStage(
				PhotoFinishStage::Saving,
				0.96f);

		Util::FileHelpers::EnsureDirectoryExists(screenshot.outputPath.parent_path());

		const bool saveOk = SaveScreenshotToDisk(
			image,
			screenshot.outputPath,
			screenshot.format,
			screenshot.hdrPngBitDepth,
			screenshot.saveAsHdrPng,
			screenshot.saveAsSdrPng);
		if (!saveOk) {
			logger::error(
				"Failed to save {} screenshot.",
				screenshot.saveAsHdrPng ? "HDR PNG" : "SDR");
		}

		if (saveOk) {
			CopySavedPathToClipboard(screenshot.copyToClipboard, screenshot.outputPath);
		}

		if (!saveOk && screenshot.notify) {
			ShowInGameNotification("Screenshot failed - see PIXLRenderer.log");
		} else if (saveOk) {
			if (screenshot.photoFinish) {
				const auto* finished =
					image.GetImage(0, 0, 0);

				logger::info(
					"Saved Photo Finish {}x{} ({}x reconstruction) to {}",
					finished ? finished->width : 0,
					finished ? finished->height : 0,
					screenshot.photoFinishScale,
					screenshot.outputPath.string());
			} else {
				logger::info(
					"Saved screenshot to {}",
					screenshot.outputPath.string());
			}

			if (screenshot.notify) {
				const auto filename =
					screenshot.outputPath
						.filename()
						.string();

				if (screenshot.photoFinish) {
					ShowInGameNotification(
						std::format(
							"Photo Finish saved: {}",
							filename));
				} else {
					ShowInGameNotification(
						std::format(
							"Screenshot saved: {}",
							filename));
				}
			}
		}
		if (screenshot.photoFinish)
			SetPhotoFinishStage(
				PhotoFinishStage::Idle,
				0.0f);

		pendingCaptureCount.fetch_sub(1, std::memory_order_release);
	}
	CoUninitialize();
}

void PixelCapture::ShowInGameNotification(std::string message)
{
	// ShowHUDMessage must run on the game's main thread. Third arg dedupes spam-clicks.
	RunOnMainThread([msg = std::move(message)]() {
		RE::SendHUDMessage::ShowHUDMessage(msg.c_str(), nullptr, true);
	});
}

void PixelCapture::Capture()
{
	CaptureImpl(std::nullopt, true);
}

void PixelCapture::CaptureImpl(const std::optional<std::filesystem::path>& outputPath, bool notify)
{
	auto device = globals::d3d::device;
	auto context = globals::d3d::context;

	if (!device || !context)
		return;

	winrt::com_ptr<ID3D11Texture2D> sourceTextureKeepAlive;
	const auto src = SelectCaptureSource(sourceTextureKeepAlive, /*forCapture=*/true);
	logger::debug("Capturing from {}", src.description);

	if (!src.texture) {
		logger::error("Failed to acquire screenshot source texture ({}).", src.description);
		return;
	}
	ID3D11Texture2D* sourceTexture = src.texture;

	D3D11_TEXTURE2D_DESC srcDesc{};
	sourceTexture->GetDesc(&srcDesc);

	uint32_t copyX = 0;
	uint32_t copyY = 0;
	uint32_t copyW = srcDesc.Width;
	uint32_t copyH = srcDesc.Height;

	if (!outputPath.has_value() && applyCropToScreenshot) {
		auto region = subrect.GetPixelRegion(srcDesc.Width, srcDesc.Height);
		copyX = region.x;
		copyY = region.y;
		copyW = region.w;
		copyH = region.h;
	}

	D3D11_TEXTURE2D_DESC stagingDesc = srcDesc;
	stagingDesc.Width = copyW;
	stagingDesc.Height = copyH;
	stagingDesc.MipLevels = 1;
	stagingDesc.ArraySize = 1;
	stagingDesc.SampleDesc.Count = 1;
	stagingDesc.SampleDesc.Quality = 0;
	stagingDesc.Usage = D3D11_USAGE_STAGING;
	stagingDesc.BindFlags = 0;
	stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	stagingDesc.MiscFlags = 0;

	winrt::com_ptr<ID3D11Texture2D> stagingTexture;
	if (FAILED(device->CreateTexture2D(&stagingDesc, nullptr, stagingTexture.put()))) {
		logger::error("Failed to create screenshot staging texture.");
		return;
	}

	D3D11_BOX sourceRegion{};
	sourceRegion.left = copyX;
	sourceRegion.top = copyY;
	sourceRegion.front = 0;
	sourceRegion.right = copyX + copyW;
	sourceRegion.bottom = copyY + copyH;
	sourceRegion.back = 1;

	context->CopySubresourceRegion(stagingTexture.get(), 0, 0, 0, 0, sourceTexture, 0, &sourceRegion);

	// Match SelectCaptureSource: only the flat HDR back-buffer path uses HDR PNG.
	// Do not key off DXGI format alone — kFRAMEBUFFER can be float/HDR-sized in SDR mode.
	const bool flatHdrCapture = IsFlatHdrScreenshotCapture();
	if (flatHdrCapture && !IsHdrCaptureFormat(srcDesc.Format)) {
		logger::error("Unsupported HDR screenshot format: {}", static_cast<uint32_t>(srcDesc.Format));
		return;
	}
	const bool saveAsHdrPng = flatHdrCapture && IsHdrCaptureFormat(srcDesc.Format);
	const bool exactPng = outputPath.has_value() && outputPath->extension() == ".png";
	const bool saveAsSdrPng = !saveAsHdrPng && (sdrUsePng || exactPng);

	EnsureWorkerThread();
	PendingScreenshot screenshot;
	screenshot.stagingTexture = std::move(stagingTexture);
	screenshot.format = srcDesc.Format;
	screenshot.width = copyW;
	screenshot.height = copyH;
	screenshot.saveAsHdrPng = saveAsHdrPng;
	screenshot.saveAsSdrPng = saveAsSdrPng;
	screenshot.hdrPngBitDepth = static_cast<int>(hdrPngBitDepth);
	screenshot.outputPath = outputPath.value_or(BuildScreenshotPath(screenshotPath, saveAsHdrPng || saveAsSdrPng));
	screenshot.copyToClipboard = outputPath.has_value() ? false : copyToClipboard;
	screenshot.notify = notify;
	EnqueueScreenshot(std::move(screenshot));
}
#undef I18N_KEY_PREFIX
