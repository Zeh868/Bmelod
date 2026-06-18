param([int]$TimeoutSec = 15)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Elf = Join-Path $Root 'build_qemu\qemu_hybrid_smoke_cm0.elf'
$Qemu = (Get-Command qemu-system-arm -ErrorAction SilentlyContinue).Source

if (-not (Test-Path $Elf)) {
    Write-Error "Build firmware first: cmake -B build_qemu -S tests/qemu -G MinGW Makefiles -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-none-eabi.cmake && cmake --build build_qemu --target qemu_hybrid_smoke_cm0.elf"
}
if (-not $Qemu) {
    Write-Error 'qemu-system-arm not found in PATH'
}

$outFile = Join-Path $env:TEMP 'bmelod_hybrid_smoke_out.txt'
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
    if ($partial -match 'ok 1 - hybrid_hrt_smoke' -or $partial -match 'not ok 1') {
        break
    }
}
if (-not $p.HasExited) {
    $p.Kill()
}

$out = Get-Content $outFile -Raw
Write-Host $out
if ($out -notmatch '(?m)^ok 1 - hybrid_hrt_smoke') {
    exit 1
}
