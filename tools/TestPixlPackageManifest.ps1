[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$testRoot = Join-Path $repo ('build\manifest-tests-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
$validator = Join-Path $PSScriptRoot 'VerifyPixlPackageManifest.ps1'
$cases = [ordered]@{
    'valid' = ''
    'hash-mismatch' = 'Payload integrity mismatch'
    'size-mismatch' = 'Payload integrity mismatch'
    'missing' = 'Missing payload'
    'extra' = 'Unmanifested package file'
    'duplicate' = 'Duplicate manifest payload'
    'traversal' = 'Invalid manifest payload path'
    'rooted' = 'Invalid manifest payload path'
    'alternate-stream' = 'Invalid manifest payload path'
    'empty-segment' = 'Invalid manifest payload path'
    'empty-manifest' = 'Package manifest contains no payloads'
    'bad-channel' = 'Invalid PIXL package manifest identity'
    'bad-hash' = 'Invalid manifest size/hash'
    'missing-executable' = 'Executable is not covered'
}
foreach ($name in $cases.Keys) {
    $caseRoot = Join-Path $testRoot $name
    $dllPath = Join-Path $caseRoot 'SKSE\Plugins\PIXLRenderer.dll'
    New-Item -ItemType Directory -Path (Split-Path -Parent $dllPath) -Force | Out-Null
    # Synthetic bytes, not a native DLL. Tests exercise integrity only; the
    # build/package audit remains responsible for native binary provenance.
    [IO.File]::WriteAllText($dllPath, 'fixture')
    $entry = [ordered]@{
        path = 'SKSE/Plugins/PIXLRenderer.dll'
        bytes = (Get-Item -LiteralPath $dllPath).Length
        sha256 = (Get-FileHash -LiteralPath $dllPath).Hash
    }
    $manifest = [ordered]@{
        product = 'PIXL Renderer'
        executable = 'SKSE/Plugins/PIXLRenderer.dll'
        channel = 'RELEASE-CANDIDATE'
        cacheMode = 'compile-on-device'
        files = @($entry)
    }
    switch ($name) {
        'hash-mismatch' { [IO.File]::WriteAllText($dllPath, 'changed') }
        'size-mismatch' { $entry.bytes++ }
        'missing' { $entry.path = 'absent.bin' }
        'extra' { [IO.File]::WriteAllText((Join-Path $caseRoot 'extra.txt'), 'not declared') }
        'duplicate' { $manifest.files = @($entry, $entry) }
        'traversal' { $entry.path = '../outside.txt' }
        'rooted' { $entry.path = 'C:/outside.txt' }
        'alternate-stream' { $entry.path = 'file:stream' }
        'empty-segment' { $entry.path = 'SKSE//Plugins/PIXLRenderer.dll' }
        'empty-manifest' { $manifest.files = @() }
        'bad-channel' { $manifest.channel = 'TYPO' }
        'bad-hash' { $entry.sha256 = 'not-a-hash' }
        'missing-executable' {
            $entry.path = 'payload.bin'
            Move-Item -LiteralPath $dllPath -Destination (Join-Path $caseRoot 'payload.bin')
        }
    }
    $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $caseRoot 'PIXL-RENDERER.manifest.json') -Encoding UTF8
    $failure = ''
    try { & $validator -PackageDirectory $caseRoot }
    catch { $failure = $_.Exception.Message }
    if ($cases[$name]) {
        if (-not $failure.StartsWith($cases[$name], [StringComparison]::Ordinal)) {
            throw "Case '$name' expected '$($cases[$name])'; got '$failure'."
        }
    } elseif ($failure) { throw "Valid package was rejected: $failure" }
    Write-Host "PASS $name"
}
Write-Host "All $($cases.Count) manifest tests passed. Fixtures retained at $testRoot"
