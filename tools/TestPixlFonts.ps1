[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$VcVars)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$vc = (Resolve-Path -LiteralPath $VcVars).Path
$dependencies = Join-Path $repo 'build\PIXL-12C\vcpkg_installed\x64-windows-static-md-release'
$output = Join-Path $repo ('build\font-tests-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null
$fonts = Get-Content -LiteralPath (Join-Path $repo 'engine\Menu\Fonts.cpp') -Raw
$themes = Get-Content -LiteralPath (Join-Path $repo 'engine\Menu\ThemeManager.cpp') -Raw
$blur = Get-Content -LiteralPath (Join-Path $repo 'engine\Menu\BackgroundBlur.cpp') -Raw
function Extract([string]$Source, [string]$Pattern) {
    $match = [regex]::Match($Source, $Pattern)
    if (-not $match.Success) { throw "Font test extraction contract changed: $Pattern" }
    return $match.Value
}
$roles = Extract $fonts '(?s)namespace MenuFonts\s*\{.*?(?=\r?\n\tconst FontRoleSettings& GetDefaultRole\()'
$lower = Extract $fonts '(?s)std::string ToLowerCopy\(.*?\r?\n\t\t\}'
$rank = Extract $fonts '(?s)int StyleRank\(.*?\r?\n\t\t\}'
$reloadPrefix = Extract $themes '(?s)bool ThemeManager::ReloadFont\(.*?(?=\r?\n\t// Clear existing fonts)'
$rangeStorage = Extract $themes '(?s)struct SupplementalGlyphMerge.*?supplementalGlyphMerges.clear\(\);'
$visibleArea = Extract $blur '(?s)bool HasVisibleWindowArea\(.*?\r?\n\t\t\}'
if ($rangeStorage -notmatch 'static std::vector<SupplementalGlyphMerge>') { throw 'Glyph ranges must outlive ReloadFont.' }
if ($themes.IndexOf('io.Fonts->Clear();') -gt $themes.IndexOf('supplementalGlyphMerges.clear();')) { throw 'Clear atlas before releasing its range storage.' }
$prefix = @'
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <imgui.h>
#include <imgui_internal.h>
using UINT = unsigned int;
struct Menu {
    enum class FontRole { Body, Heading, Count };
    struct ThemeSettings {
        struct FontRoleSettings { std::string Family, Style, File; float SizeScale = 1.f; };
        std::string FontName = "Jost/Jost-Regular.ttf";
        std::array<FontRoleSettings, 2> FontRoles = {{
            {"Jost", "Regular", "Jost/Jost-Regular.ttf", 1.f},
            {"Jost", "Regular", "Jost/Jost-Regular.ttf", 1.f}}};
    } theme;
    const ThemeSettings& GetTheme() const { return theme; }
};
namespace MenuFonts { using FontRoleSettings = Menu::ThemeSettings::FontRoleSettings; }
namespace logger { template<class... T> void error(const char*, T&&...) {} }
namespace globals::d3d { inline void* device = nullptr; inline void* context = nullptr; }
struct ThemeManager { static bool ReloadFont(const Menu&, float&); };
'@
$tests = @'
int main(int argc, char** argv) {
    assert(argc == 2);
    assert(HasVisibleWindowArea({0, 0}, {100, 100}, 1920, 1080));
    assert(HasVisibleWindowArea({-50, -50}, {10, 10}, 1920, 1080));
    assert(!HasVisibleWindowArea({-50, -50}, {0, 0}, 1920, 1080));
    assert(!HasVisibleWindowArea({1920, 0}, {2000, 100}, 1920, 1080));
    assert(!HasVisibleWindowArea({0, 1080}, {100, 1200}, 1920, 1080));
    assert(!HasVisibleWindowArea({50, 50}, {40, 40}, 1920, 1080));
    assert(!HasVisibleWindowArea({0, 0}, {100, 100}, 0, 0));
    Menu menu;
    auto& theme = menu.theme;
    theme.FontName = "Sanguis/Sanguis-Bold.ttf";
    MenuFonts::NormalizeFontRoles(theme, false);
    assert(theme.FontRoles[0].Family == "Sanguis");
    assert(theme.FontRoles[0].Style == "Bold");
    assert(theme.FontRoles[0].File == theme.FontName);
    assert(theme.FontRoles[1].Family == "Jost");
    theme.FontRoles[0] = {"", "", "Custom/Custom-Italic.ttf", 8.f};
    MenuFonts::NormalizeFontRoles(theme, true);
    assert(theme.FontRoles[0].Family == "Custom");
    assert(theme.FontRoles[0].Style == "Italic");
    assert(theme.FontRoles[0].SizeScale == 4.f);
    theme.FontRoles[0] = {"Explicit", "Label", "Custom/File.ttf", 1.f};
    MenuFonts::NormalizeFontRoles(theme, true);
    assert(theme.FontRoles[0].Family == "Explicit" && theme.FontRoles[0].Style == "Label");
    theme.FontRoles[0] = {};
    MenuFonts::NormalizeFontRoles(theme, true);
    assert(theme.FontRoles[0].File == "Jost/Jost-Regular.ttf");
    assert(StyleRank("ExtraBold") == 7 && StyleRank("Extra Bold") == 7);
    assert(StyleRank("Bold") == 6 && StyleRank("SemiBold") == 5);
    assert(StyleRank("ExtraBold Italic") == 8);
    assert(ToLowerCopy(".TTF") == ".ttf");
    for (unsigned i = 0; i < 256; ++i) {
        const auto byte = static_cast<unsigned char>(i);
        const auto result = ToLowerCopy(std::string(1, static_cast<char>(byte)));
        assert(result[0] == static_cast<char>(std::tolower(byte)));
    }
    float size = 0;
    assert(!ThemeManager::ReloadFont(menu, size)); // Must not call GetIO without a context.
    ImGui::CreateContext();
    assert(!ThemeManager::ReloadFont(menu, size)); // Null D3D prerequisites.
    globals::d3d::device = &size;
    globals::d3d::context = &size;
    ImGui::GetCurrentContext()->WithinFrameScope = true;
    assert(!ThemeManager::ReloadFont(menu, size));
    ImGui::GetCurrentContext()->WithinFrameScope = false;
    assert(ThemeManager::ReloadFont(menu, size));
    assert(ThemeManager::ReloadFont(menu, size)); // RAII reentrancy flag released.
    for (int i = 0; i < 20; ++i) {
        ImGui::GetIO().Fonts->Clear();
        AddRetainedRangeFont(argv[1]);
        std::vector<std::string> churn(1000, std::string(500, 'x'));
        assert(!churn.empty());
        assert(ImGui::GetIO().Fonts->Build());
        assert(ImGui::GetIO().Fonts->Fonts[0]->IsGlyphInFont('A'));
    }
    ImGui::DestroyContext();
    std::cout << "PASS exact-source font normalization/ranking, all-byte case conversion, reload preconditions, retained-range storage with 20 real ImGui atlas builds\n";
}
'@
$rangeTest = "void AddRetainedRangeFont(const char* path) {`r`n" + $rangeStorage + @'

    SupplementalGlyphMerge merge;
    ImFontGlyphRangesBuilder builder;
    builder.AddText("ABC");
    builder.BuildRanges(&merge.glyphRanges);
    supplementalGlyphMerges.push_back(std::move(merge));
    auto* font = ImGui::GetIO().Fonts->AddFontFromFileTTF(path, 21.f, nullptr, supplementalGlyphMerges[0].glyphRanges.Data);
    assert(font);
}
'@
$source = $prefix + "`r`n" + $roles + "`r`n}`r`n" + $lower + "`r`n" + $rank + "`r`n" + $visibleArea + "`r`n" + $reloadPrefix + "`r`n(void)themeSettings; (void)cachedFontSize; return true; }`r`n" + $rangeTest + "`r`n" + $tests
$sourcePath = Join-Path $output 'font-tests.cpp'
$source | Set-Content -LiteralPath $sourcePath -Encoding UTF8
$fontPath = Join-Path $repo 'distribution\Interface\PIXLRenderer\Fonts\Jost\Jost-Regular.ttf'
if (-not (Test-Path -LiteralPath $fontPath)) { throw "Missing fixture font: $fontPath" }
Push-Location $output
try {
    $command = 'call "{0}" >nul && cl.exe /nologo /std:c++latest /EHsc /W4 /WX /MD /O2 /fp:fast /I"{1}\include" "{2}" /Fe:font-tests.exe /link /LIBPATH:"{1}\lib" imgui.lib user32.lib imm32.lib && font-tests.exe "{3}"' -f $vc, $dependencies, $sourcePath, $fontPath
    $ErrorActionPreference = 'Continue'
    & cmd.exe /d /c $command 2>&1 | Tee-Object -FilePath (Join-Path $output 'test.log')
    $result = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($result -ne 0) { throw "Font tests failed ($result)." }
} finally { $ErrorActionPreference = 'Stop'; Pop-Location }
Write-Host "Font test artifacts: $output"
