[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$PackageDirectory,
    [string]$SchemaPath = "",
    [string]$ExpectedVersion = "1.1.0"
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $PackageDirectory).Path
$core = Join-Path $root 'PIXL-Core'
$legacyShaderRoot = Join-Path $core 'Shaders'
if (Test-Path -LiteralPath $legacyShaderRoot) { throw 'FOMOD core must not contain the old shared Shaders root.' }
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
if ($allInstallerText -notmatch 'regular default look' -or $allInstallerText -match '(?i)bleak') {
    throw 'FOMOD must describe the regular default look and must not select a bleak default.'
}

$sources = @($config.SelectNodes('//@source') | ForEach-Object { $_.Value })
foreach ($source in $sources) {
    if (!(Test-Path -LiteralPath (Join-Path $root $source))) { throw "FOMOD source does not exist: $source" }
}
$pgliteChoice = @($config.SelectNodes('//plugin[@name="Include PGLite source adapter and setup guide"]'))
if ($pgliteChoice.Count -ne 1 -or $sources -notcontains 'PGLite-Adapter') {
    throw 'PGLite source adapter choice or payload is missing.'
}
foreach ($required in @('PGLite-PIXLRenderer.patch','PGLITE.md','PGLite-ADAPTER-README.md')) {
    if (!(Test-Path -LiteralPath (Join-Path $root "PGLite-Adapter\$required"))) {
        throw "PGLite adapter payload is missing: $required"
    }
}
$pgliteRuntime = Join-Path $root 'PGLite-Runtime\SKSE\Plugins\PGLite.dll'
$pgliteRuntimeChoice = @($config.SelectNodes('//plugin[@name="Install PIXL-aware PGLite runtime (experimental)"]'))
if (Test-Path -LiteralPath $pgliteRuntime) {
    if ($pgliteRuntimeChoice.Count -ne 1 -or
        $sources -notcontains 'PGLite-Runtime\SKSE\Plugins\PGLite.dll') {
        throw 'PGLite runtime is present without its optional installer choice.'
    }
    $pgliteActiveGate = @($pgliteRuntimeChoice[0].SelectNodes('.//fileDependency[@file="SKSE\Plugins\PGLite.dll" and @state="Active"]'))
    $pgliteMissingNotice = @($config.SelectNodes('//plugin[@name="PGLite is missing - install PGLite first"]'))
    if ($pgliteMissingNotice.Count -ne 1) {
        throw 'PGLite missing-prerequisite notice is absent.'
    }
    $pgliteMissingGate = @($pgliteMissingNotice[0].SelectNodes('.//fileDependency[@file="SKSE\Plugins\PGLite.dll" and (@state="Missing" or @state="Inactive")]'))
    if ($pgliteActiveGate.Count -ne 1 -or $pgliteMissingGate.Count -ne 2) {
        throw 'PGLite compatibility is missing its active-file gate or missing/inactive prerequisite notice.'
    }
    if ((Get-Content -LiteralPath $configPath -Raw) -notmatch '(?is)PGLite is missing.*?Install and activate PGLite') {
        throw 'PGLite prerequisite notice does not tell users to install and activate PGLite.'
    }
    if (@($pgliteRuntimeChoice[0].SelectNodes('.//@destination') | ForEach-Object { $_.Value }) -contains 'SKSE\Plugins\PGLite.ini') {
        throw 'The PGLite runtime option must preserve an existing user INI.'
    }
    foreach ($required in @('PGLite-Runtime\SKSE\Plugins\PGLite.ini',
        'PGLite-Runtime\textures\cubemaps\dynamic1pxcubemap_black.dds',
        'PGLite-Source\LICENSE', 'PGLite-Source\PGLite\EXCEPTIONS.md',
        'PGLite-Source\PGLite\src\ShaderFrameworks.cpp')) {
        if (!(Test-Path -LiteralPath (Join-Path $root $required) -PathType Leaf)) {
            throw "PGLite runtime source or support file is missing: $required"
        }
    }
} elseif ($pgliteRuntimeChoice.Count -ne 0 -or
    (Get-ChildItem -LiteralPath $root -Filter 'PGLite.dll' -Recurse -File -ErrorAction SilentlyContinue)) {
    throw 'PGLite runtime choice or DLL is incomplete.'
}
$images = @($config.SelectNodes('//@path') | ForEach-Object { $_.Value } | Where-Object { $_ -match '\.(png|jpg|jpeg)$' })
foreach ($image in $images) {
    if (!(Test-Path -LiteralPath (Join-Path $root $image))) { throw "FOMOD image does not exist: $image" }
}

foreach ($required in @(
    'SKSE\Plugins\PIXLRenderer.dll',
    'SKSE\Plugins\PIXLRenderer\Presets\PIXL-Renderer-Live-Tested.pixlpreset.json',
    'SKSE\Plugins\PIXL\Config\RendererDefaults.json',
    'PIXL-Shaders\Water.hlsl',
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
$bundledRenderPresets = @(Get-ChildItem -LiteralPath (Join-Path $core 'SKSE\Plugins\PIXLRenderer\Presets') -File -Filter '*.pixlpreset.json')
if ($bundledRenderPresets.Count -ne 6) { throw "Expected six bundled renderer presets; found $($bundledRenderPresets.Count)." }
foreach ($presetPath in $bundledRenderPresets) {
    $preset = Get-Content -LiteralPath $presetPath.FullName -Raw | ConvertFrom-Json
    if ($preset.format -ne 'PIXL.RenderPreset' -or $preset.schemaVersion -ne 1 -or $null -eq $preset.settings) {
        throw "Bundled renderer preset has an invalid portable schema: $($presetPath.FullName)"
    }
    if ($preset.rendererVersion -ne '1-0-9-0') {
        throw "Bundled renderer preset version does not match PIXL's runtime version format: $($presetPath.FullName)"
    }
}
$presetGuide = Join-Path $core 'SKSE\Plugins\PIXL\Documentation\RENDER_PRESETS.md'
if (!(Test-Path -LiteralPath $presetGuide)) { throw 'The user-facing renderer preset guide is missing from the package.' }
$pipelineRoot = Join-Path $core 'PIXL\PipelineLibrary'
& (Join-Path $PSScriptRoot 'VerifyPixlPackageManifest.ps1') -PackageDirectory $core
$manifest = Get-Content (Join-Path $core 'PIXL-RENDERER.manifest.json') -Raw | ConvertFrom-Json
if ($manifest.version -ne $ExpectedVersion -or $manifest.title -ne "PIXL Renderer v$ExpectedVersion") {
    throw 'FOMOD core manifest version/title do not match the installer metadata.'
}
if ($manifest.cacheMode -eq 'compile-on-device' -and $info.fomod.Description -notmatch 'compile on first launch') {
    throw 'Compile-on-device FOMOD description does not explain first-launch shader compilation.'
}
if ($manifest.cacheMode -eq 'preloaded' -and $info.fomod.Description -notmatch 'validated shader cache') {
    throw 'Preloaded-cache FOMOD description does not match its payload.'
}
if ($manifest.cacheMode -eq 'preloaded') {
    $pipelineFiles = @(Get-ChildItem -LiteralPath $pipelineRoot -File -Recurse -Filter '*.pixlbin')
    $identityFiles = @(Get-ChildItem -LiteralPath $pipelineRoot -File -Recurse -Filter '*.pixlbin.identity')
    $pipelineCount = $pipelineFiles.Count
    if ($pipelineCount -lt 3000 -or $manifest.preloadedPipelineStages -lt 3000) {
        throw "Bundled shader cache is incomplete ($pipelineCount stages)."
    }
    if ($identityFiles.Count -ne $pipelineCount) {
        throw "Bundled shader cache identity sidecars do not match its stages ($pipelineCount stages, $($identityFiles.Count) sidecars)."
    }
    foreach ($stage in $pipelineFiles) {
        $identityPath = $stage.FullName + '.identity'
        if (!(Test-Path -LiteralPath $identityPath -PathType Leaf)) { throw "Cached shader stage has no identity sidecar: $($stage.FullName)" }
        $identity = [IO.File]::ReadAllBytes($identityPath)
        if ($identity.Length -ne 80 -or [Text.Encoding]::ASCII.GetString($identity, 0, 4) -ne 'PXAC' -or
            [BitConverter]::ToUInt32($identity, 4) -ne 1 -or
            [BitConverter]::ToUInt64($identity, 8) -ne $stage.Length) {
            throw "Cached shader stage has invalid identity metadata: $($stage.FullName)"
        }
        $artifactBytes = [IO.File]::ReadAllBytes($stage.FullName)
        $sha256 = [Security.Cryptography.SHA256]::Create()
        try { $payloadHash = $sha256.ComputeHash($artifactBytes) } finally { $sha256.Dispose() }
        if ([Convert]::ToBase64String($payloadHash) -ne [Convert]::ToBase64String([byte[]]$identity[48..79])) {
            throw "Cached shader stage does not match its identity payload hash: $($stage.FullName)"
        }
    }
} elseif ($manifest.channel -in @('LIVE-TEST', 'RELEASE-CANDIDATE') -and $manifest.cacheMode -eq 'compile-on-device') {
    if (Test-Path -LiteralPath $pipelineRoot) { throw 'Compile-on-device FOMOD must not include a stale PipelineLibrary.' }
    if ($manifest.preloadedPipelineStages -ne 0) { throw 'Compile-on-device FOMOD has an inconsistent preloaded stage count.' }
    $pipelineCount = 0
} else {
    throw 'FOMOD must use a validated preloaded cache, except for explicit LIVE-TEST or RELEASE-CANDIDATE compile-on-device packages.'
}

$defaultsPath = Join-Path $core 'SKSE\Plugins\PIXL\Config\RendererDefaults.json'
$defaults = Get-Content -LiteralPath $defaultsPath -Raw | ConvertFrom-Json
$water = $defaults.'Water Optics'
# These are the active Steam and source defaults; keep the package check aligned
# with the renderer's actual release payload rather than an older tuning pass.
if ([Math]::Abs([double]$water.ProjectedCausticsStrength - 1.0) -gt 0.001 -or
    [Math]::Abs([double]$water.ProjectedCausticsDistance - 180.0) -gt 0.01) {
    throw 'Default projected caustic strength/distance do not match the current source and Steam defaults.'
}
if ([Math]::Abs([double]$water.CausticsStrength - 1.2) -gt 0.001 -or
    [Math]::Abs([double]$water.CausticsVisibility - 1.8) -gt 0.001) {
    throw 'Regular underwater caustic defaults changed unexpectedly.'
}

$nestedArchives = @(Get-ChildItem -LiteralPath $root -File -Recurse | Where-Object { $_.Extension -match '^\.(zip|7z|rar|tar|gz|bz2|xz)$' })
if ($nestedArchives.Count) { throw "FOMOD contains nested archive(s): $($nestedArchives.FullName -join ', ')" }
$vendorRuntimeRoot = Join-Path $core 'PIXL-Shaders\ImageReconstruction'
if (Test-Path -LiteralPath $vendorRuntimeRoot) {
    foreach ($vendorDll in Get-ChildItem -LiteralPath $vendorRuntimeRoot -File -Recurse -Filter '*.dll') {
        $signature = Get-AuthenticodeSignature -LiteralPath $vendorDll.FullName
        if ($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid) {
            throw "Vendor runtime signature is not valid ($($signature.Status)): $($vendorDll.FullName)"
        }
    }
}
Write-Host "PASS: PIXL $ExpectedVersion three-page FOMOD, regular defaults, cache mode '$($manifest.cacheMode)' validated ($pipelineCount stages)."
