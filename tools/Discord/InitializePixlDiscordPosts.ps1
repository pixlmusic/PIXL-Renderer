[CmdletBinding()]
param([string]$SecretPath = (Join-Path $env:LOCALAPPDATA 'PIXLRenderer\discord-bot-token.dpapi'))

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $SecretPath)) { throw 'Saved PIXL Discord bot credential not found.' }
$secureToken = ConvertTo-SecureString (Get-Content -LiteralPath $SecretPath -Raw)
$tokenPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secureToken)
try { $botToken = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($tokenPointer) }
finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($tokenPointer) }
$headers = @{ Authorization = "Bot $botToken"; 'User-Agent' = 'DiscordBot (https://github.com/PIXLRenderer, 1.0)' }
$baseUri = 'https://discord.com/api/v10'

$posts = @(
    @{ id='1552232264979718179'; content=@'
# Welcome to PIXL Renderer
PIXL is a real-time rendering framework for **Skyrim Special Edition**. This server is for releases, setup help, compatibility reports, visual work, and thoughtful renderer discussion.

**New here?** Read #rules and #getting-started first. Find official updates in #announcements and #release-notes. Need help? Use #install-help, #bug-reports, or #mod-conflicts. Share your work in #screenshots and #photo-mode-gallery.

Please keep support reports specific and kind. Screenshots, logs, and reproduction steps make a huge difference.
'@ }
    @{ id='1552232264979718180'; content=@'
# Community guidelines
• Be respectful and constructive. Critique results and ideas, not people.
• Keep discussion in the relevant channel; use one thread per distinct bug or conflict.
• Share only files you have permission to redistribute. Do not post private keys, access tokens, personal data, or pirated software.
• Do not spam, harass, or post unsafe links. Staff may move or remove off-topic or harmful content.
• When asking for help, include enough detail for someone else to reproduce the issue.
'@ }
    @{ id='1552232264979718181'; content=@'
# Getting started
1. Install PIXL Renderer from its official release package and follow the included installer/readme.
2. Launch Skyrim once and let the renderer finish any first-run shader compilation.
3. If something looks wrong, note your **PIXL version**, **Skyrim version**, mod manager, and whether it happens with other graphics mods enabled.
4. Ask in #install-help. For a reproducible defect, use #bug-reports; for a suspected interaction with another mod, use #mod-conflicts.

The PIXL log is usually under `Documents/My Games/Skyrim Special Edition/SKSE/PIXLRenderer.log`. Share relevant excerpts, not passwords or tokens. Existing shader caches and user settings can matter when diagnosing a problem—please do not delete them unless support specifically asks.
'@ }
    @{ id='1552232265482772493'; content=@'
# Bug reports
Please start a **new thread for each issue** and include:
• PIXL version and Skyrim SE/AE version
• A short title and what you expected versus what happened
• Exact steps to reproduce, including location, weather, camera mode, and setting changes if relevant
• Screenshot or short clip when the defect is visual
• Relevant `PIXLRenderer.log` excerpt and any shader compilation error
• Whether disabling another graphics mod changes the result

Avoid posting a whole log if a small relevant excerpt will do. Never post secrets or private data.
'@ }
    @{ id='1552232265482772494'; content=@'
# Mod compatibility reports
Use **one thread per mod interaction**. Name both PIXL and the other mod/version in the title. Include your mod manager, which installation wins any file overwrite, the affected module, and before/after screenshots if possible.

For shader problems, mention what owns `Data/Shaders` and whether Community Shaders, an upscaler, ENB/ReShade, weather, water, or material packs are involved. A conflict report is most useful when you can describe a repeatable test, not just a load order dump.
'@ }
    @{ id='1552232265268854819'; content=@'
# PIXL screenshots
Show us what the renderer looks like in your game. A location, time of day, weather, and a few notable visual mods help others understand the result. For finished compositions, use #photo-mode-gallery; for setup details or shared looks, use #setups-and-presets.
'@ }
)

try {
    foreach ($post in $posts) {
        $uri = "$baseUri/channels/$($post.id)/messages"
        $existing = Invoke-RestMethod -Uri "${uri}?limit=1" -Headers $headers -Method Get
        if (@($existing).Count -gt 0) {
            Write-Output "Skipped $($post.id): channel already has a message."
            if (@($existing).Count -eq 1 -and [string]$existing[0].author.id -eq '1552252362310025286') {
                try {
                    Invoke-RestMethod -Uri "$baseUri/channels/$($post.id)/pins/$($existing[0].id)" -Headers $headers -Method Put | Out-Null
                    Write-Output "Pinned existing post in $($post.id)."
                } catch {
                    Write-Warning "Could not pin existing post in $($post.id): HTTP $([int]$_.Exception.Response.StatusCode)"
                }
            }
            continue
        }
        $json = @{ content = $post.content.Trim(); allowed_mentions = @{ parse = @() } } | ConvertTo-Json -Depth 4 -Compress
        $message = Invoke-RestMethod -Uri $uri -Headers $headers -Method Post -ContentType 'application/json' -Body ([Text.Encoding]::UTF8.GetBytes($json))
        Write-Output "Posted to $($post.id)."
        try {
            Invoke-RestMethod -Uri "$baseUri/channels/$($post.id)/pins/$($message.id)" -Headers $headers -Method Put | Out-Null
            Write-Output "Pinned in $($post.id)."
        } catch {
            Write-Warning "Post succeeded but pin failed in $($post.id): HTTP $([int]$_.Exception.Response.StatusCode)"
        }
        Start-Sleep -Milliseconds 350
    }
} finally {
    $headers.Clear()
    $botToken = $null
}
