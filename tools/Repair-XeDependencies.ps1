# SPDX-License-Identifier: MIT
# Standalone assisted repair. Dot-sourcing exposes transaction functions for
# tests; the interactive entry point never offers an unattended confirmation.
[CmdletBinding()]
param([string]$GameDirectory,[int]$GameProcessId,[long]$GameCreationTicks,
      [string]$GameExecutable,[string]$CatalogPath,[string]$RollbackJournal)
$ErrorActionPreference='Stop'
# The log goes beside the dependency backups in the game folder. Lines written
# before the installation root is known are buffered; a folder that needs
# administrator rights falls back to the temporary folder.
$script:XeLog=$null; $script:XeLogBuffer=New-Object Collections.Generic.List[string]
function Write-XeLog([string]$Message) {
  $line=(Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff')+'  '+$Message
  if(!$script:XeLog) { $script:XeLogBuffer.Add($line); return }
  try { [IO.File]::AppendAllText($script:XeLog,$line+"`r`n") } catch {}
}
function Open-XeLog([string]$Directory) {
  $name='helper-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+$PID+'.log'
  $path=$null
  if($Directory) {
    try {
      $folder=Join-Path $Directory '.mfg-dependency-backups'
      if(!(Test-Path -LiteralPath $folder)) { New-Item -ItemType Directory -Path $folder | Out-Null }
      $path=Join-Path $folder $name; [IO.File]::AppendAllText($path,'')
    } catch { $path=$null }
  }
  if(!$path) { $path=Join-Path ([IO.Path]::GetTempPath()) ('MFG-XeHelper-'+$name); [IO.File]::AppendAllText($path,'') }
  $script:XeLog=$path
  foreach($line in $script:XeLogBuffer) { [IO.File]::AppendAllText($path,$line+"`r`n") }
  $script:XeLogBuffer.Clear()
  return $path
}
function Test-XeStagingFolder([string]$Path,[string]$TempRoot=[IO.Path]::GetTempPath()) {
  # Only this helper's own temporary folders may be removed.
  if(!$Path -or !(Test-Path -LiteralPath $Path -PathType Container)) { return $false }
  $temp=[IO.Path]::GetFullPath($TempRoot).TrimEnd('\')
  $full=[IO.Path]::GetFullPath($Path).TrimEnd('\')
  if([IO.Path]::GetDirectoryName($full) -ne $temp) { return $false }
  if((Split-Path -Leaf $full) -notmatch '^MFG-Xe(Helper|Repair)-') { return $false }
  return !((Get-Item -LiteralPath $full -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)
}
function Remove-XeStaging([string]$Path,[string]$TempRoot=[IO.Path]::GetTempPath()) {
  if(Test-XeStagingFolder $Path $TempRoot) { try { Remove-Item -LiteralPath $Path -Recurse -Force } catch { Write-XeLog ('Could not remove temporary folder '+$Path+': '+$_.Exception.Message) } }
}
function Clear-XeStaleStaging([string]$Keep,[string]$TempRoot=[IO.Path]::GetTempPath(),[TimeSpan]$Age=[TimeSpan]::FromDays(1)) {
  # Folders left by earlier helpers (before 1.5.5 nothing removed them).
  foreach($dir in @(Get-ChildItem -LiteralPath $TempRoot -Directory -Filter 'MFG-Xe*' -ErrorAction SilentlyContinue)) {
    if($Keep -and $dir.FullName.TrimEnd('\') -eq [IO.Path]::GetFullPath($Keep).TrimEnd('\')) { continue }
    if((Get-Date)-$dir.LastWriteTime -gt $Age) { Remove-XeStaging $dir.FullName $TempRoot }
  }
}
function Get-XeDigest([string]$Path) {
  if(!(Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
  return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}
function Read-XeCatalog([string]$Path) {
  $c=Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json
  $base='https://raw.githubusercontent.com/intel/xess/de0fb9c1c510661c571164e1418ceca8101dab69/'
  $pins=@{
    'libxess_fg.dll'=@('bin/libxess_fg.dll','ec5e0c65e075570c6ede72618bb666d0be0c2e10b2ea9762c0fe8cb8e375ab27')
    'libxell.dll'=@('bin/libxell.dll','d2030dcd694fda8f2ec7e044b13e6db8f0b56d4ba9113a5efad334e3f3ded8c7')
    'Intel-XeSS-LICENSE.txt'=@('LICENSE.txt','784be42c39a4a4d03cf85d88e15e59d75828fca773ce45b8dbfffbd7eaf208df')
    'Intel-XeSS-third-party-programs.txt'=@('third-party-programs.txt','e8d8899dbfc4de0e8ca25560be9358e03c1405d7c956d20a1838c3c709b9b51c')
  }
  if($c.schema -ne 1 -or $c.base_url -cne $base -or @($c.files).Count -ne 4) { throw 'Untrusted dependency catalog.' }
  $seen=@{}
  foreach($entry in $c.files) {
    if(!$pins.ContainsKey($entry.name) -or $seen.ContainsKey($entry.name)) { throw 'Unknown or duplicated catalog entry.' }
    $seen[$entry.name]=$true; $pin=$pins[$entry.name]
    if($entry.source -cne $pin[0] -or $entry.sha256 -cne $pin[1]) { throw 'Catalog source/hash differs from the trusted helper.' }
    if([bool]$entry.required -ne $entry.name.EndsWith('.dll')) { throw 'Invalid required-library classification.' }
    if($entry.name -eq 'libxess_fg.dll' -and ($entry.version -cne '1.3.1.78' -or $entry.accepted_older)) { throw 'Unknown frame-generation profile.' }
    if($entry.name -eq 'libxell.dll' -and $entry.version -cne '1.3.2.10') { throw 'Unknown latency version.' }
    if($entry.name -eq 'libxell.dll' -and (@($entry.accepted_older).Count -ne 1 -or $entry.accepted_older[0] -cne '61aeeab7fdcfb6d1742f86c14c68c663d759607d01a6132645c857291eaebb86')) { throw 'Unknown optional runtime profile.' }
  }
  return $c
}
function Resolve-XeFinalPath([string]$Path) {
  # Store/Game Pass processes report C:\Program Files\WindowsApps\<package>, a
  # junction to the user-visible install. Resolve to the directory that is
  # actually written, so the reparse-point guard below applies to that path.
  if(-not ('MfgXe.FinalPath' -as [type])) {
    Add-Type -Namespace MfgXe -Name FinalPath -MemberDefinition @'
[DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
public static extern Microsoft.Win32.SafeHandles.SafeFileHandle CreateFileW(string name, uint access, uint share, IntPtr security, uint disposition, uint flags, IntPtr template);
[DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
public static extern uint GetFinalPathNameByHandleW(Microsoft.Win32.SafeHandles.SafeFileHandle file, System.Text.StringBuilder path, uint length, uint flags);
'@
  }
  # Zero access with backup semantics opens files and directories for metadata only.
  $handle=[MfgXe.FinalPath]::CreateFileW([IO.Path]::GetFullPath($Path),0,7,[IntPtr]::Zero,3,0x02000000,[IntPtr]::Zero)
  if($handle.IsInvalid) { throw ('Cannot resolve path: '+$Path) }
  try {
    $buffer=New-Object Text.StringBuilder 32768
    $n=[MfgXe.FinalPath]::GetFinalPathNameByHandleW($handle,$buffer,32768,0)
    if(!$n -or $n -ge 32768) { throw ('Cannot resolve path: '+$Path) }
  } finally { $handle.Dispose() }
  $final=$buffer.ToString()
  if($final.StartsWith('\\?\UNC\')) { return '\\'+$final.Substring(8) }
  if($final.StartsWith('\\?\')) { return $final.Substring(4) }
  return $final
}
function Find-XeInstallRoot([string]$Directory) {
  # Bounded scope for Intel copies the game ships itself, such as engine
  # plugin folders. Unknown layouts stay limited to the executable folder.
  $dir=[IO.Path]::GetFullPath($Directory).TrimEnd('\')
  $current=$dir
  for($i=0;$i -lt 5;$i++) {
    $parent=[IO.Path]::GetDirectoryName($current)
    if(!$parent) { break }
    $grandparent=[IO.Path]::GetDirectoryName($parent)
    if((Split-Path -Leaf $parent) -ieq 'common' -and $grandparent -and (Split-Path -Leaf $grandparent) -ieq 'steamapps') { return $current }
    if((Split-Path -Leaf $current) -ieq 'Content' -and $grandparent -and (Split-Path -Leaf $grandparent) -ieq 'XboxGames') { return $current }
    if(Test-Path -LiteralPath (Join-Path $current '.egstore') -PathType Container) { return $current }
    $current=$parent
  }
  # Unreal Engine: <install>\<Project>\Binaries\Win64\<Project>-Win64-Shipping.exe
  if($dir -match '\\Binaries\\(Win64|WinGDK)$') {
    $candidate=[IO.Path]::GetDirectoryName([IO.Path]::GetDirectoryName([IO.Path]::GetDirectoryName($dir)))
    if($candidate -and [IO.Path]::GetPathRoot($candidate).TrimEnd('\') -ne $candidate.TrimEnd('\')) { return $candidate }
  }
  return $dir
}
function Find-XeRuntimeCopies([string]$Root,[string[]]$Names,[int]$Limit=250000,[int]$MaxDepth=10) {
  # Never follows links or junctions and skips this helper's own backups.
  $found=New-Object Collections.Generic.List[string]; $visited=0; $complete=$true
  $pending=New-Object Collections.Generic.Stack[object]; $pending.Push(@([IO.DirectoryInfo]$Root,0))
  while($pending.Count) {
    $item=$pending.Pop(); $dir=$item[0]; $depth=$item[1]
    try { $entries=@($dir.EnumerateFileSystemInfos()) } catch { $complete=$false; continue }
    foreach($entry in $entries) {
      if(++$visited -gt $Limit) { return [pscustomobject]@{files=$found.ToArray();complete=$false} }
      if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) { continue }
      if($entry -is [IO.DirectoryInfo]) {
        if($entry.Name -ieq '.mfg-dependency-backups') { continue }
        if($depth -lt $MaxDepth) { $pending.Push(@($entry,($depth+1))) } else { $complete=$false }
      } elseif($Names -icontains $entry.Name) { $found.Add($entry.FullName) }
    }
  }
  return [pscustomobject]@{files=$found.ToArray();complete=$complete}
}
function Test-XeWritable([string]$Directory) {
  $probe=Join-Path $Directory ('.mfg-write-'+[guid]::NewGuid().ToString('N'))
  try { [IO.File]::WriteAllText($probe,''); Remove-Item -LiteralPath $probe -Force; return $true }
  catch [UnauthorizedAccessException] { return $false }
}
function Test-XeNeedsInstall([string]$Hash,$Entry) {
  # A missing file is always installed. Without accepted_older,
  # @($Entry.accepted_older) is @($null), which a missing file's $null hash
  # would match and silently drop from the plan.
  if(!$Hash) { return $true }
  if($Hash -eq $Entry.sha256) { return $false }
  if(!$Entry.required) { return $true }
  return $Hash -notin @($Entry.accepted_older | Where-Object { $_ })
}
function Get-XeRuntimeTargets([string]$GameRoot,[string]$InstallRoot,[string]$Name,[string[]]$Mapped,[string[]]$Copies) {
  # XeMFG loads beside the executable (or a mod loader's _storage_), so that
  # copy is always planned. Loaded and shipped copies elsewhere in the same
  # installation are updated together so the game cannot map a different pair.
  $targets=New-Object Collections.Generic.List[object]; $seen=@{}
  $add={ param($path,$role)
    $full=[IO.Path]::GetFullPath($path); $key=$full.ToLowerInvariant()
    if(!$seen.ContainsKey($key)) { $seen[$key]=$true; $targets.Add([pscustomobject]@{path=$full;role=$role}) } }
  & $add (Join-Path $GameRoot $Name) 'game folder'
  $storage=Join-Path $GameRoot ('_storage_\'+$Name)
  if(Test-Path -LiteralPath $storage) { & $add $storage 'mod loader copy' }
  foreach($m in @($Mapped)) { if($m) { & $add $m 'loaded by the game' } }
  foreach($c in @($Copies)) { if($c -and (Split-Path -Leaf $c) -ieq $Name) { & $add $c 'shipped with the game' } }
  foreach($t in $targets) { [void](Assert-XePath $InstallRoot $t.path) }
  return $targets.ToArray()
}
function Assert-XePath([string]$Root,[string]$Path) {
  $base=[IO.Path]::GetFullPath($Root).TrimEnd('\')
  $full=[IO.Path]::GetFullPath($Path)
  if(!$full.StartsWith($base+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Path escapes game directory.' }
  $part=$full
  while($part) {
    if(Test-Path -LiteralPath $part) {
      if((Get-Item -LiteralPath $part -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse-point paths require manual installation.' }
    }
    if($part -eq $base) { break }
    $part=Split-Path -Parent $part
  }
  return $full
}
function Write-XeJournal($Journal,[string]$Path) {
  $temp=$Path+'.writing'
  $json=$Journal | ConvertTo-Json -Depth 12
  $bytes=[Text.Encoding]::UTF8.GetBytes($json)
  $stream=[IO.File]::Open($temp,[IO.FileMode]::Create,[IO.FileAccess]::Write,[IO.FileShare]::None)
  try { $stream.Write($bytes,0,$bytes.Length); $stream.Flush($true) } finally { $stream.Dispose() }
  if(Test-Path -LiteralPath $Path) { [IO.File]::Replace($temp,$Path,[NullString]::Value) } else { [IO.File]::Move($temp,$Path) }
}
function Restore-XeTransaction([string]$JournalPath,[scriptblock]$AssertClosed) {
  & $AssertClosed
  $j=Get-Content -LiteralPath $JournalPath -Raw -Encoding UTF8 | ConvertFrom-Json
  if($j.schema -ne 1) { throw 'Unknown journal schema.' }
  $backupRoot=Split-Path -Parent ([IO.Path]::GetFullPath($JournalPath))
  [void](Assert-XePath $j.root $backupRoot)
  # Validate all targets BEFORE restoring any, including a crash between the
  # atomic replacement and journal advancement. Never overwrite foreign edits.
  foreach($e in $j.files) {
    [void](Assert-XePath $j.root $e.target)
    $current=Get-XeDigest $e.target
    if($current -ne $e.before -and $current -ne $e.after) { throw ('Concurrent change; rollback refused: '+$e.target) }
    if($e.before) {
      [void](Assert-XePath $backupRoot $e.backup)
      if((Get-XeDigest $e.backup) -ne $e.before) { throw 'Backup integrity check failed.' }
    }
  }
  foreach($e in @($j.files)[($j.files.Count-1)..0]) {
    & $AssertClosed
    $current=Get-XeDigest $e.target
    if($current -eq $e.before) { continue }
    if($e.before) {
      $stage=$e.target+'.mfg-restore-'+[guid]::NewGuid().ToString('N')
      try {
        [IO.File]::Copy($e.backup,$stage,$false)
        if((Get-XeDigest $e.target) -ne $e.after) { throw 'Target changed during rollback.' }
        [IO.File]::Replace($stage,$e.target,[NullString]::Value)
      } finally { if(Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Force } }
    } else {
      if((Get-XeDigest $e.target) -ne $e.after) { throw 'New target changed during rollback.' }
      Remove-Item -LiteralPath $e.target
    }
  }
  $j.state='rolled-back'; Write-XeJournal $j $JournalPath
}
function Invoke-XeTransaction([string]$Root,$Plan,[scriptblock]$AssertClosed) {
  if(!$Plan -or !$Plan.Count) { throw 'Nothing to install.' }
  & $AssertClosed
  $rootPath=(Resolve-Path -LiteralPath $Root).Path
  foreach($e in $Plan) {
    [void](Assert-XePath $rootPath $e.target)
    if((Get-XeDigest $e.target) -ne $e.before) { throw 'Target changed after review. Prepare a fresh review.' }
    if((Get-XeDigest $e.stage) -ne $e.after) { throw 'Staged download failed integrity check.' }
  }
  $backup=Join-Path $rootPath ('.mfg-dependency-backups\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N'))
  [void](Assert-XePath $rootPath $backup)
  New-Item -ItemType Directory -Path $backup | Out-Null
  $files=@(); $index=0
  foreach($e in $Plan) {
    $stored=Join-Path $backup ([string]$index+'.original'); $index++
    if($e.before) {
      [IO.File]::Copy($e.target,$stored,$false)
      if((Get-XeDigest $stored) -ne $e.before) { throw 'Backup changed during preparation.' }
    }
    $files+= [ordered]@{target=$e.target;before=$e.before;after=$e.after;stage=$e.stage;backup=$stored}
  }
  $j=[ordered]@{schema=1;root=$rootPath;state='prepared';files=$files}
  $journal=Join-Path $backup 'journal.json'; Write-XeJournal $j $journal
  try {
    foreach($e in $files) {
      & $AssertClosed
      [void](Assert-XePath $rootPath $e.target)
      if((Get-XeDigest $e.target) -ne $e.before) { throw 'Target changed before replacement.' }
      $stage=$e.target+'.mfg-stage-'+[guid]::NewGuid().ToString('N')
      try {
        [IO.File]::Copy($e.stage,$stage,$false)
        if((Get-XeDigest $stage) -ne $e.after) { throw 'Stage copy failed verification.' }
        if((Get-XeDigest $e.target) -ne $e.before) { throw 'Target changed while staging.' }
        if($e.before) { [IO.File]::Replace($stage,$e.target,[NullString]::Value) } else { [IO.File]::Move($stage,$e.target) }
        if((Get-XeDigest $e.target) -ne $e.after) { throw 'Installed hash differs.' }
      } finally { if(Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Force } }
      $j.state='applying'; Write-XeJournal $j $journal
    }
    $j.state='complete'; Write-XeJournal $j $journal
  } catch {
    $reason=$_
    try { Restore-XeTransaction $journal $AssertClosed } catch { Write-Warning ('Rollback requires review: '+$journal+'; '+$_) }
    throw $reason
  }
  return $journal
}
function Start-XeRepair {
  Add-Type -AssemblyName System.Windows.Forms
  Write-XeLog ('Helper started. Game: '+$GameExecutable+' (process '+$GameProcessId+'). Administrator: '+([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator))
  if(!$GameExecutable -or !$GameDirectory) { throw 'Game identity is required.' }
  $root=Resolve-XeFinalPath $GameDirectory
  if((Resolve-XeFinalPath ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($GameExecutable)))) -ne $root) { throw 'Game path does not match installation root.' }
  $installRoot=Find-XeInstallRoot $root
  [void](Open-XeLog $installRoot)
  Write-XeLog ('Intel files folder: '+$root+'. Installation root: '+$installRoot)
  Clear-XeStaleStaging (Split-Path -Parent $PSCommandPath)
  $assertClosed={
    foreach($p in Get-Process -Name ([IO.Path]::GetFileNameWithoutExtension($GameExecutable)) -ErrorAction SilentlyContinue) {
      try { $path=$p.Path } catch { throw 'Cannot verify running game identity. Close it and retry.' }
      if(!$path -or $path -eq $GameExecutable) { throw 'Game is running; installation is deferred until it exits.' }
    }
  }.GetNewClosure()
  if($RollbackJournal) {
    if([Windows.Forms.MessageBox]::Show('Restore the verified dependency backup? Game must be closed.','MFG Unlock rollback','YesNo') -eq 'Yes') {
      Restore-XeTransaction $RollbackJournal $assertClosed
      Write-XeLog ('Rolled back: '+$RollbackJournal)
    } else { Write-XeLog 'Rollback declined.' }
    return
  }
  $process=Get-Process -Id $GameProcessId -ErrorAction Stop
  if($process.Path -ne $GameExecutable -or $process.StartTime.ToFileTimeUtc() -ne $GameCreationTicks) { throw 'The game process identity changed.' }
  $catalog=Read-XeCatalog $CatalogPath
  $base='https://raw.githubusercontent.com/intel/xess/de0fb9c1c510661c571164e1418ceca8101dab69/'
  # Pin the review to selected module paths inside this installation. A copy
  # mapped from elsewhere requires a manual repair rather than a guessed path.
  $mapped=@{}
  foreach($m in $process.Modules) {
    if($m.ModuleName -in @('libxess_fg.dll','libxell.dll')) {
      $file=Resolve-XeFinalPath $m.FileName
      try { [void](Assert-XePath $installRoot $file) } catch { throw ('An Intel DLL is loaded from outside this game installation; repair manually: '+$file) }
      $mapped[$m.ModuleName.ToLowerInvariant()]=$file
      Write-XeLog ('Loaded by the game: '+$file)
    }
  }
  $scan=Find-XeRuntimeCopies $installRoot @('libxess_fg.dll','libxell.dll')
  Write-XeLog ('Intel copies found in the installation: '+@($scan.files).Count+$(if(!$scan.complete){' (search stopped at its size limit)'}else{''}))
  $cache=Join-Path ([IO.Path]::GetTempPath()) ('MFG-XeRepair-'+[guid]::NewGuid().ToString('N'))
  New-Item -ItemType Directory -Path $cache | Out-Null
  try {
  $plan=@(); $roles=@{}
  foreach($entry in $catalog.files) {
    $download=Join-Path $cache $entry.name
    if($entry.name -notin @('libxess_fg.dll','libxell.dll','Intel-XeSS-LICENSE.txt','Intel-XeSS-third-party-programs.txt') -or $entry.source -match '\.\.|:|\\') { throw 'Invalid catalog file.' }
    if($entry.required) { $located=Get-XeRuntimeTargets $root $installRoot $entry.name @($mapped[$entry.name]) $scan.files }
    else { $located=@([pscustomobject]@{path=(Join-Path $root $entry.name);role='game folder'}) }
    foreach($l in $located) { $roles[$l.path]=$l.role }
    $targets=@($located | ForEach-Object { $_.path })
    $needed=@()
    foreach($target in $targets) {
      $hash=Get-XeDigest $target; $install=Test-XeNeedsInstall $hash $entry
      if($install) { $needed+=$target }
      Write-XeLog ($entry.name+' at '+$target+' ('+$roles[$target]+'): '+$(if(!$hash){'missing'}elseif($hash -eq $entry.sha256){'pinned version'}elseif(!$install){'accepted older version'}else{'different file '+$hash})+' -> '+$(if($install){'install'}else{'keep'}))
    }
    if(!$needed.Count) { continue }
    Write-Host ('Downloading original Intel file: '+$entry.name)
    Write-XeLog ('Downloading '+$base+$entry.source)
    [Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -UseBasicParsing -Uri ($base+$entry.source) -OutFile $download
    if((Get-XeDigest $download) -ne $entry.sha256) { throw 'Official download did not match the pinned hash.' }
    if($entry.required -and [Diagnostics.FileVersionInfo]::GetVersionInfo($download).FileVersion -ne $entry.version) { throw 'Downloaded runtime version differs from the admitted profile.' }
    Write-XeLog ('Download verified: '+$entry.name+' '+$entry.sha256)
    foreach($target in $needed) {
      [void](Assert-XePath $installRoot $target)
      $plan+=[ordered]@{target=$target;before=(Get-XeDigest $target);after=$entry.sha256;stage=$download}
    }
  }
  if(!$plan.Count) {
    Write-XeLog 'Nothing to install: the Intel files are already admitted.'
    [void][Windows.Forms.MessageBox]::Show("The installed Intel files are already admitted. No replacement is needed.`r`nLog: "+$script:XeLog,'MFG Unlock'); return
  }
  $blocked=@($plan | ForEach-Object { [IO.Path]::GetDirectoryName($_.target) } | Sort-Object -Unique | Where-Object { !(Test-XeWritable $_) })
  if($blocked.Count) {
    $admin=([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    if($admin) { throw ('Cannot write to: '+($blocked -join ', ')) }
    Write-XeLog ('Administrator permission needed for: '+($blocked -join ', '))
    $ask="Windows requires administrator permission to write to:`r`n"+($blocked -join "`r`n")+"`r`n`r`nRestart this helper as administrator? You will review the same file list again."
    if([Windows.Forms.MessageBox]::Show($ask,'MFG Unlock','YesNo','Question') -ne 'Yes') { Write-XeLog 'Administrator restart declined.'; return }
    $arguments='-NoProfile -ExecutionPolicy Bypass -File "'+$PSCommandPath+'" -GameDirectory "'+$GameDirectory+'" -GameProcessId '+$GameProcessId+' -GameCreationTicks '+$GameCreationTicks+' -GameExecutable "'+$GameExecutable+'" -CatalogPath "'+$CatalogPath+'"'
    Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') -Verb RunAs -ArgumentList $arguments | Out-Null
    Write-XeLog 'Continuing in the administrator helper.'
    return 'elevated'
  }
  $review=($plan | ForEach-Object { $_.target+"`r`n  Location: "+$roles[$_.target]+"`r`n  Existing: "+$(if($_.before){$_.before}else{'MISSING'})+"`r`n  Install: "+$_.after }) -join "`r`n`r`n"
  $scope="Game installation: "+$installRoot+$(if(!$scan.complete){"`r`nSearch stopped at its size limit; copies deeper in this folder were not listed."}else{''})
  $message="Install only these verified original Intel files? Unknown existing files will be backed up. Settings, overlays and libxess.dll are preserved.`r`n`r`n"+$scope+"`r`n`r`n"+$review+"`r`n`r`nRead LICENSE and notices in: "+$cache+"`r`nAfter confirmation this helper waits for the game to exit. It will never close the game for you."
  if([Windows.Forms.MessageBox]::Show($message,'Review Intel dependency repair','YesNo','Question') -ne 'Yes') { Write-XeLog 'Review declined; nothing changed.'; return }
  Write-XeLog ('Approved '+$plan.Count+' file(s). Waiting for the game to exit.')
  Write-Host 'Approved. Close the game normally to continue; do not relaunch until this helper finishes.'
  $process.WaitForExit()
  $journal=Invoke-XeTransaction $installRoot $plan $assertClosed
  Write-XeLog ('Repair complete. Journal: '+$journal)
  [void][Windows.Forms.MessageBox]::Show("Repair complete. Restart the game.`r`nRollback journal: "+$journal+"`r`nLog: "+$script:XeLog,'MFG Unlock')
  } finally { Remove-XeStaging $cache }
}
if($MyInvocation.InvocationName -ne '.') {
  # Exit 3 tells the addon that an administrator copy continues the review.
  # That copy runs this script from the same temporary folder, so the folder
  # is removed by whichever copy finishes the review.
  $outcome=$null
  try { $outcome=Start-XeRepair } catch {
    Write-XeLog ('Repair stopped: '+$_.Exception.Message)
    if(!$script:XeLog) { [void](Open-XeLog $null) }
    Write-Error $_ -ErrorAction Continue
    try { Add-Type -AssemblyName System.Windows.Forms; [void][Windows.Forms.MessageBox]::Show($_.Exception.Message+"`r`nLog: "+$script:XeLog,'MFG Unlock repair stopped') } catch {}
    Remove-XeStaging (Split-Path -Parent $PSCommandPath)
    exit 1
  }
  if($outcome -eq 'elevated') { exit 3 }
  Remove-XeStaging (Split-Path -Parent $PSCommandPath)
}

