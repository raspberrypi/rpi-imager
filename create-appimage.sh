#!/bin/sh
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
if [ "$ARCH" != "x86_64" ] && [ "$ARCH" != "aarch64" ]; then
    echo "Error: Architecture must be one of: x86_64, aarch64"
    exit 1
fi

echo "Building for architecture: $ARCH"

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

echo "Building $PROJECT_NAME version $GIT_VERSION (numeric: $PROJECT_VERSION)"

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
    
    if [ -f "./qt/build-qt.sh" ]; then
        echo "You can build Qt using the provided script:"
        echo "  ./qt/build-qt.sh --version=6.9.1"
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
OUTPUT_FILE="$PWD/Raspberry_Pi_Imager-${GIT_VERSION}-desktop-${ARCH}.AppImage"

# Tools directory for downloaded binaries
TOOLS_DIR="$PWD/appimage-tools"
mkdir -p "$TOOLS_DIR"

# Download linuxdeploy and plugins if they don't exist
echo "Ensuring linuxdeploy tools are available..."

# Pin to specific stable versions
LINUXDEPLOY_VERSION="1-alpha-20250213-2"
LINUXDEPLOY_PLUGIN_QT_VERSION="1-alpha-20250213-1"

# Choose the right linuxdeploy tools based on architecture
if [ "$ARCH" = "x86_64" ]; then
    LINUXDEPLOY="$TOOLS_DIR/linuxdeploy-x86_64.AppImage"
    LINUXDEPLOY_QT="$TOOLS_DIR/linuxdeploy-plugin-qt-x86_64.AppImage"
    
    if [ ! -f "$LINUXDEPLOY" ]; then
        echo "Downloading linuxdeploy $LINUXDEPLOY_VERSION for x86_64..."
        curl -L -o "$LINUXDEPLOY" "https://github.com/linuxdeploy/linuxdeploy/releases/download/$LINUXDEPLOY_VERSION/linuxdeploy-x86_64.AppImage"
        chmod +x "$LINUXDEPLOY"
    fi
    
    if [ ! -f "$LINUXDEPLOY_QT" ]; then
        echo "Downloading linuxdeploy-plugin-qt $LINUXDEPLOY_PLUGIN_QT_VERSION for x86_64..."
        curl -L -o "$LINUXDEPLOY_QT" "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/$LINUXDEPLOY_PLUGIN_QT_VERSION/linuxdeploy-plugin-qt-x86_64.AppImage"
        chmod +x "$LINUXDEPLOY_QT"
    fi
elif [ "$ARCH" = "aarch64" ]; then
    LINUXDEPLOY="$TOOLS_DIR/linuxdeploy-aarch64.AppImage"
    LINUXDEPLOY_QT="$TOOLS_DIR/linuxdeploy-plugin-qt-aarch64.AppImage"
    
    if [ ! -f "$LINUXDEPLOY" ]; then
        echo "Downloading linuxdeploy $LINUXDEPLOY_VERSION for aarch64..."
        curl -L -o "$LINUXDEPLOY" "https://github.com/linuxdeploy/linuxdeploy/releases/download/$LINUXDEPLOY_VERSION/linuxdeploy-aarch64.AppImage"
        chmod +x "$LINUXDEPLOY"
    fi
    
    if [ ! -f "$LINUXDEPLOY_QT" ]; then
        echo "Downloading linuxdeploy-plugin-qt $LINUXDEPLOY_PLUGIN_QT_VERSION for aarch64..."
        curl -L -o "$LINUXDEPLOY_QT" "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/$LINUXDEPLOY_PLUGIN_QT_VERSION/linuxdeploy-plugin-qt-aarch64.AppImage"
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
make -j"$(nproc)"

echo "Creating AppDir..."
# Install to AppDir
make DESTDIR="$APPDIR" install
cd ..

# Copy the desktop file from debian directory
if [ ! -f "$APPDIR/usr/share/applications/com.raspberrypi.rpi-imager.desktop" ]; then
    mkdir -p "$APPDIR/usr/share/applications"
    cp "debian/com.raspberrypi.rpi-imager.desktop" "$APPDIR/usr/share/applications/"
    # Update the Exec line to match the AppImage requirements (preserve %F for file arguments)
    sed -i 's|Exec=.*|Exec=rpi-imager %F|' "$APPDIR/usr/share/applications/com.raspberrypi.rpi-imager.desktop"
fi

# Create the AppRun file if not created by the install process
if [ ! -f "$APPDIR/AppRun" ]; then
    cat > "$APPDIR/AppRun" << 'EOF'
#!/bin/sh
HERE="$(dirname "$(readlink -f "${0}")")"
export PATH="${HERE}/usr/bin:${PATH}"
export LD_LIBRARY_PATH="${HERE}/usr/lib:${LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="${HERE}/usr/plugins"
export QML_IMPORT_PATH="${HERE}/usr/qml"
export QT_QPA_PLATFORM_PLUGIN_PATH="${HERE}/usr/plugins/platforms"

# Handle X11 authorization when running as root (via sudo or pkexec)
# This fixes "Authorization required, but no authorization protocol specified" errors
if [ "$(id -u)" = "0" ]; then
    # Determine original user from sudo or pkexec
    ORIGINAL_USER=""
    ORIGINAL_UID=""
    ORIGINAL_HOME=""
    
    if [ -n "$SUDO_USER" ]; then
        ORIGINAL_USER="$SUDO_USER"
        ORIGINAL_UID="$SUDO_UID"
        ORIGINAL_HOME=$(getent passwd "$SUDO_USER" | cut -d: -f6)
    elif [ -n "$PKEXEC_UID" ]; then
        ORIGINAL_UID="$PKEXEC_UID"
        ORIGINAL_USER=$(getent passwd "$PKEXEC_UID" | cut -d: -f1)
        ORIGINAL_HOME=$(getent passwd "$PKEXEC_UID" | cut -d: -f6)
    fi
    
    if [ -n "$ORIGINAL_USER" ] && [ -n "$ORIGINAL_HOME" ]; then
        # Try to grant root access to the X display using xhost
        # This must be run as the original user who owns the display
        if command -v xhost >/dev/null 2>&1; then
            if [ -n "$DISPLAY" ]; then
                # Run xhost as the original user to grant root access
                su "$ORIGINAL_USER" -c "xhost +local:root" >/dev/null 2>&1 || true
            fi
        fi
        
        # Set XAUTHORITY to the original user's .Xauthority if not already set
        if [ -z "$XAUTHORITY" ]; then
            if [ -f "$ORIGINAL_HOME/.Xauthority" ]; then
                export XAUTHORITY="$ORIGINAL_HOME/.Xauthority"
            fi
        fi
        
        # Ensure DISPLAY is set (for pkexec which may not preserve it)
        if [ -z "$DISPLAY" ]; then
            # Try common X11 display socket
            if [ -S "/tmp/.X11-unix/X0" ]; then
                export DISPLAY=":0"
            fi
        fi
    fi
fi

# The binary handles privilege elevation internally via pkexec if needed
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
# Set library paths to include Qt libraries (both runtime and linker search paths)
export LD_LIBRARY_PATH="$QT_DIR/lib:$LD_LIBRARY_PATH"
# Optimize deployment: exclude translations and unnecessary libraries
export LINUXDEPLOY_PLUGIN_QT_IGNORE_GLOB="*/translations/*"
"$LINUXDEPLOY" --appdir="$APPDIR" --plugin=qt --exclude-library="libwayland-*" --verbosity=0

# Hook for removing files before AppImage creation
echo "Pre-packaging hook - opportunity to remove unwanted files"

# Remove unused QML Controls themes (size optimization)
rm -rf "$APPDIR/usr/qml/QtQuick/Controls/Universal"
rm -rf "$APPDIR/usr/qml/QtQuick/Controls/Fusion"
rm -rf "$APPDIR/usr/qml/QtQuick/Controls/Imagine"
rm -rf "$APPDIR/usr/qml/QtQuick/Controls/FluentWinUI3"

# Remove QtWidgets if included (we don't use it)
rm -f "$APPDIR/usr/lib/libQt6Widgets.so"*
rm -f "$APPDIR/usr/lib/libQt"*"Widgets.so"*

# Remove QML debugging tools (development-only)
rm -rf "$APPDIR/usr/qml/QtTest"*
rm -rf "$APPDIR/usr/plugins/qmltooling"

# Remove Qt translations (we excluded them but remove any that might have slipped through)
rm -rf "$APPDIR/usr/translations"
rm -rf "$APPDIR/usr/share/qt6/translations"

# Remove unnecessary image format plugins (consistency with all platforms)
# Excludes: TIFF, WebP, GIF (less common formats)
# Keeps: JPEG, PNG, SVG (common formats + icons)
rm -f "$APPDIR/usr/plugins/imageformats/libqtiff.so"
rm -f "$APPDIR/usr/plugins/imageformats/libqwebp.so"
rm -f "$APPDIR/usr/plugins/imageformats/libqgif.so"

# Remove unused Qt Quick Controls 2 style libraries (size optimization)
rm -f "$APPDIR/usr/lib/libQt6QuickControls2Fusion.so"*
rm -f "$APPDIR/usr/lib/libQt6QuickControls2Universal.so"*
rm -f "$APPDIR/usr/lib/libQt6QuickControls2Imagine.so"*
rm -f "$APPDIR/usr/lib/libQt6QuickControls2FluentWinUI3.so"*
rm -f "$APPDIR/usr/lib/libQt6QuickControls2FusionStyleImpl.so"*
rm -f "$APPDIR/usr/lib/libQt6QuickControls2UniversalStyleImpl.so"*
rm -f "$APPDIR/usr/lib/libQt6QuickControls2ImagineStyleImpl.so"*
rm -f "$APPDIR/usr/lib/libQt6QuickControls2FluentWinUI3StyleImpl.so"*
rm -f "$APPDIR/usr/lib/libQt6QuickControls2WindowsStyleImpl.so"*

# Create the AppImage
echo "Creating AppImage..."
# Remove old symlinks for this variant only
rm -f "$PWD/rpi-imager-desktop-$ARCH.AppImage"
rm -f "$PWD/rpi-imager-$ARCH.AppImage"  # Legacy symlink name
# Ensure LD_LIBRARY_PATH is still set for this call too
export LD_LIBRARY_PATH="$QT_DIR/lib:$LD_LIBRARY_PATH"
# Explicitly specify the desktop file to ensure correct naming
"$LINUXDEPLOY" --appdir="$APPDIR" \
    --desktop-file="$APPDIR/usr/share/applications/com.raspberrypi.rpi-imager.desktop" \
    --output=appimage \
    --verbosity=0

# Rename the output file if needed
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
    # Skip if this is a CLI or embedded variant (has those keywords in the name)
    case "$appimage" in
        *"CLI"*|*"Embedded"*)
            continue
            ;;
    esac
    # Check if this matches the expected linuxdeploy output pattern
    case "$appimage" in
        Raspberry_Pi_Imager-*"-${ARCH}.AppImage"|Raspberry_Pi_Imager"-${ARCH}.AppImage")
            echo "Renaming '$appimage' to '$(basename "$OUTPUT_FILE")'"
            mv "$appimage" "$OUTPUT_FILE"
            RENAMED=true
            break
            ;;
    esac
done

# Check if we successfully found/created the output file
if [ "$RENAMED" = "false" ] && [ ! -f "$OUTPUT_FILE" ]; then
    echo "Warning: Could not find AppImage to rename. Looking for any matching AppImage..."
    ls -la ./*.AppImage 2>/dev/null || true
fi

echo "AppImage created at $OUTPUT_FILE"

# Create symlinks for debian packaging and user convenience
# Primary symlink matches debian/rpi-imager.install expectations
DEBIAN_SYMLINK="$PWD/rpi-imager-$ARCH.AppImage"
if [ -L "$DEBIAN_SYMLINK" ] || [ -f "$DEBIAN_SYMLINK" ]; then
    rm -f "$DEBIAN_SYMLINK"
fi
ln -s "$(basename "$OUTPUT_FILE")" "$DEBIAN_SYMLINK"
echo "Created symlink: $DEBIAN_SYMLINK -> $(basename "$OUTPUT_FILE")"

# Additional descriptive symlink for clarity when multiple variants exist
DESCRIPTIVE_SYMLINK="$PWD/rpi-imager-desktop-$ARCH.AppImage"
if [ -L "$DESCRIPTIVE_SYMLINK" ] || [ -f "$DESCRIPTIVE_SYMLINK" ]; then
    rm -f "$DESCRIPTIVE_SYMLINK"
fi
ln -s "$(basename "$OUTPUT_FILE")" "$DESCRIPTIVE_SYMLINK"
echo "Created symlink: $DESCRIPTIVE_SYMLINK -> $(basename "$OUTPUT_FILE")"

echo "Build completed successfully for $ARCH architecture."
