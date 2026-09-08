[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ShaderRoot,
    [Parameter(Mandatory = $true)][string]$Fxc
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $ShaderRoot).Path
$compiler = (Resolve-Path -LiteralPath $Fxc).Path
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$output = Join-Path $repo ('build\material-shader-tests-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null
$results = [Collections.Generic.List[object]]::new()
# Narrow ordinary Lighting coverage for the Thin Surface settings/ABI audit.
# This is not every engine Lighting technique or every integrated module set.
foreach ($mask in 0..15) {
    foreach ($stage in @('vs', 'ps')) {
        $defines = @('WINPC', 'DX11', $(if ($stage -eq 'vs') { 'VSHADER' } else { 'PSHADER' }))
        if ($mask -band 1) { $defines += 'SKINNED' }
        if ($mask -band 2) { $defines += 'MODELSPACENORMALS' }
        if ($mask -band 4) { $defines += 'THIN_SURFACE' }
        if ($mask -band 8) { $defines += 'DO_ALPHA_TEST' }
        $prefix = Join-Path $output ($mask.ToString('D2') + '-' + $stage)
        $compilerArguments = @('/nologo', '/WX', '/Ges', '/O3', '/T', ($stage + '_5_0'), '/E', 'main', '/I', $root)
        foreach ($define in $defines) { $compilerArguments += @('/D', $define) }
        $compilerArguments += @('/Fo', ($prefix + '.cso'), (Join-Path $root 'Lighting.hlsl'))
        $previousErrorAction = $ErrorActionPreference
        try {
            # Windows PowerShell wraps native stderr as ErrorRecords. Retain all
            # compiler diagnostics and classify failure using the real exit code.
            $ErrorActionPreference = 'Continue'
            $compileOutput = & $compiler @compilerArguments 2>&1
            $code = $LASTEXITCODE
        } finally { $ErrorActionPreference = $previousErrorAction }
        $compileOutput | Set-Content -LiteralPath ($prefix + '.log') -Encoding UTF8
        $results.Add([pscustomobject]@{ stage = $stage; defines = ($defines -join ';'); exitCode = $code; log = ($prefix + '.log') })
        if ($code -ne 0) { Write-Host "FAIL $mask/$stage : $compileOutput" }
    }
}
$results | Export-Csv -LiteralPath (Join-Path $output 'results.csv') -NoTypeInformation
foreach ($path in @('Lighting.hlsl', 'ThinSurface/ThinSurface.hlsli', 'Common/SharedData.hlsli')) {
    Write-Host "$path SHA256: $((Get-FileHash -LiteralPath (Join-Path $root $path) -Algorithm SHA256).Hash)"
}
Write-Host "Material shader evidence: $output"
$failed = @($results | Where-Object { $_.exitCode -ne 0 })
if ($failed.Count) { throw "$($failed.Count) of $($results.Count) material shader cases failed." }
Write-Host "PASS all $($results.Count) material vertex/pixel cases with strictness, validation and warnings-as-errors."
