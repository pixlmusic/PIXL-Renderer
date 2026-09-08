[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$VcVars)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$vcVarsPath = (Resolve-Path -LiteralPath $VcVars).Path
$output = Join-Path $repo ('build\rcas-tests-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null
# Compile the actual RCAS declarations, implementation and caller. D3D/game
# services are recording stubs: this tests control flow, not GPU execution.
$header = Get-Content -LiteralPath (Join-Path $repo 'engine\Modules\ImageReconstruction\RCAS\RCAS.h') -Raw
$impl = Get-Content -LiteralPath (Join-Path $repo 'engine\Modules\ImageReconstruction\RCAS\RCAS.cpp') -Raw
$header = [regex]::Replace($header, '(?m)^#(?:include|pragma)[^\r\n]*', '')
$impl = [regex]::Replace($impl, '(?m)^#include[^\r\n]*', '')
$callerSource = Get-Content -LiteralPath (Join-Path $repo 'engine\Modules\ImageReconstruction.cpp') -Raw
$caller = [regex]::Match($callerSource, '(?s)void ImageReconstruction::ApplySharpening\(\).*?\r?\n\}')
if (-not $caller.Success) { throw 'RCAS caller extraction contract changed.' }
$prefix = @'
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>
#define ZoneScoped
#define TracyD3D11Zone(...)
#define ARRAYSIZE(a) static_cast<unsigned>(sizeof(a) / sizeof((a)[0]))
struct float2 { float x = 0, y = 0; };
struct ID3D11ComputeShader {};
struct ID3D11ShaderResourceView {};
struct ID3D11UnorderedAccessView {};
struct ID3D11Buffer {};
struct Texture {};
namespace winrt {
template<class T> struct com_ptr {
    T* value = nullptr;
    void attach(T* p) { value = p; }
    T* get() const { return value; }
    explicit operator bool() const { return value != nullptr; }
};
}
int warnings = 0, dispatches = 0, copies = 0, profilerDepth = 0, eventDepth = 0;
float uploadedSharpness = 0;
uint32_t uploadedConfidence = 0;
bool compileAvailable = true;
namespace logger {
void info(const char*) {}
void warn(const char*) { ++warnings; }
}
namespace Util {
void* CompileShader(const wchar_t*, const std::vector<std::pair<const char*, const char*>>&, const char*) {
    static ID3D11ComputeShader shader;
    return compileAvailable ? &shader : nullptr;
}
float2 ConvertToDynamic(float2 dims) { return {dims.x * 0.5f, dims.y * 0.5f}; }
}
template<class T> int ConstantBufferDesc() { return sizeof(T); }
struct ConstantBuffer {
    explicit ConstantBuffer(int) {}
    template<class T> void Update(const T& config) {
        uploadedSharpness = config.sharpness;
        uploadedConfidence = config.useConfidence;
    }
    ID3D11Buffer* CB() { static ID3D11Buffer buffer; return &buffer; }
};
struct State {
    void BeginPerfEvent(const char*) { ++eventDepth; }
    void EndPerfEvent() { --eventDepth; }
};
struct Profiler {
    void BeginPass(const char*) { ++profilerDepth; }
    void EndPass() { --profilerDepth; }
};
struct Context {
    void CSSetShader(ID3D11ComputeShader*, void*, unsigned) {}
    void CSSetConstantBuffers(unsigned, unsigned, ID3D11Buffer**) {}
    void CSSetShaderResources(unsigned, unsigned, ID3D11ShaderResourceView**) {}
    void CSSetUnorderedAccessViews(unsigned, unsigned, ID3D11UnorderedAccessView**, void*) {}
    void Dispatch(unsigned x, unsigned y, unsigned z) { assert(x == 3 && y == 2 && z == 1); ++dispatches; }
    void OMSetRenderTargets(unsigned, void*, void*) {}
    void CopyResource(Texture* dst, Texture* src) { assert(dst && src && dst != src); ++copies; }
};
struct GraphicsState { unsigned screenWidth = 17, screenHeight = 9; };
namespace RE {
enum RENDER_TARGETS { kMAIN };
namespace BSGraphics { enum ShaderFlags { DIRTY_RENDERTARGET }; }
}
struct Flags { void set(RE::BSGraphics::ShaderFlags) {} };
struct Target { Texture* texture = nullptr; ID3D11UnorderedAccessView* UAV = nullptr; };
struct Renderer {
    Target renderTargets[1];
    Renderer& GetRuntimeData() { return *this; }
};
namespace globals {
State stateStorage; State* state = &stateStorage;
Profiler profilerStorage; Profiler* profiler = &profilerStorage;
namespace d3d { Context storage; Context* context = &storage; }
namespace game {
GraphicsState graphicsStorage; GraphicsState* graphicsState = &graphicsStorage;
Renderer rendererStorage; Renderer* renderer = &rendererStorage;
Flags flagStorage; Flags* stateUpdateFlags = &flagStorage;
}
}
'@
$callerTypes = @'
struct Texture2D {
    winrt::com_ptr<ID3D11ShaderResourceView> srv;
    winrt::com_ptr<Texture> resource;
};
struct ImageReconstruction {
    struct Settings { bool sharpnessEnabledDLSS = true; float sharpnessDLSS = 0.5f; } settings;
    Texture2D* sharpenerTexture = nullptr;
    Texture2D* reactiveMaskTexture = nullptr;
    Texture2D* transparencyCompositionMaskTexture = nullptr;
    Texture2D* motionVectorCopyTexture = nullptr;
    RCAS rcas;
    void ApplySharpening();
};
'@
$tests = @'
int main() {
    ID3D11ShaderResourceView srv;
    ID3D11UnorderedAccessView uav;
    Texture src, dst;
    Texture2D input;
    input.srv.attach(&srv); input.resource.attach(&src);
    auto& target = globals::game::renderer->renderTargets[0];
    target.texture = &dst; target.UAV = &uav;
    {
        ImageReconstruction failed;
        failed.sharpenerTexture = &input;
        failed.ApplySharpening(); // Not initialized: must preserve reconstructed frame.
        assert(copies == 1 && dispatches == 0);
        compileAvailable = false;
        failed.rcas.Initialize();
        for (int i = 0; i < 10; ++i) failed.ApplySharpening();
        assert(copies == 11 && dispatches == 0 && warnings == 1);
    }
    compileAvailable = true;
    ImageReconstruction good;
    good.sharpenerTexture = &input;
    good.rcas.Initialize(); good.rcas.Initialize();
    good.ApplySharpening();
    assert(dispatches == 1 && copies == 11 && uploadedSharpness == 0.5f);
    good.settings.sharpnessEnabledDLSS = false;
    good.ApplySharpening(); assert(copies == 12);
    good.settings.sharpnessEnabledDLSS = true;
    good.settings.sharpnessDLSS = 0;
    good.ApplySharpening(); assert(copies == 13);
    good.settings.sharpnessDLSS = 0.5f;
    target.UAV = nullptr;
    good.ApplySharpening(); assert(copies == 14);
    target.UAV = &uav;
    good.settings.sharpnessDLSS = std::numeric_limits<float>::quiet_NaN();
    good.ApplySharpening(); assert(copies == 15);
    good.settings.sharpnessDLSS = std::numeric_limits<float>::infinity();
    good.ApplySharpening(); assert(copies == 16);
    assert(!good.rcas.ApplySharpen(nullptr, &uav, 1));
    assert(!good.rcas.ApplySharpen(&srv, nullptr, 1));
    assert(!good.rcas.ApplySharpen(&srv, &uav, std::numeric_limits<float>::quiet_NaN()));
    assert(good.rcas.ApplySharpen(&srv, &uav, 3));
    assert(uploadedSharpness == 1 && uploadedConfidence == 0);
    assert(good.rcas.ApplySharpen(&srv, &uav, -1));
    assert(uploadedSharpness == 0);
    assert(good.rcas.ApplySharpen(&srv, &uav, 1, &srv, &srv, &srv, {8, 4}));
    assert(uploadedConfidence == 1);
    assert(good.rcas.ApplySharpen(&srv, &uav, 1, &srv, nullptr, &srv, {8, 4}));
    assert(uploadedConfidence == 0);
    assert(good.rcas.ApplySharpen(&srv, &uav, 1, &srv, &srv, &srv, {8, std::numeric_limits<float>::infinity()}));
    assert(uploadedConfidence == 0);
    assert(profilerDepth == 0 && eventDepth == 0 && warnings == 1);
    std::cout << "PASS exact RCAS/caller: missing shader fallback, no per-frame warning, successful dispatch, disabled/zero/no-UAV resolve, numeric bounds and confidence inputs\n";
}
'@
$sourcePath = Join-Path $output 'rcas-tests.cpp'
($prefix + "`r`n" + $header + "`r`n" + $impl + "`r`n" + $callerTypes + "`r`n" + $caller.Value + "`r`n" + $tests) | Set-Content -LiteralPath $sourcePath -Encoding UTF8
Push-Location $output
try {
    $command = 'call "{0}" >nul && cl.exe /nologo /std:c++latest /EHsc /W4 /WX /MD /O2 /fp:fast "{1}" /Fe:rcas-tests.exe && rcas-tests.exe' -f $vcVarsPath, $sourcePath
    & cmd.exe /d /c $command
    if ($LASTEXITCODE -ne 0) { throw "Native RCAS regression tests failed ($LASTEXITCODE)." }
} finally { Pop-Location }
Write-Host "Exact-source test artifacts: $output"
