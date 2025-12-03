#!/bin/sh
set -e

# Script to create AppImage for embedded systems using linuxfb as a renderer
# This creates an AppImage that runs with direct rendering (no window manager required)

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
    echo "This script creates an AppImage optimized for embedded systems:"
    echo "  - Uses linuxfb for direct rendering (no X11/Wayland required)"
    echo "  - Optimized for headless/embedded environments"
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

echo "Building embedded AppImage for architecture: $ARCH"

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

echo "Building $PROJECT_NAME version $GIT_VERSION (numeric: $PROJECT_VERSION) for embedded systems"

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
        NEWEST_QT=$(find /opt/Qt -maxdepth 1 -type d -name "6.*" | sort -V | tail -n 1)
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
    echo "Warning: Could not determine ICU version, using default 76"
    ICU_VERSION="76.1"
    ICU_MAJOR_VERSION="76"
fi

# Configuration
BUILD_TYPE="MinSizeRel"  # Optimize for size in embedded systems

# Location of AppDir and output file
APPDIR="$PWD/AppDir-embedded-$ARCH"
OUTPUT_FILE="$PWD/Raspberry_Pi_Imager-${GIT_VERSION}-embedded-${ARCH}.AppImage"

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
elif [ "$ARCH" = "armv7l" ]; then
    # Note: linuxdeploy may not have armv7l builds, fallback to manual deployment
    echo "Warning: linuxdeploy may not support armv7l, attempting manual deployment"
    LINUXDEPLOY=""
    LINUXDEPLOY_QT=""
fi

# Set up build directory
BUILD_DIR="build-embedded-$ARCH"

# Clean up previous builds if requested
if [ "$CLEAN_BUILD" -eq 1 ]; then
    echo "Cleaning previous build..."
    rm -rf "$APPDIR" "$BUILD_DIR"
fi

mkdir -p "$APPDIR"
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

echo "Creating embedded AppDir..."
# Install to AppDir
make DESTDIR="$APPDIR" install
cd ..

# Copy the desktop file from debian directory and modify for embedded use
mkdir -p "$APPDIR/usr/share/applications"
# Remove any existing desktop files to avoid confusion
rm -f "$APPDIR/usr/share/applications/"*.desktop
# Copy and modify the embedded desktop file
cp "debian/com.raspberrypi.rpi-imager.desktop" "$APPDIR/usr/share/applications/com.raspberrypi.rpi-imager-embedded.desktop"
# Update the desktop file for embedded use (preserve %F for file arguments)
sed -i 's|Name=.*|Name=Raspberry Pi Imager (Embedded)|' "$APPDIR/usr/share/applications/com.raspberrypi.rpi-imager-embedded.desktop"
sed -i 's|Comment=.*|Comment=Raspberry Pi Imager for embedded systems|' "$APPDIR/usr/share/applications/com.raspberrypi.rpi-imager-embedded.desktop"
sed -i 's|Exec=.*|Exec=rpi-imager %F|' "$APPDIR/usr/share/applications/com.raspberrypi.rpi-imager-embedded.desktop"

# Create the AppRun file
cat > "$APPDIR/AppRun" << 'EOF'
#!/bin/sh
HERE="$(dirname "$(readlink -f "${0}")")"

# Set up paths
export PATH="${HERE}/usr/bin:${PATH}"
export LD_LIBRARY_PATH="${HERE}/usr/lib:${LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="${HERE}/usr/plugins"
export QML_IMPORT_PATH="${HERE}/usr/qml"
export QT_QPA_PLATFORM_PLUGIN_PATH="${HERE}/usr/plugins/platforms"
QT_QPA_PLATFORM=linuxfb

# Font configuration for bundled fontconfig
export FONTCONFIG_PATH="${HERE}/etc/fonts"
export FONTCONFIG_FILE="${HERE}/etc/fonts/fonts.conf"
# Add our bundled fonts to the font path
export XDG_DATA_DIRS="${HERE}/usr/share:${XDG_DATA_DIRS}"

# Disable desktop-specific features for embedded use
export QT_QUICK_CONTROLS_STYLE=Material

# Calculate appropriate scale factor based on display DPI
# Qt6 auto-scaling doesn't always work correctly on embedded platforms

# Try to get display info from DRM EDID data
SCALE_FACTOR_SET=0

for drm_connector in /sys/class/drm/card*/status; do
    if [ ! -f "$drm_connector" ]; then
        continue
    fi
    
    connector_status=$(cat "$drm_connector")
    if [ "$connector_status" != "connected" ]; then
        continue
    fi
    
    # Get the connector directory (remove /status suffix)
    connector_dir="${drm_connector%/status}"
    edid_file="${connector_dir}/edid"
    modes_file="${connector_dir}/modes"
    
    if [ -f "$edid_file" ] && [ -f "$modes_file" ]; then
        # Parse EDID to get physical screen dimensions
        # Byte 21 (0x15): horizontal screen size in cm
        # Byte 22 (0x16): vertical screen size in cm
        # Note: sysfs files always show size 0, but can be read
        EDID_BYTES=$(od -An -t u1 -N 128 "$edid_file" 2>/dev/null)
        
        if [ -n "$EDID_BYTES" ]; then
            # Convert to array
            set -- $EDID_BYTES
            
            # Get bytes at offset 21 and 22 (physical size in cm)
            PHYS_WIDTH_CM=${22}  # 22nd element (offset 21)
            PHYS_HEIGHT_CM=${23} # 23rd element (offset 22)
            
            if [ "$PHYS_WIDTH_CM" -gt 0 ] && [ "$PHYS_HEIGHT_CM" -gt 0 ] 2>/dev/null; then
                # Convert cm to mm
                PHYS_WIDTH=$((PHYS_WIDTH_CM * 10))
                PHYS_HEIGHT=$((PHYS_HEIGHT_CM * 10))
                
                # Get resolution from first mode
                RESOLUTION=$(head -1 "$modes_file")
                VIRT_WIDTH=$(echo "$RESOLUTION" | cut -d'x' -f1)
                VIRT_HEIGHT=$(echo "$RESOLUTION" | cut -d'x' -f2)
                
                if [ "$VIRT_WIDTH" -gt 0 ] && [ "$VIRT_HEIGHT" -gt 0 ] 2>/dev/null; then
                    # Calculate DPI: pixels / (mm / 25.4)
                    # We multiply by 254 and divide by 10 to avoid floating point
                    DPI=$((VIRT_WIDTH * 254 / PHYS_WIDTH / 10))
                    
                    # Set scale factor based on DPI (base DPI = 96)
                    # Use common scale factors: 1.0, 1.25, 1.5, 2.0, 2.5
                    if [ "$DPI" -ge 216 ]; then
                        export QT_SCALE_FACTOR=2.5
                    elif [ "$DPI" -ge 168 ]; then
                        export QT_SCALE_FACTOR=2.0
                    elif [ "$DPI" -ge 132 ]; then
                        export QT_SCALE_FACTOR=1.5
                    elif [ "$DPI" -ge 108 ]; then
                        export QT_SCALE_FACTOR=1.25
                    else
                        export QT_SCALE_FACTOR=1.0
                    fi
                    
                    echo "Detected display (DRM): ${VIRT_WIDTH}x${VIRT_HEIGHT}px, ${PHYS_WIDTH}x${PHYS_HEIGHT}mm, ${DPI} DPI -> Scale: ${QT_SCALE_FACTOR}"
                    SCALE_FACTOR_SET=1
                    break
                fi
            fi
        fi
    fi
done

# Fallback if DRM detection didn't work
if [ "$SCALE_FACTOR_SET" -eq 0 ]; then
    export QT_AUTO_SCREEN_SCALE_FACTOR=1
    echo "Could not detect display DPI from DRM, using Qt auto-scaling"
fi

# GPU memory optimization for embedded systems
export QT_QUICK_BACKEND=software

# Software renderer with DPI scaling fix
# Use basic render loop which is simpler and handles scale factors better
export QSG_RENDER_LOOP=basic
# Disable distance field text rendering which has scaling artifacts
export QT_QUICK_DEFAULT_TEXT_RENDER_TYPE=NativeRendering
# Disable texture atlasing to prevent cached texture scaling issues
export QSG_ATLAS_WIDTH=0
export QSG_ATLAS_HEIGHT=0
export QT_QPA_FB_DRM=/dev/dri/card1

# Logging (can be disabled in production)
# export QT_LOGGING_RULES="*.debug=true;*.qpa.*=false"

exec "${HERE}/usr/bin/rpi-imager" "$@"
EOF
chmod +x "$APPDIR/AppRun"

# Manual Qt deployment for embedded systems (optimized)
echo "Deploying Qt dependencies for embedded systems..."

# Copy essential Qt libraries (QtWidgets excluded - not used)
mkdir -p "$APPDIR/usr/lib"
cp -d "$QT_DIR/lib/libQt6Core.so"* "$APPDIR/usr/lib/"
cp -d "$QT_DIR/lib/libQt6Gui.so"* "$APPDIR/usr/lib/"
cp -d "$QT_DIR/lib/libQt6DBus.so"* "$APPDIR/usr/lib/"  # Required by linuxfb plugin
# cp -d "$QT_DIR/lib/libQt6Widgets.so"* "$APPDIR/usr/lib/"    # QtWidgets excluded
cp -d "$QT_DIR/lib/libQt6Quick.so"* "$APPDIR/usr/lib/"
cp -d "$QT_DIR/lib/libQt6Qml.so"* "$APPDIR/usr/lib/"
cp -d "$QT_DIR/lib/libQt6QmlCore.so"* "$APPDIR/usr/lib/" 2>/dev/null || true
cp -d "$QT_DIR/lib/libQt6Network.so"* "$APPDIR/usr/lib/"
cp -d "$QT_DIR/lib/libQt6QmlMeta.so"* "$APPDIR/usr/lib/" 2>/dev/null || true
cp -d "$QT_DIR/lib/libQt6QuickTemplates2.so"* "$APPDIR/usr/lib/" 2>/dev/null || true
cp -d "$QT_DIR/lib/libQt6QuickControls2Material.so"* "$APPDIR/usr/lib/" 2>/dev/null || true
cp -d "$QT_DIR/lib/libQt6QuickControls2Basic.so"* "$APPDIR/usr/lib/" 2>/dev/null || true
cp -d "$QT_DIR/lib/libQt6QuickControls2Impl.so"* "$APPDIR/usr/lib/" 2>/dev/null || true
cp -d "$QT_DIR/lib/libQt6QuickLayouts.so"* "$APPDIR/usr/lib/" 2>/dev/null || true
cp -d "$QT_DIR/lib/libQt6QuickControls2MaterialStyleImpl.so"* "$APPDIR/usr/lib/" 2>/dev/null || true
cp -d "$QT_DIR/lib/libQt6Svg.so.6"* "$APPDIR/usr/lib/" 2>/dev/null || true
cp -d "$QT_DIR/lib/libQt6QuickDialogs2.so.6"* "$APPDIR/usr/lib/" 2>/dev/null || true
cp -d "$QT_DIR/lib/libQt6LabsFolderListModel.so.6"* "$APPDIR/usr/lib/" 2>/dev/null || true

# Copy font-related system libraries (required for embedded systems without desktop environment)
echo "Copying font rendering libraries..."
if [ -f "/lib/${ARCH}-linux-gnu/libfontconfig.so.1" ] || [ -f "/usr/lib/${ARCH}-linux-gnu/libfontconfig.so.1" ]; then
    cp -d "/lib/${ARCH}-linux-gnu"/libfontconfig.so* "$APPDIR/usr/lib/" 2>/dev/null || \
        cp -d "/usr/lib/${ARCH}-linux-gnu"/libfontconfig.so* "$APPDIR/usr/lib/" 2>/dev/null || \
        echo "Warning: Could not find libfontconfig"
    
    cp -d "/lib/${ARCH}-linux-gnu"/libfreetype.so* "$APPDIR/usr/lib/" 2>/dev/null || \
        cp -d "/usr/lib/${ARCH}-linux-gnu"/libfreetype.so* "$APPDIR/usr/lib/" 2>/dev/null || \
        echo "Warning: Could not find libfreetype"
    
    # Copy dependencies of fontconfig and freetype
    # Direct dependencies of fontconfig
    cp -d "/lib/${ARCH}-linux-gnu"/libexpat.so* "$APPDIR/usr/lib/" 2>/dev/null || \
        cp -d "/usr/lib/${ARCH}-linux-gnu"/libexpat.so* "$APPDIR/usr/lib/" 2>/dev/null || true
    
    # Dependencies of freetype (fontconfig -> freetype -> these)
    cp -d "/lib/${ARCH}-linux-gnu"/libz.so* "$APPDIR/usr/lib/" 2>/dev/null || \
        cp -d "/usr/lib/${ARCH}-linux-gnu"/libz.so* "$APPDIR/usr/lib/" 2>/dev/null || true
    
    cp -d "/lib/${ARCH}-linux-gnu"/libbz2.so* "$APPDIR/usr/lib/" 2>/dev/null || \
        cp -d "/usr/lib/${ARCH}-linux-gnu"/libbz2.so* "$APPDIR/usr/lib/" 2>/dev/null || true
    
    cp -d "/lib/${ARCH}-linux-gnu"/libpng16.so* "$APPDIR/usr/lib/" 2>/dev/null || \
        cp -d "/usr/lib/${ARCH}-linux-gnu"/libpng16.so* "$APPDIR/usr/lib/" 2>/dev/null || true
    
    cp -d "/lib/${ARCH}-linux-gnu"/libbrotlidec.so* "$APPDIR/usr/lib/" 2>/dev/null || \
        cp -d "/usr/lib/${ARCH}-linux-gnu"/libbrotlidec.so* "$APPDIR/usr/lib/" 2>/dev/null || true
    
    cp -d "/lib/${ARCH}-linux-gnu"/libbrotlicommon.so* "$APPDIR/usr/lib/" 2>/dev/null || \
        cp -d "/usr/lib/${ARCH}-linux-gnu"/libbrotlicommon.so* "$APPDIR/usr/lib/" 2>/dev/null || true
    
    # Copy font configuration files for fontconfig to work properly
    mkdir -p "$APPDIR/etc/fonts"
    if [ -d "/etc/fonts" ]; then
        cp -r /etc/fonts/* "$APPDIR/etc/fonts/" 2>/dev/null || true
        echo "Copied system font configuration"
    fi
    
    echo "Font rendering libraries packaged for embedded deployment"
else
    echo "Warning: Font libraries not found for architecture $ARCH"
    echo "AppImage may require fontconfig to be installed on target system"
fi

# Copy input device libraries (required by linuxfb platform plugin)
echo "Copying input device libraries for linuxfb..."
cp -d "/lib/${ARCH}-linux-gnu"/libudev.so* "$APPDIR/usr/lib/" 2>/dev/null || \
    cp -d "/usr/lib/${ARCH}-linux-gnu"/libudev.so* "$APPDIR/usr/lib/" 2>/dev/null || true

cp -d "/lib/${ARCH}-linux-gnu"/libinput.so* "$APPDIR/usr/lib/" 2>/dev/null || \
    cp -d "/usr/lib/${ARCH}-linux-gnu"/libinput.so* "$APPDIR/usr/lib/" 2>/dev/null || true

cp -d "/lib/${ARCH}-linux-gnu"/libmtdev.so* "$APPDIR/usr/lib/" 2>/dev/null || \
    cp -d "/usr/lib/${ARCH}-linux-gnu"/libmtdev.so* "$APPDIR/usr/lib/" 2>/dev/null || true

cp -d "/lib/${ARCH}-linux-gnu"/libevdev.so* "$APPDIR/usr/lib/" 2>/dev/null || \
    cp -d "/usr/lib/${ARCH}-linux-gnu"/libevdev.so* "$APPDIR/usr/lib/" 2>/dev/null || true

cp -d "/lib/${ARCH}-linux-gnu"/libxkbcommon.so* "$APPDIR/usr/lib/" 2>/dev/null || \
    cp -d "/usr/lib/${ARCH}-linux-gnu"/libxkbcommon.so* "$APPDIR/usr/lib/" 2>/dev/null || true

cp -d "/lib/${ARCH}-linux-gnu"/libdrm.so* "$APPDIR/usr/lib/" 2>/dev/null || \
    cp -d "/usr/lib/${ARCH}-linux-gnu"/libdrm.so* "$APPDIR/usr/lib/" 2>/dev/null || true

cp -d "/lib/${ARCH}-linux-gnu"/libwacom.so* "$APPDIR/usr/lib/" 2>/dev/null || \
    cp -d "/usr/lib/${ARCH}-linux-gnu"/libwacom.so* "$APPDIR/usr/lib/" 2>/dev/null || true

# Copy GLib libraries (required by libinput)
cp -d "/lib/${ARCH}-linux-gnu"/libglib-2.0.so* "$APPDIR/usr/lib/" 2>/dev/null || \
    cp -d "/usr/lib/${ARCH}-linux-gnu"/libglib-2.0.so* "$APPDIR/usr/lib/" 2>/dev/null || true

cp -d "/lib/${ARCH}-linux-gnu"/libgobject-2.0.so* "$APPDIR/usr/lib/" 2>/dev/null || \
    cp -d "/usr/lib/${ARCH}-linux-gnu"/libgobject-2.0.so* "$APPDIR/usr/lib/" 2>/dev/null || true

cp -d "/lib/${ARCH}-linux-gnu"/libgudev-1.0.so* "$APPDIR/usr/lib/" 2>/dev/null || \
    cp -d "/usr/lib/${ARCH}-linux-gnu"/libgudev-1.0.so* "$APPDIR/usr/lib/" 2>/dev/null || true

# Copy additional system libraries
cp -d "/lib/${ARCH}-linux-gnu"/libdbus-1.so* "$APPDIR/usr/lib/" 2>/dev/null || \
    cp -d "/usr/lib/${ARCH}-linux-gnu"/libdbus-1.so* "$APPDIR/usr/lib/" 2>/dev/null || true

cp -d "/lib/${ARCH}-linux-gnu"/libsystemd.so* "$APPDIR/usr/lib/" 2>/dev/null || \
    cp -d "/usr/lib/${ARCH}-linux-gnu"/libsystemd.so* "$APPDIR/usr/lib/" 2>/dev/null || true

cp -d "/lib/${ARCH}-linux-gnu"/libcap.so* "$APPDIR/usr/lib/" 2>/dev/null || \
    cp -d "/usr/lib/${ARCH}-linux-gnu"/libcap.so* "$APPDIR/usr/lib/" 2>/dev/null || true

cp -d "/lib/${ARCH}-linux-gnu"/libatomic.so* "$APPDIR/usr/lib/" 2>/dev/null || \
    cp -d "/usr/lib/${ARCH}-linux-gnu"/libatomic.so* "$APPDIR/usr/lib/" 2>/dev/null || true

cp -d "/lib/${ARCH}-linux-gnu"/libdouble-conversion.so* "$APPDIR/usr/lib/" 2>/dev/null || \
    cp -d "/usr/lib/${ARCH}-linux-gnu"/libdouble-conversion.so* "$APPDIR/usr/lib/" 2>/dev/null || true

cp -d "/lib/${ARCH}-linux-gnu"/libpcre2-*.so* "$APPDIR/usr/lib/" 2>/dev/null || \
    cp -d "/usr/lib/${ARCH}-linux-gnu"/libpcre2-*.so* "$APPDIR/usr/lib/" 2>/dev/null || true

cp -d "/lib/${ARCH}-linux-gnu"/libzstd.so* "$APPDIR/usr/lib/" 2>/dev/null || \
    cp -d "/usr/lib/${ARCH}-linux-gnu"/libzstd.so* "$APPDIR/usr/lib/" 2>/dev/null || true

cp -d "/lib/${ARCH}-linux-gnu"/libffi.so* "$APPDIR/usr/lib/" 2>/dev/null || \
    cp -d "/usr/lib/${ARCH}-linux-gnu"/libffi.so* "$APPDIR/usr/lib/" 2>/dev/null || true

echo "Input device libraries packaged for embedded deployment"

mkdir -p "$APPDIR/usr/qml/QtCore/"
cp -d "$QT_DIR/qml/QtCore/"* "$APPDIR/usr/qml/QtCore/" 2>/dev/null || true
mkdir -p "$APPDIR/usr/qml/Qt/labs/folderlistmodel/"
cp -d "$QT_DIR/qml/Qt/labs/folderlistmodel/"* "$APPDIR/usr/qml/Qt/labs/folderlistmodel/" 2>/dev/null || true

mkdir -p "$APPDIR/usr/plugins/platforms"
cp "$QT_DIR/plugins/platforms/libqlinuxfb.so" "$APPDIR/usr/plugins/platforms/" 2>/dev/null || true
cp -r "$QT_DIR/plugins/tls" "$APPDIR/usr/plugins/" 2>/dev/null || true

# Copy only essential image format plugins (consistency with all platforms)
# Includes: JPEG, PNG, SVG (common formats + icons)
# Excludes: TIFF, WebP, GIF (less common, saves space on embedded systems)
mkdir -p "$APPDIR/usr/plugins/imageformats"
cp "$QT_DIR/plugins/imageformats/libqjpeg.so" "$APPDIR/usr/plugins/imageformats/" 2>/dev/null || true
cp "$QT_DIR/plugins/imageformats/libqpng.so" "$APPDIR/usr/plugins/imageformats/" 2>/dev/null || true
cp "$QT_DIR/plugins/imageformats/libqsvg.so" "$APPDIR/usr/plugins/imageformats/" 2>/dev/null || true

mkdir -p "$APPDIR/usr/qml/QtQuick/Controls/Material/impl"
mkdir -p "$APPDIR/usr/qml/QtQuick/Controls/impl"
mkdir -p "$APPDIR/usr/qml/QtQuick/Controls/Basic/impl"
cp "$QT_DIR/qml/QtQuick/Controls/libqtquickcontrols2plugin.so" "$APPDIR/usr/qml/QtQuick/Controls/" 2>/dev/null || true
cp "$QT_DIR/qml/QtQuick/Controls/Material/impl/libqtquickcontrols2materialstyleimplplugin.so" "$APPDIR/usr/qml/QtQuick/Controls/Material/impl/" 2>/dev/null || true
cp "$QT_DIR/qml/QtQuick/Controls/impl/libqtquickcontrols2implplugin.so" "$APPDIR/usr/qml/QtQuick/Controls/impl/" 2>/dev/null || true
cp "$QT_DIR/qml/QtQuick/Controls/Basic/impl/libqtquickcontrols2basicstyleimplplugin.so" "$APPDIR/usr/qml/QtQuick/Controls/Basic/impl/" 2>/dev/null || true
cp "$QT_DIR/qml/QtQuick/Controls/Basic/libqtquickcontrols2basicstyleplugin.so" "$APPDIR/usr/qml/QtQuick/Controls/Basic/" 2>/dev/null || true
cp "$QT_DIR/qml/QtQuick/Controls/Material/libqtquickcontrols2materialstyleplugin.so" "$APPDIR/usr/qml/QtQuick/Controls/Material/" 2>/dev/null || true

# Copy ICU libraries (using detected version)
echo "Copying ICU $ICU_VERSION libraries..."
ICU_LIB_DIR="$PWD/qt/icu/icu4c/source/lib"
if [ -d "$ICU_LIB_DIR" ]; then
    # Copy ICU libraries with proper version
    cp "$ICU_LIB_DIR/libicudata.so.$ICU_MAJOR_VERSION" "$APPDIR/usr/lib/" 2>/dev/null || \
        echo "Warning: Could not find libicudata.so.$ICU_MAJOR_VERSION"
    cp "$ICU_LIB_DIR/libicui18n.so.$ICU_MAJOR_VERSION" "$APPDIR/usr/lib/" 2>/dev/null || \
        echo "Warning: Could not find libicui18n.so.$ICU_MAJOR_VERSION"
    cp "$ICU_LIB_DIR/libicuuc.so.$ICU_MAJOR_VERSION" "$APPDIR/usr/lib/" 2>/dev/null || \
        echo "Warning: Could not find libicuuc.so.$ICU_MAJOR_VERSION"
    
    # Create symlinks without version for compatibility
    (cd "$APPDIR/usr/lib" && \
        [ -f "libicudata.so.$ICU_MAJOR_VERSION" ] && ln -sf "libicudata.so.$ICU_MAJOR_VERSION" "libicudata.so" || true && \
        [ -f "libicui18n.so.$ICU_MAJOR_VERSION" ] && ln -sf "libicui18n.so.$ICU_MAJOR_VERSION" "libicui18n.so" || true && \
        [ -f "libicuuc.so.$ICU_MAJOR_VERSION" ] && ln -sf "libicuuc.so.$ICU_MAJOR_VERSION" "libicuuc.so" || true)
else
    echo "Warning: ICU libraries not found at $ICU_LIB_DIR"
    echo "You may need to build Qt with ICU support first"
fi

mkdir -p "$APPDIR/usr/share/fonts/truetype/dejavu"
mkdir -p "$APPDIR/usr/share/fonts/truetype/freefont"
mkdir -p "$APPDIR/usr/share/fonts/truetype/droid"
mkdir -p "$APPDIR/usr/share/fonts/truetype/roboto"

# Copy DejaVu fonts (default Qt fallback)
cp src/fonts/DejaVuSans.ttf "$APPDIR/usr/share/fonts/truetype/dejavu"
cp src/fonts/DejaVuSans-Bold.ttf "$APPDIR/usr/share/fonts/truetype/dejavu"

# Copy FreeSans (additional fallback)
cp src/fonts/FreeSans.ttf "$APPDIR/usr/share/fonts/truetype/freefont"

# Copy Roboto fonts (Qt Material style default - CRITICAL for embedded!)
cp src/fonts/Roboto-Regular.ttf "$APPDIR/usr/share/fonts/truetype/roboto"
cp src/fonts/Roboto-Bold.ttf "$APPDIR/usr/share/fonts/truetype/roboto"
cp src/fonts/Roboto-Light.ttf "$APPDIR/usr/share/fonts/truetype/roboto"

# Copy Droid Sans Fallback (comprehensive Unicode support for special characters)
if [ "$INCLUDE_CJK_FONTS" -eq 1 ]; then
    echo "Including CJK font support (+4MB)"
    cp src/fonts/DroidSansFallbackFull.ttf "$APPDIR/usr/share/fonts/truetype/droid"
else
    echo "Skipping CJK fonts (use --include-cjk-fonts to enable)"
fi

# Create custom fontconfig configuration for embedded deployment
# This ensures Qt Material Controls can find our bundled fonts
# XDG_DATA_DIRS in AppRun will point fontconfig to our bundled fonts
cat > "$APPDIR/etc/fonts/local.conf" << 'FONTCONFIG_EOF'
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
    mkdir -p "$APPDIR/usr/qml"
    cp -r "$QT_DIR/qml/QtQuick" "$APPDIR/usr/qml/" 2>/dev/null || true
    cp -r "$QT_DIR/qml/QtQuick.2" "$APPDIR/usr/qml/" 2>/dev/null || true
    cp -r "$QT_DIR/qml/QtQml" "$APPDIR/usr/qml/" 2>/dev/null || true
fi
#fi

# Embedded-specific optimizations
echo "Applying embedded system optimizations..."

# Remove unused QML Controls themes (size optimization)
rm -rf "$APPDIR/usr/qml/QtQuick/Controls/Universal"
rm -rf "$APPDIR/usr/qml/QtQuick/Controls/Fusion"
rm -rf "$APPDIR/usr/qml/QtQuick/Controls/Imagine"
rm -rf "$APPDIR/usr/qml/QtQuick/Controls/FluentWinUI3"

# QtWidgets already excluded during copy phase - no removal needed

# Remove QML debugging tools (development-only, especially important for embedded)
rm -rf "$APPDIR/usr/qml/QtTest"* 2>/dev/null || true
rm -rf "$APPDIR/usr/plugins/qmltooling" 2>/dev/null || true

# Remove Qt translations (not needed on embedded systems)
rm -rf "$APPDIR/usr/translations" 2>/dev/null || true
rm -rf "$APPDIR/usr/share/qt6/translations" 2>/dev/null || true

# Remove unnecessary image format plugins (consistency with all platforms)
# Excludes: TIFF, WebP, GIF (less common formats)
# Keeps: JPEG, PNG, SVG (common formats + icons)
rm -f "$APPDIR/usr/plugins/imageformats/libqtiff.so" 2>/dev/null || true
rm -f "$APPDIR/usr/plugins/imageformats/libqwebp.so" 2>/dev/null || true
rm -f "$APPDIR/usr/plugins/imageformats/libqgif.so" 2>/dev/null || true

# Remove unused Qt Quick Controls 2 style libraries (critical size optimization for embedded)
rm -f "$APPDIR/usr/lib/libQt6QuickControls2Fusion.so"* 2>/dev/null || true
rm -f "$APPDIR/usr/lib/libQt6QuickControls2Universal.so"* 2>/dev/null || true
rm -f "$APPDIR/usr/lib/libQt6QuickControls2Imagine.so"* 2>/dev/null || true
rm -f "$APPDIR/usr/lib/libQt6QuickControls2FluentWinUI3.so"* 2>/dev/null || true
rm -f "$APPDIR/usr/lib/libQt6QuickControls2FusionStyleImpl.so"* 2>/dev/null || true
rm -f "$APPDIR/usr/lib/libQt6QuickControls2UniversalStyleImpl.so"* 2>/dev/null || true
rm -f "$APPDIR/usr/lib/libQt6QuickControls2ImagineStyleImpl.so"* 2>/dev/null || true
rm -f "$APPDIR/usr/lib/libQt6QuickControls2FluentWinUI3StyleImpl.so"* 2>/dev/null || true
rm -f "$APPDIR/usr/lib/libQt6QuickControls2WindowsStyleImpl.so"* 2>/dev/null || true
rm -f "$APPDIR/usr/lib/libQt6Widgets.so"* 2>/dev/null || true

# Remove desktop-specific libraries that may have been included
rm -f "$APPDIR/usr/lib/libwayland"* 2>/dev/null || true
rm -f "$APPDIR/usr/lib/libX11"* 2>/dev/null || true
rm -f "$APPDIR/usr/lib/libxcb"* 2>/dev/null || true
rm -rf "$APPDIR/usr/plugins/platforms/libqwayland"* 2>/dev/null || true
rm -rf "$APPDIR/usr/plugins/platforms/libqxcb"* 2>/dev/null || true

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

echo "Creating embedded AppImage..."
# Remove old symlinks for embedded variant only
rm -f "$PWD/rpi-imager-embedded.AppImage"
rm -f "$PWD/rpi-imager-embedded-$ARCH.AppImage"

if [ -n "$LINUXDEPLOY" ] && [ -f "$LINUXDEPLOY" ]; then
    # Create AppImage using linuxdeploy
    # Explicitly specify the desktop file to ensure correct naming
    LD_LIBRARY_PATH="$QT_DIR/lib:$LD_LIBRARY_PATH" "$LINUXDEPLOY" --appdir="$APPDIR" \
        --desktop-file="$APPDIR/usr/share/applications/com.raspberrypi.rpi-imager-embedded.desktop" \
        --output=appimage \
        --exclude-library="libwayland-*" \
        --exclude-library="libX11*" \
        --exclude-library="libxcb*" \
        --exclude-library="libXext*" \
        --exclude-library="libLLVM*" \
        --exclude-library="libgallium*" \
        --exclude-library="libXrender*" \
        --exclude-library="libicudata*"
    
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
        # Only rename if this contains "Embedded" (linuxdeploy creates Raspberry_Pi_Imager_(Embedded)-arch.AppImage)
        case "$appimage" in
            *"Embedded"*"${ARCH}"*)
                echo "Renaming '$appimage' to '$(basename "$OUTPUT_FILE")'"
                mv "$appimage" "$OUTPUT_FILE"
                RENAMED=true
                break
                ;;
        esac
    done
    
    # Check if we successfully found/created the output file
    if [ "$RENAMED" = "false" ] && [ ! -f "$OUTPUT_FILE" ]; then
        echo "Warning: Could not find Embedded AppImage to rename. Looking for any matching AppImage..."
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
    echo "Embedded AppImage created at $OUTPUT_FILE"
    
    # Create symlinks for debian packaging and user convenience
    # Primary symlink matches debian/rpi-imager-embedded.install expectations
    DEBIAN_SYMLINK="$PWD/rpi-imager-embedded.AppImage"
    if [ -L "$DEBIAN_SYMLINK" ] || [ -f "$DEBIAN_SYMLINK" ]; then
        rm -f "$DEBIAN_SYMLINK"
    fi
    ln -s "$(basename "$OUTPUT_FILE")" "$DEBIAN_SYMLINK"
    echo "Created symlink: $DEBIAN_SYMLINK -> $(basename "$OUTPUT_FILE")"
    
    # Additional architecture-specific symlink for clarity when building for multiple architectures
    ARCH_SYMLINK="$PWD/rpi-imager-embedded-$ARCH.AppImage"
    if [ -L "$ARCH_SYMLINK" ] || [ -f "$ARCH_SYMLINK" ]; then
        rm -f "$ARCH_SYMLINK"
    fi
    ln -s "$(basename "$OUTPUT_FILE")" "$ARCH_SYMLINK"
    echo "Created symlink: $ARCH_SYMLINK -> $(basename "$OUTPUT_FILE")"
    
    echo ""
    echo "Embedded AppImage build completed successfully for $ARCH architecture."
    echo ""
    echo "This AppImage is optimized for embedded systems:"
    echo "  - Uses linuxfb for direct rendering (no X11/Wayland required)"
    echo "  - Smaller size with desktop libraries excluded"
    echo "  - Configured for headless/embedded environments"
    echo ""
    echo "To run on embedded systems:"
    echo "  - Ensure /dev/dri/card1 is available"
    echo "  - Run directly: ./$(basename "$OUTPUT_FILE")"
    echo "  - No desktop environment required"
else
    echo "AppImage creation completed, but output file verification failed."
    echo "Check the build process for any errors."
fi
