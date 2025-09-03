#!/bin/sh
#
# Script to download and build Qt with minimal configuration for macOS
# Specifically designed for rpi-imager - builds only what we need
#
# Based on the Linux build-qt-embedded.sh script but adapted for macOS
# Uses POSIX shell for maximum compatibility
#

set -e

# Default values
QT_VERSION="6.9.1"    # 6.9.1 is the latest version as of 2025-08-04
PREFIX="/opt/Qt/${QT_VERSION}"     # Default installation prefix with capital 'Q'
CORES=$(sysctl -n hw.ncpu)        # Default to all available CPU cores
CLEAN_BUILD=1         # Clean the build directory by default
BUILD_TYPE="MinSizeRel"  # Default build type
SKIP_DEPENDENCIES=0   # Don't skip installing dependencies by default
MAC_OPTIMIZE=1        # macOS optimizations enabled by default
VERBOSE_BUILD=0       # By default, don't show verbose build output
UNPRIVILEGED=0        # By default, allow sudo usage
UNIVERSAL_BUILD=1     # By default, build universal binaries (x86_64 + arm64)


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
    echo "  --no-mac-optimize    Disable macOS specific optimizations"
    echo "  --no-universal       Disable universal build (host architecture only)"
    echo "  --verbose            Show verbose build output"
    echo "  --unprivileged       Run without sudo (skips dependency installation)"
    echo "  -h, --help           Show this help message"
    echo ""
    echo "This script builds a minimal Qt for rpi-imager on macOS:"
    echo "  - Excludes unnecessary modules and features"
    echo "  - Optimized for size and rpi-imager use case"
    echo "  - Uses Homebrew for dependency management"
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
        --no-mac-optimize)
            MAC_OPTIMIZE=0
            ;;
        --universal)
            UNIVERSAL_BUILD=1
            ;;
        --no-universal)
            UNIVERSAL_BUILD=0
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
case "$CORES" in
    ''|*[!0-9]*) 
        echo "Error: Cores must be a number"
        exit 1
        ;;
esac

# In unprivileged mode, suggest a user-writable prefix if using default
if [ "$UNPRIVILEGED" -eq 1 ] && [ "$PREFIX" = "/opt/Qt/${QT_VERSION}" ]; then
    PREFIX="$HOME/Qt/${QT_VERSION}"
    echo "Unprivileged mode: Using user-writable prefix: $PREFIX"
fi

# Major version (for directory structure)
QT_MAJOR_VERSION=$(echo "$QT_VERSION" | cut -d. -f1)

# Detect system architecture and set appropriate directory suffix
ARCH=$(uname -m)
if [ "$UNIVERSAL_BUILD" -eq 1 ]; then
    PREFIX="$PREFIX/macos"
    echo "Building universal Qt for both Intel (x86_64) and Apple Silicon (arm64)"
elif [ "$ARCH" = "x86_64" ] || [ "$ARCH" = "arm64" ]; then
    PREFIX="$PREFIX/macos"
fi

# Detect macOS version and set optimization flags
MACOS_VERSION=$(sw_vers -productVersion)
MACOS_MAJOR=$(echo "$MACOS_VERSION" | cut -d. -f1)
MAC_CFLAGS=""

if [ "$MAC_OPTIMIZE" -eq 1 ]; then
    echo "Detected macOS $MACOS_VERSION"
    
    if [ "$UNIVERSAL_BUILD" -eq 1 ]; then
        echo "  Optimizing for Universal Binary (Intel + Apple Silicon)"
        # For universal builds, we'll use general optimization flags and let Qt's
        # build system handle per-architecture optimization via CMAKE_OSX_ARCHITECTURES
        # and CFLAGS_x86_64/CFLAGS_arm64 variables
        if [ "$MACOS_MAJOR" -ge 11 ]; then
            MAC_CFLAGS="-mmacos-version-min=11.0"
        else
            MAC_CFLAGS="-mmacos-version-min=10.15"
        fi
        
        # Set architecture-specific optimization flags
        MAC_CFLAGS_X86_64="-march=x86-64-v2 -mtune=intel"
        MAC_CFLAGS_ARM64="-march=armv8.4-a+crypto -mtune=apple-a14"
        echo "  Intel optimizations: $MAC_CFLAGS_X86_64"
        echo "  Apple Silicon optimizations: $MAC_CFLAGS_ARM64"
    else
        # Set optimization flags based on architecture and macOS version
        if [ "$ARCH" = "arm64" ]; then
            echo "  Optimizing for Apple Silicon (arm64)"
            MAC_CFLAGS="-march=armv8.4-a+crypto -mtune=apple-a14"
        elif [ "$ARCH" = "x86_64" ]; then
            echo "  Optimizing for Intel x86_64"
            MAC_CFLAGS="-march=x86-64-v2 -mtune=intel"
        fi
        
        # Add macOS version-specific flags
        if [ "$MACOS_MAJOR" -ge 11 ]; then
            MAC_CFLAGS="$MAC_CFLAGS -mmacos-version-min=11.0"
        else
            MAC_CFLAGS="$MAC_CFLAGS -mmacos-version-min=10.15"
        fi
    fi
fi

echo "Qt build configuration:"
echo "  Version: $QT_VERSION"
echo "  Prefix: $PREFIX"
echo "  Cores: $CORES"
echo "  Build Type: $BUILD_TYPE"
echo "  Clean Build: $CLEAN_BUILD"
echo "  macOS Optimizations: $MAC_OPTIMIZE"
echo "  Universal Build: $UNIVERSAL_BUILD"
echo "  Verbose Build: $VERBOSE_BUILD"
echo "  Unprivileged Mode: $UNPRIVILEGED"
if [ -n "$MAC_CFLAGS" ]; then
    echo "  macOS CFLAGS: $MAC_CFLAGS"
fi

# Check for required tools
if ! command -v cmake >/dev/null 2>&1; then
    echo "Error: cmake is required but not installed"
    echo "Install with: brew install cmake"
    exit 1
fi

if ! command -v ninja >/dev/null 2>&1; then
    echo "Error: ninja is required but not installed"
    echo "Install with: brew install ninja"
    exit 1
fi

# Install dependencies if not skipped
if [ "$SKIP_DEPENDENCIES" -eq 0 ]; then
    echo "Installing build dependencies using Homebrew..."
    
    # Check if Homebrew is installed
    if ! command -v brew >/dev/null 2>&1; then
        echo "Error: Homebrew is required for dependency installation"
        echo "Install Homebrew from: https://brew.sh"
        exit 1
    fi
    
    # Special handling for universal builds
    if [ "$UNIVERSAL_BUILD" -eq 1 ]; then
        echo ""
        echo "⚠️  UNIVERSAL BUILD DEPENDENCY NOTICE:"
        echo "Universal builds require dependencies available for both x86_64 and arm64."
        echo ""
        
        # Check current architecture and Homebrew setup
        CURRENT_ARCH=$(uname -m)
        HOMEBREW_PREFIX=$(brew --prefix)
        
        if [ "$CURRENT_ARCH" = "arm64" ] && [ "$HOMEBREW_PREFIX" = "/opt/homebrew" ]; then
            echo "Detected Apple Silicon Mac with ARM64 Homebrew at /opt/homebrew"
            echo ""
            echo "For universal builds, you have these options:"
            echo "1. RECOMMENDED: Use system libraries (handled automatically)"
            echo "2. Install Intel Homebrew alongside ARM64 Homebrew"
            echo "3. Build dependencies from source (slower but reliable)"
            echo ""
            
            # Check if Intel Homebrew exists
            if [ -d "/usr/local/bin" ] && [ -x "/usr/local/bin/brew" ]; then
                echo "✅ Intel Homebrew detected at /usr/local - we'll use both"
                HAVE_INTEL_BREW=1
            else
                echo "ℹ️  Intel Homebrew not found - using system libraries where possible"
                HAVE_INTEL_BREW=0
            fi
        else
            echo "Standard dependency installation for current architecture"
            HAVE_INTEL_BREW=0
        fi
        echo ""
    fi
    
    # Update Homebrew and install dependencies
    brew update
    
    # Install basic tools (these work fine for universal builds)
    brew install \
        cmake \
        ninja \
        pkg-config \
        perl \
        python3
    
    # For universal builds, handle libraries more carefully
    if [ "$UNIVERSAL_BUILD" -eq 1 ]; then
        echo "Installing libraries with universal build considerations..."
        
        # These libraries often work better from system or with special handling
        echo "Note: For universal builds, Qt will prefer system libraries where available"
        echo "Installing Homebrew libraries for build tools and fallbacks..."
        
        # Install libraries that are needed for the build process itself
        brew install \
            icu4c \
            pcre2 \
            libpng \
            jpeg-turbo \
            freetype \
            fontconfig \
            openssl@3
            
        # If we have Intel Homebrew, we could install Intel versions too
        if [ "${HAVE_INTEL_BREW:-0}" -eq 1 ]; then
            echo "Also installing Intel versions of key libraries..."
            /usr/local/bin/brew install \
                icu4c \
                pcre2 \
                libpng \
                openssl@3 2>/dev/null || echo "Intel brew install failed (may already be installed)"
        fi
        
        echo ""
        echo "Universal build dependency setup complete."
        echo "Qt's configure script will detect the best available libraries."
    else
        # Standard single-architecture build
        brew install \
            icu4c \
            pcre2 \
            libpng \
            jpeg-turbo \
            freetype \
            fontconfig \
            openssl@3
    fi
    
    echo "macOS dependencies installed via Homebrew"
else
    if [ "$UNPRIVILEGED" -eq 1 ]; then
        echo "Skipping dependency installation (unprivileged mode)"
        echo "Please ensure all Qt build dependencies are already installed via Homebrew."
    else
        echo "Skipping dependency installation as requested"
    fi
fi

# Create directories
DOWNLOAD_DIR="$PWD/qt-src"
BUILD_DIR="$PWD/qt-build-macos"
# Directory containing this script (resolves relative invocation)
BASE_DIR="$(cd "$(dirname "$0")" >/dev/null 2>&1 && pwd)"
mkdir -p "$DOWNLOAD_DIR" "$BUILD_DIR"

# Download Qt source code
echo "Downloading Qt $QT_VERSION source code..."
cd "$DOWNLOAD_DIR"
if [ ! -d "qt-everywhere-src-$QT_VERSION" ]; then
    if [ ! -f "qt-everywhere-src-$QT_VERSION.tar.xz" ]; then
        echo "Downloading Qt source archive..."
        curl -L -o "qt-everywhere-src-$QT_VERSION.tar.xz" \
            "https://download.qt.io/official_releases/qt/${QT_VERSION%.*}/$QT_VERSION/single/qt-everywhere-src-$QT_VERSION.tar.xz"
    fi
    echo "Extracting Qt source..."
    tar xf "qt-everywhere-src-$QT_VERSION.tar.xz"
fi

# Clean build directory if requested
if [ "$CLEAN_BUILD" -eq 1 ]; then
    echo "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR"
fi

# macOS will use system ICU or Homebrew's ICU4C - no need for custom build

# Configure and build Qt
cd "$BASE_DIR"

# Set up environment variables for compilation
if [ "$MAC_OPTIMIZE" -eq 1 ] && [ -n "$MAC_CFLAGS" ]; then
    if [ "$UNIVERSAL_BUILD" -eq 1 ]; then
        # For universal builds, set architecture-specific flags
        export CFLAGS="$MAC_CFLAGS"
        export CXXFLAGS="$MAC_CFLAGS"
        export CFLAGS_x86_64="$MAC_CFLAGS $MAC_CFLAGS_X86_64"
        export CXXFLAGS_x86_64="$MAC_CFLAGS $MAC_CFLAGS_X86_64"
        export CFLAGS_arm64="$MAC_CFLAGS $MAC_CFLAGS_ARM64"
        export CXXFLAGS_arm64="$MAC_CFLAGS $MAC_CFLAGS_ARM64"
        echo "Using base CFLAGS: $CFLAGS"
        echo "Using x86_64 CFLAGS: $CFLAGS_x86_64"
        echo "Using arm64 CFLAGS: $CFLAGS_arm64"
    else
        export CFLAGS="$MAC_CFLAGS"
        export CXXFLAGS="$MAC_CFLAGS"
        echo "Using CFLAGS: $CFLAGS"
        echo "Using CXXFLAGS: $CXXFLAGS"
    fi
fi

# Set up Homebrew paths for dependencies
if [ "$UNIVERSAL_BUILD" -eq 1 ]; then
    # For universal builds, set up both Intel and ARM Homebrew paths
    echo "Setting up paths for universal build (both Intel and ARM libraries)"
    export PKG_CONFIG_PATH="/opt/homebrew/lib/pkgconfig:/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH"
    export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
    
    # Add architecture-specific library paths
    export LIBRARY_PATH="/opt/homebrew/lib:/usr/local/lib:${LIBRARY_PATH:-}"
    export CPATH="/opt/homebrew/include:/usr/local/include:${CPATH:-}"
else
    # Standard single-architecture setup
    export PKG_CONFIG_PATH="/opt/homebrew/lib/pkgconfig:/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH"
    export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
fi

# Base configuration options - minimal build similar to embedded Linux version
CONFIG_OPTS="-prefix $PREFIX -opensource -confirm-license -no-glib -make libs -skip qt3d -skip qtandroidextras -skip qtwinextras"

# Add build type specific options
if [ "$BUILD_TYPE" = "Debug" ]; then
    CONFIG_OPTS="$CONFIG_OPTS -debug"
elif [ "$BUILD_TYPE" = "MinSizeRel" ]; then
    CONFIG_OPTS="$CONFIG_OPTS -release -optimize-size"
else
    # Default to Release
    CONFIG_OPTS="$CONFIG_OPTS -release"
fi

# Platform-specific configuration for macOS
CONFIG_OPTS="$CONFIG_OPTS -opengl desktop"

# Exclude DBus support (not needed on macOS, only used on Linux)
CONFIG_OPTS="$CONFIG_OPTS -no-dbus"

# Add macOS-specific options
CONFIG_OPTS="$CONFIG_OPTS -no-framework"  # Build as dylibs instead of frameworks for easier deployment

echo "Excluding features and modules for minimal build..."

# Read and apply feature exclusions (from project root)
FEATURES_LIST="$BASE_DIR/features_exclude.macos.list"
if [ -f "$FEATURES_LIST" ]; then
    echo "Using features exclusion list: $(basename \""$FEATURES_LIST"\")"
    while IFS= read -r feature || [ -n "$feature" ]; do
        # Skip empty lines and comments
        case "$feature" in
            ''|'#'*|' '*'#'*|'	'*'#'*) 
                # Skip empty lines, comments, and lines starting with whitespace+comment
                ;;
            *)
                CONFIG_OPTS="$CONFIG_OPTS -no-feature-$feature"
                echo "Excluding feature: $feature"
                ;;
        esac
    done < "$FEATURES_LIST"
else
    echo "Warning: No features exclusion list found at $FEATURES_LIST"
fi

# Read and apply module exclusions (from project root)
MODULES_LIST="$BASE_DIR/modules_exclude.macos.list"
if [ -f "$MODULES_LIST" ]; then
    echo "Using modules exclusion list: $(basename \""$MODULES_LIST"\")"
    while IFS= read -r module || [ -n "$module" ]; do
        # Skip empty lines and comments
        case "$module" in
            ''|'#'*|' '*'#'*|'	'*'#'*) 
                # Skip empty lines, comments, and lines starting with whitespace+comment
                ;;
            *)
                CONFIG_OPTS="$CONFIG_OPTS -skip $module"
                echo "Excluding module: $module"
                ;;
        esac
    done < "$MODULES_LIST"
else
    echo "Warning: No modules exclusion list found at $MODULES_LIST"
fi

# Configure and build Qt
cd "$BUILD_DIR"

# Add CMake-specific options
CONFIG_OPTS="$CONFIG_OPTS -- -DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=$BUILD_EXAMPLES"

# Add universal build configuration if requested
if [ "$UNIVERSAL_BUILD" -eq 1 ]; then
    CONFIG_OPTS="$CONFIG_OPTS -DCMAKE_OSX_ARCHITECTURES=\"x86_64;arm64\""
    echo "Configuring for universal build: x86_64 + arm64"
fi

# Run the configure script with verbose output if requested
echo "Configuring Qt with options: $CONFIG_OPTS"
if [ "$VERBOSE_BUILD" -eq 1 ]; then
    eval "\"$DOWNLOAD_DIR/qt-everywhere-src-$QT_VERSION/configure\" $CONFIG_OPTS -verbose"
else
    eval "\"$DOWNLOAD_DIR/qt-everywhere-src-$QT_VERSION/configure\" $CONFIG_OPTS"
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
    echo "- Make sure you have all required Qt build dependencies installed via Homebrew"
    echo "- Common missing dependencies can cause build failures"
    echo "- If you encounter build errors, install dependencies manually with:"
    echo "  brew install cmake ninja pkg-config icu4c pcre2 libpng jpeg-turbo freetype fontconfig openssl@3"
    echo ""
fi

echo "To use it, you may want to set the following environment variables:"
echo "  export PATH=\"$PREFIX/bin:\$PATH\""
echo "  export DYLD_LIBRARY_PATH=\"$PREFIX/lib:\$DYLD_LIBRARY_PATH\""
echo "  export QT_PLUGIN_PATH=\"$PREFIX/plugins\""
echo "  export QML_IMPORT_PATH=\"$PREFIX/qml\""

# Create a qtenv.sh script for setting up the environment
QTENV_CONTENT="#!/bin/bash
# Source this file to set up the Qt environment
export PATH=\"$PREFIX/bin:\$PATH\"
export DYLD_LIBRARY_PATH=\"$PREFIX/lib:\$DYLD_LIBRARY_PATH\"
export QT_PLUGIN_PATH=\"$PREFIX/plugins\"
export QML_IMPORT_PATH=\"$PREFIX/qml\"
export PKG_CONFIG_PATH=\"$PREFIX/lib/pkgconfig:\$PKG_CONFIG_PATH\"
export CMAKE_PREFIX_PATH=\"$PREFIX:\$CMAKE_PREFIX_PATH\"
export QT_BUILD_DIR=\"$BUILD_DIR\"
echo \"Qt $QT_VERSION environment initialized for macOS\""

if [ "$UNPRIVILEGED" -eq 1 ]; then
    mkdir -p "$PREFIX/bin"
    echo "$QTENV_CONTENT" > "$PREFIX/bin/qtenv.sh"
    chmod +x "$PREFIX/bin/qtenv.sh"
else
    sudo mkdir -p "$PREFIX/bin"
    echo "$QTENV_CONTENT" | sudo tee "$PREFIX/bin/qtenv.sh" > /dev/null
    sudo chmod +x "$PREFIX/bin/qtenv.sh"
fi
echo "Created environment setup script at $PREFIX/bin/qtenv.sh"
echo "Source it with: source $PREFIX/bin/qtenv.sh"

# Create a CMake toolchain file for using this Qt with CMake
TOOLCHAIN_CONTENT="# CMake toolchain file for Qt $QT_VERSION on macOS
set(CMAKE_PREFIX_PATH \"${PREFIX}\" \${CMAKE_PREFIX_PATH})
set(QT_QMAKE_EXECUTABLE \"${PREFIX}/bin/qmake\")
set(Qt${QT_MAJOR_VERSION}_DIR \"${PREFIX}/lib/cmake/Qt${QT_MAJOR_VERSION}\")

# macOS specific settings
set(CMAKE_OSX_DEPLOYMENT_TARGET \"11.0\")"

# Add architecture-specific settings
if [ "$UNIVERSAL_BUILD" -eq 1 ]; then
    TOOLCHAIN_CONTENT="$TOOLCHAIN_CONTENT

# Universal build - support both architectures
set(CMAKE_OSX_ARCHITECTURES \"x86_64;arm64\")"
else
    TOOLCHAIN_CONTENT="$TOOLCHAIN_CONTENT

# Architecture-specific build
if(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL \"arm64\")
    set(CMAKE_OSX_ARCHITECTURES \"arm64\")
else()
    set(CMAKE_OSX_ARCHITECTURES \"x86_64\")
endif()"
fi

# Write the toolchain file with appropriate permissions
if [ "$UNPRIVILEGED" -eq 1 ]; then
    echo "$TOOLCHAIN_CONTENT" > "$PREFIX/qt$QT_MAJOR_VERSION-toolchain.cmake"
else
    echo "$TOOLCHAIN_CONTENT" | sudo tee "$PREFIX/qt$QT_MAJOR_VERSION-toolchain.cmake" > /dev/null
fi

echo "Created CMake toolchain file at $PREFIX/qt$QT_MAJOR_VERSION-toolchain.cmake"
echo "Use with: cmake -DCMAKE_TOOLCHAIN_FILE=$PREFIX/qt$QT_MAJOR_VERSION-toolchain.cmake ..."