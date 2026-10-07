#pragma once

#include <Windows.Foundation.h>
#include <atomic>
#include <memory>
#include <stdio.h>
#include <winrt/base.h>
#include <wrl\client.h>
#include <wrl\wrappers\corewrappers.h>

#include <d3d11_4.h>
#include <d3d12.h>

#include <directx/d3dx12.h>

class WrappedResource
{
public:
	WrappedResource(D3D11_TEXTURE2D_DESC a_texDesc, ID3D11Device5* a_d3d11Device, ID3D12Device* a_d3d12Device);
	~WrappedResource();

	ID3D11Texture2D* resource11 = nullptr;
	ID3D11ShaderResourceView* srv = nullptr;
	ID3D11UnorderedAccessView* uav = nullptr;
	ID3D11RenderTargetView* rtv = nullptr;
	winrt::com_ptr<ID3D12Resource> resource;
};

struct DXGISwapChainProxy : IDXGISwapChain
{
public:
	DXGISwapChainProxy(IDXGISwapChain4* a_swapChain);

	IDXGISwapChain4* swapChain = nullptr;

	/****IUnknown****/
	virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObj) override;
	virtual ULONG STDMETHODCALLTYPE AddRef() override;
	virtual ULONG STDMETHODCALLTYPE Release() override;

	/****IDXGIObject****/
	virtual HRESULT STDMETHODCALLTYPE SetPrivateData(_In_ REFGUID Name, UINT DataSize, _In_reads_bytes_(DataSize) const void* pData) override;
	virtual HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(_In_ REFGUID Name, _In_opt_ const IUnknown* pUnknown) override;
	virtual HRESULT STDMETHODCALLTYPE GetPrivateData(_In_ REFGUID Name, _Inout_ UINT* pDataSize, _Out_writes_bytes_(*pDataSize) void* pData) override;
	virtual HRESULT STDMETHODCALLTYPE GetParent(_In_ REFIID riid, _COM_Outptr_ void** ppParent) override;

	/****IDXGIDeviceSubObject****/
	virtual HRESULT STDMETHODCALLTYPE GetDevice(_In_ REFIID riid, _COM_Outptr_ void** ppDevice) override;

	/****IDXGISwapChain****/
	virtual HRESULT STDMETHODCALLTYPE Present(UINT SyncInterval, UINT Flags);
	virtual HRESULT STDMETHODCALLTYPE GetBuffer(UINT Buffer, _In_ REFIID riid, _COM_Outptr_ void** ppSurface);
	virtual HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL Fullscreen, _In_opt_ IDXGIOutput* pTarget);
	virtual HRESULT STDMETHODCALLTYPE GetFullscreenState(_Out_opt_ BOOL* pFullscreen, _COM_Outptr_opt_result_maybenull_ IDXGIOutput** ppTarget);
	virtual HRESULT STDMETHODCALLTYPE GetDesc(_Out_ DXGI_SWAP_CHAIN_DESC* pDesc);
	virtual HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
	virtual HRESULT STDMETHODCALLTYPE ResizeTarget(_In_ const DXGI_MODE_DESC* pNewTargetParameters);
	virtual HRESULT STDMETHODCALLTYPE GetContainingOutput(_COM_Outptr_ IDXGIOutput** ppOutput);
	virtual HRESULT STDMETHODCALLTYPE GetFrameStatistics(_Out_ DXGI_FRAME_STATISTICS* pStats);
	virtual HRESULT STDMETHODCALLTYPE GetLastPresentCount(_Out_ UINT* pLastPresentCount);
};

class DX12SwapChain
{
public:
	enum class Presenter : std::uint8_t
	{
		kNone,
		kFidelityFX,
		kDLSSG,
		kNeuralOnly
	};

	Presenter presenter = Presenter::kNone;
	winrt::com_ptr<ID3D12Device> d3d12Device;
	// Native device stays available to NGX Neural Rendering and shared resources.
	// Only mandatory Streamline hooks use this separate device wrapper.
	winrt::com_ptr<ID3D12Device> dlssgDevice;
	winrt::com_ptr<ID3D12CommandQueue> commandQueue;
	winrt::com_ptr<ID3D12CommandAllocator> commandAllocators[3];
	winrt::com_ptr<ID3D12GraphicsCommandList4> commandLists[3];

	IDXGISwapChain4* swapChain = nullptr;

	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};

	std::unique_ptr<WrappedResource> swapChainBufferWrapped;
	std::unique_ptr<WrappedResource> uiBufferWrapped;
	// Optional DLSS Neural Rendering output. It is allocated with the sidecar
	// but remains untouched unless the capability-gated experimental path runs.
	// Double-buffered so D3D11 photo capture can copy the last completed neural
	// frame while D3D12 writes the next one. A single shared output races at the
	// nested Present boundary used by Skyrim's D3D11 swap-chain hook.
	std::unique_ptr<WrappedResource> neuralRenderingOutputWrapped[2];

	// D3D12 interop resources for frame generation
	std::unique_ptr<WrappedResource> depthBufferShared12;
	std::unique_ptr<WrappedResource> motionVectorBufferShared12;
	// Neural Rendering uses post-DLSS encoded/dilated guides. Keep them separate
	// from FSR frame generation's raw pre-upscale guides when both features run.
	std::unique_ptr<WrappedResource> neuralDepthBufferShared12;
	std::unique_ptr<WrappedResource> neuralMotionVectorBufferShared12;

	// Active render subrect written into the full-size shared guide textures.
	// Neural Rendering consumes only this region; frame generation continues to
	// use the existing full-resource contract.
	UINT neuralGuideWidth = 0;
	UINT neuralGuideHeight = 0;

	// Published only after the D3D12 queue has completed a successful neural
	// frame and the D3D11 fence wait has been queued. Consumers use the serial to
	// reject stale frames during deterministic Photo Finish accumulation.
	std::atomic_uint64_t completedNeuralFrameSerial{ 0 };
	std::atomic_uint32_t completedNeuralOutputIndex{ UINT32_MAX };

	winrt::com_ptr<ID3D11Device5> d3d11Device;
	winrt::com_ptr<ID3D11DeviceContext4> d3d11Context;

	winrt::com_ptr<ID3D11Fence> d3d11Fence;
	winrt::com_ptr<ID3D12Fence> d3d12Fence;
	winrt::handle allocatorFenceEvent;
	UINT64 allocatorFenceValues[3]{};

	winrt::com_ptr<ID3D12Resource> swapChainBuffers[3];

	UINT frameIndex = 0;
	UINT64 fenceValue = 0;

	LARGE_INTEGER qpf{};

	double refreshRate = 0;
	std::unique_ptr<DXGISwapChainProxy> swapChainProxy;

	// Returns the current frame time (in seconds) for accurate FPS calculation when frame generation is active
	float GetFrameTime() const;

	void CreateD3D12Device(IDXGIAdapter* a_adapter);
	void RecreateCommandQueue();
	void RecreateCommandObjects();
	void CreateSwapChain(IDXGIAdapter* adapter, DXGI_SWAP_CHAIN_DESC swapChainDesc, Presenter requestedPresenter);
	void CreateSwapChainDirect(IDXGIAdapter* adapter, DXGI_SWAP_CHAIN_DESC swapChainDesc, Presenter requestedPresenter);

	void CreateInterop();

	DXGISwapChainProxy* GetSwapChainProxy();
	void SetD3D11Device(ID3D11Device* a_d3d11Device);
	void SetD3D11DeviceContext(ID3D11DeviceContext* a_d3d11Context);

	HRESULT GetBuffer(UINT buffer, REFIID riid, void** ppSurface);
	HRESULT Present(UINT SyncInterval, UINT Flags);
	HRESULT GetDevice(_In_ REFIID riid, _COM_Outptr_ void** ppDevice);
	HANDLE GetFrameLatencyWaitableObject();

	void SetColorSpace(bool enableHDR);

	// Resources needed by BackgroundBlur when D3D12 swap chain is active
	struct BlurResources
	{
		ID3D11Texture2D* backbufferTex = nullptr;
		ID3D11RenderTargetView* backbufferRTV = nullptr;
		ID3D11ShaderResourceView* backbufferSRV = nullptr;
		ID3D11ShaderResourceView* uiBufferSRV = nullptr;
		ID3D11RenderTargetView* uiBufferRTV = nullptr;
	};

	// Get all resources needed for background blur in one call
	BlurResources GetBlurResources() const;

	// D3D12 interop resource management
	void CreateSharedResources();

	[[nodiscard]] ID3D11Texture2D* GetCompletedNeuralOutput() const;
	[[nodiscard]] ID3D11Texture2D* GetProvisionedNeuralOutput() const;
	[[nodiscard]] std::uint64_t GetCompletedNeuralFrameSerial() const;

};
