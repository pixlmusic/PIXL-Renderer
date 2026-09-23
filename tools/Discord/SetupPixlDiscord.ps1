[CmdletBinding()]
param(
    [switch]$Apply,
    [string]$SecretPath = (Join-Path $env:LOCALAPPDATA 'PIXLRenderer\discord-bot-token.dpapi')
)

$ErrorActionPreference = 'Stop'
$guildId = '1552232263553650762'
$changes = @(
    @{ id='1552232264979718174'; name='STAFF RESOURCES' }
    @{ id='1552232264979718175'; name='staff-guide'; topic='Internal PIXL server notes and moderation guidance.' }
    @{ id='1552232264979718176'; name='bot-operations'; topic='Internal bot setup, commands, and integration notes.' }
    @{ id='1552232264979718177'; name='support-triage'; topic='Internal notes for reproducing and routing support reports.' }
    @{ id='1552232264979718178'; name='01 | START HERE' }
    @{ id='1552232264979718179'; name='welcome'; topic='Welcome to PIXL Renderer: a real-time rendering framework for Skyrim Special Edition.' }
    @{ id='1552232264979718180'; name='rules'; topic='Community guidelines, support etiquette, and safe sharing.' }
    @{ id='1552232264979718181'; name='getting-started'; topic='Install PIXL Renderer, check requirements, and find the right support channel.' }
    @{ id='1552232264979718182'; name='02 | NEWS & RELEASES' }
    @{ id='1552232264979718183'; name='announcements'; topic='Official PIXL Renderer news and important notices.' }
    @{ id='1552232265268854815'; name='release-notes'; topic='Version changes, download links, and upgrade guidance.' }
    @{ id='1552232265268854816'; name='known-issues'; topic='Confirmed issues, workarounds, and current investigation status.' }
    @{ id='1552232265268854817'; name='03 | COMMUNITY' }
    @{ id='1552232265268854818'; name='general'; topic='Talk about PIXL Renderer, Skyrim, and the worlds you create.' }
    @{ id='1552232265268854819'; name='screenshots'; topic='Share PIXL screenshots. Include your location, weather, and relevant mods when useful.' }
    @{ id='1552232265268854820'; name='photo-mode-gallery'; topic='Finished captures, compositions, and photo-mode work.' }
    @{ id='1552232265268854821'; name='tips-and-tricks'; topic='Share tuning tips, workflows, and discoveries.' }
    @{ id='1552232265268854822'; name='setups-and-presets'; topic='Share your setup, load order highlights, and PIXL looks.' }
    @{ id='1552232265268854823'; name='rendering-discussion'; topic='Lighting, materials, water, weather, performance, and renderer tech.' }
    @{ id='1552232265268854824'; name='ideas-and-feedback'; topic='Thoughtful feature ideas and usability feedback.' }
    @{ id='1552232265482772491'; name='04 | HELP & REPORTS' }
    @{ id='1552232265482772492'; name='install-help'; topic='Installation and setup questions. Include PIXL version, Skyrim version, mod manager, and relevant log excerpts.' }
    @{ id='1552232265482772493'; name='bug-reports'; topic='One issue per thread. Include reproduction steps, PIXL version, logs, screenshots, and what you expected.' }
    @{ id='1552232265482772494'; name='mod-conflicts'; topic='Compatibility with other mods, weather, ENB/ReShade, UI, textures, and upscalers. Share load order and symptoms.' }
    @{ id='1552232265482772495'; name='05 | PIXL TEAM' }
    @{ id='1552232265482772496'; name='team-chat'; topic='Private PIXL team discussion.' }
    @{ id='1552232265482772497'; name='report-review'; topic='Private bug and compatibility triage.' }
    @{ id='1552232265482772498'; name='release-planning'; topic='Private release notes and coordination.' }
    @{ id='1552232265482772499'; name='06 | SERVER LOGS' }
    @{ id='1552232265482772500'; name='server-log'; topic='Private server activity log.' }
    @{ id='1552232265684357151'; name='invite-log'; topic='Private invitation log.' }
    @{ id='1552232265684357152'; name='moderation-log'; topic='Private moderation log.' }
)

if (-not $Apply) {
    $changes | ForEach-Object { '{0} -> {1}' -f $_.id, $_.name }
    Write-Output 'Dry run only. Pass -Apply to rename channels and set topics.'
    return
}

if (-not (Test-Path -LiteralPath $SecretPath)) { throw 'Saved PIXL Discord bot credential not found.' }
$secureToken = ConvertTo-SecureString (Get-Content -LiteralPath $SecretPath -Raw)
$tokenPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secureToken)
try { $botToken = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($tokenPointer) }
finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($tokenPointer) }
$headers = @{ Authorization = "Bot $botToken"; 'User-Agent' = 'DiscordBot (https://github.com/PIXLRenderer, 1.0)' }

try {
    $guild = Invoke-RestMethod -Uri "https://discord.com/api/v10/guilds/$guildId" -Headers $headers
    if ($guild.id -ne $guildId -or $guild.name -ne 'PIXL Renderer') { throw 'Unexpected Discord server; no changes made.' }
    $existing = Invoke-RestMethod -Uri "https://discord.com/api/v10/guilds/$guildId/channels" -Headers $headers
    $byId = @{}
    foreach ($channel in $existing) { $byId[[string]$channel.id] = $channel }
    foreach ($change in $changes) {
        $current = $byId[$change.id]
        if (-not $current) { throw "Expected channel $($change.id) is missing; stopped." }
        $body = @{ name = $change.name }
        if ($change.ContainsKey('topic') -and [int]$current.type -eq 0) { $body.topic = $change.topic }
        $json = $body | ConvertTo-Json -Compress
        $bytes = [Text.Encoding]::UTF8.GetBytes($json)
        Invoke-RestMethod -Uri "https://discord.com/api/v10/channels/$($change.id)" -Headers $headers -Method Patch -ContentType 'application/json' -Body $bytes | Out-Null
        Write-Output "Updated $($change.name)"
        Start-Sleep -Milliseconds 300
    }
} finally {
    $headers.Clear()
    $botToken = $null
}
