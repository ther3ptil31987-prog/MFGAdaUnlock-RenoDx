[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$RenoDxSource,
  [string]$BuildDirectory = 'build/tests',
  [string[]]$OfflineProviders = @(),
  [ValidateSet('engineering-experimental','release-1.4.5')][string]$Variant = 'engineering-experimental'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
  & git merge-base --is-ancestor 3e898157fd59ed9eef65c181619060ddb305c6bd HEAD
  if ($LASTEXITCODE) { throw 'Expected a descendant of release 1.4.4.' }
  $headers = @{
    'thin_geometry_cubins.generated.hpp'='d8fb04a369315539b63b3d72f59ae644ca7e517cd12dce63fe4358d648794bbe'
    'blackwell_cubins.generated.hpp'='388f6f264102c6fbfa694719e9f5fa7d3b239a8892ff5ce37153a8f80adf60a4'
    'thin_geometry_stability.generated.hpp'='688f033ded45effdcfbdffe27194c87749b50a20442f7595a3ee72dfb7d7d968'
  }
  foreach ($name in $headers.Keys) {
    $path = Join-Path $root ('src/addons/mfgunlock/'+$name)
    if (!(Test-Path -LiteralPath $path) -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $headers[$name]) {
      throw ('Missing or different local 1.4.4 generated input: '+$name)
    }
  }
  # Generated tables are verified, never regenerated or overwritten by this script.
  & cmake -S tests -B $BuildDirectory -G 'Visual Studio 17 2022' -A x64 "-DRENODX_SOURCE_DIR=$RenoDxSource" "-DENGINEERING_OFFLINE_PROVIDERS=$($OfflineProviders -join ';')"
  if ($LASTEXITCODE) { throw 'CMake configuration failed.' }
  & cmake --build $BuildDirectory --config Release
  if ($LASTEXITCODE) { throw 'Test suite build failed.' }
  & cmake --build $BuildDirectory --config Release --target mfgunlock_engineering_experimental
  if ($LASTEXITCODE) { throw 'Experimental addon build failed.' }
  if ($Variant -eq 'release-1.4.5') {
    & cmake --build $BuildDirectory --config Release --target mfgunlock_release_1_4_5
    if ($LASTEXITCODE) { throw 'Release 1.4.5 addon build failed.' }
  }
  & ctest --test-dir $BuildDirectory -C Release --output-on-failure
  if ($LASTEXITCODE) { throw 'Validation failed; no package should be promoted.' }
  $artifactName = if ($Variant -eq 'release-1.4.5') { 'renodx-mfgunlock-1.4.5' } else { 'renodx-mfgunlock-engineering-exp' }
  $artifactTarget = if ($Variant -eq 'release-1.4.5') { 'mfgunlock_release_1_4_5' } else { 'mfgunlock_engineering_experimental' }
  & python tools/write_build_manifest.py --addon "$BuildDirectory/Release/$artifactName.addon64" --source-root . --variant $Variant --build-project "$BuildDirectory/$artifactTarget.vcxproj"
  if ($LASTEXITCODE) { throw 'Artifact provenance failed.' }
  Write-Output "$Variant artifact built; game installation and gameplay validation are separate steps."
} finally { Pop-Location }
