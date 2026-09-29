[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$pattern = 'CameraPosAdjust|CameraPreviousPosAdjust|CameraPosition|WorldPosition|worldPosition|positionWS|PreviousWorldPosition|PrevCameraPosition|ViewProj|PosOffset|OcclusionTransform'
$rows = @(& rg --json -n $pattern engine distribution/Shaders pipeline -g '*.cpp' -g '*.h' -g '*.hpp' -g '*.hlsl' -g '*.hlsli' | ForEach-Object {
    $entry = $_ | ConvertFrom-Json
    if ($entry.type -ne 'match') { return }
    $path = $entry.data.path.text.Replace('\','/')
    $line = $entry.data.lines.text.Trim()
    $space = if ($line -match 'Previous|previous|Prev|prev|MotionVector') { 'Temporal: native current/previous spaces; inspect producer' }
        elseif ($line -match 'ViewProj|OcclusionTransform') { 'Matrix: native camera/light transform; do not rebase blindly' }
        elseif ($line -match 'PosOffset|cell|Cell|Hash|noise|Noise') { 'Spatial identity/grid: preserve persistent addressing' }
        elseif ($line -match 'CameraPosAdjust') { 'Native engine-relative / absolute bridge' }
        else { 'Position: coordinate contract depends on caller' }
    [pscustomobject]@{ Path=$path; Line=$entry.data.line_number; Classification=$space; Evidence=$line }
})
if ($LASTEXITCODE -gt 1) { throw 'Coordinate search failed' }
$output = Join-Path $repo 'docs\release_polish\RENDER_ORIGIN_COORDINATE_OCCURRENCES.csv'
$rows | Sort-Object Path,Line | Export-Csv -LiteralPath $output -NoTypeInformation -Encoding UTF8
Write-Host "Coordinate inventory: $($rows.Count) occurrences across $(@($rows.Path | Sort-Object -Unique).Count) files. Lexical classification; semantic decisions are in PIXL_RENDER_ORIGIN.md."
