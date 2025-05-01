#!/bin/bash
set -e

# Parse command line arguments
ARCH=$(uname -m)  # Default to current architecture
CLEAN_BUILD=1

usage() {
    echo "Usage: $0 [options]"
    echo "Options:"
    echo "  --arch=ARCH    Target architecture (x86_64, aarch64)"
    echo "  --no-clean     Don't clean build directory"
    echo "  -h, --help     Show this help message"
    exit 1
}

for arg in "$@"; do
    case $arg in
        --arch=*)
            ARCH="${arg#*=}"
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
if [[ "$ARCH" != "x86_64" && "$ARCH" != "aarch64" ]]; then
    echo "Error: Architecture must be one of: x86_64, aarch64"
    exit 1
fi

echo "Building for architecture: $ARCH"

# Extract project information from CMakeLists.txt
SOURCE_DIR="src/"
CMAKE_FILE="${SOURCE_DIR}CMakeLists.txt"

# Extract version components
MAJOR=$(grep "IMAGER_VERSION_MAJOR" "$CMAKE_FILE" | head -1 | sed 's/.*IMAGER_VERSION_MAJOR *\([0-9]*\).*/\1/')
MINOR=$(grep "IMAGER_VERSION_MINOR" "$CMAKE_FILE" | head -1 | sed 's/.*IMAGER_VERSION_MINOR *\([0-9]*\).*/\1/')
PATCH=$(grep "IMAGER_VERSION_PATCH" "$CMAKE_FILE" | head -1 | sed 's/.*IMAGER_VERSION_PATCH *\([0-9]*\).*/\1/')
PROJECT_VERSION="$MAJOR.$MINOR.$PATCH"

# Extract project name (lowercase for AppImage naming convention)
PROJECT_NAME=$(grep "project(" "$CMAKE_FILE" | head -1 | sed 's/project(\([^[:space:]]*\).*/\1/' | tr '[:upper:]' '[:lower:]')
PROJECT_NAME="org.raspberrypi.$PROJECT_NAME"

echo "Building $PROJECT_NAME version $PROJECT_VERSION"

# Configuration
BUILD_TYPE="MinSizeRel"
QML_SOURCES_PATH="$PWD/src/qmlcomponents/"

# Location of AppDir and output file
APPDIR="$PWD/AppDir-$ARCH"
OUTPUT_FILE="$PWD/${PROJECT_NAME}-${PROJECT_VERSION}-${ARCH}.AppImage"

# Tools directory for downloaded binaries
TOOLS_DIR="$PWD/appimage-tools"
mkdir -p "$TOOLS_DIR"

# Download linuxdeploy and plugins if they don't exist
echo "Ensuring linuxdeploy tools are available..."

# Choose the right linuxdeploy tools based on architecture
if [ "$ARCH" = "x86_64" ]; then
    LINUXDEPLOY="$TOOLS_DIR/linuxdeploy-x86_64.AppImage"
    LINUXDEPLOY_QT="$TOOLS_DIR/linuxdeploy-plugin-qt-x86_64.AppImage"
    
    if [ ! -f "$LINUXDEPLOY" ]; then
        echo "Downloading linuxdeploy for x86_64..."
        curl -L -o "$LINUXDEPLOY" "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
        chmod +x "$LINUXDEPLOY"
    fi
    
    if [ ! -f "$LINUXDEPLOY_QT" ]; then
        echo "Downloading linuxdeploy-plugin-qt for x86_64..."
        curl -L -o "$LINUXDEPLOY_QT" "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage"
        chmod +x "$LINUXDEPLOY_QT"
    fi
elif [ "$ARCH" = "aarch64" ]; then
    LINUXDEPLOY="$TOOLS_DIR/linuxdeploy-aarch64.AppImage"
    LINUXDEPLOY_QT="$TOOLS_DIR/linuxdeploy-plugin-qt-aarch64.AppImage"
    
    if [ ! -f "$LINUXDEPLOY" ]; then
        echo "Downloading linuxdeploy for aarch64..."
        curl -L -o "$LINUXDEPLOY" "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-aarch64.AppImage"
        chmod +x "$LINUXDEPLOY"
    fi
    
    if [ ! -f "$LINUXDEPLOY_QT" ]; then
        echo "Downloading linuxdeploy-plugin-qt for aarch64..."
        curl -L -o "$LINUXDEPLOY_QT" "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-aarch64.AppImage"
        chmod +x "$LINUXDEPLOY_QT"
    fi
fi

# Set up build directory
BUILD_DIR="build-$ARCH"

# Clean up previous builds if requested
if [ "$CLEAN_BUILD" -eq 1 ]; then
    echo "Cleaning previous build..."
    rm -rf "$APPDIR" "$BUILD_DIR"
fi

mkdir -p "$APPDIR"
mkdir -p "$BUILD_DIR"

echo "Building rpi-imager for $ARCH..."
# Configure and build with CMake
cd "$BUILD_DIR"

# Set architecture-specific CMake flags
CMAKE_EXTRA_FLAGS=""
if [ "$ARCH" = "aarch64" ] && [ "$(uname -m)" = "x86_64" ]; then
    # Cross-compiling from x86_64 to aarch64
    echo "Cross-compiling from $(uname -m) to $ARCH"
    # You may need to adjust these flags depending on your cross-compilation setup
    CMAKE_EXTRA_FLAGS="-DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64"
fi

# shellcheck disable=SC2086
cmake "../$SOURCE_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DCMAKE_INSTALL_PREFIX=/usr $CMAKE_EXTRA_FLAGS
make -j$(nproc)

echo "Creating AppDir..."
# Install to AppDir
make DESTDIR="$APPDIR" install
cd ..

# Copy the desktop file from debian directory
if [ ! -f "$APPDIR/usr/share/applications/$PROJECT_NAME.desktop" ]; then
    mkdir -p "$APPDIR/usr/share/applications"
    cp "debian/$PROJECT_NAME.desktop" "$APPDIR/usr/share/applications/"
    # Update the Exec line to match the AppImage requirements
    sed -i 's|Exec=.*|Exec=rpi-imager|' "$APPDIR/usr/share/applications/$PROJECT_NAME.desktop"
fi

# Create the AppRun file if not created by the install process
if [ ! -f "$APPDIR/AppRun" ]; then
    cat > "$APPDIR/AppRun" << 'EOF'
#!/bin/bash
HERE="$(dirname "$(readlink -f "${0}")")"
export PATH="${HERE}/usr/bin:${PATH}"
export LD_LIBRARY_PATH="${HERE}/usr/lib:${LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="${HERE}/usr/plugins"
export QML_IMPORT_PATH="${HERE}/usr/qml"
export QT_QPA_PLATFORM_PLUGIN_PATH="${HERE}/usr/plugins/platforms"
exec "${HERE}/usr/bin/rpi-imager" "$@"
EOF
    chmod +x "$APPDIR/AppRun"
fi

# Deploy Qt dependencies
echo "Deploying Qt dependencies..."
export QML_SOURCES_PATHS="$QML_SOURCES_PATH"
# Enable FUSE to run the AppImages without extraction
export APPIMAGE_EXTRACT_AND_RUN=1
"$LINUXDEPLOY" --appdir="$APPDIR" --plugin=qt --exclude-library="libwayland-*"

# Hook for removing files before AppImage creation
echo "Pre-packaging hook - opportunity to remove unwanted files"
# Add your file removal commands here, for example:
# rm -rf "$APPDIR/usr/lib/libunwanted.so"

# Create the AppImage
echo "Creating AppImage..."
"$LINUXDEPLOY" --appdir="$APPDIR" --output=appimage

# Rename the output file if needed
for appimage in *.AppImage; do
    if [ "$appimage" != "$OUTPUT_FILE" ] && [[ "$appimage" == *".AppImage" ]]; then
        mv "$appimage" "$OUTPUT_FILE"
    fi
done

echo "AppImage created at $OUTPUT_FILE"
echo "Build completed successfully for $ARCH architecture." 