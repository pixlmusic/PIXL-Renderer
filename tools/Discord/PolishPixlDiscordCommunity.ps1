[CmdletBinding()]
param([string]$SecretPath = (Join-Path $env:LOCALAPPDATA 'PIXLRenderer\discord-bot-token.dpapi'), [string]$ImageDirectory = (Join-Path $env:USERPROFILE 'Desktop\install images'))

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Net.Http
$guildId = '1552232263553650762'; $botId = '1552252362310025286'; $api = 'https://discord.com/api/v10'
$nexus = 'https://www.nexusmods.com/skyrimspecialedition/mods/189836'
if (-not (Test-Path -LiteralPath $SecretPath)) { throw 'Saved PIXLBOT credential not found.' }
$secure = ConvertTo-SecureString (Get-Content -LiteralPath $SecretPath -Raw)
$ptr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
try { $token = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($ptr) } finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($ptr) }
$headers = @{ Authorization = "Bot $token"; 'User-Agent' = 'PIXLBOT (PIXL Renderer, 1.0)' }

function Json([string]$Method, [string]$Uri, [hashtable]$Body) {
    # Guild channel edits share a conservative Discord rate bucket.
    Start-Sleep -Milliseconds 2200
    $json = $Body | ConvertTo-Json -Depth 12 -Compress
    Invoke-RestMethod -Uri $Uri -Headers $headers -Method $Method -ContentType 'application/json' -Body ([Text.Encoding]::UTF8.GetBytes($json))
}
function Channel([string]$Id, [int]$Icon, [string]$Name, [string]$Topic) {
    # Discord's API rejects emoji in these legacy text-channel identifiers.
    # Keep the tidy lowercase names and put PIXL's visual personality in cards/topics.
    Json 'Patch' "$api/channels/$Id" @{ name = $Name; topic = $Topic } | Out-Null
}
function Rename([string]$Id, [string]$Name) { Json 'Patch' "$api/channels/$Id" @{ name = $Name } | Out-Null }
function EveryonePermissions([string]$ChannelId, [string]$Allow, [string]$Deny) {
    Json 'Put' "$api/channels/$ChannelId/permissions/$guildId" @{ id=$guildId; type=0; allow=$Allow; deny=$Deny } | Out-Null
}
function Card([string]$ChannelId, [string]$Title, [string]$Text, [int]$Color = 3246543) {
    $uri = "$api/channels/$ChannelId/messages"
    $old = @(Invoke-RestMethod -Uri "${uri}?limit=100" -Headers $headers | Where-Object { $_.author.id -eq $botId -and $_.type -eq 0 } | Select-Object -First 1)
    $body = @{ embeds = @(@{title=$Title;description=$Text;color=$Color;footer=@{text='PIXL Renderer | Skyrim, but thicc'}});allowed_mentions=@{parse=@()} }
    if ($old.Count) { $msg = Json 'Patch' "$uri/$($old[0].id)" $body } else { $msg = Json 'Post' $uri $body }
    try { Invoke-RestMethod -Uri "$uri/pins/$($msg.id)" -Headers $headers -Method Put | Out-Null } catch {}
}
function GuideImage([string]$Marker, [string]$Title, [string]$Text, [string]$FileName) {
    $channelId = '1552267699466272829'; $uri = "$api/channels/$channelId/messages"
    if (@(Invoke-RestMethod -Uri "${uri}?limit=100" -Headers $headers | Where-Object { $_.author.id -eq $botId -and $_.content -eq $Marker }).Count) { return }
    $path = Join-Path $ImageDirectory $FileName
    if (-not (Test-Path -LiteralPath $path)) { throw "Guide image missing: $path" }
    $client = [System.Net.Http.HttpClient]::new()
    try {
        $client.DefaultRequestHeaders.Authorization = [System.Net.Http.Headers.AuthenticationHeaderValue]::new('Bot', $token)
        $form = [System.Net.Http.MultipartFormDataContent]::new()
        $payload = @{content=$Marker;embeds=@(@{title=$Title;description=$Text;color=3246543;image=@{url="attachment://$FileName"};footer=@{text='PIXL Renderer | Neural Rendering is optional and experimental'}});allowed_mentions=@{parse=@()}} | ConvertTo-Json -Depth 12 -Compress
        $form.Add([System.Net.Http.StringContent]::new($payload, [Text.Encoding]::UTF8, 'application/json'), 'payload_json')
        $stream = [IO.File]::OpenRead($path)
        try {
            $file = [System.Net.Http.StreamContent]::new($stream); $file.Headers.ContentType = [System.Net.Http.Headers.MediaTypeHeaderValue]::Parse('image/png'); $form.Add($file, 'files[0]', $FileName)
            $response = $client.PostAsync($uri, $form).GetAwaiter().GetResult(); if (-not $response.IsSuccessStatusCode) { throw "Image upload failed: $([int]$response.StatusCode)" }
            $msg = $response.Content.ReadAsStringAsync().GetAwaiter().GetResult() | ConvertFrom-Json
            try { Invoke-RestMethod -Uri "$uri/pins/$($msg.id)" -Headers $headers -Method Put | Out-Null } catch {}
        } finally { $stream.Dispose() }
    } finally { $client.Dispose() }
}

try {
    $guild = Invoke-RestMethod -Uri "$api/guilds/$guildId" -Headers $headers; $bot = Invoke-RestMethod -Uri "$api/users/@me" -Headers $headers
    if ($guild.name -ne 'PIXL Renderer' -or $bot.id -ne $botId -or $bot.username -ne 'PIXLBOT') { throw 'Unexpected Discord identity. No changes made.' }

    # Entry is deliberately just two read-only channels.  The guide channels
    # live under their own category so the welcome flow stays calm and obvious.
    Rename '1552232264979718178' 'WELCOME!'
    Rename '1552232264979718179' 'hello'
    Rename '1552232264979718180' 'rules'
    Rename '1552232264979718182' 'NEWS + RELEASES'
    Rename '1552232265268854817' 'COMMUNITY'
    Rename '1552232265482772491' 'HELP + REPORTS'
    $allChannels = @(Invoke-RestMethod -Uri "$api/guilds/$guildId/channels" -Headers $headers)
    $setupCategory = @($allChannels | Where-Object { $_.type -eq 4 -and $_.name -eq 'GET STARTED' } | Select-Object -First 1)
    if (-not $setupCategory.Count) { $setupCategory = @(Json 'Post' "$api/guilds/$guildId/channels" @{ name='GET STARTED'; type=4 }) }
    foreach ($guide in @('1552232264979718181','1552267695674757143','1552267699466272829','1552267702351954011','1552267707930509393')) { Json 'Patch' "$api/channels/$guide" @{ parent_id=[string]$setupCategory[0].id } | Out-Null }

    # Read-only Welcome: members can read the two information channels but
    # cannot post, reply in threads, create threads, or react there.
    $readOnlyDeny = '377957124160'
    foreach ($id in @('1552232264979718178','1552232264979718179','1552232264979718180')) { EveryonePermissions $id '0' $readOnlyDeny }
    # News is announcement-only, with reactions available for quick feedback.
    $newsDeny = '377957124096'
    foreach ($id in @('1552232264979718182','1552232264979718183','1552232265268854815','1552232265268854816','1552267711361458206')) { EveryonePermissions $id '64' $newsDeny }

    Channel '1552232264979718179' 0x1F30A 'hello' 'Welcome to PIXL Renderer. Install it, tune it, and make Skyrim look thicc.'
    Channel '1552232264979718180' 0x1F6E1 'rules' 'Keep it friendly, useful, and safe for everyone.'
    Channel '1552232264979718181' 0x1F680 'getting-started' 'The fast route from download to a good first session.'
    Channel '1552267695674757143' 0x2728 'reconstruction' 'Choose the PIXL reconstruction path that fits your GPU and image-quality goal.'
    Channel '1552267699466272829' 0x1F9E0 'neural-rendering' 'Optional Neural Rendering setup. PIXL Quick Setup checks the file path for you.'
    Channel '1552267702351954011' 0x26A1 'frame-generation' 'Optional Frame Generation guidance and requirements.'
    Channel '1552267707930509393' 0x1F517 'compatibility' 'What works with PIXL, what needs testing, and what cannot be stacked.'
    Channel '1552232264979718183' 0x1F4E3 'announcements' 'Official PIXL Renderer news, important notices, and fresh drops.'
    Channel '1552232265268854815' 0x1F4E6 'release-notes' 'Versions, changes, and the official download reference.'
    Channel '1552232265268854816' 0x26A0 'known-issues' 'Confirmed issues, workarounds, and what is currently being tested.'
    Channel '1552267711361458206' 0x1F527 'development-log' 'WIP experiments, previews, and dev notes from PIXL.'
    Channel '1552232265268854818' 0x1F4AC 'general' 'Talk PIXL, Skyrim, setups, and whatever is making your game look sick.'
    Channel '1552232265268854819' 0x1F4F7 'screenshots' 'Show us your world. Location, weather, and mod details are always welcome.'
    Channel '1552232265268854820' 0x1F5BC 'photo-mode' 'Finished shots, creative captures, and photo-mode work.'
    Channel '1552232265268854821' 0x1F4A1 'tips-and-tricks' 'Useful tuning finds, workflows, and things that simply look rad.'
    Channel '1552232265268854822' 0x2699 'setups-and-presets' 'Share load-order highlights, hardware, PIXL settings, and your favourite look.'
    Channel '1552232265268854823' 0x1F52E 'rendering-talk' 'Lighting, materials, water, weather, performance, and renderer nerd stuff.'
    Channel '1552232265268854824' 0x1F4AD 'ideas-and-feedback' 'Feature ideas, usability feedback, and fun stuff to explore.'
    Channel '1552232265482772492' 0x1F6DF 'install-help' 'Installation and first-run help. Include your PIXL version, runtime, and mod manager.'
    Channel '1552232265482772493' 0x1F41B 'bug-reports' 'One issue per thread: steps, setup, screenshots, logs, and expected result.'
    Channel '1552232265482772494' 0x1F9E9 'mod-conflicts' 'Help PIXL reproduce compatibility issues with other mods and shader ownership.'
    Channel '1552267717980061717' 0x1F4C8 'performance-help' 'Bring hardware details, PIXL settings, and a repeatable scene if possible.'

    Card '1552232264979718179' 'WELCOME TO PIXL RENDERER' "Skyrim, but thicc. Welcome in!\n\n**Easy start:** install PIXL, open the built-in **Quick Setup Guide**, let it check your setup, then play Skyrim. Start Balanced, have a look around, and tune from there.\n\n**New here?** Visit <#1552232264979718181>. Need a hand? Use <#1552232265482772492>. Show off your game in <#1552232265268854819>.\n\n[Official downloads + current requirements]($nexus)"
    Card '1552232264979718180' 'THE GOOD VIBES RULEBOOK' 'Keep it friendly, constructive, and useful. PIXL is a solo project, so clear reports and a little patience go a long way :)\n\nRespect people. Keep links and files safe. Use the right support channel. Credit creators. Be cool.' 14251830
    Card '1552232264979718181' 'QUICK START // LET PIXL CHECK YOUR SETUP' 'Install PIXL with MO2 or Vortex, launch through SKSE, then open the built-in **Quick Setup Guide**. It walks through the important choices and tells you when something needs attention.\n\nStart with Balanced, allow first-run shaders to compile, and do not stack PIXL with ENB, Community Shaders, Kreate, or another engine-level renderer.'
    Card '1552267699466272829' 'NEURAL RENDERING // OPTIONAL, EXPERIMENTAL, VERY COOL' 'The cards below show the whole flow. Find the RenoDX discussion, open its pinned messages, follow the pinned file information, then place the DLL in PIXL Streamline.\n\nFinish in the built-in **PIXL Quick Setup Guide**: choose Neural Rendering, press **I HAVE COPIED IT - CHECK FILE**, and let PIXL confirm the location. Fully exit Skyrim and relaunch through SKSE afterwards.\n\nUse pinned information only. PIXL does not download, install, or verify third-party files for you.'
    Card '1552232265268854815' 'RELEASES // THE OFFICIAL PIXL DROP ZONE' "Nexus is the home for PIXL downloads, requirements, and release notes. Discord is where we hang out, share shots, test fun stuff, and help each other get the good pixels.\n\n[Open the official PIXL Renderer page]($nexus)" 14251830
    Card '1552232265268854816' 'KNOWN ISSUES // WHAT WE ARE POKING AT' 'This is the living list for confirmed issues and current testing. Before opening a report, have a quick look here. If you can reproduce something new, bring your PIXL version, runtime, GPU, location/weather, settings, and useful screenshots or logs.' 14251830
    Card '1552267711361458206' 'DEV LOG // WIP, TESTING, FUN STUFF' 'PIXL drops experiments, previews, and progress notes here. Things can change a lot before release, get reworked, or occasionally become a glorious rabbit hole lol.'
    Card '1552232265268854818' 'GENERAL // COME SAY HI' 'Talk PIXL, Skyrim, mods, setups, and the random little details that make a scene hit different. Keep it kind and have fun :)'
    Card '1552232265268854819' 'SCREENSHOTS // MAKE SKYRIM LOOK SICK' 'Drop your favourite PIXL shots here. Location, weather, time of day, and the mods behind the image are always cool to know.'
    Card '1552232265482772492' 'INSTALL HELP // WE WILL GET YOU THERE' 'Start a thread with your PIXL version, Skyrim runtime, mod manager, and what happened. A log excerpt or screenshot is super helpful. If PIXL Quick Setup Guide gives you a checker message, include that too.'
    Card '1552232265482772493' 'BUG REPORTS // LETS REPRODUCE THE WEIRDNESS' 'One issue per thread please. Include PIXL version, Skyrim runtime, GPU/driver, location/weather, relevant settings, what you expected, what happened, and exact steps. Screenshots, clips, and PIXLRenderer.log make a huge difference.'
    Card '1552232265482772494' 'MOD CONFLICTS // LETS FIND THE CLASH' 'Tell us the other mod and version, your mod manager, which files win conflicts, the PIXL module involved, and the exact symptom. Data/Shaders overwrite details are especially useful.'
    Card '1552267717980061717' 'PERFORMANCE HELP // LETS FIND THE HOTSPOT' 'Please include GPU/VRAM, CPU, resolution, reconstruction mode, PIXL quality profile, location/weather, and whether shaders were compiling. PIXL Resource Monitor / GPU hotspot screenshots are gold.'

    GuideImage 'PIXL_NEURAL_STEP_1' 'STEP 1 // JOIN RENO DX' 'Open the RenoDX invite and join the server. This is where the relevant discussion is hosted.' 'discord-reno.png'
    GuideImage 'PIXL_NEURAL_STEP_2' 'STEP 2 // FIND THE DLSS5 FORUM' 'Open the dlss5-forum channel. Look for the patched DLSS-NR discussion shown here.' 'forum-post.png'
    GuideImage 'PIXL_NEURAL_STEP_3' 'STEP 3 // OPEN PINNED MESSAGES' 'Use the pin icon in the top-right of the channel. Follow the pinned ShortFuse post and its file details only.' 'pinned-messages.png'
    GuideImage 'PIXL_NEURAL_STEP_4' 'STEP 4 // PLACE THE FILE, THEN CHECK IT IN PIXL' 'Place nvngx_dlssnr.dll in Data/Shaders/ImageReconstruction/Streamline/. Then open PIXL Quick Setup Guide and use I HAVE COPIED IT - CHECK FILE. PIXL will tell you whether it sees the file in the right place.' 'save-location.png'
    Write-Output 'PIXL Discord community polish completed through PIXLBOT.'
} finally { $headers.Clear(); $token = $null }
