#!/bin/bash
set -e

# Script to create AppImage for embedded systems using linuxfb as a renderer
# This creates an AppImage that runs with direct rendering (no window manager required)

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
    echo "This script creates an AppImage optimized for embedded systems:"
    echo "  - Uses linuxfb for direct rendering (no X11/Wayland required)"
    echo "  - Optimized for headless/embedded environments"
    echo "  - Smaller size by excluding desktop-specific libraries"
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

echo "Building embedded AppImage for architecture: $ARCH"

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

echo "Building $PROJECT_NAME version $PROJECT_VERSION for embedded systems"

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

# Configuration
BUILD_TYPE="MinSizeRel"  # Optimize for size in embedded systems
QML_SOURCES_PATH="$PWD/src/qmlcomponents/"

# Location of AppDir and output file
APPDIR="$PWD/AppDir-embedded-$ARCH"
OUTPUT_FILE="$PWD/Raspberry_Pi_Imager-${PROJECT_VERSION}-embedded-${ARCH}.AppImage"

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
make -j$(nproc)

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
# Update the desktop file for embedded use
sed -i 's|Name=.*|Name=Raspberry Pi Imager (Embedded)|' "$APPDIR/usr/share/applications/com.raspberrypi.rpi-imager-embedded.desktop"
sed -i 's|Comment=.*|Comment=Raspberry Pi Imager for embedded systems|' "$APPDIR/usr/share/applications/com.raspberrypi.rpi-imager-embedded.desktop"
sed -i 's|Exec=.*|Exec=rpi-imager|' "$APPDIR/usr/share/applications/com.raspberrypi.rpi-imager-embedded.desktop"

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

# Disable desktop-specific features for embedded use
export QT_QUICK_CONTROLS_STYLE=Material
export QT_SCALE_FACTOR=1.0

# GPU memory optimization for embedded systems
export QT_QUICK_BACKEND=software
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
# cp -d "$QT_DIR/lib/libQt6Widgets.so"* "$APPDIR/usr/lib/"    # QtWidgets excluded
cp -d "$QT_DIR/lib/libQt6Quick.so"* "$APPDIR/usr/lib/"
cp -d "$QT_DIR/lib/libQt6Qml.so"* "$APPDIR/usr/lib/"
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

cp "$PWD/qt/icu/icu4c/source/lib/libicudata.so.72" "$APPDIR/usr/lib/libicudata.so.72"

mkdir -p "$APPDIR/usr/share/fonts/truetype/dejavu"
mkdir -p "$APPDIR/usr/share/fonts/truetype/freefont"
mkdir -p "$APPDIR/usr/share/fonts/truetype/droid"

cp src/fonts/DejaVuSans.ttf "$APPDIR/usr/share/fonts/truetype/dejavu"
cp src/fonts/DejaVuSans-Bold.ttf "$APPDIR/usr/share/fonts/truetype/dejavu"
cp src/fonts/FreeSans.ttf "$APPDIR/usr/share/fonts/truetype/freefont"
# cp src/fonts/DroidSansFallbackFull.ttf "$APPDIR/usr/share/fonts/truetype/droid"

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
pushd "$APPDIR"
echo $(find . -name "*.so*")
strip --strip-unneeded $(find . -name "*.so*")
popd

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
        if [[ "$appimage" == *"Embedded"* ]] && [[ "$appimage" == *"${ARCH}"* ]]; then
            echo "Renaming '$appimage' to '$(basename "$OUTPUT_FILE")'"
            mv "$appimage" "$OUTPUT_FILE"
            RENAMED=true
            break
        fi
    done
    
    # Check if we successfully found/created the output file
    if [ "$RENAMED" = false ] && [ ! -f "$OUTPUT_FILE" ]; then
        echo "Warning: Could not find Embedded AppImage to rename. Looking for any matching AppImage..."
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

