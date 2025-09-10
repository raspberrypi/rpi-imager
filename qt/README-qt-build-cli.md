# Qt CLI-Only Build Guide

This guide covers building a minimal Qt installation specifically for CLI-only applications like rpi-imager-cli.

## Overview

The CLI-only Qt build is designed to be as minimal as possible, excluding all GUI, multimedia, and graphics components. This results in:

- **Minimal dependencies** (no X11, Wayland, OpenGL, etc.)
- **Perfect for headless environments**

## Quick Start

```bash
# Build minimal Qt for CLI applications
./build-qt-cli.sh --version=6.9.1

# This installs to /opt/Qt/6.9.1/gcc_64_cli (separate from GUI Qt)
```

## CLI-Specific Exclude Lists

The CLI build uses aggressive exclusion lists to minimize the build:

### `modules_exclude.cli.list`
Excludes entire Qt modules that aren't needed for CLI applications:
- **All GUI modules**: qtquick, qtdeclarative, qtsvg, qtwidgets, qtgui
- **Multimedia**: qtmultimedia, qt3d, qtcharts
- **Web components**: qtwebengine, qtwebchannel
- **Platform-specific**: qtwayland, qtactiveqt
- **IoT/Communication**: qtconnectivity, qtserialbus, qtmqtt
- **And many more...**

### `features_exclude.cli.list`
Excludes specific Qt features within modules:
- **GUI widgets**: calendarwidget, clipboard, systemtrayicon
- **Graphics**: movie, graphicsframecapture, gestures
- **Printing**: All print-related features
- **Development tools**: designer, assistant, qdoc
- **And many more...**

## What's Included

The CLI-only build includes only essential components:

### Core Modules
- **QtCore**: Essential classes, containers, threading, file I/O
- **QtNetwork**: HTTP, SSL/TLS, network requests (for downloads)

### Essential Features
- **File system**: File operations, directory watching
- **Process management**: Running external commands
- **Threading**: Multi-threaded operations
- **Command line parsing**: Argument processing
- **Settings**: Configuration file handling
- **SSL/TLS**: **CRITICAL** - Required for HTTPS downloads from raspberrypi.org
- **HTTP client**: Required for downloading OS lists and images
- **Network proxy**: Required for corporate/proxy environments

## Usage Examples

```bash
# Build CLI-optimized Qt
./build-qt-cli.sh --version=6.9.1 --cores=4

# Use with rpi-imager CLI build
export Qt6_ROOT="/opt/Qt/6.9.1/gcc_64_cli"
mkdir build && cd build
cmake ../src -DBUILD_CLI_ONLY=ON
make -j$(nproc)
```

## Size Comparison

| Build Type | Qt Size | Build Time | Use Case |
|------------|---------|------------|----------|
| Full Qt    | ~800MB  | 2-4 hours  | GUI applications |
| Embedded Qt| ~400MB  | 1-2 hours  | Embedded GUI |
| **CLI Qt** | ~100MB  | 30-60 min  | **CLI-only apps** |

## Customizing Exclude Lists

You can modify the exclude lists to suit your needs:

1. **Add modules/features**: Add lines to the `.cli.list` files
2. **Remove exclusions**: Comment out lines with `#`
3. **Test changes**: Rebuild Qt to verify functionality

### Example: Enable SQL Support
```bash
# In modules_exclude.cli.list, comment out:
# qtbase-sql

# In features_exclude.cli.list, comment out:
# sql-sqlite
```

## Troubleshooting

### Missing Features
If your CLI application needs a feature that's excluded:
1. Check which exclude list contains it
2. Comment out the line in the appropriate `.cli.list` file
3. Rebuild Qt

### Build Errors
- Ensure you have minimal build dependencies installed
- Check that you're not trying to use GUI features in CLI code
- Verify exclude lists don't conflict with required features

## Integration with rpi-imager-cli

The CLI-only Qt build is specifically designed for:
- `rpi-imager` built with `-DBUILD_CLI_ONLY=ON`
- CLI-only AppImage creation
- Headless/server deployments
- Docker containers
- CI/CD pipelines

This minimal Qt installation provides everything needed for rpi-imager's CLI functionality while keeping the footprint as small as possible.
