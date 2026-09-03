#include "DX12SwapChain.h"

#include <FidelityFX/api/include/dx12/ffx_api_dx12.hpp>
#include <algorithm>
#include <dxgi1_6.h>

#include "../CameraSuite.h"
#include "../ImageReconstruction.h"
#include "FidelityFX.h"
#include "Streamline.h"

namespace
{
	std::uint32_t ResolveNeuralPerformanceQuality(std::uint32_t overrideMode, std::uint32_t dlssQualityMode)
	{
		// NVSDK_NGX_PerfQuality_Value: MaxPerf=0, Balanced=1,
		// MaxQuality=2, UltraPerformance=3, UltraQuality=4, DLAA=5.
		switch (overrideMode) {
		case 1: return 5;  // DLAA
		case 2: return 2;  // Quality
		case 3: return 1;  // Balanced
		case 4: return 0;  // Performance
		case 5: return 3;  // Ultra Performance
		case 6: return 4;  // Ultra Quality
		default: break;
		}

		switch (dlssQualityMode) {
		case 0: return 5;
		case 1: return 2;
		case 2: return 1;
		case 3: return 0;
		case 4: return 3;
		default: return 2;
		}
	}
}

void DX12SwapChain::CreateD3D12Device(IDXGIAdapter* a_adapter)
{
	DX::ThrowIfFailed(D3D12CreateDevice(a_adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&d3d12Device)));

	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	queueDesc.NodeMask = 0;

	DX::ThrowIfFailed(d3d12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue)));

	for (int i = 0; i < 2; i++) {
		DX::ThrowIfFailed(d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocators[i])));
		DX::ThrowIfFailed(d3d12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocators[i].get(), nullptr, IID_PPV_ARGS(&commandLists[i])));
		commandLists[i]->Close();
	}
}

void DX12SwapChain::CreateSwapChain(IDXGIAdapter* adapter, DXGI_SWAP_CHAIN_DESC a_swapChainDesc)
{
	CreateD3D12Device(adapter);

	winrt::com_ptr<IDXGIFactory4> dxgiFactory;
	DX::ThrowIfFailed(adapter->GetParent(IID_PPV_ARGS(dxgiFactory.put())));

	// Runtime format negotiation for swap chain
	DXGI_FORMAT attemptedFormat = DXGI_FORMAT_R10G10B10A2_UNORM;
	DXGI_FORMAT negotiatedFormat = DXGI_FORMAT_R10G10B10A2_UNORM;
	bool fallbackUsed = false;

	// Test R10G10B10A2 support for HDR capability
	D3D12_FEATURE_DATA_FORMAT_SUPPORT formatSupport = { DXGI_FORMAT_R10G10B10A2_UNORM, D3D12_FORMAT_SUPPORT1_RENDER_TARGET, D3D12_FORMAT_SUPPORT2_NONE };
	if (SUCCEEDED(d3d12Device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &formatSupport, sizeof(formatSupport)))) {
		if ((formatSupport.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET) == 0) {
			logger::warn("[DX12SwapChain] R10G10B10A2_UNORM not supported as render target, falling back to R8G8B8A8_UNORM");
			negotiatedFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
			fallbackUsed = true;
		}
	} else {
		logger::warn("[DX12SwapChain] CheckFeatureSupport failed for R10G10B10A2_UNORM, falling back to R8G8B8A8_UNORM");
		negotiatedFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
		fallbackUsed = true;
	}

	logger::info("[DX12SwapChain] Swap chain format negotiation: attempted={}, negotiated={}, fallback={}",
		static_cast<uint32_t>(attemptedFormat),
		static_cast<uint32_t>(negotiatedFormat),
		fallbackUsed ? "true" : "false");

	swapChainDesc = {};
	swapChainDesc.Width = a_swapChainDesc.BufferDesc.Width;
	swapChainDesc.Height = a_swapChainDesc.BufferDesc.Height;
	swapChainDesc.Format = negotiatedFormat;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = 2;
	swapChainDesc.SwapEffect = a_swapChainDesc.SwapEffect;
	swapChainDesc.Flags = a_swapChainDesc.Flags;

	ffx::CreateContextDescFrameGenerationSwapChainForHwndDX12 ffxSwapChainDesc{};

	ffxSwapChainDesc.desc = &swapChainDesc;
	ffxSwapChainDesc.dxgiFactory = dxgiFactory.get();
	ffxSwapChainDesc.fullscreenDesc = nullptr;
	ffxSwapChainDesc.gameQueue = commandQueue.get();
	ffxSwapChainDesc.hwnd = a_swapChainDesc.OutputWindow;
	ffxSwapChainDesc.swapchain = &swapChain;

	auto& fidelityFX = globals::pipeline::imageReconstruction.fidelityFX;

	if (ffx::CreateContext(fidelityFX.swapChainContext, nullptr, ffxSwapChainDesc) != ffx::ReturnCode::Ok) {
		logger::critical("[FidelityFX] Failed to create swap chain context!");
		DX::ThrowIfFailed(E_FAIL);
	}

	// Manual Streamline integration must observe Present once per frame so its
	// common plugin can retire frame-scoped tags and internal resources. Wrap
	// the internal FidelityFX swap chain here while leaving PIXL's D3D11-facing
	// DXGISwapChainProxy as the outermost interface returned to Skyrim.
	auto& streamline = globals::pipeline::imageReconstruction.streamline;
	if (streamline.initialized && streamline.slUpgradeInterface) {
		const sl::Result upgradeResult = streamline.slUpgradeInterface(reinterpret_cast<void**>(&swapChain));
		if (upgradeResult != sl::Result::eOk) {
			logger::error(
				"[Streamline] Failed to wrap the frame-generation swap chain: {}",
				magic_enum::enum_name(upgradeResult));
		} else {
			logger::info("[Streamline] Frame-generation present path connected");
		}
	}

	DX::ThrowIfFailed(swapChain->GetBuffer(0, IID_PPV_ARGS(&swapChainBuffers[0])));
	DX::ThrowIfFailed(swapChain->GetBuffer(1, IID_PPV_ARGS(&swapChainBuffers[1])));

	frameIndex = swapChain->GetCurrentBackBufferIndex();

	// Set color space based on Camera Suite feature state and negotiated format
	auto* hdr = globals::pipeline::cameraSuite.loaded ? &globals::pipeline::cameraSuite : nullptr;
	bool enableHDR = hdr && hdr->settings.enableHDR;
	// Only set HDR color space if not falling back to SDR format
	SetColorSpace(enableHDR && !fallbackUsed);

	fidelityFX.SetupFrameGeneration();
}

void DX12SwapChain::CreateInterop()
{
	HANDLE sharedFenceHandle;
	DX::ThrowIfFailed(d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&d3d12Fence)));
	DX::ThrowIfFailed(d3d12Device->CreateSharedHandle(d3d12Fence.get(), nullptr, GENERIC_ALL, nullptr, &sharedFenceHandle));
	DX::ThrowIfFailed(d3d11Device->OpenSharedFence(sharedFenceHandle, IID_PPV_ARGS(&d3d11Fence)));
	CloseHandle(sharedFenceHandle);

	swapChainProxy = std::make_unique<DXGISwapChainProxy>(swapChain);

	D3D11_TEXTURE2D_DESC texDesc11{};
	texDesc11.Width = swapChainDesc.Width;
	texDesc11.Height = swapChainDesc.Height;
	texDesc11.MipLevels = 1;
	texDesc11.ArraySize = 1;
	texDesc11.Format = swapChainDesc.Format;
	texDesc11.SampleDesc.Count = 1;
	texDesc11.SampleDesc.Quality = 0;
	texDesc11.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS;

	swapChainBufferWrapped = std::make_unique<WrappedResource>(texDesc11, d3d11Device.get(), d3d12Device.get());
	if (globals::pipeline::imageReconstruction.neuralRenderingProvisionedAtBoot) {
		for (auto& output : neuralRenderingOutputWrapped)
			output = std::make_unique<WrappedResource>(texDesc11, d3d11Device.get(), d3d12Device.get());
	} else {
		for (auto& output : neuralRenderingOutputWrapped)
			output.reset();
	}
	completedNeuralFrameSerial.store(0, std::memory_order_release);
	completedNeuralOutputIndex.store(UINT32_MAX, std::memory_order_release);

	// UI buffer uses R8G8B8A8_UNORM - vanilla UI is SDR and 8-bit precision
	texDesc11.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	uiBufferWrapped = std::make_unique<WrappedResource>(texDesc11, d3d11Device.get(), d3d12Device.get());
}

DXGISwapChainProxy* DX12SwapChain::GetSwapChainProxy()
{
	return swapChainProxy.get();
}

void DX12SwapChain::SetD3D11Device(ID3D11Device* a_d3d11Device)
{
	DX::ThrowIfFailed(a_d3d11Device->QueryInterface(IID_PPV_ARGS(&d3d11Device)));
}

void DX12SwapChain::SetD3D11DeviceContext(ID3D11DeviceContext* a_d3d11Context)
{
	DX::ThrowIfFailed(a_d3d11Context->QueryInterface(IID_PPV_ARGS(&d3d11Context)));
}

HRESULT DX12SwapChain::GetBuffer(UINT buffer, REFIID riid, void** ppSurface)
{
	if (!ppSurface)
		return E_POINTER;
	*ppSurface = nullptr;
	if (buffer != 0 || !swapChainBufferWrapped || !swapChainBufferWrapped->resource11)
		return DXGI_ERROR_INVALID_CALL;
	return swapChainBufferWrapped->resource11->QueryInterface(riid, ppSurface);
}

HRESULT DX12SwapChain::Present(UINT SyncInterval, UINT Flags)
{
	auto& imageReconstruction = globals::pipeline::imageReconstruction;
	bool completedNeuralThisFrame = false;
	std::uint32_t neuralOutputIndexThisFrame = UINT32_MAX;

	// The proxy swap chain bypasses PIXL's native D3D11 Present detour. Draw the
	// renderer overlay here so its input queue, compiler panel and settings menu
	// remain alive whenever DLSS/FSR frame generation owns presentation.
	globals::pipeline::cameraSuite.DrawRendererUIForPresent();
	static std::once_flag overlayPathLogged;
	std::call_once(overlayPathLogged, []() {
		logger::info("[DX12SwapChain] PIXL overlay/input routed through frame-generation Present");
	});

	// Scale UI brightness BEFORE fence sync so the D3D11 UIBrightnessCS dispatch
	// is covered by the D3D11→D3D12 fence. Without this, FidelityFX may read
	// uiBufferWrapped on D3D12 before the PQ encoding completes on D3D11.
	// Only runs when Camera Suite feature is loaded (UIBrightnessCS may not exist otherwise)
	auto* hdr = globals::pipeline::cameraSuite.loaded ? &globals::pipeline::cameraSuite : nullptr;
	if (hdr)
		hdr->ScaleUIBrightnessForFG();

	bool isHDR = hdr && hdr->settings.enableHDR;

	// Wait for D3D11 to finish (includes ApplyHDR scene encoding AND UIBrightnessCS)
	DX::ThrowIfFailed(d3d11Context->Signal(d3d11Fence.get(), fenceValue));
	DX::ThrowIfFailed(commandQueue->Wait(d3d12Fence.get(), fenceValue));
	fenceValue++;

	// New frame, reset
	DX::ThrowIfFailed(commandAllocators[frameIndex]->Reset());
	DX::ThrowIfFailed(commandLists[frameIndex]->Reset(commandAllocators[frameIndex].get(), nullptr));

	// Run optional Neural Rendering and copy the selected source to the real
	// swap-chain buffer. This records on the same queue/list as presentation,
	// avoiding a second D3D12 device or unsafe allocator overlap.
	{
		auto fakeSwapChain = swapChainBufferWrapped->resource.get();
		auto realSwapChain = swapChainBuffers[frameIndex].get();
		ID3D12Resource* presentationSource = fakeSwapChain;
		bool neuralTransitionsActive = false;

		auto& neuralOutput = neuralRenderingOutputWrapped[frameIndex];
		if (imageReconstruction.ShouldUseNeuralRenderingThisFrame() &&
			neuralOutput && neuralOutput->resource &&
			neuralDepthBufferShared12 && neuralDepthBufferShared12->resource &&
			neuralMotionVectorBufferShared12 && neuralMotionVectorBufferShared12->resource) {
			auto& neural = ImageReconstruction::neuralRendering;
			if (neural.GetStatus() == NeuralRendering::Status::NotProbed ||
				neural.GetStatus() == NeuralRendering::Status::Ready)
				neural.Initialize(d3d12Device.get());

			if (neural.GetStatus() == NeuralRendering::Status::Initialized) {
				D3D11_TEXTURE2D_DESC guideDesc{};
				neuralDepthBufferShared12->resource11->GetDesc(&guideDesc);
				const UINT guideWidth = std::min(neuralGuideWidth, guideDesc.Width);
				const UINT guideHeight = std::min(neuralGuideHeight, guideDesc.Height);
				// Present can run before the first encoded depth/motion copy. Do not
				// manufacture a 1x1 guide contract: Feature 18 treats it as a runtime
				// fault and latches itself off for the session.
				if (guideWidth > 1 && guideHeight > 1) {

				ID3D12Resource* neuralInputs[]{
					fakeSwapChain,
					neuralDepthBufferShared12->resource.get(),
					neuralMotionVectorBufferShared12->resource.get(),
				};
				D3D12_RESOURCE_BARRIER inputBarriers[3]{};
				for (std::size_t i = 0; i < std::size(inputBarriers); ++i) {
					inputBarriers[i] = CD3DX12_RESOURCE_BARRIER::Transition(
						neuralInputs[i],
						D3D12_RESOURCE_STATE_COMMON,
						D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				}
				commandLists[frameIndex]->ResourceBarrier(
					static_cast<UINT>(std::size(inputBarriers)),
					inputBarriers);

				NeuralRendering::Tuning tuning{
					.intensity = imageReconstruction.settings.neuralRenderingIntensity,
					.localToneStrength = imageReconstruction.settings.neuralRenderingLocalTone,
					.localStructureStrength = imageReconstruction.settings.neuralRenderingLocalStructure,
					.skinStructureStrength = imageReconstruction.settings.neuralRenderingSkinStructure,
					.style = imageReconstruction.settings.neuralRenderingStyle,
					.performanceQuality = ResolveNeuralPerformanceQuality(
						imageReconstruction.neuralRenderingQualityModeAtBoot,
						imageReconstruction.GetEffectiveQualityMode()),
					.outputPreset = imageReconstruction.neuralRenderingOutputPresetAtBoot,
					.useAutoMask = imageReconstruction.settings.neuralRenderingAutoMask,
					.uiCorrection = imageReconstruction.settings.neuralRenderingUICorrection,
				};
				const bool initialReset =
					imageReconstruction.pendingNeuralRenderingReset.exchange(false, std::memory_order_acq_rel);
				const std::uint32_t targetIndex = static_cast<std::uint32_t>(frameIndex) & 1u;
				auto& target = neuralRenderingOutputWrapped[targetIndex];
				ID3D12Resource* finalNeuralOutput = nullptr;
				std::uint32_t finalNeuralOutputIndex = UINT32_MAX;
				if (target && target->resource) {
					const auto targetToUav = CD3DX12_RESOURCE_BARRIER::Transition(
						target->resource.get(),
						D3D12_RESOURCE_STATE_COMMON,
						D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
					commandLists[frameIndex]->ResourceBarrier(1, &targetToUav);

					// Feature 18 is a temporal reconstruction model, not a recursively
					// composable image generator. Re-feeding its output with unchanged
					// geometry guides visibly low-passes detail. Photo Finish therefore
					// accumulates fresh model outputs over real jittered frames instead.
					const bool succeeded = neural.Evaluate(
						commandLists[frameIndex].get(), fakeSwapChain,
						neuralDepthBufferShared12->resource.get(),
						neuralMotionVectorBufferShared12->resource.get(),
						target->resource.get(),
						guideWidth, guideHeight,
						swapChainDesc.Width, swapChainDesc.Height,
						static_cast<float>(guideWidth), static_cast<float>(guideHeight),
						tuning,
						initialReset);

					const auto outputTransition = CD3DX12_RESOURCE_BARRIER::Transition(
						target->resource.get(),
						D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
						succeeded ? D3D12_RESOURCE_STATE_COPY_SOURCE : D3D12_RESOURCE_STATE_COMMON);
					commandLists[frameIndex]->ResourceBarrier(1, &outputTransition);
					if (succeeded) {
						finalNeuralOutput = target->resource.get();
						finalNeuralOutputIndex = targetIndex;
					}
				}

				D3D12_RESOURCE_BARRIER restoreInputs[3]{};
				for (std::size_t i = 0; i < std::size(restoreInputs); ++i) {
					restoreInputs[i] = CD3DX12_RESOURCE_BARRIER::Transition(
						neuralInputs[i],
						D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
						D3D12_RESOURCE_STATE_COMMON);
				}
				commandLists[frameIndex]->ResourceBarrier(
					static_cast<UINT>(std::size(restoreInputs)),
					restoreInputs);

				if (finalNeuralOutput) {
					presentationSource = finalNeuralOutput;
					neuralTransitionsActive = true;
					completedNeuralThisFrame = true;
					neuralOutputIndexThisFrame = finalNeuralOutputIndex;
				}
				}
			}
		}
		{
			std::vector<D3D12_RESOURCE_BARRIER> barriers;
			if (presentationSource == fakeSwapChain)
				barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(fakeSwapChain, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE));
			barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(realSwapChain, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST));
			commandLists[frameIndex]->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
		}

		commandLists[frameIndex]->CopyResource(realSwapChain, presentationSource);

		{
			std::vector<D3D12_RESOURCE_BARRIER> barriers;
			if (presentationSource == fakeSwapChain)
				barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(fakeSwapChain, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON));
			else if (neuralTransitionsActive)
				barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(presentationSource, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON));
			barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(realSwapChain, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT));
			commandLists[frameIndex]->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
		}
	}

	imageReconstruction.fidelityFX.Present(imageReconstruction.ShouldUseFrameGenerationThisFrame(), isHDR);

	DX::ThrowIfFailed(commandLists[frameIndex]->Close());

	ID3D12CommandList* commandListsToExecute[] = { commandLists[frameIndex].get() };
	commandQueue->ExecuteCommandLists(1, commandListsToExecute);

	// Present the frame
	DX::ThrowIfFailed(swapChain->Present(SyncInterval, Flags));

	// Wait for D3D12 to finish
	DX::ThrowIfFailed(commandQueue->Signal(d3d12Fence.get(), fenceValue));
	DX::ThrowIfFailed(d3d11Context->Wait(d3d11Fence.get(), fenceValue));
	fenceValue++;

	if (completedNeuralThisFrame) {
		completedNeuralOutputIndex.store(neuralOutputIndexThisFrame, std::memory_order_release);
		completedNeuralFrameSerial.fetch_add(1, std::memory_order_acq_rel);
	}

	// Update the frame index
	frameIndex = swapChain->GetCurrentBackBufferIndex();

	float clearColor[4]{ 0, 0, 0, 0 };
	d3d11Context->ClearRenderTargetView(uiBufferWrapped->rtv, clearColor);

	// If VSync is disabled, use frame limiter to prevent tearing and optimise pacing
	if (SyncInterval == 0)
		imageReconstruction.FrameLimiter();

	return S_OK;
}

HRESULT DX12SwapChain::GetDevice(REFIID uuid, void** ppDevice)
{
	if (!ppDevice)
		return E_POINTER;
	*ppDevice = nullptr;
	if (uuid == __uuidof(ID3D11Device) || uuid == __uuidof(ID3D11Device1) || uuid == __uuidof(ID3D11Device2) || uuid == __uuidof(ID3D11Device3) || uuid == __uuidof(ID3D11Device4) || uuid == __uuidof(ID3D11Device5)) {
		return d3d11Device ? d3d11Device->QueryInterface(uuid, ppDevice) : E_NOINTERFACE;
	}

	return swapChain ? swapChain->GetDevice(uuid, ppDevice) : E_NOINTERFACE;
}

HANDLE DX12SwapChain::GetFrameLatencyWaitableObject()
{
	return swapChain->GetFrameLatencyWaitableObject();
}

float DX12SwapChain::GetFrameTime() const
{
	// Calculate frame time based on swap chain presentation
	static float lastPresentTime = 0.0f;
	static float frameTime = 1.0f / 60.0f;  // Default to 60 fps
	static LARGE_INTEGER frequency = {};
	static LARGE_INTEGER currentTime = {};

	if (frequency.QuadPart == 0) {
		QueryPerformanceFrequency(&frequency);
	}

	QueryPerformanceCounter(&currentTime);
	float time = static_cast<float>(currentTime.QuadPart) / static_cast<float>(frequency.QuadPart);

	if (lastPresentTime > 0.0f) {
		frameTime = time - lastPresentTime;
	}
	lastPresentTime = time;

	return frameTime;
}

WrappedResource::WrappedResource(D3D11_TEXTURE2D_DESC a_texDesc, ID3D11Device5* a_d3d11Device, ID3D12Device* a_d3d12Device)
{
	// Create D3D11 shared texture directly instead of wrapping D3D12 resource
	a_texDesc.MiscFlags |= D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
	DX::ThrowIfFailed(a_d3d11Device->CreateTexture2D(&a_texDesc, nullptr, &resource11));

	// Get shared handle from D3D11 texture to enable D3D12 access
	winrt::com_ptr<IDXGIResource1> dxgiResource;
	DX::ThrowIfFailed(resource11->QueryInterface(IID_PPV_ARGS(dxgiResource.put())));
	HANDLE sharedHandle = nullptr;
	DX::ThrowIfFailed(dxgiResource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &sharedHandle));

	// Open the shared D3D11 texture as D3D12 resource
	DX::ThrowIfFailed(a_d3d12Device->OpenSharedHandle(sharedHandle, IID_PPV_ARGS(resource.put())));
	CloseHandle(sharedHandle);

	if (a_texDesc.BindFlags & D3D11_BIND_SHADER_RESOURCE) {
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = a_texDesc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;

		DX::ThrowIfFailed(a_d3d11Device->CreateShaderResourceView(resource11, &srvDesc, &srv));
	}

	if (a_texDesc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) {
		if (a_texDesc.ArraySize > 1) {
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format = a_texDesc.Format;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
			uavDesc.Texture2DArray.FirstArraySlice = 0;
			uavDesc.Texture2DArray.ArraySize = a_texDesc.ArraySize;

			DX::ThrowIfFailed(a_d3d11Device->CreateUnorderedAccessView(resource11, &uavDesc, &uav));
		} else {
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format = a_texDesc.Format;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			uavDesc.Texture2D.MipSlice = 0;

			DX::ThrowIfFailed(a_d3d11Device->CreateUnorderedAccessView(resource11, &uavDesc, &uav));
		}
	}

	if (a_texDesc.BindFlags & D3D11_BIND_RENDER_TARGET) {
		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		rtvDesc.Format = a_texDesc.Format;
		rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		rtvDesc.Texture2D.MipSlice = 0;
		DX::ThrowIfFailed(a_d3d11Device->CreateRenderTargetView(resource11, &rtvDesc, &rtv));
	}
}

WrappedResource::~WrappedResource()
{
	if (resource11) {
		resource11->Release();
		resource11 = nullptr;
	}
	if (srv) {
		srv->Release();
		srv = nullptr;
	}
	if (uav) {
		uav->Release();
		uav = nullptr;
	}
	if (rtv) {
		rtv->Release();
		rtv = nullptr;
	}
	// resource (winrt::com_ptr) will be automatically released
}

DXGISwapChainProxy::DXGISwapChainProxy(IDXGISwapChain4* a_swapChain)
{
	swapChain = a_swapChain;
}

/****IUknown****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::QueryInterface(REFIID riid, void** ppvObj)
{
	if (!ppvObj)
		return E_POINTER;
	*ppvObj = nullptr;

	if (riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) ||
		riid == __uuidof(IDXGIDeviceSubObject) || riid == __uuidof(IDXGISwapChain)) {
		*ppvObj = static_cast<IDXGISwapChain*>(this);
		AddRef();
		return S_OK;
	}

	return swapChain ? swapChain->QueryInterface(riid, ppvObj) : E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE DXGISwapChainProxy::AddRef()
{
	return swapChain->AddRef();
}

ULONG STDMETHODCALLTYPE DXGISwapChainProxy::Release()
{
	return swapChain->Release();
}

/****IDXGIObject****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetPrivateData(_In_ REFGUID Name, UINT DataSize, _In_reads_bytes_(DataSize) const void* pData)
{
	return swapChain->SetPrivateData(Name, DataSize, pData);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetPrivateDataInterface(_In_ REFGUID Name, _In_opt_ const IUnknown* pUnknown)
{
	return swapChain->SetPrivateDataInterface(Name, pUnknown);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetPrivateData(_In_ REFGUID Name, _Inout_ UINT* pDataSize, _Out_writes_bytes_(*pDataSize) void* pData)
{
	return swapChain->GetPrivateData(Name, pDataSize, pData);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetParent(_In_ REFIID riid, _COM_Outptr_ void** ppParent)
{
	return swapChain->GetParent(riid, ppParent);
}

/****IDXGIDeviceSubObject****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetDevice(_In_ REFIID riid, _COM_Outptr_ void** ppDevice)
{
	return globals::pipeline::imageReconstruction.dx12SwapChain.GetDevice(riid, ppDevice);
}

/****IDXGISwapChain****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::Present(UINT SyncInterval, UINT Flags)
{
	return globals::pipeline::imageReconstruction.dx12SwapChain.Present(SyncInterval, Flags);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetBuffer(UINT Buffer, _In_ REFIID riid, _COM_Outptr_ void** ppSurface)
{
	return globals::pipeline::imageReconstruction.dx12SwapChain.GetBuffer(Buffer, riid, ppSurface);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetFullscreenState(BOOL Fullscreen, _In_opt_ IDXGIOutput* pTarget)
{
	return swapChain->SetFullscreenState(Fullscreen, pTarget);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetFullscreenState(_Out_opt_ BOOL* pFullscreen, _COM_Outptr_opt_result_maybenull_ IDXGIOutput** ppTarget)
{
	return swapChain->GetFullscreenState(pFullscreen, ppTarget);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetDesc(_Out_ DXGI_SWAP_CHAIN_DESC* pDesc)
{
	return swapChain->GetDesc(pDesc);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::ResizeBuffers(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
	return swapChain->ResizeBuffers(BufferCount, Width, Height, NewFormat, SwapChainFlags);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::ResizeTarget(_In_ const DXGI_MODE_DESC* pNewTargetParameters)
{
	return swapChain->ResizeTarget(pNewTargetParameters);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetContainingOutput(_COM_Outptr_ IDXGIOutput** ppOutput)
{
	return swapChain->GetContainingOutput(ppOutput);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetFrameStatistics(_Out_ DXGI_FRAME_STATISTICS* pStats)
{
	return swapChain->GetFrameStatistics(pStats);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetLastPresentCount(_Out_ UINT* pLastPresentCount)
{
	return swapChain->GetLastPresentCount(pLastPresentCount);
}

void DX12SwapChain::SetColorSpace(bool enableHDR)
{
	if (!swapChain)
		return;

	if (enableHDR) {
		swapChain->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
		logger::info("[DX12SwapChain] Set color space to HDR10 (PQ/BT.2020)");
	} else {
		swapChain->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
		logger::info("[DX12SwapChain] Set color space to SDR (sRGB)");
	}
}

DX12SwapChain::BlurResources DX12SwapChain::GetBlurResources() const
{
	BlurResources res;
	if (swapChainBufferWrapped) {
		res.backbufferTex = swapChainBufferWrapped->resource11;
		res.backbufferRTV = swapChainBufferWrapped->rtv;
		res.backbufferSRV = swapChainBufferWrapped->srv;
	}
	if (uiBufferWrapped) {
		res.uiBufferSRV = uiBufferWrapped->srv;
		res.uiBufferRTV = uiBufferWrapped->rtv;
	}
	return res;
}

void DX12SwapChain::CreateSharedResources()
{
	auto renderer = globals::game::renderer;

	// Create depth buffer
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	D3D11_TEXTURE2D_DESC texDesc{};
	main.texture->GetDesc(&texDesc);
	texDesc.Format = DXGI_FORMAT_R32_FLOAT;
	depthBufferShared12 = std::make_unique<WrappedResource>(texDesc, d3d11Device.get(), d3d12Device.get());
	if (globals::pipeline::imageReconstruction.neuralRenderingProvisionedAtBoot)
		neuralDepthBufferShared12 = std::make_unique<WrappedResource>(texDesc, d3d11Device.get(), d3d12Device.get());
	else
		neuralDepthBufferShared12.reset();

	// Create motion vector buffer
	auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
	motionVector.texture->GetDesc(&texDesc);
	motionVectorBufferShared12 = std::make_unique<WrappedResource>(texDesc, d3d11Device.get(), d3d12Device.get());
	if (globals::pipeline::imageReconstruction.neuralRenderingProvisionedAtBoot)
		neuralMotionVectorBufferShared12 = std::make_unique<WrappedResource>(texDesc, d3d11Device.get(), d3d12Device.get());
	else
		neuralMotionVectorBufferShared12.reset();
}

ID3D11Texture2D* DX12SwapChain::GetCompletedNeuralOutput() const
{
	const auto index = completedNeuralOutputIndex.load(std::memory_order_acquire);
	if (index >= std::size(neuralRenderingOutputWrapped) || !neuralRenderingOutputWrapped[index])
		return nullptr;
	return neuralRenderingOutputWrapped[index]->resource11;
}

ID3D11Texture2D* DX12SwapChain::GetProvisionedNeuralOutput() const
{
	for (const auto& output : neuralRenderingOutputWrapped) {
		if (output && output->resource11)
			return output->resource11;
	}
	return nullptr;
}

std::uint64_t DX12SwapChain::GetCompletedNeuralFrameSerial() const
{
	return completedNeuralFrameSerial.load(std::memory_order_acquire);
}

#if 0  // Retired: Photo Finish uses completed live Feature 18 frames.
bool DX12SwapChain::EvaluateOfflinePhotoNeural(
	const DirectX::Image& color,
	const DirectX::Image& depth,
	DirectX::ScratchImage& output)
{
	if (!d3d12Device || !commandQueue || color.width <= 1 || color.height <= 1 ||
		depth.width != color.width || depth.height != color.height ||
		depth.format != DXGI_FORMAT_R32_FLOAT || color.format != swapChainDesc.Format) {
		logger::warn(
			"[NeuralRendering] Photo Finish offline input rejected: color={} {}x{}, depth={} {}x{}",
			static_cast<std::uint32_t>(color.format), color.width, color.height,
			static_cast<std::uint32_t>(depth.format), depth.width, depth.height);
		return false;
	}
	logger::info(
		"[DX12SwapChain] Created {} frame-interpolation swap chain at {}x{} ({}/{})",
		a_swapChainDesc.Windowed ? "windowed/borderless" : "exclusive-fullscreen",
		swapChainDesc.Width,
		swapChainDesc.Height,
		fullscreenDesc.RefreshRate.Numerator,
		fullscreenDesc.RefreshRate.Denominator);

	constexpr std::uint64_t kMaxPhotoPixels = 48ull * 1000ull * 1000ull;
	const std::uint64_t pixelCount = static_cast<std::uint64_t>(color.width) * color.height;
	if (pixelCount > kMaxPhotoPixels) {
		logger::warn(
			"[NeuralRendering] Photo Finish offline input exceeds the 48 MP safety limit: {}x{}",
			color.width, color.height);
		return false;
	}

	std::scoped_lock queueLock(neuralQueueMutex);
	auto& reconstruction = globals::pipeline::imageReconstruction;
	auto& neural = ImageReconstruction::neuralRendering;
	if ((neural.GetStatus() == NeuralRendering::Status::NotProbed ||
		 neural.GetStatus() == NeuralRendering::Status::Ready) &&
		!neural.Initialize(d3d12Device.get())) {
		return false;
	}
	if (neural.GetStatus() != NeuralRendering::Status::Initialized)
		return false;

	winrt::com_ptr<ID3D12CommandAllocator> allocator;
	winrt::com_ptr<ID3D12GraphicsCommandList4> list;
	if (FAILED(d3d12Device->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(allocator.put()))) ||
		FAILED(d3d12Device->CreateCommandList(
			0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.get(), nullptr, IID_PPV_ARGS(list.put())))) {
		logger::warn("[NeuralRendering] Photo Finish could not allocate an isolated DX12 command list");
		return false;
	}

	const auto createTexture = [&](DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags,
		D3D12_RESOURCE_STATES initialState, winrt::com_ptr<ID3D12Resource>& resource) {
		const auto desc = CD3DX12_RESOURCE_DESC::Tex2D(
			format,
			static_cast<UINT64>(color.width),
			static_cast<UINT>(color.height),
			1, 1, 1, 0, flags);
		const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
		return SUCCEEDED(d3d12Device->CreateCommittedResource(
			&heap, D3D12_HEAP_FLAG_NONE, &desc, initialState, nullptr,
			IID_PPV_ARGS(resource.put())));
	};

	winrt::com_ptr<ID3D12Resource> colorTexture;
	winrt::com_ptr<ID3D12Resource> depthTexture;
	winrt::com_ptr<ID3D12Resource> motionTexture;
	winrt::com_ptr<ID3D12Resource> outputTexture;
	if (!createTexture(color.format, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, colorTexture) ||
		!createTexture(DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, depthTexture) ||
		!createTexture(DXGI_FORMAT_R16G16_FLOAT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, motionTexture) ||
		!createTexture(color.format, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS, outputTexture)) {
		logger::warn("[NeuralRendering] Photo Finish could not allocate high-resolution neural resources");
		return false;
	}

	std::vector<winrt::com_ptr<ID3D12Resource>> uploadBuffers;
	uploadBuffers.reserve(3);
	const auto uploadImage = [&](const DirectX::Image* image, ID3D12Resource* destination,
		bool clearToZero) {
		const auto destinationDesc = destination->GetDesc();
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
		UINT rowCount = 0;
		UINT64 rowSize = 0;
		UINT64 totalBytes = 0;
		d3d12Device->GetCopyableFootprints(
			&destinationDesc, 0, 1, 0, &footprint, &rowCount, &rowSize, &totalBytes);
		const CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
		const auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(totalBytes);
		winrt::com_ptr<ID3D12Resource> upload;
		if (FAILED(d3d12Device->CreateCommittedResource(
				&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
				D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(upload.put()))))
			return false;

		std::uint8_t* mapped = nullptr;
		if (FAILED(upload->Map(0, nullptr, reinterpret_cast<void**>(&mapped))))
			return false;
		std::memset(mapped, 0, static_cast<std::size_t>(totalBytes));
		if (!clearToZero && image) {
			const std::size_t copyBytes = static_cast<std::size_t>(std::min<UINT64>(rowSize, image->rowPitch));
			for (UINT row = 0; row < rowCount; ++row) {
				std::memcpy(
					mapped + footprint.Offset + static_cast<std::size_t>(row) * footprint.Footprint.RowPitch,
					image->pixels + static_cast<std::size_t>(row) * image->rowPitch,
					copyBytes);
			}
		}
		upload->Unmap(0, nullptr);

		D3D12_TEXTURE_COPY_LOCATION src{};
		src.pResource = upload.get();
		src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		src.PlacedFootprint = footprint;
		D3D12_TEXTURE_COPY_LOCATION dst{};
		dst.pResource = destination;
		dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dst.SubresourceIndex = 0;
		list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
		uploadBuffers.push_back(std::move(upload));
		return true;
	};

	if (!uploadImage(&color, colorTexture.get(), false) ||
		!uploadImage(&depth, depthTexture.get(), false) ||
		!uploadImage(nullptr, motionTexture.get(), true)) {
		logger::warn("[NeuralRendering] Photo Finish failed to upload high-resolution neural inputs");
		return false;
	}

	ID3D12Resource* inputs[]{ colorTexture.get(), depthTexture.get(), motionTexture.get() };
	D3D12_RESOURCE_BARRIER inputBarriers[3]{};
	for (std::size_t i = 0; i < std::size(inputBarriers); ++i) {
		inputBarriers[i] = CD3DX12_RESOURCE_BARRIER::Transition(
			inputs[i], D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	}
	list->ResourceBarrier(static_cast<UINT>(std::size(inputBarriers)), inputBarriers);

	NeuralRendering::Tuning tuning{
		.intensity = reconstruction.settings.neuralRenderingIntensity,
		.localToneStrength = reconstruction.settings.neuralRenderingLocalTone,
		.localStructureStrength = reconstruction.settings.neuralRenderingLocalStructure,
		.skinStructureStrength = reconstruction.settings.neuralRenderingSkinStructure,
		.style = reconstruction.settings.neuralRenderingStyle,
		.performanceQuality = 5u,  // frozen high-resolution DLAA contract
		.outputPreset = reconstruction.neuralRenderingOutputPresetAtBoot,
		.useAutoMask = reconstruction.settings.neuralRenderingAutoMask,
		.uiCorrection = false,
	};

	const auto width = static_cast<std::uint32_t>(color.width);
	const auto height = static_cast<std::uint32_t>(color.height);
	const bool evaluated = neural.Evaluate(
		list.get(), colorTexture.get(), depthTexture.get(), motionTexture.get(), outputTexture.get(),
		width, height, width, height, static_cast<float>(width), static_cast<float>(height),
		tuning, true, false);

	D3D12_PLACED_SUBRESOURCE_FOOTPRINT outputFootprint{};
	UINT outputRows = 0;
	UINT64 outputRowSize = 0;
	UINT64 outputBytes = 0;
	winrt::com_ptr<ID3D12Resource> readback;
	if (evaluated) {
		const auto outputDesc = outputTexture->GetDesc();
		d3d12Device->GetCopyableFootprints(
			&outputDesc, 0, 1, 0, &outputFootprint, &outputRows, &outputRowSize, &outputBytes);
		const CD3DX12_HEAP_PROPERTIES readbackHeap(D3D12_HEAP_TYPE_READBACK);
		const auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(outputBytes);
		if (FAILED(d3d12Device->CreateCommittedResource(
				&readbackHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
				D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(readback.put())))) {
			logger::warn("[NeuralRendering] Photo Finish could not allocate the neural readback buffer");
			return false;
		}
		const auto outputToCopy = CD3DX12_RESOURCE_BARRIER::Transition(
			outputTexture.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
		list->ResourceBarrier(1, &outputToCopy);
		D3D12_TEXTURE_COPY_LOCATION src{};
		src.pResource = outputTexture.get();
		src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		D3D12_TEXTURE_COPY_LOCATION dst{};
		dst.pResource = readback.get();
		dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		dst.PlacedFootprint = outputFootprint;
		list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
	}

	if (FAILED(list->Close()))
		return false;
	ID3D12CommandList* lists[]{ list.get() };
	commandQueue->ExecuteCommandLists(1, lists);

	winrt::com_ptr<ID3D12Fence> completionFence;
	if (FAILED(d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(completionFence.put()))))
		return false;
	Microsoft::WRL::Wrappers::Event completionEvent(CreateEventW(nullptr, FALSE, FALSE, nullptr));
	if (!completionEvent.IsValid() || FAILED(commandQueue->Signal(completionFence.get(), 1u)) ||
		FAILED(completionFence->SetEventOnCompletion(1u, completionEvent.Get())))
		return false;
	WaitForSingleObject(completionEvent.Get(), INFINITE);

	// The offline dimensions replace the live feature handle. Recreate the normal
	// display-sized contract cleanly on the next Present rather than carrying its
	// still-image history into gameplay.
	neural.ResetFeature();
	reconstruction.pendingNeuralRenderingReset.store(true, std::memory_order_release);
	if (!evaluated || !readback)
		return false;

	if (FAILED(output.Initialize2D(color.format, color.width, color.height, 1, 1)))
		return false;
	const DirectX::Image* outputImage = output.GetImage(0, 0, 0);
	if (!outputImage)
		return false;
	const std::uint8_t* mapped = nullptr;
	D3D12_RANGE readRange{ 0, static_cast<SIZE_T>(outputBytes) };
	if (FAILED(readback->Map(0, &readRange, reinterpret_cast<void**>(const_cast<std::uint8_t**>(&mapped)))))
		return false;
	const std::size_t copyBytes = static_cast<std::size_t>(std::min<UINT64>(outputRowSize, outputImage->rowPitch));
	for (UINT row = 0; row < outputRows; ++row) {
		std::memcpy(
			output.GetPixels() + static_cast<std::size_t>(row) * outputImage->rowPitch,
			mapped + outputFootprint.Offset + static_cast<std::size_t>(row) * outputFootprint.Footprint.RowPitch,
			copyBytes);
	}
	D3D12_RANGE writeRange{ 0, 0 };
	readback->Unmap(0, &writeRange);
	logger::info(
		"[NeuralRendering] Photo Finish completed one isolated {}x{} DLAA neural refinement pass",
		width, height);
	return true;
}
#endif
