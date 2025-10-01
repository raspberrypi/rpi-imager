# Building Qt for macOS

This document explains how to build Qt from source with minimal configuration for macOS using the `build-qt-macos.sh` script.

## Overview

The `build-qt-macos.sh` script automates the process of:

1. Installing dependencies required for building Qt (via Homebrew)
2. Downloading the Qt source code
3. Configuring it with minimal modules and features for rpi-imager
4. Building and installing Qt to a specified location
5. Creating helper scripts for environment setup

This script is specifically designed to build a **minimal Qt** that contains only what's needed for rpi-imager, similar to the embedded Linux build approach.

## Prerequisites

Before running the script, ensure you have:

- macOS 10.15 (Catalina) or later
- Xcode Command Line Tools installed: `xcode-select --install`
- Homebrew package manager installed: <https://brew.sh>
- At least 15GB of free disk space
- Internet connection to download packages and Qt source
- Sufficient RAM (at least 8GB recommended for faster builds)

## Basic Usage

To build Qt with default options:

```bash
cd src/mac
./build-qt-macos.sh
```

This will:

- Build Qt 6.9.3
- Install it to `/opt/Qt/6.9.3/macos` (works for both Intel and Apple Silicon)
- Use all available CPU cores
- Apply macOS-specific optimizations
- Exclude unnecessary modules and features for minimal footprint

## Command Line Options

The script supports the following options:

```
--version=VERSION      Qt version to build (default: 6.9.3)
--prefix=PREFIX        Installation prefix (default: /opt/Qt/{VERSION})
--cores=CORES          Number of CPU cores to use (default: all)
--no-clean             Don't clean the build directory
--debug                Build with debug information
--skip-dependencies    Skip installing build dependencies
--no-mac-optimize      Disable macOS specific optimizations
--no-universal         Disable universal build (host architecture only)
--verbose              Show verbose build output
--unprivileged         Run without sudo (skips dependency installation)

-h, --help             Show this help message
```

### Examples

Build a specific Qt version:

```bash
./build-qt-macos.sh --version=6.8.0
```

Install to a custom location:

```bash
./build-qt-macos.sh --prefix=/Users/$(whoami)/Qt/6.9.3
```

Limit CPU usage (useful for background builds):

```bash
./build-qt-macos.sh --cores=4
```

Build single-architecture (host only):

```bash
./build-qt-macos.sh --no-universal
```

Run without sudo (requires pre-installed dependencies):

```bash
./build-qt-macos.sh --unprivileged --prefix=$HOME/Qt/6.9.3
```

## Minimal Build Configuration

This script builds a **minimal Qt** by excluding many modules and features that aren't needed for rpi-imager:

### Excluded Modules

- qtwebengine, qt3d, qtmultimedia
- qtwayland, qtspeech, qtconnectivity
- qtcharts, qtdatavis3d, qtlocation
- qtquick3d, qtvirtualkeyboard
- And many more (see `../../modules_exclude.macos.list` or fallback `../../modules_exclude.list`)

### Excluded Features

- PDF support, printing, WebEngine
- Bluetooth, NFC, multimedia codecs
- Advanced widgets (calendar, dial, LCD)
- Development tools (designer, qdoc)
- And many more (see `../../features_exclude.macos.list` or fallback `../../features_exclude.list`)

### ICU Support

The build uses the system ICU or Homebrew's ICU4C package, providing full internationalization support without needing a custom build.

## macOS Optimizations

The script automatically detects your Mac and applies appropriate optimizations:

- **Apple Silicon (M1/M2/M3)**: Optimized for `armv8.4-a+crypto` with `apple-a14` tuning
- **Intel x86_64**: Optimized for `x86-64-v2` with Intel tuning
- **Deployment Target**: Set to macOS 11.0 for modern API support
- **Build Type**: Uses `-no-framework` for easier deployment (builds as dylibs)

### Universal Builds

By default, the script creates fat binaries containing both Intel and Apple Silicon code:

- **Intel optimizations**: `-march=x86-64-v2 -mtune=intel` for better x86_64 performance
- **Apple Silicon optimizations**: `-march=armv8.4-a+crypto -mtune=apple-a14` for M-series chips
- **Installation path**: `/opt/Qt/6.9.3/macos` (same as single-architecture builds)
- **CMake integration**: Toolchain file automatically sets `CMAKE_OSX_ARCHITECTURES="x86_64;arm64"`

**When to use universal builds:**

- ✅ Distributing rpi-imager to end users (works on any Mac)
- ✅ CI/CD systems that need to support both architectures
- ✅ Development teams with mixed Intel/Apple Silicon Macs

**When to use single-architecture builds:**

- ✅ Personal development (faster builds, smaller size)
- ✅ Known target environment (only Intel or only Apple Silicon)
- ✅ Storage/bandwidth constraints

### Universal Build Dependencies

Universal builds require special handling of dependencies because they need libraries available for both x86_64 and arm64 architectures.

**Homebrew Dependency Challenges:**

- On Apple Silicon: Homebrew at `/opt/homebrew` provides ARM64 libraries only
- On Intel Macs: Homebrew at `/usr/local` provides x86_64 libraries only
- Universal builds need **both** architectures

**How the script handles this:**

1. **System Libraries First**: macOS system libraries are often universal, so Qt will prefer these
2. **Dual Homebrew Setup**: If both Intel (`/usr/local`) and ARM (`/opt/homebrew`) Homebrew exist, use both
3. **Automatic Detection**: The script detects your setup and adapts accordingly

**Setting up for Universal Builds (Apple Silicon Mac):**

```bash
# Option 1: Install Intel Homebrew alongside ARM Homebrew (recommended)
arch -x86_64 /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Then install Intel versions of key dependencies
arch -x86_64 /usr/local/bin/brew install icu4c pcre2 libpng openssl@3

# Option 2: Just use the script - it will use system libraries where possible
./build-qt-macos.sh  # Universal by default (add --no-universal to opt out)
```

**Dependency Resolution Priority:**

1. System libraries (universal when available)
2. Intel Homebrew libraries (`/usr/local/lib`)
3. ARM Homebrew libraries (`/opt/homebrew/lib`)
4. Built-in Qt fallbacks

## Build Dependencies

The script automatically installs these dependencies via Homebrew:

```bash
cmake ninja pkg-config perl python3 icu4c pcre2 libpng 
jpeg-turbo freetype fontconfig openssl@3
```

If you use `--skip-dependencies`, make sure these are already installed.

## Using the Built Qt

After building Qt, the script creates several helper files:

1. **Environment setup script**: `$PREFIX/bin/qtenv.sh`
2. **CMake toolchain file**: `$PREFIX/qt6-toolchain.cmake`

### Setting up the environment

To set up your environment for using the built Qt:

```bash
source /opt/Qt/6.9.3/macos/bin/qtenv.sh
```

This sets the needed environment variables for Qt on macOS.

### Building with CMake

To use this Qt build with CMake projects (like rpi-imager):

```bash
cmake -DCMAKE_TOOLCHAIN_FILE=/opt/Qt/6.9.3/macos/qt6-toolchain.cmake \
      -S /path/to/source -B build
```

### Building rpi-imager

To build rpi-imager with your custom Qt:

```bash
# Set up Qt environment
source /opt/Qt/6.9.3/macos/bin/qtenv.sh

# Configure and build rpi-imager
cd /path/to/rpi-imager
mkdir build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=/opt/Qt/6.9.3/macos/qt6-toolchain.cmake ../src
make -j$(sysctl -n hw.ncpu)
```
## Troubleshooting

### Memory Issues

If the build process fails due to memory errors, try reducing the number of cores:

```bash
./build-qt-macos.sh --cores=2
```

### Homebrew Dependencies

If dependency installation fails:

```bash
# Update Homebrew
brew update && brew upgrade

# Manually install dependencies
brew install cmake ninja pkg-config perl python3 icu4c pcre2 libpng jpeg-turbo freetype fontconfig openssl@3
```

### Build Failures

If the build fails:

1. Check the error message in the console output
2. Try with fewer cores using `--cores=2`
3. Try building with `--verbose` to get more detailed output
4. Make sure Xcode Command Line Tools are up to date: `xcode-select --install`

### Qt Detection Issues

If CMake can't find your Qt installation:

```bash
# Make sure Qt is in your path
export CMAKE_PREFIX_PATH="/opt/Qt/6.9.3/macos:$CMAKE_PREFIX_PATH"

# Or use the toolchain file
cmake -DCMAKE_TOOLCHAIN_FILE=/opt/Qt/6.9.3/macos/qt6-toolchain.cmake ...
```

## Integration with rpi-imager Build System

The script is designed to work seamlessly with the existing rpi-imager build system:

1. The built Qt follows the same directory structure as official Qt releases
2. CMake toolchain files are compatible with existing build scripts
3. Environment variables are set appropriately for macOS
4. The minimal feature set matches what rpi-imager actually uses

## Updating Qt Version

To build a different Qt version:

1. Update the `QT_VERSION` variable in the script, or use `--version=X.Y.Z`
2. Check that the version exists at <https://download.qt.io/official_releases/qt/>
3. Test the build with your desired version

## Contributing

If you need to modify the minimal build configuration:

- **Add/remove modules**: Edit `../../modules_exclude.macos.list` (macOS) or `../../modules_exclude.list` (embedded)
- **Add/remove features**: Edit `../../features_exclude.macos.list` (macOS) or `../../features_exclude.list` (embedded)
- **Modify macOS optimizations**: Edit the `MAC_CFLAGS` section in the script
- **Change dependencies**: Update the Homebrew package list

Please test any changes thoroughly before committing.
