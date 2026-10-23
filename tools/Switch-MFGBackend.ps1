[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$GameDirectory,
  [Parameter(Mandatory=$true)][ValidateSet('DLSSG','XeMFG')][string]$Backend,
  [string]$PackageDirectory = $PSScriptRoot
)
$ErrorActionPreference='Stop'
$game=(Resolve-Path -LiteralPath $GameDirectory).Path
$package=(Resolve-Path -LiteralPath $PackageDirectory).Path
foreach($process in Get-Process) {
  try { $exe=$process.Path } catch { continue }
  if($exe -and (Split-Path -Parent $exe) -eq $game) { throw 'Close the game before switching backends.' }
}
$manifest=Get-Content -Raw -LiteralPath (Join-Path $package 'manifest.json') | ConvertFrom-Json
foreach($asset in $manifest.assets) {
  $path=Join-Path $package $asset.name
  if((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $asset.sha256) { throw ('Package hash mismatch: '+$asset.name) }
}
$profile=Join-Path $game 'MFGAdaUnlock-profiles'
New-Item -ItemType Directory -Path $profile -Force | Out-Null
$dxgi=Join-Path $game 'dxgi.dll'
$reshade=Join-Path $game 'ReShade64.dll'
$opti=Join-Path $game 'OptiScaler.ini'
if(!(Test-Path -LiteralPath (Join-Path $profile 'OptiScaler.dxgi.dll'))) {
  if((Get-Item -LiteralPath $dxgi).VersionInfo.ProductName -ne 'OptiScaler' -or
      !(Test-Path -LiteralPath $reshade) -or !(Test-Path -LiteralPath $opti)) {
    throw 'Initial setup requires the working OptiScaler 10 + ReShade64.dll installation.'
  }
  Copy-Item -LiteralPath $dxgi -Destination (Join-Path $profile 'OptiScaler.dxgi.dll')
  Copy-Item -LiteralPath $reshade -Destination (Join-Path $profile 'ReShade.dxgi.dll')
}
if(!(Test-Path -LiteralPath (Join-Path $game 'plugins'))) {
  New-Item -ItemType Directory -Path (Join-Path $game 'plugins') | Out-Null
}
$names=@('dxgi.dll','OptiScaler.ini','MFGAdaUnlock.ini','renodx-mfgunlock.addon64',
  'plugins/MFGAdaUnlock.XeMFG.asi','plugins/MFGUnlocker.asi')
foreach($f in Get-ChildItem -LiteralPath $game -Filter '*mfgunlock*.addon64') { $names+=$f.Name }
$names=@($names | Select-Object -Unique)
$backup=Join-Path $profile ('backup-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $backup | Out-Null
$snapshot=@()
foreach($name in $names) {
  $target=Join-Path $game $name
  $exists=Test-Path -LiteralPath $target
  $snapshot+=@{name=$name;exists=$exists;sha256=$(if($exists){(Get-FileHash -LiteralPath $target).Hash}else{$null})}
  if($exists) {
    $copy=Join-Path $backup $name
    New-Item -ItemType Directory -Path (Split-Path -Parent $copy) -Force | Out-Null
    Copy-Item -LiteralPath $target -Destination $copy
  }
}
@{game=$game;files=$snapshot;backend=$Backend} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $backup 'snapshot.json') -Encoding UTF8
if(-not ('MfgIniWriter' -as [type])) {
  Add-Type -TypeDefinition @'
using System.Runtime.InteropServices;
public static class MfgIniWriter {
  [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]
  public static extern bool WritePrivateProfileString(string section,string key,string value,string path);
}
'@
}
function Set-Ini([string]$path,[string]$section,[string]$key,[string]$value) {
  if(![MfgIniWriter]::WritePrivateProfileString($section,$key,$value,$path)) { throw 'INI update failed.' }
}
try {
  # Back up and remove only the competing MFG plugin/addons, leaving the game's
  # RenoDX addon, presets and vendor runtime files in place.
  foreach($name in $names) {
    if($name -like '*mfgunlock*.addon64' -or $name -eq 'plugins/MFGUnlocker.asi') {
      $target=Join-Path $game $name
      if(Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target }
    }
  }
  Copy-Item -LiteralPath (Join-Path $package 'renodx-mfgunlock.addon64') -Destination (Join-Path $game 'renodx-mfgunlock.addon64') -Force
  Copy-Item -LiteralPath (Join-Path $package 'MFGAdaUnlock.XeMFG.asi') -Destination (Join-Path $game 'plugins/MFGAdaUnlock.XeMFG.asi') -Force
  Set-Ini (Join-Path $game 'MFGAdaUnlock.ini') 'Backend' 'Selected' $Backend
  if($Backend -eq 'XeMFG') {
    Copy-Item -LiteralPath (Join-Path $profile 'OptiScaler.dxgi.dll') -Destination $dxgi -Force
    Set-Ini $opti 'Plugins' 'LoadAsiPlugins' 'true'
    Set-Ini $opti 'Plugins' 'LoadReshade' 'true'
    Set-Ini $opti 'FrameGen' 'Enabled' 'true'
    Set-Ini $opti 'FrameGen' 'FGInput' 'DLSSG'
    Set-Ini $opti 'FrameGen' 'FGOutput' 'XeFG'
  } else {
    # Use the original native DLSS-G integration, without OptiScaler replacing
    # it. Keep OptiScaler's files and settings ready for the next XeMFG switch.
    Copy-Item -LiteralPath (Join-Path $profile 'ReShade.dxgi.dll') -Destination $dxgi -Force
  }
  foreach($name in 'renodx-mfgunlock.addon64','MFGAdaUnlock.XeMFG.asi') {
    $target=if($name -like '*.asi'){Join-Path $game ('plugins/'+$name)}else{Join-Path $game $name}
    if((Get-FileHash -LiteralPath $target).Hash -ne (Get-FileHash -LiteralPath (Join-Path $package $name)).Hash) { throw 'Installed hash differs.' }
  }
  Write-Output ('Backend '+$Backend+' installed. Backup: '+$backup)
} catch {
  foreach($f in $snapshot) {
    $target=Join-Path $game $f.name
    if($f.exists) { Copy-Item -LiteralPath (Join-Path $backup $f.name) -Destination $target -Force }
    elseif(Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target }
  }
  throw
}
