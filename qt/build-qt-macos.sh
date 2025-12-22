#!/bin/sh
#
# Script to download and build Qt with minimal configuration for macOS
# Specifically designed for rpi-imager - builds only what we need
#
# POSIX-compliant shell script
#

set -e

# Source common configuration and functions
BASE_DIR="$(cd "$(dirname "$0")" >/dev/null 2>&1 && pwd)"
. "$BASE_DIR/qt-build-common.sh"

# Initialize common variables (sets IS_MACOS=1 on Darwin)
init_common_variables

# macOS-specific defaults
UNIVERSAL_BUILD=1  # Build universal binaries by default on macOS
BUILD_EXAMPLES=OFF

# Parse command line arguments
usage() {
    echo "Usage: $0 [options]"
    echo "Options:"
    print_common_usage_options
    echo ""
    echo "  --build-examples     Build Qt examples"
    echo ""
    echo "This script builds a minimal Qt for rpi-imager on macOS:"
    echo "  - Excludes unnecessary modules and features"
    echo "  - Optimized for size and rpi-imager use case"
    echo "  - Uses Homebrew for dependency management"
    exit 1
}

# Parse common arguments first
parse_common_args "$@"

# Parse script-specific arguments
for arg in $COMMON_REMAINING_ARGS; do
    case $arg in
        --build-examples)
            BUILD_EXAMPLES=ON
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

# Set architecture-specific prefix
set_arch_prefix_suffix

# Check for required tools
if ! command -v cmake >/dev/null 2>&1; then
    echo "Error: cmake is required but not installed"
    echo "Install with: brew install cmake"
    exit 1
fi

if ! command -v ninja >/dev/null 2>&1; then
    echo "Error: ninja is required but not installed"
    echo "Install with: brew install ninja"
    exit 1
fi

# Print configuration
print_common_config "Qt macOS build"

# Install dependencies if not skipped
if [ "$SKIP_DEPENDENCIES" -eq 0 ]; then
    install_macos_deps
    
    if [ "$UNIVERSAL_BUILD" -eq 1 ]; then
        echo ""
        echo "Note: Universal builds require dependencies for both x86_64 and arm64."
        echo "Qt's configure script will detect the best available libraries."
    fi
else
    print_skip_deps_message "Qt macOS"
fi

# Create build directories
create_build_directories "macos"

# Download Qt source code
download_qt_source

# Clean build directory if requested
clean_build_directory

# Set up macOS compiler flags
setup_macos_flags

# Apply macOS compiler flags to environment
apply_macos_flags

# Set up Homebrew paths
setup_homebrew_paths

# Configure and build Qt
cd "$BUILD_DIR"

# Build config options using helpers
CONFIG_OPTS="$(get_base_config_opts) -no-glib -make libs $(get_common_skip_opts)"
CONFIG_OPTS="$CONFIG_OPTS $(get_build_type_opts)"

# Platform-specific configuration for macOS
CONFIG_OPTS="$CONFIG_OPTS -opengl desktop -no-dbus -no-framework"

# Apply macOS-specific exclusions
echo "Excluding features and modules for minimal build..."
apply_exclusions "$BASE_DIR/features_exclude.macos.list" "$BASE_DIR/modules_exclude.macos.list"
CONFIG_OPTS="$CONFIG_OPTS $EXCLUSION_OPTS"

# Add CMake-specific options
CONFIG_OPTS="$CONFIG_OPTS $(get_cmake_opts "$BUILD_EXAMPLES")"

# Add universal build configuration if requested
if [ "$UNIVERSAL_BUILD" -eq 1 ]; then
    CONFIG_OPTS="$CONFIG_OPTS -DCMAKE_OSX_ARCHITECTURES=\"x86_64;arm64\""
    echo "Configuring for universal build: x86_64 + arm64"
fi

# Run Qt configure
run_qt_configure "$CONFIG_OPTS"

# Build Qt
build_qt

# Install Qt
install_qt

# Create environment and toolchain files
create_qt_env_script "macos"
create_cmake_toolchain "macos"

# Print final instructions
echo ""
echo "Installation details:"
echo "  Location: $PREFIX"
echo "  Build type: $BUILD_TYPE"
if [ "$UNIVERSAL_BUILD" -eq 1 ]; then
    echo "  Architectures: x86_64 + arm64 (Universal)"
else
    echo "  Architecture: $ARCH"
fi
echo ""

print_usage_instructions "Qt macOS"

if [ -n "$MAC_CFLAGS" ]; then
    echo "macOS optimization flags used: $MAC_CFLAGS"
fi
