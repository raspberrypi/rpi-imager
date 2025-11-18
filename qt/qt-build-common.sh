#!/bin/bash
#
# Common configuration and functions for Qt build scripts
# This file should be sourced by all Qt build scripts to ensure consistency
#

# Prevent multiple sourcing
if [ -n "$QT_BUILD_COMMON_LOADED" ]; then
    return 0
fi
QT_BUILD_COMMON_LOADED=1

# =============================================================================
# COMMON CONFIGURATION VARIABLES
# =============================================================================

# Qt Version Configuration
QT_VERSION_DEFAULT="6.9.3"    # Default version for all platforms

# Build Configuration Defaults
PREFIX_DEFAULT="/opt/Qt"       # Base installation prefix (version will be appended)
BUILD_TYPE_DEFAULT="MinSizeRel"  # Default build type (Release, Debug, MinSizeRel)
CLEAN_BUILD_DEFAULT=1          # Clean the build directory by default
SKIP_DEPENDENCIES_DEFAULT=0    # Don't skip installing dependencies by default
VERBOSE_BUILD_DEFAULT=0        # By default, don't show verbose build output
UNPRIVILEGED_DEFAULT=0         # By default, allow sudo usage

# Platform Detection
PLATFORM=$(uname -s)
ARCH=$(uname -m)

# Set platform-specific defaults
case "$PLATFORM" in
    "Darwin")
        CORES_DEFAULT=$(sysctl -n hw.ncpu)
        ;;
    "Linux")
        CORES_DEFAULT=$(nproc)
        ;;
    *)
        CORES_DEFAULT=4  # Safe fallback
        ;;
esac

# Use the same Qt version for all platforms
QT_VERSION_PLATFORM_DEFAULT="$QT_VERSION_DEFAULT"

# =============================================================================
# COMMON FUNCTIONS
# =============================================================================

# Function to initialize common variables with defaults
# Usage: init_common_variables [platform_specific_version]
init_common_variables() {
    local platform_version="${1:-$QT_VERSION_PLATFORM_DEFAULT}"
    
    # Set defaults if not already set by the calling script
    QT_VERSION="${QT_VERSION:-$platform_version}"
    PREFIX="${PREFIX:-$PREFIX_DEFAULT/${QT_VERSION}}"
    CORES="${CORES:-$CORES_DEFAULT}"
    CLEAN_BUILD="${CLEAN_BUILD:-$CLEAN_BUILD_DEFAULT}"
    BUILD_TYPE="${BUILD_TYPE:-$BUILD_TYPE_DEFAULT}"
    SKIP_DEPENDENCIES="${SKIP_DEPENDENCIES:-$SKIP_DEPENDENCIES_DEFAULT}"
    VERBOSE_BUILD="${VERBOSE_BUILD:-$VERBOSE_BUILD_DEFAULT}"
    UNPRIVILEGED="${UNPRIVILEGED:-$UNPRIVILEGED_DEFAULT}"
    
    # Derived variables
    QT_MAJOR_VERSION=$(echo "$QT_VERSION" | cut -d. -f1)
    
    # Directory containing the calling script (resolves relative invocation)
    BASE_DIR="$(cd "$(dirname "${BASH_SOURCE[1]}")" >/dev/null 2>&1 && pwd)"
    
    # Common build directories
    DOWNLOAD_DIR="$PWD/qt-src"
    
    # Export for use in subprocesses
    export QT_VERSION QT_MAJOR_VERSION PREFIX CORES BUILD_TYPE BASE_DIR
}

# Function to parse common command line arguments
# Usage: parse_common_args "$@"
# Sets: COMMON_REMAINING_ARGS array with arguments that weren't processed
parse_common_args() {
    COMMON_REMAINING_ARGS=()
    
    while [[ $# -gt 0 ]]; do
        case $1 in
            --version=*)
                QT_VERSION="${1#*=}"
                ;;
            --prefix=*)
                PREFIX="${1#*=}"
                ;;
            --cores=*)
                CORES="${1#*=}"
                ;;
            --no-clean)
                CLEAN_BUILD=0
                ;;
            --debug)
                BUILD_TYPE="Debug"
                ;;
            --release)
                BUILD_TYPE="Release"
                ;;
            --minsize)
                BUILD_TYPE="MinSizeRel"
                ;;
            --skip-dependencies)
                SKIP_DEPENDENCIES=1
                ;;
            --verbose)
                VERBOSE_BUILD=1
                ;;
            --unprivileged)
                UNPRIVILEGED=1
                SKIP_DEPENDENCIES=1  # Automatically skip dependencies in unprivileged mode
                ;;
            -h|--help)
                # Let the calling script handle help
                COMMON_REMAINING_ARGS+=("$1")
                ;;
            *)
                # Pass through unknown arguments
                COMMON_REMAINING_ARGS+=("$1")
                ;;
        esac
        shift
    done
}

# Function to validate common inputs
validate_common_inputs() {
    # Validate cores is a number
    if ! [[ $CORES =~ ^[0-9]+$ ]]; then
        echo "Error: Cores must be a number, got: $CORES"
        return 1
    fi
    
    # Validate Qt version format
    if ! [[ $QT_VERSION =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
        echo "Error: Qt version must be in format X.Y.Z, got: $QT_VERSION"
        return 1
    fi
    
    # In unprivileged mode, suggest a user-writable prefix if using default
    if [ "$UNPRIVILEGED" -eq 1 ] && [[ "$PREFIX" == "$PREFIX_DEFAULT"* ]]; then
        PREFIX="$HOME/Qt/${QT_VERSION}"
        echo "Unprivileged mode: Using user-writable prefix: $PREFIX"
    fi
    
    return 0
}

# Function to print common configuration
print_common_config() {
    local script_name="${1:-Qt build}"
    
    echo "$script_name configuration:"
    echo "  Version: $QT_VERSION"
    echo "  Prefix: $PREFIX"
    echo "  Cores: $CORES"
    echo "  Build Type: $BUILD_TYPE"
    echo "  Clean Build: $CLEAN_BUILD"
    echo "  Verbose Build: $VERBOSE_BUILD"
    echo "  Unprivileged Mode: $UNPRIVILEGED"
    echo "  Platform: $PLATFORM ($ARCH)"
}

# Function to create common build directories
create_build_directories() {
    local build_dir_suffix="${1:-}"
    
    if [ -n "$build_dir_suffix" ]; then
        BUILD_DIR="$PWD/qt-build-$build_dir_suffix"
    else
        BUILD_DIR="$PWD/qt-build"
    fi
    
    mkdir -p "$DOWNLOAD_DIR" "$BUILD_DIR"
    
    # Export for use in calling script
    export BUILD_DIR DOWNLOAD_DIR
}

# Function to download Qt source code
download_qt_source() {
    echo "Downloading Qt $QT_VERSION source code..."
    cd "$DOWNLOAD_DIR"
    
    if [ ! -d "qt-everywhere-src-$QT_VERSION" ]; then
        if [ ! -f "qt-everywhere-src-$QT_VERSION.tar.xz" ]; then
            echo "Downloading Qt source archive..."
            local download_url="https://download.qt.io/official_releases/qt/${QT_VERSION%.*}/$QT_VERSION/single/qt-everywhere-src-$QT_VERSION.tar.xz"
            
            if command -v wget &> /dev/null; then
                wget "$download_url"
            elif command -v curl &> /dev/null; then
                curl -L -o "qt-everywhere-src-$QT_VERSION.tar.xz" "$download_url"
            else
                echo "Error: Neither wget nor curl found. Please install one of them."
                return 1
            fi
        fi
        
        echo "Extracting Qt source..."
        tar xf "qt-everywhere-src-$QT_VERSION.tar.xz"
    else
        echo "Qt source already extracted"
    fi
}

# Function to clean build directory if requested
clean_build_directory() {
    if [ "$CLEAN_BUILD" -eq 1 ]; then
        echo "Cleaning build directory..."
        rm -rf "$BUILD_DIR"
        mkdir -p "$BUILD_DIR"
    fi
}

# Function to apply feature and module exclusions
# Usage: apply_exclusions CONFIG_OPTS_ARRAY_NAME [features_list_file] [modules_list_file]
apply_exclusions() {
    local -n config_opts_ref=$1
    local features_list="${2:-$BASE_DIR/features_exclude.list}"
    local modules_list="${3:-$BASE_DIR/modules_exclude.list}"
    
    # Apply feature exclusions
    if [ -f "$features_list" ]; then
        echo "Using features exclusion list: $(basename "$features_list")"
        while IFS= read -r feature || [ -n "$feature" ]; do
            case "$feature" in
                ''|'#'*|' '*'#'*|$'\t'*'#'*)
                    # Skip empty lines and comments
                    ;;
                *)
                    config_opts_ref+=( -no-feature-"$feature" )
                    echo "Excluding feature: $feature"
                    ;;
            esac
        done < "$features_list"
    else
        echo "Warning: No features exclusion list found at $features_list"
    fi
    
    # Apply module exclusions
    if [ -f "$modules_list" ]; then
        echo "Using modules exclusion list: $(basename "$modules_list")"
        while IFS= read -r module || [ -n "$module" ]; do
            case "$module" in
                ''|'#'*|' '*'#'*|$'\t'*'#'*)
                    # Skip empty lines and comments
                    ;;
                *)
                    config_opts_ref+=( -skip "$module" )
                    echo "Excluding module: $module"
                    ;;
            esac
        done < "$modules_list"
    else
        echo "Warning: No modules exclusion list found at $modules_list"
    fi
}

# Function to create environment setup script
# Usage: create_qt_env_script [script_suffix] [additional_vars_function]
create_qt_env_script() {
    local script_suffix="${1:-}"
    local additional_vars_function="${2:-}"
    local env_script_name="qtenv"
    
    if [ -n "$script_suffix" ]; then
        env_script_name="qtenv-$script_suffix"
    fi
    
    local env_script="$PREFIX/bin/${env_script_name}.sh"
    
    cat > "$env_script" << EOF
#!/bin/bash
# Source this file to set up the Qt environment
export PATH="$PREFIX/bin:\$PATH"
export LD_LIBRARY_PATH="$PREFIX/lib:\$LD_LIBRARY_PATH"
export QT_PLUGIN_PATH="$PREFIX/plugins"
export QML_IMPORT_PATH="$PREFIX/qml"
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:\$PKG_CONFIG_PATH"
export CMAKE_PREFIX_PATH="$PREFIX:\$CMAKE_PREFIX_PATH"
export Qt${QT_MAJOR_VERSION}_ROOT="$PREFIX"
EOF

    # Add additional environment variables if function provided
    if [ -n "$additional_vars_function" ] && declare -f "$additional_vars_function" > /dev/null; then
        "$additional_vars_function" >> "$env_script"
    fi

    # Add final echo
    cat >> "$env_script" << EOF
echo "Qt $QT_VERSION environment initialized"
EOF

    chmod +x "$env_script"
    echo "Created environment setup script at $env_script"
    echo "Source it with: source $env_script"
}

# Function to create CMake toolchain file
# Usage: create_cmake_toolchain [toolchain_suffix] [additional_cmake_function]
create_cmake_toolchain() {
    local toolchain_suffix="${1:-}"
    local additional_cmake_function="${2:-}"
    local toolchain_name="qt$QT_MAJOR_VERSION"
    
    if [ -n "$toolchain_suffix" ]; then
        toolchain_name="qt$QT_MAJOR_VERSION-$toolchain_suffix"
    fi
    
    local toolchain_file="$PREFIX/${toolchain_name}-toolchain.cmake"
    
    cat > "$toolchain_file" << EOF
# CMake toolchain file for Qt $QT_VERSION
set(CMAKE_PREFIX_PATH "${PREFIX}" \${CMAKE_PREFIX_PATH})
set(QT_QMAKE_EXECUTABLE "${PREFIX}/bin/qmake")
set(Qt${QT_MAJOR_VERSION}_DIR "${PREFIX}/lib/cmake/Qt${QT_MAJOR_VERSION}")
set(Qt${QT_MAJOR_VERSION}_ROOT "${PREFIX}")
EOF

    # Add additional CMake settings if function provided
    if [ -n "$additional_cmake_function" ] && declare -f "$additional_cmake_function" > /dev/null; then
        "$additional_cmake_function" >> "$toolchain_file"
    fi

    echo "Created CMake toolchain file at $toolchain_file"
    echo "Use with: cmake -DCMAKE_TOOLCHAIN_FILE=$toolchain_file ..."
}

# Function to run Qt configure with common error handling
# Usage: run_qt_configure CONFIG_OPTS_ARRAY_NAME
run_qt_configure() {
    local -n config_opts_ref=$1
    
    echo "Configuring Qt with options: ${config_opts_ref[*]}"
    
    if [ "$VERBOSE_BUILD" -eq 1 ]; then
        "$DOWNLOAD_DIR/qt-everywhere-src-$QT_VERSION/configure" "${config_opts_ref[@]}" -verbose
    else
        "$DOWNLOAD_DIR/qt-everywhere-src-$QT_VERSION/configure" "${config_opts_ref[@]}"
    fi
    
    if [ $? -ne 0 ]; then
        echo "Configure failed. Try with --verbose for more details."
        echo "Common issues:"
        echo "  - Missing build dependencies"
        echo "  - Incorrect configuration options"
        echo "  - Platform-specific requirements not met"
        return 1
    fi
}

# Function to build Qt with common error handling
# Usage: build_qt
build_qt() {
    echo "Building Qt (this may take a while)..."
    
    # Map BUILD_TYPE to CMAKE_BUILD_TYPE
    local cmake_build_type=""
    case "${BUILD_TYPE}" in
        "Release")
            cmake_build_type="Release"
            ;;
        "Debug")
            cmake_build_type="Debug"
            ;;
        "MinSizeRel")
            cmake_build_type="MinSizeRel"
            ;;
        *)
            cmake_build_type="Release"
            ;;
    esac
    
    if [ "$VERBOSE_BUILD" -eq 1 ]; then
        cmake --build . --parallel "$CORES" --config "$cmake_build_type" --verbose
    else
        cmake --build . --parallel "$CORES" --config "$cmake_build_type"
    fi
    
    if [ $? -ne 0 ]; then
        echo "Build failed. Try with --verbose for more details."
        echo "Common solutions:"
        echo "  - Reduce parallel jobs: --cores=2"
        echo "  - Check available disk space"
        echo "  - Verify all dependencies are installed"
        return 1
    fi
}

# Function to install Qt with common error handling
# Usage: install_qt
install_qt() {
    echo "Installing Qt to $PREFIX..."
    
    if [ "$UNPRIVILEGED" -eq 1 ]; then
        # Create prefix directory if it doesn't exist (without sudo)
        mkdir -p "$(dirname "$PREFIX")"
        cmake --install .
    else
        sudo cmake --install .
    fi
    
    if [ $? -ne 0 ]; then
        echo "Installation failed."
        return 1
    fi
    
    echo "Qt $QT_VERSION has been built and installed to $PREFIX"
}

# Function to print final usage instructions
print_usage_instructions() {
    local build_type="${1:-Qt}"
    
    echo ""
    echo "$build_type build completed successfully!"
    echo ""
    echo "To use this Qt installation:"
    echo "  export Qt${QT_MAJOR_VERSION}_ROOT=\"$PREFIX\""
    echo "  export PATH=\"$PREFIX/bin:\$PATH\""
    echo ""
    
    if [ "$UNPRIVILEGED" -eq 1 ]; then
        echo "UNPRIVILEGED MODE NOTES:"
        echo "- Dependencies were not automatically installed"
        echo "- Make sure you have all required build dependencies installed"
        echo "- If you encounter issues, install dependencies manually"
        echo ""
    fi
}

# Function to generate common usage help text
# Usage: print_common_usage_options
print_common_usage_options() {
    cat << EOF
Common options:
  --version=VERSION    Qt version to build (default: $QT_VERSION_PLATFORM_DEFAULT)
  --prefix=PREFIX      Installation prefix (default: $PREFIX_DEFAULT/{VERSION})
  --cores=CORES        Number of CPU cores to use (default: $CORES_DEFAULT)
  --no-clean           Don't clean the build directory
  --debug              Build with debug information
  --release            Build with release optimization
  --minsize            Build with size optimization (default)
  --skip-dependencies  Skip installing build dependencies
  --verbose            Show verbose build output
  --unprivileged       Run without sudo (skips dependency installation)
  -h, --help           Show this help message
EOF
}

# =============================================================================
# PLATFORM-SPECIFIC HELPER FUNCTIONS
# =============================================================================

# Function to detect Raspberry Pi model and set optimization flags
detect_rpi_optimizations() {
    local rpi_model=""
    local rpi_cflags=""
    
    if [ -f /proc/cpuinfo ] && grep -q "Raspberry Pi" /proc/cpuinfo; then
        echo "Detected Raspberry Pi system"
        rpi_model=$(grep "Model" /proc/cpuinfo | sed 's/.*: //')
        echo "  Model: $rpi_model"
        
        # Set optimization flags based on model
        if echo "$rpi_model" | grep -q "Pi 4"; then
            echo "  Optimizing for Raspberry Pi 4"
            rpi_cflags="-march=armv8-a+crc -mtune=cortex-a72 -mfpu=neon-fp-armv8"
        elif echo "$rpi_model" | grep -q "Pi 3"; then
            echo "  Optimizing for Raspberry Pi 3"
            rpi_cflags="-march=armv8-a+crc -mtune=cortex-a53 -mfpu=neon-fp-armv8"
        elif echo "$rpi_model" | grep -q "Pi 2"; then
            echo "  Optimizing for Raspberry Pi 2"
            rpi_cflags="-march=armv7-a -mtune=cortex-a7 -mfpu=neon-vfpv4"
        elif echo "$rpi_model" | grep -q "Pi 5"; then
            echo "  Optimizing for Raspberry Pi 5"
            rpi_cflags="-march=armv8.2-a+crypto -mtune=cortex-a76"
        fi
    fi
    
    # Export for use by calling script
    export RPI_MODEL="$rpi_model"
    export RPI_CFLAGS="$rpi_cflags"
}

# Function to set architecture-specific prefix suffix
set_arch_prefix_suffix() {
    local suffix="${1:-}"
    
    case "$ARCH" in
        "x86_64")
            PREFIX="$PREFIX/gcc_64${suffix:+_$suffix}"
            ;;
        "aarch64"|"arm64")
            PREFIX="$PREFIX/gcc_arm64${suffix:+_$suffix}"
            ;;
        arm*)
            PREFIX="$PREFIX/gcc_arm32${suffix:+_$suffix}"
            ;;
        *)
            PREFIX="$PREFIX/gcc_${ARCH}${suffix:+_$suffix}"
            ;;
    esac
    
    export PREFIX
}

# =============================================================================
# DEPENDENCY INSTALLATION HELPERS
# =============================================================================

# Function to install basic build dependencies on Linux
install_linux_basic_deps() {
    if [ "$SKIP_DEPENDENCIES" -eq 0 ]; then
        echo "Installing basic build dependencies..."
        sudo apt-get update
        sudo apt-get install -y build-essential perl python3 git cmake ninja-build pkg-config
        sudo apt-get install -y bison flex gperf
    fi
}

# Function to install macOS dependencies via Homebrew
install_macos_deps() {
    if [ "$SKIP_DEPENDENCIES" -eq 0 ]; then
        echo "Installing macOS build dependencies..."
        if ! command -v brew &> /dev/null; then
            echo "Error: Homebrew is required but not installed"
            echo "Install from: https://brew.sh"
            return 1
        fi
        
        brew install cmake ninja pkg-config perl python3 icu4c pcre2 libpng \
                     jpeg-turbo freetype fontconfig openssl@3
    fi
}

# Function to add cross-compilation environment variables to qtenv script
# Usage: add_cross_compilation_env_vars
add_cross_compilation_env_vars() {
    cat << EOF

# Cross-compilation environment
export CC="$CC"
export CXX="$CXX"
export AR="$AR"
export STRIP="$STRIP"
export OBJCOPY="$OBJCOPY"
export OBJDUMP="$OBJDUMP"
export RANLIB="$RANLIB"
export PKG_CONFIG_LIBDIR="$PKG_CONFIG_LIBDIR"
export PKG_CONFIG_SYSROOT_DIR="$PKG_CONFIG_SYSROOT_DIR"
export CMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE"
echo "Sysroot: $SYSROOT"
echo "Toolchain: $TOOLCHAIN_PREFIX"
EOF
}

# Function to add cross-compilation CMake settings
# Usage: add_cross_compilation_cmake
add_cross_compilation_cmake() {
    cat << EOF

# Include the base cross-compilation toolchain
include("$TOOLCHAIN_FILE")

# Additional settings for application cross-compilation
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
EOF
}

# =============================================================================
# ICU VERSION DETECTION
# =============================================================================

# Function to get the recommended ICU version for a given Qt version
# Usage: get_icu_version_for_qt QT_VERSION
get_icu_version_for_qt() {
    local qt_version="${1:-$QT_VERSION}"
    local qt_major_minor="${qt_version%.*}"  # Extract X.Y from X.Y.Z
    
    case "$qt_version" in
        6.9.3)
            echo "76.1"
            ;;
        *)
            echo "Unknown Qt version $qt_version"
            echo "Please add the ICU version to the static mapping in get_icu_version_for_qt()"
            return 1
            ;;
    esac
}

# Function to convert ICU version to git tag format
# Usage: icu_version_to_tag "76.1" -> "release-76-1"
icu_version_to_tag() {
    local icu_version="$1"
    echo "release-${icu_version//./-}"
}

# Function to convert ICU version to data package format
# Usage: icu_version_to_data_package "76.1" -> "icu4c-76_1-data.zip"
icu_version_to_data_package() {
    local icu_version="$1"
    echo "icu4c-${icu_version//./_}-data.zip"
}

echo "Qt build common configuration loaded (version: 1.1)"
