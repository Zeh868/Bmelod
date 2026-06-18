# Shared Demo build output paths. Dot-source from tools/demo/*.ps1
# Unified CMake tree per variant: <repo>/build/demo/<variant>/
# Example ELF: <repo>/build/demo/<variant>/<example>/<example>.elf

$DemoToolsDir = $PSScriptRoot
$RootDir = (Resolve-Path (Join-Path $DemoToolsDir '../..')).Path
$DemoDir = Join-Path $RootDir 'Demo'
$DemoCMakeDir = Join-Path $DemoDir ''

if ($env:BMELOD_DEMO_BUILD_ROOT) {
    $BuildDemoRoot = $env:BMELOD_DEMO_BUILD_ROOT
} else {
    $BuildDemoRoot = Join-Path $RootDir 'build/demo'
}

$ExamplesList = Join-Path $DemoToolsDir 'examples.txt'
$ToolchainArmNoneEabi = (Resolve-Path (Join-Path $RootDir 'cmake/toolchain-arm-none-eabi.cmake')).Path.Replace('\', '/')

function Get-DemoVariantRoot {
    param([Parameter(Mandatory = $true)][string]$Variant)
    Join-Path $BuildDemoRoot $Variant
}

function Get-DemoExampleOutDir {
    param(
        [Parameter(Mandatory = $true)][string]$Variant,
        [Parameter(Mandatory = $true)][string]$Example
    )
    Join-Path (Get-DemoVariantRoot -Variant $Variant) $Example
}

function Get-DemoElfPath {
    param(
        [Parameter(Mandatory = $true)][string]$Variant,
        [Parameter(Mandatory = $true)][string]$Example
    )
    Join-Path (Get-DemoExampleOutDir -Variant $Variant -Example $Example) "$Example.elf"
}

function Get-DemoNativeExePath {
    param(
        [Parameter(Mandatory = $true)][string]$Variant,
        [Parameter(Mandatory = $true)][string]$Example
    )
    $outDir = Get-DemoExampleOutDir -Variant $Variant -Example $Example
    $debugExe = Join-Path $outDir "Debug\$Example.exe"
    if (Test-Path $debugExe) { return $debugExe }
    return Join-Path $outDir "$Example.exe"
}

# Back-compat alias: per-example output directory (not a separate CMake tree).
function Get-DemoBuildDir {
    param(
        [Parameter(Mandatory = $true)][string]$Variant,
        [Parameter(Mandatory = $true)][string]$Example
    )
    Get-DemoExampleOutDir -Variant $Variant -Example $Example
}

function Test-KnownDemoExample {
    param([Parameter(Mandatory = $true)][string]$Example)
    $known = Get-Content $ExamplesList | Where-Object { $_ -and -not $_.StartsWith('#') }
    ($Example -in $known) -and (Test-Path (Join-Path $DemoDir $Example))
}

function Test-DemoFrameworkLibsPresent {
    param(
        [Parameter(Mandatory = $true)][string]$BuildRoot
    )
    $libDir = Join-Path $BuildRoot 'bmelod'
    $required = @(
        'libbm_core.a',
        'libbm_module.a',
        'libbm_hrt.a',
        'libbm_ticker.a',
        'libbm_exec.a',
        'libbm_algorithm.a',
        'libbm_hal.a'
    )
    foreach ($name in $required) {
        if (-not (Test-Path (Join-Path $libDir $name))) {
            return $false
        }
    }
    $nativePack = Join-Path $libDir '_bm_pack_native_sim/libbm_backend_native_sim.a'
    if (-not (Test-Path $nativePack)) {
        return $false
    }
    return $true
}

function Ensure-DemoNativeFrameworkBuilt {
    param(
        [Parameter(Mandatory = $true)][string]$BuildRoot
    )
    if (Test-DemoFrameworkLibsPresent -BuildRoot $BuildRoot) {
        return
    }
    $targets = @(
        'bm_core', 'bm_module', 'bm_hrt', 'bm_ticker', 'bm_exec',
        'bm_algorithm', 'bm_hal', 'bm_backend_native_sim', 'bm_port_arch_host'
    )
    & cmake --build $BuildRoot --target $targets
    if ($LASTEXITCODE -ne 0) {
        throw 'CMake build failed for Demo framework libraries'
    }
}

function Test-DemoCacheNeedsReconfigure {
    param(
        [Parameter(Mandatory = $true)][string]$BuildRoot,
        [Parameter(Mandatory = $false)][switch]$RequireNativeSim
    )
    $cache = Join-Path $BuildRoot 'CMakeCache.txt'
    if (-not (Test-Path $cache)) {
        return $false
    }
    $gen = (Select-String -Path $cache -Pattern '^CMAKE_GENERATOR:' |
        ForEach-Object { ($_ -split '=')[-1] })
    $makeProgram = (Select-String -Path $cache -Pattern '^CMAKE_MAKE_PROGRAM:[^=]+=' |
        ForEach-Object { ($_ -split '=')[-1] })
    if ($gen -notmatch 'MinGW Makefiles|Ninja|Unix Makefiles') {
        return $true
    }
    if ($gen -match 'MinGW Makefiles') {
        if (-not $makeProgram -or -not (Test-Path $makeProgram)) {
            return $true
        }
    }
    if ($gen -match 'Ninja') {
        if (-not (Test-Path (Join-Path $BuildRoot 'build.ninja'))) {
            return $true
        }
    }
    if ($gen -match 'MinGW Makefiles|Unix Makefiles') {
        if (-not (Test-Path (Join-Path $BuildRoot 'Makefile'))) {
            return $true
        }
    }
    if ($RequireNativeSim) {
        $nativeSim = (Select-String -Path $cache -Pattern '^BMELOD_DEMO_NATIVE:BOOL=' |
            ForEach-Object { ($_ -split '=', 2)[1] })
        if ($nativeSim -ne 'ON') {
            return $true
        }
        if (-not (Test-DemoFrameworkLibsPresent -BuildRoot $BuildRoot)) {
            return $true
        }
    }
    return $false
}

function Ensure-DemoQemuFrameworkBuilt {
    param(
        [Parameter(Mandatory = $true)][string]$BuildRoot
    )
    $libDir = Join-Path $BuildRoot 'bmelod'
    $required = @(
        'libbm_core.a',
        'libbm_module.a',
        'libbm_hrt.a',
        'libbm_ticker.a',
        'libbm_exec.a',
        'libbm_algorithm.a',
        'libbm_hal.a'
    )
    foreach ($name in $required) {
        if (-not (Test-Path (Join-Path $libDir $name))) {
            $targets = @(
                'bm_backend_qemu_cortex_m0', 'bm_core', 'bm_module', 'bm_hrt',
                'bm_ticker', 'bm_exec', 'bm_algorithm', 'bm_hal',
                'bm_port_arch_armv6m'
            )
            & cmake --build $BuildRoot --target $targets
            if ($LASTEXITCODE -ne 0) {
                throw 'CMake build failed for Demo QEMU framework libraries'
            }
            return
        }
    }
}

function Ensure-DemoUnifiedConfigure {
    param(
        [Parameter(Mandatory = $true)][string]$Variant,
        [Parameter(Mandatory = $false)][switch]$NativeSim
    )
    $buildRoot = Get-DemoVariantRoot -Variant $Variant
    $cache = Join-Path $buildRoot 'CMakeCache.txt'
    $needsReconfigure = Test-DemoCacheNeedsReconfigure -BuildRoot $buildRoot -RequireNativeSim:$NativeSim
    if (-not $needsReconfigure -and -not $NativeSim -and (Test-Path $cache)) {
        $gen = (Select-String -Path $cache -Pattern '^CMAKE_GENERATOR:' |
            ForEach-Object { ($_ -split '=')[-1] })
        if ($gen -notmatch 'MinGW Makefiles|Ninja|Unix Makefiles') {
            $needsReconfigure = $true
        }
    }
    if ($needsReconfigure) {
        Remove-Item -Recurse -Force $buildRoot
    }
    if (Test-Path $cache) {
        return
    }
    if ($NativeSim) {
        $cmakeArgs = @(
            '-S', $DemoDir,
            '-B', $buildRoot,
            '-DBMELOD_DEMO_NATIVE=ON'
        )
        if (Get-Command ninja -ErrorAction SilentlyContinue) {
            $cmakeArgs = @('-G', 'Ninja') + $cmakeArgs
        } else {
            $make = Get-Command mingw32-make -ErrorAction SilentlyContinue
            if ($make) {
                $cmakeArgs += "-DCMAKE_MAKE_PROGRAM=$($make.Source)"
            }
            $cmakeArgs = @('-G', 'MinGW Makefiles') + $cmakeArgs
        }
        & cmake @cmakeArgs
    } else {
        $cmakeArgs = @(
            '-S', $DemoDir,
            '-B', $buildRoot,
            "-DCMAKE_TOOLCHAIN_FILE=$ToolchainArmNoneEabi"
        )
        if (Get-Command ninja -ErrorAction SilentlyContinue) {
            $cmakeArgs = @('-G', 'Ninja') + $cmakeArgs
        } else {
            $make = Get-Command mingw32-make -ErrorAction SilentlyContinue
            if ($make) {
                $cmakeArgs += "-DCMAKE_MAKE_PROGRAM=$($make.Source)"
            }
            $cmakeArgs = @('-G', 'MinGW Makefiles') + $cmakeArgs
        }
        & cmake @cmakeArgs
    }
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed for Demo umbrella' }
}
