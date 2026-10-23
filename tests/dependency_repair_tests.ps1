# SPDX-License-Identifier: MIT
param([Parameter(Mandatory=$true)][string]$TestRoot)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../tools/Repair-XeDependencies.ps1')
function Check($value,[string]$message) { if(!$value) { throw $message } }
function Reject([scriptblock]$action,[string]$message) { $rejected=$false; try { & $action } catch { $rejected=$true }; Check $rejected $message }
$root=[IO.Path]::GetFullPath((Join-Path $TestRoot ([guid]::NewGuid().ToString('N'))))
New-Item -ItemType Directory -Path $root | Out-Null
$game=Join-Path $root 'game';$cache=Join-Path $root 'cache'
New-Item -ItemType Directory -Path $game,$cache | Out-Null
$target=Join-Path $game 'libxell.dll';$stage=Join-Path $cache 'new.dll'
[IO.File]::WriteAllText($target,'original');[IO.File]::WriteAllText($stage,'new verified fixture')
$before=Get-XeDigest $target;$after=Get-XeDigest $stage
$plan=@(@{target=$target;before=$before;after=$after;stage=$stage})
$closed={}
$journal=Invoke-XeTransaction $game $plan $closed
Check ((Get-XeDigest $target) -eq $after) 'installed fixture differs'
Restore-XeTransaction $journal $closed
Check ((Get-XeDigest $target) -eq $before) 'rollback differs'
Reject { Invoke-XeTransaction $game $plan {throw 'game running'} } 'running game accepted'
[IO.File]::WriteAllText($target,'foreign edit')
Reject { Invoke-XeTransaction $game $plan $closed } 'changed target replaced'
[IO.File]::WriteAllText($target,'original')
[IO.File]::WriteAllText($stage,'bad download')
Reject { Invoke-XeTransaction $game $plan $closed } 'wrong download hash installed'
[IO.File]::WriteAllText($stage,'new verified fixture')
Reject { Assert-XePath $game (Join-Path $root 'escape.dll') } 'outside target accepted'
$newTarget=Join-Path $game 'libxess_fg.dll'
$newPlan=@(@{target=$newTarget;before=$null;after=$after;stage=$stage})
$newJournal=Invoke-XeTransaction $game $newPlan $closed
Restore-XeTransaction $newJournal $closed
Check (!(Test-Path -LiteralPath $newTarget)) 'new installation not removed on rollback'
# Failure after one successful replacement must roll back that first file.
$second=Join-Path $game 'locked.dll'; [IO.File]::WriteAllText($second,'original')
$both=@($plan[0],@{target=$second;before=$before;after=$after;stage=$stage})
$handle=[IO.File]::Open($second,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
try { Reject { Invoke-XeTransaction $game $both $closed } 'locked target installed' } finally { $handle.Dispose() }
Check ((Get-XeDigest $target) -eq $before -and (Get-XeDigest $second) -eq $before) 'partial failure did not roll back'
# A prepared journal can be resumed after a crash before state advancement.
$journal=Invoke-XeTransaction $game $plan $closed
$j=Get-Content -LiteralPath $journal -Raw | ConvertFrom-Json;$j.state='prepared';Write-XeJournal $j $journal
Restore-XeTransaction $journal $closed
Check ((Get-XeDigest $target) -eq $before) 'interrupted transaction did not recover'
$journal=Invoke-XeTransaction $game $plan $closed
[IO.File]::WriteAllText($target,'foreign edit after install')
Reject { Restore-XeTransaction $journal $closed } 'rollback overwrote foreign change'
# Store/Game Pass: the process reports a WindowsApps junction to the real install.
$real=Join-Path $root 'XboxGames\Game\Content'; New-Item -ItemType Directory -Path $real | Out-Null
$link=Join-Path $root 'WindowsApps-package'; New-Item -ItemType Junction -Path $link -Target $real | Out-Null
Check ((Resolve-XeFinalPath $link) -eq $real) 'junction was not resolved to the real install'
Reject { Assert-XePath $link (Join-Path $link 'libxell.dll') } 'write through a junction accepted'
[void](Assert-XePath (Resolve-XeFinalPath $link) (Join-Path $real 'libxell.dll'))
Check ((Find-XeInstallRoot $real) -eq $real) 'Xbox Content root not detected'
Check ((Find-XeInstallRoot (Join-Path $real 'Binaries\Win64')) -eq $real) 'nested Xbox executable root not detected'
$steam=Join-Path $root 'steamapps\common\Game'
Check ((Find-XeInstallRoot (Join-Path $steam 'bin\x64')) -eq $steam) 'Steam root not detected'
Check ((Find-XeInstallRoot $steam) -eq $steam) 'Steam root executable folder changed'
$unreal=Join-Path $root 'Store\Game'
Check ((Find-XeInstallRoot (Join-Path $unreal 'Project\Binaries\Win64')) -eq $unreal) 'Unreal root not detected'
$other=Join-Path $root 'Other\Game'
Check ((Find-XeInstallRoot $other) -eq $other) 'unknown layout widened the search scope'
# Engine plugin copies are found; junctions and helper backups are not followed.
$exe=Join-Path $steam 'bin\x64'; $plugin=Join-Path $steam 'Engine\Plugins\XeSS\Binaries\Win64'
$backup=Join-Path $steam '.mfg-dependency-backups\old'; $outside=Join-Path $root 'outside'
New-Item -ItemType Directory -Path $exe,$plugin,$backup,$outside | Out-Null
foreach($p in @((Join-Path $plugin 'libxess_fg.dll'),(Join-Path $backup 'libxess_fg.dll'),(Join-Path $outside 'libxell.dll'))) { [IO.File]::WriteAllText($p,'copy') }
New-Item -ItemType Junction -Path (Join-Path $steam 'linked') -Target $outside | Out-Null
$scan=Find-XeRuntimeCopies $steam @('libxess_fg.dll','libxell.dll')
Check ($scan.complete -and @($scan.files).Count -eq 1 -and $scan.files[0] -eq (Join-Path $plugin 'libxess_fg.dll')) 'shipped copy search followed links/backups or missed a plugin copy'
Check (!(Find-XeRuntimeCopies $steam @('libxess_fg.dll') -Limit 2).complete) 'search limit not reported'
$targets=Get-XeRuntimeTargets $exe $steam 'libxess_fg.dll' @((Join-Path $exe 'LIBXESS_FG.dll')) $scan.files
Check (@($targets).Count -eq 2 -and $targets[0].role -eq 'game folder' -and $targets[1].role -eq 'shipped with the game') 'runtime targets not deduplicated/classified'
Reject { Get-XeRuntimeTargets $exe $steam 'libxess_fg.dll' @((Join-Path $outside 'libxess_fg.dll')) @() } 'loaded copy outside the installation planned'
Check (Test-XeWritable $exe) 'writable folder reported read-only'
$catalog=Join-Path $PSScriptRoot '../src/addons/mfgunlock/xemfg/dependency_catalog.json'
[void](Read-XeCatalog $catalog)
$bad=Get-Content -LiteralPath $catalog -Raw | ConvertFrom-Json;$bad.files[0].sha256='0000'
$badPath=Join-Path $cache 'bad.json';$bad | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $badPath -Encoding UTF8
Reject { Read-XeCatalog $badPath } 'tampered catalog accepted'
Write-Output 'PASS: install/rollback, new files, closed-game gate, unknown edits, hash failure, locked target, interrupted recovery, junction resolution, install roots, shipped copies, pinned catalog'
