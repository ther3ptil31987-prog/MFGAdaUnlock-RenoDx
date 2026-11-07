<#
.SYNOPSIS
  Prepares an MFG Unlock release and prints the publishing commands. It never
  pushes, tags or publishes anything.

.DESCRIPTION
  1.9.0. Checks, in order, and stops at the first problem:
    - the working tree is clean and the branch is not main;
    - the version string is the same in the six files that carry it;
    - docs/releases/<Version>.md exists and README.md mentions the version;
    - fork/main can fast-forward to HEAD (after a fetch; a commit made on
      GitHub means a rebase first);
    - the native addon builds (unless -SkipBuild).
  Then it writes build/release-<Version>/ with the renamed asset and body.md
  (the notes without their title, relative links made absolute), and prints
  the push, tag and gh commands for a person to run after the full gate.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\Prepare-Release.ps1 -Version 1.9.0 -Title "MFG Unlock 1.9.0 - ..."
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version,
  [Parameter(Mandatory = $true)][string]$Title,
  [string]$Remote = 'fork',
  [string]$Repository = 'mavismmg/MFGAdaUnlock-RenoDx',
  [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

function Stop-Release([string]$Message) {
  Write-Host "STOP: $Message" -ForegroundColor Red
  exit 1
}
function Step([string]$Message) { Write-Host "-- $Message" -ForegroundColor Cyan }

Step 'Working tree and branch'
$status = git status --porcelain
if ($status) { Stop-Release "the working tree has changes:`n$status" }
$branch = (git rev-parse --abbrev-ref HEAD).Trim()
if ($branch -eq 'main' -or $branch -eq 'HEAD') { Stop-Release "release from a feature branch, not '$branch'" }

Step "Version strings ($Version)"
$expected = @(
  @{ File = 'src/addons/mfgunlock/addon.cpp';                 Text = "MFG Unlock $Version - DLSS-G and XeMFG backends" },
  @{ File = 'src/addons/mfgunlock/addon_overlay.inl';         Text = "MFG Unlock $Version\nmethod=" },
  @{ File = 'src/addons/mfgunlock/panel_ui.hpp';              Text = "MFG Unlock | $Version by" },
  @{ File = 'src/addons/mfgunlock/xemfg/native_controls.cpp'; Text = "MFG Unlock $Version\nstartup_failed=" },
  @{ File = 'src/addons/mfgunlock/xemfg/native_notices.rc.in'; Text = "`"$Version.0\0`"" },
  @{ File = 'src/addons/mfgunlock/xemfg/native_notices.rc.in'; Text = "`"$Version\0`"" },
  @{ File = 'src/addons/mfgunlock/xemfg/native_notices.rc.in'; Text = "MFG Unlock $Version - DLSS-G and XeMFG backends\0" },
  @{ File = 'src/addons/mfgunlock/xemfg/native_session.cpp';  Text = '\"addon\":\"' + $Version + '\"' }
)
$missing = @()
foreach ($item in $expected) {
  $content = [IO.File]::ReadAllText((Join-Path $root $item.File))
  if (-not $content.Contains($item.Text)) { $missing += "$($item.File): $($item.Text)" }
}
if ($missing.Count -gt 0) { Stop-Release ("version string not found:`n  " + ($missing -join "`n  ")) }

Step 'Release notes and README'
$notes = Join-Path $root "docs/releases/$Version.md"
if (-not (Test-Path $notes)) { Stop-Release "docs/releases/$Version.md does not exist" }
$readme = [IO.File]::ReadAllText((Join-Path $root 'README.md'))
if (-not $readme.Contains("docs/releases/$Version.md")) { Stop-Release "README.md does not link docs/releases/$Version.md" }

Step "Fast-forward of $Remote/main"
git fetch $Remote --quiet
if ($LASTEXITCODE -ne 0) { Stop-Release "git fetch $Remote failed" }
git merge-base --is-ancestor "$Remote/main" HEAD
if ($LASTEXITCODE -ne 0) {
  $ahead = git log --oneline "HEAD..$Remote/main"
  Stop-Release "$Remote/main has commits this branch does not (rebase onto it first):`n$ahead"
}
$tagExists = git tag --list $Version
if ($tagExists) { Stop-Release "tag $Version already exists" }

$asset = Join-Path $root 'build/tests/xemfg/Release/renodx-mfgunlock-native.addon64'
if (-not $SkipBuild) {
  Step 'Build'
  cmake --build build/tests --config Release --target mfgunlock_native_xemfg --parallel | Out-Null
  if ($LASTEXITCODE -ne 0) { Stop-Release 'the build failed' }
}
if (-not (Test-Path $asset)) { Stop-Release "no build output at $asset" }

Step 'Release folder'
$out = Join-Path $root "build/release-$Version"
New-Item -ItemType Directory -Force $out | Out-Null
Copy-Item $asset (Join-Path $out 'renodx-mfgunlock.addon64') -Force
$blob = "https://github.com/$Repository/blob/main"
$lines = [IO.File]::ReadAllLines($notes)
$start = 0
if ($lines.Length -gt 0 -and $lines[0].StartsWith('# ')) { $start = 1 }
while ($start -lt $lines.Length -and $lines[$start].Trim() -eq '') { $start++ }
$body = ($lines[$start..($lines.Length - 1)] -join "`n")
$body = $body.Replace('](../../README.md', "]($blob/README.md")
$body = [regex]::Replace($body, '\]\((\d+\.\d+\.\d+)\.md\)', "]($blob/docs/releases/`$1.md)")
[IO.File]::WriteAllText((Join-Path $out 'body.md'), $body, (New-Object Text.UTF8Encoding($false)))
$relative = $body | Select-String -Pattern '\]\((?!https?://|#)[^)]+\)' -AllMatches
if ($relative) { Write-Host "Note: body.md still has relative links: $($relative.Matches.Value -join ', ')" -ForegroundColor Yellow }
$hash = (Get-FileHash (Join-Path $out 'renodx-mfgunlock.addon64') -Algorithm SHA256).Hash

Write-Host ''
Write-Host "Prepared $Version from $branch at $((git rev-parse --short HEAD).Trim())." -ForegroundColor Green
Write-Host "  asset: $out\renodx-mfgunlock.addon64 (SHA-256 $hash)"
Write-Host "  notes: $out\body.md"
Write-Host ''
Write-Host 'Nothing was pushed. After the full gate passes, run:' -ForegroundColor Yellow
Write-Host "  git push $Remote $branch"
Write-Host "  git push $Remote HEAD:main"
Write-Host "  git push $Remote HEAD:develop"
Write-Host "  git tag -a $Version -m `"$Title`""
Write-Host "  git push $Remote $Version"
Write-Host "  gh release create $Version -R $Repository --verify-tag --title `"$Title`" --notes-file `"$out\body.md`" `"$out\renodx-mfgunlock.addon64`""
