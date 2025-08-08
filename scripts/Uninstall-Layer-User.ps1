$JsonPath = Join-Path "$PSScriptRoot" "openxr-api-layer.json"
if (Test-Path "HKCU:\Software\Khronos\OpenXR\1\ApiLayers\Implicit") {
    Remove-ItemProperty -Path HKCU:\Software\Khronos\OpenXR\1\ApiLayers\Implicit -Name $JsonPath -Force -ErrorAction SilentlyContinue | Out-Null
}
Write-Host "Uninstalled implicit layer for current user: $JsonPath"
