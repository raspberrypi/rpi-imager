# Qt Cross-Compilation for armhf (ARM Hard-Float)

This document describes how to cross-compile Qt for the armhf architecture, enabling cross-compilation of Raspberry Pi Imager for older Raspberry Pi models and other armhf-based systems.

## Overview

The `build-qt-armhf.sh` script provides automated cross-compilation of Qt 6.9.3 for the armhf (ARM hard-float) architecture. This is particularly useful for:

- Raspberry Pi 2, 3, and 4 (32-bit mode)
- Other ARM-based systems using hard-float ABI
- Cross-compiling applications from x86_64 Linux hosts

## Prerequisites

### Host System Requirements

- x86_64 Linux system (Ubuntu 20.04+ or Debian 11+ recommended)
- At least 8GB RAM and 20GB free disk space
- Internet connection for downloading Qt source and dependencies

### Required Packages

The script can automatically install dependencies, or you can install them manually:

```bash
# Basic cross-compilation toolchain
sudo apt-get update
sudo apt-get install crossbuild-essential-armhf

# Build dependencies
sudo apt-get install build-essential perl python3 git cmake ninja-build pkg-config
sudo apt-get install bison flex gperf qt6-base-dev-tools qt6-declarative-dev-tools qemu-user-static
```

## Sysroot Setup

A sysroot containing the target system's libraries is required for cross-compilation. You have several options:

### Option 1: From Running Raspberry Pi (Recommended)

If you have access to a Raspberry Pi running the target OS:

```bash
# Create sysroot directory
mkdir -p ~/rpi-sysroot

# Sync from Raspberry Pi (replace with your Pi's IP/hostname)
rsync -avz --rsync-path="sudo rsync" --delete \
    pi@raspberrypi.local:/{lib,usr} ~/rpi-sysroot/

# Fix symbolic links for cross-compilation
./fix-sysroot-symlinks.sh ~/rpi-sysroot
```

### Option 2: From Raspberry Pi OS Image

```bash
# Download and mount Raspberry Pi OS image
wget https://downloads.raspberrypi.org/raspios_lite_armhf/images/raspios_lite_armhf-2023-12-11/2023-12-05-raspios-bookworm-armhf-lite.img.xz
unxz 2023-12-05-raspios-bookworm-armhf-lite.img.xz

# Mount the image
sudo mkdir -p /mnt/rpi-image
sudo mount -o loop,offset=272629760 2023-12-05-raspios-bookworm-armhf-lite.img /mnt/rpi-image

# Copy sysroot
mkdir -p ~/rpi-sysroot
sudo cp -a /mnt/rpi-image/{lib,usr} ~/rpi-sysroot/
sudo chown -R $USER:$USER ~/rpi-sysroot

# Unmount
sudo umount /mnt/rpi-image
```

### Sysroot Symbolic Link Fixes

Cross-compilation requires absolute symbolic links to be converted to relative ones:

```bash
# Create a script to fix symlinks
cat > fix-sysroot-symlinks.sh << 'EOF'
#!/bin/bash
SYSROOT="$1"
if [ -z "$SYSROOT" ]; then
    echo "Usage: $0 <sysroot-path>"
    exit 1
fi

find "$SYSROOT" -type l | while read link; do
    target=$(readlink "$link")
    if [[ "$target" == /* ]]; then
        # Convert absolute symlink to relative
        rel_target="${SYSROOT}${target}"
        if [ -e "$rel_target" ]; then
            ln -sf "$rel_target" "$link"
            echo "Fixed: $link -> $rel_target"
        fi
    fi
done
EOF

chmod +x fix-sysroot-symlinks.sh
./fix-sysroot-symlinks.sh ~/rpi-sysroot
```

## Usage

### Basic Cross-Compilation

```bash
# Navigate to the qt directory
cd /path/to/rpi-imager/qt

# Run the cross-compilation script
./build-qt-armhf.sh --sysroot=~/rpi-sysroot
```

### Advanced Options

```bash
# Custom installation prefix
./build-qt-armhf.sh --sysroot=~/rpi-sysroot --prefix=/opt/qt-armhf

# Debug build with verbose output
./build-qt-armhf.sh --sysroot=~/rpi-sysroot --debug --verbose

# Embedded build (no X11/GUI components)
./build-qt-armhf.sh --sysroot=~/rpi-sysroot --embedded

# Custom toolchain prefix
./build-qt-armhf.sh --sysroot=~/rpi-sysroot --toolchain=arm-linux-gnueabihf-

# Use fewer CPU cores
./build-qt-armhf.sh --sysroot=~/rpi-sysroot --cores=4
```

### Complete Command Line Options

```bash
./build-qt-armhf.sh [options]

Options:
  --version=VERSION    Qt version to build (default: 6.9.3)
  --prefix=PREFIX      Installation prefix (default: /opt/Qt/6.9.3)
  --cores=CORES        Number of CPU cores to use (default: all available)
  --sysroot=PATH       Path to armhf sysroot (REQUIRED)
  --toolchain=PREFIX   Cross-compiler prefix (default: arm-linux-gnueabihf-)
  --no-clean           Don't clean the build directory
  --debug              Build with debug information
  --embedded           Build for embedded systems (no X11/GUI)
  --skip-dependencies  Skip installing build dependencies
  --verbose            Show verbose build output
  --unprivileged       Run without sudo (skips dependency installation)
  -h, --help           Show this help message
```

## Build Process

The script performs the following steps:

1. **Dependency Installation**: Installs cross-compilation toolchain and build dependencies
2. **Source Download**: Downloads and extracts Qt source code
3. **Toolchain Setup**: Creates CMake toolchain file for cross-compilation
4. **Configuration**: Configures Qt with appropriate cross-compilation flags
5. **Compilation**: Builds Qt using the specified number of CPU cores
6. **Installation**: Installs Qt to the specified prefix directory
7. **Environment Setup**: Creates scripts for using the cross-compiled Qt

## Build Variants

### Desktop Build (Default)

Includes minimal GUI components suitable for desktop applications:
- OpenGL ES2 support
- XCB platform plugin
- Basic accessibility features

### Embedded Build

Optimized for headless/embedded systems:
- No X11 dependencies
- LinuxFB platform plugin only
- Minimal feature set
- Smaller binary size

## Using the Cross-Compiled Qt

After successful compilation, the script creates environment setup files:

### For Shell Usage

```bash
# Source the environment
source /opt/Qt/6.9.3/gcc_armhf/bin/qtenv-armhf.sh

# Verify Qt installation
qmake -query
```

### For CMake Projects

```bash
# Use the generated toolchain file
cmake -DCMAKE_TOOLCHAIN_FILE=/opt/Qt/6.9.3/gcc_armhf/qt6-armhf-toolchain.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      /path/to/your/project

make -j$(nproc)
```

## Cross-Compiling Raspberry Pi Imager

Once Qt is cross-compiled, you can build Raspberry Pi Imager:

```bash
# Set up environment
source /opt/Qt/6.9.3/gcc_armhf/bin/qtenv-armhf.sh

# Create build directory
mkdir -p ~/rpi-imager-build-armhf
cd ~/rpi-imager-build-armhf

# Configure with CMake
cmake -DCMAKE_TOOLCHAIN_FILE=/opt/Qt/6.9.3/gcc_armhf/qt6-armhf-toolchain.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      /path/to/rpi-imager/src

# Build
make -j$(nproc)
```

## Troubleshooting

### Common Issues

1. **Missing Cross-Compiler**
   ```
   Error: Cross-compiler not found: arm-linux-gnueabihf-gcc
   ```
   Solution: Install `crossbuild-essential-armhf`

2. **Invalid Sysroot**
   ```
   Error: Sysroot directory does not exist
   ```
   Solution: Verify sysroot path and ensure it contains `lib` and `usr` directories

3. **Configure Failures**
   - Check that sysroot contains necessary libraries
   - Verify symbolic links are properly fixed
   - Try with `--verbose` flag for detailed error messages

4. **Build Failures**
   - Ensure sufficient disk space (20GB+)
   - Check for missing dependencies in sysroot
   - Try reducing parallel jobs with `--cores=2`

### Performance Tips

- Use an SSD for faster compilation
- Allocate more RAM if building in a VM
- Use `ccache` to speed up rebuilds:
  ```bash
  sudo apt-get install ccache
  export CC="ccache arm-linux-gnueabihf-gcc"
  export CXX="ccache arm-linux-gnueabihf-g++"
  ```

## File Structure

After successful compilation, the Qt installation will have this structure:

```
/opt/Qt/6.9.3/gcc_armhf/
├── bin/                    # Qt tools and utilities
│   ├── qmake              # Qt build system
│   ├── qtenv-armhf.sh     # Environment setup script
│   └── ...
├── lib/                   # Qt libraries
│   ├── libQt6Core.so      # Core Qt library
│   ├── cmake/             # CMake configuration files
│   └── ...
├── include/               # Qt headers
├── plugins/               # Qt plugins
├── qml/                   # QML modules
└── qt6-armhf-toolchain.cmake  # CMake toolchain file
```

## Integration with CI/CD

For automated builds, you can use the script in CI environments:

```yaml
# Example GitHub Actions workflow
- name: Setup armhf cross-compilation
  run: |
    sudo apt-get update
    sudo apt-get install crossbuild-essential-armhf
    
- name: Setup sysroot
  run: |
    # Download or sync sysroot
    ./setup-sysroot.sh
    
- name: Build Qt for armhf
  run: |
    cd qt
    ./build-qt-armhf.sh --sysroot=../rpi-sysroot --unprivileged
    
- name: Build Raspberry Pi Imager
  run: |
    source /home/runner/Qt/6.9.3/gcc_armhf/bin/qtenv-armhf.sh
    mkdir build && cd build
    cmake -DCMAKE_TOOLCHAIN_FILE=$CMAKE_TOOLCHAIN_FILE ..
    make -j$(nproc)
```

## Contributing

When modifying the cross-compilation script:

1. Test with both desktop and embedded builds
2. Verify compatibility with different sysroot sources
3. Update this documentation for any new options
4. Test the generated binaries on actual armhf hardware

## License

This script and documentation are part of the Raspberry Pi Imager project and follow the same licensing terms.
