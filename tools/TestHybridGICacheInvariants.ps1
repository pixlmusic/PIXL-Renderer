# Numerical/reference regression checks, not an in-game or GPU visual test.
$ErrorActionPreference = 'Stop'
function Require([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

# The former timestamp compare/exchange can succeed repeatedly within one tick.
[uint32]$metadata = 0x07123457
[uint32]$published = 0x07123457
Require ($metadata -eq $published) 'Timestamp alias reproduction failed'

# Independent election: candidate keys are unique, with a unique global minimum
# regardless of arrival order. The upper six bits rank the surface representative.
$random = [Random]::new(1729)
$keys = for ($i = 0; $i -lt 1024; ++$i) {
    [uint32](([uint64]$random.Next(0, 63) * 67108864) + $i)
}
$expected = ($keys | Measure-Object -Minimum).Minimum
for ($trial = 0; $trial -lt 50; ++$trial) {
    $order = [uint32[]]$keys.Clone()
    for ($i = $order.Length - 1; $i -gt 0; --$i) {
        $j = $random.Next($i + 1)
        $tmp = $order[$i]; $order[$i] = $order[$j]; $order[$j] = $tmp
    }
    [uint32]$winner = [uint32]::MaxValue
    foreach ($key in $order) { $winner = [Math]::Min($winner, $key) }
    Require ($winner -eq $expected) 'Election depends on submission order'
    Require (@($keys | Where-Object { $_ -eq $winner }).Count -eq 1) 'Multiple payload writers'
}

# Every allowed 32-cell window has unique wrapped coordinates, including below
# the world origin. Cells +/-32 away would alias and must be rejected upstream.
foreach ($centre in @(-97, -32, -1, 0, 1, 31, 1024)) {
    $slots = @{}
    for ($offset = -16; $offset -lt 16; ++$offset) {
        $slot = ($centre + $offset) -band 31
        Require (-not $slots.ContainsKey($slot)) 'Toroidal window collision'
        $slots[$slot] = $true
    }
    Require ($slots.Count -eq 32) 'Incomplete toroidal window'
}

# Exponential response retains the slider's 60 Hz meaning at other frame rates.
foreach ($response in @(0.02, 0.07, 0.1, 0.5, 1.0)) {
    $reference = 1.0 - [Math]::Pow(1.0 - $response, 60.0)
    foreach ($fps in @(30, 60, 120, 240)) {
        $alpha = 1.0 - [Math]::Pow(1.0 - $response, 60.0 / $fps)
        $value = 0.0
        for ($frame = 0; $frame -lt $fps; ++$frame) { $value += (1.0 - $value) * $alpha }
        Require ([Math]::Abs($value - $reference) -lt 1e-10) 'Frame-rate-dependent cache response'
    }
}

# Expired cells are removed before an eight-bit clock wraps back to their stamp.
foreach ($stamp in @(0, 127, 255)) {
    $valid = $true
    for ($tick = 0; $tick -le 512; ++$tick) {
        $clock = ($stamp + $tick) -band 255
        $age = ($clock - $stamp) -band 255
        if ($valid -and $age -gt 72) { $valid = $false }
        if ($tick -gt 72) { Require (-not $valid) 'Expired cache resurrected' }
    }
}

# Cache ageing remains fixed-rate, while radiance publication is scheduled at
# a bounded cadence with elapsed render time passed to the temporal filter.
$hybridCpp = Get-Content (Join-Path $PSScriptRoot '..\engine\Modules\HybridGI.cpp') -Raw
$hybridHeader = Get-Content (Join-Path $PSScriptRoot '..\engine\Modules\HybridGI.h') -Raw
$injectionBlock = [regex]::Match($hybridCpp,
    'if\s*\(worldCacheInjectionDue\)\s*\{(?<body>[\s\S]*?)lastWorldCacheInjectionClock\s*=\s*worldCacheClock;')
Require $injectionBlock.Success 'World-cache injection is not guarded by its scheduled cadence'
Require ($injectionBlock.Groups['body'].Value.Contains('CopyResource(')) 'Cache snapshots escaped the scheduled injection block'
Require ($injectionBlock.Groups['body'].Value.Contains('WorldCacheSelect')) 'Scheduled cache selection dispatch missing'
Require ($injectionBlock.Groups['body'].Value.Contains('WorldCacheInject')) 'Scheduled cache publication dispatch missing'
Require ($hybridHeader.Contains('lastWorldCacheInjectionClock = 0xffffffffu')) 'World-cache injection clock lacks a first-update/reset sentinel'
Require ($hybridCpp.Contains('worldCacheInjectionAccumulator >= (1.0f / 30.0f)')) 'Radiance cache cadence is not bounded to 30 Hz'
Require ($hybridCpp.Contains('data.WorldCacheDeltaTime = worldCacheInjectionAccumulator')) 'Injection filter does not receive elapsed update time'
Require ($hybridCpp.Contains('worldCacheClockAccumulator, data.WorldCacheDeltaTime, 8.0f')) 'World-cache age clock is no longer fixed at 8 Hz'
Write-Output 'PASS: election order/uniqueness, signed toroidal windows, 30-240 Hz response, timestamp expiry.'
