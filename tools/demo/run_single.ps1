param([Parameter(Mandatory = $true)][string]$Example)

$ErrorActionPreference = 'Stop'
$DemoToolsDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
. (Join-Path $DemoToolsDir 'demo_paths.ps1')

if (-not (Test-KnownDemoExample $Example)) {
    Write-Error "Unknown example '$Example'"
    exit 1
}

Ensure-DemoUnifiedConfigure -Variant 'windows'
$BuildRoot = Get-DemoVariantRoot -Variant 'windows'

Write-Host "=== Building $Example ==="
cmake --build $BuildRoot --target "$Example.elf"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "=== Running $Example in QEMU ==="
Write-Host 'Press Ctrl+C to stop.'
$Elf = Get-DemoElfPath -Variant 'windows' -Example $Example
& qemu-system-arm -machine microbit -cpu cortex-m0 -kernel $Elf `
    --semihosting -display none
