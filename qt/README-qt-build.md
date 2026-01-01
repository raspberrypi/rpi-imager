# Building Qt for Raspberry Pi OS

This document explains how to build Qt from source with Debian-like configuration for Raspberry Pi OS using the `build-qt.sh` script.

## Overview

The `build-qt.sh` script automates the process of:
1. Installing dependencies required for building Qt
2. Downloading the Qt source code
3. Configuring it for Raspberry Pi OS
4. Building and installing Qt to a specified location

By default, Qt is built to work with the Raspberry Pi OS Wayland-based desktop environment.

## Prerequisites

Before running the script, ensure you have:
- A Raspberry Pi running Raspberry Pi OS (Debian-based)
- At least 10GB of free disk space
- Internet connection to download packages and Qt source
- Sufficient RAM (at least 2GB, 4GB+ recommended)
- Optionally: a swap file if memory is limited

## Basic Usage

To build Qt with default options:

```bash
./build-qt.sh
```

This will:
- Build Qt 6.9.1
- Install it to `/opt/qt6`
- Use all available CPU cores
- Configure for the Wayland desktop environment

## Command Line Options

The script supports the following options:

```
--version=VERSION    Qt version to build (default: 6.9.1)
--prefix=PREFIX      Installation prefix (default: /opt/qt6)
--cores=CORES        Number of CPU cores to use (default: all)
--no-clean           Don't clean the build directory
--debug              Build with debug information
--skip-dependencies  Skip installing build dependencies
-h, --help           Show this help message
```

### Examples

Build a specific Qt version:
```bash
./build-qt.sh --version=6.9.1
```

Install to a custom location:
```bash
./build-qt.sh --prefix=/home/pi/qt6
```

Limit CPU usage (useful on low-end Raspberry Pi models):
```bash
./build-qt.sh --cores=2
```

## Platform Configuration

### Wayland (Default)

By default, Qt is built to work with the Raspberry Pi OS desktop, which uses Wayland. This configuration is ideal for:
- Regular desktop applications
- Windowed applications
- Applications that need to work alongside other desktop software

## Using the Built Qt

After building Qt, the script creates several helper files:

1. Environment setup script: `$PREFIX/bin/qtenv.sh`
2. CMake toolchain file: `$PREFIX/qt6-toolchain.cmake`

### Setting up the environment

To set up your environment for using the built Qt:

```bash
source /opt/qt6/bin/qtenv.sh
```

This will set the needed environment variables for Qt.

### Building with CMake

To use this Qt build with CMake projects:

```bash
cmake -DCMAKE_TOOLCHAIN_FILE=/opt/qt6/qt6-toolchain.cmake -S /path/to/source -B build
```

## Troubleshooting

### Memory Issues

If the build process fails due to memory errors, investigate the rpi-swapd package.

### Build Failures

If the build fails:
1. Check the error message in the console output
2. Look for missing dependencies 
3. Try with fewer cores using `--cores=2`
4. Try building with `--debug` to get more verbose output

### Platform-Specific Issues

#### Wayland Issues
- If your application doesn't appear or has graphical glitches on Wayland, try forcing the XCB platform:
  ```bash
  export QT_QPA_PLATFORM=xcb
  ```

## Using with rpi-imager

If you're building Qt specifically for the Raspberry Pi Imager:

1. Build Qt using this script
2. Modify the `create-appimage.sh` script to point to your Qt installation:

```bash
# Example: Add to create-appimage.sh
export Qt6_ROOT="/opt/Qt/6.9.1/gcc_arm64"
``` 