#!/bin/bash
#
# Script to download and build Qt with Debian-like configuration
# Specifically designed for Raspberry Pi OS (and other Debian-based distributions)
#

set -e

# Default values
QT_VERSION="6.9.1"    # 6.9.1 is the latest version as of 2025-08-04
PREFIX="/opt/Qt/${QT_VERSION}"     # Default installation prefix with capital 'Q'
CORES=$(nproc)        # Default to all available CPU cores
CLEAN_BUILD=1         # Clean the build directory by default
BUILD_TYPE="MinSizeRel"  # Default build type
SKIP_DEPENDENCIES=0   # Don't skip installing dependencies by default
RPI_OPTIMIZED=0       # Raspberry Pi optimizations disabled by default
VERBOSE_BUILD=0       # By default, don't show verbose build output
UNPRIVILEGED=0        # By default, allow sudo usage

# Parse command line arguments
usage() {
    echo "Usage: $0 [options]"
    echo "Options:"
    echo "  --version=VERSION    Qt version to build (default: $QT_VERSION)"
    echo "  --prefix=PREFIX      Installation prefix (default: $PREFIX)"
    echo "  --cores=CORES        Number of CPU cores to use (default: $CORES)"
    echo "  --no-clean           Don't clean the build directory"
    echo "  --debug              Build with debug information"
    echo "  --skip-dependencies  Skip installing build dependencies"
    echo "  --rpi-optimize       Apply Raspberry Pi specific optimizations"
    echo "  --verbose            Show verbose build output"
    echo "  --unprivileged       Run without sudo (skips dependency installation)"
    echo "  -h, --help           Show this help message"
    exit 1
}

BUILD_EXAMPLES=OFF

for arg in "$@"; do
    case $arg in
        --version=*)
            QT_VERSION="${arg#*=}"
            ;;
        --prefix=*)
            PREFIX="${arg#*=}"
            ;;
        --cores=*)
            CORES="${arg#*=}"
            ;;
        --build-examples)
            BUILD_EXAMPLES=ON
            ;;
        --no-clean)
            CLEAN_BUILD=0
            ;;
        --debug)
            BUILD_TYPE="Debug"
            ;;
        --skip-dependencies)
            SKIP_DEPENDENCIES=1
            ;;
        --rpi-optimize)
            RPI_OPTIMIZED=1
            ;;
        --verbose)
            VERBOSE_BUILD=1
            ;;
        --unprivileged)
            UNPRIVILEGED=1
            SKIP_DEPENDENCIES=1  # Automatically skip dependencies in unprivileged mode
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

# Validate inputs
if ! [[ $CORES =~ ^[0-9]+$ ]]; then
    echo "Error: Cores must be a number"
    exit 1
fi

# In unprivileged mode, suggest a user-writable prefix if using default
if [ "$UNPRIVILEGED" -eq 1 ] && [ "$PREFIX" = "/opt/Qt/${QT_VERSION}" ]; then
    PREFIX="$HOME/Qt/${QT_VERSION}"
    echo "Unprivileged mode: Using user-writable prefix: $PREFIX"
fi

# Major version (for directory structure)
QT_MAJOR_VERSION=$(echo "$QT_VERSION" | cut -d. -f1)

# Detect system architecture and set appropriate directory suffix
ARCH=$(uname -m)
if [ "$ARCH" = "x86_64" ]; then
    PREFIX="$PREFIX/gcc_64"
elif [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "arm64" ]; then
    PREFIX="$PREFIX/gcc_arm64"
elif [[ "$ARCH" == arm* ]]; then
    PREFIX="$PREFIX/gcc_arm32"
fi

# Detect Raspberry Pi model and set appropriate optimization flags
RPI_MODEL=""
RPI_CFLAGS=""
if [ -f /proc/cpuinfo ] && grep -q "Raspberry Pi" /proc/cpuinfo; then
    echo "Detected Raspberry Pi system"
    RPI_MODEL=$(grep "Model" /proc/cpuinfo | sed 's/.*: //')
    echo "  Model: $RPI_MODEL"
    
    # Set optimization flags based on model
    if echo "$RPI_MODEL" | grep -q "Pi 4"; then
        echo "  Optimizing for Raspberry Pi 4"
        # Cortex-A72 optimization for Pi 4
        RPI_CFLAGS="-march=armv8-a+crc -mtune=cortex-a72 -mfpu=neon-fp-armv8"
    elif echo "$RPI_MODEL" | grep -q "Pi 3"; then
        echo "  Optimizing for Raspberry Pi 3"
        # Cortex-A53 optimization for Pi 3
        RPI_CFLAGS="-march=armv8-a+crc -mtune=cortex-a53 -mfpu=neon-fp-armv8"
    elif echo "$RPI_MODEL" | grep -q "Pi 2"; then
        echo "  Optimizing for Raspberry Pi 2"
        # Cortex-A7 optimization for Pi 2
        RPI_CFLAGS="-march=armv7-a -mtune=cortex-a7 -mfpu=neon-vfpv4"
    elif echo "$RPI_MODEL" | grep -q "Pi 5"; then
        echo "  Optimizing for Raspberry Pi 5"
        # Cortex-A76 optimization for Pi 5
        RPI_CFLAGS="-march=armv8.2-a+crypto -mtune=cortex-a76"
    fi
fi

echo "Qt build configuration:"
echo "  Version: $QT_VERSION"
echo "  Prefix: $PREFIX"
echo "  Cores: $CORES"
echo "  Build Type: $BUILD_TYPE"
echo "  Clean Build: $CLEAN_BUILD"
echo "  Raspberry Pi Optimizations: $RPI_OPTIMIZED"
echo "  Verbose Build: $VERBOSE_BUILD"
echo "  Unprivileged Mode: $UNPRIVILEGED"
if [ -n "$RPI_CFLAGS" ]; then
    echo "  Raspberry Pi CFLAGS: $RPI_CFLAGS"
fi

# Install dependencies if not skipped
if [ "$SKIP_DEPENDENCIES" -eq 0 ]; then
    echo "Installing build dependencies..."
    # Basic build tools
    sudo apt-get update
    sudo apt-get install -y build-essential perl python3 git cmake ninja-build pkg-config

    # Qt dependencies - based on Debian's build dependencies for Qt
    sudo apt-get install -y \
        `# Build tools` \
        bison flex gperf \
        `# Keyboard and input` \
        libinput-dev libxkbcommon-dev libxkbcommon-x11-dev \
        `# Font and text rendering` \
        libfontconfig1-dev libfreetype6-dev libicu-dev \
        `# Image and media formats` \
        libjpeg-dev libpng-dev zlib1g-dev \
        `# Security and crypto` \
        libnss3-dev libssl-dev \
        `# System libraries` \
        libdbus-1-dev libglib2.0-dev libsqlite3-dev \
        `# Utility libraries` \
        libdouble-conversion-dev libpcre2-dev \
        `# Accessibility` \
        libatk1.0-dev libatk-bridge2.0-dev \

    # Additional dependencies for Raspberry Pi
    if [ "$RPI_OPTIMIZED" -eq 1 ] && [ -n "$RPI_MODEL" ]; then
        echo "Installing Raspberry Pi specific dependencies..."
        sudo apt-get install -y libraspberrypi-dev
    fi
else
    if [ "$UNPRIVILEGED" -eq 1 ]; then
        echo "Skipping dependency installation (unprivileged mode)"
        echo "Please ensure all Qt build dependencies are already installed."
    else
        echo "Skipping dependency installation as requested"
    fi
fi

# Create directories
DOWNLOAD_DIR="$PWD/qt-src"
BUILD_DIR="$PWD/qt-build"
BASE_DIR="$(cd "$(dirname "$0")" >/dev/null 2>&1 && pwd)"
mkdir -p "$DOWNLOAD_DIR" "$BUILD_DIR"

# Download Qt source code
echo "Downloading Qt $QT_VERSION source code..."
cd "$DOWNLOAD_DIR"
if [ ! -d "qt-everywhere-src-$QT_VERSION" ]; then
    if [ ! -f "qt-everywhere-src-$QT_VERSION.tar.xz" ]; then
        wget "https://download.qt.io/official_releases/qt/${QT_VERSION%.*}/$QT_VERSION/single/qt-everywhere-src-$QT_VERSION.tar.xz"
    fi
    echo "Extracting Qt source..."
    tar xf "qt-everywhere-src-$QT_VERSION.tar.xz"
fi

# Apply any needed patches for Raspberry Pi
if [ "$RPI_OPTIMIZED" -eq 1 ] && [ -n "$RPI_MODEL" ]; then
    echo "Checking for Raspberry Pi specific patches..."
    # You can add specific patches for Pi here if needed
fi

# Clean build directory if requested
if [ "$CLEAN_BUILD" -eq 1 ]; then
    echo "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR"
fi

if [ -d "$BASE_DIR/icu/icu4c/source/lib" ]; then
    echo "ICU already built"
else
    echo "Building ICU..."

    cd "$BASE_DIR"
    echo "Building custom ICU..."
    # Compile Languages List
    LANG_DIR="$BASE_DIR/src/i18n"

    pushd $LANG_DIR
    # shellcheck disable=SC2207
    LANGUAGES=($(find . -maxdepth 1 -name "*.ts" | grep -oP 'rpi-imager_\K[^.]+' | sort -u))

    JSON_INCLUDELIST=""
    for lang in "${LANGUAGES[@]}"; do
        if [[ -z "$JSON_INCLUDELIST" ]]; then
            JSON_INCLUDELIST="\"$lang\""
        else
            JSON_INCLUDELIST="${JSON_INCLUDELIST}, \"$lang\""
        fi
    done
    popd
    cat << EOF > "$BASE_DIR/language_filters.json"
    {
    "localeFilter": {
        "filterType": "language",
        "includelist": [
        $JSON_INCLUDELIST
        ]
    },
    "featureFilters": {
        "locales_tree": "exclude",
        "brkitr_dictionaries": "exclude",
        "translit": "exclude",
        "region_tree": "exclude",
        "lang_tree": "exclude",
        "curr_tree": "exclude",
        "coll_tree": "exclude",
        "conversion_mappings": "exclude"
    }
    }
EOF
    echo "Language filters: $JSON_INCLUDELIST"

    git clone https://github.com/unicode-org/icu.git
    cd "$BASE_DIR/icu/icu4c/source"
    git checkout release-72-1
    rm -rf data
    wget https://github.com/unicode-org/icu/releases/download/release-72-1/icu4c-72_1-data.zip
    unzip icu4c-72_1-data.zip
    ICU_DATA_FILTER_FILE="$BASE_DIR/language_filters.json" ./runConfigureICU Linux
    make -j"$CORES"
fi

# Configure and build Qt
cd "$BASE_DIR"

# Set up environment variables for compilation
if [ "$RPI_OPTIMIZED" -eq 1 ] && [ -n "$RPI_CFLAGS" ]; then
    export CFLAGS="$RPI_CFLAGS"
    export CXXFLAGS="$RPI_CFLAGS"
    echo "Using CFLAGS: $CFLAGS"
    echo "Using CXXFLAGS: $CXXFLAGS"
fi

# Base configuration options - simplified and closer to Debian's configuration
CONFIG_OPTS=(
    -prefix "$PREFIX"
    -opensource
    -confirm-license
    -no-glib
    -optimize-size
    -release
    -make libs
    -skip qt3d
    -skip qtandroidextras
    -skip qtwinextras
)

# Add build type specific options
if [ "$BUILD_TYPE" = "Debug" ]; then
    CONFIG_OPTS+=(-debug)
elif [ "$BUILD_TYPE" = "MinSizeRel" ]; then
    CONFIG_OPTS+=(-release -optimize-size)
else
    # Default to Release
    CONFIG_OPTS+=(-release)
fi

# Platform-specific configuration
CONFIG_OPTS+=(
    -no-opengl
    -qpa linuxfb
)

echo "Excluding Stage"
# Prefer embedded-specific exclusion lists; fall back to generic if not present
FEATURES_LIST="$BASE_DIR/features_exclude.embedded.list"
IFS=', ' read -r -a array <<< "$(< $FEATURES_LIST tr '\n' ', ')"

for feature in "${array[@]}"
do
    CONFIG_OPTS+=(-no-feature-"${feature}")
    echo "Excluding -no-feature-${feature}"
done
MODULES_LIST="$BASE_DIR/modules_exclude.embedded.list"
IFS=', ' read -r -a array <<< "$(< $MODULES_LIST tr '\n' ', ')"

for module in "${array[@]}"
do
    CONFIG_OPTS+=(-skip "${module}")
    echo "Excluding -skip ${module}"
done

# Configure and build Qt
cd "$BUILD_DIR"


CONFIG_OPTS+=(
        --
        -DQT_BUILD_TESTS=OFF
        -DQT_BUILD_EXAMPLES="${BUILD_EXAMPLES}"
        -DICU_ROOT="$BASE_DIR/icu/icu4c/source/lib"
)

# Run the configure script with verbose output if requested
echo "Configuring Qt with options: ${CONFIG_OPTS[*]}"
if [ "$VERBOSE_BUILD" -eq 1 ]; then
    "$DOWNLOAD_DIR/qt-everywhere-src-$QT_VERSION/configure" "${CONFIG_OPTS[@]}" -verbose
else
    "$DOWNLOAD_DIR/qt-everywhere-src-$QT_VERSION/configure" "${CONFIG_OPTS[@]}"
fi

# Check configure status
if [ $? -ne 0 ]; then
    echo "Configure failed. Try with --verbose for more details."
    echo "You may want to remove problematic flags and try again."
    exit 1
fi

# Build Qt
echo "Building Qt (this may take a while)..."
if [ "$VERBOSE_BUILD" -eq 1 ]; then
    cmake --build . --parallel "$CORES" --verbose
else
    cmake --build . --parallel "$CORES"
fi

# Check build status
if [ $? -ne 0 ]; then
    echo "Build failed. Try with --verbose for more details."
    exit 1
fi

# Install Qt
echo "Installing Qt to $PREFIX..."
if [ "$UNPRIVILEGED" -eq 1 ]; then
    # Create prefix directory if it doesn't exist (without sudo)
    mkdir -p "$(dirname "$PREFIX")"
    cmake --install .
else
    sudo cmake --install .
fi

echo "Qt $QT_VERSION has been built and installed to $PREFIX"

if [ "$UNPRIVILEGED" -eq 1 ]; then
    echo ""
    echo "UNPRIVILEGED MODE NOTES:"
    echo "- Dependencies were not automatically installed"
    echo "- Make sure you have all required Qt build dependencies installed"
    echo "- Common missing dependencies can cause build failures"
    echo "- If you encounter build errors, install dependencies manually with:"
    echo "  sudo apt-get install build-essential perl python3 git cmake ninja-build pkg-config libfontconfig1-dev libfreetype6-dev libx11-dev [and others...]"
    echo ""
fi

echo "To use it, you may want to set the following environment variables:"
echo "  export PATH=\"$PREFIX/bin:\$PATH\""
echo "  export LD_LIBRARY_PATH=\"$PREFIX/lib:\$LD_LIBRARY_PATH\""
echo "  export QT_PLUGIN_PATH=\"$PREFIX/plugins\""
echo "  export QML_IMPORT_PATH=\"$PREFIX/qml\""

# Create a qtenv.sh script for setting up the environment
cat > "$PREFIX/bin/qtenv.sh" << EOF
#!/bin/bash
# Source this file to set up the Qt environment
export PATH="$PREFIX/bin:\$PATH"
export LD_LIBRARY_PATH="$PREFIX/lib:\$LD_LIBRARY_PATH"
export QT_PLUGIN_PATH="$PREFIX/plugins"
export QML_IMPORT_PATH="$PREFIX/qml"
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:\$PKG_CONFIG_PATH"
export CMAKE_PREFIX_PATH="$PREFIX:\$CMAKE_PREFIX_PATH"
EOF

# Add final echo
cat >> "$PREFIX/bin/qtenv.sh" << EOF
echo "Qt $QT_VERSION environment initialized"
EOF

chmod +x "$PREFIX/bin/qtenv.sh"
echo "Created environment setup script at $PREFIX/bin/qtenv.sh"
echo "Source it with: source $PREFIX/bin/qtenv.sh"

# Create a CMake toolchain file for using this Qt with CMake
cat > "$PREFIX/qt$QT_MAJOR_VERSION-toolchain.cmake" << EOF
# CMake toolchain file for Qt $QT_VERSION
set(CMAKE_PREFIX_PATH "${PREFIX}" \${CMAKE_PREFIX_PATH})
set(QT_QMAKE_EXECUTABLE "${PREFIX}/bin/qmake")
set(Qt${QT_MAJOR_VERSION}_DIR "${PREFIX}/lib/cmake/Qt${QT_MAJOR_VERSION}")
EOF

echo "Created CMake toolchain file at $PREFIX/qt$QT_MAJOR_VERSION-toolchain.cmake"
echo "Use with: cmake -DCMAKE_TOOLCHAIN_FILE=$PREFIX/qt$QT_MAJOR_VERSION-toolchain.cmake ..."