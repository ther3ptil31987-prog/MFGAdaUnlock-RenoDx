[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$GameDirectory,
  [string]$PackageDirectory=$PSScriptRoot,
  [ValidateSet('DLSSG','XeMFG')][string]$Backend='XeMFG'
)
$ErrorActionPreference='Stop'
$game=(Resolve-Path -LiteralPath $GameDirectory).Path
$package=(Resolve-Path -LiteralPath $PackageDirectory).Path
foreach($process in Get-Process) {
  try { $exe=$process.Path } catch { continue }
  if($exe -and (Split-Path -Parent $exe) -eq $game) { throw 'Close the game before installing.' }
}
$manifest=Get-Content -Raw -LiteralPath (Join-Path $package 'manifest.json') | ConvertFrom-Json
if($manifest.identity -ne 'Native ReShade XeMFG R1') { throw 'Not a native ReShade package.' }
foreach($asset in $manifest.assets) {
  $path=[IO.Path]::GetFullPath((Join-Path $package $asset.name))
  if(!$path.StartsWith($package+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Asset outside package.' }
  if((Get-FileHash -LiteralPath $path).Hash -ne $asset.sha256) { throw ('Package hash mismatch: '+$asset.name) }
}
$dxgi=Join-Path $game 'dxgi.dll'
$reshade=Join-Path $game 'ReShade64.dll'
if(!(Test-Path -LiteralPath $dxgi)) { throw 'Install ReShade with addon support before using this installer.' }
$product=(Get-Item -LiteralPath $dxgi).VersionInfo.ProductName
if($product -eq 'OptiScaler') {
  if(!(Test-Path -LiteralPath $reshade) -or (Get-Item -LiteralPath $reshade).VersionInfo.ProductName -ne 'ReShade') {
    throw 'A verified ReShade64.dll is required to replace the OptiScaler loader.'
  }
} elseif($product -ne 'ReShade') { throw 'Unknown DXGI loader. No files were changed.' }
$expected=@{'libxess_fg.dll'='ec5e0c65e075570c6ede72618bb666d0be0c2e10b2ea9762c0fe8cb8e375ab27'}
foreach($name in $expected.Keys) {
  if((Get-FileHash -LiteralPath (Join-Path $game $name)).Hash -ne $expected[$name]) { throw ('Unsupported Intel runtime: '+$name) }
}
$ll=(Get-FileHash -LiteralPath (Join-Path $game 'libxell.dll')).Hash.ToLowerInvariant()
if($ll -notin @('61aeeab7fdcfb6d1742f86c14c68c663d759607d01a6132645c857291eaebb86','d2030dcd694fda8f2ec7e044b13e6db8f0b56d4ba9113a5efad334e3f3ded8c7')) { throw 'Unsupported XeLL runtime.' }
$names=@('dxgi.dll','MFGAdaUnlock.ini','ReShade.ini','renodx-mfgunlock.addon64',
  'plugins/MFGAdaUnlock.XeMFG.asi','plugins/MFGUnlocker.asi')
foreach($file in Get-ChildItem -LiteralPath $game -Filter '*mfgunlock*.addon64') { $names+=$file.Name }
$names=@($names | Select-Object -Unique)
$profile=Join-Path $game 'MFGAdaUnlock-profiles'
$backup=Join-Path $profile ('native-backup-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $backup -Force | Out-Null
$snapshot=@()
foreach($name in $names) {
  $target=Join-Path $game $name; $exists=Test-Path -LiteralPath $target
  $snapshot+=@{name=$name;exists=$exists;sha256=$(if($exists){(Get-FileHash -LiteralPath $target).Hash}else{$null})}
  if($exists) {
    $copy=Join-Path $backup $name
    New-Item -ItemType Directory -Path (Split-Path -Parent $copy) -Force | Out-Null
    Copy-Item -LiteralPath $target -Destination $copy
  }
}
@{game=$game;files=$snapshot;backend=$Backend;package_sha256=(Get-FileHash -LiteralPath (Join-Path $package 'manifest.json')).Hash} |
  ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $backup 'snapshot.json') -Encoding UTF8
if(-not ('NativeMfgIni' -as [type])) {
  Add-Type -TypeDefinition @'
using System.Runtime.InteropServices;
using System.Text;
public static class NativeMfgIni {
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] public static extern bool WritePrivateProfileString(string s,string k,string v,string p);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] public static extern uint GetPrivateProfileString(string s,string k,string v,StringBuilder b,uint n,string p);
}
'@
}
function Set-Ini([string]$path,[string]$section,[string]$key,[string]$value) {
  if(![NativeMfgIni]::WritePrivateProfileString($section,$key,$value,$path)) { throw 'INI update failed.' }
}
try {
  foreach($name in $names) {
    if($name -like '*mfgunlock*.addon64' -or $name -like 'plugins/*.asi') {
      $target=Join-Path $game $name
      if(Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target }
    }
  }
  if($product -eq 'OptiScaler') { Copy-Item -LiteralPath $reshade -Destination $dxgi -Force }
  Copy-Item -LiteralPath (Join-Path $package 'renodx-mfgunlock.addon64') -Destination (Join-Path $game 'renodx-mfgunlock.addon64') -Force
  Set-Ini (Join-Path $game 'MFGAdaUnlock.ini') 'Backend' 'Selected' $Backend
  Set-Ini (Join-Path $game 'MFGAdaUnlock.ini') 'XeFG' 'Multiplier' '4'
  $config=Join-Path $game 'ReShade.ini'
  $values=New-Object Text.StringBuilder 32768
  [void][NativeMfgIni]::GetPrivateProfileString('ADDON','LoadFromDllMain','',$values,32768,$config)
  $entries=@($values.ToString().Split(',') | Where-Object {$_ -and $_ -notlike '*mfgunlock*.addon64'})
  $entries+='renodx-mfgunlock.addon64'
  Set-Ini $config 'ADDON' 'LoadFromDllMain' ($entries -join ',')
  if((Get-FileHash -LiteralPath (Join-Path $game 'renodx-mfgunlock.addon64')).Hash -ne
     (Get-FileHash -LiteralPath (Join-Path $package 'renodx-mfgunlock.addon64')).Hash) { throw 'Installed addon hash differs.' }
  Write-Output ('Native ReShade '+$Backend+' installed. Backup: '+$backup)
} catch {
  foreach($f in $snapshot) {
    $target=Join-Path $game $f.name
    if($f.exists) { Copy-Item -LiteralPath (Join-Path $backup $f.name) -Destination $target -Force }
    elseif(Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target }
  }
  throw
}
