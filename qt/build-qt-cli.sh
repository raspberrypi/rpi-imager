#!/bin/bash
#
# Script to download and build Qt with minimal configuration for CLI-only applications
# Builds only QtCore and essential components, excluding all GUI modules
#

set -e

# Source common configuration and functions
BASE_DIR="$(cd "$(dirname "$0")" >/dev/null 2>&1 && pwd)"
source "$BASE_DIR/qt-build-common.sh"

# Initialize common variables
init_common_variables

# CLI-specific defaults (MinSizeRel is already the default)

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

# Parse common arguments (CLI script has no additional arguments)
remaining_args=($(parse_common_args "$@"))

# Check for any remaining unknown arguments
for arg in "${remaining_args[@]}"; do
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
    # Install basic Linux dependencies
    install_linux_basic_deps

    # Minimal Qt dependencies - only what's needed for QtCore
    sudo apt-get install -y \
        `# Font and text rendering (for QtCore text processing)` \
        libfontconfig1-dev libfreetype6-dev libicu-dev \
        `# Security and crypto (for QtNetwork)` \
        libnss3-dev libssl-dev \
        `# System libraries` \
        libdbus-1-dev libglib2.0-dev libsqlite3-dev \
        `# Utility libraries` \
        libdouble-conversion-dev libpcre2-dev \
        `# Compression (for QtCore)` \
        zlib1g-dev
else
    if [ "$UNPRIVILEGED" -eq 1 ]; then
        echo "Skipping dependency installation (unprivileged mode)"
        echo "Please ensure minimal Qt build dependencies are already installed."
    else
        echo "Skipping dependency installation as requested"
    fi
fi

# Create build directories
create_build_directories "cli"

# Download Qt source code
download_qt_source

# Clean build directory if requested
clean_build_directory

cd "$BUILD_DIR"

# Configure Qt with CLI-only options
echo "Configuring Qt for CLI-only build..."

# Base configuration - minimal Qt with only Core components
QT_CONFIGURE_ARGS=(
    # Installation and build options
    -prefix "$PREFIX"
    -opensource
    -confirm-license
    -release  # Qt configure only accepts -release or -debug
    -nomake examples
    -nomake tests
    -no-compile-examples
    
    # CLI-only: Disable ALL GUI-related modules
    -no-gui
    -no-widgets
    -no-opengl
    -no-vulkan
    -no-directfb
    -no-eglfs
    -no-gbm
    -no-kms
    -no-linuxfb
    -no-xcb
    -no-wayland
    -no-feature-accessibility
    
    # Disable multimedia and graphics
    -no-feature-imageformat-bmp
    -no-feature-imageformat-jpeg
    -no-feature-imageformat-png
    -no-feature-imageformat-ppm
    -no-feature-imageformat-xbm
    -no-feature-imageformat-xpm
    -no-feature-movie
    
    # Disable printing
    -no-feature-printer
    -no-feature-printdialog
    -no-feature-printpreviewdialog
    -no-feature-printpreviewwidget
    
    # Keep essential network features for downloads
    -feature-networkproxy
    -ssl
    -openssl-linked
    
    # Keep essential file and system features
    -feature-filesystemwatcher
    -feature-process
    -feature-thread
    -feature-commandlineparser
    -feature-settings
    -feature-temporaryfile
    -feature-library
    
    # Disable unnecessary features
    -no-feature-completer
    -no-feature-systemtrayicon
    -no-feature-clipboard
    -no-feature-draganddrop
    
    # Build only essential modules - skip everything in CLI exclude list
)

# Apply CLI-specific exclusions
apply_exclusions QT_CONFIGURE_ARGS "$BASE_DIR/features_exclude.cli.list" "$BASE_DIR/modules_exclude.cli.list"

# Add verbose flag if requested
if [ "$VERBOSE_BUILD" -eq 1 ]; then
    QT_CONFIGURE_ARGS+=(-verbose)
fi

# Run Qt configure
run_qt_configure QT_CONFIGURE_ARGS

# Build Qt
build_qt

# Install Qt
install_qt

# Create environment and toolchain files
create_qt_env_script "cli"
create_cmake_toolchain "cli"

# Print final usage instructions
echo ""
echo "Qt CLI-only build completed successfully!"
echo ""
echo "Installation details:"
echo "  Location: $PREFIX"
echo "  Architecture: $ARCH"
echo "  Build type: $BUILD_TYPE"
echo "  Modules: QtCore + essential CLI modules only (no GUI, multimedia, or graphics)"
echo ""
print_usage_instructions "Qt CLI"
echo "This Qt build is optimized for CLI applications and has a much smaller footprint"
echo "than a full Qt installation. It includes only essential components needed for"
echo "command-line applications with network and file system capabilities."
