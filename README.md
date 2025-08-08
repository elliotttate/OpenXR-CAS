# OpenXR Sharpener (D3D11 CAS + optional Levels)

OpenXR API layer that applies AMD FidelityFX CAS (Contrast Adaptive Sharpening) to any D3D11 OpenXR app at `xrEndFrame`, with an optional Levels post-process (black/white/gamma).

## What is this?

This is a post-processing layer for OpenXR VR applications that enhances image quality by applying sharpening and optional color adjustments. It works with any OpenXR application using Direct3D 11 rendering, making VR content appear sharper and more detailed without modifying the original application.

- **Platform**: Windows 10/11, x64
- **Graphics**: Direct3D 11 only
- **Status**: Production-ready, minimal overhead
- **Compatible with**: Any OpenXR runtime (SteamVR, Oculus, Windows Mixed Reality, etc.)

## Features
- CAS sharpening with strength >= 0 (values > 1 run multiple passes)
- Optional Levels adjustment (in/out black/white and gamma)
- Minimal overhead, no per-frame allocations (texture pooling)
- Robust format handling (UNORM/SRGB/TYPELESS, R16G16B16A16_FLOAT)

## Quick Start

### Download Pre-built Release
1. Download the latest release from the [Releases](https://github.com/elliotttate/OpenXR-CAS/releases) page
2. Extract the ZIP file to a folder of your choice
3. Right-click `Install-Layer.ps1` and select "Run with PowerShell" (requires admin rights)
   - This registers the layer system-wide for all OpenXR applications
4. Launch any OpenXR VR application - the sharpening will be applied automatically

### Build from Source
1. Prerequisites:
   - Visual Studio 2019 or 2022 with C++ development tools
   - Windows SDK
   - Git (to clone the repository)
2. Clone the repository:
   ```bash
   git clone https://github.com/elliotttate/OpenXR-CAS.git
   cd OpenXR-CAS
   ```
3. Build using Visual Studio or command line:
   ```powershell
   & "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" .\XR_APILAYER_OPENXR_SHARPENER.sln /t:Build /p:Configuration=Release /p:Platform=x64
   ```
4. The built files will be in `bin\x64\Release`
5. Run `Install-Layer.ps1` from the output directory to install

### Uninstall
To remove the layer, run `Uninstall-Layer.ps1` from the installation folder with admin rights.

## Configuration

### How to Configure

The layer automatically creates a configuration file at:
`%LOCALAPPDATA%\XR_APILAYER_OPENXR_SHARPENER\config.cfg`

You can edit this file with any text editor (e.g., Notepad) to customize the sharpening and color adjustment settings.

### Configuration Options

**Sharpening Settings:**
```ini
# Sharpness strength (0.0 = off, 0.6 = default, 1.0 = maximum single pass)
# Values > 1.0 apply multiple sharpening passes for extreme sharpening
sharpness=0.6
```

**Color Adjustment Settings (Levels):**
```ini
# Enable/disable color levels adjustment
levels_enable=0  # 0 = disabled, 1 = enabled

# Input black and white points (values from 0.0 to 1.0)
# Adjusts which input values map to black and white
levels_in_black=0.0   # Values below this become black
levels_in_white=1.0   # Values above this become white

# Output black and white points (values from 0.0 to 1.0)
# Remaps the output range
levels_out_black=0.0  # Black level in output
levels_out_white=1.0  # White level in output

# Gamma correction (> 0.001)
# 1.0 = no change, < 1.0 = darker, > 1.0 = brighter
levels_gamma=1.0
```

### Recommended Settings

**For general VR gaming:**
```ini
sharpness=0.6
levels_enable=0
```

**For slightly washed-out content:**
```ini
sharpness=0.7
levels_enable=1
levels_in_black=0.05
levels_in_white=0.95
levels_gamma=1.1
```

**For maximum sharpness (may introduce artifacts):**
```ini
sharpness=1.5
levels_enable=0
```

### Advanced Configuration

The configuration is read in this priority order:
1. Environment variables (e.g., `XR_CAS_SHARPNESS=0.8`)
2. User config: `%LOCALAPPDATA%\XR_APILAYER_OPENXR_SHARPENER\config.cfg`
3. Installation folder config: `config.cfg` in the DLL directory

## Usage

Once installed, the layer works automatically with any OpenXR application:

1. **No application modification needed** - The layer intercepts OpenXR calls transparently
2. **Works with all OpenXR runtimes** - Compatible with SteamVR, Oculus, WMR, Varjo, etc.
3. **Real-time adjustments** - Edit the config file while your VR app is running; changes apply on next frame
4. **Per-application settings** - You can create different configs and swap them for different games

### Testing Your Configuration

1. Start with default settings (`sharpness=0.6`)
2. Launch your VR application
3. If the image looks over-sharpened, reduce to `0.4` or `0.5`
4. If you want more sharpness, try `0.8` or `0.9`
5. Values above `1.0` apply multiple passes - use carefully as this can introduce artifacts

### Performance Impact

- **Minimal overhead**: Typically < 0.5ms per frame on modern GPUs
- **No CPU overhead**: All processing happens on GPU
- **Memory efficient**: Uses texture pooling to avoid allocations

## Troubleshooting

### Common Issues and Solutions

**Layer not loading:**
- Ensure the manifest is registered (run `Install-Layer.ps1` as admin)
- Verify both the app and runtime are 64-bit
- Check Windows Event Viewer for OpenXR errors
- Try reinstalling with admin privileges

**Black or corrupted output:**
- The layer requires specific swapchain formats:
  - Supported: R8G8B8A8/B8G8R8A8 (UNORM/SRGB/TYPELESS), R16G16B16A16_FLOAT
  - Not supported: MSAA swapchains, compressed formats
- Try disabling other OpenXR layers that might conflict

**Performance issues:**
- Reduce sharpness value (lower values = less processing)
- Disable levels adjustment if not needed (`levels_enable=0`)
- Update GPU drivers to latest version

**Application crashes with high sharpness:**
- Values above 1.0 use multiple passes and may cause issues
- Start with `sharpness=0.6` and increase gradually
- Some older GPUs may not handle extreme values well

### Verifying Installation

To check if the layer is properly installed:
1. Open PowerShell as admin
2. Run: `reg query "HKLM\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit"`
3. You should see an entry for `XR_APILAYER_OPENXR_SHARPENER`

### Getting Help

If you encounter issues:
1. Check the [Issues](https://github.com/elliotttate/OpenXR-CAS/issues) page for known problems
2. Ensure you're using the latest release
3. Try with a simple OpenXR application first to isolate the problem

## License
- Code: MIT (see LICENSE)
- AMD FidelityFX CAS headers (`ffx_a.h`, `ffx_cas.h`) are © AMD; see their respective licenses.

## Credits
Based on the OpenXR Layer Template by Matthieu Bucchianeri with extensive modifications for CAS/Levels and D3D11 pipeline integration.
