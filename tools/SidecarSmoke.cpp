// Standalone startup/presentation diagnostic; never reads or rebuilds Skyrim shaders.
#include <windows.h>
#include <d3d12.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdio>
#include <filesystem>
#include <sl.h>
#include <sl_core_api.h>
#include <sl_dlss_g.h>
#include <sl_reflex.h>
using Microsoft::WRL::ComPtr;
static void Check(HRESULT hr, const char* stage) {
    std::printf("%s: %08lx\n", stage, hr); std::fflush(stdout);
    if (FAILED(hr)) ExitProcess(2);
}
static void CheckSL(sl::Result r, const char* stage) {
    std::printf("%s: %u\n", stage, unsigned(r)); std::fflush(stdout);
    if (r != sl::Result::eOk) ExitProcess(3);
}
static void Log(sl::LogType, const char* message) { std::printf("SL: %s\n", message); std::fflush(stdout); }
int wmain(int argc, wchar_t** argv) {
    if (argc != 3 && argc != 4) { std::puts("SidecarSmoke <game directory> <DX12 runtime directory> [--dual]"); return 1; }
    const std::filesystem::path game(argv[1]), runtime(argv[2]);
    auto proxy = LoadLibraryW((game / L"version.dll").c_str());
    std::printf("SM86 proxy loaded: %d\n", proxy != nullptr);
    auto module = LoadLibraryW((runtime / L"sl.interposer.dll").c_str());
    if (!module) return 2;
    auto init = reinterpret_cast<PFun_slInit*>(GetProcAddress(module, "slInit"));
    auto bind = reinterpret_cast<PFun_slSetD3DDevice*>(GetProcAddress(module, "slSetD3DDevice"));
    auto upgrade = reinterpret_cast<PFun_slUpgradeInterface*>(GetProcAddress(module, "slUpgradeInterface"));
    auto shutdown = reinterpret_cast<PFun_slShutdown*>(GetProcAddress(module, "slShutdown"));
    if (!init || !bind || !upgrade || !shutdown) return 2;
    const auto path = runtime.wstring();
    const wchar_t* paths[] = { path.c_str() };
    sl::Feature features[] = { sl::kFeatureDLSS_G, sl::kFeatureReflex, sl::kFeaturePCL };
    sl::Preferences prefs{};
    prefs.renderAPI = sl::RenderAPI::eD3D12;
    prefs.flags = sl::PreferenceFlags::eUseManualHooking | sl::PreferenceFlags::eUseFrameBasedResourceTagging;
    prefs.pathsToPlugins = paths; prefs.numPathsToPlugins = 1;
    prefs.featuresToLoad = features; prefs.numFeaturesToLoad = 3;
    prefs.engine = sl::EngineType::eCustom; prefs.engineVersion = "1.0.0";
    prefs.projectId = "f8776929-c969-43bd-ac2b-294b4de58aac";
    prefs.logLevel = sl::LogLevel::eVerbose; prefs.logMessageCallback = Log;
    HMODULE dx11Module{};
    PFun_slSetD3DDevice* bind11{};
    const auto dx11Path = (game / L"Data/Shaders/ImageReconstruction/Streamline").wstring();
    const wchar_t* dx11Paths[] = { dx11Path.c_str() };
    sl::Feature dx11Features[] = { sl::kFeatureDLSS, sl::kFeatureReflex, sl::kFeaturePCL };
    if (argc == 4) {
        if (std::wstring_view(argv[3]) != L"--dual") return 1;
        dx11Module = LoadLibraryW((std::filesystem::path(dx11Path) / L"sl.interposer.dll").c_str());
        auto init11 = reinterpret_cast<PFun_slInit*>(GetProcAddress(dx11Module, "slInit"));
        bind11 = reinterpret_cast<PFun_slSetD3DDevice*>(GetProcAddress(dx11Module, "slSetD3DDevice"));
        if (!init11 || !bind11) return 2;
        auto prefs11 = prefs;
        prefs11.renderAPI = sl::RenderAPI::eD3D11;
        prefs11.pathsToPlugins = dx11Paths; prefs11.featuresToLoad = dx11Features;
        CheckSL(init11(prefs11, sl::kSDKVersion), "DX11 slInit");
    }
    CheckSL(init(prefs, sl::kSDKVersion), "slInit");
    ComPtr<IDXGIFactory4> factory;
    Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "factory");
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i=0; SUCCEEDED(factory->EnumAdapters1(i, &adapter)); ++i) {
        DXGI_ADAPTER_DESC1 desc{}; adapter->GetDesc1(&desc);
        if (desc.VendorId == 0x10de) break;
        adapter.Reset();
    }
    if (!adapter) return 2;
    ComPtr<ID3D12Device> device;
    Check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)), "device");
    CheckSL(bind(device.Get()), "bind");
    ComPtr<ID3D12Device> deviceProxy = device;
    ID3D12Device* upgraded = deviceProxy.Detach();
    CheckSL(upgrade(reinterpret_cast<void**>(&upgraded)), "upgrade device");
    deviceProxy.Attach(upgraded);
    ComPtr<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC q{};
    Check(deviceProxy->CreateCommandQueue(&q, IID_PPV_ARGS(&queue)), "queue");
    ComPtr<ID3D11Device> device11;
    ComPtr<ID3D11DeviceContext> context11;
    if (dx11Module) {
        Check(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
            &device11, nullptr, &context11), "DX11 device");
        CheckSL(bind11(device11.Get()), "DX11 bind");
    }
    IDXGIFactory4* upgradedFactory = factory.Detach();
    CheckSL(upgrade(reinterpret_cast<void**>(&upgradedFactory)), "upgrade factory");
    factory.Attach(upgradedFactory);
    WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"PIXL.SidecarSmoke";
    RegisterClassW(&wc);
    HWND window = CreateWindowW(wc.lpszClassName, L"PIXL sidecar diagnostic", WS_OVERLAPPEDWINDOW, 0, 0, 640, 480, nullptr, nullptr, wc.hInstance, nullptr);
    if (!window) return 2;
    DXGI_SWAP_CHAIN_DESC1 desc{}; desc.Width=640; desc.Height=480; desc.Format=DXGI_FORMAT_R10G10B10A2_UNORM;
    desc.SampleDesc.Count=1; desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount=3; desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
    ComPtr<IDXGISwapChain1> swap;
    std::puts("Calling CreateSwapChainForHwnd"); std::fflush(stdout);
    Check(factory->CreateSwapChainForHwnd(queue.Get(),window,&desc,nullptr,nullptr,&swap), "swap chain");
    auto getFunction = reinterpret_cast<PFun_slGetFeatureFunction*>(GetProcAddress(module, "slGetFeatureFunction"));
    PFun_slDLSSGGetState* getState{};
    if (!getFunction) return 2;
    CheckSL(getFunction(sl::kFeatureDLSS_G, "slDLSSGGetState", reinterpret_cast<void*&>(getState)), "get state function");
    PFun_slReflexGetState* getReflexState{};
    PFun_slReflexSetOptions* setReflexOptions{};
    PFun_slReflexSleep* reflexSleep{};
    auto getToken = reinterpret_cast<PFun_slGetNewFrameToken*>(GetProcAddress(module, "slGetNewFrameToken"));
    if (!getToken) return 2;
    CheckSL(getFunction(sl::kFeatureReflex, "slReflexGetState", reinterpret_cast<void*&>(getReflexState)), "Reflex get-state bind");
    CheckSL(getFunction(sl::kFeatureReflex, "slReflexSetOptions", reinterpret_cast<void*&>(setReflexOptions)), "Reflex options bind");
    CheckSL(getFunction(sl::kFeatureReflex, "slReflexSleep", reinterpret_cast<void*&>(reflexSleep)), "Reflex sleep bind");
    sl::ReflexState reflexState{};
    CheckSL(getReflexState(reflexState), "Reflex state");
    std::printf("Reflex low latency available: %d\n", reflexState.lowLatencyAvailable);
    if (!reflexState.lowLatencyAvailable) return 3;
    for (int i=0;i<3;++i) {
        sl::ReflexOptions reflexOptions{};
        reflexOptions.mode = i == 0 ? sl::ReflexMode::eLowLatency :
            (i == 1 ? sl::ReflexMode::eLowLatencyWithBoost : sl::ReflexMode::eOff);
        reflexOptions.frameLimitUs = i == 1 ? 16667u : 0u;
        CheckSL(setReflexOptions(reflexOptions), "Reflex mode/boost/limiter options");
        sl::FrameToken* token{};
        CheckSL(getToken(token, nullptr), "Reflex frame token");
        CheckSL(reflexSleep(*token), "Reflex sleep");
        Check(swap->Present(0,0), "present (FG off)");
        sl::DLSSGState state{};
        CheckSL(getState(sl::ViewportHandle(0), state, nullptr), "present-thread state");
        std::printf("Runtime maximum generated frames: %u (%ux output)\n", state.numFramesToGenerateMax, state.numFramesToGenerateMax + 1u);
        if (state.inputsProcessingCompletionFence)
            Check(queue->Wait(static_cast<ID3D12Fence*>(state.inputsProcessingCompletionFence),
                state.lastPresentInputsProcessingCompletionFenceValue), "input completion wait");
    }
    CheckSL(shutdown(), "shutdown");
    if (dx11Module) {
        auto shutdown11 = reinterpret_cast<PFun_slShutdown*>(GetProcAddress(dx11Module, "slShutdown"));
        if (!shutdown11) return 2;
        CheckSL(shutdown11(), "DX11 shutdown");
    }
    swap.Reset(); DestroyWindow(window);
    std::puts("PASS: startup and FG-off present only; no interpolation or visual validation.");
}
