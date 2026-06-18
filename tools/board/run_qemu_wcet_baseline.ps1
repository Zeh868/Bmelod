param(
    [Parameter(Mandatory = $true)]
    [string]$Label,
    [string]$Demo = "stream_array_mvdr",
    [string]$BuildDir = "build/qemu_wcet",
    [string]$Config = "Debug",
    [string]$OutputDir = "board/reports"
)

$ErrorActionPreference = 'Stop'
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$DemoDir = Join-Path $RepoRoot "Demo\$Demo"
$BuildPath = Join-Path $RepoRoot $BuildDir
$ReportDir = Join-Path $RepoRoot $OutputDir
$MeasureScript = Join-Path $RepoRoot 'tools\measure_wcet.py'

if (-not (Test-Path $DemoDir)) {
    Write-Error "Demo not found: $DemoDir"
    exit 1
}
if (-not (Test-Path $MeasureScript)) {
    Write-Error "measure_wcet.py not found: $MeasureScript"
    exit 1
}

New-Item -ItemType Directory -Force -Path $ReportDir | Out-Null

Write-Host "=== Configure $Demo (native_sim) ==="
cmake -B $BuildPath -S $DemoDir `
    -DBM_BACKEND=native_sim `
    -DBM_ENABLE_ALGORITHM=ON `
    -DBM_BUILD_TESTS=OFF
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "=== Build ($Config) ==="
cmake --build $BuildPath --config $Config
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$ExeName = $Demo
$ExePath = Join-Path $BuildPath "$ExeName.exe"
if (-not (Test-Path $ExePath)) {
    $ExePath = Join-Path $BuildPath $ExeName
}
if (-not (Test-Path $ExePath)) {
  $ExePath = Join-Path $BuildPath "Debug\$ExeName.exe"
}
if (-not (Test-Path $ExePath)) {
    Write-Error "Executable not found under $BuildPath"
    exit 1
}

$SafeLabel = ($Label -replace '[^\w\-]', '_')
$ReportPath = Join-Path $ReportDir "qemu_${SafeLabel}.json"

Write-Host "=== QEMU WCET baseline: $Label ==="
python $MeasureScript --label $Label --output $ReportPath -- $ExePath
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Report written: $ReportPath"
