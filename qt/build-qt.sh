#!/bin/sh
#
# Script to download and build Qt with Debian-like configuration
# Specifically designed for Raspberry Pi OS (and other Debian-based distributions)
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
    exit 1
}

# Parse common arguments first
parse_common_args "$@"

# Parse script-specific arguments
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

# Set architecture-specific prefix suffix
set_arch_prefix_suffix

# Print configuration
print_common_config "Qt desktop build"

# Install dependencies if not skipped
if [ "$SKIP_DEPENDENCIES" -eq 0 ]; then
    install_linux_basic_deps

    echo "Installing Qt desktop dependencies..."
    sudo apt-get install -y \
        libx11-dev libx11-xcb-dev libxext-dev libxfixes-dev libxi-dev \
        libxrender-dev libxcomposite-dev libxcursor-dev libxdamage-dev \
        libxrandr-dev libxtst-dev \
        libxcb1-dev libxcb-cursor-dev libxcb-glx0-dev libxcb-icccm4-dev \
        libxcb-image0-dev libxcb-keysyms1-dev libxcb-randr0-dev \
        libxcb-render-util0-dev libxcb-shape0-dev libxcb-shm0-dev \
        libxcb-sync-dev libxcb-util-dev libxcb-xfixes0-dev \
        libxcb-xinerama0-dev libxcb-xkb-dev \
        libinput-dev libxkbcommon-dev libxkbcommon-x11-dev \
        libfontconfig1-dev libfreetype6-dev libicu-dev \
        libdrm-dev libegl1-mesa-dev libgbm-dev libgles2-mesa-dev \
        libvulkan-dev \
        libjpeg-dev libpng-dev zlib1g-dev \
        libasound2-dev libpulse-dev \
        libnss3-dev libssl-dev \
        libdbus-1-dev libglib2.0-dev libsqlite3-dev \
        libdouble-conversion-dev libpcre2-dev \
        libatk1.0-dev libatk-bridge2.0-dev \
        libcups2-dev \
        libassimp-dev

    echo "Installing Wayland specific dependencies..."
    sudo apt-get install -y libwayland-dev wayland-protocols \
        libxkbcommon-dev libwayland-cursor0 libwayland-egl1 \
        libwayland-server0 libwayland-client0 libwayland-bin
else
    print_skip_deps_message "Qt desktop"
fi

# Create build directories
create_build_directories

# Download Qt source code
download_qt_source

# Clean build directory if requested
clean_build_directory

# Configure and build Qt
cd "$BUILD_DIR"

# Build config options using helpers
CONFIG_OPTS="$(get_base_config_opts) -make libs $(get_common_skip_opts)"
CONFIG_OPTS="$CONFIG_OPTS $(get_build_type_opts)"

# Apply exclusions
echo "Applying exclusions for desktop build..."
apply_exclusions
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
create_qt_env_script
create_cmake_toolchain

# Print final usage instructions
print_usage_instructions "Qt desktop"
