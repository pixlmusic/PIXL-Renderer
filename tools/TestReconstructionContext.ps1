[CmdletBinding()]
param()
$ErrorActionPreference='Stop'; $repo=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
function Require-Contract($p,$rx,$why){$f=Join-Path $repo $p;if(!(Test-Path -LiteralPath $f)){throw "Missing $p"};if((Get-Content -LiteralPath $f -Raw)-notmatch $rx){throw "Missing reconstruction contract ($why): $p"}}
$h='engine\Renderer\ReconstructionContext.h'
foreach($n in @('ReconstructionContext','ReconstructionBackend','depth','motion','reactive','transparency','exposure','temporalValid','RegisterReactiveContributor','ApplyReactiveContributors')){Require-Contract $h $n $n}
Require-Contract 'engine\Renderer\ReconstructionContext.cpp' 'contributors\.size\(\) >= 32' 'bounded contributors'
Require-Contract 'engine\Modules\ImageReconstruction.cpp' 'RegisterReactiveContributor\([\s\S]*ContainedLiquids' 'contained liquid migration'
Require-Contract 'engine\Modules\ImageReconstruction.cpp' 'RegisterReactiveContributor\([\s\S]*ReactiveFX' 'ReactiveFX migration'
Require-Contract 'engine\Modules\ImageReconstruction.cpp' 'ReconstructionContext::Get\(\)\.Publish' 'frame publication'
Write-Host 'PASS: shared reconstruction frame and bounded reactive contribution contracts validated.'
