<#
.SYNOPSIS
    Install SearchPlusPlus build dependencies on Windows.

.DESCRIPTION
    Installs CMake, Ninja, and Git via winget (preferred) or Chocolatey, then
    clones and bootstraps vcpkg under $env:VCPKG_ROOT (default:
    $env:USERPROFILE\vcpkg).

    Requires Visual Studio 2022 with the "Desktop development with C++"
    workload already installed (this script does NOT install MSVC because the
    download is large and the EULA must be accepted interactively). If Visual
    Studio isn't detected the script prints the download URL and exits.

.PARAMETER NoVcpkg
    Skip the vcpkg clone and bootstrap step.

.PARAMETER VcpkgRoot
    Clone vcpkg to a custom directory (default: $env:USERPROFILE\vcpkg).

.PARAMETER PackageManager
    Force a specific package manager: 'winget', 'choco', or 'auto' (default).

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\install.ps1
#>

[CmdletBinding()]
param(
    [switch]$NoVcpkg,
    [string]$VcpkgRoot = "",
    [ValidateSet('auto', 'winget', 'choco')]
    [string]$PackageManager = 'auto'
)

$ErrorActionPreference = 'Stop'

function Write-Info($msg)  { Write-Host "==> $msg" -ForegroundColor Cyan }
function Write-Warn2($msg) { Write-Host "==> $msg" -ForegroundColor Yellow }
function Die($msg)         { Write-Host "==> $msg" -ForegroundColor Red; exit 1 }

function Test-Command($name) {
    $null = Get-Command $name -ErrorAction SilentlyContinue
    return $?
}

function Resolve-PackageManager {
    param([string]$Requested)
    if ($Requested -eq 'winget') {
        if (-not (Test-Command winget)) { Die "winget not found on PATH" }
        return 'winget'
    }
    if ($Requested -eq 'choco') {
        if (-not (Test-Command choco)) { Die "choco not found on PATH" }
        return 'choco'
    }
    if (Test-Command winget) { return 'winget' }
    if (Test-Command choco)  { return 'choco' }
    Die "Neither winget nor choco is installed. Install one (winget ships with Windows 10 21H1+; choco: https://chocolatey.org/install)."
}

function Install-WithWinget {
    param([string[]]$Ids)
    foreach ($id in $Ids) {
        Write-Info "winget install --id $id"
        # --silent suppresses interactive prompts; --accept-* avoids EULA stalls.
        winget install --id $id -e --silent --accept-package-agreements --accept-source-agreements 2>&1 |
            Out-Host
        if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne -1978335189) {
            # -1978335189 = APPINSTALLER_CLI_ERROR_NO_APPLICABLE_UPGRADE (already installed)
            Write-Warn2 "winget exit code: $LASTEXITCODE for $id (may already be installed; continuing)"
        }
    }
}

function Install-WithChoco {
    param([string[]]$Pkgs)
    foreach ($pkg in $Pkgs) {
        Write-Info "choco install $pkg"
        choco install $pkg --no-progress -y --limit-output 2>&1 | Out-Host
    }
}

function Test-VisualStudioCpp {
    $vswhere = "$env:ProgramFiles(x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { return $false }
    $vsInstance = & $vswhere -latest -products '*' `
        -requires 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64' `
        -property installationPath 2>$null
    return -not [string]::IsNullOrEmpty($vsInstance)
}

function Initialize-Vcpkg {
    param([string]$Root)
    $gitDir = Join-Path $Root '.git'
    $vcpkgExe = Join-Path $Root 'vcpkg.exe'
    if ((Test-Path $gitDir) -and (Test-Path $vcpkgExe)) {
        Write-Info "vcpkg already present at $Root (skipping clone)"
        return
    }
    if (-not (Test-Path $Root)) {
        Write-Info "Cloning vcpkg into $Root"
        git clone --depth 1 https://github.com/microsoft/vcpkg.git $Root
    }
    Write-Info "Bootstrapping vcpkg"
    & (Join-Path $Root 'bootstrap-vcpkg.bat') -disableMetrics
    if ($LASTEXITCODE -ne 0) { Die "vcpkg bootstrap failed (exit $LASTEXITCODE)" }
}

# --- main ---

if (-not (Test-VisualStudioCpp)) {
    Write-Warn2 "Visual Studio 2022 with 'Desktop development with C++' was not detected."
    Write-Warn2 "Install it from https://visualstudio.microsoft.com/downloads/ (Community is free)"
    Write-Warn2 "and select the 'Desktop development with C++' workload, then re-run this script."
    Die "missing required Visual Studio C++ workload"
}
Write-Info "Visual Studio C++ workload detected"

$pm = Resolve-PackageManager -Requested $PackageManager
Write-Info "Using package manager: $pm"

switch ($pm) {
    'winget' { Install-WithWinget -Ids @('Kitware.CMake', 'Ninja-build.Ninja', 'Git.Git') }
    'choco'  { Install-WithChoco  -Pkgs @('cmake', 'ninja', 'git') }
}

# Ensure the freshly-installed tools are visible to this PowerShell session.
$env:Path = [System.Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' +
            [System.Environment]::GetEnvironmentVariable('Path', 'User')

if (-not $NoVcpkg) {
    if ([string]::IsNullOrEmpty($VcpkgRoot)) {
        $VcpkgRoot = if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { Join-Path $env:USERPROFILE 'vcpkg' }
    }
    Initialize-Vcpkg -Root $VcpkgRoot

    Write-Info "Done. Add the following to your PowerShell `$PROFILE`:"
    Write-Host "    `$env:VCPKG_ROOT = '$VcpkgRoot'"
    Write-Host "    `$env:CMAKE_TOOLCHAIN_FILE = '$VcpkgRoot\scripts\buildsystems\vcpkg.cmake'"
} else {
    Write-Info "Done (vcpkg skipped — set `$env:VCPKG_ROOT before running build.ps1)."
}
