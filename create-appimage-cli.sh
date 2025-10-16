#!/bin/bash
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
    echo "  --arch=ARCH            Target architecture (x86_64, aarch64, armv7l)"
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

# Validate architecture
if [[ "$ARCH" != "x86_64" && "$ARCH" != "aarch64" && "$ARCH" != "armv7l" ]]; then
    echo "Error: Architecture must be one of: x86_64, aarch64, armv7l"
    exit 1
fi

echo "Building CLI-only AppImage for architecture: $ARCH"

# Extract project information from CMakeLists.txt
SOURCE_DIR="src/"
CMAKE_FILE="${SOURCE_DIR}CMakeLists.txt"

# Extract version components
MAJOR=$(grep -E "set\(IMAGER_VERSION_MAJOR [0-9]+" "$CMAKE_FILE" | sed 's/set(IMAGER_VERSION_MAJOR \([0-9]*\).*/\1/')
MINOR=$(grep -E "set\(IMAGER_VERSION_MINOR [0-9]+" "$CMAKE_FILE" | sed 's/set(IMAGER_VERSION_MINOR \([0-9]*\).*/\1/')
PATCH=$(grep -E "set\(IMAGER_VERSION_PATCH [0-9]+" "$CMAKE_FILE" | sed 's/set(IMAGER_VERSION_PATCH \([0-9]*\).*/\1/')
PROJECT_VERSION="$MAJOR.$MINOR.$PATCH"

# Extract project name (lowercase for AppImage naming convention)
PROJECT_NAME=$(grep "project(" "$CMAKE_FILE" | head -1 | sed 's/project(\([^[:space:]]*\).*/\1/' | tr '[:upper:]' '[:lower:]')

echo "Building $PROJECT_NAME version $PROJECT_VERSION for CLI-only operation"

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
# Auto-detect Qt installation in /opt/Qt (look for CLI-specific builds first)
else
    if [ -d "/opt/Qt" ]; then
        echo "Checking for Qt installations in /opt/Qt..."
        # Find the newest Qt6 version installed
        NEWEST_QT=$(find /opt/Qt -maxdepth 1 -type d -name "6.*" | sort -V | tail -n 1)
        if [ -n "$NEWEST_QT" ]; then
            QT_VERSION=$(basename "$NEWEST_QT")
            
            # Find appropriate compiler directory for the architecture
            # Priority: CLI-specific builds, then regular builds
            if [ "$ARCH" = "x86_64" ]; then
                if [ -d "$NEWEST_QT/gcc_64_cli" ]; then
                    QT_DIR="$NEWEST_QT/gcc_64_cli"
                    echo "Found CLI-optimized Qt build"
                elif [ -d "$NEWEST_QT/gcc_64" ]; then
                    QT_DIR="$NEWEST_QT/gcc_64"
                    echo "Using regular Qt build (consider building CLI-optimized version)"
                fi
            elif [ "$ARCH" = "aarch64" ]; then
                if [ -d "$NEWEST_QT/gcc_arm64_cli" ]; then
                    QT_DIR="$NEWEST_QT/gcc_arm64_cli"
                    echo "Found CLI-optimized Qt build"
                elif [ -d "$NEWEST_QT/gcc_arm64" ]; then
                    QT_DIR="$NEWEST_QT/gcc_arm64"
                    echo "Using regular Qt build (consider building CLI-optimized version)"
                fi
            elif [ "$ARCH" = "armv7l" ]; then
                if [ -d "$NEWEST_QT/gcc_arm32_cli" ]; then
                    QT_DIR="$NEWEST_QT/gcc_arm32_cli"
                    echo "Found CLI-optimized Qt build"
                elif [ -d "$NEWEST_QT/gcc_arm32" ]; then
                    QT_DIR="$NEWEST_QT/gcc_arm32"
                    echo "Using regular Qt build (consider building CLI-optimized version)"
                fi
            fi
            
            if [ -n "$QT_DIR" ]; then
                echo "Found Qt $QT_VERSION for $ARCH at $QT_DIR"
            else
                echo "Found Qt $QT_VERSION, but no binary directory for $ARCH"
                QT_VERSION=""
            fi
        fi
    fi
fi

# If Qt not found, suggest building it
if [ -z "$QT_DIR" ]; then
    echo "Error: No suitable Qt installation found for $ARCH"
    
    if [ -f "./qt/build-qt-cli.sh" ]; then
        echo "You can build a CLI-optimized Qt using:"
        echo "  ./qt/build-qt-cli.sh --version=6.9.1"
        echo "Or specify the Qt location with:"
        echo "  $0 --qt-root=/path/to/qt"
    else
        echo "You can specify the Qt location with:"
        echo "  $0 --qt-root=/path/to/qt"
    fi
    
    exit 1
fi

# Check if Qt Version
if [ -f "$QT_DIR/bin/qmake" ]; then
    QT_VERSION=$("$QT_DIR/bin/qmake" -query QT_VERSION)
    echo "Qt version: $QT_VERSION"
fi

# Configuration
BUILD_TYPE="MinSizeRel"  # Optimize for size

# Location of AppDir and output file
APPDIR="$PWD/AppDir-cli-$ARCH"
OUTPUT_FILE="$PWD/Raspberry_Pi_Imager-${PROJECT_VERSION}-cli-${ARCH}.AppImage"

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
elif [ "$ARCH" = "armv7l" ]; then
    # Note: linuxdeploy may not have armv7l builds, fallback to manual deployment
    echo "Warning: linuxdeploy may not support armv7l, attempting manual deployment"
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
elif [ "$ARCH" = "armv7l" ] && [ "$(uname -m)" = "x86_64" ]; then
    # Cross-compiling from x86_64 to armv7l
    echo "Cross-compiling from $(uname -m) to $ARCH"
    CMAKE_EXTRA_FLAGS="-DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=arm"
fi

# Add Qt path to CMake flags
CMAKE_EXTRA_FLAGS="$CMAKE_EXTRA_FLAGS -DQt6_ROOT=$QT_DIR"

# Build CLI-only version
CMAKE_EXTRA_FLAGS="$CMAKE_EXTRA_FLAGS -DBUILD_CLI_ONLY=ON"

# shellcheck disable=SC2086
cmake "../$SOURCE_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DCMAKE_INSTALL_PREFIX=/usr $CMAKE_EXTRA_FLAGS
make -j$(nproc)

echo "Creating CLI-only AppDir..."
# Install to AppDir
make DESTDIR="$APPDIR" install
cd ..

# Desktop file is already installed by CMake from debian/com.raspberrypi.rpi-imager-cli.desktop
# No need to create it here

# Copy the icon for AppImage tools (required even though NoDisplay=true)
mkdir -p "$APPDIR/usr/share/icons/hicolor/128x128/apps"
cp "debian/rpi-imager.png" "$APPDIR/usr/share/icons/hicolor/128x128/apps/com.raspberrypi.rpi-imager.png"

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
pushd "$APPDIR"
if [ -n "$(find . -name "*.so*")" ]; then
    echo $(find . -name "*.so*")
    strip --strip-unneeded $(find . -name "*.so*")
fi
popd

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
        --exclude-library="libEGL*"
    
    # Rename the output file
    # Find and rename the AppImage created by linuxdeploy to our standardized name
    RENAMED=false
    for appimage in *.AppImage; do
        # Skip symlinks
        if [ -L "$appimage" ]; then
            continue
        fi
        # Skip if this is already our target file
        if [ "$appimage" = "$(basename "$OUTPUT_FILE")" ]; then
            RENAMED=true
            break
        fi
        # Only rename if this contains "CLI" (linuxdeploy creates Raspberry_Pi_Imager_(CLI)-arch.AppImage)
        if [[ "$appimage" == *"CLI"* ]] && [[ "$appimage" == *"${ARCH}"* ]]; then
            echo "Renaming '$appimage' to '$(basename "$OUTPUT_FILE")'"
            mv "$appimage" "$OUTPUT_FILE"
            RENAMED=true
            break
        fi
    done
    
    # Check if we successfully found/created the output file
    if [ "$RENAMED" = false ] && [ ! -f "$OUTPUT_FILE" ]; then
        echo "Warning: Could not find CLI AppImage to rename. Looking for any matching AppImage..."
        ls -la *.AppImage 2>/dev/null || true
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
    
    # Show size comparison if regular AppImage exists
    REGULAR_APPIMAGE="$PWD/Raspberry_Pi_Imager-${PROJECT_VERSION}-${ARCH}.AppImage"
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
