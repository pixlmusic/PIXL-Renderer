[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$PackageDirectory,
    [string]$SchemaPath = "",
    [string]$ExpectedVersion = "1.0.7"
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $PackageDirectory).Path
$core = Join-Path $root 'PIXL-Core'
$configPath = Join-Path $root 'fomod\ModuleConfig.xml'
$infoPath = Join-Path $root 'fomod\info.xml'
if (!(Test-Path -LiteralPath $configPath) -or !(Test-Path -LiteralPath $infoPath)) { throw 'Missing FOMOD XML.' }
[xml]$config = Get-Content -LiteralPath $configPath -Raw
[xml]$info = Get-Content -LiteralPath $infoPath -Raw
if ($info.fomod.Version.'#text' -ne $ExpectedVersion) { throw "Incorrect installer version; expected $ExpectedVersion." }

if ($SchemaPath) {
    $schema = (Resolve-Path -LiteralPath $SchemaPath).Path
    $settings = [System.Xml.XmlReaderSettings]::new()
    $settings.ValidationType = [System.Xml.ValidationType]::Schema
    [void]$settings.Schemas.Add($null,$schema)
    $schemaErrors = [Collections.Generic.List[string]]::new()
    $handler = [System.Xml.Schema.ValidationEventHandler]{ param($sender,$eventArgs) $schemaErrors.Add($eventArgs.Message) }
    $settings.add_ValidationEventHandler($handler)
    $reader = [System.Xml.XmlReader]::Create($configPath,$settings)
    try { while ($reader.Read()) {} } finally { $reader.Dispose() }
    if ($schemaErrors.Count) { throw "FOMOD schema validation failed: $($schemaErrors -join '; ')" }
}

$steps = @($config.SelectNodes('/config/installSteps/installStep'))
$expectedSteps = @('Hi','New updates','Install')
if ($steps.Count -ne $expectedSteps.Count) { throw 'FOMOD should contain exactly the Hi, New updates, and Install pages.' }
for ($index = 0; $index -lt $expectedSteps.Count; ++$index) {
    if ($steps[$index].name -ne $expectedSteps[$index]) { throw "Unexpected FOMOD page order at $index." }
}
$allInstallerText = (Get-Content -LiteralPath $configPath,$infoPath -Raw) -join "`n"
if ($allInstallerText -match '(?i)SurfaceTides') { throw 'SurfaceTides must not be referenced in the 1.0.7 FOMOD.' }
if (Get-ChildItem -LiteralPath $root -Recurse -File | Where-Object { $_.Name -in @('SurfaceTides.dll','SurfaceTides.ini') -or $_.FullName -match '[\\/]SurfaceTides[\\/]' }) {
    throw 'SurfaceTides payload is present in this FOMOD.'
}
if ($allInstallerText -notmatch 'regular default look' -or $allInstallerText -match '(?i)bleak') {
    throw 'FOMOD must describe the regular default look and must not select a bleak default.'
}

$sources = @($config.SelectNodes('//@source') | ForEach-Object { $_.Value })
foreach ($source in $sources) {
    if (!(Test-Path -LiteralPath (Join-Path $root $source))) { throw "FOMOD source does not exist: $source" }
}
$images = @($config.SelectNodes('//@path') | ForEach-Object { $_.Value } | Where-Object { $_ -match '\.(png|jpg|jpeg)$' })
foreach ($image in $images) {
    if (!(Test-Path -LiteralPath (Join-Path $root $image))) { throw "FOMOD image does not exist: $image" }
}

foreach ($required in @(
    'SKSE\Plugins\PIXLRenderer.dll',
    'SKSE\Plugins\PIXL\Config\RendererDefaults.json',
    'PIXL\PipelineLibrary\Library.ini',
    'Shaders\Water.hlsl',
    'SKSE\Plugins\PIXL\Documentation\COPYING',
    'SKSE\Plugins\PIXL\Documentation\EXCEPTIONS.md',
    'SKSE\Plugins\PIXL\Documentation\NOTICE.md',
    'SKSE\Plugins\PIXL\Documentation\ATTRIBUTION.md',
    'SKSE\Plugins\PIXL\Documentation\THIRD_PARTY_NOTICES.md',
    'SKSE\Plugins\PIXL\Documentation\TRADEMARKS.md',
    'SKSE\Plugins\PIXL\Documentation\SOURCE-AND-CREDITS.md'
)) {
    if (!(Test-Path -LiteralPath (Join-Path $core $required))) { throw "Missing FOMOD core payload: $required" }
}
$pipelineRoot = Join-Path $core 'PIXL\PipelineLibrary'
$pipelineCount = (Get-ChildItem -LiteralPath $pipelineRoot -File -Recurse -Filter '*.pixlbin').Count
if ($pipelineCount -lt 3000) { throw "Bundled shader cache is incomplete ($pipelineCount stages)." }
& (Join-Path $PSScriptRoot 'VerifyPixlPackageManifest.ps1') -PackageDirectory $core
$manifest = Get-Content (Join-Path $core 'PIXL-RENDERER.manifest.json') -Raw | ConvertFrom-Json
if ($manifest.cacheMode -ne 'preloaded' -or $manifest.preloadedPipelineStages -lt 3000) { throw 'FOMOD core does not declare the validated preloaded shader cache.' }

$defaultsPath = Join-Path $core 'SKSE\Plugins\PIXL\Config\RendererDefaults.json'
$defaults = Get-Content -LiteralPath $defaultsPath -Raw | ConvertFrom-Json
$water = $defaults.'Water Optics'
if ([Math]::Abs([double]$water.ProjectedCausticsStrength - 2.29) -gt 0.001 -or
    [Math]::Abs([double]$water.ProjectedCausticsDistance - 598.0) -gt 0.01) {
    throw 'Default projected caustic strength/distance do not match the owner-approved live settings.'
}
if ([Math]::Abs([double]$water.CausticsStrength - 1.2) -gt 0.001 -or
    [Math]::Abs([double]$water.CausticsVisibility - 1.8) -gt 0.001) {
    throw 'Regular underwater caustic defaults changed unexpectedly.'
}

$nestedArchives = @(Get-ChildItem -LiteralPath $root -File -Recurse | Where-Object { $_.Extension -match '^\.(zip|7z|rar|tar|gz|bz2|xz)$' })
if ($nestedArchives.Count) { throw "FOMOD contains nested archive(s): $($nestedArchives.FullName -join ', ')" }
$vendorRuntimeRoot = Join-Path $core 'Shaders\ImageReconstruction'
if (Test-Path -LiteralPath $vendorRuntimeRoot) {
    foreach ($vendorDll in Get-ChildItem -LiteralPath $vendorRuntimeRoot -File -Recurse -Filter '*.dll') {
        $signature = Get-AuthenticodeSignature -LiteralPath $vendorDll.FullName
        if ($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid) {
            throw "Vendor runtime signature is not valid ($($signature.Status)): $($vendorDll.FullName)"
        }
    }
}
Write-Host "PASS: PIXL $ExpectedVersion three-page FOMOD, regular defaults, preloaded shader cache and no SurfaceTides payload validated ($pipelineCount stages)."
