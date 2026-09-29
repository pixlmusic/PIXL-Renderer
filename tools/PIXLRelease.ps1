[CmdletBinding()]
param(
    [ValidateSet('BuildDeploy', 'Deploy', 'PackageCommitPush')]
    [string]$Action = 'Deploy',
    [string]$Message = 'PIXL Renderer update'
)

$ErrorActionPreference = 'Stop'
$developerTools = Join-Path $PSScriptRoot 'PIXLDeveloperTools.ps1'
if (-not (Test-Path -LiteralPath $developerTools -PathType Leaf)) {
    throw "PIXL developer console is missing: $developerTools"
}

# Compatibility entry point retained for existing shortcuts. All behavior is
# owned by PIXLDeveloperTools.ps1 so build, package, deployment, and Git guards
# cannot drift between two implementations.
switch ($Action) {
    'BuildDeploy' { & $developerTools -Action BuildDeploy }
    'Deploy' { & $developerTools -Action Deploy }
    'PackageCommitPush' { & $developerTools -Action Git -CommitMessage $Message }
}

if ($LASTEXITCODE) { exit $LASTEXITCODE }
