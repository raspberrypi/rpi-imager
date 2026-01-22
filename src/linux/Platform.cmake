# Linux platform-specific sources and link settings

find_package(GnuTLS REQUIRED)

# Find liburing for async I/O (Linux 5.1+)
# Uses pkg-config since liburing doesn't have a CMake config
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBURING liburing)

if(LIBURING_FOUND)
    message(STATUS "Found liburing: ${LIBURING_VERSION}")
    add_definitions(-DHAVE_LIBURING)
else()
    message(WARNING "liburing not found - async I/O will be disabled. Install with: sudo apt install liburing-dev")
endif()

set(PLATFORM_SOURCES
    drivelist/drivelist_linux.cpp
    linux/stpanalyzer.h
    linux/stpanalyzer.cpp
    linux/acceleratedcryptographichash_gnutls.cpp
    linux/bootimgcreator_linux.cpp
    linux/rsakeyfingerprint_linux.cpp
    linux/file_operations_linux.cpp
    linux/platformquirks_linux.cpp
)

# Only include DBus-dependent and GUI components for non-CLI builds
if(NOT BUILD_CLI_ONLY)
    list(APPEND PLATFORM_SOURCES
        linux/linux_suspend_inhibitor.cpp
        linux/networkmanagerapi.h
        linux/networkmanagerapi.cpp
        linux/nativefiledialog_linux.cpp
        linux/urihandler_dbus.h
        linux/urihandler_dbus.cpp
    )
else()
    # Use stub implementations for CLI builds (no DBus dependency)
    list(APPEND PLATFORM_SOURCES
        linux/suspend_inhibitor_stub.cpp
        linux/wlancredentials_stub.cpp
    )
endif()

set(EXTRALIBS ${EXTRALIBS} GnuTLS::GnuTLS idn2 nettle)

# Add liburing if available
if(LIBURING_FOUND)
    set(EXTRALIBS ${EXTRALIBS} ${LIBURING_LIBRARIES})
    include_directories(${LIBURING_INCLUDE_DIRS})
endif()

set(DEPENDENCIES "")
add_definitions(-DHAVE_GNUTLS)


