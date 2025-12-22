#!/bin/sh
#
# Script to download and build Qt with minimal configuration for CLI-only applications
# Builds only QtCore and essential components, excluding all GUI modules
#
# POSIX-compliant shell script
#

set -e

# Source common configuration and functions
BASE_DIR="$(cd "$(dirname "$0")" >/dev/null 2>&1 && pwd)"
. "$BASE_DIR/qt-build-common.sh"

# Initialize common variables
init_common_variables

# Parse command line arguments
usage() {
    echo "Usage: $0 [options]"
    echo "Options:"
    print_common_usage_options
    echo ""
    echo "This script builds a minimal Qt installation for CLI-only applications:"
    echo "  - Only QtCore and essential network/file components"
    echo "  - No GUI modules (QtQuick, QtWidgets, QtSvg, etc.)"
    echo "  - Optimized for headless/server environments"
    echo "  - Significantly smaller build size and faster compilation"
    exit 1
}

# Parse common arguments
parse_common_args "$@"

# Check for any remaining unknown arguments
for arg in $COMMON_REMAINING_ARGS; do
    case $arg in
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

# Set architecture-specific prefix with CLI suffix
set_arch_prefix_suffix "cli"

# Print configuration
print_common_config "Qt CLI build"
echo "  Target: CLI-only (no GUI modules)"

# Install dependencies if not skipped
if [ "$SKIP_DEPENDENCIES" -eq 0 ]; then
    echo "Installing minimal build dependencies for CLI-only Qt..."
    install_linux_basic_deps

    sudo apt-get install -y \
        libfontconfig1-dev libfreetype6-dev libicu-dev \
        libnss3-dev libssl-dev \
        libdbus-1-dev libglib2.0-dev libsqlite3-dev \
        libdouble-conversion-dev libpcre2-dev \
        zlib1g-dev
else
    print_skip_deps_message "Qt CLI"
fi

# Create build directories
create_build_directories "cli"

# Download Qt source code
download_qt_source

# Clean build directory if requested
clean_build_directory

cd "$BUILD_DIR"

echo "Configuring Qt for CLI-only build..."

# Build config options - CLI-only with no GUI
CONFIG_OPTS="$(get_base_config_opts) $(get_build_type_opts)"

# CLI-only: Disable ALL GUI-related modules
CONFIG_OPTS="$CONFIG_OPTS -no-gui -no-widgets -no-opengl -no-vulkan -no-directfb -no-eglfs -no-gbm -no-kms -no-linuxfb -no-xcb"

# Apply CLI-specific exclusions
apply_exclusions "$BASE_DIR/features_exclude.cli.list" "$BASE_DIR/modules_exclude.cli.list"
CONFIG_OPTS="$CONFIG_OPTS $EXCLUSION_OPTS"

# Add CMake options
CONFIG_OPTS="$CONFIG_OPTS $(get_cmake_opts)"

# Run Qt configure
run_qt_configure "$CONFIG_OPTS"

# Build Qt
build_qt

# Install Qt
install_qt

# Create environment and toolchain files
create_qt_env_script "cli"
create_cmake_toolchain "cli"

# Print final usage instructions
echo ""
echo "Installation details:"
echo "  Location: $PREFIX"
echo "  Architecture: $ARCH"
echo "  Build type: $BUILD_TYPE"
echo "  Modules: QtCore + essential CLI modules only (no GUI, multimedia, or graphics)"
echo ""
print_usage_instructions "Qt CLI"
echo "This Qt build is optimized for CLI applications and has a much smaller footprint"
echo "than a full Qt installation."
