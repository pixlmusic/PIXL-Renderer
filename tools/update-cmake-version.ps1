param(
    [Parameter(Mandatory = $true)]
    [string]$Tag
)

if ($Tag -notmatch '^v?(?<Version>\d+\.\d+\.\d+)$') {
    throw "Invalid version '$Tag'. Use vMAJOR.MINOR.PATCH or MAJOR.MINOR.PATCH."
}
$version = $Matches.Version

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$cmakeFile = Join-Path $repoRoot 'CMakeLists.txt'

if (-not (Test-Path -LiteralPath $cmakeFile -PathType Leaf)) {
    throw "CMakeLists.txt not found at the repository root: $cmakeFile"
}

$content = [System.IO.File]::ReadAllText($cmakeFile)
$versionLines = @(
    [pscustomobject]@{
        Name = 'project VERSION'
        Pattern = '(?m)^(?<prefix>[ \t]*VERSION[ \t]+)(?<version>\d+\.\d+\.\d+)(?<suffix>[ \t]*(?:#.*)?)$'
    },
    [pscustomobject]@{
        Name = 'PIXL_DISPLAY_VERSION'
        Pattern = '(?m)^(?<prefix>[ \t]*set\(PIXL_DISPLAY_VERSION[ \t]+")(?<version>\d+\.\d+\.\d+)(?<suffix>"[ \t]*\))$'
    }
)
$updated = $content
foreach ($line in $versionLines) {
    $lineMatches = [regex]::Matches($updated, $line.Pattern)
    if ($lineMatches.Count -ne 1) {
        throw "Expected exactly one $($line.Name) line in $cmakeFile; found $($lineMatches.Count)."
    }

    $match = $lineMatches[0]
    $group = $match.Groups['version']
    $relativeStart = $group.Index - $match.Index
    $replacementLine = $match.Value.Substring(0, $relativeStart) + $version +
        $match.Value.Substring($relativeStart + $group.Length)
    $updated = $updated.Substring(0, $match.Index) + $replacementLine +
        $updated.Substring($match.Index + $match.Length)
}

if ($updated -ceq $content) {
    Write-Host "CMakeLists.txt already at version $version. No change needed."
    exit 0
}

Write-Host "Updating CMake project and PIXL display versions to $version."
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText($cmakeFile, $updated, $utf8NoBom)
Write-Host "CMakeLists.txt updated."
