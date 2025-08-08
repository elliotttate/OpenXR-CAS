# OpenXR Sharpener (D3D11 CAS + optional Levels)

OpenXR API layer that applies AMD FidelityFX CAS (Contrast Adaptive Sharpening) to any D3D11 OpenXR app at `xrEndFrame`, with an optional Levels post-process (black/white/gamma).

- Platform: Windows, x64
- Graphics: Direct3D 11 only
- Status: Minimal, production-oriented (no debug overlays)

## Features
- CAS sharpening with strength >= 0 (values > 1 run multiple passes)
- Optional Levels adjustment (in/out black/white and gamma)
- Minimal overhead, no per-frame allocations (texture pooling)
- Robust format handling (UNORM/SRGB/TYPELESS, R16G16B16A16_FLOAT)

## Install
1. Build Release x64 (see Build).
2. From `bin\x64\Release`, run:
   - `Install-Layer.ps1` (admin) to register the layer for all users
   - or `Install-Layer-User.ps1` (per-user) if present
3. To remove:
   - `Uninstall-Layer.ps1` (admin)
   - or `Uninstall-Layer-User.ps1`

This registers `openxr-api-layer.json` so the OpenXR loader will discover `XR_APILAYER_OPENXR_SHARPENER`.

## Configuration
The layer reads settings in this order (first wins):
1. Environment variables (where applicable)
2. `%LOCALAPPDATA%\XR_APILAYER_OPENXR_SHARPENER\config.cfg`
3. `config.cfg` in the DLL folder (`bin\x64\Release`)

A default config is auto-created under `%LOCALAPPDATA%\XR_APILAYER_OPENXR_SHARPENER\config.cfg` if missing.

Supported keys in `config.cfg`:
```
# Sharpen amount (>= 0). Values > 1.0 add extra CAS passes
sharpness=0.6

# Optional Levels pass (0/1)
levels_enable=0
# Input remap range
levels_in_black=0.0
levels_in_white=1.0
# Output range
levels_out_black=0.0
levels_out_white=1.0
# Gamma (>= 0.001)
levels_gamma=1.0
```

Environment variables (optional):
- `XR_CAS_SHARPNESS` (float) – overrides config `sharpness`

Note: Legacy debug overlay settings were removed in production.

## Usage
- Launch any D3D11 OpenXR app. The layer sharpens the color swapchain images at `xrEndFrame`.
- To disable Levels, set `levels_enable=0`.

## Build
- Open the solution `XR_APILAYER_OPENXR_SHARPENER.sln` in Visual Studio 2019/2022 and build `Release|x64`, or run:
  ```powershell
  & "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" .\XR_APILAYER_OPENXR_SHARPENER.sln /t:Build /p:Configuration=Release /p:Platform=x64 /m:2
  ```
- Outputs are in `bin\x64\Release`:
  - `XR_APILAYER_OPENXR_SHARPENER.dll`
  - `openxr-api-layer.json`
  - `shaders` folder (CAS.hlsl, Levels.hlsl, FidelityFX headers)
  - Install/Uninstall scripts

## Troubleshooting
- Layer not loading: ensure the manifest is registered and the app/runtime are 64-bit.
- Black output: use supported swapchain formats (R8G8B8A8/B8G8R8A8 UNORM/SRGB/TYPELESS, R16G16B16A16_FLOAT) and no MSAA.
- Crashes with high sharpness: reduce `sharpness` or ensure GPU drivers are up to date.

## License
- Code: MIT (see LICENSE)
- AMD FidelityFX CAS headers (`ffx_a.h`, `ffx_cas.h`) are © AMD; see their respective licenses.

## Credits
Based on the OpenXR Layer Template by Matthieu Bucchianeri with extensive modifications for CAS/Levels and D3D11 pipeline integration.
