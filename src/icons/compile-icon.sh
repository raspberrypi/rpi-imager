#!/bin/bash
#
# compile-icon.sh - Compile the Icon Composer .icon file into Assets.car
#
# This script uses a minimal Xcode project to compile app_icon_macos.icon
# into Assets.car with proper support for:
#   - Dark mode (NSAppearanceNameDarkAqua)
#   - Tinted appearance (ISAppearanceTintable)  
#   - Liquid Glass rendering (macOS Tahoe+)
#
# The compiled outputs are:
#   - AppIcon-compiled.car  (Assets.car with all icon variants)
#   - AppIcon-compiled.icns (Legacy .icns for pre-Tahoe macOS)
#
# Usage: ./compile-icon.sh
#
# Requirements:
#   - Xcode with command line tools installed
#   - app_icon_macos.icon in the same directory
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ICON_SOURCE="${SCRIPT_DIR}/app_icon_macos.icon"
XCODE_PROJECT_DIR="${SCRIPT_DIR}/xcode-icon-compiler"
BUILD_DIR="${XCODE_PROJECT_DIR}/build"

# Verify source icon exists
if [ ! -d "${ICON_SOURCE}" ]; then
    echo "Error: Icon source not found: ${ICON_SOURCE}"
    exit 1
fi

# Create Xcode project directory if needed
mkdir -p "${XCODE_PROJECT_DIR}/IconCompiler.xcodeproj"

# Copy icon source to project directory (actool doesn't follow symlinks reliably)
echo "Copying icon source..."
rm -rf "${XCODE_PROJECT_DIR}/AppIcon.icon"
cp -R "${ICON_SOURCE}" "${XCODE_PROJECT_DIR}/AppIcon.icon"

# Generate the Xcode project file
echo "Generating Xcode project..."
cat > "${XCODE_PROJECT_DIR}/IconCompiler.xcodeproj/project.pbxproj" << 'PBXPROJ'
// !$*UTF8*$!
{
	archiveVersion = 1;
	classes = {
	};
	objectVersion = 56;
	objects = {

/* Begin PBXBuildFile section */
		ICON_BUILD /* AppIcon.icon in Resources */ = {isa = PBXBuildFile; fileRef = ICON_REF /* AppIcon.icon */; };
/* End PBXBuildFile section */

/* Begin PBXFileReference section */
		ICON_REF /* AppIcon.icon */ = {isa = PBXFileReference; lastKnownFileType = folder.icon; path = AppIcon.icon; sourceTree = "<group>"; };
		PRODUCT_REF /* IconCompiler.app */ = {isa = PBXFileReference; explicitFileType = wrapper.application; includeInIndex = 0; path = IconCompiler.app; sourceTree = BUILT_PRODUCTS_DIR; };
/* End PBXFileReference section */

/* Begin PBXGroup section */
		ROOT_GROUP = {
			isa = PBXGroup;
			children = (
				ICON_REF /* AppIcon.icon */,
				PRODUCTS_GROUP /* Products */,
			);
			sourceTree = "<group>";
		};
		PRODUCTS_GROUP /* Products */ = {
			isa = PBXGroup;
			children = (
				PRODUCT_REF /* IconCompiler.app */,
			);
			name = Products;
			sourceTree = "<group>";
		};
/* End PBXGroup section */

/* Begin PBXNativeTarget section */
		TARGET_REF /* IconCompiler */ = {
			isa = PBXNativeTarget;
			buildConfigurationList = CONFIG_LIST /* Build configuration list for PBXNativeTarget "IconCompiler" */;
			buildPhases = (
				RESOURCES_PHASE /* Resources */,
			);
			buildRules = (
			);
			dependencies = (
			);
			name = IconCompiler;
			productName = IconCompiler;
			productReference = PRODUCT_REF /* IconCompiler.app */;
			productType = "com.apple.product-type.application";
		};
/* End PBXNativeTarget section */

/* Begin PBXProject section */
		PROJECT_REF /* Project object */ = {
			isa = PBXProject;
			attributes = {
				BuildIndependentTargetsInParallel = 1;
				LastUpgradeCheck = 1600;
			};
			buildConfigurationList = PROJECT_CONFIG_LIST /* Build configuration list for PBXProject "IconCompiler" */;
			compatibilityVersion = "Xcode 14.0";
			developmentRegion = en;
			hasScannedForEncodings = 0;
			knownRegions = (
				en,
				Base,
			);
			mainGroup = ROOT_GROUP;
			productRefGroup = PRODUCTS_GROUP /* Products */;
			projectDirPath = "";
			projectRoot = "";
			targets = (
				TARGET_REF /* IconCompiler */,
			);
		};
/* End PBXProject section */

/* Begin PBXResourcesBuildPhase section */
		RESOURCES_PHASE /* Resources */ = {
			isa = PBXResourcesBuildPhase;
			buildActionMask = 2147483647;
			files = (
				ICON_BUILD /* AppIcon.icon in Resources */,
			);
			runOnlyForDeploymentPostprocessing = 0;
		};
/* End PBXResourcesBuildPhase section */

/* Begin XCBuildConfiguration section */
		DEBUG_CONFIG /* Debug */ = {
			isa = XCBuildConfiguration;
			buildSettings = {
				ALWAYS_SEARCH_USER_PATHS = NO;
				ASSETCATALOG_COMPILER_GENERATE_SWIFT_ASSET_SYMBOL_EXTENSIONS = YES;
				CLANG_ANALYZER_NONNULL = YES;
				CLANG_CXX_LANGUAGE_STANDARD = "gnu++20";
				CLANG_ENABLE_MODULES = YES;
				CLANG_ENABLE_OBJC_ARC = YES;
				COPY_PHASE_STRIP = NO;
				DEBUG_INFORMATION_FORMAT = dwarf;
				ENABLE_STRICT_OBJC_MSGSEND = YES;
				ENABLE_TESTABILITY = YES;
				GCC_DYNAMIC_NO_PIC = NO;
				GCC_NO_COMMON_BLOCKS = YES;
				GCC_OPTIMIZATION_LEVEL = 0;
				GCC_WARN_64_TO_32_BIT_CONVERSION = YES;
				GCC_WARN_ABOUT_RETURN_TYPE = YES_ERROR;
				GCC_WARN_UNDECLARED_SELECTOR = YES;
				GCC_WARN_UNINITIALIZED_AUTOS = YES_AGGRESSIVE;
				GCC_WARN_UNUSED_FUNCTION = YES;
				GCC_WARN_UNUSED_VARIABLE = YES;
				MACOSX_DEPLOYMENT_TARGET = 12.0;
				MTL_ENABLE_DEBUG_INFO = INCLUDE_SOURCE;
				MTL_FAST_MATH = YES;
				ONLY_ACTIVE_ARCH = YES;
				SDKROOT = macosx;
			};
			name = Debug;
		};
		RELEASE_CONFIG /* Release */ = {
			isa = XCBuildConfiguration;
			buildSettings = {
				ALWAYS_SEARCH_USER_PATHS = NO;
				ASSETCATALOG_COMPILER_GENERATE_SWIFT_ASSET_SYMBOL_EXTENSIONS = YES;
				CLANG_ANALYZER_NONNULL = YES;
				CLANG_CXX_LANGUAGE_STANDARD = "gnu++20";
				CLANG_ENABLE_MODULES = YES;
				CLANG_ENABLE_OBJC_ARC = YES;
				COPY_PHASE_STRIP = NO;
				DEBUG_INFORMATION_FORMAT = "dwarf-with-dsym";
				ENABLE_NS_ASSERTIONS = NO;
				ENABLE_STRICT_OBJC_MSGSEND = YES;
				GCC_NO_COMMON_BLOCKS = YES;
				GCC_WARN_64_TO_32_BIT_CONVERSION = YES;
				GCC_WARN_ABOUT_RETURN_TYPE = YES_ERROR;
				GCC_WARN_UNDECLARED_SELECTOR = YES;
				GCC_WARN_UNINITIALIZED_AUTOS = YES_AGGRESSIVE;
				GCC_WARN_UNUSED_FUNCTION = YES;
				GCC_WARN_UNUSED_VARIABLE = YES;
				MACOSX_DEPLOYMENT_TARGET = 12.0;
				MTL_ENABLE_DEBUG_INFO = NO;
				MTL_FAST_MATH = YES;
				SDKROOT = macosx;
			};
			name = Release;
		};
		TARGET_DEBUG_CONFIG /* Debug */ = {
			isa = XCBuildConfiguration;
			buildSettings = {
				ASSETCATALOG_COMPILER_APPICON_NAME = AppIcon;
				CODE_SIGN_STYLE = Automatic;
				COMBINE_HIDPI_IMAGES = YES;
				CURRENT_PROJECT_VERSION = 1;
				GENERATE_INFOPLIST_FILE = YES;
				INFOPLIST_KEY_NSMainNibFile = "";
				INFOPLIST_KEY_NSPrincipalClass = NSApplication;
				MARKETING_VERSION = 1.0;
				PRODUCT_BUNDLE_IDENTIFIER = com.example.IconCompiler;
				PRODUCT_NAME = "$(TARGET_NAME)";
			};
			name = Debug;
		};
		TARGET_RELEASE_CONFIG /* Release */ = {
			isa = XCBuildConfiguration;
			buildSettings = {
				ASSETCATALOG_COMPILER_APPICON_NAME = AppIcon;
				CODE_SIGN_STYLE = Automatic;
				COMBINE_HIDPI_IMAGES = YES;
				CURRENT_PROJECT_VERSION = 1;
				GENERATE_INFOPLIST_FILE = YES;
				INFOPLIST_KEY_NSMainNibFile = "";
				INFOPLIST_KEY_NSPrincipalClass = NSApplication;
				MARKETING_VERSION = 1.0;
				PRODUCT_BUNDLE_IDENTIFIER = com.example.IconCompiler;
				PRODUCT_NAME = "$(TARGET_NAME)";
			};
			name = Release;
		};
/* End XCBuildConfiguration section */

/* Begin XCConfigurationList section */
		PROJECT_CONFIG_LIST /* Build configuration list for PBXProject "IconCompiler" */ = {
			isa = XCConfigurationList;
			buildConfigurations = (
				DEBUG_CONFIG /* Debug */,
				RELEASE_CONFIG /* Release */,
			);
			defaultConfigurationIsVisible = 0;
			defaultConfigurationName = Release;
		};
		CONFIG_LIST /* Build configuration list for PBXNativeTarget "IconCompiler" */ = {
			isa = XCConfigurationList;
			buildConfigurations = (
				TARGET_DEBUG_CONFIG /* Debug */,
				TARGET_RELEASE_CONFIG /* Release */,
			);
			defaultConfigurationIsVisible = 0;
			defaultConfigurationName = Release;
		};
/* End XCConfigurationList section */
	};
	rootObject = PROJECT_REF /* Project object */;
}
PBXPROJ

# Clean previous build
echo "Cleaning previous build..."
rm -rf "${BUILD_DIR}"

# Build with Xcode
echo "Building with xcodebuild..."
cd "${XCODE_PROJECT_DIR}"
xcodebuild -project IconCompiler.xcodeproj -configuration Release -quiet

# Extract compiled assets
COMPILED_APP="${BUILD_DIR}/Release/IconCompiler.app/Contents/Resources"
if [ -f "${COMPILED_APP}/Assets.car" ]; then
    echo "Copying compiled Assets.car..."
    cp "${COMPILED_APP}/Assets.car" "${SCRIPT_DIR}/AppIcon-compiled.car"
else
    echo "Error: Assets.car not found in build output"
    exit 1
fi

if [ -f "${COMPILED_APP}/AppIcon.icns" ]; then
    echo "Copying compiled AppIcon.icns..."
    cp "${COMPILED_APP}/AppIcon.icns" "${SCRIPT_DIR}/AppIcon-compiled.icns"
else
    echo "Error: AppIcon.icns not found in build output"
    exit 1
fi

# Clean up build artifacts (keep the project for future rebuilds)
echo "Cleaning up build artifacts..."
rm -rf "${BUILD_DIR}"
rm -rf "${XCODE_PROJECT_DIR}/AppIcon.icon"

echo ""
echo "Successfully compiled icon assets:"
echo "  - ${SCRIPT_DIR}/AppIcon-compiled.car"
echo "  - ${SCRIPT_DIR}/AppIcon-compiled.icns"
echo ""
echo "These files are used by the CMake build for macOS Liquid Glass icon support."
