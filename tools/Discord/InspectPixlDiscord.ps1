[CmdletBinding()]
param(
    [ValidateSet('SaveToken', 'Inventory')]
    [string]$Action = 'Inventory',
    [ValidatePattern('^[0-9]{17,20}$')]
    [string]$GuildId = '1552232263553650762',
    [string]$SecretPath = (Join-Path $env:LOCALAPPDATA 'PIXLRenderer\discord-bot-token.dpapi')
)

$ErrorActionPreference = 'Stop'

if ($Action -eq 'SaveToken') {
    if (Test-Path -LiteralPath $SecretPath) {
        throw "A local Discord credential already exists at $SecretPath. Remove it yourself before replacing the bot token."
    }

    $secretDirectory = Split-Path -Parent $SecretPath
    New-Item -ItemType Directory -Path $secretDirectory -Force | Out-Null
    $secureToken = Read-Host 'Paste your PIXL setup bot token here (input is hidden)' -AsSecureString
    if ($secureToken.Length -eq 0) { throw 'No bot token entered.' }
    ConvertFrom-SecureString -SecureString $secureToken |
        Set-Content -LiteralPath $SecretPath -Encoding ASCII -NoNewline
    Write-Host 'Bot token saved with Windows user protection. Do not send the token or the credential file in chat.'
    return
}

if (-not (Test-Path -LiteralPath $SecretPath)) {
    throw "No local bot credential found. Run this script with -Action SaveToken in your own PowerShell window first."
}

$secureToken = ConvertTo-SecureString (Get-Content -LiteralPath $SecretPath -Raw)
$tokenPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secureToken)
try {
    $botToken = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($tokenPointer)
} finally {
    [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($tokenPointer)
}

$headers = @{
    Authorization = "Bot $botToken"
    'User-Agent' = 'DiscordBot (https://github.com/PIXLRenderer, 1.0)'
}
$baseUri = 'https://discord.com/api/v10'
$inventoryStep = 'bot identity'
try {
    $bot = Invoke-RestMethod -Uri "$baseUri/users/@me" -Headers $headers -Method Get
    $inventoryStep = 'server details'
    $guild = Invoke-RestMethod -Uri "$baseUri/guilds/$GuildId" -Headers $headers -Method Get
    $inventoryStep = 'channel list'
    $channels = Invoke-RestMethod -Uri "$baseUri/guilds/$GuildId/channels" -Headers $headers -Method Get
    $inventoryStep = 'role list'
    $roles = Invoke-RestMethod -Uri "$baseUri/guilds/$GuildId/roles" -Headers $headers -Method Get
} catch {
    $response = $_.Exception.Response
    $status = if ($response) { [int]$response.StatusCode } else { 'network error' }
    throw "Discord inventory failed during $inventoryStep (HTTP $status). No server changes were made."
} finally {
    $headers.Clear()
    $botToken = $null
}

$channelRows = foreach ($channel in $channels) {
    [pscustomobject]@{
        id = [string]$channel.id
        name = [string]$channel.name
        type = [int]$channel.type
        parent_id = [string]$channel.parent_id
        position = [int]$channel.position
        topic = [string]$channel.topic
        tags = @($channel.available_tags | ForEach-Object { [string]$_.name })
        permission_overwrite_count = @($channel.permission_overwrites).Count
    }
}

$roleRows = foreach ($role in $roles) {
    [pscustomobject]@{
        id = [string]$role.id
        name = [string]$role.name
        position = [int]$role.position
        managed = [bool]$role.managed
    }
}

[pscustomobject]@{
    guild = [pscustomobject]@{
        id = [string]$guild.id
        name = [string]$guild.name
        features = @($guild.features)
        rules_channel_id = [string]$guild.rules_channel_id
        system_channel_id = [string]$guild.system_channel_id
    }
    bot = [pscustomobject]@{ id = [string]$bot.id; username = [string]$bot.username }
    channels = @($channelRows | Sort-Object parent_id, position, name)
    roles = @($roleRows | Sort-Object position -Descending)
}
