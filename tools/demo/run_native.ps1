param([Parameter(Mandatory = $true)][string]$Example)

$ErrorActionPreference = 'Stop'
$DemoToolsDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
. (Join-Path $DemoToolsDir 'demo_paths.ps1')

if (-not (Test-KnownDemoExample $Example)) {
    Write-Error "Unknown example '$Example'"
    exit 1
}

Ensure-DemoUnifiedConfigure -Variant 'native' -NativeSim
$BuildRoot = Get-DemoVariantRoot -Variant 'native'
Ensure-DemoNativeFrameworkBuilt -BuildRoot $BuildRoot

$generator = (Select-String -Path (Join-Path $BuildRoot 'CMakeCache.txt') -Pattern '^CMAKE_GENERATOR:' | ForEach-Object { ($_ -split '=')[-1] })
if ($generator -match 'Visual Studio') {
    cmake --build $BuildRoot --target $Example --config Debug
} else {
    cmake --build $BuildRoot --target $Example
}
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$Exe = Get-DemoNativeExePath -Variant 'native' -Example $Example
$out = & $Exe 2>&1 | Out-String
Write-Host $out
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if ($out -notmatch ': PASS') { Write-Error 'Example did not report PASS'; exit 1 }
