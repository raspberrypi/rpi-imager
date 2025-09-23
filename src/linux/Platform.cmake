# Linux platform-specific sources and link settings

find_package(GnuTLS REQUIRED)

set(PLATFORM_SOURCES
    dependencies/mountutils/src/linux/functions.cpp
    linux/linuxdrivelist.cpp
    linux/linuxfile.cpp
    linux/networkmanagerapi.h
    linux/networkmanagerapi.cpp
    linux/stpanalyzer.h
    linux/stpanalyzer.cpp
    linux/acceleratedcryptographichash_gnutls.cpp
    linux/file_operations_linux.cpp
    linux/platformquirks_linux.cpp
    linux/nativefiledialog_linux.cpp
)

set(EXTRALIBS ${EXTRALIBS} GnuTLS::GnuTLS idn2 nettle)
set(DEPENDENCIES "")
add_definitions(-DHAVE_GNUTLS)


