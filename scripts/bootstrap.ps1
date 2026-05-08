# bootstrap.ps1 — one-command provisioner for Aurora Mail on Windows.
#
# Bootstraps a local vcpkg checkout under vendor/vcpkg, installs project
# dependencies via the manifest in vcpkg.json, then configures and builds the
# project through the CMake `windows-<release|debug>` preset.
#
# The script is idempotent: re-running it reuses the existing vcpkg clone and
# only re-installs dependencies if vcpkg.json has changed.
#
# Prerequisites that the script does NOT install (Microsoft tooling):
#   - Visual Studio 2022 with the "Desktop development with C++" workload
#   - CMake 3.22+ (bundled with current Visual Studio installs)
#   - Git
#
# Usage:
#   .\scripts\bootstrap.ps1                     # release build (default)
#   .\scripts\bootstrap.ps1 -BuildType debug    # debug build with unit tests
#   .\scripts\bootstrap.ps1 -SkipDeps           # skip vcpkg install step
#   .\scripts\bootstrap.ps1 -SkipBuild          # only install dependencies

[CmdletBinding()]
param(
    [ValidateSet('debug', 'release', 'Debug', 'Release')]
    [string]$BuildType = 'release',
    [switch]$SkipDeps,
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$BuildType = $BuildType.ToLower()

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
Set-Location $RepoRoot

function Write-Log     { param([string]$m) Write-Host "[bootstrap] $m" -ForegroundColor Cyan }
function Write-WarnLog { param([string]$m) Write-Host "[bootstrap] $m" -ForegroundColor Yellow }
function Write-DieLog  { param([string]$m) Write-Host "[bootstrap] $m" -ForegroundColor Red; exit 1 }

# ---------------------------------------------------------------------------
# Tool checks
# ---------------------------------------------------------------------------
foreach ($tool in @('git', 'cmake')) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        Write-DieLog "$tool not found in PATH. Install Visual Studio 2022 (with C++ workload) and Git for Windows."
    }
}

$Preset = "windows-$BuildType"
Write-Log "Host:    Windows ($([System.Environment]::OSVersion.Version))"
Write-Log "Preset:  $Preset"
Write-Log "Repo:    $RepoRoot"

# ---------------------------------------------------------------------------
# vcpkg bootstrap
# ---------------------------------------------------------------------------
$VcpkgDir = Join-Path $RepoRoot 'vendor\vcpkg'
$VcpkgExe = Join-Path $VcpkgDir 'vcpkg.exe'

if (-not $SkipDeps) {
    if (-not (Test-Path $VcpkgDir)) {
        Write-Log "Cloning microsoft/vcpkg into $VcpkgDir"
        New-Item -ItemType Directory -Path (Split-Path $VcpkgDir) -Force | Out-Null
        git clone --depth 1 https://github.com/microsoft/vcpkg.git $VcpkgDir
    } else {
        Write-Log "Reusing existing vcpkg checkout at $VcpkgDir"
        Push-Location $VcpkgDir
        try { git pull --ff-only 2>$null | Out-Null } catch { }
        Pop-Location
    }

    if (-not (Test-Path $VcpkgExe)) {
        Write-Log 'Bootstrapping vcpkg (running bootstrap-vcpkg.bat)'
        & "$VcpkgDir\bootstrap-vcpkg.bat" -disableMetrics
        if ($LASTEXITCODE -ne 0) { Write-DieLog 'bootstrap-vcpkg.bat failed' }
    }

    $env:VCPKG_ROOT = $VcpkgDir
    Write-Log "VCPKG_ROOT=$env:VCPKG_ROOT"

    Write-Log 'Installing vcpkg.json dependencies for triplet x64-windows'
    & $VcpkgExe install --triplet x64-windows --x-feature=tests
    if ($LASTEXITCODE -ne 0) { Write-DieLog 'vcpkg install failed' }
} else {
    Write-Log '-SkipDeps set, skipping vcpkg install'
    $env:VCPKG_ROOT = $VcpkgDir
}

if ($SkipBuild) {
    Write-Log '-SkipBuild set, dependency installation complete — exiting before configure'
    return
}

# ---------------------------------------------------------------------------
# Configure / build / test
# ---------------------------------------------------------------------------
Write-Log "Configuring with preset '$Preset'"
cmake --preset $Preset
if ($LASTEXITCODE -ne 0) { Write-DieLog 'cmake configure failed' }

Write-Log "Building with preset '$Preset'"
cmake --build --preset $Preset --parallel
if ($LASTEXITCODE -ne 0) { Write-DieLog 'cmake build failed' }

if ($BuildType -eq 'debug') {
    Write-Log "Running tests with preset '$Preset'"
    ctest --preset $Preset
    if ($LASTEXITCODE -ne 0) { Write-WarnLog 'Some tests failed (see output above)' }
}

Write-Log "Done. Artifact: build\$Preset\desktop\$BuildType\aurora-mail.exe"
