# FreeBSD platform-specific sources and link settings

find_package(GnuTLS REQUIRED)

find_package(PkgConfig REQUIRED)

# local include packages
set(__PKG_CONFIG_OLD ${PKG_CONFIG})
set(PKG_CONFIG "${PKG_CONFIG} --static")
pkg_check_modules(ZSTD REQUIRED IMPORTED_TARGET libzstd)
set(PKG_CONFIG ${__PKG_CONFIG_OLD})
unset(__PKG_CONFIG_OLD)

pkg_check_modules(NETTLE REQUIRED IMPORTED_TARGET nettle)
pkg_check_modules(LIBIDN2 REQUIRED IMPORTED_TARGET libidn2)

set(PLATFORM_SOURCES
    drivelist/drivelist_freebsd.cpp
    linux/acceleratedcryptographichash_gnutls.cpp
    freebsd/bootimgcreator_freebsd.cpp
    linux/secureboot_crypto_linux.cpp
    unix/file_operations_unix.cpp
    freebsd/file_operations_freebsd.cpp
    freebsd/platformquirks_freebsd.cpp
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

set(EXTRALIBS ${EXTRALIBS} GnuTLS::GnuTLS PkgConfig::ZSTD PkgConfig::NETTLE PkgConfig::LIBIDN2)

set(DEPENDENCIES "")
add_definitions(-DHAVE_GNUTLS)

# libusb requires libudev for rpiboot support
pkg_check_modules(UDEV REQUIRED libudev)
