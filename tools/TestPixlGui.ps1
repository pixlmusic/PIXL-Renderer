[CmdletBinding()]
param([string]$VcVars = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat')
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$vc = (Resolve-Path -LiteralPath $VcVars).Path
$dependencies = Join-Path $repo 'build\PIXL-12C\vcpkg_installed\x64-windows-static-md-release'
$output = Join-Path $repo ('build\gui-tests-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null
Push-Location $output
try {
    $command = 'call "{0}" >nul && cl.exe /nologo /std:c++latest /EHsc /W4 /WX /MD /O2 /I"{1}\include" /I"{2}\engine" "{2}\tools\TestPixlGui.cpp" /Fe:gui-tests.exe /link /LIBPATH:"{1}\lib" imgui.lib user32.lib imm32.lib && gui-tests.exe' -f $vc, $dependencies, $repo
    $ErrorActionPreference = 'Continue'
    & cmd.exe /d /c $command 2>&1 | Tee-Object -FilePath (Join-Path $output 'test.log')
    $ErrorActionPreference = 'Stop'
    if ($LASTEXITCODE -ne 0) { throw "GUI tests failed ($LASTEXITCODE)." }
} finally { Pop-Location }
Write-Host "GUI test artifacts: $output"
