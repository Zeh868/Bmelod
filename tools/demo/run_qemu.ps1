param(
    [Parameter(Mandatory = $true)][string]$Example,
    [int]$TimeoutSec = 60
)

$ErrorActionPreference = 'Stop'
$DemoToolsDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
. (Join-Path $DemoToolsDir 'demo_paths.ps1')

$Qemu = (Get-Command qemu-system-arm -ErrorAction SilentlyContinue).Source

if (-not (Test-KnownDemoExample $Example)) {
    Write-Error "Unknown example '$Example'"
    exit 1
}
if (-not $Qemu) {
    Write-Error 'qemu-system-arm not found in PATH'
}

Ensure-DemoUnifiedConfigure -Variant 'qemu'
$BuildRoot = Get-DemoVariantRoot -Variant 'qemu'
Ensure-DemoQemuFrameworkBuilt -BuildRoot $BuildRoot

cmake --build $BuildRoot --target "$Example.elf"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$Elf = Get-DemoElfPath -Variant 'qemu' -Example $Example
$outFile = Join-Path $env:TEMP "bmelod_qemu_${Example}_$PID.txt"
$p = Start-Process -FilePath $Qemu -ArgumentList @(
    '-machine', 'microbit',
    '-cpu', 'cortex-m0',
    '-kernel', $Elf,
    '--semihosting',
    '-display', 'none'
) -PassThru -NoNewWindow -RedirectStandardOutput $outFile

$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSec)
while (-not $p.HasExited -and [DateTime]::UtcNow -lt $deadline) {
    Start-Sleep -Milliseconds 200
    $partial = Get-Content $outFile -Raw -ErrorAction SilentlyContinue
    if ($partial -match ': PASS' -or $partial -match ': FAIL') {
        break
    }
}
$out = Get-Content $outFile -Raw -ErrorAction SilentlyContinue
if (-not $p.HasExited) {
    $p.Kill()
}
Write-Host $out
if ($out -notmatch ': PASS' -and $out -notmatch ': FAIL') {
    Write-Error "QEMU example '$Example' timed out without result"
    exit 1
}
if ($out -notmatch ': PASS') {
    exit 1
}
Remove-Item -Force $outFile -ErrorAction SilentlyContinue
