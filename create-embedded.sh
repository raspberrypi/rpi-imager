#!/bin/sh
set -e

# Script to create a .deb package for embedded systems using linuxfb as a renderer.
# The .deb installs a vendored directory tree under /opt/rpi-imager-embedded/ with
# a wrapper script at /usr/bin/rpi-imager-embedded.

# Source common build functions for ICU version detection
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
if [ -f "$SCRIPT_DIR/qt/qt-build-common.sh" ]; then
    . "$SCRIPT_DIR/qt/qt-build-common.sh"
fi

# Parse command line arguments
ARCH=$(uname -m)  # Default to current architecture
CLEAN_BUILD=1
QT_ROOT_ARG=""
INCLUDE_CJK_FONTS=0  # Default: do not include 4MB CJK font

usage() {
    echo "Usage: $0 [options]"
    echo "Options:"
    echo "  --arch=ARCH            Target architecture (x86_64, aarch64, armv7l)"
    echo "  --qt-root=PATH         Path to Qt installation directory"
    echo "  --no-clean             Don't clean build directory"
    echo "  --include-cjk-fonts    Include DroidSansFallbackFull.ttf for CJK support (+4MB)"
    echo "  -h, --help             Show this help message"
    echo ""
    echo "This script creates a .deb optimised for embedded systems:"
    echo "  - Uses linuxfb for direct rendering (no X11/Wayland required)"
    echo "  - All dependencies vendored under /opt/rpi-imager-embedded/"
    echo "  - Smaller size by excluding desktop-specific libraries"
    echo ""
    echo "Font Configuration:"
    echo "  By default, includes Roboto (Material Design) and DejaVu fonts (~2.7MB)"
    echo "  Use --include-cjk-fonts to add comprehensive CJK character support (+4MB)"
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
        --include-cjk-fonts)
            INCLUDE_CJK_FONTS=1
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
if [ "$ARCH" != "x86_64" ] && [ "$ARCH" != "aarch64" ] && [ "$ARCH" != "armv7l" ]; then
    echo "Error: Architecture must be one of: x86_64, aarch64, armv7l"
    exit 1
fi

echo "Building embedded .deb for architecture: $ARCH"

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

echo "Building rpi-imager version $GIT_VERSION (numeric: $PROJECT_VERSION) for embedded systems"

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
# Auto-detect Qt installation in /opt/Qt
else
    if [ -d "/opt/Qt" ]; then
        echo "Checking for Qt installations in /opt/Qt..."
        # Find the newest Qt6 version installed
        NEWEST_QT=$(find -L /opt/Qt -maxdepth 1 -type d -name "6.*" | sort -V | tail -n 1)
        if [ -n "$NEWEST_QT" ]; then
            QT_VERSION=$(basename "$NEWEST_QT")

            # Find appropriate compiler directory for the architecture
            if [ "$ARCH" = "x86_64" ]; then
                if [ -d "$NEWEST_QT/gcc_64_embedded" ]; then
                    QT_DIR="$NEWEST_QT/gcc_64_embedded"
                fi
            elif [ "$ARCH" = "aarch64" ]; then
                if [ -d "$NEWEST_QT/gcc_arm64_embedded" ]; then
                    QT_DIR="$NEWEST_QT/gcc_arm64_embedded"
                fi
            elif [ "$ARCH" = "armv7l" ]; then
                if [ -d "$NEWEST_QT/gcc_arm32_embedded" ]; then
                    QT_DIR="$NEWEST_QT/gcc_arm32_embedded"
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

    if [ -f "./qt/build-qt.sh" ]; then
        echo "You can build Qt using:"
        echo "  ./qt/build-qt.sh --version=6.9.1"
        echo "Or specify the Qt location with:"
        echo "  $0 --qt-root=/path/to/qt"
    else
        echo "You can specify the Qt location with:"
        echo "  $0 --qt-root=/path/to/qt"
    fi

    exit 1
fi

# Ensure Qt host tools (qmake, rcc, etc.) can load custom ICU runtime libs.
# This matters for embedded Qt builds linked against ICU 73 from qt/icu/install.
for ICU_RUNTIME_LIB_DIR in \
    "$SCRIPT_DIR/qt/icu/install/lib" \
    "$SCRIPT_DIR/qt/icu/icu4c/source/lib"
do
    if [ -d "$ICU_RUNTIME_LIB_DIR" ]; then
        case "${LD_LIBRARY_PATH:-}" in
            *"$ICU_RUNTIME_LIB_DIR"*) ;;
            *)
                export LD_LIBRARY_PATH="$ICU_RUNTIME_LIB_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
                echo "Using ICU runtime libraries from: $ICU_RUNTIME_LIB_DIR"
                ;;
        esac
        break
    fi
done

# Check if Qt Version
if [ -f "$QT_DIR/bin/qmake" ]; then
    QT_VERSION=$("$QT_DIR/bin/qmake" -query QT_VERSION)
    echo "Qt version: $QT_VERSION"
fi

# Detect ICU version for this Qt version
# Check if the function exists (was sourced from qt-build-common.sh)
if type get_icu_version_for_qt >/dev/null 2>&1; then
    ICU_VERSION=$(get_icu_version_for_qt "$QT_VERSION")
    ICU_MAJOR_VERSION="${ICU_VERSION%%.*}"  # Extract major version (e.g., 76 from 76.1)
    echo "Using ICU version: $ICU_VERSION (major: $ICU_MAJOR_VERSION)"
else
    # Fallback if common functions not available
    echo "Warning: Could not determine ICU version, using default 73.2"
    ICU_VERSION="73.2"
    ICU_MAJOR_VERSION="73"
fi

# Configuration
BUILD_TYPE="MinSizeRel"  # Optimize for size in embedded systems

# .deb staging root and output path
DEBDIR="$PWD/debroot-embedded-$ARCH"
OPTDIR="$DEBDIR/opt/rpi-imager-embedded"

# Map arch to dpkg architecture name
case "$ARCH" in
    aarch64) DEB_ARCH="arm64" ;;
    x86_64)  DEB_ARCH="amd64" ;;
    armv7l)  DEB_ARCH="armhf" ;;
    *)       DEB_ARCH="$ARCH" ;;
esac

OUTPUT_FILE="$PWD/rpi-imager-embedded_${PROJECT_VERSION}_${DEB_ARCH}.deb"

# Set up build directory
BUILD_DIR="build-embedded-$ARCH"

# Clean up previous builds if requested
if [ "$CLEAN_BUILD" -eq 1 ]; then
    echo "Cleaning previous build..."
    rm -rf "$DEBDIR" "$BUILD_DIR"
fi

mkdir -p "$OPTDIR/bin"
mkdir -p "$OPTDIR/lib"
mkdir -p "$DEBDIR/usr/bin"
mkdir -p "$DEBDIR/DEBIAN"
mkdir -p "$BUILD_DIR"

echo "Building rpi-imager for embedded $ARCH..."
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

## Build embedded version
CMAKE_EXTRA_FLAGS="$CMAKE_EXTRA_FLAGS -DBUILD_EMBEDDED=ON"

# shellcheck disable=SC2086
cmake "../$SOURCE_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DCMAKE_INSTALL_PREFIX=/usr $CMAKE_EXTRA_FLAGS
make -j"$(nproc)"
cd ..

echo "Populating .deb staging tree..."

# Copy binary directly from build directory
cp "$BUILD_DIR/rpi-imager" "$OPTDIR/bin/rpi-imager"

# Create the wrapper script
cat > "$DEBDIR/usr/bin/rpi-imager-embedded" << 'EOF'
#!/bin/sh
HERE="/opt/rpi-imager-embedded"

# Set up paths
export PATH="${HERE}/bin:${PATH}"
export LD_LIBRARY_PATH="${HERE}/lib:${LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="${HERE}/plugins"
export QML_IMPORT_PATH="${HERE}/qml"
export QT_QPA_PLATFORM_PLUGIN_PATH="${HERE}/plugins/platforms"
export QT_QPA_PLATFORM=linuxfb

# Font configuration for bundled fontconfig
export FONTCONFIG_PATH="${HERE}/etc/fonts"
export FONTCONFIG_FILE="${HERE}/etc/fonts/fonts.conf"
# Add our bundled fonts to the font path
export XDG_DATA_DIRS="${HERE}/share:${XDG_DATA_DIRS}"

# Disable desktop-specific features for embedded use
export QT_QUICK_CONTROLS_STYLE=Material

# UI scale factor is determined by the application itself: it reads the
# connected display's resolution and EDID from DRM and sets QT_SCALE_FACTOR
# before Qt initialises (see PlatformQuirks::applyEmbeddedDisplayScaling).
# Export QT_SCALE_FACTOR here only to force a manual override — the app
# respects an already-set value and won't overwrite it.

# GPU memory optimization for embedded systems
export QT_QUICK_BACKEND=software

# Software renderer with DPI scaling fix
export QSG_RENDER_LOOP=basic
export QT_QUICK_DEFAULT_TEXT_RENDER_TYPE=NativeRendering
export QSG_ATLAS_WIDTH=0
export QSG_ATLAS_HEIGHT=0
export QT_QPA_FB_DRM=/dev/dri/card1

exec "${HERE}/bin/rpi-imager" "$@"
EOF
chmod +x "$DEBDIR/usr/bin/rpi-imager-embedded"

# ---------------------------------------------------------------------------
# Deploy vendored dependencies into /opt/rpi-imager-embedded/
# ---------------------------------------------------------------------------

echo "Deploying Qt dependencies for embedded systems..."

# Copy essential Qt libraries (QtWidgets excluded - not used)
cp -d "$QT_DIR/lib/libQt6Core.so"* "$OPTDIR/lib/"
cp -d "$QT_DIR/lib/libQt6Gui.so"* "$OPTDIR/lib/"
cp -d "$QT_DIR/lib/libQt6DBus.so"* "$OPTDIR/lib/"  # Required by linuxfb plugin
cp -d "$QT_DIR/lib/libQt6Quick.so"* "$OPTDIR/lib/"
cp -d "$QT_DIR/lib/libQt6Qml.so"* "$OPTDIR/lib/"
cp -d "$QT_DIR/lib/libQt6QmlCore.so"* "$OPTDIR/lib/" 2>/dev/null || true
cp -d "$QT_DIR/lib/libQt6Network.so"* "$OPTDIR/lib/"
cp -d "$QT_DIR/lib/libQt6QmlMeta.so"* "$OPTDIR/lib/" 2>/dev/null || true
cp -d "$QT_DIR/lib/libQt6QuickTemplates2.so"* "$OPTDIR/lib/" 2>/dev/null || true
cp -d "$QT_DIR/lib/libQt6QuickControls2Material.so"* "$OPTDIR/lib/" 2>/dev/null || true
cp -d "$QT_DIR/lib/libQt6QuickControls2Basic.so"* "$OPTDIR/lib/" 2>/dev/null || true
cp -d "$QT_DIR/lib/libQt6QuickControls2Impl.so"* "$OPTDIR/lib/" 2>/dev/null || true
cp -d "$QT_DIR/lib/libQt6QuickLayouts.so"* "$OPTDIR/lib/" 2>/dev/null || true
cp -d "$QT_DIR/lib/libQt6QuickControls2MaterialStyleImpl.so"* "$OPTDIR/lib/" 2>/dev/null || true
cp -d "$QT_DIR/lib/libQt6Svg.so.6"* "$OPTDIR/lib/" 2>/dev/null || true
cp -d "$QT_DIR/lib/libQt6QuickDialogs2.so.6"* "$OPTDIR/lib/" 2>/dev/null || true
cp -d "$QT_DIR/lib/libQt6LabsFolderListModel.so.6"* "$OPTDIR/lib/" 2>/dev/null || true

# Copy font-related system libraries (required for embedded systems without desktop environment)
echo "Copying font rendering libraries..."
if [ -f "/lib/${ARCH}-linux-gnu/libfontconfig.so.1" ] || [ -f "/usr/lib/${ARCH}-linux-gnu/libfontconfig.so.1" ]; then
    cp -d "/lib/${ARCH}-linux-gnu"/libfontconfig.so* "$OPTDIR/lib/" 2>/dev/null || \
        cp -d "/usr/lib/${ARCH}-linux-gnu"/libfontconfig.so* "$OPTDIR/lib/" 2>/dev/null || \
        echo "Warning: Could not find libfontconfig"

    cp -d "/lib/${ARCH}-linux-gnu"/libfreetype.so* "$OPTDIR/lib/" 2>/dev/null || \
        cp -d "/usr/lib/${ARCH}-linux-gnu"/libfreetype.so* "$OPTDIR/lib/" 2>/dev/null || \
        echo "Warning: Could not find libfreetype"

    # Copy dependencies of fontconfig and freetype
    cp -d "/lib/${ARCH}-linux-gnu"/libexpat.so* "$OPTDIR/lib/" 2>/dev/null || \
        cp -d "/usr/lib/${ARCH}-linux-gnu"/libexpat.so* "$OPTDIR/lib/" 2>/dev/null || true

    cp -d "/lib/${ARCH}-linux-gnu"/libz.so* "$OPTDIR/lib/" 2>/dev/null || \
        cp -d "/usr/lib/${ARCH}-linux-gnu"/libz.so* "$OPTDIR/lib/" 2>/dev/null || true

    cp -d "/lib/${ARCH}-linux-gnu"/libbz2.so* "$OPTDIR/lib/" 2>/dev/null || \
        cp -d "/usr/lib/${ARCH}-linux-gnu"/libbz2.so* "$OPTDIR/lib/" 2>/dev/null || true

    cp -d "/lib/${ARCH}-linux-gnu"/libpng16.so* "$OPTDIR/lib/" 2>/dev/null || \
        cp -d "/usr/lib/${ARCH}-linux-gnu"/libpng16.so* "$OPTDIR/lib/" 2>/dev/null || true

    cp -d "/lib/${ARCH}-linux-gnu"/libbrotlidec.so* "$OPTDIR/lib/" 2>/dev/null || \
        cp -d "/usr/lib/${ARCH}-linux-gnu"/libbrotlidec.so* "$OPTDIR/lib/" 2>/dev/null || true

    cp -d "/lib/${ARCH}-linux-gnu"/libbrotlicommon.so* "$OPTDIR/lib/" 2>/dev/null || \
        cp -d "/usr/lib/${ARCH}-linux-gnu"/libbrotlicommon.so* "$OPTDIR/lib/" 2>/dev/null || true

    # Copy font configuration files for fontconfig to work properly
    mkdir -p "$OPTDIR/etc/fonts"
    if [ -d "/etc/fonts" ]; then
        cp -r /etc/fonts/* "$OPTDIR/etc/fonts/" 2>/dev/null || true
        echo "Copied system font configuration"
    fi

    echo "Font rendering libraries packaged for embedded deployment"
else
    echo "Warning: Font libraries not found for architecture $ARCH"
    echo "The .deb may require fontconfig to be installed on target system"
fi

# Copy input device libraries (required by linuxfb platform plugin)
echo "Copying input device libraries for linuxfb..."
cp -d "/lib/${ARCH}-linux-gnu"/libudev.so* "$OPTDIR/lib/" 2>/dev/null || \
    cp -d "/usr/lib/${ARCH}-linux-gnu"/libudev.so* "$OPTDIR/lib/" 2>/dev/null || true

cp -d "/lib/${ARCH}-linux-gnu"/libinput.so* "$OPTDIR/lib/" 2>/dev/null || \
    cp -d "/usr/lib/${ARCH}-linux-gnu"/libinput.so* "$OPTDIR/lib/" 2>/dev/null || true

cp -d "/lib/${ARCH}-linux-gnu"/libmtdev.so* "$OPTDIR/lib/" 2>/dev/null || \
    cp -d "/usr/lib/${ARCH}-linux-gnu"/libmtdev.so* "$OPTDIR/lib/" 2>/dev/null || true

cp -d "/lib/${ARCH}-linux-gnu"/libevdev.so* "$OPTDIR/lib/" 2>/dev/null || \
    cp -d "/usr/lib/${ARCH}-linux-gnu"/libevdev.so* "$OPTDIR/lib/" 2>/dev/null || true

cp -d "/lib/${ARCH}-linux-gnu"/libxkbcommon.so* "$OPTDIR/lib/" 2>/dev/null || \
    cp -d "/usr/lib/${ARCH}-linux-gnu"/libxkbcommon.so* "$OPTDIR/lib/" 2>/dev/null || true

cp -d "/lib/${ARCH}-linux-gnu"/libdrm.so* "$OPTDIR/lib/" 2>/dev/null || \
    cp -d "/usr/lib/${ARCH}-linux-gnu"/libdrm.so* "$OPTDIR/lib/" 2>/dev/null || true

cp -d "/lib/${ARCH}-linux-gnu"/libwacom.so* "$OPTDIR/lib/" 2>/dev/null || \
    cp -d "/usr/lib/${ARCH}-linux-gnu"/libwacom.so* "$OPTDIR/lib/" 2>/dev/null || true

# Copy GLib libraries (required by libinput)
cp -d "/lib/${ARCH}-linux-gnu"/libglib-2.0.so* "$OPTDIR/lib/" 2>/dev/null || \
    cp -d "/usr/lib/${ARCH}-linux-gnu"/libglib-2.0.so* "$OPTDIR/lib/" 2>/dev/null || true

cp -d "/lib/${ARCH}-linux-gnu"/libgobject-2.0.so* "$OPTDIR/lib/" 2>/dev/null || \
    cp -d "/usr/lib/${ARCH}-linux-gnu"/libgobject-2.0.so* "$OPTDIR/lib/" 2>/dev/null || true

cp -d "/lib/${ARCH}-linux-gnu"/libgudev-1.0.so* "$OPTDIR/lib/" 2>/dev/null || \
    cp -d "/usr/lib/${ARCH}-linux-gnu"/libgudev-1.0.so* "$OPTDIR/lib/" 2>/dev/null || true

# Copy additional system libraries
cp -d "/lib/${ARCH}-linux-gnu"/libdbus-1.so* "$OPTDIR/lib/" 2>/dev/null || \
    cp -d "/usr/lib/${ARCH}-linux-gnu"/libdbus-1.so* "$OPTDIR/lib/" 2>/dev/null || true

# Note: libsystemd is intentionally NOT included - it must come from the host system
# to work correctly with DBus (see https://github.com/raspberrypi/rpi-imager/issues/1304)

cp -d "/lib/${ARCH}-linux-gnu"/libcap.so* "$OPTDIR/lib/" 2>/dev/null || \
    cp -d "/usr/lib/${ARCH}-linux-gnu"/libcap.so* "$OPTDIR/lib/" 2>/dev/null || true

cp -d "/lib/${ARCH}-linux-gnu"/libatomic.so* "$OPTDIR/lib/" 2>/dev/null || \
    cp -d "/usr/lib/${ARCH}-linux-gnu"/libatomic.so* "$OPTDIR/lib/" 2>/dev/null || true

cp -d "/lib/${ARCH}-linux-gnu"/libdouble-conversion.so* "$OPTDIR/lib/" 2>/dev/null || \
    cp -d "/usr/lib/${ARCH}-linux-gnu"/libdouble-conversion.so* "$OPTDIR/lib/" 2>/dev/null || true

cp -d "/lib/${ARCH}-linux-gnu"/libpcre2-*.so* "$OPTDIR/lib/" 2>/dev/null || \
    cp -d "/usr/lib/${ARCH}-linux-gnu"/libpcre2-*.so* "$OPTDIR/lib/" 2>/dev/null || true

cp -d "/lib/${ARCH}-linux-gnu"/libzstd.so* "$OPTDIR/lib/" 2>/dev/null || \
    cp -d "/usr/lib/${ARCH}-linux-gnu"/libzstd.so* "$OPTDIR/lib/" 2>/dev/null || true

cp -d "/lib/${ARCH}-linux-gnu"/libffi.so* "$OPTDIR/lib/" 2>/dev/null || \
    cp -d "/usr/lib/${ARCH}-linux-gnu"/libffi.so* "$OPTDIR/lib/" 2>/dev/null || true

echo "Input device libraries packaged for embedded deployment"

# Qt plugins
mkdir -p "$OPTDIR/plugins/platforms"
cp "$QT_DIR/plugins/platforms/libqlinuxfb.so" "$OPTDIR/plugins/platforms/" 2>/dev/null || true
# Only include the certificate-parsing backend; the OpenSSL TLS backend is not
# needed because all HTTPS traffic goes through libcurl (linked to GnuTLS).
mkdir -p "$OPTDIR/plugins/tls"
cp "$QT_DIR/plugins/tls/libqcertonlybackend.so" "$OPTDIR/plugins/tls/" 2>/dev/null || true

# Copy only essential image format plugins
mkdir -p "$OPTDIR/plugins/imageformats"
cp "$QT_DIR/plugins/imageformats/libqjpeg.so" "$OPTDIR/plugins/imageformats/" 2>/dev/null || true
cp "$QT_DIR/plugins/imageformats/libqpng.so" "$OPTDIR/plugins/imageformats/" 2>/dev/null || true
cp "$QT_DIR/plugins/imageformats/libqsvg.so" "$OPTDIR/plugins/imageformats/" 2>/dev/null || true

# QML modules
mkdir -p "$OPTDIR/qml/QtCore"
cp -d "$QT_DIR/qml/QtCore/"* "$OPTDIR/qml/QtCore/" 2>/dev/null || true
mkdir -p "$OPTDIR/qml/Qt/labs/folderlistmodel"
cp -d "$QT_DIR/qml/Qt/labs/folderlistmodel/"* "$OPTDIR/qml/Qt/labs/folderlistmodel/" 2>/dev/null || true

mkdir -p "$OPTDIR/qml/QtQuick/Controls/Material/impl"
mkdir -p "$OPTDIR/qml/QtQuick/Controls/impl"
mkdir -p "$OPTDIR/qml/QtQuick/Controls/Basic/impl"
cp "$QT_DIR/qml/QtQuick/Controls/libqtquickcontrols2plugin.so" "$OPTDIR/qml/QtQuick/Controls/" 2>/dev/null || true
cp "$QT_DIR/qml/QtQuick/Controls/Material/impl/libqtquickcontrols2materialstyleimplplugin.so" "$OPTDIR/qml/QtQuick/Controls/Material/impl/" 2>/dev/null || true
cp "$QT_DIR/qml/QtQuick/Controls/impl/libqtquickcontrols2implplugin.so" "$OPTDIR/qml/QtQuick/Controls/impl/" 2>/dev/null || true
cp "$QT_DIR/qml/QtQuick/Controls/Basic/impl/libqtquickcontrols2basicstyleimplplugin.so" "$OPTDIR/qml/QtQuick/Controls/Basic/impl/" 2>/dev/null || true
cp "$QT_DIR/qml/QtQuick/Controls/Basic/libqtquickcontrols2basicstyleplugin.so" "$OPTDIR/qml/QtQuick/Controls/Basic/" 2>/dev/null || true
cp "$QT_DIR/qml/QtQuick/Controls/Material/libqtquickcontrols2materialstyleplugin.so" "$OPTDIR/qml/QtQuick/Controls/Material/" 2>/dev/null || true

# Copy ICU libraries (using detected version)
echo "Copying ICU $ICU_VERSION libraries..."
ICU_LIB_DIR="$PWD/qt/icu/install/lib"
if [ ! -d "$ICU_LIB_DIR" ]; then
    ICU_LIB_DIR="$PWD/qt/icu/icu4c/source/lib"
fi
if [ -d "$ICU_LIB_DIR" ]; then
    cp "$ICU_LIB_DIR/libicudata.so.$ICU_MAJOR_VERSION" "$OPTDIR/lib/" 2>/dev/null || \
        echo "Warning: Could not find libicudata.so.$ICU_MAJOR_VERSION"
    cp "$ICU_LIB_DIR/libicui18n.so.$ICU_MAJOR_VERSION" "$OPTDIR/lib/" 2>/dev/null || \
        echo "Warning: Could not find libicui18n.so.$ICU_MAJOR_VERSION"
    cp "$ICU_LIB_DIR/libicuuc.so.$ICU_MAJOR_VERSION" "$OPTDIR/lib/" 2>/dev/null || \
        echo "Warning: Could not find libicuuc.so.$ICU_MAJOR_VERSION"

    # Create symlinks without version for compatibility
    (cd "$OPTDIR/lib" && \
        [ -f "libicudata.so.$ICU_MAJOR_VERSION" ] && ln -sf "libicudata.so.$ICU_MAJOR_VERSION" "libicudata.so" || true && \
        [ -f "libicui18n.so.$ICU_MAJOR_VERSION" ] && ln -sf "libicui18n.so.$ICU_MAJOR_VERSION" "libicui18n.so" || true && \
        [ -f "libicuuc.so.$ICU_MAJOR_VERSION" ] && ln -sf "libicuuc.so.$ICU_MAJOR_VERSION" "libicuuc.so" || true)
else
    echo "Warning: ICU libraries not found at $ICU_LIB_DIR"
    echo "You may need to build Qt with ICU support first"
fi

# Fonts
mkdir -p "$OPTDIR/share/fonts/truetype/dejavu"
mkdir -p "$OPTDIR/share/fonts/truetype/freefont"
mkdir -p "$OPTDIR/share/fonts/truetype/droid"
mkdir -p "$OPTDIR/share/fonts/truetype/roboto"

cp src/fonts/DejaVuSans.ttf "$OPTDIR/share/fonts/truetype/dejavu"
cp src/fonts/DejaVuSans-Bold.ttf "$OPTDIR/share/fonts/truetype/dejavu"
cp src/fonts/FreeSans.ttf "$OPTDIR/share/fonts/truetype/freefont"
cp src/fonts/Roboto-Regular.ttf "$OPTDIR/share/fonts/truetype/roboto"
cp src/fonts/Roboto-Bold.ttf "$OPTDIR/share/fonts/truetype/roboto"
cp src/fonts/Roboto-Light.ttf "$OPTDIR/share/fonts/truetype/roboto"

if [ "$INCLUDE_CJK_FONTS" -eq 1 ]; then
    echo "Including CJK font support (+4MB)"
    cp src/fonts/DroidSansFallbackFull.ttf "$OPTDIR/share/fonts/truetype/droid"
else
    echo "Skipping CJK fonts (use --include-cjk-fonts to enable)"
fi

# Create custom fontconfig configuration for embedded deployment
cat > "$OPTDIR/etc/fonts/local.conf" << 'FONTCONFIG_EOF'
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "fonts.dtd">
<fontconfig>
    <!-- Set Roboto as the primary sans-serif font -->
    <alias>
        <family>sans-serif</family>
        <prefer>
            <family>Roboto</family>
            <family>DejaVu Sans</family>
            <family>FreeSans</family>
            <family>Droid Sans Fallback</family>
        </prefer>
    </alias>

    <!-- Default to Roboto for system UI fonts -->
    <alias>
        <family>system-ui</family>
        <prefer>
            <family>Roboto</family>
        </prefer>
    </alias>

    <!-- Ensure Qt Material style finds Roboto -->
    <match target="pattern">
        <test name="family" compare="eq">
            <string>Roboto</string>
        </test>
        <edit name="family" mode="assign" binding="strong">
            <string>Roboto</string>
        </edit>
    </match>

    <!-- Cache directory for font cache -->
    <cachedir>/tmp/fontconfig-cache-rpi-imager</cachedir>
</fontconfig>
FONTCONFIG_EOF

echo "Created custom fontconfig configuration for bundled fonts"

# Copy QML components
if [ -d "$QT_DIR/qml" ]; then
    cp -r "$QT_DIR/qml/QtQuick" "$OPTDIR/qml/" 2>/dev/null || true
    cp -r "$QT_DIR/qml/QtQuick.2" "$OPTDIR/qml/" 2>/dev/null || true
    cp -r "$QT_DIR/qml/QtQml" "$OPTDIR/qml/" 2>/dev/null || true
fi

# ---------------------------------------------------------------------------
# Embedded-specific optimisations
# ---------------------------------------------------------------------------
echo "Applying embedded system optimisations..."

# Remove unused QML Controls themes (size optimisation)
rm -rf "$OPTDIR/qml/QtQuick/Controls/Universal"
rm -rf "$OPTDIR/qml/QtQuick/Controls/Fusion"
rm -rf "$OPTDIR/qml/QtQuick/Controls/Imagine"
rm -rf "$OPTDIR/qml/QtQuick/Controls/FluentWinUI3"

# Remove QML debugging tools
rm -rf "$OPTDIR/qml/QtTest"* 2>/dev/null || true
rm -rf "$OPTDIR/plugins/qmltooling" 2>/dev/null || true

# Remove Qt translations (not needed on embedded systems)
rm -rf "$OPTDIR/translations" 2>/dev/null || true
rm -rf "$OPTDIR/share/qt6/translations" 2>/dev/null || true

# Remove unused image format plugins that may have been copied with QML
rm -f "$OPTDIR/plugins/imageformats/libqtiff.so" 2>/dev/null || true
rm -f "$OPTDIR/plugins/imageformats/libqwebp.so" 2>/dev/null || true
rm -f "$OPTDIR/plugins/imageformats/libqgif.so" 2>/dev/null || true

# Remove unused Qt Quick Controls 2 style libraries
rm -f "$OPTDIR/lib/libQt6QuickControls2Fusion.so"* 2>/dev/null || true
rm -f "$OPTDIR/lib/libQt6QuickControls2Universal.so"* 2>/dev/null || true
rm -f "$OPTDIR/lib/libQt6QuickControls2Imagine.so"* 2>/dev/null || true
rm -f "$OPTDIR/lib/libQt6QuickControls2FluentWinUI3.so"* 2>/dev/null || true
rm -f "$OPTDIR/lib/libQt6QuickControls2FusionStyleImpl.so"* 2>/dev/null || true
rm -f "$OPTDIR/lib/libQt6QuickControls2UniversalStyleImpl.so"* 2>/dev/null || true
rm -f "$OPTDIR/lib/libQt6QuickControls2ImagineStyleImpl.so"* 2>/dev/null || true
rm -f "$OPTDIR/lib/libQt6QuickControls2FluentWinUI3StyleImpl.so"* 2>/dev/null || true
rm -f "$OPTDIR/lib/libQt6QuickControls2WindowsStyleImpl.so"* 2>/dev/null || true
rm -f "$OPTDIR/lib/libQt6Widgets.so"* 2>/dev/null || true

# Remove desktop-specific libraries that may have been included
rm -f "$OPTDIR/lib/libwayland"* 2>/dev/null || true
rm -f "$OPTDIR/lib/libX11"* 2>/dev/null || true
rm -f "$OPTDIR/lib/libxcb"* 2>/dev/null || true
rm -rf "$OPTDIR/plugins/platforms/libqwayland"* 2>/dev/null || true
rm -rf "$OPTDIR/plugins/platforms/libqxcb"* 2>/dev/null || true

# Remove development files
find "$OPTDIR" -name "*.a" -delete 2>/dev/null || true
find "$OPTDIR" -name "*.la" -delete 2>/dev/null || true
find "$OPTDIR" -name "*.prl" -delete 2>/dev/null || true

# Strip binaries to reduce size
find "$OPTDIR" -type f -executable -exec strip {} \; 2>/dev/null || true

echo "Stripping shared libraries..."
SO_FILES=$(find "$OPTDIR" -name "*.so*" 2>/dev/null || true)
if [ -n "$SO_FILES" ]; then
    # shellcheck disable=SC2086
    strip --strip-unneeded $SO_FILES 2>/dev/null || true
fi

# ---------------------------------------------------------------------------
# Build the .deb
# ---------------------------------------------------------------------------
echo "Creating embedded .deb package..."

# Calculate installed size in KiB
INSTALLED_SIZE=$(du -sk "$DEBDIR" | cut -f1)

cat > "$DEBDIR/DEBIAN/control" << CTRL_EOF
Package: rpi-imager-embedded
Version: ${PROJECT_VERSION}
Architecture: ${DEB_ARCH}
Installed-Size: ${INSTALLED_SIZE}
Maintainer: Raspberry Pi Ltd <info@raspberrypi.com>
Depends: dosfstools, fdisk, util-linux (>= 2.37), libdrm2, libinput10, libudev1
Suggests: rpi-eeprom, firmware-brcm80211
Conflicts: rpi-imager
Section: admin
Priority: optional
Homepage: https://www.raspberrypi.com/software
Description: Raspberry Pi Imaging utility for embedded systems
 Optimised for embedded systems with vendored Qt6 and dependencies.
 Uses linuxfb for direct rendering (no desktop environment required).
CTRL_EOF

# Remove old output
rm -f "$OUTPUT_FILE"
rm -f "$PWD/rpi-imager-embedded.deb"

dpkg-deb --build --root-owner-group "$DEBDIR" "$OUTPUT_FILE"

if [ -f "$OUTPUT_FILE" ]; then
    echo ""
    echo "Embedded .deb created: $OUTPUT_FILE"

    # Create convenience symlink for debian packaging
    ln -sf "$(basename "$OUTPUT_FILE")" "$PWD/rpi-imager-embedded.deb"

    echo ""
    echo "Embedded .deb build completed successfully for $ARCH."
    echo "Install with: sudo dpkg -i $(basename "$OUTPUT_FILE")"
else
    echo "Error: .deb creation failed."
    exit 1
fi
