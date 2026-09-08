[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$VcVars)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$vc = (Resolve-Path -LiteralPath $VcVars).Path
$include = Join-Path $repo 'build\PIXL-12C\vcpkg_installed\x64-windows-static-md-release\include'
$output = Join-Path $repo ('build\landscape-settings-tests-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null
function Extract([string]$Source, [string]$Pattern) {
    $match = [regex]::Match($Source, $Pattern)
    if (-not $match.Success) { throw "Landscape extraction changed: $Pattern" }
    return $match.Value
}
$source = @'
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <nlohmann/json.hpp>
using json = nlohmann::json;
using uint = unsigned int;
int detectionCalls = 0, messages = 0;
bool pluginPresent = false;
void* GetModuleHandleW(const wchar_t*) { ++detectionCalls; return pluginPresent ? &detectionCalls : nullptr; }
namespace logger { template<class... T> void info(const char*, T&&...) { ++messages; } }
struct HorizonBlend { bool loaded = true; std::string failedLoadedMessage; void PostPostLoad(); };
'@
foreach ($module in @('DistanceBlend', 'TerrainDetail')) {
    $header = Get-Content -LiteralPath (Join-Path $repo "engine\Modules\$module.h") -Raw
    $impl = Get-Content -LiteralPath (Join-Path $repo "engine\Modules\$module.cpp") -Raw
    $settings = Extract $header '(?s)struct (?:alignas\(16\) )?Settings.*?\r?\n\t\};'
    $serialization = Extract $impl '(?s)NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT\(.*?\)'
    $source += "`r`nstruct $module {`r`n$settings`r`nSettings settings; void LoadSettings(json&); void SaveSettings(json&); void RestoreDefaultSettings(); };`r`n$serialization`r`n"
    foreach ($function in @('LoadSettings', 'SaveSettings', 'RestoreDefaultSettings')) {
        $source += (Extract $impl ('(?s)void ' + $module + '::' + $function + '\(.*?\r?\n\}')) + "`r`n"
    }
}
$horizon = Get-Content -LiteralPath (Join-Path $repo 'engine\Modules\HorizonBlend.cpp') -Raw
$source += (Extract $horizon '(?s)void HorizonBlend::PostPostLoad\(.*?\r?\n\}') + @'

int main() {
    static_assert(sizeof(DistanceBlend::Settings) == 32);
    static_assert(offsetof(DistanceBlend::Settings, DisableTerrainVertexColors) == 12);
    static_assert(offsetof(DistanceBlend::Settings, LODTerrainGamma) == 16);
    static_assert(sizeof(TerrainDetail::Settings) == 16);
    DistanceBlend distance;
    json input = { {"LODTerrainBrightness", -1}, {"LODObjectBrightness", 100}, {"LODObjectSnowBrightness", 0.5},
        {"LODTerrainGamma", 0}, {"LODObjectGamma", 99}, {"LODObjectSnowGamma", 1.2f}, {"DisableTerrainVertexColors", 99u} };
    distance.LoadSettings(input);
    assert(distance.settings.LODTerrainBrightness == 0.01f && distance.settings.LODObjectBrightness == 5.f);
    assert(distance.settings.LODObjectSnowBrightness == 0.5f);
    assert(distance.settings.LODTerrainGamma == 0.1f && distance.settings.LODObjectGamma == 3.f);
    assert(distance.settings.LODObjectSnowGamma == 1.2f && distance.settings.DisableTerrainVertexColors == 1);
    assert(distance.settings.pad == 0);
    for (const float invalid : {std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        input = {{"LODTerrainGamma", invalid}};
        distance.LoadSettings(input);
        assert(distance.settings.LODTerrainGamma == 1.f);
    }
    input = {{"LODTerrainBrightness", 1.25}, {"LODObjectGamma", 1.7f}};
    distance.LoadSettings(input);
    json saved, roundTrip;
    distance.SaveSettings(saved);
    DistanceBlend other;
    other.LoadSettings(saved); other.SaveSettings(roundTrip);
    assert(saved == roundTrip && distance.settings.LODTerrainBrightness == 1.25f);
    distance.RestoreDefaultSettings();
    assert(distance.settings.LODTerrainGamma == 1 && distance.settings.LODTerrainBrightness == 1);
    TerrainDetail terrain;
    for (const uint value : {0u, 1u, 42u}) {
        input = {{"enableLODTerrainTilingFix", value}};
        terrain.LoadSettings(input);
        assert(terrain.settings.enableLODTerrainTilingFix == (value != 0 ? 1u : 0u));
        terrain.SaveSettings(saved); terrain.LoadSettings(saved); terrain.SaveSettings(roundTrip);
        assert(saved == roundTrip);
    }
    input = json::object(); terrain.LoadSettings(input);
    assert(terrain.settings.enableLODTerrainTilingFix == 1);
    HorizonBlend horizon;
    horizon.PostPostLoad();
    assert(!horizon.loaded && !horizon.failedLoadedMessage.empty() && detectionCalls == 1 && messages == 1);
    horizon.PostPostLoad();
    assert(detectionCalls == 1 && messages == 1);
    pluginPresent = true; horizon.loaded = true; horizon.PostPostLoad();
    assert(horizon.loaded && detectionCalls == 2 && messages == 2);
    std::cout << "PASS exact landscape settings/JSON: ABI, bounded and nonfinite values, toggle normalization, round trip/defaults, optional-plugin detection gate\n";
}
'@
$sourcePath = Join-Path $output 'landscape-tests.cpp'
$source | Set-Content -LiteralPath $sourcePath -Encoding UTF8
Push-Location $output
try {
    $command = 'call "{0}" >nul && cl.exe /nologo /std:c++latest /EHsc /W4 /WX /MD /O2 /fp:fast /I"{1}" "{2}" /Fe:landscape-tests.exe && landscape-tests.exe' -f $vc, $include, $sourcePath
    $ErrorActionPreference = 'Continue'
    & cmd.exe /d /c $command 2>&1 | Tee-Object -FilePath (Join-Path $output 'test.log')
    $result = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($result -ne 0) { throw "Landscape settings tests failed ($result)." }
} finally { $ErrorActionPreference = 'Stop'; Pop-Location }
Write-Host "Landscape test artifacts: $output"
