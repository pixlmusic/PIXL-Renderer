[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$VcVars,
    [string]$VcpkgInstalled = ''
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $VcpkgInstalled) { $VcpkgInstalled = Join-Path $repo 'build\PIXL-12C\vcpkg_installed\x64-windows-static-md-release' }
$vcpkgRoot = (Resolve-Path -LiteralPath $VcpkgInstalled).Path
$vcVarsPath = (Resolve-Path -LiteralPath $VcVars).Path
$output = Join-Path $repo ('build\define-tests-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null

# Compile the exact current function and macro-assembly block, not a second
# parser implementation. Only engine context/logging are stubbed. This tests
# CPU input behavior without loading SKSE or touching a live shader cache.
$stateSource = Get-Content -LiteralPath (Join-Path $repo 'engine\State.cpp') -Raw
$parser = [regex]::Match($stateSource, '(?s)void State::SetDefines\(std::string a_defines\).*?\r?\n\}')
$cacheSource = Get-Content -LiteralPath (Join-Path $repo 'engine\ShaderCache.cpp') -Raw
$assembly = [regex]::Match($cacheSource, '(?s)std::array<D3D_SHADER_MACRO, 64> engineDefines\{\};.*?defines\.push_back\(\{ nullptr, nullptr \}\);')
if (-not $parser.Success -or -not $assembly.Success) { throw 'Test extraction contract changed; update the harness.' }
$prefix = @'
#include <algorithm>
#include <array>
#include <cassert>
#include <iostream>
#include <span>
#include <string>
#include <vector>
#include <pystring/pystring.h>
namespace logger {
template<class... T> void warn(T&&...) {}
template<class... T> void debug(T&&...) {}
}
struct State {
    std::string shaderDefinesString;
    std::vector<std::pair<std::string, std::string>> shaderDefines;
    bool developer = false;
    bool IsDeveloperMode() const { return developer; }
    auto* GetDefines() { return &shaderDefines; }
    void SetDefines(std::string);
};
namespace globals { State* state; }
struct D3D_SHADER_MACRO { const char* Name; const char* Definition; };
enum class ShaderClass { Vertex, Pixel, Compute };
void GetShaderDefines(int, int, std::span<D3D_SHADER_MACRO> macros) {
    for (int i = 0; i < 60; ++i) macros[i] = { "ENGINE", nullptr };
    macros[60] = { nullptr, nullptr };
}
'@
$test = @'
int main() {
    State state;
    globals::state = &state;
    state.SetDefines("A=7;B;C=;D=2; E ;BAD=X=Y;F");
    assert(state.shaderDefines.size() == 6);
    assert(state.shaderDefines[0].second == "7");
    assert(state.shaderDefines[1].second.empty());
    assert(state.shaderDefines[2].second.empty());
    assert(state.shaderDefines[3].second == "2");
    assert(state.shaderDefines[4].second.empty());
    assert(state.shaderDefines[5].second.empty());
    state.SetDefines("");
    assert(state.shaderDefines.empty() && state.shaderDefinesString.empty());
    for (const auto count : { 0, 1, 63, 64, 1000 }) {
        std::string input;
        for (int i = 0; i < count; ++i) input += "CUSTOM_" + std::to_string(i) + ";";
        state.SetDefines(input);
        for (const bool developer : { false, true }) {
            state.developer = developer;
            for (auto shaderClass : { ShaderClass::Vertex, ShaderClass::Pixel, ShaderClass::Compute }) {
                const int shader = 0, descriptor = 0;
                // ASSEMBLY
                const size_t prefix = developer ? 3 : 1;
                assert(defines.size() == prefix + count + 60 + 1);
                for (int i = 0; i < count; ++i)
                    assert(std::string(defines[prefix + i].Name) == "CUSTOM_" + std::to_string(i));
                assert(std::string(defines[prefix + count].Name) == "ENGINE");
                assert(defines.back().Name == nullptr && defines.back().Definition == nullptr);
            }
        }
    }
    std::cout << "PASS parser regressions and 30 macro assembly cases (up to 1000 custom macros)\n";
}
'@
$test = $test.Replace('// ASSEMBLY', $assembly.Value)
$sourcePath = Join-Path $output 'define-tests.cpp'
($prefix + "`r`n" + $parser.Value + "`r`n" + $test) | Set-Content -LiteralPath $sourcePath -Encoding UTF8
$include = Join-Path $vcpkgRoot 'include'
$library = Join-Path $vcpkgRoot 'lib\pystring.lib'
if (-not (Test-Path -LiteralPath $library)) { throw "Missing pystring build dependency: $library" }
Push-Location $output
try {
    $command = 'call "{0}" >nul && cl.exe /nologo /std:c++latest /EHsc /W4 /WX /MD /I"{1}" "{2}" "{3}" /Fe:define-tests.exe && define-tests.exe' -f $vcVarsPath, $include, $sourcePath, $library
    & cmd.exe /d /c $command
    if ($LASTEXITCODE -ne 0) { throw "Native define regression tests failed ($LASTEXITCODE)." }
} finally { Pop-Location }
Write-Host "Exact-source test artifacts: $output"
