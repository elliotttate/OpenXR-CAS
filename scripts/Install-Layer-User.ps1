$RegistryPath = "HKCU:\Software\Khronos\OpenXR\1\ApiLayers\Implicit"
$JsonPath = Join-Path "$PSScriptRoot" "openxr-api-layer.json"

if (-not (Test-Path $RegistryPath)) {
    New-Item -Path $RegistryPath -Force | Out-Null
}
New-ItemProperty -Path $RegistryPath -Name $JsonPath -PropertyType DWord -Value 0 -Force | Out-Null
Write-Host "Installed implicit layer for current user: $JsonPath"
