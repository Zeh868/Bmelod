# 构建 ATK-DMG474 RTT 冒烟固件
param(
    [string]$BuildDir = "build_atk_dmg474_smoke",
    [string]$CubePath = ""
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

# 优先：D:\Code\Bmelod-sdks\stm32\STM32CubeG4；其次仓库内 .sdks；可用 -CubePath 覆盖
if (-not $CubePath) {
    $candidates = @(
        "D:\Code\Bmelod-sdks\stm32\STM32CubeG4",
        (Join-Path $Root ".sdks\STM32CubeG4")
    )
    foreach ($c in $candidates) {
        if (Test-Path (Join-Path $c "Drivers\CMSIS\Device\ST\STM32G4xx\Include\stm32g4xx.h")) {
            $CubePath = $c
            break
        }
    }
}

$Toolchain = Join-Path $Root "cmake\toolchain-arm-none-eabi-g4.cmake"
$Source = Join-Path $Root "board\atk_dmg474_smoke"

if (-not $CubePath -or -not (Test-Path (Join-Path $CubePath "Drivers\CMSIS\Device\ST\STM32G4xx\Include\stm32g4xx.h"))) {
    Write-Error @"
STM32CubeG4 not found.
  Expected: D:\Code\Bmelod-sdks\stm32\STM32CubeG4
  Or pass:  -CubePath <STM32CubeG4 root>
"@
}

Write-Host "Root     : $Root"
Write-Host "Cube     : $CubePath"
Write-Host "BuildDir : $BuildDir"

cmake -S $Source -B (Join-Path $Root $BuildDir) -G Ninja `
    "-DCMAKE_TOOLCHAIN_FILE=$Toolchain" `
    "-DBM_STM32_CUBE_PATH=$CubePath"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmake --build (Join-Path $Root $BuildDir)
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$Elf = Join-Path $Root "$BuildDir\atk_dmg474_smoke.elf"
Write-Host "OK: $Elf"
