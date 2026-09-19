[CmdletBinding()]
param(
    [string]$OutputDirectory = "",
    [string]$CompatibilityMirror = "",
    [string]$ArchivePath = "",
    [string]$BuildDirectory = "",
    [string]$PipelineLibrary = "",
    [string]$UserConfigPath = "",
    [string]$NeuralRuntimePath = "",
    [string]$AllowedOutputRoot = "",
    [ValidateSet("LIVE-TEST", "RELEASE-CANDIDATE", "RELEASE")]
    [string]$Channel = "LIVE-TEST",
    [switch]$SkipPipelineLibrary,
    [switch]$SkipArchive
)

$ErrorActionPreference = "Stop"
$sourceRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$sourceCommit = (& git -C $sourceRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or -not $sourceCommit) { throw "Unable to resolve package source commit." }
$sourceChanges = @(& git -C $sourceRoot status --porcelain --untracked-files=normal)
if ($LASTEXITCODE -ne 0) { throw 'Unable to inspect source worktree.' }
$reproducibleDependencyPatch = $false
if ($sourceChanges -contains ' m extern/FidelityFX-SDK') {
    $dependency = Join-Path $sourceRoot 'extern\FidelityFX-SDK'
    $changedFiles = @(& git -C $dependency diff --name-only)
    if ($LASTEXITCODE -ne 0) { throw 'Unable to inspect FidelityFX patch.' }
    if ($changedFiles.Count -eq 1 -and $changedFiles[0] -eq 'sdk/src/backends/dx11/CMakeLists.txt') {
        & git -C $dependency apply --reverse --check (Join-Path $sourceRoot 'cmake\patches\FidelityFX-DX11-Short-Output.patch')
        $reproducibleDependencyPatch = $LASTEXITCODE -eq 0
    }
}
$sourceWorkingTreeDirty = [bool]@($sourceChanges | Where-Object {
    -not ($reproducibleDependencyPatch -and $_ -eq ' m extern/FidelityFX-SDK')
})
$allowedRoot = [IO.Path]::GetFullPath($(if ($AllowedOutputRoot) { $AllowedOutputRoot } else { Join-Path $sourceRoot "dist" }))
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $allowedRoot "PIXL-Renderer-1.0.2a-Clean-Cache" }
if (-not $ArchivePath) { $ArchivePath = Join-Path $allowedRoot "PIXL-Renderer-1.0.2a-Clean-Cache.zip" }
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $sourceRoot "build\PIXL-12C\Release" }
$output = [IO.Path]::GetFullPath($OutputDirectory)
$archive = if ($ArchivePath) { [IO.Path]::GetFullPath($ArchivePath) } else { "" }
$mirror = if ($CompatibilityMirror) { [IO.Path]::GetFullPath($CompatibilityMirror) } else { "" }

function Assert-PackageTarget([string]$Path) {
    if (-not $Path.StartsWith($allowedRoot.TrimEnd('\') + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing target outside the allowed output root '$allowedRoot': $Path"
    }
    if ([string]::Equals($Path.TrimEnd('\'), $allowedRoot.TrimEnd('\'), [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to operate on the allowed output root itself."
    }
}

function Remove-PackageItem([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return }
    Assert-PackageTarget $Path
    $item = Get-Item -LiteralPath $Path -Force
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        Remove-Item -LiteralPath $Path -Force
    } else {
        Remove-Item -LiteralPath $Path -Recurse -Force
    }
}

function Copy-Tree([string]$Source, [string]$Destination) {
    if (-not (Test-Path -LiteralPath $Source)) { throw "Missing package source: $Source" }
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    Copy-Item -Path (Join-Path $Source "*") -Destination $Destination -Recurse -Force
}

function Copy-TrackedTree([string]$Source, [string]$Destination) {
    if (-not (Test-Path -LiteralPath $Source -PathType Container)) { throw "Missing package source: $Source" }
    $resolvedSource = (Resolve-Path -LiteralPath $Source).Path.TrimEnd('\')
    if (-not $resolvedSource.StartsWith($sourceRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Tracked package source is outside the repository: $resolvedSource"
    }
    $relativeRoot = $resolvedSource.Substring($sourceRoot.TrimEnd('\').Length + 1).Replace('\', '/')
    $trackedFiles = @(& git -C $sourceRoot ls-files -- "$relativeRoot/")
    if ($LASTEXITCODE -ne 0) { throw "Unable to enumerate tracked package files below $relativeRoot" }
    if ($trackedFiles.Count -eq 0) { throw "No tracked package files found below $relativeRoot" }
    foreach ($tracked in $trackedFiles) {
        $relative = $tracked.Substring($relativeRoot.Length + 1).Replace('/', '\')
        $sourcePath = Join-Path $sourceRoot $tracked.Replace('/', '\')
        if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) { throw "Tracked package file is missing: $tracked" }
        $destinationPath = Join-Path $Destination $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $destinationPath) -Force | Out-Null
        Copy-Item -LiteralPath $sourcePath -Destination $destinationPath -Force
    }
}

Assert-PackageTarget $output
if ($mirror) { Assert-PackageTarget $mirror }
if (-not $SkipArchive -and $archive) { Assert-PackageTarget $archive }

if ($Channel -eq 'RELEASE' -and (![string]::IsNullOrWhiteSpace($UserConfigPath))) {
    throw 'Public RELEASE packages must not include UserGraphics.json. Ship RendererDefaults.json only.'
}
if ($Channel -eq 'RELEASE' -and ($SkipPipelineLibrary -or [string]::IsNullOrWhiteSpace($PipelineLibrary))) {
    throw 'A RELEASE package must provide a validated preloaded PipelineLibrary. Use RELEASE-CANDIDATE for compile-on-device testing.'
}

$dll = Join-Path $BuildDirectory "PIXLRenderer.dll"
$required = @(
    $dll,
    (Join-Path $sourceRoot "distribution\Shaders"),
    (Join-Path $sourceRoot "distribution\Shaders\Common\Color.hlsli"),
    (Join-Path $sourceRoot "pipeline\Camera Suite\Kernels\CameraSuite\HDROutputCS.hlsl"),
    (Join-Path $sourceRoot "distribution\SKSE\Plugins\PIXLRenderer\SettingsDefault.json"),
    (Join-Path $sourceRoot "COPYING")
)
foreach ($path in $required) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing verified artifact: $path" }
}

# A public binary must carry the legal notices that describe the source and
# every bundled dependency. Fail closed if the release staging tree cannot
# provide the core notice set.
$requiredNotices = @(
    'COPYING',
    'EXCEPTIONS.md',
    'ATTRIBUTION.md',
    'THIRD_PARTY_NOTICES.md',
    'SOURCE-AND-CREDITS.md'
)

function Assert-NoticeSet([string]$Root) {
    foreach ($notice in $requiredNotices) {
        $path = Join-Path $Root "SKSE\Plugins\PIXL\Documentation\$notice"
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Release notice is missing from staging input: $path"
        }
    }
}

Remove-PackageItem $output
New-Item -ItemType Directory -Path $output -Force | Out-Null

$shaderRoot = Join-Path $output "Shaders"
$moduleCatalog = Join-Path $shaderRoot "PIXL\Modules"
$pluginRoot = Join-Path $output "SKSE\Plugins\PIXL"
$configRoot = Join-Path $pluginRoot "Config"
$profileRoot = Join-Path $pluginRoot "Profiles"
$interfaceRoot = Join-Path $pluginRoot "Interface"
New-Item -ItemType Directory -Path $shaderRoot,$moduleCatalog,$configRoot,$profileRoot,$interfaceRoot -Force | Out-Null

# One closed shader pipeline: Skyrim entry points plus every integrated PIXL kernel.
Copy-Tree (Join-Path $sourceRoot "distribution\Shaders") $shaderRoot
Get-ChildItem -LiteralPath (Join-Path $sourceRoot "pipeline") -Directory | Sort-Object Name | ForEach-Object {
    $descriptor = Join-Path $_.FullName "Module.ini"
    $kernels = Join-Path $_.FullName "Kernels"
    if (-not (Test-Path -LiteralPath $descriptor)) { throw "Missing PIXL module descriptor: $descriptor" }
    $descriptorText = Get-Content -LiteralPath $descriptor -Raw
    # Preserve the shipping boundary: retired source and descriptors are not
    # runtime payloads. Guarded historical includes are checked by the audit.
    if ($descriptorText -match '(?im)^\s*Pipeline\s*=\s*Retired\s*$') {
        return
    }
    $idMatch = [regex]::Match($descriptorText, '(?m)^\s*Id\s*=\s*([^\r\n]+?)\s*$')
    if (-not $idMatch.Success) { throw "Module descriptor has no Id: $descriptor" }
    $moduleId = $idMatch.Groups[1].Value.Trim()
    Copy-Item -LiteralPath $descriptor -Destination (Join-Path $moduleCatalog ($moduleId + ".ini")) -Force
    # Kernel directories can also contain ignored, machine-local SDK runtimes.
    # Public packages must contain only reviewable files recorded in Git.
    if (Test-Path -LiteralPath $kernels) { Copy-TrackedTree $kernels $shaderRoot }
}

# The experimental NR runtime is intentionally not stored in Git. Include it
# only through an explicit release input so ignored local files cannot leak into
# a package merely because they happen to be beside tracked shader assets.
if (-not [string]::IsNullOrWhiteSpace($NeuralRuntimePath)) {
    $resolvedNeuralRuntime = (Resolve-Path -LiteralPath $NeuralRuntimePath).Path
    if ([IO.Path]::GetFileName($resolvedNeuralRuntime) -cne 'nvngx_dlssnr.dll') {
        throw "Unexpected Neural Rendering runtime filename: $resolvedNeuralRuntime"
    }
    $neuralVersionInfo = (Get-Item -LiteralPath $resolvedNeuralRuntime).VersionInfo
    $neuralVersion = $neuralVersionInfo.FileVersion
    if ($neuralVersionInfo.FileMajorPart -ne 310 -or $neuralVersionInfo.FileMinorPart -ne 8) {
        throw "PIXL requires the validated DLSSNR 310.8.x contract; found '$neuralVersion'"
    }
    $neuralDestination = Join-Path $shaderRoot 'ImageReconstruction\Streamline\nvngx_dlssnr.dll'
    New-Item -ItemType Directory -Path (Split-Path -Parent $neuralDestination) -Force | Out-Null
    Copy-Item -LiteralPath $resolvedNeuralRuntime -Destination $neuralDestination -Force
    $neuralSignature = Get-AuthenticodeSignature -LiteralPath $neuralDestination
    if ($neuralSignature.Status -ne [System.Management.Automation.SignatureStatus]::Valid) {
        Write-Warning "Neural Rendering runtime signature status is $($neuralSignature.Status). Nexus may flag this DLL."
    }
}

# Validate the optional DLSS-G runtime as a coherent package before release.
$sidecarRuntime = Join-Path $shaderRoot 'ImageReconstruction\StreamlineDX12'
if (Test-Path -LiteralPath $sidecarRuntime) {
    & (Join-Path $PSScriptRoot 'TestPixlSidecarRuntime.ps1') -RuntimeDirectory $sidecarRuntime
}

# Native data paths that Skyrim or the corresponding PIXL hook consumes directly.
Copy-Tree (Join-Path $sourceRoot "pipeline\Radiant Grid\Assets\LightProfiles") (Join-Path $output "ParticleLights")
Copy-Tree (Join-Path $sourceRoot "pipeline\Terrain Occlusion\Assets\HeightMaps") (Join-Path $output "textures\heightmaps")
Copy-Tree (Join-Path $sourceRoot "pipeline\Waterbody\Assets\Meshes\Water") (Join-Path $output "meshes\water")
Copy-Tree (Join-Path $sourceRoot "pipeline\Waterbody\Assets\WorldWater") (Join-Path $pluginRoot "World\Water")
Copy-Item -LiteralPath (Join-Path $sourceRoot "pipeline\Terrain Field\Assets\Plugin\PIXL-TerrainField.esp") -Destination $output -Force

# PIXL runtime identity and compact presentation assets.
Copy-Item -LiteralPath $dll -Destination (Join-Path $output "SKSE\Plugins\PIXLRenderer.dll") -Force
$runtimeSource = Join-Path $sourceRoot "distribution\SKSE\Plugins\PIXLRenderer"
Copy-Item -LiteralPath (Join-Path $runtimeSource "SettingsDefault.json") -Destination (Join-Path $configRoot "RendererDefaults.json") -Force
if (-not [string]::IsNullOrWhiteSpace($UserConfigPath)) {
    $resolvedUserConfig = (Resolve-Path -LiteralPath $UserConfigPath).Path
    try {
        Get-Content -LiteralPath $resolvedUserConfig -Raw | ConvertFrom-Json | Out-Null
    } catch {
        throw "Invalid user graphics configuration '$resolvedUserConfig': $($_.Exception.Message)"
    }
    Copy-Item -LiteralPath $resolvedUserConfig -Destination (Join-Path $configRoot "UserGraphics.json") -Force
}
Copy-Item -LiteralPath (Join-Path $runtimeSource "Presets\PIXL-Renderer-Live-Tested.json") -Destination (Join-Path $profileRoot "PIXL-Golden-Baseline.json") -Force
New-Item -ItemType Directory -Path (Join-Path $interfaceRoot "Themes"),(Join-Path $interfaceRoot "Locale") -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $runtimeSource "Themes\PIXL.json") -Destination (Join-Path $interfaceRoot "Themes\PIXL.json") -Force
Copy-Item -LiteralPath (Join-Path $runtimeSource "Translations\en.json") -Destination (Join-Path $interfaceRoot "Locale\en.json") -Force
Copy-Tree (Join-Path $runtimeSource "QualityPreviews") (Join-Path $interfaceRoot "QualityPreviews")

$presentationSource = Join-Path $sourceRoot "distribution\Interface\PIXLRenderer"
Copy-Tree (Join-Path $presentationSource "Fonts\Jost") (Join-Path $interfaceRoot "Fonts\Jost")
Copy-Tree (Join-Path $presentationSource "Fonts\Sanguis") (Join-Path $interfaceRoot "Fonts\Sanguis")
Copy-Tree (Join-Path $presentationSource "Visuals\Brand") (Join-Path $interfaceRoot "Visuals\Brand")

$pipelineCount = 0
$includePipelineLibrary = -not $SkipPipelineLibrary -and -not [string]::IsNullOrWhiteSpace($PipelineLibrary)
if ($includePipelineLibrary) {
    $pipelineRoot = [IO.Path]::GetFullPath($PipelineLibrary)
    $libraryIni = Join-Path $pipelineRoot "Library.ini"
    if (-not (Test-Path -LiteralPath $libraryIni)) { throw "Missing validated PIXL pipeline metadata: $libraryIni" }
    $metadata = Get-Content -LiteralPath $libraryIni -Raw
    if ($metadata -notmatch 'Layout\s*=\s*PIXL\.StageShard\.v1') { throw "Pipeline library is not PIXL.StageShard.v1" }
    if ($metadata -notmatch 'ShaderABI\s*=\s*PIXL\.SharedBuffers\.20260902\.1') { throw "Pipeline library was built for an incompatible PIXL shared-shader ABI" }
    $pipelineCount = (Get-ChildItem -LiteralPath $pipelineRoot -File -Recurse -Filter "*.pixlbin").Count
    if ($pipelineCount -lt 3000) { throw "Pipeline library is incomplete ($pipelineCount stages; expected at least 3000)" }
    # Public caches contain compiled stages and identity metadata, never mod-manager
    # markers or unrelated files from the owner's live installation.
    $cacheOutput = Join-Path $output 'PIXL\PipelineLibrary'
    New-Item -ItemType Directory -Path $cacheOutput -Force | Out-Null
    Copy-Item -LiteralPath $libraryIni -Destination (Join-Path $cacheOutput 'Library.ini')
    $cacheSections = @{}
    $section = ''
    foreach ($line in ($metadata -split "`r?`n")) {
        if ($line -match '^\s*\[([^\]]+)\]') { $section = $matches[1]; $cacheSections[$section] = @{} }
        elseif ($section -and $line -match '^\s*([^=]+?)\s*=\s*(.*?)\s*$') { $cacheSections[$section][$matches[1]] = $matches[2] }
    }
    # Product version is provenance only. ShaderCache validates the shared
    # layout/ABI and each active module independently, so an older
    # ABI-compatible GOG cache remains valid for this beta build.
    if ([string]::IsNullOrWhiteSpace($cacheSections['Cache']['PluginVersion'])) {
        throw 'Pipeline cache metadata has no product-version provenance.'
    }
    foreach ($descriptor in Get-ChildItem -LiteralPath $moduleCatalog -Filter '*.ini' -File) {
        $moduleText = Get-Content -LiteralPath $descriptor.FullName -Raw
        $id = [regex]::Match($moduleText, '(?m)^\s*Id\s*=\s*([^\r\n]+)').Groups[1].Value.Trim()
        $version = [regex]::Match($moduleText, '(?m)^\s*Version\s*=\s*([^\r\n]+)').Groups[1].Value.Trim()
        if (!$cacheSections.ContainsKey($id) -or $cacheSections[$id]['Version'] -ne $version) { throw "Stale/missing module cache identity: $id (expected $version)" }
    }
    foreach ($stage in Get-ChildItem -LiteralPath $pipelineRoot -File -Recurse -Filter '*.pixlbin') {
        $relative = $stage.FullName.Substring($pipelineRoot.TrimEnd('\').Length + 1)
        if ($relative -notmatch '^(Vertex|Pixel|Compute)[\\/]') { throw "Unrecognized cache stage path: $relative" }
        $cacheDestination = Join-Path $cacheOutput $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $cacheDestination) -Force | Out-Null
        Copy-Item -LiteralPath $stage.FullName -Destination $cacheDestination
    }

    # ShaderCache uses file mtimes as a fast source-change check when the file
    # watcher is disabled. Stamp validated binaries after the source payload is
    # copied so archive extraction cannot make a valid cache look stale.
    $cacheStamp = [DateTime]::UtcNow
    Get-ChildItem -LiteralPath (Join-Path $output "PIXL\PipelineLibrary") -File -Recurse -Filter '*.pixlbin' |
        ForEach-Object { $_.LastWriteTimeUtc = $cacheStamp }
}

if ($Channel -eq 'RELEASE' -and (Get-ChildItem -LiteralPath $output -Recurse -File -Filter 'UserGraphics.json')) {
    throw 'Public release unexpectedly contains UserGraphics.json.'
}
$documentationRoot = Join-Path $pluginRoot 'Documentation'
New-Item -ItemType Directory -Path $documentationRoot -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $sourceRoot 'distribution\PIXL-RENDERER-README.md') -Destination $output -Force
Copy-Item -LiteralPath (Join-Path $sourceRoot 'distribution\SOURCE-AND-CREDITS.md') -Destination $documentationRoot -Force
Copy-Item -LiteralPath (Join-Path $sourceRoot 'docs\MOD_COMPATIBILITY.md') -Destination $documentationRoot -Force
Copy-Item -LiteralPath (Join-Path $sourceRoot 'extern\ReShade\LICENSE.md') -Destination (Join-Path $documentationRoot 'ReShade-API-LICENSE.md') -Force
Copy-Item -LiteralPath (Join-Path $sourceRoot "docs\ImageReconstruction\DLSSG_SM86_INTEGRATION.md") -Destination $documentationRoot -Force
foreach ($document in @("COPYING", "EXCEPTIONS.md", "ATTRIBUTION.md", "THIRD_PARTY_NOTICES.md", "SOURCE-AND-CREDITS.md")) {
    $documentSource = Join-Path $sourceRoot $document
    if ($document -eq 'SOURCE-AND-CREDITS.md') {
        $documentSource = Join-Path $sourceRoot 'distribution\SOURCE-AND-CREDITS.md'
    }
    Copy-Item -LiteralPath $documentSource -Destination $documentationRoot -Force
}
Assert-NoticeSet $output

# Every vendor DLL must remain next to a licence/notice file in the package.
# This catches accidental staging changes without asserting that a proprietary
# vendor licence is GPL-compatible; those terms remain component-specific.
$vendorRoot = Join-Path $shaderRoot 'ImageReconstruction'
if (Test-Path -LiteralPath $vendorRoot) {
    foreach ($vendorDll in Get-ChildItem -LiteralPath $vendorRoot -File -Recurse -Filter '*.dll') {
        $noticeFiles = @(Get-ChildItem -LiteralPath $vendorDll.Directory.FullName -File |
            Where-Object { $_.Name -match '(?i)(license|notice|third)' })
        if ($noticeFiles.Count -eq 0) {
            throw "Vendor runtime has no adjacent licence/notice: $($vendorDll.FullName)"
        }
    }
}

# Nexus cannot scan archives nested inside the upload. Vendor runtime DLLs must
# also retain a valid publisher signature; a HashMismatch is never releasable.
$nestedArchives = @(Get-ChildItem -LiteralPath $output -File -Recurse | Where-Object {
    $_.Extension -match '^\.(zip|7z|rar|tar|gz|bz2|xz)$'
})
if ($nestedArchives.Count) {
    throw "Public package contains nested archive(s): $($nestedArchives.FullName -join ', ')"
}
$vendorRuntimeRoot = Join-Path $shaderRoot 'ImageReconstruction'
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

$manifestFiles = Get-ChildItem -LiteralPath $output -File -Recurse | Sort-Object FullName | ForEach-Object {
    [ordered]@{
        path = $_.FullName.Substring($output.Length + 1).Replace('\', '/')
        bytes = $_.Length
        sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
    }
}
[ordered]@{
    product = "PIXL Renderer"
    title = "PIXL Renderer v1.0.2a"
    version = "1.0.2a"
    requirements = @([ordered]@{
        id = "EngineFixes"
        path = "SKSE/Plugins/EngineFixes.dll"
        required = $true
        note = "Install the Engine Fixes release matching the user's Skyrim SE runtime; PIXL does not bundle this third-party dependency."
    })
    sourceCommit = $sourceCommit
    sourceWorkingTreeDirty = $sourceWorkingTreeDirty
    reproducibleFidelityFXPatchApplied = $reproducibleDependencyPatch
    sourceUrl = "https://github.com/pixlmusic/PIXL-Renderer/tree/$sourceCommit"
    channel = $Channel
    executable = "SKSE/Plugins/PIXLRenderer.dll"
    dataRoot = "SKSE/Plugins/PIXL"
    exclusiveRenderer = $true
    cacheMode = if ($includePipelineLibrary) { "preloaded" } else { "compile-on-device" }
    preloadedPipelineStages = $pipelineCount
    shaderCompilePattern = "PIXL.StageShard.v1"
    shaderABI = "PIXL.SharedBuffers.20260902.1"
    generatedUtc = [DateTime]::UtcNow.ToString("o")
    files = $manifestFiles
} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $output "PIXL-RENDERER.manifest.json") -Encoding utf8

& (Join-Path $PSScriptRoot 'VerifyPixlPackageManifest.ps1') -PackageDirectory $output

if ($mirror) {
    if (-not [string]::Equals($mirror, $output, [StringComparison]::OrdinalIgnoreCase)) {
        Remove-PackageItem $mirror
        New-Item -ItemType Directory -Path (Split-Path -Parent $mirror) -Force | Out-Null
        New-Item -ItemType Junction -Path $mirror -Target $output -Force | Out-Null
    }
}

if (-not $SkipArchive -and $archive) {
    New-Item -ItemType Directory -Path (Split-Path -Parent $archive) -Force | Out-Null
    if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force }
    $sevenZipCommand = Get-Command 7z.exe -ErrorAction SilentlyContinue
    $sevenZip = @(
        $(if ($sevenZipCommand) { $sevenZipCommand.Source }),
        $(if (${env:ProgramFiles}) { Join-Path ${env:ProgramFiles} "7-Zip\7z.exe" }),
        $(if (${env:ProgramFiles(x86)}) { Join-Path ${env:ProgramFiles(x86)} "7-Zip\7z.exe" })
    ) |
        Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if ($sevenZip) {
        Push-Location $output
        try {
            & $sevenZip a -tzip -mx=7 -mmt=on $archive "*"
            if ($LASTEXITCODE -ne 0) { throw "7-Zip failed with exit code $LASTEXITCODE" }
        } finally {
            Pop-Location
        }
    } else {
        Compress-Archive -Path (Join-Path $output "*") -DestinationPath $archive -CompressionLevel Optimal
    }
    $hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
    Set-Content -LiteralPath ($archive + ".sha256") -Value "$hash  $([IO.Path]::GetFileName($archive))" -Encoding ascii
    Write-Host "Archive: $archive"
    Write-Host "SHA256: $hash"
}

Write-Host "PIXL live-test stage: $output"
if ($mirror) { Write-Host "Current test path: $mirror" }
