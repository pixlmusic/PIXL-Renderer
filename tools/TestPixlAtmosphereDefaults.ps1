[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$defaults = Get-Content -LiteralPath (Join-Path $root 'distribution/SKSE/Plugins/PIXLRenderer/SettingsDefault.json') -Raw | ConvertFrom-Json
$header = [string](Get-Content -LiteralPath (Join-Path $root 'engine/Modules/Atmosphere.h') -Raw)
$source = [string](Get-Content -LiteralPath (Join-Path $root 'engine/Modules/Atmosphere.cpp') -Raw)
$serialization = $source.Substring($source.IndexOf('NLOHMANN_DEFINE'), $source.IndexOf('namespace') - $source.IndexOf('NLOHMANN_DEFINE'))
$count = 0
foreach ($property in $defaults.Atmosphere.PSObject.Properties) {
    $name = [regex]::Escape($property.Name)
    $match = [regex]::Match($header, "\b(uint|float|float4) $name = ([^;]+);")
    if (-not $match.Success -or $serialization -notmatch "\b$name\b") { throw "Missing Atmosphere field/serialization: $name" }
    $values = @($match.Groups[2].Value.Trim(' ', '{', '}') -split ',' | ForEach-Object {
        [single]::Parse($_.Trim().TrimEnd('f'), [Globalization.CultureInfo]::InvariantCulture)
    })
    $expected = @($property.Value)
    if ($values.Count -ne $expected.Count) { throw "Atmosphere vector size mismatch: $name" }
    for ($i = 0; $i -lt $values.Count; $i++) {
        if ([single]::IsNaN($values[$i]) -or [single]::IsInfinity($values[$i]) -or $values[$i] -ne [single]$expected[$i]) {
            throw "Atmosphere default mismatch: $name [$i]"
        }
    }
    $count++
}
$serializedCount = [regex]::Matches($serialization, '(?m)^\s+\w+[,)]').Count
if ($count -ne $serializedCount) { throw "Atmosphere defaults cover $count fields but serialization has $serializedCount." }
Write-Output "PASS: $count serialized Atmosphere defaults match C++ float32 values."
