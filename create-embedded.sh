#!/bin/sh
set -e

# Script to create a .deb package for embedded systems using linuxfb as a renderer.
# The .deb installs a vendored directory tree under /opt/rpi-imager-embedded/ with
# a wrapper script at /usr/bin/rpi-imager-embedded.

# Source common build functions for ICU version detection
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TOP="$SCRIPT_DIR"
# shellcheck disable=SC1091
. "$TOP/debian/lib.sh"
export_cmake_parallel
sh "$TOP/debian/fetch-vendor-deps.sh"
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
    echo "  --arch=ARCH            Target architecture (x86_64, aarch64, armhf)"
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

# Validate architecture (armv6l/armv7l from uname on 32-bit Pi OS → armhf)
case "$ARCH" in
    armv6l|armv7l) ARCH=armhf ;;
esac
if [ "$ARCH" != "x86_64" ] && [ "$ARCH" != "aarch64" ] && [ "$ARCH" != "armhf" ]; then
    echo "Error: Architecture must be one of: x86_64, aarch64, armhf" >&2
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
            elif [ "$ARCH" = "armhf" ]; then
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

    if [ -f "./qt/build-qt-embedded.sh" ]; then
        echo "The embedded package needs the dedicated -no-opengl -no-dbus"
        echo "linuxfb Qt, which debian/build-embedded.sh builds for you:"
        echo "  debian/build-embedded.sh arm64"
        echo "Or build it directly (version defaults to QT_VERSION_DEFAULT in"
        echo "qt/qt-build-common.sh):"
        echo "  ./qt/build-qt-embedded.sh"
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

# Qt is built with -no-feature-icu (see qt/features_exclude.list), so nothing
# in the vendored tree links ICU and none is bundled.

# Configuration
BUILD_TYPE="MinSizeRel"  # Optimize for size in embedded systems

# .deb staging root and output path
DEBDIR="$PWD/debroot-embedded-$ARCH"
OPTDIR="$DEBDIR/opt/rpi-imager-embedded"

# Map arch to dpkg architecture name
case "$ARCH" in
    aarch64) DEB_ARCH="arm64" ;;
    x86_64)  DEB_ARCH="amd64" ;;
    armhf)   DEB_ARCH="armhf" ;;
    *)       DEB_ARCH="$ARCH" ;;
esac

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
elif [ "$ARCH" = "armhf" ] && [ "$(uname -m)" = "x86_64" ]; then
    # Cross-compiling from x86_64 to armhf (Pi 1 / Pi 2 32-bit OS)
    echo "Cross-compiling from $(uname -m) to $ARCH"
    CMAKE_EXTRA_FLAGS="-DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=arm"
fi

# Add Qt path to CMake flags
CMAKE_EXTRA_FLAGS="$CMAKE_EXTRA_FLAGS -DQt6_ROOT=$QT_DIR"

## Build embedded version
CMAKE_EXTRA_FLAGS="$CMAKE_EXTRA_FLAGS -DBUILD_EMBEDDED=ON"

# Bake the deployed rpath in at build time. cmake's default build rpath points
# at $QT_DIR/lib in the build tree; the binary is copied to /opt as-is (not
# installed), so without this it ships a leaked absolute build-host path that
# also breaks dpkg-shlibdeps. $ORIGIN/../lib matches the bundled layout and the
# launcher's LD_LIBRARY_PATH.
CMAKE_EXTRA_FLAGS="$CMAKE_EXTRA_FLAGS -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON -DCMAKE_INSTALL_RPATH=\$ORIGIN/../lib"

# shellcheck disable=SC2086
cmake -G Ninja "../$SOURCE_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DCMAKE_INSTALL_PREFIX=/usr $CMAKE_EXTRA_FLAGS
cmake --build . --parallel "$(cmake_build_jobs)"
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
        # -L dereferences the conf.d/*.conf symlinks (which point at absolute
        # /usr/share/fontconfig/conf.avail paths) so the bundle is self-contained
        # rather than shipping links that dangle on a target without fontconfig.
        cp -rL /etc/fonts/* "$OPTDIR/etc/fonts/" 2>/dev/null || true
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

# Note: libdbus-1 is intentionally NOT included. The embedded build links no
# QtDBus (see the QT_DBUS_LIB guards in the app), so nothing in the tree needs
# it; bundling it only dragged in libsystemd, which the netboot image lacks.

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

# ICU is intentionally absent: Qt is built with -no-feature-icu, so no ICU
# library is linked or bundled.

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

# Prune the QML tree, style libraries and tooling to what the UI imports. The
# `cp -r` of QtQuick/QtQml above is wholesale, so unused modules arrive whether
# or not anything imports them. Shared with the AppImage packaging path -- see
# prune_qml_to_imports() in debian/lib.sh for the import list.
prune_qml_to_imports "$OPTDIR/qml" "$OPTDIR/lib" "$OPTDIR/plugins"

# Remove Qt translations (not needed on embedded systems)
rm -rf "$OPTDIR/translations" 2>/dev/null || true
rm -rf "$OPTDIR/share/qt6/translations" 2>/dev/null || true

# Remove unused image format plugins that may have been copied with QML
rm -f "$OPTDIR/plugins/imageformats/libqtiff.so" 2>/dev/null || true
rm -f "$OPTDIR/plugins/imageformats/libqwebp.so" 2>/dev/null || true
rm -f "$OPTDIR/plugins/imageformats/libqgif.so" 2>/dev/null || true

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

# The explicit cp -d list above is curated to keep this package small (it is
# often fetched over the network). Keep curating it -- this call is additive and
# only fills in libraries the staged tree actually names in DT_NEEDED, chiefly
# the Qt libraries pulled in by the wholesale `cp -r` of the QML tree that the
# list does not mention. Runs after pruning so removed plugins' dependencies
# are not pulled back in. See embedded_deploy_lib_closure() in debian/lib.sh
# for how to revert to a purely curated list if size regresses.
embedded_deploy_lib_closure "$OPTDIR" "$QT_DIR/lib" || exit 1

# The cp -d globs above also match unversioned *.so development symlinks, which
# point at absolute host paths and would ship dangling.
prune_dev_symlinks "$OPTDIR/lib"

# Strip binaries to reduce size
find "$OPTDIR" -type f -executable -exec strip {} \; 2>/dev/null || true

echo "Stripping shared libraries..."
SO_FILES=$(find "$OPTDIR" -name "*.so*" 2>/dev/null || true)
if [ -n "$SO_FILES" ]; then
    # shellcheck disable=SC2086
    strip --strip-unneeded $SO_FILES 2>/dev/null || true
fi

# ---------------------------------------------------------------------------
# Assemble the .deb via debhelper, so debian/control is the single source of
# the package metadata (Depends, version, description) and md5sums are
# generated. dh_shlibdeps is deliberately NOT used: this package vendors Qt and
# its support libraries under /opt, and shlibdeps would demand from the target
# the very libraries bundled to avoid that. The external Depends are therefore
# maintained explicitly in debian/control's rpi-imager-embedded stanza.
# ---------------------------------------------------------------------------
echo "Assembling embedded .deb via debhelper..."

_pkg=rpi-imager-embedded
_stage="$TOP/debian/$_pkg"

# Populate the debhelper install tree from the vendored staging root.
rm -rf "$_stage"
mkdir -p "$_stage"
cp -a "$DEBDIR/opt" "$_stage/"
cp -a "$DEBDIR/usr" "$_stage/"

# dh_gencontrol: version/arch/Depends from debian/control + debian/changelog.
# dh_md5sums:    DEBIAN/md5sums.  dh_builddeb: the .deb into $TOP.
export DEB_BUILD_PROFILES=embedded
(
    cd "$TOP"
    dh_gencontrol -p"$_pkg"
    dh_md5sums -p"$_pkg"
    dh_builddeb -p"$_pkg" --destdir="$TOP"
)

# Remove the debhelper working files so the tree is left clean.
rm -rf "$_stage" "$TOP/debian/$_pkg.substvars" "$TOP/debian/$_pkg.debhelper.log" \
    "$TOP/debian/files" "$TOP/debian/.debhelper"

_out=$(ls "$TOP"/${_pkg}_*_"${DEB_ARCH}".deb 2>/dev/null | head -1)
if [ -n "$_out" ] && [ -f "$_out" ]; then
    echo ""
    echo "Embedded .deb created: $_out"
    echo "Embedded .deb build completed successfully for $ARCH."
    echo "Install with: sudo dpkg -i $(basename "$_out")"
else
    echo "Error: .deb creation failed." >&2
    exit 1
fi
