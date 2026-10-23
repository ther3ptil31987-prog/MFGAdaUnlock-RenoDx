$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$dir=Join-Path $root 'build/tests/semantic-fixtures'
New-Item -ItemType Directory -Path $dir -Force | Out-Null
function Check($value,$message) { if (!$value) { throw $message } }
function Capture([string]$name,[string]$latency,[string]$path,[string]$mode='Hardware: Independent Flip') {
  $file=Join-Path $dir ($name+'.csv')
  $rows=for($i=0;$i -lt 128;$i++) {
    $row=[ordered]@{Application='test.exe';ProcessID=1;SwapChainAddress='0x1';PresentMode=$mode;
      AnimationTime=if($i%4 -eq 0){$i+1}else{0};DisplayedTime=5;FrameTime=20;GPUBusy=4}
    $row[$latency]=12
    [pscustomobject]$row
  }
  $rows | Export-Csv -LiteralPath $file -NoTypeInformation
  & (Join-Path $root 'src/addons/mfgdiagnostics/Analyze-PresentMon.ps1') -Path $file -CollectorVersion '2.4-test' -TimingPath $path | Out-Null
  return [IO.Path]::ChangeExtension($file,'.summary.json')
}
$a=Capture 'a' 'InstrumentedLatency' 'runtime'
$b=Capture 'b' 'DisplayLatency' 'legacy'
$report=Get-Content -LiteralPath $a -Raw | ConvertFrom-Json
Check ($report.schema -eq 2) 'Missing semantic schema'
Check ($report.metric_semantics.pc_or_instrumented_latency_ms.column -eq 'InstrumentedLatency') 'Column identity lost'
Check ($report.original_column_metrics.InstrumentedLatency.mean_ms -eq 12) 'Original metric missing'
$json=& (Join-Path $root 'src/addons/mfgdiagnostics/Compare-PresentMon.ps1') -Baseline $a -Candidate $b
$comparison=$json | ConvertFrom-Json
Check $comparison.metrics.display_intervals.comparable 'Same display metric must remain comparable across paths'
Check (!$comparison.metrics.pc_or_instrumented_latency_ms.comparable) 'Different latency endpoints were compared'
Check (!$comparison.metrics.gpu_busy_ms.comparable) 'GPU timing across paths was compared'
$c=Capture 'c' 'InstrumentedLatency' 'runtime' 'Hardware Composed: Independent Flip'
$xe=Capture 'xemfg' 'InstrumentedLatency' 'xemfg'
$xeReport=Get-Content -LiteralPath $xe -Raw | ConvertFrom-Json
Check ($null -eq $xeReport.application_intervals -and $null -eq $xeReport.inferred_frame_generation) 'XeFG reused NVIDIA source-frame inference'
Check ($xeReport.display_intervals.p999_ms -eq 5) 'Displayed p99.9 missing'
Check (!$xeReport.metric_semantics.application_intervals) 'Unavailable source population has a comparable identity'
& (Join-Path $root 'src/addons/mfgdiagnostics/Analyze-PresentMon.ps1') -Path ($xe.Replace('.summary.json','.csv')) -TimingPath 'xemfg' -DisplayedStutterThresholdMs 4 | Out-Null
$fixed=Get-Content -LiteralPath $xe -Raw | ConvertFrom-Json
Check ($fixed.display_intervals.large_interval_threshold_ms -eq 4 -and $fixed.display_intervals.large_intervals -eq 128) 'Fixed display outlier threshold not respected'
$comparison=(& (Join-Path $root 'src/addons/mfgdiagnostics/Compare-PresentMon.ps1') -Baseline $a -Candidate $c) | ConvertFrom-Json
Check (!$comparison.metrics.display_intervals.comparable) 'Presentation-mode confound was hidden'
$legacy=Join-Path $dir 'old.json'; '{"schema":1}' | Set-Content -LiteralPath $legacy
$comparison=(& (Join-Path $root 'src/addons/mfgdiagnostics/Compare-PresentMon.ps1') -Baseline $legacy -Candidate $a) | ConvertFrom-Json
Check (!$comparison.metrics.display_intervals.comparable) 'Old summaries must require re-analysis'
Write-Output 'PresentMon column identity, timing path, legacy schema and presentation-mode checks passed'

foreach ($script in @('Capture-Performance.ps1','Analyze-PresentMon.ps1','Compare-PresentMon.ps1')) {
  $tokens=$null; $parseErrors=$null
  [void][System.Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $root ('src/addons/mfgdiagnostics/'+$script)),[ref]$tokens,[ref]$parseErrors)
  Check (!$parseErrors.Count) ('PowerShell syntax error in '+$script)
}

$bad=Get-Content -LiteralPath $a -Raw | ConvertFrom-Json
$bad.metric_semantics.display_intervals.column=$null
$badPath=Join-Path $dir 'missing-identity.json'
$bad | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $badPath -Encoding UTF8
$comparison=(& (Join-Path $root 'src/addons/mfgdiagnostics/Compare-PresentMon.ps1') -Baseline $badPath -Candidate $badPath) | ConvertFrom-Json
Check (!$comparison.metrics.display_intervals.comparable) 'A matching but incomplete identity must not be accepted'
