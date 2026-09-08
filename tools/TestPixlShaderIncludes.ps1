[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$VcVars)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$vcVarsPath = (Resolve-Path -LiteralPath $VcVars).Path
$output = Join-Path $repo ('build\include-tests-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null
$source = Get-Content -LiteralPath (Join-Path $repo 'engine\ShaderCache.cpp') -Raw
$handler = [regex]::Match($source, '(?s)class TrackingIncludeHandler : public ID3DInclude.*?\r?\n\t\};')
if (-not $handler.Success) { throw 'Include handler extraction contract changed.' }
$prefix = @'
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3dcompiler.h>
#include <algorithm>
#include <cassert>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
'@
$test = @'
int main() {
    const auto root = std::filesystem::current_path() / "Shaders";
    std::filesystem::create_directories(root / "Common");
    std::ofstream(root / "Common/test.hlsli") << "test";
    std::ofstream(root / "empty.hlsli");
    std::ofstream(root.parent_path() / "outside-fixture.txt") << "outside";
    {
        std::ofstream huge(root / "oversize.hlsli", std::ios::binary);
        const std::string block(1024 * 1024, 'x');
        for (int i = 0; i < 17; ++i) huge.write(block.data(), block.size());
    }
    TrackingIncludeHandler handler(root);
    const void* data = nullptr;
    UINT bytes = 0;
    assert(handler.Open(D3D_INCLUDE_LOCAL, "Common/test.hlsli", nullptr, &data, &bytes) == S_OK);
    assert(bytes == 4 && std::string(static_cast<const char*>(data), bytes) == "test");
    const auto* first = data;
    assert(handler.Open(D3D_INCLUDE_LOCAL, "Common/../Common/test.hlsli", nullptr, &data, &bytes) == S_OK);
    assert(std::string(static_cast<const char*>(first), 4) == "test");
    for (const auto* path : { "../outside-fixture.txt", "Common/../../outside-fixture.txt" }) {
        data = first; bytes = 999;
        assert(handler.Open(D3D_INCLUDE_LOCAL, path, nullptr, &data, &bytes) == E_ACCESSDENIED);
        assert(data == nullptr && bytes == 0);
    }
    for (const auto* path : { "missing.hlsli", "oversize.hlsli" }) {
        data = first; bytes = 999;
        assert(FAILED(handler.Open(D3D_INCLUDE_LOCAL, path, nullptr, &data, &bytes)));
        assert(data == nullptr && bytes == 0);
    }
    assert(handler.Open(D3D_INCLUDE_LOCAL, nullptr, nullptr, &data, &bytes) == E_INVALIDARG);
    assert(handler.Open(D3D_INCLUDE_LOCAL, "empty.hlsli", nullptr, nullptr, &bytes) == E_INVALIDARG);
    assert(handler.Open(D3D_INCLUDE_LOCAL, "empty.hlsli", nullptr, &data, nullptr) == E_INVALIDARG);
    assert(handler.Open(D3D_INCLUDE_LOCAL, "empty.hlsli", nullptr, &data, &bytes) == S_OK && bytes == 0);
    const auto absoluteOutside = (root.parent_path() / "outside-fixture.txt").string();
    assert(handler.Open(D3D_INCLUDE_LOCAL, absoluteOutside.c_str(), nullptr, &data, &bytes) == E_ACCESSDENIED);
    assert(handler.Close(first) == S_OK);
    std::cout << "PASS include paths, traversal, oversized/missing/empty input, null arguments and buffer lifetime\n";
}
'@
$sourcePath = Join-Path $output 'include-tests.cpp'
($prefix + "`r`n" + $handler.Value + "`r`n" + $test) | Set-Content -LiteralPath $sourcePath -Encoding UTF8
Push-Location $output
try {
    $command = 'call "{0}" >nul && cl.exe /nologo /std:c++latest /EHsc /W4 /WX /MD "{1}" /Fe:include-tests.exe && include-tests.exe' -f $vcVarsPath, $sourcePath
    & cmd.exe /d /c $command
    if ($LASTEXITCODE -ne 0) { throw "Native include regression tests failed ($LASTEXITCODE)." }
} finally { Pop-Location }
Write-Host "Exact-source test artifacts: $output"
