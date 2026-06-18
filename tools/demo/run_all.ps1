$ErrorActionPreference = 'Stop'
$DemoToolsDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
. (Join-Path $DemoToolsDir 'demo_paths.ps1')

$BuildRoot = Get-DemoVariantRoot -Variant 'windows'
$Examples = Get-Content $ExamplesList |
    Where-Object { $_ -and -not $_.StartsWith('#') }
$Failed = @()

Ensure-DemoUnifiedConfigure -Variant 'windows'

foreach ($Example in $Examples) {
    Write-Host "=== Building $Example ==="
    & cmake --build $BuildRoot --target "$Example.elf"
    if ($LASTEXITCODE -ne 0) { throw "Build failed for $Example" }

    Write-Host "=== Running $Example in QEMU ==="
    $Elf = Get-DemoElfPath -Variant 'windows' -Example $Example
    $Stdout = Join-Path (Get-DemoExampleOutDir -Variant 'windows' -Example $Example) 'qemu.stdout'
    $Stderr = Join-Path (Get-DemoExampleOutDir -Variant 'windows' -Example $Example) 'qemu.stderr'
    Remove-Item $Stdout, $Stderr -ErrorAction SilentlyContinue

    $Arguments = "-machine microbit -cpu cortex-m0 -kernel `"$Elf`" " +
        '--semihosting -display none'
    $Process = Start-Process qemu-system-arm -ArgumentList $Arguments `
        -PassThru -WindowStyle Hidden `
        -RedirectStandardOutput $Stdout -RedirectStandardError $Stderr
    Start-Sleep -Seconds 8
    if (-not $Process.HasExited) {
        Stop-Process -Id $Process.Id -Force
    }

    $Output = (Get-Content $Stdout, $Stderr -ErrorAction SilentlyContinue) -join "`n"
    ($Output -split "`n" | Select-Object -First 30) | ForEach-Object {
        Write-Host $_
    }

    if ($Output -match 'EXAMPLE_.*: PASS') {
        Write-Host "$Example ... PASS"
    } else {
        Write-Host "$Example ... FAIL"
        $Failed += $Example
    }
}

if ($Failed.Count -gt 0) {
    Write-Error "Failed examples: $($Failed -join ', ')"
    exit 1
}

Write-Host 'All examples passed.'
