<#
.SYNOPSIS
    Configure, build, and test SearchPlusPlus on Windows.

.DESCRIPTION
    Assumes scripts/install.ps1 has been run (CMake / Ninja / Git / vcpkg
    installed). Must be invoked from a "Developer PowerShell for VS 2022"
    prompt (or any shell where `cl.exe` is on PATH) — this script does not
    auto-enter the MSVC environment because doing so reliably from a script
    requires probing vswhere + Enter-VsDevShell, which is fragile across VS
    versions. The error message below tells the user what to do if cl.exe
    isn't found.

.PARAMETER Preset
    CMake preset to use. Default: 'default-windows' (Debug, no sanitizers).
    Other options: 'release'.

.PARAMETER NoTest
    Configure + build only; skip ctest.

.PARAMETER Jobs
    Build parallelism (forwarded to cmake --build -j).

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\build.ps1
    powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Preset release
#>

[CmdletBinding()]
param(
    [string]$Preset = 'default-windows',
    [switch]$NoTest,
    [int]$Jobs = 0
)

$ErrorActionPreference = 'Stop'

function Write-Info($msg) { Write-Host "==> $msg" -ForegroundColor Cyan }
function Die($msg)        { Write-Host "==> $msg" -ForegroundColor Red; exit 1 }

function Test-Command($name) {
    $null = Get-Command $name -ErrorAction SilentlyContinue
    return $?
}

if (-not (Test-Command cl)) {
    Die @"
cl.exe (MSVC) is not on PATH. Open 'Developer PowerShell for VS 2022' from the
Start Menu, then cd to this repo and re-run scripts\build.ps1. Alternatively,
install/repair Visual Studio with the 'Desktop development with C++' workload.
"@
}

function Resolve-Toolchain {
    if ($env:CMAKE_TOOLCHAIN_FILE) { return $env:CMAKE_TOOLCHAIN_FILE }
    $root = if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT }
            elseif ($env:VCPKG_INSTALLATION_ROOT) { $env:VCPKG_INSTALLATION_ROOT }
            elseif (Test-Path (Join-Path $env:USERPROFILE 'vcpkg\scripts\buildsystems\vcpkg.cmake')) {
                Join-Path $env:USERPROFILE 'vcpkg'
            } else { '' }
    if ([string]::IsNullOrEmpty($root)) { return '' }
    $tc = Join-Path $root 'scripts\buildsystems\vcpkg.cmake'
    if (-not (Test-Path $tc)) {
        Die "VCPKG_ROOT=$root but $tc does not exist — run scripts\install.ps1"
    }
    return $tc
}

$toolchain = Resolve-Toolchain
if ([string]::IsNullOrEmpty($toolchain)) {
    Write-Info "No vcpkg toolchain found; relying on system-installed dependencies"
} else {
    Write-Info "Using vcpkg toolchain: $toolchain"
}
Write-Info "Preset: $Preset"

Write-Info "Configure"
if ([string]::IsNullOrEmpty($toolchain)) {
    cmake --preset $Preset
} else {
    cmake --preset $Preset "-DCMAKE_TOOLCHAIN_FILE=$toolchain"
}
if ($LASTEXITCODE -ne 0) { Die "configure failed (exit $LASTEXITCODE)" }

Write-Info "Build"
if ($Jobs -gt 0) {
    cmake --build --preset $Preset -j $Jobs
} else {
    cmake --build --preset $Preset -j
}
if ($LASTEXITCODE -ne 0) { Die "build failed (exit $LASTEXITCODE)" }

if (-not $NoTest) {
    Write-Info "Test"
    ctest --preset $Preset --output-on-failure
    if ($LASTEXITCODE -ne 0) { Die "tests failed (exit $LASTEXITCODE)" }
}

Write-Info "Done."
