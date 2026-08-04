# Linux platform-specific sources and link settings

find_package(GnuTLS REQUIRED)

# Find liburing for async I/O (Linux 5.1+)
# Uses pkg-config since liburing doesn't have a CMake config
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBURING liburing)

# file_operations_linux.cpp uses the 64-bit user_data helpers
# (io_uring_sqe_set_data64 / io_uring_prep_cancel64), added in liburing 2.2.
# Some distros ship an older liburing (e.g. Ubuntu 22.04's 2.1) that provides
# the library but not these helpers, so probe for them before enabling io_uring.
set(LIBURING_USABLE FALSE)
if(LIBURING_FOUND)
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_INCLUDES ${LIBURING_INCLUDE_DIRS})
    set(CMAKE_REQUIRED_LIBRARIES ${LIBURING_LIBRARIES})
    check_cxx_source_compiles("
#include <liburing.h>
int main() {
    struct io_uring_sqe *sqe = 0;
    io_uring_sqe_set_data64(sqe, 0);
    io_uring_prep_cancel64(sqe, 0, 0);
    return 0;
}" LIBURING_HAS_DATA64)
    unset(CMAKE_REQUIRED_INCLUDES)
    unset(CMAKE_REQUIRED_LIBRARIES)
    set(LIBURING_USABLE ${LIBURING_HAS_DATA64})
endif()

if(LIBURING_USABLE)
    message(STATUS "Found liburing: ${LIBURING_VERSION} (async io_uring enabled)")
    add_definitions(-DHAVE_LIBURING)
elseif(LIBURING_FOUND)
    message(WARNING "liburing ${LIBURING_VERSION} lacks the 64-bit user_data API (need >= 2.2); async io_uring disabled, falling back to synchronous writes")
else()
    message(WARNING "liburing not found - async I/O will be disabled. Install with: sudo apt install liburing-dev")
endif()

set(PLATFORM_SOURCES
    drivelist/drivelist_linux.cpp
    linux/stpanalyzer.h
    linux/stpanalyzer.cpp
    linux/acceleratedcryptographichash_gnutls.cpp
    linux/bootimgcreator_linux.cpp
    linux/secureboot_crypto_linux.cpp
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

# Add liburing if usable (see the API probe above)
if(LIBURING_USABLE)
    set(EXTRALIBS ${EXTRALIBS} ${LIBURING_LIBRARIES})
    include_directories(${LIBURING_INCLUDE_DIRS})
endif()

set(DEPENDENCIES "")
add_definitions(-DHAVE_GNUTLS)

# libusb requires libudev for rpiboot support
pkg_check_modules(UDEV REQUIRED libudev)


