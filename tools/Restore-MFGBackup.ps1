[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$BackupDirectory)
$ErrorActionPreference='Stop'
$backup=(Resolve-Path -LiteralPath $BackupDirectory).Path
$snapshot=Get-Content -LiteralPath (Join-Path $backup 'snapshot.json') -Raw | ConvertFrom-Json
$game=(Resolve-Path -LiteralPath $snapshot.game).Path
foreach($process in Get-Process) {
  try { $exe=$process.Path } catch { continue }
  if($exe -and (Split-Path -Parent $exe) -eq $game) { throw 'Close the game before restoring.' }
}
foreach($file in $snapshot.files) {
  $target=[IO.Path]::GetFullPath((Join-Path $game $file.name))
  $stored=[IO.Path]::GetFullPath((Join-Path $backup $file.name))
  if(!$target.StartsWith($game+'\',[StringComparison]::OrdinalIgnoreCase) -or
     !$stored.StartsWith($backup+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Snapshot path outside the backup/game directory.' }
  if($file.exists -and (Get-FileHash -LiteralPath $stored).Hash -ne $file.sha256) { throw ('Backup hash differs: '+$file.name) }
}
foreach($file in $snapshot.files) {
  $target=Join-Path $game $file.name
  if($file.exists) { Copy-Item -LiteralPath (Join-Path $backup $file.name) -Destination $target -Force }
  elseif(Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target }
}
Write-Output ('Restored original installation from '+$backup)
