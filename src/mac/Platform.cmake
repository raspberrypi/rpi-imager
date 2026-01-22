# macOS platform-specific sources and link settings

# Set up icon resources
# We use pre-compiled icon assets for dark mode/Liquid Glass support (macOS Tahoe+)
# These are generated from app_icon_macos.icon using compile-icon.sh
# The actual installation happens in PlatformPackaging.cmake after macdeployqt runs
set(PRECOMPILED_ICNS "${CMAKE_CURRENT_SOURCE_DIR}/icons/AppIcon-compiled.icns")
if(EXISTS "${PRECOMPILED_ICNS}" AND NOT BUILD_CLI_ONLY)
    set_source_files_properties("${PRECOMPILED_ICNS}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    message(STATUS "Found pre-compiled icon assets for dark mode support")
else()
    message(STATUS "Pre-compiled icon assets not found - run src/icons/compile-icon.sh")
endif()

set(PLATFORM_SOURCES
    mac/acceleratedcryptographichash_commoncrypto.cpp
    mac/macfile.cpp
    mac/macfile.h
    mac/bootimgcreator_macos.cpp
    mac/rsakeyfingerprint_macos.mm
    drivelist/drivelist_darwin.mm
    mac/file_operations_macos.cpp
    mac/platformquirks_macos.mm
    mac/mac_suspend_inhibitor.cpp
)

# Only include GUI-specific components for non-CLI builds
if(NOT BUILD_CLI_ONLY)
    list(APPEND PLATFORM_SOURCES
        mac/macwlancredentials.h
        mac/macwlancredentials.cpp
        mac/ssid_helper.h
        mac/ssid_helper.mm
        mac/location_helper.h
        mac/location_helper.mm
        mac/nativefiledialog_macos.mm
    )
else()
    # Use stub implementation for CLI builds
    list(APPEND PLATFORM_SOURCES
        linux/wlancredentials_stub.cpp
    )
endif()

# Include pre-compiled .icns for fallback (only if it exists)
if(EXISTS "${PRECOMPILED_ICNS}")
    set(DEPENDENCIES "${PRECOMPILED_ICNS}")
endif()

enable_language(OBJC C)

# Frameworks for linking
find_library(Cocoa Cocoa)
find_library(CoreFoundation CoreFoundation)
find_library(DiskArbitration DiskArbitration)
find_library(Security Security)
find_library(IOKit IOKit)
find_library(SystemConfiguration SystemConfiguration)
find_library(CoreWLAN CoreWLAN)
find_library(CoreLocation CoreLocation)
set(EXTRALIBS ${EXTRALIBS} ${CoreFoundation} ${DiskArbitration} ${Security} ${Cocoa} ${IOKit} ${SystemConfiguration} ${CoreWLAN} ${CoreLocation} iconv)


