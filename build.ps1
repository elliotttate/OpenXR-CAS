# Build script for OpenXR-CAS
# This script automatically finds and uses MSBuild to build the project

param(
    [string]$Configuration = "Release",
    [string]$Platform = "x64"
)

# Try to find MSBuild using multiple methods
$msbuildPath = $null

# Method 1: Try vswhere (most reliable)
$vswherePath = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswherePath) {
    Write-Host "Finding MSBuild using vswhere..." -ForegroundColor Green
    $vsPath = & $vswherePath -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
    if ($vsPath) {
        # Prefer 64-bit MSBuild for better performance
        $msbuildPath = Join-Path $vsPath "MSBuild\Current\Bin\amd64\MSBuild.exe"
        if (-not (Test-Path $msbuildPath)) {
            $msbuildPath = Join-Path $vsPath "MSBuild\Current\Bin\MSBuild.exe"
        }
    }
}

# Method 2: Check common paths if vswhere didn't work
if (-not $msbuildPath -or -not (Test-Path $msbuildPath)) {
    Write-Host "Checking common MSBuild locations..." -ForegroundColor Yellow
    $commonPaths = @(
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2019\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
    )
    
    foreach ($path in $commonPaths) {
        if (Test-Path $path) {
            $msbuildPath = $path
            break
        }
    }
}

if (-not $msbuildPath -or -not (Test-Path $msbuildPath)) {
    Write-Host "ERROR: Could not find MSBuild.exe" -ForegroundColor Red
    Write-Host "Please install Visual Studio 2022 or Build Tools for Visual Studio 2022" -ForegroundColor Red
    exit 1
}

Write-Host "Found MSBuild at: $msbuildPath" -ForegroundColor Green
Write-Host "Building OpenXR-CAS with Configuration=$Configuration, Platform=$Platform" -ForegroundColor Cyan

# Build the solution
$solutionPath = Join-Path $PSScriptRoot "XR_APILAYER_OPENXR_SHARPENER.sln"
if (-not (Test-Path $solutionPath)) {
    Write-Host "ERROR: Solution file not found at $solutionPath" -ForegroundColor Red
    exit 1
}

& $msbuildPath $solutionPath /p:Configuration=$Configuration /p:Platform=$Platform /m /v:minimal

if ($LASTEXITCODE -eq 0) {
    Write-Host "`nBuild completed successfully!" -ForegroundColor Green
    Write-Host "Output files are in: bin\$Platform\$Configuration\" -ForegroundColor Cyan
} else {
    Write-Host "`nBuild failed with exit code: $LASTEXITCODE" -ForegroundColor Red
    exit $LASTEXITCODE
}