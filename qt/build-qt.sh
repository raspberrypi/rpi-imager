#!/bin/bash
#
# Script to download and build Qt with Debian-like configuration
# Specifically designed for Raspberry Pi OS (and other Debian-based distributions)
#

set -e

# Source common configuration and functions
BASE_DIR="$(cd "$(dirname "$0")" >/dev/null 2>&1 && pwd)"
source "$BASE_DIR/qt-build-common.sh"

# Initialize common variables
init_common_variables

# Script-specific defaults
BUILD_TYPE="MinSizeRel"  # Desktop build uses MinSizeRel by default

# Parse command line arguments
usage() {
    echo "Usage: $0 [options]"
    echo "Options:"
    print_common_usage_options
    exit 1
}

# Parse common arguments first
parse_common_args "$@"

# Parse script-specific arguments (COMMON_REMAINING_ARGS is a space-separated string)
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
    # Install basic Linux dependencies
    install_linux_basic_deps

    # Qt dependencies - based on Debian's build dependencies for Qt
    echo "Installing Qt desktop dependencies..."
    sudo apt-get install -y \
        `# X11 core libraries` \
        libx11-dev libx11-xcb-dev libxext-dev libxfixes-dev libxi-dev \
        libxrender-dev libxcomposite-dev libxcursor-dev libxdamage-dev \
        libxrandr-dev libxtst-dev \
        `# XCB libraries` \
        libxcb1-dev libxcb-cursor-dev libxcb-glx0-dev libxcb-icccm4-dev \
        libxcb-image0-dev libxcb-keysyms1-dev libxcb-randr0-dev \
        libxcb-render-util0-dev libxcb-shape0-dev libxcb-shm0-dev \
        libxcb-sync-dev libxcb-util-dev libxcb-xfixes0-dev \
        libxcb-xinerama0-dev libxcb-xkb-dev \
        `# Keyboard and input` \
        libinput-dev libxkbcommon-dev libxkbcommon-x11-dev \
        `# Font and text rendering` \
        libfontconfig1-dev libfreetype6-dev libicu-dev \
        `# Graphics and OpenGL` \
        libdrm-dev libegl1-mesa-dev libgbm-dev libgles2-mesa-dev \
        libvulkan-dev \
        `# Image and media formats` \
        libjpeg-dev libpng-dev zlib1g-dev \
        `# Audio` \
        libasound2-dev libpulse-dev \
        `# Security and crypto` \
        libnss3-dev libssl-dev \
        `# System libraries` \
        libdbus-1-dev libglib2.0-dev libsqlite3-dev \
        `# Utility libraries` \
        libdouble-conversion-dev libpcre2-dev \
        `# Accessibility` \
        libatk1.0-dev libatk-bridge2.0-dev \
        `# Printing` \
        libcups2-dev \
        `# 3D and assets` \
        libassimp-dev

    # Install Wayland-specific dependencies
    echo "Installing Wayland specific dependencies..."
    sudo apt-get install -y libwayland-dev wayland-protocols \
        libxkbcommon-dev libwayland-cursor0 libwayland-egl1 \
        libwayland-server0 libwayland-client0 libwayland-bin
else
    if [ "$UNPRIVILEGED" -eq 1 ]; then
        echo "Skipping dependency installation (unprivileged mode)"
        echo "Please ensure all Qt build dependencies are already installed."
    else
        echo "Skipping dependency installation as requested"
    fi
fi

# Create build directories
create_build_directories

# Download Qt source code
download_qt_source

# Clean build directory if requested
clean_build_directory

# Configure and build Qt
cd "$BUILD_DIR"

# Base configuration options - simplified and closer to Debian's configuration
CONFIG_OPTS=(
    -prefix "$PREFIX"
    -opensource
    -confirm-license
    -make libs
    -skip qt3d
    -skip qtandroidextras
    -skip qtwinextras
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

# Apply feature and module exclusions for desktop build
echo "Applying exclusions for desktop build..."
apply_exclusions CONFIG_OPTS


CONFIG_OPTS+=(
        --
        -DQT_BUILD_TESTS=OFF
        -DQT_BUILD_EXAMPLES=OFF
)

# Run Qt configure
run_qt_configure CONFIG_OPTS

# Build Qt
build_qt

# Install Qt
install_qt

# Create environment and toolchain files
create_qt_env_script
create_cmake_toolchain

# Print final usage instructions
print_usage_instructions "Qt desktop"