[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$VcVars)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$vcVarsPath = (Resolve-Path -LiteralPath $VcVars).Path
$include = Join-Path $repo 'build\PIXL-12C\vcpkg_installed\x64-windows-static-md-release\include'
if (-not (Test-Path -LiteralPath (Join-Path $include 'nlohmann\json.hpp'))) { throw 'Configure PIXL-12C dependencies first.' }
$output = Join-Path $repo ('build\thin-settings-tests-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null
$header = Get-Content -LiteralPath (Join-Path $repo 'engine\Modules\ThinSurface.h') -Raw
$impl = Get-Content -LiteralPath (Join-Path $repo 'engine\Modules\ThinSurface.cpp') -Raw
function Extract([string]$Source, [string]$Pattern) {
    $match = [regex]::Match($Source, $Pattern)
    if (-not $match.Success) { throw "Thin Surface extraction contract changed: $Pattern" }
    return $match.Value
}
$enum = Extract $header '(?s)enum MaterialModel : uint32_t.*?\r?\n\t\};'
$frame = Extract $header '(?s)struct alignas\(16\) PerFrame.*?\r?\n\t\};'
$settings = Extract $header '(?s)struct Settings : PerFrame.*?\r?\n\t\};'
$serialization = Extract $impl '(?s)NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT\(.*?\);'
$functions = @('LoadSettings', 'SaveSettings', 'RestoreDefaultSettings') | ForEach-Object {
    Extract $impl ('(?s)void ThinSurface::' + $_ + '\([^)]*\).*?\r?\n\}')
}
$prefix = @'
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <nlohmann/json.hpp>
using json = nlohmann::json;
'@
$test = @'
int main() {
    static_assert(sizeof(ThinSurface::PerFrame) == 16);
    static_assert(offsetof(ThinSurface::PerFrame, AlphaStrength) == 12);
    ThinSurface material;
    json defaults;
    material.SaveSettings(defaults);
    json input = {{"AlphaMode", 999u}, {"AlphaReduction", -10.0}, {"AlphaSoftness", 2.0}, {"AlphaStrength", 5.0}, {"SkinnedOnly", false}};
    material.LoadSettings(input);
    assert(material.settings.AlphaMode == ThinSurface::Disabled);
    assert(material.settings.AlphaReduction == 0);
    assert(material.settings.AlphaSoftness == 1);
    assert(material.settings.AlphaStrength == 1);
    assert(!material.settings.SkinnedOnly);
    for (unsigned mode = 0; mode <= 3; ++mode) {
        input = {{"AlphaMode", mode}, {"AlphaReduction", 0.2f}, {"AlphaSoftness", 0.13f}, {"AlphaStrength", 0.35f}};
        material.LoadSettings(input);
        assert(material.settings.AlphaMode == mode);
        assert(material.settings.AlphaReduction == 0.2f);
        assert(material.settings.AlphaSoftness == 0.13f);
        assert(material.settings.AlphaStrength == 0.35f); // Preserve inverse blend semantics.
        json saved, reSaved;
        material.SaveSettings(saved);
        ThinSurface roundTrip;
        roundTrip.LoadSettings(saved);
        roundTrip.SaveSettings(reSaved);
        assert(saved == reSaved);
    }
    input = json::object();
    material.LoadSettings(input);
    json reset;
    material.SaveSettings(reset);
    assert(reset == defaults);
    material.settings.AlphaSoftness = 0.6f;
    material.RestoreDefaultSettings();
    material.SaveSettings(reset);
    assert(reset == defaults);
    std::cout << "PASS exact Thin Surface structs/JSON/functions: 16-byte ABI, fail-closed invalid mode, numeric bounds, valid values/semantics, round-trip and defaults\n";
}
'@
$sourcePath = Join-Path $output 'thin-settings-tests.cpp'
($prefix + "`r`nstruct ThinSurface {`r`n" + $enum + "`r`n" + $frame + "`r`n" + $settings + "`r`nSettings settings; void LoadSettings(json&); void SaveSettings(json&); void RestoreDefaultSettings();`r`n};`r`n" + $serialization + "`r`n" + ($functions -join "`r`n") + "`r`n" + $test) | Set-Content -LiteralPath $sourcePath -Encoding UTF8
Push-Location $output
try {
    $command = 'call "{0}" >nul && cl.exe /nologo /std:c++latest /EHsc /W4 /WX /MD /O2 /fp:fast /I"{1}" "{2}" /Fe:thin-settings-tests.exe && thin-settings-tests.exe' -f $vcVarsPath, $include, $sourcePath
    & cmd.exe /d /c $command
    if ($LASTEXITCODE -ne 0) { throw "Thin Surface settings tests failed ($LASTEXITCODE)." }
} finally { Pop-Location }
$locale = Get-Content -LiteralPath (Join-Path $repo 'distribution\SKSE\Plugins\PIXLRenderer\Translations\en.json') -Raw | ConvertFrom-Json
if (-not $locale) { throw 'Invalid English localization payload.' }
Write-Host "Exact-source test artifacts: $output"
