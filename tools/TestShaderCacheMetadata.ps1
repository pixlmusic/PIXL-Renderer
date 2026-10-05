[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
function Require($path,$pattern,$reason) {
    $full=Join-Path $repo $path
    if (!(Test-Path -LiteralPath $full)) { throw "Missing $path" }
    if ((Get-Content -LiteralPath $full -Raw) -notmatch $pattern) { throw "Missing shader-cache contract ($reason): $path" }
}
Require 'engine\ShaderCache.cpp' 'ComputeShaderSourceFingerprint' 'content fingerprint helper'
Require 'engine\ShaderCache.cpp' 'SourceFingerprint' 'metadata fingerprint key'
Require 'engine\ShaderCache.cpp' 'retaining legacy-compatible cache' 'backward-compatible cache migration'
Write-Host 'PASS: shader cache metadata includes a backward-compatible source/include fingerprint.'
