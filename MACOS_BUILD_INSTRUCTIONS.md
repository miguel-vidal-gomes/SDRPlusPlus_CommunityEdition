# SDR++ Community Edition - macOS App Bundle Build Instructions

## Overview
This document provides step-by-step instructions for building SDR++ Community Edition as a proper macOS app bundle.

## Prerequisites

### Required Tools
- **Xcode Command Line Tools**: `xcode-select --install`
- **Homebrew**: [Install from brew.sh](https://brew.sh)
- **CMake**: `brew install cmake`

### Required Dependencies
Install all dependencies via Homebrew:
```bash
# Core dependencies
brew install fftw glfw volk zstd

# SDR Hardware support
brew install airspy airspyhf hackrf librtlsdr

# Audio support
brew install rtaudio

# Optional dependencies
brew install libusb
```

## Build Process

### Step 1: Clean Environment
```bash
# Navigate to project root
cd /path/to/SDRPlusPlus

# Remove any existing build directory
rm -rf build

# Create fresh build directory
mkdir build && cd build
```

### Step 2: CMake Configuration
```bash
cmake -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 .. \
  -DUSE_BUNDLE_DEFAULTS=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -DOPT_BUILD_PLUTOSDR_SOURCE=OFF
```

**Configuration Options Explained:**
- `CMAKE_OSX_DEPLOYMENT_TARGET=11.0`: Target macOS 11.0+ (**CHANGED FROM 10.15** - see note below)
- `USE_BUNDLE_DEFAULTS=ON`: **CRITICAL** - Enables proper macOS bundle behavior
- `CMAKE_BUILD_TYPE=Release`: Optimized release build
- `OPT_BUILD_PLUTOSDR_SOURCE=OFF`: Disable PlutoSDR (requires libiio)

**⚠️ IMPORTANT: macOS Deployment Target Change**
- **Previous versions** used `CMAKE_OSX_DEPLOYMENT_TARGET=10.15`
- **Current version** requires `CMAKE_OSX_DEPLOYMENT_TARGET=11.0` due to `std::filesystem` usage in core files
- **DO NOT** attempt to manually fix filesystem includes - use macOS 11.0 target instead

### Step 3: Build
```bash
# Build with parallel jobs for speed
make -j8
```

**Expected Output:**
- Build should complete with warnings only (no errors)
- All modules should compile successfully
- Core library `libsdrpp_core.dylib` should be created

### Step 4: Create App Bundle
```bash
# Return to project root
cd ..

# Create the app bundle using the official script
./make_macos_bundle.sh build ./SDR++CE.app
```

**What This Does:**
- Creates proper macOS app bundle structure
- Copies all binaries and modules
- Installs and fixes library dependencies
- Generates proper app icons from `sdrpp_ce.macos.png`
- Creates `Info.plist` with Community Edition branding
- Code signs the bundle

### Step 5: Launch and Test
```bash
# Launch the app bundle
open SDR++CE.app
```

## Verification Checklist

### ✅ App Launches Successfully
- App opens without errors
- Window displays properly
- All UI elements render correctly

### ✅ Community Edition Branding
- Title bar shows "SDR++ Community Edition v1.2.2-CE"
- About/Credits screen displays "SDR++ Community Edition"
- Startup logs show "SDR++ Community Edition v1.2.2-CE"

### ✅ Core Functionality
- **Source modules** load correctly (RTL-SDR, HackRF, etc.)
- **Audio sink** appears in Sinks section
- **Frequency tuning** works properly
- **Waterfall display** renders correctly

### ✅ New Features
- **MPX Analysis** visible in WFM demodulator (L+R, L-R, spectrum)
- **FFT Zoom persistence** - zoom level survives restarts
- **Scanner blacklisting** functionality available
- **Enhanced configuration** with persistent settings

## Troubleshooting

### Issue: CMake Configuration Fails
**Symptom:** Missing dependencies or CMake errors
**Solution:**
```bash
# Ensure all dependencies are installed
brew install fftw glfw volk zstd airspy airspyhf hackrf librtlsdr rtaudio

# Clean and retry
rm -rf build && mkdir build && cd build
```

### Issue: Build Fails with Compatibility Errors
**Symptom:** `cmake_minimum_required` version errors
**Solution:** These are warnings in third-party libraries, safe to ignore.

### Issue: std::filesystem Compilation Errors
**Symptom:** Errors like `'absolute' is unavailable: introduced in macOS 10.15` or `'exists' is unavailable: introduced in macOS 10.15`
**WRONG Solution:** ❌ Do NOT manually edit core files to change `#include <filesystem>` includes
**CORRECT Solution:** ✅ Use `CMAKE_OSX_DEPLOYMENT_TARGET=11.0` instead of 10.15

This error occurs because:
- Core SDR++ files now use `std::filesystem` 
- macOS 10.15 deployment target has compatibility issues with `std::filesystem`
- The correct fix is to target macOS 11.0+ which has full `std::filesystem` support

### Issue: Icon Generation Fails
**Symptom:** `sips` command errors during bundle creation
**Solution:** Verify `root/res/icons/sdrpp_ce.macos.png` exists and is valid PNG format.

### Issue: App Bundle Doesn't Launch
**Symptom:** App fails to open or crashes immediately
**Solution:**
```bash
# Check for missing resources
ls -la SDR++CE.app/Contents/Resources/

# Verify library dependencies
otool -L SDR++CE.app/Contents/MacOS/sdrpp_ce

# Check console logs
Console.app # Look for SDR++ related errors
```

### Issue: Wrong Config Location
**Symptom:** Settings don't persist or "Resource directory doesn't exist"
**Solution:** Ensure `USE_BUNDLE_DEFAULTS=ON` was used during CMake configuration.

## Advanced Options

### Debug Build
For development and debugging:
```bash
cmake -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 .. \
  -DUSE_BUNDLE_DEFAULTS=ON \
  -DCMAKE_BUILD_TYPE=Debug
```

### Custom Module Selection
Disable specific modules if needed:
```bash
cmake -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 .. \
  -DUSE_BUNDLE_DEFAULTS=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -DOPT_BUILD_DISCORD_INTEGRATION=OFF \
  -DOPT_BUILD_FREQUENCY_MANAGER=OFF
```

### Bundle Customization
The `make_macos_bundle.sh` script can be customized:
- **App name**: Modify `bundle_create_plist` call
- **Version**: Update version parameter
- **Icon**: Replace `sdrpp_ce.macos.png`

## Directory Structure

**Successful build produces:**
```
SDR++CE.app/
├── Contents/
│   ├── Info.plist
│   ├── MacOS/
│   │   └── sdrpp_ce
│   ├── Resources/
│   │   ├── sdrpp.icns
│   │   └── res/
│   │       ├── fonts/
│   │       ├── icons/
│   │       └── themes/
│   ├── Frameworks/
│   │   ├── libfftw3f.3.dylib
│   │   ├── libglfw.3.dylib
│   │   └── [other dependencies]
│   └── modules/
│       ├── audio_sink.dylib
│       ├── radio.dylib
│       └── [other modules]
```

## Performance Tips

### Build Speed
- Use `-j8` or `-j$(nproc)` for parallel compilation
- Consider `ccache` for incremental builds
- SSD storage significantly improves build times

### Bundle Size
- Release builds are significantly smaller than Debug
- Consider disabling unused modules
- Strip symbols if needed: `strip SDR++CE.app/Contents/MacOS/sdrpp_ce`

## Community Edition Specific Notes

### Config Location
- **Development builds**: `~/.config/sdrpp/config.json`
- **App bundles**: `~/Library/Application Support/sdrpp/config.json`

### New Features Integration
- MPX Analysis requires FFTW3 (automatically handled)
- FFT zoom persistence uses `defConfig["fftZoom"]` 
- Scanner enhancements are plugin-based

### Version Information
- App displays: "SDR++ Community Edition v1.2.2-CE"
- Build timestamp shown in About dialog
- Git hash embedded in debug builds

---

**🎉 Congratulations!** You now have a fully functional SDR++ Community Edition macOS app bundle ready for distribution or personal use.

For issues or contributions, visit: [SDRPlusPlus-CommunityEdition](https://github.com/miguel-vidal-gomes/SDRPlusPlus-CommunityEdition)
