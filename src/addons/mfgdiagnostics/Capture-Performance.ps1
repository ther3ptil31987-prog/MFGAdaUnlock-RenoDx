[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]*$')]
  [string]$Label,

  [ValidateRange(10, 300)]
  [int]$DurationSeconds = 30,

  [Parameter(Mandatory = $true)]
  [ValidateNotNullOrEmpty()]
  [string]$ProcessName,

  [string]$ConfigurationNotes = '',
  [ValidateSet('unknown','runtime','legacy','native','xemfg')][string]$TimingPath = 'unknown',
  [string]$RuntimeIdentity = 'unknown',
  [string]$BuildManifest = '',
  [ValidateRange(0,10000)][double]$DisplayedStutterThresholdMs = 0,

  [string]$OutputRoot = (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'MFGBenchmarks'),

  [string]$PresentMonPath = (Join-Path ([Environment]::GetFolderPath('MyDocuments')) `
    'Tooling\PresentMon\PresentMon-2.4.1-x64.exe'),

  [string]$NvapiProbePath = (Join-Path ([Environment]::GetFolderPath('MyDocuments')) `
    'Tooling\NVIDIA\nvapi_mfg_probe.exe')
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $PresentMonPath -PathType Leaf)) {
  throw "PresentMon was not found at: $PresentMonPath"
}

$processes = @(Get-Process -Name ([IO.Path]::GetFileNameWithoutExtension($ProcessName)) `
  -ErrorAction SilentlyContinue)
if ($processes.Count -ne 1) {
  throw "Expected exactly one $ProcessName process, found $($processes.Count). Start the game first."
}
$target = $processes[0]

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$directory = Join-Path $OutputRoot "$stamp-$Label"
New-Item -ItemType Directory -Force -Path $directory | Out-Null
$csv = Join-Path $directory 'presentmon.csv'

$modules = @()
try {
  $modules = @($target.Modules | ForEach-Object {
    $loadedModule = $_
    $path = $loadedModule.FileName
    $name = [IO.Path]::GetFileName($path)
    if ($name -match '^(sl\.|nvngx_)' -or
        $name -match '^(ReShade|renodx-mfg|libxess_fg|libxell|nvcuda|nvapi64)' -or
        $name -like '*.addon64' -or $path -match '(?i)[\\/]models[\\/](dlssg|sl_dlss_g_0)[\\/]') {
      $item = Get-Item -LiteralPath $path -ErrorAction Stop
      [ordered]@{
        name = $name
        path = $path
        base_address = ('0x{0:x}' -f $loadedModule.BaseAddress.ToInt64())
        image_bytes = $loadedModule.ModuleMemorySize
        role = 'mapped candidate; active feature selection unproven'
        file_version = $item.VersionInfo.FileVersion
        product_version = $item.VersionInfo.ProductVersion
        sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
      }
    }
  })
} catch {
  $modules = @([ordered]@{ inventory_error = $_.Exception.Message })
}

$video = @()
try {
  $video = @(Get-CimInstance Win32_VideoController | ForEach-Object {
    [ordered]@{
      name = $_.Name
      driver_version = $_.DriverVersion
      driver_date = $_.DriverDate
    }
  })
} catch {
  $video = @([ordered]@{ inventory_error = $_.Exception.Message })
}

$collectorItem = Get-Item -LiteralPath $PresentMonPath
$collectorHash = (Get-FileHash -LiteralPath $PresentMonPath -Algorithm SHA256).Hash
$collectorIdentity = "$( $collectorItem.VersionInfo.FileVersion )|sha256=$collectorHash"
$provenance = [ordered]@{ status = 'manifest not supplied'; active_provider = 'unproven' }
if ($BuildManifest) {
  $manifest = Get-Content -LiteralPath $BuildManifest -Raw | ConvertFrom-Json
  $addonHash=$manifest.addon_sha256
  if(!$addonHash -and $manifest.assets) {
    $addons=@($manifest.assets | Where-Object { $_.name -eq 'renodx-mfgunlock.addon64' })
    if($addons.Count -eq 1) { $addonHash=$addons[0].sha256 }
  }
  if (!$addonHash -or !(@($modules | Where-Object { $_.sha256 -eq $addonHash }).Count)) {
    throw 'Build manifest does not match a mapped addon hash; refusing an ambiguous capture.'
  }
  Copy-Item -LiteralPath $BuildManifest -Destination (Join-Path $directory 'addon-build-manifest.json')
  $provenance = [ordered]@{ status='mapped addon hash matched'; source_commit=$manifest.source_commit;
    addon_sha256=$addonHash; manifest_sha256=(Get-FileHash -LiteralPath $BuildManifest -Algorithm SHA256).Hash;
    active_provider='unproven; use RuntimeIdentity only after feature selection is established' }
}
$metadata = [ordered]@{
  schema = 2
  build_provenance = $provenance
  timing_path = $TimingPath
  active_runtime_identity = $RuntimeIdentity
  collector_identity = $collectorIdentity
  collector_path = $PresentMonPath
  label = $Label
  captured_at = (Get-Date).ToString('o')
  duration_seconds = $DurationSeconds
  process = $target.ProcessName
  process_id = $target.Id
  configuration_notes = $ConfigurationNotes
  video_controllers = $video
  loaded_runtime_modules = $modules
  cautions = @(
    'The module list is a point-in-time inventory, not proof that every mapped candidate executed.',
    'Record game build, save, route, resolution, HDR, VSync/G-SYNC, cap and multiplier in configuration_notes.',
    'Do not compare runs made with different drivers, runtime DLLs or addon hashes.'
  )
}
$metadata | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (
  Join-Path $directory 'capture-metadata.json') -Encoding UTF8

if ($TimingPath -ne 'xemfg' -and (Test-Path -LiteralPath $NvapiProbePath -PathType Leaf)) {
  & $NvapiProbePath $target.Id | Set-Content -LiteralPath (Join-Path $directory 'nvapi-before.txt')
}

$arguments = @(
  '--process_id', [string]$target.Id,
  '--output_file', $csv,
  '--qpc_time_ms',
  '--v2_metrics',
  '--track_frame_type',
  '--track_pc_latency',
  '--timed', [string]$DurationSeconds,
  '--terminate_after_timed',
  '--no_console_stats',
  '--session_name', "MFG-$stamp"
)

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
$isAdmin = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

Write-Host "Capturing PID $($target.Id) for $DurationSeconds seconds: $Label"
Write-Host 'Keep the ReShade overlay closed and reproduce the same camera path.'
if ($isAdmin) {
  & $PresentMonPath @arguments
  if ($LASTEXITCODE -ne 0) { throw "PresentMon exited with code $LASTEXITCODE" }
} else {
  Write-Host 'PresentMon requires elevation for ETW; accept the Windows UAC prompt.'
  $run = Start-Process -FilePath $PresentMonPath -ArgumentList $arguments -Verb RunAs -Wait -PassThru
  if ($run.ExitCode -ne 0) { throw "PresentMon exited with code $($run.ExitCode)" }
}

if ($TimingPath -ne 'xemfg' -and (Test-Path -LiteralPath $NvapiProbePath -PathType Leaf)) {
  & $NvapiProbePath $target.Id | Set-Content -LiteralPath (Join-Path $directory 'nvapi-after.txt')
}
if (-not (Test-Path -LiteralPath $csv -PathType Leaf)) {
  throw "PresentMon completed but did not create $csv"
}

$analyzer = Join-Path $PSScriptRoot 'Analyze-PresentMon.ps1'
if (Test-Path -LiteralPath $analyzer -PathType Leaf) {
  & $analyzer -Path $csv -Label $Label -CollectorVersion $collectorIdentity -TimingPath $TimingPath -RuntimeIdentity $RuntimeIdentity -DisplayedStutterThresholdMs $DisplayedStutterThresholdMs
}
Write-Host "Capture saved under: $directory"
