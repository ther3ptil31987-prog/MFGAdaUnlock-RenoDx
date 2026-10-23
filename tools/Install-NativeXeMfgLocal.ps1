# Installs the reviewed local native addon and Intel pair with a reversible snapshot.
[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$GameDirectory,
  [Parameter(Mandatory=$true)][string]$ProcessName,
  [Parameter(Mandatory=$true)][string]$PackageDirectory,
  [Parameter(Mandatory=$true)][string]$FrameGenerationRuntime,
  [Parameter(Mandatory=$true)][string]$LatencyRuntime,
  [Parameter(Mandatory=$true)][string]$BackupRoot
)
$ErrorActionPreference='Stop'
$game=(Resolve-Path -LiteralPath $GameDirectory).Path
$package=(Resolve-Path -LiteralPath $PackageDirectory).Path
$backupBase=(Resolve-Path -LiteralPath $BackupRoot).Path
function Assert-Closed {
  if(Get-Process -Name $ProcessName -ErrorAction SilentlyContinue) { throw 'Close the game before installation.' }
}
Assert-Closed
$manifest=Get-Content -LiteralPath (Join-Path $package 'manifest.json') -Raw | ConvertFrom-Json
$addon=Join-Path $package 'renodx-mfgunlock.addon64'
if((Get-FileHash -LiteralPath $addon).Hash -ne $manifest.addon_sha256) { throw 'Addon differs from package manifest.' }
$fg=(Resolve-Path -LiteralPath $FrameGenerationRuntime).Path
$ll=(Resolve-Path -LiteralPath $LatencyRuntime).Path
if((Get-FileHash -LiteralPath $fg).Hash -ne 'ec5e0c65e075570c6ede72618bb666d0be0c2e10b2ea9762c0fe8cb8e375ab27') { throw 'Unreviewed XeFG runtime.' }
if((Get-FileHash -LiteralPath $ll).Hash -notin @('61aeeab7fdcfb6d1742f86c14c68c663d759607d01a6132645c857291eaebb86','d2030dcd694fda8f2ec7e044b13e6db8f0b56d4ba9113a5efad334e3f3ded8c7')) { throw 'Unreviewed XeLL runtime.' }
$copies=[ordered]@{
  'renodx-mfgunlock.addon64'=$addon
  'renodx-mfgunlock.addon64.manifest.json'=(Join-Path $package 'manifest.json')
  'libxess_fg.dll'=$fg
  'libxell.dll'=$ll
}
foreach($name in @('renodx-mfgunlock.addon64','libxess_fg.dll','libxell.dll')) {
  if(Test-Path -LiteralPath (Join-Path $game ('_storage_\'+$name)) -PathType Leaf) {
    $copies['_storage_\'+$name]=$copies[$name]
    if($name -eq 'renodx-mfgunlock.addon64') { $copies['_storage_\'+$name+'.manifest.json']=Join-Path $package 'manifest.json' }
  }
}
$backup=[IO.Path]::GetFullPath((Join-Path $backupBase ($ProcessName+'-'+(Get-Date -Format 'yyyyMMdd-HHmmss-ffff'))))
if(!$backup.StartsWith($backupBase+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Backup path escaped its root.' }
New-Item -ItemType Directory -Path $backup | Out-Null
$files=@()
foreach($name in $copies.Keys) {
  $target=[IO.Path]::GetFullPath((Join-Path $game $name))
  if(!$target.StartsWith($game+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Target path escaped game directory.' }
  $exists=Test-Path -LiteralPath $target -PathType Leaf
  $digest=$null
  if($exists) {
    $stored=Join-Path $backup $name
    New-Item -ItemType Directory -Path (Split-Path -Parent $stored) -Force | Out-Null
    Copy-Item -LiteralPath $target -Destination $stored
    $digest=(Get-FileHash -LiteralPath $target).Hash
    if((Get-FileHash -LiteralPath $stored).Hash -ne $digest) { throw 'Backup verification failed.' }
  }
  $files+= [ordered]@{name=$name;exists=$exists;sha256=$digest}
}
$protected=@{}
foreach($name in @('MFGAdaUnlock.ini','ReShade.ini','ReShadePreset.ini','dxgi.dll','dinput8.dll')) {
  $path=Join-Path $game $name
  if(Test-Path -LiteralPath $path -PathType Leaf) {
    $protected[$name]=(Get-FileHash -LiteralPath $path).Hash
    if($name.EndsWith('.ini')) { Copy-Item -LiteralPath $path -Destination (Join-Path $backup $name) }
  }
}
foreach($log in @('MFGAdaUnlock-XeMFG.log','ReShade.log')) {
  $path=Join-Path $game $log
  if(Test-Path -LiteralPath $path -PathType Leaf) { Copy-Item -LiteralPath $path -Destination (Join-Path $backup $log) }
}
$snapshot=[ordered]@{game=$game;files=$files;preserved_sha256=$protected}
$snapshot | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $backup 'snapshot.json') -Encoding UTF8
Assert-Closed
try {
  foreach($name in $copies.Keys) {
    $target=Join-Path $game $name
    Copy-Item -LiteralPath $copies[$name] -Destination $target -Force
    if((Get-FileHash -LiteralPath $target).Hash -ne (Get-FileHash -LiteralPath $copies[$name]).Hash) { throw ('Installed hash differs: '+$name) }
  }
  foreach($name in $protected.Keys) {
    if((Get-FileHash -LiteralPath (Join-Path $game $name)).Hash -ne $protected[$name]) { throw ('Protected file changed: '+$name) }
  }
} catch {
  & (Join-Path $PSScriptRoot 'Restore-MFGBackup.ps1') -BackupDirectory $backup
  throw
}
$receipt=[ordered]@{game=$game;backup=$backup;addon_sha256=$manifest.addon_sha256;source_commit=$manifest.source_commit;files=@($copies.Keys);preserved_sha256=$protected}
$receipt | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $backup 'installation.json') -Encoding UTF8
$receipt | ConvertTo-Json -Depth 6
