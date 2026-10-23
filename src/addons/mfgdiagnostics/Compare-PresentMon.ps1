[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Baseline,
      [Parameter(Mandatory=$true)][string]$Candidate,
      [string]$Output)
$ErrorActionPreference = 'Stop'
$a = Get-Content -LiteralPath $Baseline -Raw | ConvertFrom-Json
$b = Get-Content -LiteralPath $Candidate -Raw | ConvertFrom-Json
$metrics = [ordered]@{}
foreach ($name in @('display_intervals','application_intervals','present_intervals','present_api_ms',
    'gpu_busy_ms','gpu_time_ms','source_frame_gpu_busy_ms','source_frame_gpu_time_ms',
    'generated_candidate_gpu_busy_ms','generated_candidate_gpu_time_ms',
    'pc_or_instrumented_latency_ms','source_frame_latency_ms','generated_candidate_latency_ms','animation_error_ms')) {
  $reason = ''
  $left = if ($a.metric_semantics) { $a.metric_semantics.$name } else { $null }
  $right = if ($b.metric_semantics) { $b.metric_semantics.$name } else { $null }
  if ($a.schema -lt 2 -or $b.schema -lt 2 -or !$left -or !$right) { $reason = 'Metric identity unavailable (legacy schema or missing column).' }
  elseif (!$a.$name -or !$b.$name) { $reason = 'Metric samples unavailable.' }
  elseif ([string]::IsNullOrWhiteSpace([string]$a.selected_present_mode) -or [string]::IsNullOrWhiteSpace([string]$b.selected_present_mode)) { $reason = 'Presentation mode is unrecorded.' }
  elseif ($a.selected_present_mode -ne $b.selected_present_mode) { $reason = 'Presentation mode changed; the combined effect requires separate interpretation.' }
  elseif ($left.collector_version -eq 'unknown' -or $right.collector_version -eq 'unknown') { $reason = 'Collector version is unrecorded.' }
  else {
    foreach ($key in @('column','units','definition','population','classification','collector_version')) {
      if ([string]::IsNullOrWhiteSpace([string]($left.$key)) -or [string]::IsNullOrWhiteSpace([string]($right.$key))) { $reason = "Metric identity missing: $key."; break }
      if ($left.$key -ne $right.$key) { $reason = "Metric identity differs: $key."; break }
    }
    if (!$reason -and !$left.cross_path_comparable -and
        ([string]::IsNullOrWhiteSpace([string]$left.runtime_identity) -or [string]::IsNullOrWhiteSpace([string]$right.runtime_identity) -or $left.runtime_identity -eq 'unknown' -or $right.runtime_identity -eq 'unknown' -or $left.runtime_identity -ne $right.runtime_identity)) {
      $reason = 'Active runtime identity is unknown or differs.'
    }
    if (!$reason -and !$left.cross_path_comparable -and
        ([string]::IsNullOrWhiteSpace([string]$left.timing_path) -or [string]::IsNullOrWhiteSpace([string]$right.timing_path) -or $left.timing_path -eq 'unknown' -or $right.timing_path -eq 'unknown' -or $left.timing_path -ne $right.timing_path)) {
      $reason = 'Runtime timing domain is unknown or differs across paths.'
    }
  }
  $metrics[$name] = [ordered]@{
    comparable = !$reason
    reason = $reason
    mean_delta_ms = if (!$reason) { [math]::Round($b.$name.mean_ms - $a.$name.mean_ms,4) } else { $null }
    p99_delta_ms = if (!$reason) { [math]::Round($b.$name.p99_ms - $a.$name.p99_ms,4) } else { $null }
    p999_delta_ms = if (!$reason -and $null -ne $a.$name.p999_ms -and $null -ne $b.$name.p999_ms) { [math]::Round($b.$name.p999_ms - $a.$name.p999_ms,4) } else { $null }
  }
}
$report = [ordered]@{schema=1; baseline=$Baseline; candidate=$Candidate; metrics=$metrics;
  caution='Comparable definitions do not establish causal improvement; repeated paired runs and physical latency remain separate requirements.'}
$json = $report | ConvertTo-Json -Depth 8
if ($Output) { $json | Set-Content -LiteralPath $Output -Encoding UTF8 }
$json
