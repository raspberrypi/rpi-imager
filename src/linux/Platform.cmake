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

# DBus-backed components. The embedded (linuxfb netboot) build has no session
# bus and is built without QtDBus, so it uses the same stubs as the CLI build
# for the WiFi-credential and suspend-inhibitor backends, and drops the
# NetworkManager and Pi Connect URI-handler sources entirely. It keeps the
# GUI file dialog (nativefiledialog_linux.cpp), whose DBus portal path is
# QT_DBUS_LIB-guarded and compiles to a QML-only fallback without DBus.
if(BUILD_CLI_ONLY)
    # CLI: no GUI, no DBus.
    list(APPEND PLATFORM_SOURCES
        linux/suspend_inhibitor_stub.cpp
        linux/wlancredentials_stub.cpp
    )
elseif(BUILD_EMBEDDED)
    # Embedded GUI: file dialog kept (QML-only without DBus), rest stubbed.
    list(APPEND PLATFORM_SOURCES
        linux/nativefiledialog_linux.cpp
        linux/suspend_inhibitor_stub.cpp
        linux/wlancredentials_stub.cpp
    )
else()
    # Desktop GUI: full DBus-backed components.
    list(APPEND PLATFORM_SOURCES
        linux/linux_suspend_inhibitor.cpp
        linux/networkmanagerapi.h
        linux/networkmanagerapi.cpp
        linux/nativefiledialog_linux.cpp
        linux/urihandler_dbus.h
        linux/urihandler_dbus.cpp
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


