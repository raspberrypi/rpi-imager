#!/bin/bash
#
# Script to download and build Qt for embedded systems using linuxfb
# Specifically designed for embedded/headless environments without X11/Wayland
#

set -e

# Source common configuration and functions
BASE_DIR="$(cd "$(dirname "$0")" >/dev/null 2>&1 && pwd)"
PROJECT_ROOT="$(cd "$BASE_DIR/.." && pwd)"
source "$BASE_DIR/qt-build-common.sh"

# Initialize common variables
init_common_variables

# Parse command line arguments
usage() {
    echo "Usage: $0 [options]"
    echo "Options:"
    print_common_usage_options
    echo ""
    echo "This script builds Qt optimized for embedded systems:"
    echo "  - Uses linuxfb for direct rendering (no X11/Wayland required)"
    echo "  - Optimized for headless/embedded environments"
    echo "  - Smaller size by excluding desktop-specific libraries"
    exit 1
}

# Parse common arguments first
parse_common_args "$@"

# Parse script-specific arguments (COMMON_REMAINING_ARGS is a space-separated string)
for arg in $COMMON_REMAINING_ARGS; do
    case $arg in
        -h|--help)
            usage
            ;;
        *)
            echo "Unknown option: $arg"
            usage
            ;;
    esac
done

# Validate common inputs
validate_common_inputs

# Set architecture-specific prefix with embedded suffix
set_arch_prefix_suffix "embedded"

# Print configuration
print_common_config "Qt embedded build"
echo "  Target: Embedded systems (linuxfb)"

# Install dependencies if not skipped
if [ "$SKIP_DEPENDENCIES" -eq 0 ]; then
    # Install basic Linux dependencies
    install_linux_basic_deps

    # Qt dependencies for embedded - minimal set without X11/Wayland
    echo "Installing Qt embedded dependencies..."
    sudo apt-get install -y \
        `# Keyboard and input (for linuxfb)` \
        libinput-dev libxkbcommon-dev \
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
        `# Graphics (DRM/KMS for some embedded systems)` \
        libdrm-dev libgbm-dev libegl1-mesa-dev libgles2-mesa-dev
else
    if [ "$UNPRIVILEGED" -eq 1 ]; then
        echo "Skipping dependency installation (unprivileged mode)"
        echo "Please ensure all Qt build dependencies are already installed."
    else
        echo "Skipping dependency installation as requested"
    fi
fi

# Create build directories
create_build_directories "embedded"

# Download Qt source code
download_qt_source

# Clean build directory if requested
clean_build_directory

# Build custom ICU if needed
if [ -d "$BASE_DIR/icu/icu4c/source/lib" ]; then
    echo "ICU already built"
else
    echo "Building custom ICU..."

    # Detect the appropriate ICU version for this Qt version
    ICU_VERSION=$(get_icu_version_for_qt "$QT_VERSION")
    ICU_TAG=$(icu_version_to_tag "$ICU_VERSION")
    ICU_DATA_ZIP=$(icu_version_to_data_package "$ICU_VERSION")
    
    echo "Using ICU version $ICU_VERSION (tag: $ICU_TAG) for Qt $QT_VERSION"

    cd "$BASE_DIR"
    # Compile Languages List from project i18n directory
    LANG_DIR="$PROJECT_ROOT/src/i18n"

    if [ -d "$LANG_DIR" ]; then
        pushd "$LANG_DIR" > /dev/null
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
        popd > /dev/null
        
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

        if [ ! -d "$BASE_DIR/icu" ]; then
            git clone https://github.com/unicode-org/icu.git
        fi
        cd "$BASE_DIR/icu/icu4c/source"
        
        # Checkout the appropriate ICU version
        echo "Checking out ICU $ICU_TAG..."
        git fetch --tags 2>/dev/null || true
        git checkout "$ICU_TAG" 2>/dev/null || {
            echo "Warning: Could not checkout $ICU_TAG, trying latest stable..."
            git checkout $(git describe --tags --abbrev=0) 2>/dev/null || true
        }
        
        if [ ! -d "data" ]; then
            echo "Downloading ICU data package $ICU_DATA_ZIP..."
            wget "https://github.com/unicode-org/icu/releases/download/$ICU_TAG/$ICU_DATA_ZIP" || {
                echo "Warning: Could not download $ICU_DATA_ZIP, using default data"
            }
            if [ -f "$ICU_DATA_ZIP" ]; then
                unzip -q "$ICU_DATA_ZIP"
            fi
        fi
        
        # Build ICU with language filters
        ICU_DATA_FILTER_FILE="$BASE_DIR/language_filters.json" ./runConfigureICU Linux
        make -j"$CORES"
    else
        echo "Warning: Language directory not found at $LANG_DIR, building ICU without language filters"
        
        # Build ICU without language filters
        ./runConfigureICU Linux
        make -j"$CORES"
    fi
fi

# Configure and build Qt
cd "$BUILD_DIR"

# Configure Qt with embedded-specific options
echo "Configuring Qt for embedded systems..."

# Base configuration - embedded with linuxfb
CONFIG_OPTS=(
    # Installation and build options
    -prefix "$PREFIX"
    -opensource
    -confirm-license
    -release  # Qt configure only accepts -release or -debug
    -optimize-size
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

# Embedded platform-specific configuration
CONFIG_OPTS+=(
    # Use linuxfb as the platform (no X11/Wayland)
    -no-opengl
    -qpa linuxfb
)

# Apply embedded-specific exclusions
apply_exclusions CONFIG_OPTS "$BASE_DIR/features_exclude.embedded.list" "$BASE_DIR/modules_exclude.embedded.list"

# Add CMake-specific options
CONFIG_OPTS+=(
    --
    -DQT_BUILD_TESTS=OFF
    -DQT_BUILD_EXAMPLES=OFF
)

# Add ICU if built
if [ -d "$BASE_DIR/icu/icu4c/source/lib" ]; then
    CONFIG_OPTS+=(-DICU_ROOT="$BASE_DIR/icu/icu4c/source/lib")
fi

# Run Qt configure
run_qt_configure CONFIG_OPTS

# Build Qt
build_qt

# Install Qt
install_qt

# Create environment and toolchain files
create_qt_env_script "embedded"
create_cmake_toolchain "embedded"

# Print final usage instructions
echo ""
echo "Qt embedded build completed successfully!"
echo ""
echo "Installation details:"
echo "  Location: $PREFIX"
echo "  Architecture: $ARCH"
echo "  Build type: $BUILD_TYPE"
echo "  Platform: linuxfb (no X11/Wayland required)"
echo ""
print_usage_instructions "Qt embedded"
echo ""
echo "This Qt build is optimized for embedded systems:"
echo "  - Uses linuxfb for direct rendering"
echo "  - No X11/Wayland dependencies"
echo "  - Smaller size with desktop libraries excluded"
echo "  - Perfect for headless/embedded environments"
echo ""
echo "To run applications with this Qt build:"
echo "  export QT_QPA_PLATFORM=linuxfb"
echo "  export QT_QPA_FB_DRM=/dev/dri/card0  # or appropriate device"
