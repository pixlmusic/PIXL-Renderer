[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$PackageDirectory,
    [string]$SchemaPath = ""
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $PackageDirectory).Path
$configPath = Join-Path $root 'fomod\ModuleConfig.xml'
$infoPath = Join-Path $root 'fomod\info.xml'
if (!(Test-Path -LiteralPath $configPath) -or !(Test-Path -LiteralPath $infoPath)) { throw 'Missing FOMOD XML.' }
[xml]$config = Get-Content -LiteralPath $configPath -Raw
[xml]$info = Get-Content -LiteralPath $infoPath -Raw
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
if ($info.fomod.Version.'#text' -ne '1.0.2') { throw 'Incorrect installer version.' }
$allText = (Get-Content -LiteralPath $configPath,$infoPath,(Join-Path $root 'PIXL-Core\PIXL-INSTALLER-NOTICE.md') -Raw) -join "`n"
foreach ($phrase in @('substantial AI assistance','SurfaceTides 1.0.2 Integration','AllowPIXL=1','replaces SurfaceTides.dll','PAGE DOWN','HOME','HIGHLIGHTS SINCE 1.0.1 HOTFIX')) {
    if ($allText -notmatch [regex]::Escape($phrase)) { throw "Missing required disclosure or warning: $phrase" }
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
    'PIXL-Core\SKSE\Plugins\PIXLRenderer.dll',
    'PIXL-Core\Shaders\Water.hlsl',
    'PIXL-Optional\SurfaceTides-1.0.2\SKSE\Plugins\SurfaceTides.dll',
    'PIXL-Optional\SurfaceTides-1.0.2\SKSE\Plugins\SurfaceTides.ini',
    'PIXL-Optional\SurfaceTides-1.0.2\Shaders\SurfaceTides\Water.hlsl'
)) {
    if (!(Test-Path -LiteralPath (Join-Path $root $required))) { throw "Missing installer payload: $required" }
}
$surfaceDll = Join-Path $root 'PIXL-Optional\SurfaceTides-1.0.2\SKSE\Plugins\SurfaceTides.dll'
$surfaceBinaryText = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($surfaceDll))
foreach ($export in @('SKSEPlugin_Load','SKSEPlugin_Query','SKSEPlugin_Version')) {
    if ($surfaceBinaryText.IndexOf($export,[StringComparison]::Ordinal) -lt 0) {
        throw "SurfaceTides universal DLL is missing required SKSE export: $export"
    }
}
$nestedArchives = @(Get-ChildItem -LiteralPath $root -File -Recurse | Where-Object {
    $_.Extension -match '^\.(zip|7z|rar|tar|gz|bz2|xz)$'
})
if ($nestedArchives.Count) {
    throw "Nexus upload must not contain nested archive(s): $($nestedArchives.FullName -join ', ')"
}
$vendorRuntimeRoot = Join-Path $root 'PIXL-Core\Shaders\ImageReconstruction'
if (Test-Path -LiteralPath $vendorRuntimeRoot) {
    foreach ($vendorDll in Get-ChildItem -LiteralPath $vendorRuntimeRoot -File -Recurse -Filter '*.dll') {
        $signature = Get-AuthenticodeSignature -LiteralPath $vendorDll.FullName
        if ($vendorDll.Name -ieq 'nvngx_dlssnr.dll') {
            if ($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid) {
                Write-Warning "Neural Rendering runtime signature status is $($signature.Status): $($vendorDll.FullName)"
            }
        } elseif ($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid) {
            throw "Vendor runtime signature is not valid ($($signature.Status)): $($vendorDll.FullName)"
        }
    }
}
$presetPath = Join-Path $root 'PIXL-Optional\SurfaceTides-1.0.2\SKSE\Plugins\SurfaceTides.ini'
$preset = Get-Content -LiteralPath $presetPath -Raw
foreach ($setting in @('AllowPIXL=1','Damping=0.24','Wind=7','GustStrength=1.3','NormalStrength=2.35','DisplacementStrength=2','PIXLCityDisplacementScale=0.55','PIXLInteriorDisplacementScale=0.25')) {
    # Get-Content -Raw preserves CRLF. Permit the carriage return before the
    # multiline end anchor so normal Windows INI files validate correctly.
    if ($preset -notmatch "(?m)^$([regex]::Escape($setting))\r?$") { throw "PIXL SurfaceTides preset is missing: $setting" }
}
$halfLife = [Math]::Log(2.0) / 0.24
$continuousForcingRatio = 7.0 / 5.0
$gustExcitationRatio = (7.0 * 1.3) / (5.0 * 1.0)
if ($halfLife -lt 2.85 -or $halfLife -gt 2.95 -or $continuousForcingRatio -lt 1.39 -or $gustExcitationRatio -lt 1.81) {
    throw 'PIXL SurfaceTides preset no longer meets its documented retention/excitation targets.'
}
$groups = @($config.SelectNodes('//group'))
if ($groups.Count -ne 3 -or @($groups | Where-Object { $_.type -ne 'SelectExactlyOne' }).Count) { throw 'Unexpected installer selection structure.' }
if ($config.SelectSingleNode("//plugin[contains(@name,'SurfaceTides 1.0.2 Integration')]/typeDescriptor/dependencyType")) {
    throw 'SurfaceTides integration must remain selectable; Vortex deployment state is not a reliable FOMOD dependency gate.'
}
Write-Host "PASS: PIXL 1.0.2 FOMOD XML, branding assets, disclosures, core payload and universal SurfaceTides 1.0.2 bridge validated."
