[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$VcVars)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$vc = (Resolve-Path -LiteralPath $VcVars).Path
$include = Join-Path $repo 'build\PIXL-12C\vcpkg_installed\x64-windows-static-md-release\include'
$output = Join-Path $repo ('build\icon-tests-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $output 'Themes\Custom') -Force | Out-Null
New-Item -ItemType File -Path (Join-Path $output 'Themes\Custom\PIXL-Mark.png') | Out-Null
$impl = Get-Content -LiteralPath (Join-Path $repo 'engine\Menu\IconLoader.cpp') -Raw
function Extract([string]$Pattern) {
    $match = [regex]::Match($impl, $Pattern)
    if (-not $match.Success) { throw "Icon extraction changed: $Pattern" }
    return $match.Value
}
$loader = Extract '(?s)bool LoadTextureFromFile\(.*?\r?\n\t\}'
$definition = Extract '(?s)struct IconDefinition.*?\r?\n\t\};'
$functions = @('GetIconDefinitions', 'LoadThemeSpecificIcons', 'InitializeMenuIcons') | ForEach-Object {
    Extract ('(?s)(?:std::vector<IconDefinition>|void|bool) ' + $_ + '\(.*?\r?\n\t\}')
}
$prefix = @'
#define NOMINMAX
#include <Windows.h>
#include <d3d11.h>
#include <winrt/base.h>
#include <imgui.h>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
namespace logger {
template<class... T> void warn(const char*, T&&...) {}
template<class... T> void debug(const char*, T&&...) {}
template<class... T> void info(const char*, T&&...) {}
template<class... T> void trace(const char*, T&&...) {}
}
namespace globals::d3d { ID3D11Device* device; ID3D11DeviceContext* context; }
struct Menu {
    struct Settings { std::string SelectedThemePreset = "Custom"; } settings;
    struct Icon { ID3D11ShaderResourceView* texture = nullptr; ImVec2 size{}; };
    struct { Icon logo, search; } uiIcons;
    Settings& GetSettings() { return settings; }
};
namespace Util {
void SetResourceName(ID3D11DeviceChild*, const char*, ...) {}
namespace PathHelpers {
std::filesystem::path fixture;
std::filesystem::path GetThemesPath() { return fixture / "Themes"; }
std::filesystem::path GetIconsPath() { return fixture / "Base"; }
}
namespace FileHelpers { std::string SanitizeFileName(const std::string& s) { return s; } }
}
// Controlled decoder adapter: test production upload/fallback, not the third-party decoder.
bool failTheme = false, failBase = false, oversizeProbe = false, oversizeDecode = false;
int allocations = 0, frees = 0;
int stbi_info(const char*, int* width, int* height, int* components) {
    *width = oversizeProbe ? 5000 : 2; *height = 2; *components = 4; return 1;
}
unsigned char* stbi_load(const char* path, int* width, int* height, int*, int) {
    const bool theme = std::string(path).find("Themes") != std::string::npos;
    if ((theme && failTheme) || (!theme && failBase)) return nullptr;
    *width = oversizeDecode ? 5000 : 2; *height = 2;
    auto* pixels = static_cast<unsigned char*>(std::malloc(16));
    assert(pixels); ++allocations;
    for (int i = 0; i < 4; ++i) {
        pixels[i * 4] = 10; pixels[i * 4 + 1] = 20; pixels[i * 4 + 2] = 30; pixels[i * 4 + 3] = 64;
    }
    return pixels;
}
void stbi_image_free(void* p) { ++frees; std::free(p); }
'@
$tests = @'
void VerifyPixels(ID3D11ShaderResourceView* srv, bool mask) {
    winrt::com_ptr<ID3D11Resource> resource;
    srv->GetResource(resource.put());
    auto texture = resource.as<ID3D11Texture2D>();
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    assert(desc.Width == 2 && desc.Height == 2 && desc.MipLevels == 2);
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; desc.MiscFlags = 0;
    winrt::com_ptr<ID3D11Texture2D> staging;
    assert(SUCCEEDED(globals::d3d::device->CreateTexture2D(&desc, nullptr, staging.put())));
    globals::d3d::context->CopyResource(staging.get(), texture.get());
    for (UINT mip = 0; mip < 2; ++mip) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        assert(SUCCEEDED(globals::d3d::context->Map(staging.get(), mip, D3D11_MAP_READ, 0, &mapped)));
        const auto* pixel = static_cast<const unsigned char*>(mapped.pData);
        assert(pixel[0] == (mask ? 255 : 10) && pixel[1] == (mask ? 255 : 20) && pixel[2] == (mask ? 255 : 30) && pixel[3] == 64);
        globals::d3d::context->Unmap(staging.get(), mip);
    }
}
int main(int argc, char** argv) {
    assert(argc == 2);
    Util::PathHelpers::fixture = argv[1];
    winrt::com_ptr<ID3D11Device> device;
    winrt::com_ptr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL level{};
    assert(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.put(), &level, context.put())));
    globals::d3d::device = device.get(); globals::d3d::context = context.get();
    ID3D11ShaderResourceView* failedOutput = reinterpret_cast<ID3D11ShaderResourceView*>(1);
    ImVec2 size{99,99};
    assert(!Util::LoadTextureFromFile(nullptr, "unit", &failedOutput, size, false));
    assert(!failedOutput && size.x == 0 && size.y == 0);
    assert(!Util::LoadTextureFromFile(device.get(), "unit", nullptr, size, false));
    for (const bool mask : {false, true}) {
        winrt::com_ptr<ID3D11ShaderResourceView> srv;
        assert(Util::LoadTextureFromFile(device.get(), "unit", srv.put(), size, mask));
        assert(size.x == 2 && size.y == 2);
        VerifyPixels(srv.get(), mask);
    }
    oversizeProbe = true;
    assert(!Util::LoadTextureFromFile(device.get(), "unit", &failedOutput, size, true));
    oversizeProbe = false; oversizeDecode = true;
    assert(!Util::LoadTextureFromFile(device.get(), "unit", &failedOutput, size, true));
    assert(!failedOutput && size.x == 0 && allocations == frees);
    oversizeDecode = false;
    Menu menu;
    assert(Util::LoadTextureFromFile(device.get(), "unit", &menu.uiIcons.logo.texture, menu.uiIcons.logo.size, true));
    auto* base = menu.uiIcons.logo.texture;
    failTheme = true;
    auto definitions = Util::IconLoader::GetIconDefinitions(&menu);
    Util::IconLoader::LoadThemeSpecificIcons(&menu, device.get(), definitions);
    assert(menu.uiIcons.logo.texture == base && menu.uiIcons.logo.size.x == 2);
    VerifyPixels(menu.uiIcons.logo.texture, true);
    failTheme = false;
    Util::IconLoader::LoadThemeSpecificIcons(&menu, device.get(), definitions);
    assert(menu.uiIcons.logo.texture && menu.uiIcons.logo.texture != base);
    VerifyPixels(menu.uiIcons.logo.texture, true);
    failBase = true;
    assert(Util::IconLoader::InitializeMenuIcons(&menu)); // Theme-only success.
    assert(menu.uiIcons.logo.texture);
    menu.uiIcons.logo.texture->Release(); menu.uiIcons.logo.texture = nullptr;
    failTheme = true;
    assert(!Util::IconLoader::InitializeMenuIcons(&menu));
    assert(!menu.uiIcons.logo.texture && allocations == frees);
    std::cout << "PASS exact icon loader with D3D11 WARP: RGBA/mask alpha + generated mips, failure outputs, post-decode bounds, preserved base icon and theme-only success\n";
}
'@
$source = $prefix + "`r`nnamespace Util {`r`n" + $loader + "`r`n}`r`nnamespace Util::IconLoader {`r`n" + $definition + "`r`n" + ($functions -join "`r`n") + "`r`n}`r`n" + $tests
$sourcePath = Join-Path $output 'icon-tests.cpp'
$source | Set-Content -LiteralPath $sourcePath -Encoding UTF8
Push-Location $output
try {
    $command = 'call "{0}" >nul && cl.exe /nologo /std:c++latest /EHsc /W4 /WX /MD /O2 /I"{1}" "{2}" /Fe:icon-tests.exe /link d3d11.lib dxgi.lib windowsapp.lib && icon-tests.exe "{3}"' -f $vc, $include, $sourcePath, $output
    $ErrorActionPreference = 'Continue'
    & cmd.exe /d /c $command 2>&1 | Tee-Object -FilePath (Join-Path $output 'test.log')
    $result = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($result -ne 0) { throw "Icon loader tests failed ($result)." }
} finally { $ErrorActionPreference = 'Stop'; Pop-Location }
Write-Host "Icon WARP test artifacts: $output"
