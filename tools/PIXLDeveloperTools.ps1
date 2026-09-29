[CmdletBinding()]
param(
    [ValidateSet('Menu','Build','Package','Deploy','BuildDeploy','Git','Doctor')]
    [string]$Action='Menu',
    [ValidateSet('DevFast','Release','CleanRelease','None')]
    [string]$BuildMode='None',
    [ValidateSet('None','Core','FOMOD','Both')]
    [string]$Package='None',
    [ValidateSet('Development','Production')]
    [string]$PackageMode='Development',
    [ValidateSet('Full','Dll','Shaders','Custom')]
    [string]$DeployScope='Full',
    [string[]]$IncludePattern=@(),
    [string]$GameDirectory='',
    [ValidateRange(0,3)][int]$GameIndex=0,
    [switch]$IncludeLiveCache,
    [string]$CacheGameDirectory='',
    [string]$CommitMessage='',
    [switch]$NonInteractive,
    [switch]$AllowDirtyRelease
)

$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$localConfig=Join-Path $repo 'build\PIXLDevTools.local.json'
$script:step=0
$script:coreStage=''

function Write-Step([string]$message) {
    $script:step++
    Write-Host ("`n[{0:00}] {1}" -f $script:step,$message) -ForegroundColor Cyan
}
function Invoke-Native([string]$file,[string[]]$arguments) {
    & $file @arguments
    if ($LASTEXITCODE -ne 0) { throw "Command failed ($LASTEXITCODE): $file $($arguments -join ' ')" }
}
function Read-Choice([string]$title,[string[]]$items,[int]$default=0) {
    Write-Host "`n$title" -ForegroundColor White
    for($i=0;$i -lt $items.Count;$i++){
        $defaultLabel=if($i -eq $default){'  (default)'}else{''}
        Write-Host ("  [{0}] {1}{2}" -f ($i+1),$items[$i],$defaultLabel)
    }
    $answer=Read-Host "Choose 1-$($items.Count)"
    if([string]::IsNullOrWhiteSpace($answer)){return $default}
    $number=0
    if(-not [int]::TryParse($answer,[ref]$number) -or $number -lt 1 -or $number -gt $items.Count){throw 'Invalid menu choice.'}
    return $number-1
}
function Confirm([string]$question,[bool]$defaultNo=$true) {
    $suffix=if($defaultNo){'[y/N]'}else{'[Y/n]'}
    $answer=(Read-Host "$question $suffix").Trim().ToLowerInvariant()
    if(!$answer){return -not $defaultNo}
    return $answer -in @('y','yes')
}
function Assert-ChildPath([string]$path,[string]$root) {
    $full=[IO.Path]::GetFullPath($path);$base=[IO.Path]::GetFullPath($root).TrimEnd('\')
    if(-not $full.StartsWith($base+'\',[StringComparison]::OrdinalIgnoreCase)){throw "Unsafe path outside '$base': $full"}
    return $full
}
function Get-LocalConfig {
    if(Test-Path -LiteralPath $localConfig){
        try{return Get-Content -LiteralPath $localConfig -Raw|ConvertFrom-Json}catch{Write-Warning "Ignoring invalid local tool config: $($_.Exception.Message)"}
    }
    return [pscustomobject]@{gameDirectories=@()}
}
function Save-GameRoots([string[]]$roots) {
    New-Item -ItemType Directory -Path (Split-Path $localConfig) -Force|Out-Null
    [pscustomobject]@{gameDirectories=@($roots|Select-Object -Unique|Select-Object -First 3)} |
        ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $localConfig -Encoding UTF8
}
function Get-GameRoots {
    $candidates=[Collections.Generic.List[string]]::new()
    foreach($value in @($env:PIXL_SKYRIM_ROOT_1,$env:PIXL_SKYRIM_ROOT_2,$env:PIXL_SKYRIM_ROOT_3,(Get-LocalConfig).gameDirectories)){
        if($value){[void]$candidates.Add([string]$value)}
    }
    $parent=$repo
    while($parent){
        if(Test-Path -LiteralPath (Join-Path $parent 'SkyrimSE.exe')){[void]$candidates.Add($parent);break}
        $next=Split-Path -Parent $parent;if($next -eq $parent){break};$parent=$next
    }
    $steam=(Get-ItemProperty -Path 'HKCU:\Software\Valve\Steam' -Name SteamPath -ErrorAction SilentlyContinue).SteamPath
    if($steam){
        $libraries=@($steam)
        $vdf=Join-Path $steam 'steamapps\libraryfolders.vdf'
        if(Test-Path $vdf){
            foreach($match in [regex]::Matches((Get-Content $vdf -Raw),'"path"\s+"([^"]+)"')){$libraries+=$match.Groups[1].Value.Replace('\\','\')}
        }
        foreach($library in $libraries){[void]$candidates.Add((Join-Path $library 'steamapps\common\Skyrim Special Edition'))}
    }
    return @($candidates|ForEach-Object{try{if(Test-Path -LiteralPath (Join-Path $_ 'SkyrimSE.exe')){(Resolve-Path -LiteralPath $_).Path}}catch{}}|Select-Object -Unique|Select-Object -First 3)
}
function Select-GameRoot([string]$purpose,[bool]$allowAll=$false) {
    if($GameDirectory){
        if(-not (Test-Path -LiteralPath (Join-Path $GameDirectory 'SkyrimSE.exe'))){throw "Invalid Skyrim directory: $GameDirectory"}
        return @((Resolve-Path $GameDirectory).Path)
    }
    $roots=@(Get-GameRoots)
    if($GameIndex -gt 0){if($GameIndex -gt $roots.Count){throw "Game index $GameIndex is not configured."};return @($roots[$GameIndex-1])}
    if($NonInteractive){throw 'Specify -GameDirectory or -GameIndex for noninteractive deployment.'}
    $labels=@($roots|ForEach-Object{"Skyrim: $_"})
    if($allowAll -and $roots.Count -gt 1){$labels+='All configured installations'}
    $labels+='Configure another installation'
    $choice=Read-Choice $purpose $labels
    if($allowAll -and $roots.Count -gt 1 -and $choice -eq $roots.Count){return $roots}
    $configureIndex=$labels.Count-1
    if($choice -eq $configureIndex){
        $new=(Read-Host 'Full directory containing SkyrimSE.exe').Trim('"')
        if(-not (Test-Path -LiteralPath (Join-Path $new 'SkyrimSE.exe'))){throw 'SkyrimSE.exe was not found there.'}
        $roots=@($roots+(Resolve-Path $new).Path|Select-Object -Unique|Select-Object -First 3);Save-GameRoots $roots
        return @($roots[-1])
    }
    return @($roots[$choice])
}
function Assert-GameClosed {
    $running=@(Get-Process -Name SkyrimSE,skse64_loader -ErrorAction SilentlyContinue)
    if($running){throw 'Skyrim or the SKSE loader is running. Close it before build/deployment.'}
}
function Ensure-VsEnvironment {
    if(Get-Command cl.exe -ErrorAction SilentlyContinue){return}
    $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if(-not (Test-Path $vswhere)){throw 'Visual Studio vswhere.exe was not found.'}
    $install=(& $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath).Trim()
    if(!$install){throw 'Visual Studio 2022 C++ build tools were not found.'}
    $vcvars=Join-Path $install 'VC\Auxiliary\Build\vcvars64.bat'
    $environment=& cmd.exe /d /s /c "`"call `"$vcvars`" >nul && set`""
    if($LASTEXITCODE){throw 'Visual Studio x64 environment initialization failed.'}
    foreach($line in $environment){$equals=$line.IndexOf('=');if($equals -gt 0){Set-Item -Path "env:$($line.Substring(0,$equals))" -Value $line.Substring($equals+1)}}
}
function Invoke-Build([string]$mode) {
    if($mode -eq 'None'){return}
    Assert-GameClosed
    $preset=if($mode -eq 'DevFast'){'Dev-Fast'}else{'PIXL-12C'}
    $buildRoot=Assert-ChildPath (Join-Path $repo "build\$preset") (Join-Path $repo 'build')
    if($mode -eq 'CleanRelease' -and (Test-Path $buildRoot)){
        Write-Step "Cleaning canonical Release intermediates"
        Remove-Item -LiteralPath $buildRoot -Recurse -Force
    }
    if($preset -eq 'Dev-Fast'){Ensure-VsEnvironment}
    Write-Step "Configuring $preset"
    Invoke-Native 'cmake' @('-S',$repo,"--preset=$preset")
    Write-Step "Building $preset"
    Invoke-Native 'cmake' @('--build',"--preset=$preset")
    $dll=if($preset -eq 'Dev-Fast'){Join-Path $repo 'build\Dev-Fast\PIXLRenderer.dll'}else{Join-Path $repo 'build\PIXL-12C\Release\PIXLRenderer.dll'}
    if(-not (Test-Path $dll)){throw "Build reported success but DLL is missing: $dll"}
    Write-Host "DLL: $dll" -ForegroundColor Green
    Write-Host "SHA256: $((Get-FileHash $dll).Hash)"
}
function Select-PackageMode {
    if($Package -ne 'None'){return $Package}
    if($NonInteractive){return 'None'}
    return @('None','Core','FOMOD','Both')[(Read-Choice 'Package output' @('No package','Core ZIP','FOMOD ZIP','Core + FOMOD'))]
}
function Select-CacheRoot {
    $root=if($CacheGameDirectory){$CacheGameDirectory}else{(Select-GameRoot 'Select the live shader cache for the production Core')[0]}
    $cache=Join-Path $root 'Data\PIXL\PipelineLibrary'
    if(-not (Test-Path $cache)){throw "PipelineLibrary was not found: $cache"}
    return $cache
}
function Invoke-Package([string]$kind,[string]$mode) {
    if($kind -eq 'None'){return}
    $dll=Join-Path $repo 'build\PIXL-12C\Release\PIXLRenderer.dll'
    if(-not (Test-Path $dll)){throw 'Canonical Release DLL is missing. Build Release first.'}
    $dirty=[bool]@(& git -C $repo status --porcelain --untracked-files=normal)
    if($mode -eq 'Production' -and $dirty -and -not $AllowDirtyRelease){throw 'Production packaging requires a clean worktree. Use Development or explicitly pass -AllowDirtyRelease for a private checkpoint.'}
    $stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
    $label=if($mode -eq 'Production'){'Production'}else{'Dev'}
    $stage=Join-Path $repo "dist\PIXL-Renderer-v1.0.4-$label-Core-$stamp"
    $zip="$stage.zip";$compression=if($mode -eq 'Production'){9}else{1}
    $channel=if($mode -eq 'Production'){'RELEASE'}else{'RELEASE-CANDIDATE'}
    $arguments=@('-OutputDirectory',$stage,'-ArchivePath',$zip,'-Channel',$channel,'-CompressionLevel',$compression)
    if($mode -eq 'Production' -or $IncludeLiveCache){$arguments+=@('-PipelineLibrary',(Select-CacheRoot))}else{$arguments+='-SkipPipelineLibrary'}
    Write-Step "Staging $label Core"
    & (Join-Path $PSScriptRoot 'StagePixlRendererStandalone.ps1') @arguments
    if($LASTEXITCODE){throw 'Core staging failed.'}
    $script:coreStage=$stage
    if($kind -in @('FOMOD','Both')){
        $surface=if($env:PIXL_SURFACETIDES_SOURCE){$env:PIXL_SURFACETIDES_SOURCE}else{Join-Path $repo 'build\SurfaceTides-FOMOD-Input-20260925'}
        if(-not (Test-Path $surface)){throw 'SurfaceTides source was not found. Set PIXL_SURFACETIDES_SOURCE.'}
        $fomod=Join-Path $repo "dist\PIXL-Renderer-v1.0.4-$label-FOMOD-$stamp"
        Write-Step "Building $label FOMOD"
        & (Join-Path $PSScriptRoot 'BuildPixlFomod.ps1') -BasePackageDirectory $stage -SurfaceTidesSource $surface -OutputDirectory $fomod -ArchivePath "$fomod.zip" -CompressionLevel $compression
        if($LASTEXITCODE){throw 'FOMOD build failed.'}
    }
    Write-Host "Core: $zip" -ForegroundColor Green
}
function Find-LatestCore {
    $candidate=Get-ChildItem -LiteralPath (Join-Path $repo 'dist') -Directory -ErrorAction SilentlyContinue |
        Where-Object {Test-Path (Join-Path $_.FullName 'PIXL-RENDERER.manifest.json')}|Sort-Object LastWriteTime -Descending|Select-Object -First 1
    if(!$candidate){throw 'No staged Core package was found under dist.'};return $candidate.FullName
}
function Invoke-Deploy([string]$scope) {
    Assert-GameClosed
    $stage=if($script:coreStage){$script:coreStage}else{Find-LatestCore}
    $patterns=switch($scope){'Dll'{@('SKSE/Plugins/PIXLRenderer.dll')};'Shaders'{@('Shaders/*')};'Custom'{if(!$IncludePattern.Count){@((Read-Host 'Manifest wildcard, for example Shaders/WindowLife/*').Trim())}else{$IncludePattern}}default{@('*')}}
    $targets=@(Select-GameRoot 'Choose deployment target' $true)
    foreach($target in $targets){
        Write-Step "Deploying $scope payload to $target"
        & (Join-Path $PSScriptRoot 'DeployPixlRelease.ps1') -PackageDirectory $stage -GameDirectory $target -IncludePattern $patterns
        if($LASTEXITCODE){throw "Deployment failed: $target"}
    }
}
function Push-CurrentBranch {
    $branch=(& git -C $repo branch --show-current).Trim()
    if(!$branch){throw 'Detached HEAD cannot be pushed.'}
    Invoke-Native 'git' @('-C',$repo,'fetch','--prune')
    & git -C $repo show-ref --verify --quiet "refs/remotes/origin/$branch"
    $remoteExists=$LASTEXITCODE -eq 0
    if($remoteExists){
        & git -C $repo merge-base --is-ancestor "origin/$branch" HEAD 2>$null
        if($LASTEXITCODE -ne 0){throw "origin/$branch is not an ancestor of HEAD. Pull/rebase and review before pushing."}
    }
    if(-not $NonInteractive -and -not (Confirm "Push current branch '$branch' to origin/$branch?")){return}
    Invoke-Native 'git' @('-C',$repo,'push','origin',"HEAD:$branch")
}
function Invoke-GitTool {
    Write-Step 'Inspecting Git state'
    Invoke-Native 'git' @('-C',$repo,'status','--short','--branch')
    if($NonInteractive){
        if(!$CommitMessage){throw 'Noninteractive Git action requires -CommitMessage.'}
        $choice=0
    }else{$choice=Read-Choice 'Git action' @('Commit tracked modifications','Select paths to stage','Push current branch','Return')}
    if($choice -eq 3){return}
    if($choice -eq 2){
        Push-CurrentBranch;return
    }
    if($choice -eq 0){Invoke-Native 'git' @('-C',$repo,'add','-u')}
    else{
        $paths=Read-Host 'Space-separated repository paths to stage'
        if(!$paths){return};Invoke-Native 'git' (@('-C',$repo,'add','--')+($paths -split '\s+'))
    }
    Invoke-Native 'git' @('-C',$repo,'diff','--cached','--check')
    $staged=@(& git -C $repo diff --cached --name-only)
    if(!$staged){throw 'Nothing is staged.'}
    $blocked=$staged|Where-Object{$_ -match '(^|/)(build|dist|\.vs)/|\.(obj|pdb|ilk|zip|log|tmp)$|(^|/)(id_rsa|id_ed25519|.*\.pem)$'}
    if($blocked){throw "Generated or sensitive paths are staged:`n$($blocked -join "`n")"}
    $message=if($CommitMessage){$CommitMessage}else{Read-Host 'Commit message'};if(!$message){throw 'Commit message is required.'}
    Invoke-Native 'git' @('-C',$repo,'commit','-m',$message)
    if($NonInteractive){return}
    if(Confirm 'Push this branch now?'){Push-CurrentBranch}
}
function Invoke-Doctor {
    Write-Step 'Developer environment'
    foreach($tool in @('git','cmake','powershell')){if(Get-Command $tool -ErrorAction SilentlyContinue){Write-Host "PASS $tool" -ForegroundColor Green}else{Write-Host "FAIL $tool" -ForegroundColor Red}}
    $roots=@(Get-GameRoots);for($i=0;$i -lt $roots.Count;$i++){Write-Host "GAME $($i+1): $($roots[$i])"}
    $dll=Join-Path $repo 'build\PIXL-12C\Release\PIXLRenderer.dll';if(Test-Path $dll){Write-Host "RELEASE DLL: $dll"}else{Write-Warning 'Release DLL is not built.'}
    Write-Host "Local configuration: $localConfig"
}
function Invoke-Menu {
    $choice=Read-Choice 'PIXL Renderer developer tools' @('Build','Package','Deploy','Build + package + deploy','Git commit / push','Environment doctor','Exit')
    switch($choice){0{$script:requested='Build'}1{$script:requested='Package'}2{$script:requested='Deploy'}3{$script:requested='BuildDeploy'}4{$script:requested='Git'}5{$script:requested='Doctor'}default{$script:requested='Exit'}}
}

Set-Location $repo
Write-Host 'PIXL RENDERER 1.0.4 | DEVELOPMENT CONSOLE' -ForegroundColor Cyan
Write-Host "Repository: $repo"
try {
    if($Action -eq 'Menu'){Invoke-Menu;$Action=$script:requested;if($Action -eq 'Exit'){exit 0}}
    if($Action -eq 'Doctor'){Invoke-Doctor;exit 0}
    if($Action -eq 'Git'){Invoke-GitTool;exit 0}
    if($Action -in @('Build','BuildDeploy')){
        if($BuildMode -eq 'None' -and -not $NonInteractive){$BuildMode=@('DevFast','Release','CleanRelease')[(Read-Choice 'Build mode' @('Fast incremental DLL','Audited Release','Clean audited Release') 1)]}
        if($BuildMode -eq 'None'){$BuildMode='Release'}
        Invoke-Build $BuildMode
    }
    if($Action -in @('Package','BuildDeploy')){
        $kind=Select-PackageMode;if($kind -eq 'None' -and $Action -eq 'BuildDeploy'){$kind='Core'}
        if(-not $NonInteractive){$PackageMode=@('Development','Production')[(Read-Choice 'Package profile' @('Development: fast ZIP, compile shaders on device','Production: maximum ZIP, include selected live cache'))]}
        Invoke-Package $kind $PackageMode
    }
    if($Action -in @('Deploy','BuildDeploy')){
        if(-not $NonInteractive){$DeployScope=@('Full','Dll','Shaders','Custom')[(Read-Choice 'Deployment scope' @('Full verified package','DLL only','Shaders only','Custom manifest wildcard'))]}
        Invoke-Deploy $DeployScope
    }
    Write-Host "`nPASS: requested PIXL operation completed." -ForegroundColor Green
} catch {
    Write-Host "`nFAILED: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host "At: $($_.InvocationInfo.PositionMessage)" -ForegroundColor DarkGray
    exit 1
}
