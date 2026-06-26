#!/bin/sh
set -e

# Script to create AppImage for CLI-only rpi-imager
# This creates a minimal AppImage that only supports command-line operation

# Parse command line arguments
ARCH=$(uname -m)  # Default to current architecture
CLEAN_BUILD=1
QT_ROOT_ARG=""

usage() {
    echo "Usage: $0 [options]"
    echo "Options:"
    echo "  --arch=ARCH            Target architecture (x86_64, aarch64, armhf)"
    echo "  --qt-root=PATH         Path to Qt installation directory"
    echo "  --no-clean             Don't clean build directory"
    echo "  -h, --help             Show this help message"
    echo ""
    echo "This script creates an AppImage optimized for CLI-only operation:"
    echo "  - Forces CLI mode (no GUI components)"
    echo "  - Uses only Qt Core libraries"
    echo "  - Minimal size by excluding all GUI dependencies"
    echo "  - Perfect for headless/server environments"
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

# Validate architecture (armv6l/armv7l from uname on 32-bit Pi OS → armhf)
case "$ARCH" in
    armv6l|armv7l) ARCH=armhf ;;
esac
if [ "$ARCH" != "x86_64" ] && [ "$ARCH" != "aarch64" ] && [ "$ARCH" != "armhf" ]; then
    echo "Error: Architecture must be one of: x86_64, aarch64, armhf" >&2
    exit 1
fi

echo "Building CLI-only AppImage for architecture: $ARCH"

# Extract project information from CMakeLists.txt
SOURCE_DIR="src/"
CMAKE_FILE="${SOURCE_DIR}CMakeLists.txt"

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

# Extract project name (lowercase for AppImage naming convention)
PROJECT_NAME=$(grep "project(" "$CMAKE_FILE" | head -1 | sed 's/project(\([^[:space:]]*\).*/\1/' | tr '[:upper:]' '[:lower:]')

echo "Building $PROJECT_NAME version $GIT_VERSION (numeric: $PROJECT_VERSION) for CLI-only operation"

QT_VERSION=""
QT_DIR=""

# Reject desktop Qt paths — CLI AppImages must use gcc_*_cli builds only.
validate_cli_qt_dir() {
	_dir=$1
	case "$_dir" in
		*/gcc_64_cli|*/gcc_arm64_cli|*/gcc_arm32_cli) return 0 ;;
	esac
	echo "Error: CLI AppImages require a CLI-only Qt build (gcc_*_cli), not desktop Qt." >&2
	echo "  Refusing: $_dir" >&2
	echo "  Build one with: ./qt/build-qt-cli.sh" >&2
	exit 1
}

# Check if Qt root is specified via command line argument (highest priority)
if [ -n "$QT_ROOT_ARG" ]; then
    echo "Using Qt from command line argument: $QT_ROOT_ARG"
    QT_DIR="$QT_ROOT_ARG"
# Check if Qt6_ROOT is explicitly set in environment
elif [ -n "$Qt6_ROOT" ]; then
    echo "Using Qt from Qt6_ROOT environment variable: $Qt6_ROOT"
    QT_DIR="$Qt6_ROOT"
# Auto-detect CLI Qt in QT_CACHE, then /opt/Qt (gcc_*_cli only)
else
    _qt_search_dirs=""
    if [ -n "${QT_CACHE:-}" ] && [ -d "$QT_CACHE" ]; then
        case "$ARCH" in
            x86_64) _qt_deb_arch=amd64 ;;
            aarch64) _qt_deb_arch=arm64 ;;
            armhf) _qt_deb_arch=armhf ;;
            *) _qt_deb_arch="" ;;
        esac
        if [ -n "$_qt_deb_arch" ] && [ -d "$QT_CACHE/$_qt_deb_arch" ]; then
            _qt_search_dirs="$QT_CACHE/$_qt_deb_arch"
        fi
    fi
    if [ -z "$_qt_search_dirs" ] && [ -d "/opt/Qt" ]; then
        _qt_search_dirs="/opt/Qt"
    fi

    if [ -n "$_qt_search_dirs" ]; then
        echo "Checking for CLI Qt installations in $_qt_search_dirs..."
        NEWEST_QT=$(find -L "$_qt_search_dirs" -maxdepth 1 -type d -name "6.*" | sort -V | tail -n 1)
        if [ -n "$NEWEST_QT" ]; then
            QT_VERSION=$(basename "$NEWEST_QT")
            
            if [ "$ARCH" = "x86_64" ]; then
                if [ -d "$NEWEST_QT/gcc_64_cli" ]; then
                    QT_DIR="$NEWEST_QT/gcc_64_cli"
                fi
            elif [ "$ARCH" = "aarch64" ]; then
                if [ -d "$NEWEST_QT/gcc_arm64_cli" ]; then
                    QT_DIR="$NEWEST_QT/gcc_arm64_cli"
                fi
            elif [ "$ARCH" = "armhf" ]; then
                if [ -d "$NEWEST_QT/gcc_arm32_cli" ]; then
                    QT_DIR="$NEWEST_QT/gcc_arm32_cli"
                fi
            fi
            
            if [ -n "$QT_DIR" ]; then
                echo "Found CLI Qt $QT_VERSION for $ARCH at $QT_DIR"
            elif [ -n "$QT_VERSION" ]; then
                echo "Found Qt $QT_VERSION, but no CLI build for $ARCH (gcc_*_cli)"
                QT_VERSION=""
            fi
        fi
    fi
fi

# If Qt not found, suggest building it
if [ -z "$QT_DIR" ]; then
    echo "Error: No CLI-only Qt installation found for $ARCH" >&2
    echo "Desktop Qt (gcc_64, gcc_arm64, …) must not be used for CLI AppImages." >&2
    
    if [ -f "./qt/build-qt-cli.sh" ]; then
        echo "Build a CLI-only Qt with:" >&2
        echo "  debian/ensure-qt.sh <arch>" >&2
        echo "Or specify the CLI Qt path with:" >&2
        echo "  $0 --qt-root=/opt/Qt/VERSION/gcc_*_cli" >&2
    else
        echo "Specify the CLI Qt path with:" >&2
        echo "  $0 --qt-root=/path/to/gcc_*_cli" >&2
    fi
    
    exit 1
fi

validate_cli_qt_dir "$QT_DIR"

# Check if Qt Version
if [ -f "$QT_DIR/bin/qmake" ]; then
    QT_VERSION=$("$QT_DIR/bin/qmake" -query QT_VERSION)
    echo "Qt version: $QT_VERSION"
fi

# Configuration
BUILD_TYPE="MinSizeRel"  # Optimize for size

# Location of AppDir and output file
APPDIR="$PWD/AppDir-cli-$ARCH"
OUTPUT_FILE="$PWD/Raspberry_Pi_Imager-${GIT_VERSION}-cli-${ARCH}.AppImage"

# Tools directory for downloaded binaries
TOOLS_DIR="$PWD/appimage-tools"
mkdir -p "$TOOLS_DIR"

# Download linuxdeploy and plugins if they don't exist
echo "Ensuring linuxdeploy tools are available..."

# Choose the right linuxdeploy tools based on architecture
if [ "$ARCH" = "x86_64" ]; then
    LINUXDEPLOY="$TOOLS_DIR/linuxdeploy-x86_64.AppImage"
    
    if [ ! -f "$LINUXDEPLOY" ]; then
        echo "Downloading linuxdeploy for x86_64..."
        curl -L -o "$LINUXDEPLOY" "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
        chmod +x "$LINUXDEPLOY"
    fi
elif [ "$ARCH" = "aarch64" ]; then
    LINUXDEPLOY="$TOOLS_DIR/linuxdeploy-aarch64.AppImage"
    
    if [ ! -f "$LINUXDEPLOY" ]; then
        echo "Downloading linuxdeploy for aarch64..."
        curl -L -o "$LINUXDEPLOY" "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-aarch64.AppImage"
        chmod +x "$LINUXDEPLOY"
    fi
elif [ "$ARCH" = "armhf" ]; then
    # Note: linuxdeploy has no armhf build; bundle libraries manually.
    echo "Warning: linuxdeploy does not support armhf, attempting manual deployment"
    LINUXDEPLOY=""
fi

# Set up build directory
BUILD_DIR="build-cli-$ARCH"

# Clean up previous builds if requested
if [ "$CLEAN_BUILD" -eq 1 ]; then
    echo "Cleaning previous build..."
    rm -rf "$APPDIR" "$BUILD_DIR"
fi

mkdir -p "$APPDIR"
mkdir -p "$BUILD_DIR"

echo "Building rpi-imager CLI-only for $ARCH..."
# Configure and build with CMake
cd "$BUILD_DIR"

# Set architecture-specific CMake flags
CMAKE_EXTRA_FLAGS=""
if [ "$ARCH" = "aarch64" ] && [ "$(uname -m)" = "x86_64" ]; then
    # Cross-compiling from x86_64 to aarch64
    echo "Cross-compiling from $(uname -m) to $ARCH"
    CMAKE_EXTRA_FLAGS="-DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64"
elif [ "$ARCH" = "armhf" ] && [ "$(uname -m)" = "x86_64" ]; then
    # Cross-compiling from x86_64 to armhf (Pi 1 / Pi 2 32-bit OS)
    echo "Cross-compiling from $(uname -m) to $ARCH"
    CMAKE_EXTRA_FLAGS="-DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=arm"
fi

# Add Qt path to CMake flags
CMAKE_EXTRA_FLAGS="$CMAKE_EXTRA_FLAGS -DQt6_ROOT=$QT_DIR"

# Build CLI-only version
CMAKE_EXTRA_FLAGS="$CMAKE_EXTRA_FLAGS -DBUILD_CLI_ONLY=ON"

# shellcheck disable=SC2086
cmake "../$SOURCE_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DCMAKE_INSTALL_PREFIX=/usr $CMAKE_EXTRA_FLAGS
make -j"$(nproc)"

echo "Creating CLI-only AppDir..."
# Install to AppDir
make DESTDIR="$APPDIR" install
cd ..

# Desktop file and icon are already installed by CMake

# Create the AppRun file for CLI operation
cat > "$APPDIR/AppRun" << 'EOF'
#!/bin/sh
HERE="$(dirname "$(readlink -f "${0}")")"

# Set up paths
export PATH="${HERE}/usr/bin:${PATH}"
export LD_LIBRARY_PATH="${HERE}/usr/lib:${LD_LIBRARY_PATH}"

# Execute the CLI-only binary directly (no --cli flag needed, it's built-in)
exec "${HERE}/usr/bin/rpi-imager-cli" "$@"
EOF
chmod +x "$APPDIR/AppRun"

# Manual Qt deployment for CLI-only (minimal)
echo "Deploying minimal Qt dependencies for CLI-only operation..."

# Copy only essential Qt libraries (CLI-only)
mkdir -p "$APPDIR/usr/lib"
cp -d "$QT_DIR/lib/libQt6Core.so"* "$APPDIR/usr/lib/"
cp -d "$QT_DIR/lib/libQt6Network.so"* "$APPDIR/usr/lib/" 2>/dev/null || true
# Note: No GUI libraries (QtGui, QtQuick, QtWidgets, QtSvg, etc.)

# Copy minimal plugins (no GUI plugins needed)
mkdir -p "$APPDIR/usr/plugins/platforms"
# Note: No platform plugins needed for CLI-only operation

# Copy only essential network plugins
mkdir -p "$APPDIR/usr/plugins"
cp -r "$QT_DIR/plugins/tls" "$APPDIR/usr/plugins/" 2>/dev/null || true

# CLI-specific optimizations
echo "Applying CLI-only optimizations..."

# Remove any GUI-related files that might have been included
rm -rf "$APPDIR/usr/qml" 2>/dev/null || true
rm -rf "$APPDIR/usr/plugins/platforms" 2>/dev/null || true
rm -rf "$APPDIR/usr/plugins/imageformats" 2>/dev/null || true
rm -rf "$APPDIR/usr/plugins/iconengines" 2>/dev/null || true
rm -rf "$APPDIR/usr/share/fonts" 2>/dev/null || true
rm -rf "$APPDIR/usr/translations" 2>/dev/null || true

# Remove GUI libraries if they somehow got included
rm -f "$APPDIR/usr/lib/libQt6Gui.so"* 2>/dev/null || true
rm -f "$APPDIR/usr/lib/libQt6Quick.so"* 2>/dev/null || true
rm -f "$APPDIR/usr/lib/libQt6Qml.so"* 2>/dev/null || true
rm -f "$APPDIR/usr/lib/libQt6Widgets.so"* 2>/dev/null || true
rm -f "$APPDIR/usr/lib/libQt6Svg.so"* 2>/dev/null || true
rm -f "$APPDIR/usr/lib/libQt"*"QuickControls"*.so* 2>/dev/null || true

# Remove development files to save space
find "$APPDIR" -name "*.a" -delete 2>/dev/null || true
find "$APPDIR" -name "*.la" -delete 2>/dev/null || true
find "$APPDIR" -name "*.prl" -delete 2>/dev/null || true

# Strip binaries to reduce size
find "$APPDIR" -type f -executable -exec strip {} \; 2>/dev/null || true

echo "Stripping shared libraries..."
SAVED_DIR="$PWD"
cd "$APPDIR"
SO_FILES=$(find . -name "*.so*" 2>/dev/null || true)
if [ -n "$SO_FILES" ]; then
    echo "$SO_FILES"
    # shellcheck disable=SC2086
    strip --strip-unneeded $SO_FILES 2>/dev/null || true
fi
cd "$SAVED_DIR"

echo "Creating CLI-only AppImage..."
# Remove old symlinks for CLI variant only
rm -f "$PWD/rpi-imager-cli.AppImage"
rm -f "$PWD/rpi-imager-cli-$ARCH.AppImage"

if [ -n "$LINUXDEPLOY" ] && [ -f "$LINUXDEPLOY" ]; then
    # Create AppImage using linuxdeploy
    # Explicitly specify the desktop file to ensure correct naming
    LD_LIBRARY_PATH="$QT_DIR/lib:$LD_LIBRARY_PATH" "$LINUXDEPLOY" --appdir="$APPDIR" \
        --desktop-file="$APPDIR/usr/share/applications/com.raspberrypi.rpi-imager-cli.desktop" \
        --output=appimage \
        --exclude-library="libwayland-*" \
        --exclude-library="libX11*" \
        --exclude-library="libxcb*" \
        --exclude-library="libXext*" \
        --exclude-library="libLLVM*" \
        --exclude-library="libgallium*" \
        --exclude-library="libXrender*" \
        --exclude-library="libGL*" \
        --exclude-library="libEGL*" \
        --exclude-library="libsystemd*"

    # Remove libsystemd if it slipped through --exclude-library
    # It must come from the host system to work with lsblk/libmount/DBus
    # (see https://github.com/raspberrypi/rpi-imager/issues/1304, #1577)
    rm -f "$APPDIR/usr/lib/libsystemd"*

    # Rename the output file from linuxdeploy's default name to our versioned name
    # linuxdeploy creates: Raspberry_Pi_Imager_(CLI)-${ARCH}.AppImage (based on Name= in desktop file)
    LINUXDEPLOY_OUTPUT="Raspberry_Pi_Imager_(CLI)-${ARCH}.AppImage"
    if [ -f "$LINUXDEPLOY_OUTPUT" ]; then
        echo "Renaming '$LINUXDEPLOY_OUTPUT' to '$(basename "$OUTPUT_FILE")'"
        mv "$LINUXDEPLOY_OUTPUT" "$OUTPUT_FILE"
    elif [ -f "$OUTPUT_FILE" ]; then
        echo "Output file already exists: $OUTPUT_FILE"
    else
        echo "Warning: Expected linuxdeploy output '$LINUXDEPLOY_OUTPUT' not found"
        echo "Looking for any matching AppImage..."
        ls -la ./*.AppImage 2>/dev/null || true
    fi
else
    # Manual AppImage creation (basic implementation)
    echo "Creating AppImage manually (basic implementation)..."
    # This is a simplified approach - for full AppImage creation,
    # you would need appimagetool or similar
    tar czf "${OUTPUT_FILE%.AppImage}.tar.gz" -C "$APPDIR" .
    echo "Created compressed archive: ${OUTPUT_FILE%.AppImage}.tar.gz"
    echo "Note: Full AppImage creation requires appimagetool for $ARCH"
fi

if [ -f "$OUTPUT_FILE" ]; then
    echo "CLI-only AppImage created at $OUTPUT_FILE"
    
    # Create symlinks for debian packaging and user convenience
    # Primary symlink matches debian/rpi-imager-cli.install expectations
    DEBIAN_SYMLINK="$PWD/rpi-imager-cli-$ARCH.AppImage"
    if [ -L "$DEBIAN_SYMLINK" ] || [ -f "$DEBIAN_SYMLINK" ]; then
        rm -f "$DEBIAN_SYMLINK"
    fi
    ln -s "$(basename "$OUTPUT_FILE")" "$DEBIAN_SYMLINK"
    echo "Created symlink: $DEBIAN_SYMLINK -> $(basename "$OUTPUT_FILE")"
    
    # Additional architecture-independent symlink for convenience
    SIMPLE_SYMLINK="$PWD/rpi-imager-cli.AppImage"
    if [ -L "$SIMPLE_SYMLINK" ] || [ -f "$SIMPLE_SYMLINK" ]; then
        rm -f "$SIMPLE_SYMLINK"
    fi
    ln -s "$(basename "$OUTPUT_FILE")" "$SIMPLE_SYMLINK"
    echo "Created symlink: $SIMPLE_SYMLINK -> $(basename "$OUTPUT_FILE")"
    
    # Show size comparison if desktop AppImage exists
    # Desktop AppImage uses GIT_VERSION and has -desktop- suffix
    REGULAR_APPIMAGE="$PWD/Raspberry_Pi_Imager-${GIT_VERSION}-desktop-${ARCH}.AppImage"
    if [ -f "$REGULAR_APPIMAGE" ]; then
        CLI_SIZE=$(stat -f%z "$OUTPUT_FILE" 2>/dev/null || stat -c%s "$OUTPUT_FILE" 2>/dev/null || echo "unknown")
        REGULAR_SIZE=$(stat -f%z "$REGULAR_APPIMAGE" 2>/dev/null || stat -c%s "$REGULAR_APPIMAGE" 2>/dev/null || echo "unknown")
        
        if [ "$CLI_SIZE" != "unknown" ] && [ "$REGULAR_SIZE" != "unknown" ]; then
            REDUCTION=$((100 - (CLI_SIZE * 100 / REGULAR_SIZE)))
            echo "Size comparison:"
            echo "  CLI AppImage: $(( CLI_SIZE / 1024 / 1024 )) MB"
            echo "  Regular AppImage: $(( REGULAR_SIZE / 1024 / 1024 )) MB"
            echo "  Size reduction: ${REDUCTION}%"
        fi
    fi
    
    echo ""
    echo "CLI-only AppImage build completed successfully for $ARCH architecture."
    echo ""
    echo "This AppImage is optimized for CLI-only operation:"
    echo "  - Automatically forces CLI mode (no GUI)"
    echo "  - Minimal Qt dependencies (Core + Network only)"
    echo "  - No QML, GUI libraries, or graphics dependencies"
    echo "  - Perfect for headless/server environments"
    echo ""
    echo "Usage examples:"
    echo "  ./$(basename "$OUTPUT_FILE") image.img /dev/sdX"
    echo "  ./$(basename "$OUTPUT_FILE") --help"
    echo "  ./$(basename "$OUTPUT_FILE") --version"
else
    echo "AppImage creation completed, but output file verification failed."
    echo "Check the build process for any errors."
fi
