#!/bin/bash
#
# Script to cross-compile Qt for armhf (ARM hard-float) architecture
# Specifically designed for cross-compiling Qt for Raspberry Pi from x86_64 Linux hosts
#

set -e

# Source common configuration and functions
BASE_DIR="$(cd "$(dirname "$0")" >/dev/null 2>&1 && pwd)"
source "$BASE_DIR/qt-build-common.sh"

# Initialize common variables
init_common_variables

# Script-specific defaults
SYSROOT=""            # Sysroot path (required for cross-compilation)
TOOLCHAIN_PREFIX="arm-linux-gnueabihf-"  # Default toolchain prefix
EMBEDDED_BUILD=0      # Build for embedded systems (no X11/GUI)

# Parse command line arguments
usage() {
    echo "Usage: $0 [options]"
    echo "Options:"
    print_common_usage_options
    echo ""
    echo "Cross-compilation specific options:"
    echo "  --sysroot=PATH       Path to armhf sysroot (REQUIRED)"
    echo "  --toolchain=PREFIX   Cross-compiler prefix (default: $TOOLCHAIN_PREFIX)"
    echo "  --embedded           Build for embedded systems (no X11/GUI)"
    echo ""
    echo "Cross-compilation requirements:"
    echo "  - armhf cross-compilation toolchain must be installed"
    echo "  - Sysroot with target system libraries (--sysroot is required)"
    echo "  - For Debian/Ubuntu hosts: sudo apt-get install crossbuild-essential-armhf"
    echo ""
    echo "Example sysroot setup:"
    echo "  # On target Raspberry Pi:"
    echo "  rsync -avz --rsync-path=\"sudo rsync\" --delete pi@raspberrypi.local:/{lib,usr} ./rpi-sysroot/"
    echo "  # Then use: --sysroot=./rpi-sysroot"
    exit 1
}

# Parse common arguments first
parse_common_args "$@"

# Parse script-specific arguments
for arg in "${COMMON_REMAINING_ARGS[@]}"; do
    case $arg in
        --sysroot=*)
            SYSROOT="${arg#*=}"
            ;;
        --toolchain=*)
            TOOLCHAIN_PREFIX="${arg#*=}"
            ;;
        --embedded)
            EMBEDDED_BUILD=1
            ;;
        -h|--help)
            usage
            ;;
        *)
            echo "Unknown option: $arg"
            usage
            ;;
    esac
done

# Validate common inputs
validate_common_inputs

# Validate cross-compilation specific inputs
if [ -z "$SYSROOT" ]; then
    echo "Error: Sysroot path is required for cross-compilation"
    echo "Use --sysroot=/path/to/armhf/sysroot"
    echo "See --help for sysroot setup instructions"
    exit 1
fi

if [ ! -d "$SYSROOT" ]; then
    echo "Error: Sysroot directory does not exist: $SYSROOT"
    exit 1
fi

# Validate cross-compiler availability
CROSS_CC="${TOOLCHAIN_PREFIX}gcc"
CROSS_CXX="${TOOLCHAIN_PREFIX}g++"
if ! command -v "$CROSS_CC" &> /dev/null || ! command -v "$CROSS_CXX" &> /dev/null; then
    echo "Error: Cross-compiler not found: $CROSS_CC or $CROSS_CXX"
    echo "Install with: sudo apt-get install crossbuild-essential-armhf"
    exit 1
fi

# Set architecture-specific prefix with embedded suffix if needed
if [ "$EMBEDDED_BUILD" -eq 1 ]; then
    set_arch_prefix_suffix "armhf_embedded"
else
    set_arch_prefix_suffix "armhf"
fi

# Convert sysroot to absolute path
SYSROOT=$(realpath "$SYSROOT")

# Print configuration
print_common_config "Qt armhf cross-compilation"
echo "  Embedded Build: $EMBEDDED_BUILD"
echo "  Sysroot: $SYSROOT"
echo "  Toolchain: $TOOLCHAIN_PREFIX"
echo "  Cross-compiler: $CROSS_CC"

# Install dependencies if not skipped
if [ "$SKIP_DEPENDENCIES" -eq 0 ]; then
    echo "Installing cross-compilation dependencies..."
    # Install basic Linux dependencies
    install_linux_basic_deps
    
    # Install cross-compilation specific dependencies
    sudo apt-get install -y crossbuild-essential-armhf

    # Additional dependencies for Qt cross-compilation
    sudo apt-get install -y \
        `# Host-side Qt tools (needed for cross-compilation)` \
        qt6-base-dev-tools qt6-declarative-dev-tools \
        `# Additional cross-compilation tools` \
        qemu-user-static
        
    echo "Cross-compilation dependencies installed"
else
    if [ "$UNPRIVILEGED" -eq 1 ]; then
        echo "Skipping dependency installation (unprivileged mode)"
        echo "Please ensure cross-compilation dependencies are already installed."
    else
        echo "Skipping dependency installation as requested"
    fi
fi

# Create build directories
create_build_directories "armhf"

# Download Qt source code
download_qt_source

# Clean build directory if requested
clean_build_directory

# Configure and build Qt
cd "$BUILD_DIR"

# Create CMake toolchain file for cross-compilation
TOOLCHAIN_FILE="$BUILD_DIR/armhf-toolchain.cmake"
cat > "$TOOLCHAIN_FILE" << EOF
# CMake toolchain file for armhf cross-compilation
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Cross-compiler settings
set(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}g++)
set(CMAKE_ASM_COMPILER ${TOOLCHAIN_PREFIX}gcc)

# Sysroot settings
set(CMAKE_SYSROOT ${SYSROOT})
set(CMAKE_FIND_ROOT_PATH ${SYSROOT})

# Search settings
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Compiler flags for armhf
set(CMAKE_C_FLAGS_INIT "-march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard")
set(CMAKE_CXX_FLAGS_INIT "-march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard")

# PKG_CONFIG settings for cross-compilation
set(ENV{PKG_CONFIG_PATH} "")
set(ENV{PKG_CONFIG_LIBDIR} "${SYSROOT}/usr/lib/pkgconfig:${SYSROOT}/usr/share/pkgconfig:${SYSROOT}/usr/lib/arm-linux-gnueabihf/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${SYSROOT}")
EOF

echo "Created CMake toolchain file: $TOOLCHAIN_FILE"

# Base configuration options for cross-compilation
CONFIG_OPTS=(
    -prefix "$PREFIX"
    -opensource
    -confirm-license
    -make libs
    -skip qt3d
    -skip qtandroidextras
    -skip qtwinextras
    -skip qtmacextras
    # Cross-compilation specific options
    -device linux-generic-g++
    -device-option CROSS_COMPILE="$TOOLCHAIN_PREFIX"
    -sysroot "$SYSROOT"
)

# Add build type specific options
if [ "$BUILD_TYPE" = "Debug" ]; then
    CONFIG_OPTS+=(-debug)
elif [ "$BUILD_TYPE" = "MinSizeRel" ]; then
    CONFIG_OPTS+=(-release -optimize-size)
else
    # Default to Release
    CONFIG_OPTS+=(-release)
fi

# Configure for embedded or desktop build
if [ "$EMBEDDED_BUILD" -eq 1 ]; then
    echo "Configuring for embedded build (no X11/GUI)..."
    CONFIG_OPTS+=(
        -no-opengl
        -no-xcb
        -no-gtk
        -qpa linuxfb
        -no-feature-accessibility
    )
    
    # Use embedded-specific exclusions (same as embedded Linux build)
    apply_exclusions CONFIG_OPTS "$BASE_DIR/features_exclude.embedded.list" "$BASE_DIR/modules_exclude.embedded.list"
else
    echo "Configuring for desktop build with minimal GUI..."
    CONFIG_OPTS+=(
        -opengl es2
        -xcb
        -feature-accessibility
    )
    
    # Use regular Linux exclusions (same as desktop Linux build)
    apply_exclusions CONFIG_OPTS "$BASE_DIR/features_exclude.list" "$BASE_DIR/modules_exclude.list"
fi

# Add CMake-specific options
CONFIG_OPTS+=(
    --
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE"
    -DQT_BUILD_TESTS=OFF
    -DQT_BUILD_EXAMPLES=OFF
    -DQT_HOST_PATH="/usr"  # Path to host Qt tools
)

# Set environment variables for cross-compilation
export CC="$CROSS_CC"
export CXX="$CROSS_CXX"
export AR="${TOOLCHAIN_PREFIX}ar"
export STRIP="${TOOLCHAIN_PREFIX}strip"
export OBJCOPY="${TOOLCHAIN_PREFIX}objcopy"
export OBJDUMP="${TOOLCHAIN_PREFIX}objdump"
export RANLIB="${TOOLCHAIN_PREFIX}ranlib"

# PKG_CONFIG settings for cross-compilation
export PKG_CONFIG_PATH=""
export PKG_CONFIG_LIBDIR="$SYSROOT/usr/lib/pkgconfig:$SYSROOT/usr/share/pkgconfig:$SYSROOT/usr/lib/arm-linux-gnueabihf/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="$SYSROOT"

echo "Cross-compilation environment configured:"
echo "  CC: $CC"
echo "  CXX: $CXX"
echo "  PKG_CONFIG_LIBDIR: $PKG_CONFIG_LIBDIR"

# Run Qt configure
run_qt_configure CONFIG_OPTS

# Build Qt
build_qt

# Install Qt
install_qt

# Print cross-compilation specific information
echo "Cross-compilation completed successfully!"
echo ""
echo "Installation details:"
echo "  Target Architecture: armhf (ARM hard-float)"
echo "  Installation Path: $PREFIX"
echo "  Sysroot Used: $SYSROOT"
echo "  Build Type: $BUILD_TYPE"
if [ "$EMBEDDED_BUILD" -eq 1 ]; then
    echo "  Configuration: Embedded (no X11/GUI)"
else
    echo "  Configuration: Desktop with minimal GUI"
fi

echo ""
echo "To use this Qt for cross-compiling applications:"
echo "  export Qt6_ROOT=\"$PREFIX\""
echo "  export PATH=\"$PREFIX/bin:\$PATH\""
echo "  export CMAKE_TOOLCHAIN_FILE=\"$TOOLCHAIN_FILE\""

# Create environment script with cross-compilation variables
create_qt_env_script "armhf" "add_cross_compilation_env_vars"

# Create CMake toolchain file with cross-compilation settings
create_cmake_toolchain "armhf" "add_cross_compilation_cmake"

echo ""
echo "Next steps for cross-compiling Raspberry Pi Imager:"
echo "1. Source the environment: source $PREFIX/bin/qtenv-armhf.sh"
echo "2. Configure your application build with the toolchain file"
echo "3. Build your application using the cross-compiled Qt"
