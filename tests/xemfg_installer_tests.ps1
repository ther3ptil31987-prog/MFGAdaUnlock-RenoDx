param([Parameter(Mandatory=$true)][string]$PackageDirectory,
      [Parameter(Mandatory=$true)][string]$FixtureDll,
      [Parameter(Mandatory=$true)][string]$TestDirectory)
$ErrorActionPreference='Stop'
$root=$TestDirectory
if(Test-Path -LiteralPath $root) { throw 'Use a fresh test directory.' }
New-Item -ItemType Directory -Path (Join-Path $root 'plugins') -Force | Out-Null
Copy-Item -LiteralPath $FixtureDll -Destination (Join-Path $root 'dxgi.dll')
Set-Content -LiteralPath (Join-Path $root 'ReShade64.dll') -Value 'synthetic reshade profile' -Encoding ASCII
Set-Content -LiteralPath (Join-Path $root 'OptiScaler.ini') -Value "[Upscalers]`r`nDx12Upscaler=dlss`r`n[Plugins]`r`nLoadAsiPlugins=false" -Encoding ASCII
Set-Content -LiteralPath (Join-Path $root 'plugins/MFGUnlocker.asi') -Value 'preserve third party plugin' -Encoding ASCII
Set-Content -LiteralPath (Join-Path $root 'renodx-cp2077.addon64') -Value 'preserve game addon' -Encoding ASCII
$original=@{}
foreach($f in Get-ChildItem -LiteralPath $root -Recurse -File) { $original[$f.FullName]=(Get-FileHash -LiteralPath $f.FullName).Hash }
& (Join-Path $PackageDirectory 'Switch-MFGBackend.ps1') -GameDirectory $root -Backend XeMFG -PackageDirectory $PackageDirectory
$first=Get-ChildItem -LiteralPath (Join-Path $root 'MFGAdaUnlock-profiles') -Directory -Filter 'backup-*' | Sort-Object Name | Select-Object -First 1
if(Test-Path -LiteralPath (Join-Path $root 'plugins/MFGUnlocker.asi')) { throw 'Competing plugin remained active.' }
if(!(Get-Content -Raw -LiteralPath (Join-Path $root 'OptiScaler.ini')).Contains('Dx12Upscaler=dlss')) { throw 'Upscaler changed.' }
& (Join-Path $PackageDirectory 'Switch-MFGBackend.ps1') -GameDirectory $root -Backend DLSSG -PackageDirectory $PackageDirectory
if((Get-FileHash -LiteralPath (Join-Path $root 'dxgi.dll')).Hash -ne (Get-FileHash -LiteralPath (Join-Path $root 'ReShade64.dll')).Hash) { throw 'Native loader profile differs.' }
& (Join-Path $PackageDirectory 'Restore-MFGBackup.ps1') -BackupDirectory $first.FullName
foreach($path in $original.Keys) {
  if(!(Test-Path -LiteralPath $path) -or (Get-FileHash -LiteralPath $path).Hash -ne $original[$path]) { throw ('Rollback differs: '+$path) }
}
foreach($name in 'renodx-mfgunlock.addon64','MFGAdaUnlock.ini','plugins/MFGAdaUnlock.XeMFG.asi') {
  if(Test-Path -LiteralPath (Join-Path $root $name)) { throw ('Unexpected leftover: '+$name) }
}
Write-Output 'PASS: XeMFG install, DLSSG loader switch, exact initial rollback; upscaler and game addon preserved.'
