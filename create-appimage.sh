#!/bin/bash
set -e

# Parse command line arguments
ARCH=$(uname -m)  # Default to current architecture
CLEAN_BUILD=1
QT_ROOT_ARG=""

usage() {
    echo "Usage: $0 [options]"
    echo "Options:"
    echo "  --arch=ARCH        Target architecture (x86_64, aarch64)"
    echo "  --qt-root=PATH     Path to Qt installation directory"
    echo "  --no-clean         Don't clean build directory"
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
MAJOR=$(grep -E "set\(IMAGER_VERSION_MAJOR [0-9]+" "$CMAKE_FILE" | sed 's/set(IMAGER_VERSION_MAJOR \([0-9]*\).*/\1/')
MINOR=$(grep -E "set\(IMAGER_VERSION_MINOR [0-9]+" "$CMAKE_FILE" | sed 's/set(IMAGER_VERSION_MINOR \([0-9]*\).*/\1/')
PATCH=$(grep -E "set\(IMAGER_VERSION_PATCH [0-9]+" "$CMAKE_FILE" | sed 's/set(IMAGER_VERSION_PATCH \([0-9]*\).*/\1/')
PROJECT_VERSION="$MAJOR.$MINOR.$PATCH"

# Extract project name (lowercase for AppImage naming convention)
PROJECT_NAME=$(grep "project(" "$CMAKE_FILE" | head -1 | sed 's/project(\([^[:space:]]*\).*/\1/' | tr '[:upper:]' '[:lower:]')

echo "Building $PROJECT_NAME version $PROJECT_VERSION"

# Check for Qt installation
# Priority: 1. Command line argument, 2. Environment variable, 3. Auto-detection
QT_VERSION=""
QT_DIR=""

# Check if Qt root is specified via command line argument (highest priority)
if [ -n "$QT_ROOT_ARG" ]; then
    echo "Using Qt from command line argument: $QT_ROOT_ARG"
    QT_DIR="$QT_ROOT_ARG"
    # Try to determine the version if possible
    if [ -f "$QT_DIR/bin/qmake" ]; then
        QT_VERSION=$("$QT_DIR/bin/qmake" -query QT_VERSION)
        echo "Qt version: $QT_VERSION"
    fi
# Check if Qt6_ROOT is explicitly set in environment
elif [ -n "$Qt6_ROOT" ]; then
    echo "Using Qt from Qt6_ROOT environment variable: $Qt6_ROOT"
    QT_DIR="$Qt6_ROOT"
    # Try to determine the version if possible
    if [ -f "$QT_DIR/bin/qmake" ]; then
        QT_VERSION=$("$QT_DIR/bin/qmake" -query QT_VERSION)
        echo "Qt version: $QT_VERSION"
    fi
# Auto-detect Qt installation in /opt/Qt
else
    if [ -d "/opt/Qt" ]; then
        echo "Checking for Qt installations in /opt/Qt..."
        # Find the newest Qt6 version installed
        NEWEST_QT=$(find /opt/Qt -maxdepth 1 -type d -name "6.*" | sort -V | tail -n 1)
        if [ -n "$NEWEST_QT" ]; then
            QT_VERSION=$(basename "$NEWEST_QT")
            
            # Find appropriate compiler directory for the architecture
            if [ "$ARCH" = "x86_64" ]; then
                if [ -d "$NEWEST_QT/gcc_64" ]; then
                    QT_DIR="$NEWEST_QT/gcc_64"
                fi
            elif [ "$ARCH" = "aarch64" ]; then
                if [ -d "$NEWEST_QT/gcc_arm64" ]; then
                    QT_DIR="$NEWEST_QT/gcc_arm64"
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

# If Qt not found, suggest running build-qt.sh
if [ -z "$QT_DIR" ]; then
    echo "Error: No suitable Qt installation found for $ARCH"
    
    if [ -f "./build-qt.sh" ]; then
        echo "You can build Qt using the provided script:"
        echo "  ./build-qt.sh --version=6.9.0"
        echo "Or specify the Qt location with:"
        echo "  $0 --qt-root=/path/to/qt"
        echo "  export Qt6_ROOT=/path/to/qt"
    else
        echo "You can specify the Qt location with:"
        echo "  $0 --qt-root=/path/to/qt"
        echo "  export Qt6_ROOT=/path/to/qt"
    fi
    
    exit 1
fi

# Configuration
BUILD_TYPE="MinSizeRel"
QML_SOURCES_PATH="$PWD/src/qmlcomponents/"

# Location of AppDir and output file
APPDIR="$PWD/AppDir-$ARCH"
OUTPUT_FILE="$PWD/Raspberry_Pi_Imager-${PROJECT_VERSION}-${ARCH}.AppImage"

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

# Add Qt path to CMake flags
CMAKE_EXTRA_FLAGS="$CMAKE_EXTRA_FLAGS -DQt6_ROOT=$QT_DIR"

# shellcheck disable=SC2086
cmake "../$SOURCE_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DCMAKE_INSTALL_PREFIX=/usr $CMAKE_EXTRA_FLAGS
make -j$(nproc)

echo "Creating AppDir..."
# Install to AppDir
make DESTDIR="$APPDIR" install
cd ..

# Copy the desktop file from debian directory
if [ ! -f "$APPDIR/usr/share/applications/org.raspberrypi.rpi-imager.desktop" ]; then
    mkdir -p "$APPDIR/usr/share/applications"
    cp "debian/org.raspberrypi.rpi-imager.desktop" "$APPDIR/usr/share/applications/"
    # Update the Exec line to match the AppImage requirements
    sed -i 's|Exec=.*|Exec=rpi-imager|' "$APPDIR/usr/share/applications/org.raspberrypi.rpi-imager.desktop"
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
echo "Deploying Qt dependencies using $QT_DIR..."
export QML_SOURCES_PATHS="$QML_SOURCES_PATH"
# Enable FUSE to run the AppImages without extraction
export APPIMAGE_EXTRACT_AND_RUN=1
# Set Qt path for linuxdeploy-plugin-qt
export QMAKE="$QT_DIR/bin/qmake"
# Set LD_LIBRARY_PATH to include Qt libraries
export LD_LIBRARY_PATH="$QT_DIR/lib:$LD_LIBRARY_PATH"
"$LINUXDEPLOY" --appdir="$APPDIR" --plugin=qt --exclude-library="libwayland-*"

# Hook for removing files before AppImage creation
echo "Pre-packaging hook - opportunity to remove unwanted files"
# Add your file removal commands here, for example:
# rm -rf "$APPDIR/usr/lib/libunwanted.so"
rm -rf "$APPDIR/usr/qml/QtQuick/Controls/Universal"
rm -rf "$APPDIR/usr/qml/QtQuick/Controls/Fusion"
rm -rf "$APPDIR/usr/qml/QtQuick/Controls/Imagine"
rm -rf "$APPDIR/usr/qml/QtQuick/Controls/FluentWinUI3"

# Create the AppImage
echo "Creating AppImage..."
# Remove old AppImage symlink
rm -f "$PWD/rpi-imager.AppImage"
# Ensure LD_LIBRARY_PATH is still set for this call too
"$LINUXDEPLOY" --appdir="$APPDIR" --output=appimage

# Rename the output file if needed
for appimage in *.AppImage; do
    if [ "$PWD/$appimage" != "$OUTPUT_FILE" ]; then
        mv "$appimage" "$OUTPUT_FILE"
    fi
done

echo "AppImage created at $OUTPUT_FILE"

# Create a symlink with an architecture-specific name
SYMLINK_NAME="$PWD/rpi-imager-$ARCH.AppImage"
if [ -L "$SYMLINK_NAME" ] || [ -f "$SYMLINK_NAME" ]; then
    rm -f "$SYMLINK_NAME"
fi
ln -s "$(basename "$OUTPUT_FILE")" "$SYMLINK_NAME"
echo "Created symlink: $SYMLINK_NAME -> $(basename "$OUTPUT_FILE")"

echo "Build completed successfully for $ARCH architecture." 
