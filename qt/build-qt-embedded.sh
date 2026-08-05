#!/bin/sh
#
# Script to download and build Qt for embedded systems using linuxfb
# Specifically designed for embedded/headless environments without X11/Wayland
#
# POSIX-compliant shell script
#

set -e

# Source common configuration and functions
BASE_DIR="$(cd "$(dirname "$0")" >/dev/null 2>&1 && pwd)"
PROJECT_ROOT="$(cd "$BASE_DIR/.." && pwd)"
. "$BASE_DIR/qt-build-common.sh"

# Initialize common variables
init_common_variables

# Parse command line arguments
usage() {
    echo "Usage: $0 [options]"
    echo "Options:"
    print_common_usage_options
    echo ""
    echo "This script builds Qt optimized for embedded systems:"
    echo "  - Uses linuxfb for direct rendering (no X11/Wayland required)"
    echo "  - Optimized for headless/embedded environments"
    echo "  - Smaller size by excluding desktop-specific libraries"
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

# Set architecture-specific prefix with embedded suffix
set_arch_prefix_suffix "embedded"

# Print configuration
print_common_config "Qt embedded build"
echo "  Target: Embedded systems (linuxfb)"

# Install dependencies if not skipped
if [ "$SKIP_DEPENDENCIES" -eq 0 ]; then
    install_linux_basic_deps

    echo "Installing Qt embedded dependencies..."
    sudo apt-get install -y \
        libinput-dev libxkbcommon-dev \
        libfontconfig1-dev libfreetype6-dev \
        libjpeg-dev libpng-dev zlib1g-dev \
        libnss3-dev libssl-dev \
        libdbus-1-dev libglib2.0-dev libsqlite3-dev \
        libdouble-conversion-dev libpcre2-dev \
        libdrm-dev libgbm-dev libegl1-mesa-dev libgles2-mesa-dev
else
    print_skip_deps_message "Qt embedded"
fi

# Create build directories
create_build_directories "embedded"

# Download Qt source code
download_qt_source

# Clean build directory if requested
clean_build_directory

# ICU is disabled for the embedded build (icu is in features_exclude.embedded.list),
# so no custom ICU is built or linked -- rpi-imager does not use any ICU-backed
# Qt feature. This keeps the embedded image free of the ~35 MB ICU libraries.

# Configure and build Qt
cd "$BUILD_DIR"

echo "Configuring Qt for embedded systems..."

# Build config options using helpers
CONFIG_OPTS="$(get_base_config_opts) $(get_common_skip_opts)"
CONFIG_OPTS="$CONFIG_OPTS $(get_build_type_opts)"

# Embedded platform-specific configuration.
# -no-opengl: the netboot image has no Mesa (far too large); linuxfb renders in
#   software, so Qt must not link libEGL/libGL/libX11.
# -no-dbus:   the image has no session bus (SYSTEMD=0); the app is built without
#   QtDBus for embedded, so building it into Qt would only add an unused
#   libQt6DBus -> libdbus -> libsystemd chain.
CONFIG_OPTS="$CONFIG_OPTS -no-opengl -no-dbus -qpa linuxfb"

# Apply embedded-specific exclusions
apply_exclusions "$BASE_DIR/features_exclude.embedded.list" "$BASE_DIR/modules_exclude.embedded.list"
CONFIG_OPTS="$CONFIG_OPTS $EXCLUSION_OPTS"

# Add CMake-specific options
CONFIG_OPTS="$CONFIG_OPTS $(get_cmake_opts)"

# Run Qt configure
run_qt_configure "$CONFIG_OPTS"

# Build Qt
build_qt

# Install Qt
install_qt

# Create environment and toolchain files
create_qt_env_script "embedded"
create_cmake_toolchain "embedded"

# Print final usage instructions
echo ""
echo "Installation details:"
echo "  Location: $PREFIX"
echo "  Architecture: $ARCH"
echo "  Build type: $BUILD_TYPE"
echo "  Platform: linuxfb (no X11/Wayland required)"
echo ""
print_usage_instructions "Qt embedded"
echo ""
echo "To run applications with this Qt build:"
echo "  export QT_QPA_PLATFORM=linuxfb"
echo "  export QT_QPA_FB_DRM=/dev/dri/card0  # or appropriate device"
