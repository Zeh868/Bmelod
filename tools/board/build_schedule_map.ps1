<#
.SYNOPSIS
schedule-map 二档：Keil/IAR post-build 一条命令出调度表（需宿主装 CMake + 任一 C 编译器）。

.DESCRIPTION
借宿主默认工具链单独配置+构建 cmake/schedule_map_host/ 这个迷你工程，跑
schedule_map_dump 出 <name>.{txt,json}，再跑 tools/schedule_map_tool.py 出
级 2 复合总表 schedule_map_all.txt。与 cmake/bm_schedule_map.cmake 的
CMAKE_CROSSCOMPILING 分支调起的是同一个宿主子构建模板，供构建系统不是 CMake
（Keil MDK / IAR EWARM 的 post-build 命令行）的工程使用。

.PARAMETER Generator
宿主子构建用的 CMake 生成器，默认 Ninja（单一配置产物目录，出表 exe 路径可预测；
多配置生成器如 Visual Studio 会在产物路径里插入 Debug/Release 子目录）。

.PARAMETER CCompiler
宿主 C 编译器绝对路径；留空则让 CMake 自动探测（PATH 上的默认编译器）。

.EXAMPLE
powershell -File tools\board\build_schedule_map.ps1 `
  -BmelodRoot D:\proj\framework\Bmelod -ConfigFile D:\proj\bm_config_app.h `
  -Reg D:\proj\schedule_reg.c -Sources D:\proj\Source\control\xxx_schedule.c `
  -IncludeDirs D:\proj\Source -OutDir D:\proj\build\schedule_map
#>
param(
    [Parameter(Mandatory)][string]$BmelodRoot,
    [string]$ConfigFile = "",
    [Parameter(Mandatory)][string]$Reg,
    [Parameter(Mandatory)][string[]]$Sources,
    [string[]]$IncludeDirs = @(),
    [Parameter(Mandatory)][string]$OutDir,
    [string]$Generator = "Ninja",
    [string]$CCompiler = ""
)
$ErrorActionPreference = "Stop"
$bin = Join-Path $OutDir "_host_build"
New-Item -ItemType Directory -Force $OutDir | Out-Null

$configureArgs = @(
    "-S", (Join-Path $BmelodRoot "cmake\schedule_map_host"),
    "-B", $bin,
    "-G", $Generator,
    "-DBM_SM_BMELOD_ROOT=$BmelodRoot",
    "-DBM_SM_CONFIG_FILE=$ConfigFile",
    "-DBM_SM_REG=$Reg",
    "-DBM_SM_SOURCES=$($Sources -join ';')",
    "-DBM_SM_INCLUDE_DIRS=$($IncludeDirs -join ';')"
)
if (-not [string]::IsNullOrWhiteSpace($CCompiler)) {
    $configureArgs += "-DCMAKE_C_COMPILER=$CCompiler"
}

Write-Host "=== schedule-map: 宿主子构建配置 ==="
cmake @configureArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "=== schedule-map: 宿主子构建 ==="
cmake --build $bin
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$dump = Get-ChildItem -Recurse $bin -Filter "schedule_map_dump*" |
    Where-Object { $_.Extension -in ".exe", "" } | Select-Object -First 1
if (-not $dump) {
    Write-Error "schedule-map: 未找到 schedule_map_dump 产物（$bin 下），宿主子构建可能没成功链接"
    exit 1
}

Write-Host "=== schedule-map: 出表 ==="
& $dump.FullName $OutDir
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "=== schedule-map: 级 2 复合分析 ==="
python (Join-Path $BmelodRoot "tools\schedule_map_tool.py") --dir $OutDir `
    --out (Join-Path $OutDir "schedule_map_all.txt")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Done. 输出目录: $OutDir"
exit 0
