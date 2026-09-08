[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$VcVars)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$vcVarsPath = (Resolve-Path -LiteralPath $VcVars).Path
$include = Join-Path $repo 'build\PIXL-12C\vcpkg_installed\x64-windows-static-md-release\include'
if (-not (Test-Path -LiteralPath (Join-Path $include 'nlohmann\json.hpp'))) { throw 'Configure the PIXL-12C release dependencies first.' }
$output = Join-Path $repo ('build\foliage-settings-tests-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null
$header = Get-Content -LiteralPath (Join-Path $repo 'engine\Modules\FoliageDynamics.h') -Raw
$impl = Get-Content -LiteralPath (Join-Path $repo 'engine\Modules\FoliageDynamics.cpp') -Raw
function Extract([string]$Source, [string]$Pattern) {
    $match = [regex]::Match($Source, $Pattern)
    if (-not $match.Success) { throw "Foliage extraction contract changed: $Pattern" }
    return $match.Value
}
$settings = Extract $header '(?s)struct alignas\(16\) Settings.*?\r?\n\t\};'
$tuning = Extract $header '(?s)struct alignas\(16\) TuningSettings.*?\r?\n\t\};'
$magic = Extract $header 'static constexpr uint TuningMagic[^\r\n]+'
$version = Extract $header 'static constexpr uint TuningVersion[^\r\n]+'
$serialization = Extract $impl '(?s)NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT\(.*?(?=void FoliageDynamics::SetupResources)'
$functions = @('LoadSettings', 'SaveSettings', 'RestoreDefaultSettings') | ForEach-Object {
    Extract $impl ('(?s)void FoliageDynamics::' + $_ + '\([^)]*\).*?\r?\n\}')
}
$prefix = @'
#include <algorithm>
#include <cassert>
#include <iostream>
#include <nlohmann/json.hpp>
using json = nlohmann::json;
using uint = unsigned;
'@
$tests = @'
int main() {
    static_assert(sizeof(FoliageDynamics::Settings) == 80);
    static_assert(sizeof(FoliageDynamics::TuningSettings) == 96);
    FoliageDynamics foliage;
    json baseline;
    foliage.SaveSettings(baseline);
    json configured = baseline;
    configured["PIXLGrassTuning"]["GrassSaturation"] = 0.4f;
    configured["PIXLGrassTuning"]["GrassMirrorSpecularY"] = 9;
    foliage.LoadSettings(configured);
    assert(foliage.tuningSettings.GrassSaturation == 0.4f);
    assert(foliage.tuningSettings.GrassMirrorSpecularY == 1);
    json legacy = json::object();
    foliage.LoadSettings(legacy);
    json restored;
    foliage.SaveSettings(restored);
    assert(restored == baseline); // No stale tuning from a prior config.
    foliage.LoadSettings(configured);
    json invalidExtension = {{"PIXLGrassTuning", "not an object"}};
    foliage.LoadSettings(invalidExtension);
    foliage.SaveSettings(restored);
    assert(restored == baseline);
    configured["Glossiness"] = -5;
    configured["WindStrength"] = 9;
    configured["ComplexGrassMode"] = 500;
    configured["PIXLGrassTuning"]["GrassNormalStrength"] = -8;
    configured["PIXLGrassTuning"]["GrassSpecularNormalization"] = 99;
    configured["PIXLGrassTuning"]["GrassAlphaPower"] = 100;
    foliage.LoadSettings(configured);
    assert(foliage.settings.Glossiness == 1);
    assert(foliage.settings.WindStrength == 2);
    assert(foliage.settings.ComplexGrassMode == 3);
    assert(foliage.tuningSettings.GrassNormalStrength == 0);
    assert(foliage.tuningSettings.GrassSpecularNormalization == 4);
    assert(foliage.tuningSettings.GrassAlphaPower == 4);
    assert(foliage.tuningSettings.Magic == FoliageDynamics::TuningMagic);
    assert(foliage.tuningSettings.Version == FoliageDynamics::TuningVersion);
    json saved;
    foliage.SaveSettings(saved);
    FoliageDynamics roundTrip;
    roundTrip.LoadSettings(saved);
    json reSaved;
    roundTrip.SaveSettings(reSaved);
    assert(saved == reSaved);
    roundTrip.RestoreDefaultSettings();
    roundTrip.SaveSettings(reSaved);
    assert(reSaved == baseline);
    std::cout << "PASS exact foliage structs/JSON/functions: legacy reload, malformed extension fallback, bounds, boolean normalization, round-trip, defaults, 80/96-byte ABI sizes\n";
}
'@
$sourcePath = Join-Path $output 'foliage-settings-tests.cpp'
($prefix + "`r`nstruct FoliageDynamics {`r`n" + $magic + "`r`n" + $version + "`r`n" + $settings + "`r`n" + $tuning + "`r`nSettings settings; TuningSettings tuningSettings;`r`nvoid LoadSettings(json&); void SaveSettings(json&); void RestoreDefaultSettings();`r`n};`r`n" + $serialization + "`r`n" + ($functions -join "`r`n") + "`r`n" + $tests) | Set-Content -LiteralPath $sourcePath -Encoding UTF8
Push-Location $output
try {
    $command = 'call "{0}" >nul && cl.exe /nologo /std:c++latest /EHsc /W4 /WX /MD /O2 /fp:fast /I"{1}" "{2}" /Fe:foliage-settings-tests.exe && foliage-settings-tests.exe' -f $vcVarsPath, $include, $sourcePath
    & cmd.exe /d /c $command
    if ($LASTEXITCODE -ne 0) { throw "Foliage settings tests failed ($LASTEXITCODE)." }
} finally { Pop-Location }
Write-Host "Exact-source test artifacts: $output"
