# macOS platform-specific sources and link settings

set_source_files_properties("icons/rpi-imager.icns" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")

set(PLATFORM_SOURCES
    mac/acceleratedcryptographichash_commoncrypto.cpp
    mac/macfile.cpp
    mac/macfile.h
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


