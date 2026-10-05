param(
    [Parameter(Mandatory = $true)]
    [string] $AddonPath
)

$ErrorActionPreference = 'Stop'
$resolvedAddon = (Resolve-Path -LiteralPath $AddonPath).Path
$addonText = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($resolvedAddon))
$identity = 'Local low-overhead; V2 inpaint; no confidence history or CUDA/NVAPI launch interception'
if (!$addonText.Contains($identity)) {
    throw 'Local build identity missing; refusing to audit a different variant.'
}
# These are host-side lookup/self-test strings, not names in the embedded
# approved provider cubin table. Legitimate NVAPI status/VRR support is retained.
foreach ($forbidden in @(
    'cuLaunchKernel', 'cuLaunchKernelEx', 'cuFuncGetModule', 'cuModuleGetGlobal',
    'nvcuda.dll', 'GPU R8 self-test', 'MfgUnlockPublishControl'
)) {
    if ($addonText.Contains($forbidden)) {
        throw "Temporal host entry point retained: $forbidden"
    }
}
Write-Output 'Local PE audit passed: no CUDA hook lookup strings or NVAPI temporal self-test.'
Get-FileHash -LiteralPath $resolvedAddon -Algorithm SHA256
