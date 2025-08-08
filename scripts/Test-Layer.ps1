param(
    [switch]$X86
)

$ErrorActionPreference = 'Stop'

# Determine registry hive and manifest name
if ($X86) {
    $RegPath = 'HKLM:\Software\WOW6432Node\Khronos\OpenXR\1\ApiLayers\Implicit'
    $JsonName = 'openxr-api-layer-32.json'
} else {
    $RegPath = 'HKLM:\Software\Khronos\OpenXR\1\ApiLayers\Implicit'
    $JsonName = 'openxr-api-layer.json'
}

$ManifestPath = Join-Path $PSScriptRoot $JsonName
$DllName = ($X86) ? 'XR_APILAYER_name-32.dll' : 'XR_APILAYER_name.dll'
$DllPath = Join-Path $PSScriptRoot $DllName

Write-Host "Checking manifest presence: $ManifestPath"
if (-not (Test-Path $ManifestPath)) { throw "Manifest not found: $ManifestPath" }

# Read manifest and resolve actual DLL name after sed replacement in post-build
$manifest = Get-Content $ManifestPath | ConvertFrom-Json
$dllFromManifest = $manifest.api_layer.library_path
Write-Host "Manifest library_path: $dllFromManifest"

# The DLL should be next to the manifest in the output folder
$OutDir = Split-Path -Parent $ManifestPath

# Validate registry registration
Write-Host "Checking registry: $RegPath"
$reg = Get-Item -Path $RegPath -ErrorAction SilentlyContinue
if (-not $reg) { throw "Registry path missing: $RegPath" }
$props = (Get-ItemProperty -Path $RegPath)
$found = $false
foreach ($p in $props.PSObject.Properties) {
    if ($p.Name -ieq $ManifestPath -and $p.Value -eq 0) { $found = $true; break }
}
if (-not $found) { throw "Registry does not reference manifest as implicit layer: $ManifestPath" }
Write-Host "Registry entry OK"

# Check DLL presence (post-build outputs)
$dllOnDisk = Join-Path $OutDir (Split-Path -Leaf $dllFromManifest)
Write-Host "Checking DLL on disk: $dllOnDisk"
if (-not (Test-Path $dllOnDisk)) { throw "Layer DLL not found: $dllOnDisk" }

# Log file location
$LocalAppData = Join-Path $env:LOCALAPPDATA $($manifest.api_layer.name)
$LogPath = Join-Path $LocalAppData ("{0}.log" -f $manifest.api_layer.name)
Write-Host "Expected log path: $LogPath"
if (Test-Path $LogPath) {
    Write-Host "Recent log entries:"
    Get-Content $LogPath -Tail 50
} else {
    Write-Host "Log not found yet. It will appear after the loader loads the layer."
}

Write-Host "Basic checks passed. To validate runtime activation:"
Write-Host "1) Run: OpenXR Developer Tools -> Demo Scene (or an OpenXR app)."
Write-Host "2) Set XR_CAS_DEBUG_FRAMES=600 and XR_CAS_SHARPNESS=1 to make the effect obvious."
Write-Host "3) Watch the log update and verify 'CAS: processing view' messages."
