param(
    [Parameter(Mandatory = $true)]
    [string]$Name,

    [string]$Branch = $Name,
    [string]$Path,
    [string]$StartPoint = "HEAD",
    [switch]$NoSubmodules,
    [switch]$ForcePresetCopy
)

$repoRoot = ([string](& git rev-parse --show-toplevel 2>$null)).Trim()
if ($LASTEXITCODE -ne 0 -or -not $repoRoot) {
    Write-Error "Run this script from within a git checkout or worktree for this repository."
    exit 1
}

$commonDir = ([string](& git rev-parse --path-format=absolute --git-common-dir 2>$null)).Trim()
if ($LASTEXITCODE -ne 0 -or -not $commonDir) {
    Write-Error "Failed to resolve the repository common git directory."
    exit 1
}

$mainRepoRoot = Split-Path $commonDir -Parent
$repoName = Split-Path $mainRepoRoot -Leaf
$defaultWorktreeRoot = Join-Path (Split-Path $mainRepoRoot -Parent) ($repoName + ".worktrees")

$pathWasExplicit = -not [string]::IsNullOrWhiteSpace($Path)
if (-not $pathWasExplicit) {
    $Path = Join-Path $defaultWorktreeRoot $Name
}

$Path = [System.IO.Path]::GetFullPath($Path)
$defaultWorktreeRoot = [System.IO.Path]::GetFullPath($defaultWorktreeRoot).TrimEnd('\')
if (-not $pathWasExplicit) {
    $allowedPrefix = $defaultWorktreeRoot + [System.IO.Path]::DirectorySeparatorChar
    if (-not $Path.StartsWith($allowedPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Worktree name must resolve below the default worktree directory: $defaultWorktreeRoot"
    }
}
$mainRepoRoot = [System.IO.Path]::GetFullPath($mainRepoRoot)
if ([string]::Equals($Path.TrimEnd('\'), $mainRepoRoot.TrimEnd('\'), [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Worktree target cannot be the main checkout: $mainRepoRoot"
}
$branchCheck = & git -C $mainRepoRoot check-ref-format --branch $Branch 2>$null
if ($LASTEXITCODE -ne 0) {
    throw "Invalid worktree branch name: $Branch"
}

$branchRef = "refs/heads/$Branch"
& git -C $mainRepoRoot show-ref --verify --quiet $branchRef
$branchStatus = $LASTEXITCODE
if ($branchStatus -gt 1) {
    throw "Unable to inspect local branch: $Branch"
}
$branchExists = $branchStatus -eq 0
if (-not $branchExists) {
    $startRevision = "${StartPoint}^{commit}"
    & git -C $mainRepoRoot rev-parse --verify --quiet --end-of-options $startRevision 2>$null | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Start point does not resolve to a commit: $StartPoint"
    }
}

$targetParent = Split-Path $Path -Parent
if (-not (Test-Path -LiteralPath $Path)) {
    [System.IO.Directory]::CreateDirectory($targetParent) | Out-Null
}

if (Test-Path -LiteralPath $Path) {
    Write-Error "Target path already exists: $Path"
    exit 1
}

if ($branchExists) {
    Write-Host "Creating worktree for existing branch '$Branch' at $Path"
    & git -C $mainRepoRoot worktree add $Path $Branch
}
else {
    Write-Host "Creating worktree at $Path with new branch '$Branch' from '$StartPoint'"
    & git -C $mainRepoRoot worktree add -b $Branch $Path $StartPoint
}

if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

if (-not $NoSubmodules) {
    Write-Host "Initializing submodules in new worktree"
    & git -C $Path submodule update --init --recursive
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Submodule initialization failed. The worktree was created but may be incomplete."
        exit $LASTEXITCODE
    }
}

$sourcePreset = Join-Path $mainRepoRoot "CMakeUserPresets.json"
$targetPreset = Join-Path $Path "CMakeUserPresets.json"

if (Test-Path -LiteralPath $sourcePreset) {
    if ((-not (Test-Path -LiteralPath $targetPreset)) -or $ForcePresetCopy) {
        try {
            Copy-Item -LiteralPath $sourcePreset -Destination $targetPreset -Force -ErrorAction Stop
            Write-Host "Copied CMakeUserPresets.json into the new worktree"
        }
        catch {
            Write-Error "Failed to copy CMakeUserPresets.json: $($_.Exception.Message)"
            exit 1
        }
    }
    else {
        Write-Host "Skipped preset copy because the worktree already has CMakeUserPresets.json"
    }
}
else {
    Write-Host "No CMakeUserPresets.json found in the main repo checkout; skipping preset copy"
}

Write-Host "Worktree ready: $Path"
