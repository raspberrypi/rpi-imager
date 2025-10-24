# Linux platform-specific sources and link settings

find_package(GnuTLS REQUIRED)

set(PLATFORM_SOURCES
    dependencies/mountutils/src/linux/functions.cpp
    linux/linuxdrivelist.cpp
    linux/stpanalyzer.h
    linux/stpanalyzer.cpp
    linux/acceleratedcryptographichash_gnutls.cpp
    linux/file_operations_linux.cpp
    linux/platformquirks_linux.cpp
    linux/linux_suspend_inhibitor.cpp
)

# Only include networkmanagerapi for GUI builds (requires Qt Network)
if(NOT BUILD_CLI_ONLY)
    list(APPEND PLATFORM_SOURCES
        linux/networkmanagerapi.h
        linux/networkmanagerapi.cpp
        linux/nativefiledialog_linux.cpp
    )
else()
    # Use stub implementation for CLI builds
    list(APPEND PLATFORM_SOURCES
        linux/wlancredentials_stub.cpp
    )
endif()

set(EXTRALIBS ${EXTRALIBS} GnuTLS::GnuTLS idn2 nettle)
set(DEPENDENCIES "")
add_definitions(-DHAVE_GNUTLS)


