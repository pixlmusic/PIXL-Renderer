[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$VcVars)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$vcVarsPath = (Resolve-Path -LiteralPath $VcVars).Path
$output = Join-Path $repo ('build\dependency-tests-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null
$tracker = Get-Content -LiteralPath (Join-Path $repo 'engine\ShaderFileWatcher.h') -Raw
# Remove only the unrelated declaration header and pragma from the standalone
# translation unit; compile the real tracker and path-normalization function.
$tracker = $tracker.Replace('#pragma once', '').Replace('#include "Utils/Format.h"', '')
$format = Get-Content -LiteralPath (Join-Path $repo 'engine\Utils\Format.cpp') -Raw
$normalize = [regex]::Match($format, '(?s)std::string FixFilePath\(const std::string& a_path\).*?\r?\n\t\}')
if (-not $normalize.Success) { throw 'Path normalization extraction contract changed.' }
$prefix = @'
#include <algorithm>
#include <cassert>
#include <cctype>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
'@
$test = @'
int main() {
    SIE::ShaderFileDependencyTracker tracker;
    tracker.RegisterDependencies("Lighting.hlsl", { "Common/A.hlsli" });
    tracker.RegisterDependencies("LIGHTING.HLSL", { "Common/B.hlsli" });
    assert(tracker.GetDependents("common/a.hlsli").size() == 1);
    assert(tracker.GetDependents("common/b.hlsli").size() == 1);
    tracker.RegisterDependencies("Lighting.hlsl", {});
    assert(tracker.GetDependents("common/a.hlsli").size() == 1);
    tracker.RegisterDependencies("Water.hlsl", { "Common/A.hlsli", "Common/A.hlsli" });
    assert(tracker.GetDependents("common/a.hlsli").size() == 2);
    tracker.UnregisterDependencies("LIGHTING.HLSL");
    assert(tracker.GetDependents("common/a.hlsli").size() == 1);
    assert(tracker.GetDependents("common/b.hlsli").empty());
    tracker.Clear();
    assert(tracker.GetDependents("common/a.hlsli").empty());
    std::vector<std::thread> workers;
    for (int i = 0; i < 32; ++i) {
        workers.emplace_back([&, i] {
            for (int repeat = 0; repeat < 50; ++repeat)
                tracker.RegisterDependencies("Lighting.hlsl", { "variant" + std::to_string(i) + ".hlsli" });
        });
    }
    for (auto& worker : workers) worker.join();
    for (int i = 0; i < 32; ++i)
        assert(tracker.GetDependents("variant" + std::to_string(i) + ".hlsli").size() == 1);
    tracker.UnregisterDependencies("Lighting.hlsl");
    for (int i = 0; i < 32; ++i)
        assert(tracker.GetDependents("variant" + std::to_string(i) + ".hlsli").empty());
    std::cout << "PASS permutation union, empty input, shared edges, removal, clear and 32 concurrent writers\n";
}
'@
$sourcePath = Join-Path $output 'dependency-tests.cpp'
($prefix + "`r`nnamespace Util {`r`n" + $normalize.Value + "`r`n}`r`n" + $tracker + "`r`n" + $test) | Set-Content -LiteralPath $sourcePath -Encoding UTF8
Push-Location $output
try {
    $command = 'call "{0}" >nul && cl.exe /nologo /std:c++latest /EHsc /W4 /WX /MD "{1}" /Fe:dependency-tests.exe && dependency-tests.exe' -f $vcVarsPath, $sourcePath
    & cmd.exe /d /c $command
    if ($LASTEXITCODE -ne 0) { throw "Native dependency regression tests failed ($LASTEXITCODE)." }
} finally { Pop-Location }
Write-Host "Exact-source test artifacts: $output"
