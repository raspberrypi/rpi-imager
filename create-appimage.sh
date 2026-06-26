#!/bin/sh
set -e

# Parse command line arguments
ARCH=$(uname -m)  # Default to current architecture
CLEAN_BUILD=1
QT_ROOT_ARG=""
TRY_BUILD_QT=0

usage() {
    echo "Usage: $0 [options]"
    echo "Options:"
    echo "  --arch=ARCH        Target architecture (x86_64, aarch64, armhf)"
    echo "  --qt-root=PATH     Path to Qt installation directory"
    echo "  --try-build-qt     Run debian/ensure-qt.sh if Qt is missing (best-effort)"
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
        --try-build-qt)
            TRY_BUILD_QT=1
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

# Normalise 32-bit Pi kernel names to Debian armhf.
case "$ARCH" in
    armv6l|armv7l) ARCH=armhf ;;
esac
if [ "$ARCH" != "x86_64" ] && [ "$ARCH" != "aarch64" ] && [ "$ARCH" != "armhf" ]; then
    echo "Error: Architecture must be one of: x86_64, aarch64, armhf" >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=debian/qt-resolve.sh
. "$SCRIPT_DIR/debian/qt-resolve.sh"

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

# Resolve Qt (vendored cache, /opt/Qt, or system qmake6).
QT_DIR=""
if ! QT_DIR=$(qt_resolve_desktop_dir "$ARCH"); then
    if [ "$TRY_BUILD_QT" -eq 1 ] && [ -x "$SCRIPT_DIR/debian/ensure-qt.sh" ]; then
        _deb_arch=$ARCH
        case "$ARCH" in
            x86_64) _deb_arch=amd64 ;;
            aarch64) _deb_arch=arm64 ;;
        esac
        echo "create-appimage: attempting Qt build via ensure-qt.sh $_deb_arch..."
        if "$SCRIPT_DIR/debian/ensure-qt.sh" "$_deb_arch"; then
            QT_DIR=$(qt_resolve_desktop_dir "$ARCH") || true
        fi
    fi
fi

if [ -z "$QT_DIR" ]; then
    echo "Error: No suitable Qt6 installation found for $ARCH" >&2
    echo "  Vendored: debian/ensure-qt.sh <arch>  or  ./qt/build-qt.sh" >&2
    echo "  System:   apt install qt6-base-dev qt6-declarative-dev" >&2
    echo "  Or:       $0 --qt-root=/path/to/qt6  --try-build-qt" >&2
    exit 1
fi

if [ -f "$QT_DIR/bin/qmake" ]; then
    QT_VERSION=$("$QT_DIR/bin/qmake" -query QT_VERSION)
    echo "Using Qt $QT_VERSION from $QT_DIR (source: ${QT_RESOLVE_SOURCE:-unknown})"
elif _qmake=$(qt6_qmake); then
    QT_VERSION=$("$_qmake" -query QT_VERSION)
    echo "Using system Qt $QT_VERSION (prefix $QT_DIR)"
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
elif [ "$ARCH" = "armhf" ]; then
    echo "Note: linuxdeploy has no armhf build; using manual Qt deployment + appimagetool"
    LINUXDEPLOY=""
    LINUXDEPLOY_QT=""
    APPIMAGETOOL="$TOOLS_DIR/appimagetool-armhf.AppImage"
    if [ ! -f "$APPIMAGETOOL" ]; then
        echo "Downloading appimagetool for armhf..."
        curl -L -o "$APPIMAGETOOL" "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-armhf.AppImage"
        chmod +x "$APPIMAGETOOL"
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
    echo "Cross-compiling from $(uname -m) to $ARCH"
    CMAKE_EXTRA_FLAGS="-DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64"
elif [ "$ARCH" = "armhf" ] && [ "$(uname -m)" = "x86_64" ]; then
    echo "Cross-compiling from $(uname -m) to $ARCH"
    CMAKE_EXTRA_FLAGS="-DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=arm"
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
                su "$ORIGINAL_USER" -c "xhost +SI:localuser:root" >/dev/null 2>&1 || true
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
export LD_LIBRARY_PATH="$QT_DIR/lib:$LD_LIBRARY_PATH"

if [ -n "$LINUXDEPLOY" ] && [ -f "$LINUXDEPLOY" ]; then
export APPIMAGE_EXTRACT_AND_RUN=1
export QMAKE="$QT_DIR/bin/qmake"
export LINUXDEPLOY_PLUGIN_QT_IGNORE_GLOB="*/translations/*"
"$LINUXDEPLOY" --appdir="$APPDIR" --plugin=qt --exclude-library="libwayland-*" --exclude-library="libsystemd*" --exclude-library="libdbus-*" --exclude-library="libcap*" --verbosity=0
else
    echo "Manual Qt deployment for $ARCH..."
    mkdir -p "$APPDIR/usr/lib" "$APPDIR/usr/plugins" "$APPDIR/usr/qml"
    cp -d "$QT_DIR/lib/libQt6"*.so* "$APPDIR/usr/lib/" 2>/dev/null || true
    cp -d "$QT_DIR/lib/libicu"*.so* "$APPDIR/usr/lib/" 2>/dev/null || true
    for _plug in platforms imageformats tls iconengines xcbglintegrations; do
        if [ -d "$QT_DIR/plugins/$_plug" ]; then
            mkdir -p "$APPDIR/usr/plugins/$_plug"
            cp -a "$QT_DIR/plugins/$_plug/." "$APPDIR/usr/plugins/$_plug/"
        fi
    done
    for _qml in QtCore QtGui QtQml QtQuick QtQuickControls2 QML; do
        if [ -d "$QT_DIR/qml/$_qml" ]; then
            mkdir -p "$APPDIR/usr/qml/$_qml"
            cp -a "$QT_DIR/qml/$_qml/." "$APPDIR/usr/qml/$_qml/"
        fi
    done
fi

# Hook for removing files before AppImage creation
echo "Pre-packaging hook - opportunity to remove unwanted files"

# Remove host-coupled libraries that linuxdeploy-plugin-qt deploys despite --exclude-library
# These must come from the host system:
#   libsystemd: works with lsblk/libmount/DBus (see #1304, #1577)
#   libdbus-1:  communicates with host session bus, NEEDED libsystemd which we exclude
#   libcap:     kernel capabilities interface, orphaned transitive dep of libsystemd
rm -f "$APPDIR/usr/lib/libsystemd"*
rm -f "$APPDIR/usr/lib/libdbus-1"*
rm -f "$APPDIR/usr/lib/libcap"*
rm -rf "$APPDIR/usr/share/doc/libsystemd"*
rm -rf "$APPDIR/usr/share/doc/libdbus"*
rm -rf "$APPDIR/usr/share/doc/libcap"*

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
rm -f "$PWD/rpi-imager-desktop-$ARCH.AppImage"
rm -f "$PWD/rpi-imager-$ARCH.AppImage"

export LD_LIBRARY_PATH="$QT_DIR/lib:$LD_LIBRARY_PATH"

if [ -n "$LINUXDEPLOY" ] && [ -f "$LINUXDEPLOY" ]; then
export APPIMAGE_EXTRACT_AND_RUN=1
"$LINUXDEPLOY" --appdir="$APPDIR" \
    --desktop-file="$APPDIR/usr/share/applications/com.raspberrypi.rpi-imager.desktop" \
    --exclude-library="libsystemd*" \
    --exclude-library="libdbus-*" \
    --exclude-library="libcap*" \
    --exclude-library="libwayland-*" \
    --output=appimage \
    --verbosity=0

LINUXDEPLOY_OUTPUT="Raspberry_Pi_Imager-${ARCH}.AppImage"
if [ -f "$LINUXDEPLOY_OUTPUT" ]; then
    echo "Renaming '$LINUXDEPLOY_OUTPUT' to '$(basename "$OUTPUT_FILE")'"
    mv "$LINUXDEPLOY_OUTPUT" "$OUTPUT_FILE"
elif [ -f "$OUTPUT_FILE" ]; then
    echo "Output file already exists: $OUTPUT_FILE"
else
    echo "Warning: Expected linuxdeploy output '$LINUXDEPLOY_OUTPUT' not found"
    ls -la ./*.AppImage 2>/dev/null || true
fi
elif [ -n "${APPIMAGETOOL:-}" ] && [ -f "$APPIMAGETOOL" ]; then
    export APPIMAGE_EXTRACT_AND_RUN=1
    ARCH="$ARCH" "$APPIMAGETOOL" "$APPDIR" "$OUTPUT_FILE"
else
    echo "Error: no AppImage tooling available for $ARCH" >&2
    exit 1
fi

echo "AppImage created at $OUTPUT_FILE"

if [ ! -f "$OUTPUT_FILE" ]; then
    echo "Error: AppImage was not created at $OUTPUT_FILE" >&2
    exit 1
fi

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
