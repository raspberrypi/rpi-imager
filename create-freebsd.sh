#!/bin/sh
set -e

# Parse command line arguments
ARCH=$(uname -m)  # Default to current architecture
QT_ROOT_ARG=""
CLEAN_BUILD=1
CLI_BUILD=0

usage() {
    echo "Usage: $0 [options]"
    echo "Options:"
    echo "  --arch=ARCH        Target architecture (amd64, arm64)"
    echo "  --qt-root=PATH     Path to Qt installation directory"
    echo "  --no-clean         Don't clean build directory"
    echo "  --cli              Build optimized cli-only executable"
    echo "  -h, --help         Show this help message"
    exit 1
}

for arg in "$@"; do
    case $arg in
        --arch=*)
            ARCH="${arg#*=}"
            ;;
        --qt-root=*)
            QT_ROOT_ARG="${arg#*=}"
            ;;
        --no-clean)
            CLEAN_BUILD=0
            ;;
        --cli)
            CLI_BUILD=1
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

# Resolve Qt root path argument if provided (expand ~ and convert to absolute path)
if [ -n "$QT_ROOT_ARG" ]; then
    # Expand tilde if present at the start
    case "$QT_ROOT_ARG" in
        "~"/*) QT_ROOT_ARG="$HOME/${QT_ROOT_ARG#\~/}" ;;
        "~")   QT_ROOT_ARG="$HOME" ;;
    esac
    # Convert to absolute path if it exists
    if [ -e "$QT_ROOT_ARG" ]; then
        QT_ROOT_ARG=$(cd "$QT_ROOT_ARG" && pwd)
    else
        echo "Warning: Specified Qt root path does not exist: $QT_ROOT_ARG"
        echo "Will attempt to use it anyway, but this may fail..."
    fi
fi

# Validate architecture
if [ "$ARCH" != "amd64" ] && [ "$ARCH" != "arm64" ]; then
    echo "Error: Architecture must be amd64 or arm64"
    exit 1
fi

if [ "$CLI_BUILD" -eq 0 ]; then
    echo "Building GUI executable for architecture: $ARCH"
else
    echo "Building CLI-only executable for architecture: $ARCH"
fi

# Extract project information from CMakeLists.txt
SOURCE_DIR="src/"
CMAKE_FILE="${SOURCE_DIR}CMakeLists.txt"
CMAKE_INSTALL_PREFIX="/usr/local"

# Get version from git tag (same approach as CMake)
GIT_VERSION=$(git describe --tags --always --dirty 2>/dev/null || echo "0.0.0-unknown")

# Extract numeric version components for compatibility
# Match versions like: v1.2.3, 1.2.3, v1.2.3-extra, etc.
MAJOR=$(echo "$GIT_VERSION" | sed -n 's/^v\{0,1\}\([0-9]\{1,\}\)\.[0-9]\{1,\}\.[0-9]\{1,\}.*/\1/p')
MINOR=$(echo "$GIT_VERSION" | sed -n 's/^v\{0,1\}[0-9]\{1,\}\.\([0-9]\{1,\}\)\.[0-9]\{1,\}.*/\1/p')
PATCH=$(echo "$GIT_VERSION" | sed -n 's/^v\{0,1\}[0-9]\{1,\}\.[0-9]\{1,\}\.\([0-9]\{1,\}\).*/\1/p')

if [ -n "$MAJOR" ] && [ -n "$MINOR" ] && [ -n "$PATCH" ]; then
    PROJECT_VERSION="$MAJOR.$MINOR.$PATCH"
else
    MAJOR="0"
    MINOR="0"
    PATCH="0"
    PROJECT_VERSION="0.0.0"
    echo "Warning: Could not parse version from git tag: $GIT_VERSION"
fi

# Extract project name
PROJECT_NAME=$(grep "project(" "$CMAKE_FILE" | head -1 | sed 's/project(\([^[:space:]]*\).*/\1/' | tr '[:upper:]' '[:lower:]')

if [ "$CLI_BUILD" -eq 0 ]; then
    echo "Building $PROJECT_NAME version $GIT_VERSION (numeric: $PROJECT_VERSION) for GUI operation"
else
    echo "Building $PROJECT_NAME version $GIT_VERSION (numeric: $PROJECT_VERSION) for CLI-only operation"
fi

# Check for Qt installation
# Priority: 1. Command line argument, 2. Environment variable, 3. Auto-detection
QT_VERSION=""
QT_DIR=""

# Check if Qt root is specified via command line argument (highest priority)
if [ -n "$QT_ROOT_ARG" ]; then
    echo "Using Qt from command line argument: $QT_ROOT_ARG"
    QT_DIR="$QT_ROOT_ARG"
# Check if Qt6_ROOT is explicitly set in environment
elif [ -n "$Qt6_ROOT" ]; then
    echo "Using Qt from Qt6_ROOT environment variable: $Qt6_ROOT"
    QT_DIR="$Qt6_ROOT"
fi

# If Qt not found, provide instructions for getting it
# Qt does not build without FreeBSD-specific patches
if [ -z "$QT_DIR" ]; then
    echo "Error: Suitable Qt installation for $ARCH not provided"

    echo "Please install the patched version from the FreeBSD ports database:"
    echo "  pkg install qt6-base"
    if [ "$CLI_BUILD" -eq 0 ]; then
        echo "  pkg install qt6-imageformats"
    fi
    echo "If you need translations:"
    echo "  pkg install qt6-tools"

    echo ""

    echo "Or build the port from source without portmaster (make sure to clone the ports tree):"
    echo "  portsmaster --packages-build /usr/ports/devel/qt6-base && make install clean"
    if [ "$CLI_BUILD" -eq 0 ]; then
        echo "  portsmaster --packages-build /usr/ports/devel/qt6-imageformats"
    fi
    echo "If you need translations:"
    echo "  portsmaster --packages-build /usr/ports/devel/qt6-tools"

    echo ""

    echo "Or build the port from source without portmaster (make sure to clone the ports tree):"
    echo "  cd /usr/ports/devel/qt6-base && make install clean"
    if [ "$CLI_BUILD" -eq 0 ]; then
        echo "  cd /usr/ports/devel/qt6-imageformats && make install clean"
    fi
    echo "If you need translations:"
    echo "  cd /usr/ports/devel/qt6-tools && make install clean"

    echo ""

    echo "Or otherwise obtain a FreeBSD-compatible copy of the Qt6 base."

    echo ""

    echo "You can then specify the Qt location (/usr/local/lib/qt6 from the ports tree) with:"
    echo "  $0 --qt-root=/path/to/qt"

    exit 1
fi

# Try to determine the version if possible
if [ -f "$QT_DIR/bin/qmake" ]; then
    QT_VERSION=$("$QT_DIR/bin/qmake" -query QT_VERSION)
    echo "Qt version: $QT_VERSION"
fi

# Configuration
BUILD_TYPE="Release"

if [ "$CLI_BUILD" -eq 0 ]; then
    QML_SOURCES_PATH="$PWD/src/qmlcomponents/"
fi

# Set up build directory
if [ "$CLI_BUILD" -eq 0 ]; then
    BUILD_DIR="build-$ARCH"
else
    BUILD_DIR="build-cli-$ARCH"
fi

# Clean up previous builds if requested
if [ "$CLEAN_BUILD" -eq 1 ]; then
    echo "Cleaning previous build..."
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"

echo "Building rpi-imager for $ARCH..."
# Configure and build with CMake
cd "$BUILD_DIR"

# Enable only local libraries (no git fetches permitted at build time for FreeBSD ports)
CMAKE_EXTRA_FLAGS="$CMAKE_EXTRA_FLAGS -DUSE_LOCAL_ONLY=ON"

# Add Qt path to CMake flags
CMAKE_EXTRA_FLAGS="$CMAKE_EXTRA_FLAGS -DQt6_ROOT=$QT_DIR"

if [ "$CLI_BUILD" -eq 1 ]; then
    CMAKE_EXTRA_FLAGS="$CMAKE_EXTRA_FLAGS -DBUILD_CLI_ONLY=ON"
fi

# shellcheck disable=SC2086
cmake "../$SOURCE_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DCMAKE_INSTALL_PREFIX=$CMAKE_INSTALL_PREFIX $CMAKE_EXTRA_FLAGS
make -j"$(nproc)"
echo "Build completed successfully for $ARCH architecture."

# Install the binary
make install
