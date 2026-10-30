[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$BasePackageDirectory,
    [string]$OutputDirectory = "",
    [string]$ArchivePath = "",
    [string]$AllowedOutputRoot = "",
    [ValidateSet("", "r2", "r3", "r4")][string]$ReleaseRevision = "",
    [string]$ProductVersion = "",
    [ValidateRange(0, 9)][int]$CompressionLevel = 7
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$base = (Resolve-Path -LiteralPath $BasePackageDirectory).Path
$revisionVersion = "1.0.9$ReleaseRevision"
if ([string]::IsNullOrWhiteSpace($ProductVersion)) { $ProductVersion = $revisionVersion }
elseif ($ReleaseRevision -and $ProductVersion -ne $revisionVersion) { throw 'ProductVersion conflicts with ReleaseRevision.' }
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
if ($ProductVersion -notmatch '^\d+\.\d+\.\d+(?:r\d+)?$') { throw "Invalid FOMOD product version: $ProductVersion" }
$allowedRoot = [IO.Path]::GetFullPath($(if ($AllowedOutputRoot) { $AllowedOutputRoot } else { Join-Path $repo 'dist' }))
if (!$OutputDirectory) { $OutputDirectory = Join-Path $allowedRoot "PIXL-Renderer-$ProductVersion-FOMOD-$stamp" }
if (!$ArchivePath) { $ArchivePath = "$OutputDirectory.zip" }
$output = [IO.Path]::GetFullPath($OutputDirectory)
$archive = [IO.Path]::GetFullPath($ArchivePath)

function Assert-Output([string]$path) {
    if (!$path.StartsWith($allowedRoot.TrimEnd('\') + '\',[StringComparison]::OrdinalIgnoreCase)) {
        throw "Output must remain under $allowedRoot"
    }
    if ([string]::Equals($path.TrimEnd('\'),$allowedRoot.TrimEnd('\'),[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Refusing to use the dist root itself.'
    }
}
function Copy-Tree([string]$source,[string]$destination) {
    if (!(Test-Path -LiteralPath $source)) { throw "Missing source: $source" }
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    Copy-Item -Path (Join-Path $source '*') -Destination $destination -Recurse -Force
}
Assert-Output $output
Assert-Output $archive
if (Test-Path -LiteralPath $output) { throw "Output already exists: $output" }
if (Test-Path -LiteralPath $archive) { throw "Archive already exists: $archive" }

foreach ($required in @(
    'PIXL-RENDERER.manifest.json',
    'SKSE\Plugins\PIXLRenderer.dll',
    'SKSE\Plugins\PIXLRenderer\Presets\PIXL-Renderer-Live-Tested.pixlpreset.json',
    'PIXL-Shaders\Water.hlsl',
    'PIXL-Shaders\Common\Color.hlsli',
    'PIXL-Shaders\CameraSuite\HDROutputCS.hlsl',
    'PIXL-Shaders\CameraSuite\DOFCoCCS.hlsl',
    'PIXL-Shaders\CameraSuite\DOFBlurCS.hlsl',
    'SKSE\Plugins\PIXL\Documentation\COPYING',
    'SKSE\Plugins\PIXL\Documentation\EXCEPTIONS.md',
    'SKSE\Plugins\PIXL\Documentation\NOTICE.md',
    'SKSE\Plugins\PIXL\Documentation\ATTRIBUTION.md',
    'SKSE\Plugins\PIXL\Documentation\THIRD_PARTY_NOTICES.md',
    'SKSE\Plugins\PIXL\Documentation\TRADEMARKS.md',
    'SKSE\Plugins\PIXL\Documentation\SOURCE-AND-CREDITS.md',
    'SKSE\Plugins\PIXL\Documentation\RENDER_PRESETS.md'
)) {
    if (!(Test-Path -LiteralPath (Join-Path $base $required))) { throw "Incomplete PIXL package: $required" }
}
$core = Join-Path $output 'PIXL-Core'
Copy-Tree $base $core
# Keep the source attribution in the installer synchronized with the current
# repository record, even when the selected prebuilt-cache base is older.
$creditsSource = Join-Path $repo 'distribution\SOURCE-AND-CREDITS.md'
$creditsTarget = Join-Path $core 'SKSE\Plugins\PIXL\Documentation\SOURCE-AND-CREDITS.md'
if (!(Test-Path -LiteralPath $creditsTarget)) { throw 'FOMOD core is missing its source-and-credits notice.' }
Copy-Item -LiteralPath $creditsSource -Destination $creditsTarget -Force
$manifestPath = Join-Path $core 'PIXL-RENDERER.manifest.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$manifest.version = $ProductVersion
$manifest.title = "PIXL Renderer v$ProductVersion"
$relativeCreditsPath = 'SKSE/Plugins/PIXL/Documentation/SOURCE-AND-CREDITS.md'
$creditEntry = @($manifest.files | Where-Object { $_.path -ceq $relativeCreditsPath })
if ($creditEntry.Count -ne 1) { throw 'FOMOD core manifest must contain exactly one source-and-credits entry.' }
$creditEntry[0].bytes = (Get-Item -LiteralPath $creditsTarget).Length
$creditEntry[0].sha256 = (Get-FileHash -LiteralPath $creditsTarget -Algorithm SHA256).Hash
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
Copy-Tree (Join-Path $repo 'installer\PIXLRenderer\fomod') (Join-Path $output 'fomod')
Copy-Tree (Join-Path $repo 'installer\PIXLRenderer\images') (Join-Path $output 'fomod\images')
Copy-Item -LiteralPath (Join-Path $repo 'installer\PIXLRenderer\PIXL-INSTALLER-NOTICE.md') -Destination (Join-Path $core 'PIXL-INSTALLER-NOTICE.md')

$configPath = Join-Path $output 'fomod\ModuleConfig.xml'
$configText = Get-Content -LiteralPath $configPath -Raw
$configText = [regex]::Replace($configText, '1\.0\.\d+(?:r\d+)?(?=(?:</moduleName>|" type=| highlights|</plugin>))', $ProductVersion)
[IO.File]::WriteAllText($configPath, $configText, [Text.UTF8Encoding]::new($false))
$infoPath = Join-Path $output 'fomod\info.xml'
[xml]$info = Get-Content -LiteralPath $infoPath -Raw
$versionNode = $info.SelectSingleNode('/fomod/Version')
if (!$versionNode) { throw 'FOMOD metadata has no Version node.' }
$versionNode.InnerText = $ProductVersion
$descriptionNode = $info.SelectSingleNode('/fomod/Description')
if (!$descriptionNode) { throw 'FOMOD metadata has no Description node.' }
if ($manifest.cacheMode -eq 'preloaded') {
    $descriptionNode.InnerText = 'Modern DirectX 11 rendering for Skyrim Special Edition, with a validated shader cache for faster first launch. Missing or changed permutations compile on your system.'
} elseif ($manifest.channel -in @('LIVE-TEST', 'RELEASE-CANDIDATE') -and $manifest.cacheMode -eq 'compile-on-device') {
    $descriptionNode.InnerText = 'Modern DirectX 11 rendering for Skyrim Special Edition. Shader permutations compile on first launch; allow compilation to finish before evaluating performance.'
} else {
    throw 'Unsupported cache mode for FOMOD metadata.'
}
$versionMatch = [regex]::Match($ProductVersion, '^(\d+\.\d+\.\d+)(?:r(\d+))?$')
$machineVersion = $versionMatch.Groups[1].Value + '.' + $(if ($versionMatch.Groups[2].Success) { $versionMatch.Groups[2].Value } else { '0' })
$versionNode.SetAttribute('MachineVersion', $machineVersion)
$info.Save($infoPath)

$testArguments = @{ PackageDirectory=$output; ExpectedVersion=$ProductVersion }
& (Join-Path $repo 'tools\TestPixlFomod.ps1') @testArguments

$sevenZip = Join-Path $env:ProgramFiles '7-Zip\7z.exe'
if (!(Test-Path -LiteralPath $sevenZip)) { throw '7-Zip is required to create the release archive.' }
Push-Location $output
try { & $sevenZip a -tzip "-mx=$CompressionLevel" -mmt=on $archive '.' | Out-Host }
finally { Pop-Location }
if ($LASTEXITCODE -ne 0) { throw '7-Zip archive creation failed.' }
& $sevenZip t $archive | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'FOMOD archive verification failed.' }
$hash = (Get-FileHash -LiteralPath $archive).Hash
Write-Host "PIXL FOMOD: $archive"
Write-Host "SHA256: $hash"
