[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$PackageDirectory
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$package = (Resolve-Path -LiteralPath $PackageDirectory).Path
$packageDisplay = if ($package.StartsWith($repo + '\', [StringComparison]::OrdinalIgnoreCase)) {
    $package.Substring($repo.Length + 1)
} else { $package }
$manifestPath = Join-Path $package 'PIXL-RENDERER.manifest.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$shaderRoot = Join-Path $package 'PIXL-Shaders'
$reportRoot = Join-Path $repo 'docs\release_polish'
$errors = [Collections.Generic.List[string]]::new()

function Escape-Cell([string]$value) {
    if ($null -eq $value) { return '' }
    return $value.Replace('|','\|').Replace("`r",' ').Replace("`n",' ')
}
function Get-Magic([IO.FileInfo]$file) {
    $stream = [IO.File]::OpenRead($file.FullName)
    try {
        $bytes = [byte[]]::new([Math]::Min(16, [int]$stream.Length))
        $null = $stream.Read($bytes, 0, $bytes.Length)
        return ($bytes | ForEach-Object { $_.ToString('X2') }) -join ''
    } finally { $stream.Dispose() }
}
function Get-ExpectedMagic([string]$extension) {
    switch ($extension.ToLowerInvariant()) {
        '.png' { return '^89504E470D0A1A0A' }
        '.dds' { return '^44445320' }
        '.dll' { return '^4D5A' }
        '.esp' { return '^54455334' }
        '.nif' { return '^47616D656272796F|^4E657477696D6D6572' }
        '.ttf' { return '^00010000|^4F54544F' }
        '.otf' { return '^4F54544F|^00010000' }
        default { return '' }
    }
}
function Get-PayloadConsumer([string]$relative, [string]$extension) {
    $path = $relative.Replace('\','/')
    if ($path -eq 'SKSE/Plugins/PIXLRenderer.dll') { return 'SKSE plugin loader' }
    if ($path -like 'PIXL-Shaders/PIXL/Modules/*.ini') { return 'RenderModule descriptor catalog' }
    if ($extension -in @('.hlsl','.hlsli','.inl')) { return 'Shader graph/runtime compiler; see shader matrix' }
    if ($path -like 'PIXL-Shaders/ImageReconstruction/Streamline*/*' -or $path -like 'PIXL-Shaders/ImageReconstruction/FidelityFX/*') { return 'ImageReconstruction vendor runtime' }
    if ($path -like 'PIXL-Shaders/CameraSuite/Looks/*' -or $path -like 'PIXL-Shaders/CameraSuite/Lens/*') { return 'CameraSuite enumerated look/lens asset' }
    if ($path -like 'PIXL-Shaders/*/*' -and $extension -in @('.png','.dds')) { return (($path -split '/')[1] + ' module texture') }
    if ($path -like 'SKSE/Plugins/PIXL/Config/*') { return 'PIXL settings loader' }
    if ($path -like 'SKSE/Plugins/PIXL/Profiles/*') { return 'Quality profile loader' }
    if ($path -like 'SKSE/Plugins/PIXL/Interface/*') { return 'PIXL configuration interface' }
    if ($path -like 'SKSE/Plugins/PIXL/World/Water/*') { return 'Waterbody precache loader' }
    if ($path -like 'SKSE/Plugins/PIXL/Documentation/*' -or $path -like 'PIXL-Shaders/*LICENSE*' -or $path -like 'PIXL-Shaders/*license*' -or $path -like 'PIXL-Shaders/*PROVENANCE.md') { return 'Packaged documentation/license/provenance' }
    if ($path -like 'textures/heightmaps/*') { return 'TerrainField world heightmap loader' }
    if ($path -like 'meshes/water/*') { return 'Waterbody mesh replacement' }
    if ($path -like 'ParticleLights/*') { return 'Skyrim Particle Lights configuration' }
    if ($path -eq 'PIXL-TerrainField.esp') { return 'Skyrim plugin loader / TerrainField forms' }
    if ($path -in @('PIXL-INSTALLER-NOTICE.md','PIXL-RENDERER-README.md')) { return 'Package documentation' }
    return ''
}

$manifestPaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$payloadRecords = [Collections.Generic.List[object]]::new()
foreach ($entry in $manifest.files) {
    $relative = [string]$entry.path
    if (-not $manifestPaths.Add($relative)) { $errors.Add("Duplicate manifest path: $relative") }
    $path = Join-Path $package $relative
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { $errors.Add("Missing payload: $relative"); continue }
    $file = Get-Item -LiteralPath $path
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ($hash -ine $entry.sha256) { $errors.Add("Manifest hash mismatch: $relative") }
    if ($file.Length -eq 0) { $errors.Add("Empty payload: $relative") }
    $magic = Get-Magic $file
    $expectedMagic = Get-ExpectedMagic $file.Extension
    $format = 'nonempty'
    if ($expectedMagic) {
        $format = if ($magic -match $expectedMagic) { 'magic-ok' } else { 'magic-invalid'; $errors.Add("Invalid $($file.Extension) signature: $relative") }
    } elseif ($file.Extension -eq '.json') {
        try { Get-Content -LiteralPath $path -Raw | ConvertFrom-Json | Out-Null; $format='json-ok' }
        catch { $format='json-invalid'; $errors.Add("Invalid JSON payload: $relative") }
    } elseif ($file.Extension -in @('.hlsl','.hlsli','.ini','.inl','.md','.txt') -or $file.Name -in @('COPYING','LICENSE')) {
        try { $null = Get-Content -LiteralPath $path -Raw -ErrorAction Stop; $format='text-ok' }
        catch { $format='text-invalid'; $errors.Add("Unreadable text payload: $relative") }
    }
    $signature = ''
    if ($file.Extension -eq '.dll') {
        $signature = (Get-AuthenticodeSignature -LiteralPath $path).Status.ToString()
        if ($relative -like 'PIXL-Shaders/ImageReconstruction/*' -and $signature -ne 'Valid') {
            $errors.Add("Vendor DLL signature is not valid ($signature): $relative")
        }
    }
    $consumer = Get-PayloadConsumer $relative $file.Extension.ToLowerInvariant()
    if (-not $consumer) { $errors.Add("Payload has no classified consumer: $relative") }
    $payloadRecords.Add([pscustomobject]@{
        Path=$relative.Replace('\','/'); Bytes=$file.Length; SHA256=$hash; Format=$format; Signature=$signature; Consumer=$consumer
    })
}

$shaderFiles = @(Get-ChildItem -LiteralPath $shaderRoot -File -Recurse | Where-Object Extension -in @('.hlsl','.hlsli'))
$shaderAssets = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
Get-ChildItem -LiteralPath $shaderRoot -File -Recurse | ForEach-Object {
    $null = $shaderAssets.Add($_.FullName.Substring($shaderRoot.Length + 1).Replace('/','\'))
}
$shaderTexts = @{}
foreach ($shader in $shaderFiles) {
    $relative = $shader.FullName.Substring($shaderRoot.Length + 1).Replace('/','\')
    $shaderTexts[$relative] = Get-Content -LiteralPath $shader.FullName -Raw
}

$nativeFiles = @(Get-ChildItem (Join-Path $repo 'engine') -File -Recurse -Include '*.cpp','*.h')
$nativeText = ($nativeFiles | ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw }) -join "`n"
$incoming = @{}
foreach ($relative in $shaderTexts.Keys) { $incoming[$relative] = 0 }
$includePattern = '(?m)^\s*#\s*include\s+"([^"]+)"'
foreach ($owner in $shaderTexts.Keys) {
    foreach ($match in [regex]::Matches($shaderTexts[$owner], $includePattern)) {
        $include = $match.Groups[1].Value.Replace('/','\')
        while ($include.Contains('\\')) { $include = $include.Replace('\\','\') }
        if (-not $shaderAssets.Contains($include)) { $errors.Add("Unresolved staged include: $owner -> $include") }
        elseif ($incoming.ContainsKey($include)) { $incoming[$include]++ }
    }
}

$shaderRecords = [Collections.Generic.List[object]]::new()
foreach ($relative in ($shaderTexts.Keys | Sort-Object)) {
    $text = $shaderTexts[$relative]
    $file = Get-Item -LiteralPath (Join-Path $shaderRoot $relative)
    $includes = @([regex]::Matches($text, $includePattern))
    $bindings = @([regex]::Matches($text, '(?i)register\s*\(\s*([btsu]\d+)\s*\)') | ForEach-Object { $_.Groups[1].Value.ToLowerInvariant() })
    $duplicateBindings = @($bindings | Group-Object | Where-Object Count -gt 1 | ForEach-Object Name)
    $entrySignals = @([regex]::Matches($text, '(?m)^\s*\[(?:numthreads|domain|maxvertexcount)|\bSV_Target\b|\bSV_Position\b')).Count
    $rootEntry = -not $relative.Contains('\') -and $file.Extension -eq '.hlsl'
    $nativeReference = $nativeText.IndexOf($file.Name, [StringComparison]::OrdinalIgnoreCase) -ge 0
    $consumer = if ($rootEntry) { 'Skyrim entry shader' } elseif ($incoming[$relative] -gt 0 -and $nativeReference) { 'Included and native compiled' } elseif ($incoming[$relative] -gt 0) { 'Included by shader graph' } elseif ($nativeReference) { 'Native runtime compile' } elseif ($relative -like 'PIXL\Modules\*') { 'Module descriptor' } else { 'No direct text consumer found' }
    if ($file.Extension -eq '.hlsli' -and $incoming[$relative] -eq 0) { $errors.Add("Unreferenced staged shader include: $relative") }
    if ($file.Extension -eq '.hlsl' -and -not $rootEntry -and $incoming[$relative] -eq 0 -and -not $nativeReference) {
        $errors.Add("Unreferenced staged shader program: $relative")
    }
    $unsafeNormalize = [regex]::Matches($text, '(?i)(?<!Safe)\bnormalize\s*\((?![^)\r\n]*(?:max|clamp|saturate|\+\s*(?:1e|1\.0e)))').Count
    $unsafeRoot = [regex]::Matches($text, '(?i)\br?sqrt\s*\((?!\s*(?:max|abs|saturate|clamp))').Count
    $unsafeLog = [regex]::Matches($text, '(?i)\blog2?\s*\((?!\s*(?:max|abs|saturate|clamp))').Count
    $unsafePow = [regex]::Matches($text, '(?i)\bpow\s*\((?!\s*(?:max|abs|saturate|clamp))').Count
    $shaderRecords.Add([pscustomobject]@{
        Path=$relative.Replace('\','/'); Bytes=$file.Length; SHA256=(Get-FileHash $file.FullName -Algorithm SHA256).Hash;
        Consumer=$consumer; Includes=$includes.Count; Incoming=$incoming[$relative]; Bindings=$bindings.Count;
        DuplicateBindings=($duplicateBindings -join ','); EntrySignals=$entrySignals;
        NormalizeReview=$unsafeNormalize; RootReview=$unsafeRoot; LogReview=$unsafeLog; PowReview=$unsafePow
    })
}

$shaderReport = [Collections.Generic.List[string]]::new()
$shaderReport.Add('# Staged Shader Semantic Review — PIXL Renderer 1.0.9 / 12C')
$shaderReport.Add('')
$shaderReport.Add('Exact LIVE-TEST payload: `' + $packageDisplay + '`. Every staged HLSL/HLSLI file was opened, hashed, mapped to an include/native/game consumer, scanned for register declarations and reviewed for numerical-risk call sites. Risk counts identify locations requiring contextual review; they do not assert a bug. Duplicate bindings can be valid under mutually exclusive preprocessor branches.')
$shaderReport.Add('')
$shaderReport.Add("- Shader files reviewed: **$($shaderRecords.Count)**")
$shaderReport.Add("- HLSL programs: **$(@($shaderRecords | Where-Object Path -like '*.hlsl').Count)**")
$shaderReport.Add("- HLSL includes: **$(@($shaderRecords | Where-Object Path -like '*.hlsli').Count)**")
$shaderReport.Add("- Unresolved consumer/include failures: **$($errors.Count)** (must be zero for PASS)")
$shaderReport.Add('')
$shaderReport.Add('| Path | Consumer evidence | Includes | Incoming | Registers | Repeated register declarations | Entry signals | normalize review | sqrt/rsqrt review | log review | pow review | SHA-256 |')
$shaderReport.Add('|---|---|---:|---:|---:|---|---:|---:|---:|---:|---:|---|')
foreach ($record in $shaderRecords) {
    $shaderReport.Add('| `' + (Escape-Cell $record.Path) + '` | ' + (Escape-Cell $record.Consumer) +
        " | $($record.Includes) | $($record.Incoming) | $($record.Bindings) | " +
        (Escape-Cell $record.DuplicateBindings) + " | $($record.EntrySignals) | $($record.NormalizeReview) | " +
        "$($record.RootReview) | $($record.LogReview) | $($record.PowReview) | " + '`' + $record.SHA256 + '` |')
}
$shaderReport | Set-Content -LiteralPath (Join-Path $reportRoot 'SHADER_REVIEW_MATRIX.md') -Encoding utf8

$payloadReport = [Collections.Generic.List[string]]::new()
$payloadReport.Add('# Staged Payload Review — PIXL Renderer 1.0.9 / 12C')
$payloadReport.Add('')
$payloadReport.Add('Every manifest entry was opened, assigned to its runtime/package consumer class, and SHA-256 checked. JSON was parsed; known binary formats were checked by magic; vendor DLL signatures were checked; text inputs were read. Source-to-stage identity is separately enforced by `AuditPixlRenderer.ps1 -PackageDirectory`.')
$payloadReport.Add('')
$payloadReport.Add("- Manifest entries reviewed: **$($payloadRecords.Count)**")
$payloadReport.Add("- Package version/channel: **$($manifest.version) / $($manifest.channel)**")
$payloadReport.Add('')
$payloadReport.Add('| Path | Consumer | Bytes | Format validation | Signature | SHA-256 |')
$payloadReport.Add('|---|---|---:|---|---|---|')
foreach ($record in ($payloadRecords | Sort-Object Path)) {
    $payloadReport.Add('| `' + (Escape-Cell $record.Path) + '` | ' + (Escape-Cell $record.Consumer) +
        " | $($record.Bytes) | $($record.Format) | " + (Escape-Cell $record.Signature) +
        ' | `' + $record.SHA256 + '` |')
}
$payloadReport | Set-Content -LiteralPath (Join-Path $reportRoot 'PAYLOAD_REVIEW_MATRIX.md') -Encoding utf8

if ($errors.Count) {
    $errors | Sort-Object -Unique | ForEach-Object { Write-Host "ERROR: $_" -ForegroundColor Red }
    throw "Shader/payload audit failed with $(@($errors | Sort-Object -Unique).Count) unique issue(s)."
}
Write-Host "PASS: reviewed $($shaderRecords.Count) staged shader files and $($payloadRecords.Count) manifest payload entries."
