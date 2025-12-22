#!/bin/sh
#
# Script to cross-compile Qt for armhf (ARM hard-float) architecture
# Specifically designed for cross-compiling Qt for Raspberry Pi from x86_64 Linux hosts
#
# POSIX-compliant shell script
#

set -e

# Source common configuration and functions
BASE_DIR="$(cd "$(dirname "$0")" >/dev/null 2>&1 && pwd)"
. "$BASE_DIR/qt-build-common.sh"

# Initialize common variables
init_common_variables

# Script-specific defaults
SYSROOT=""
TOOLCHAIN_PREFIX="arm-linux-gnueabihf-"
EMBEDDED_BUILD=0

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
    echo "  rsync -avz --rsync-path=\"sudo rsync\" --delete pi@raspberrypi.local:/{lib,usr} ./rpi-sysroot/"
    echo "  $0 --sysroot=./rpi-sysroot"
    exit 1
}

# Parse common arguments first
parse_common_args "$@"

# Parse script-specific arguments
for arg in $COMMON_REMAINING_ARGS; do
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
if ! command -v "$CROSS_CC" >/dev/null 2>&1 || ! command -v "$CROSS_CXX" >/dev/null 2>&1; then
    echo "Error: Cross-compiler not found: $CROSS_CC or $CROSS_CXX"
    echo "Install with: sudo apt-get install crossbuild-essential-armhf"
    exit 1
fi

# Set architecture-specific prefix
if [ "$EMBEDDED_BUILD" -eq 1 ]; then
    set_arch_prefix_suffix "armhf_embedded"
else
    set_arch_prefix_suffix "armhf"
fi

# Convert sysroot to absolute path
SYSROOT=$(cd "$SYSROOT" && pwd)

# Print configuration
print_common_config "Qt armhf cross-compilation"
echo "  Embedded Build: $EMBEDDED_BUILD"
echo "  Sysroot: $SYSROOT"
echo "  Toolchain: $TOOLCHAIN_PREFIX"
echo "  Cross-compiler: $CROSS_CC"

# Install dependencies if not skipped
if [ "$SKIP_DEPENDENCIES" -eq 0 ]; then
    echo "Installing cross-compilation dependencies..."
    install_linux_basic_deps
    
    sudo apt-get install -y crossbuild-essential-armhf
    sudo apt-get install -y qt6-base-dev-tools qt6-declarative-dev-tools qemu-user-static
        
    echo "Cross-compilation dependencies installed"
else
    print_skip_deps_message "cross-compilation"
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

set(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}g++)
set(CMAKE_ASM_COMPILER ${TOOLCHAIN_PREFIX}gcc)

set(CMAKE_SYSROOT ${SYSROOT})
set(CMAKE_FIND_ROOT_PATH ${SYSROOT})

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(CMAKE_C_FLAGS_INIT "-march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard")
set(CMAKE_CXX_FLAGS_INIT "-march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard")

set(ENV{PKG_CONFIG_PATH} "")
set(ENV{PKG_CONFIG_LIBDIR} "${SYSROOT}/usr/lib/pkgconfig:${SYSROOT}/usr/share/pkgconfig:${SYSROOT}/usr/lib/arm-linux-gnueabihf/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${SYSROOT}")
EOF

echo "Created CMake toolchain file: $TOOLCHAIN_FILE"

# Build config options using helpers
CONFIG_OPTS="$(get_base_config_opts) -make libs $(get_common_skip_opts) -skip qtmacextras"
CONFIG_OPTS="$CONFIG_OPTS $(get_build_type_opts)"

# Cross-compilation specific options
CONFIG_OPTS="$CONFIG_OPTS -device linux-generic-g++ -device-option CROSS_COMPILE=\"$TOOLCHAIN_PREFIX\" -sysroot \"$SYSROOT\""

# Configure for embedded or desktop build
if [ "$EMBEDDED_BUILD" -eq 1 ]; then
    echo "Configuring for embedded build (no X11/GUI)..."
    CONFIG_OPTS="$CONFIG_OPTS -no-opengl -no-xcb -no-gtk -qpa linuxfb -no-feature-accessibility"
    apply_exclusions "$BASE_DIR/features_exclude.embedded.list" "$BASE_DIR/modules_exclude.embedded.list"
else
    echo "Configuring for desktop build with minimal GUI..."
    CONFIG_OPTS="$CONFIG_OPTS -opengl es2 -xcb -feature-accessibility"
    apply_exclusions "$BASE_DIR/features_exclude.list" "$BASE_DIR/modules_exclude.list"
fi
CONFIG_OPTS="$CONFIG_OPTS $EXCLUSION_OPTS"

# Add CMake-specific options
CONFIG_OPTS="$CONFIG_OPTS -- -DCMAKE_TOOLCHAIN_FILE=\"$TOOLCHAIN_FILE\" -DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF -DQT_HOST_PATH=\"/usr\""

# Set environment variables for cross-compilation
export CC="$CROSS_CC"
export CXX="$CROSS_CXX"
export AR="${TOOLCHAIN_PREFIX}ar"
export STRIP="${TOOLCHAIN_PREFIX}strip"
export OBJCOPY="${TOOLCHAIN_PREFIX}objcopy"
export OBJDUMP="${TOOLCHAIN_PREFIX}objdump"
export RANLIB="${TOOLCHAIN_PREFIX}ranlib"

export PKG_CONFIG_PATH=""
export PKG_CONFIG_LIBDIR="$SYSROOT/usr/lib/pkgconfig:$SYSROOT/usr/share/pkgconfig:$SYSROOT/usr/lib/arm-linux-gnueabihf/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="$SYSROOT"

echo "Cross-compilation environment configured:"
echo "  CC: $CC"
echo "  CXX: $CXX"
echo "  PKG_CONFIG_LIBDIR: $PKG_CONFIG_LIBDIR"

# Run Qt configure
run_qt_configure "$CONFIG_OPTS"

# Build Qt
build_qt

# Install Qt
install_qt

# Print results
echo ""
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
CROSS_TOOLCHAIN_FILE="$TOOLCHAIN_FILE"
create_qt_env_script "armhf" "add_cross_compilation_env_vars"
create_cmake_toolchain "armhf" "add_cross_compilation_cmake"

print_usage_instructions "Qt armhf cross-compilation"
