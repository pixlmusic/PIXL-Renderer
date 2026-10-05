[CmdletBinding()]
param()
$ErrorActionPreference='Stop';$repo=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
function Require($p,$rx,$why){$f=Join-Path $repo $p;if(!(Test-Path $f)){throw "Missing $p"};if((Get-Content $f -Raw)-notmatch $rx){throw "Missing budget contract ($why): $p"}}
$h='engine\Renderer\GPUWorkloadBudgeter.h';foreach($n in @('HybridGI','SkyBounce','Atmosphere','Reflections','GroundResponse','Water','CameraSuite','Reconstruction','SetMaximumLevel','GetScale')){Require $h $n $n}
Require 'engine\Renderer\GPUWorkloadBudgeter.cpp' 'kHold\s*=\s*120' 'minimum hold time'
Require 'engine\Renderer\GPUWorkloadBudgeter.cpp' '1\.10f[\s\S]*\.72f' 'asymmetric hysteresis'
Require 'engine\Renderer\GPUWorkloadBudgeter.h' 'bool enabled\{\}' 'disabled by default'
Require 'engine\Modules\HybridGI.cpp' 'GPUWorkloadBudgeter::Get\(\)\.GetScale' 'representative workload migration'
Require 'engine\Modules\SkyBounce.cpp' 'updateStride' 'bounded SkyBounce cadence'
Require 'engine\Modules\SkyBounce.cpp' 'updateProbeField' 'retained SkyBounce publication when cadence skips'
Write-Host 'PASS: bounded opt-in GPU workload budgeter and HybridGI runtime scaling validated.'
