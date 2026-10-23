[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$RenoDxSource,
  [Parameter(Mandatory=$true)][string]$XessSdkRoot,
  [Parameter(Mandatory=$true)][string]$ProgramSourceAsi,
  [string]$BuildDirectory = 'build/tests',
  [string]$OfflineFg = '', [string]$OfflineLl = '', [string]$OfflineHost = '',
  [string]$OfflineReShade = ''
)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
  & git merge-base --is-ancestor 0fe65579d287581107babe267ee3e48c9135bf8a HEAD
  if($LASTEXITCODE) { throw 'Expected a descendant of official 1.4.5.' }
  $headers=@{
    'blackwell_cubins.generated.hpp'='388f6f264102c6fbfa694719e9f5fa7d3b239a8892ff5ce37153a8f80adf60a4'
    'thin_geometry_cubins.generated.hpp'='d8fb04a369315539b63b3d72f59ae644ca7e517cd12dce63fe4358d648794bbe'
    'thin_geometry_stability.generated.hpp'='688f033ded45effdcfbdffe27194c87749b50a20442f7595a3ee72dfb7d7d968'
  }
  foreach($name in $headers.Keys) {
    $path=Join-Path $root ('src/addons/mfgunlock/'+$name)
    if(!(Test-Path -LiteralPath $path) -or (Get-FileHash -LiteralPath $path).Hash -ne $headers[$name]) {
      throw ('Provide the unchanged, local official 1.4.5 input: '+$name)
    }
  }
  & python tools/prepare_xemfg_inputs.py --asi $ProgramSourceAsi --output build/xemfg-inputs
  if($LASTEXITCODE) { throw 'Unverified XeMFG inputs.' }
  & python src/addons/mfgunlock/xemfg/vendor/generate_xess_programs.py --programs build/xemfg-inputs --output build/xemfg-generated
  if($LASTEXITCODE) { throw 'Program table failed.' }
  & cmake -S tests -B $BuildDirectory -G 'Visual Studio 17 2022' -A x64 "-DRENODX_SOURCE_DIR=$RenoDxSource" `
    -DMFGUNLOCK_BUILD_XEMFG=ON "-DXEMFG_SDK_ROOT=$XessSdkRoot" "-DXEMFG_OFFLINE_FG=$OfflineFg" "-DXEMFG_OFFLINE_LL=$OfflineLl" "-DXEMFG_OFFLINE_HOST=$OfflineHost" `
    "-DXEMFG_OFFLINE_RESHADE=$OfflineReShade" "-DXEMFG_INPUTS=$root/build/xemfg-inputs" "-DXEMFG_GENERATED=$root/build/xemfg-generated"
  if($LASTEXITCODE) { throw 'Configure failed.' }
  & cmake --build $BuildDirectory --config Release --parallel 4
  if($LASTEXITCODE) { throw 'Build failed.' }
  & ctest --test-dir $BuildDirectory -C Release --output-on-failure
  if($LASTEXITCODE) { throw 'Tests failed.' }
} finally { Pop-Location }
