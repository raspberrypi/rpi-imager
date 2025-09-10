#!/bin/bash
#
# Script to download and build Qt with minimal configuration for CLI-only applications
# Builds only QtCore and essential components, excluding all GUI modules
#

set -e

# Default values
QT_VERSION="6.9.1"    # 6.9.1 is the latest version as of 2025-08-04
PREFIX="/opt/Qt/${QT_VERSION}"     # Default installation prefix with capital 'Q'
CORES=$(nproc)        # Default to all available CPU cores
CLEAN_BUILD=1         # Clean the build directory by default
BUILD_TYPE="MinSizeRel"  # Default build type
SKIP_DEPENDENCIES=0   # Don't skip installing dependencies by default
VERBOSE_BUILD=0       # By default, don't show verbose build output
UNPRIVILEGED=0        # By default, allow sudo usage

# Parse command line arguments
usage() {
    echo "Usage: $0 [options]"
    echo "Options:"
    echo "  --version=VERSION    Qt version to build (default: $QT_VERSION)"
    echo "  --prefix=PREFIX      Installation prefix (default: $PREFIX)"
    echo "  --cores=CORES        Number of CPU cores to use (default: $CORES)"
    echo "  --no-clean           Don't clean the build directory"
    echo "  --debug              Build with debug information"
    echo "  --skip-dependencies  Skip installing build dependencies"
    echo "  --verbose            Show verbose build output"
    echo "  --unprivileged       Run without sudo (skips dependency installation)"
    echo "  -h, --help           Show this help message"
    echo ""
    echo "This script builds a minimal Qt installation for CLI-only applications:"
    echo "  - Only QtCore and essential network/file components"
    echo "  - No GUI modules (QtQuick, QtWidgets, QtSvg, etc.)"
    echo "  - Optimized for headless/server environments"
    echo "  - Significantly smaller build size and faster compilation"
    exit 1
}

for arg in "$@"; do
    case $arg in
        --version=*)
            QT_VERSION="${arg#*=}"
            ;;
        --prefix=*)
            PREFIX="${arg#*=}"
            ;;
        --cores=*)
            CORES="${arg#*=}"
            ;;
        --no-clean)
            CLEAN_BUILD=0
            ;;
        --debug)
            BUILD_TYPE="Debug"
            ;;
        --skip-dependencies)
            SKIP_DEPENDENCIES=1
            ;;
        --verbose)
            VERBOSE_BUILD=1
            ;;
        --unprivileged)
            UNPRIVILEGED=1
            SKIP_DEPENDENCIES=1  # Automatically skip dependencies in unprivileged mode
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

# Validate inputs
if ! [[ $CORES =~ ^[0-9]+$ ]]; then
    echo "Error: Cores must be a number"
    exit 1
fi

# In unprivileged mode, suggest a user-writable prefix if using default
if [ "$UNPRIVILEGED" -eq 1 ] && [ "$PREFIX" = "/opt/Qt/${QT_VERSION}" ]; then
    PREFIX="$HOME/Qt/${QT_VERSION}"
    echo "Unprivileged mode: Using user-writable prefix: $PREFIX"
fi

# Major version (for directory structure)
QT_MAJOR_VERSION=$(echo "$QT_VERSION" | cut -d. -f1)

# Detect system architecture and set appropriate directory suffix
ARCH=$(uname -m)
if [ "$ARCH" = "x86_64" ]; then
    PREFIX="$PREFIX/gcc_64_cli"
elif [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "arm64" ]; then
    PREFIX="$PREFIX/gcc_arm64_cli"
elif [[ "$ARCH" == arm* ]]; then
    PREFIX="$PREFIX/gcc_arm32_cli"
fi

echo "Qt CLI build configuration:"
echo "  Version: $QT_VERSION"
echo "  Prefix: $PREFIX"
echo "  Cores: $CORES"
echo "  Build Type: $BUILD_TYPE"
echo "  Clean Build: $CLEAN_BUILD"
echo "  Verbose Build: $VERBOSE_BUILD"
echo "  Unprivileged Mode: $UNPRIVILEGED"
echo "  Target: CLI-only (no GUI modules)"

# Install dependencies if not skipped
if [ "$SKIP_DEPENDENCIES" -eq 0 ]; then
    echo "Installing minimal build dependencies for CLI-only Qt..."
    # Basic build tools
    sudo apt-get update
    sudo apt-get install -y build-essential perl python3 git cmake ninja-build pkg-config

    # Minimal Qt dependencies - only what's needed for QtCore
    sudo apt-get install -y \
        `# Build tools` \
        bison flex gperf \
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

# Create directories
DOWNLOAD_DIR="$PWD/qt-src"
BUILD_DIR="$PWD/qt-build-cli"
BASE_DIR="$(cd "$(dirname "$0")" >/dev/null 2>&1 && pwd)"
mkdir -p "$DOWNLOAD_DIR" "$BUILD_DIR"

# Download Qt source code
echo "Downloading Qt $QT_VERSION source code..."
cd "$DOWNLOAD_DIR"
if [ ! -d "qt-everywhere-src-$QT_VERSION" ]; then
    if [ ! -f "qt-everywhere-src-$QT_VERSION.tar.xz" ]; then
        echo "Downloading Qt source archive..."
        curl -L -o "qt-everywhere-src-$QT_VERSION.tar.xz" \
            "https://download.qt.io/archive/qt/${QT_MAJOR_VERSION}.${QT_VERSION%.*}/$QT_VERSION/single/qt-everywhere-src-$QT_VERSION.tar.xz"
    fi
    
    echo "Extracting Qt source code..."
    tar -xf "qt-everywhere-src-$QT_VERSION.tar.xz"
fi

cd "$BUILD_DIR"

# Clean previous build if requested
if [ "$CLEAN_BUILD" -eq 1 ]; then
    echo "Cleaning previous build..."
    rm -rf ./*
fi

# Configure Qt with CLI-only options
echo "Configuring Qt for CLI-only build..."

# Base configuration - minimal Qt with only Core components
QT_CONFIGURE_ARGS=(
    # Installation and build options
    -prefix "$PREFIX"
    -opensource
    -confirm-license
    -${BUILD_TYPE,,}  # Convert to lowercase (release, debug, etc.)
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
    
    # Build only essential modules
    -skip qtquick
    -skip qtquickcontrols
    -skip qtquicktimeline
    -skip qtquickeffectmaker
    -skip qtdeclarative
    -skip qtsvg
    -skip qtimageformats
    -skip qtmultimedia
    -skip qtwebengine
    -skip qt3d
    -skip qtwayland
    -skip qtspeech
    -skip qtconnectivity
    -skip qtcharts
    -skip qtdatavis3d
    -skip qtdoc
    -skip qtgraphs
    -skip qthttpserver
    -skip qtlocation
    -skip qtlottie
    -skip qtmqtt
    -skip qtpositioning
    -skip qtquick3d
    -skip qtquick3dphysics
    -skip qtserialbus
    -skip qtserialport
    -skip qtvirtualkeyboard
    -skip qtwebchannel
    -skip qtwebview
    -skip qtremoteobjects
    -skip qt5compat
    -skip qtcoap
    -skip qtlanguageserver
    -skip qtactiveqt
    -skip qtgrpc
    -skip qtopcua
    -skip qtscxml
    -skip qtsensors
    -skip qtwebsockets
)

# Add verbose flag if requested
if [ "$VERBOSE_BUILD" -eq 1 ]; then
    QT_CONFIGURE_ARGS+=(-verbose)
fi

# Run configure
echo "Running Qt configure with CLI-only options..."
../qt-src/qt-everywhere-src-$QT_VERSION/configure "${QT_CONFIGURE_ARGS[@]}"

# Build Qt
echo "Building Qt CLI-only (this will take a while, but much faster than full Qt)..."
if [ "$VERBOSE_BUILD" -eq 1 ]; then
    cmake --build . --parallel "$CORES"
else
    cmake --build . --parallel "$CORES" | grep -E "(Building|Linking|Installing|Error|error|warning|Warning)" || true
fi

# Install Qt
echo "Installing Qt to $PREFIX..."
if [ "$UNPRIVILEGED" -eq 0 ]; then
    sudo cmake --install .
else
    cmake --install .
fi

echo ""
echo "Qt CLI-only build completed successfully!"
echo ""
echo "Installation details:"
echo "  Location: $PREFIX"
echo "  Architecture: $ARCH"
echo "  Build type: $BUILD_TYPE"
echo "  Modules: QtCore, QtNetwork (CLI-only, no GUI)"
echo ""
echo "To use this Qt installation:"
echo "  export Qt6_ROOT=\"$PREFIX\""
echo "  export PATH=\"$PREFIX/bin:\$PATH\""
echo ""
echo "This Qt build is optimized for CLI applications and has a much smaller footprint"
echo "than a full Qt installation. It includes only essential components needed for"
echo "command-line applications with network and file system capabilities."
