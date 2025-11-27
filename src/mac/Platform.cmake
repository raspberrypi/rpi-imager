# macOS platform-specific sources and link settings

# Set up icon resources
# We use the .icon file from Icon Composer for dark mode support (macOS Tahoe+)
# Plus .icns as fallback for older macOS versions
set_source_files_properties("icons/rpi-imager.icns" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")

# Check for Icon Composer .icon file (supports dark mode on macOS Tahoe+)
set(ICON_COMPOSER_FILE "${CMAKE_CURRENT_SOURCE_DIR}/icons/app_icon_macos.icon")
if(EXISTS "${ICON_COMPOSER_FILE}" AND NOT BUILD_CLI_ONLY)
    message(STATUS "Found Icon Composer file: ${ICON_COMPOSER_FILE}")
    message(STATUS "Will include .icon file for dark mode support on macOS Tahoe+")
    
    # Copy the .icon bundle to Resources as AppIcon.icon
    # The .icon file is a bundle (directory) so we track icon.json for changes
    set(APPICON_OUTPUT "${CMAKE_BINARY_DIR}/AppIcon.icon")
    set(APPICON_STAMP "${CMAKE_BINARY_DIR}/AppIcon.icon.stamp")
    add_custom_command(
        OUTPUT "${APPICON_STAMP}"
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${APPICON_OUTPUT}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${ICON_COMPOSER_FILE}" "${APPICON_OUTPUT}"
        COMMAND ${CMAKE_COMMAND} -E touch "${APPICON_STAMP}"
        DEPENDS "${ICON_COMPOSER_FILE}/icon.json"
        COMMENT "Copying Icon Composer file for dark mode support"
        VERBATIM
    )
    add_custom_target(icon_composer_copy ALL DEPENDS "${APPICON_STAMP}")
    set(ICON_COMPOSER_TARGET "icon_composer_copy")
else()
    message(STATUS "Icon Composer file not found at ${ICON_COMPOSER_FILE}")
    set(ICON_COMPOSER_TARGET "")
endif()

set(PLATFORM_SOURCES
    mac/acceleratedcryptographichash_commoncrypto.cpp
    mac/macfile.cpp
    mac/macfile.h
    mac/bootimgcreator_macos.cpp
    mac/rsakeyfingerprint_macos.mm
    dependencies/mountutils/src/darwin/functions.cpp
    dependencies/drivelist/src/darwin/list.mm
    dependencies/drivelist/src/darwin/REDiskList.m
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

# Include .icns for fallback
set(DEPENDENCIES icons/rpi-imager.icns)

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


