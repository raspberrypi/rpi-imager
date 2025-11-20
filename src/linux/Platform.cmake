# Linux platform-specific sources and link settings

find_package(GnuTLS REQUIRED)

set(PLATFORM_SOURCES
    dependencies/mountutils/src/linux/functions.cpp
    linux/linuxdrivelist.cpp
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
set(DEPENDENCIES "")
add_definitions(-DHAVE_GNUTLS)


