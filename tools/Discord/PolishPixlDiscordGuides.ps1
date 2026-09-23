[CmdletBinding()]
param([string]$SecretPath = (Join-Path $env:LOCALAPPDATA 'PIXLRenderer\discord-bot-token.dpapi'))

$ErrorActionPreference = 'Stop'
$guildId = '1552232263553650762'
$botId = '1552252362310025286'
$api = 'https://discord.com/api/v10'
$nexus = 'https://www.nexusmods.com/skyrimspecialedition/mods/189836'
$github = 'https://github.com/pixlmusic/PIXL-Renderer'
$startCategory = '1552232264979718178'
$newsCategory = '1552232264979718182'
$helpCategory = '1552232265482772491'

if (-not (Test-Path -LiteralPath $SecretPath)) { throw 'Saved PIXL Discord bot credential not found.' }
$secureToken = ConvertTo-SecureString (Get-Content -LiteralPath $SecretPath -Raw)
$tokenPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secureToken)
try { $botToken = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($tokenPointer) }
finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($tokenPointer) }
$headers = @{ Authorization = "Bot $botToken"; 'User-Agent' = 'DiscordBot (https://github.com/PIXLRenderer, 1.0)' }

function Invoke-PixlDiscordJson($method, $uri, $payload) {
    $json = $payload | ConvertTo-Json -Depth 12 -Compress
    Invoke-RestMethod -Uri $uri -Headers $headers -Method $method -ContentType 'application/json' -Body ([Text.Encoding]::UTF8.GetBytes($json))
}

function Get-ChannelByName([string]$name) {
    $current = Invoke-RestMethod -Uri "$api/guilds/$guildId/channels" -Headers $headers
    @($current | Where-Object { $_.name -eq $name }) | Select-Object -First 1
}

function Ensure-GuideChannel([string]$name, [string]$category, [string]$topic, [bool]$readOnly) {
    $found = Get-ChannelByName $name
    if ($found) { return [string]$found.id }
    $payload = @{ name=$name; type=0; parent_id=$category; topic=$topic }
    if ($readOnly) {
        $payload.permission_overwrites = @(@{ id=$guildId; type=0; allow='0'; deny='2048' })
    }
    $created = Invoke-PixlDiscordJson 'Post' "$api/guilds/$guildId/channels" $payload
    Write-Host "Created #$name ($($created.id))"
    return [string]$created.id
}

function Set-PixlCard([string]$channelId, [string]$title, [string]$body, [int]$color) {
    $uri = "$api/channels/$channelId/messages"
    $messages = Invoke-RestMethod -Uri "${uri}?limit=25" -Headers $headers
    $existing = @($messages | Where-Object { [string]$_.author.id -eq $botId -and [int]$_.type -eq 0 } | Select-Object -First 1)
    $card = @{ title=$title; description=$body; color=$color; footer=@{ text='PIXL Renderer | Nexus is the home for downloads and releases' } }
    $payload = @{ content=''; embeds=@($card); allowed_mentions=@{parse=@()} }
    if ($existing.Count -gt 0) {
        $message = Invoke-PixlDiscordJson 'Patch' "$uri/$($existing[0].id)" $payload
        Write-Output "Refined card in channel $channelId"
    } else {
        $message = Invoke-PixlDiscordJson 'Post' $uri $payload
        Write-Output "Added card in channel $channelId"
    }
    try {
        Invoke-RestMethod -Uri "$api/channels/$channelId/pins/$($message.id)" -Headers $headers -Method Put | Out-Null
    } catch {
        Write-Warning "Could not pin $title (HTTP $([int]$_.Exception.Response.StatusCode))."
    }
    Start-Sleep -Milliseconds 300
}

try {
    $guild = Invoke-RestMethod -Uri "$api/guilds/$guildId" -Headers $headers
    if ($guild.name -ne 'PIXL Renderer') { throw 'Unexpected server; no changes made.' }

    $reconstruction = Ensure-GuideChannel 'reconstruction-guide' $startCategory 'Choose TAA, FSR, DLSS or DLAA and understand the quality presets.' $true
    $neural = Ensure-GuideChannel 'neural-rendering-setup' $startCategory 'Optional experimental Neural Rendering setup and restart requirements.' $true
    $frameGen = Ensure-GuideChannel 'frame-generation-setup' $startCategory 'Optional frame-generation requirements and supported proxy guidance.' $true
    $compat = Ensure-GuideChannel 'compatibility-guide' $startCategory 'Engine-level conflicts and currently tested compatible mods.' $true
    $devLog = Ensure-GuideChannel 'development-log' $newsCategory 'Solo-developer progress notes, previews, and experiments.' $true
    $perfHelp = Ensure-GuideChannel 'performance-help' $helpCategory 'GPU timings, hardware details, and performance troubleshooting.' $false

    $welcome = @"
**Skyrim like Skyrim, just more Skyrim.** PIXL Renderer brings lighting, materials, weather, water, terrain, characters, atmosphere, reconstruction, and camera processing into one DX11 renderer.

**Install it. Complete Quick Setup. Play Skyrim.**

This is a **solo project**: PIXL handles development, testing, server management, and support. Experimental systems can change, and replies may take time. Clear reports help enormously.

**Start here:** <#1552232264979718180> • <#1552232264979718181> • <#$reconstruction> • <#$compat>
**Need help?** <#1552232265482772492> • <#1552232265482772493> • <#1552232265482772494>
**Share your world:** <#1552232265268854819> • <#1552232265268854820>

[Nexus — official downloads and release information]($nexus)  •  [GitHub — source and trackable issues]($github)
"@
    Set-PixlCard '1552232264979718179' 'WELCOME // PIXL RENDERER' $welcome 3246543

    $rules = @"
**01  Respect people.** Be constructive; no harassment, hate, spam, or dogpiling.
**02  Share responsibly.** No piracy, unauthorized redistributions, malicious links, or downloads from random mirrors. Never post tokens, private keys, or personal data.
**03  Keep support actionable.** Use the right channel, one issue per thread, and provide versions, logs, screenshots, and steps to reproduce where possible.
**04  Credit creators.** Identify third-party mods and sources accurately. PIXL is independent and is not affiliated with Bethesda, NVIDIA, or the Community Shaders team.
**05  Respect the solo workload.** PIXL is developed and supported by one person; there is no guaranteed response time or fixed update schedule.

For downloads and current requirements, use the [official Nexus page]($nexus). These rules also apply to server DMs and threads.
"@
    Set-PixlCard '1552232264979718180' 'COMMUNITY RULES // READ FIRST' $rules 14251830

    $gettingStarted = @"
**Tested runtimes:** Skyrim SE 1.5.97, Steam AE 1.6.1170, GOG AE 1.6.1179. Match SKSE and Engine Fixes to your runtime; enable `PIXL-TerrainField.esp`.

1. Back up saves and close Skyrim.
2. Install PIXL as a normal Data mod with MO2 or Vortex. If installing manually, use the package instructions and ensure its supplied `Data/Shaders` files win the intended conflicts.
3. Do **not** stack PIXL with Community Shaders, ENB, Kreate, or another engine-level renderer.
4. Launch through SKSE and allow first-run shaders to compile. Complete Quick Setup and choose a quality/reconstruction path.
5. Press **Page Down** for PIXL and **Home** for Photo Mode. If asked to restart, save your game, fully exit, then relaunch.

The release includes a preloaded pipeline library; extra permutations can still compile. Don't benchmark during compilation or routinely delete a healthy cache. Your saved settings override new defaults.

[Full installation details and current requirements]($nexus)
"@
    Set-PixlCard '1552232264979718181' 'GETTING STARTED // FIRST RUN' $gettingStarted 3246543

    $reconstructionText = @"
**Off:** native rendering without temporal reconstruction. **TAA:** full-resolution temporal anti-aliasing. **FSR Quality:** broad GPU support with a performance-oriented render resolution. **DLSS Quality:** NVIDIA RTX upscaling. **DLAA:** NVIDIA reconstruction at native resolution for image quality when GPU headroom allows.

PIXL offers **Fast, Balanced, Enhanced, and Cinematic** quality profiles. Start with Balanced for gameplay; use the profiler before pushing a heavy scene toward Cinematic.

Frame Generation and Neural Rendering are **optional**. Neither is required for PIXL to work. Frame Generation changes displayed smoothness, not Skyrim's simulation or input rate. Some options require a supported GPU and a full game restart.

See <#$neural> and <#$frameGen> for the experimental paths, or [the Nexus guide]($nexus) for the current release instructions.
"@
    Set-PixlCard $reconstruction 'RECONSTRUCTION // CHOOSE YOUR PATH' $reconstructionText 3246543

    $neuralText = @"
**Optional and experimental. The Neural Rendering DLL is not bundled.** Ordinary DLSS, DLAA, FSR, and TAA work without it.

1. In PIXL Quick Setup, choose **Enable Neural Rendering (experimental)** or open **Neural Rendering → NR Setup Guide**.
2. If you decide to obtain a compatible runtime, follow the [RenoDX Discord](https://discord.gg/renodx) `dlss5-forum` discussion for patched DLSS-NR on RTX20/30/40. Use **pinned messages only** and the pinned **ShortFuse** version described in PIXL's [Nexus guide]($nexus). Avoid chat attachments and mirrors; if the post is unavailable, leave NR disabled.
3. Only obtain files you have permission to use. Place `nvngx_dlssnr.dll` at `Skyrim Special Edition/Data/Shaders/ImageReconstruction/Streamline/`.
4. Return to PIXL, use **I'VE COPIED IT — CHECK FILE**, confirm, and save Quick Setup. **Fully exit Skyrim and relaunch through SKSE.** Reloading a save is not enough for the sidecar.

Requires a PIXL-supported NVIDIA adapter, DLSS/DLAA, SDR, and borderless/windowed mode. This is a **third-party experimental runtime**, not an official NVIDIA download. PIXL checks file presence, not authenticity; PIXL does not download or install it. For problems, return to the standard reconstruction paths.
"@
    Set-PixlCard $neural 'NEURAL RENDERING // EXPERIMENTAL SETUP' $neuralText 3246543

    $frameText = @"
**Optional.** Frame Generation can improve displayed smoothness, but it does not speed up Skyrim's simulation or input processing.

For PIXL's Frame Generation or Neural Rendering paths, use **borderless/windowed mode** (`bFull Screen=0` in `SkyrimPrefs.ini`), not exclusive fullscreen. A game restart may be required after enabling a presentation sidecar.

Some NVIDIA setups can optionally use [DLSSG for SM86 by sdli1995 and contributors](https://github.com/sdli1995/dlssg_for_sm86/blob/main/README.en.md). This third-party proxy is **not bundled**. Follow its own instructions: its proxy DLL and `dlssg_sm86.ini` go beside `SkyrimSE.exe`, **not** in Data. Do not overwrite another mod's proxy DLL; install only one supported proxy.

After installing, relaunch Skyrim, open PIXL Camera controls, select the DLSSG backend, and enable Frame Generation. Detection does not guarantee compatibility with every GPU/driver. Standard DLSS/DLAA/FSR/TAA do not need this proxy.

[Current PIXL guidance]($nexus)
"@
    Set-PixlCard $frameGen 'FRAME GENERATION // OPTIONAL PATHS' $frameText 14251830

    $compatText = @"
**PIXL is a standalone engine-level renderer.** Do not run it alongside ENB, Community Shaders, Kreate, another engine-level shader-hook renderer, or a second PIXL installation. ReShade compatibility is untested and unsupported for this release; disable it when reproducing PIXL issues.

**Currently supported or in testing:** Seasons of Skyrim, Turn of the Seasons, texture packs, PBR/Complex Materials, ParallaxGen, SmoothCam, SkyUI, Skyland AIO, Light Placer, Horizon Fix, and a specific SurfaceTides 1.0.2 compatibility option. Lux and Lux CS need broader testing; Cinematic DoF can coexist when you avoid stacking competing DoF effects.

For SurfaceTides, follow the **version-specific FOMOD guidance** on [Nexus]($nexus); do not assume newer DLLs are compatible. Custom meshes, weather mods, and shader-file overwrites can change results. Report repeatable interactions in <#1552232265482772494>.
"@
    Set-PixlCard $compat 'COMPATIBILITY // WHAT STACKS WITH PIXL' $compatText 14251830

    $knownText = @"
PIXL is in **early development**. The [Nexus page]($nexus) is the current source for release notes and known issues.

**Reported for 1.0.3a:** WindowLife curtains and occupants are experimental/broken and should remain disabled; some procedural masks may overflow non-square window panes. ENB/ReShade conversion is not working as intended. Treat experimental Neural Rendering and DLSSG proxy paths as optional and hardware-dependent.

If you can reproduce a new issue, post one thread in <#1552232265482772493> with PIXL/Skyrim versions, GPU/driver, location/weather, affected settings, screenshots, and relevant logs. PIXL is a solo project, so good reports make triage much faster.
"@
    Set-PixlCard '1552232265268854816' 'KNOWN ISSUES // CURRENT TESTING' $knownText 14251830

    $bugText = @"
**One issue per thread.** Use a short title and include: PIXL version, Skyrim runtime, GPU and driver, reconstruction/FG mode, location/weather/time, expected versus actual result, exact steps, screenshots or a short clip, relevant `PIXLRenderer.log` or shader errors, and mods involved.

Check <#1552232265268854816> first. For installation questions use <#1552232265482772492>; for a suspected mod interaction use <#1552232265482772494>; for slow scenes use <#$perfHelp>. GitHub is best for bugs that need long-term tracking: [PIXL issues]($github/issues).

Remove personal data before sharing a log. PIXL is a solo project, so clear reproduction details help more than repeated pings.
"@
    Set-PixlCard '1552232265482772493' 'BUG REPORTS // MAKE IT REPRODUCIBLE' $bugText 3246543

    $conflictText = @"
**One mod interaction per thread.** Name the other mod/version, the affected PIXL module, your mod manager, which installation wins shader/texture/DLL overwrites, and the exact symptom. Add a before/after image or small log excerpt when useful.

PIXL owns its engine-level shader path; check `Data/Shaders` conflicts carefully. Don't stack ENB, Community Shaders, Kreate, or a second PIXL renderer. Texture, mesh, weather, camera, and lighting-placement mods are a different class of compatibility question—see <#$compat> before reporting.

Nexus remains the [official install and compatibility reference]($nexus).
"@
    Set-PixlCard '1552232265482772494' 'MOD CONFLICTS // FILE OWNERSHIP MATTERS' $conflictText 3246543

    $perfText = @"
Please include GPU/VRAM, CPU, resolution, upscaler mode, PIXL quality profile, location/weather, and whether shaders were still compiling. A screenshot of PIXL's **Resource Monitor / GPU hotspots** is more useful than FPS alone.

If possible, compare the same scene with one PIXL module toggled at a time. Do not delete a healthy shader cache simply to test performance. Use one thread per issue and mention any heavy texture, water, weather, or frame-generation mods.
"@
    Set-PixlCard $perfHelp 'PERFORMANCE // MEASURE THE SCENE' $perfText 3246543

    $releaseText = @"
**Nexus is the official home for PIXL downloads, requirements, and release notes.** GitHub holds the source and issue tracker. Discord is for discussion, faster support, screenshots, and work-in-progress previews—not a separate download mirror.

Current release notes: [PIXL Renderer on Nexus]($nexus)
Development source: [PIXL Renderer on GitHub]($github)
"@
    Set-PixlCard '1552232265268854815' 'RELEASES // SOURCE OF TRUTH' $releaseText 14251830

    $devText = @"
PIXL Renderer is built and supported by one person. This channel is for short progress notes, experiments, and preview work—not a fixed release calendar. Features in progress may change or be dropped after testing.

Nexus remains the home for released downloads. For trackable bugs and changes, use [GitHub]($github). For informal ideas, use <#1552232265268854824>.
"@
    Set-PixlCard $devLog 'DEVELOPMENT LOG // EXPERIMENTAL WORK' $devText 3246543

    $rename = @(
        @{ id='1552232264979718174'; name='CREATOR RESOURCES' },
        @{ id='1552232264979718175'; name='creator-guide' },
        @{ id='1552232264979718176'; name='bot-operations' },
        @{ id='1552232264979718177'; name='support-notes' },
        @{ id='1552232265482772495'; name='05 | CREATOR DESK' },
        @{ id='1552232265482772496'; name='creator-chat' },
        @{ id='1552232265482772497'; name='issue-triage' },
        @{ id='1552232265482772498'; name='release-drafts' }
    )
    foreach ($item in $rename) {
        Invoke-PixlDiscordJson 'Patch' "$api/channels/$($item.id)" @{name=$item.name} | Out-Null
    }
    Write-Output 'Private template staff channels relabeled for solo ownership; permission overrides were preserved.'
} finally {
    $headers.Clear()
    $botToken = $null
}
