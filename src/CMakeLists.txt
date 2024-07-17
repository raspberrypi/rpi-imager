# SPDX-License-Identifier: Apache-2.0
# Copyright (C) 2020-2024 Raspberry Pi Ltd

cmake_minimum_required(VERSION 3.9.4)
OPTION (ENABLE_CHECK_VERSION "Check for version updates" ON)
OPTION (ENABLE_TELEMETRY "Enable sending telemetry" ON)
OPTION (DRIVELIST_FILTER_SYSTEM_DRIVES "Filter System drives from displayed drives" ON)

project(rpi-imager LANGUAGES CXX C)
set(IMAGER_VERSION_MAJOR 1)
set(IMAGER_VERSION_MINOR 8)
set(IMAGER_VERSION_STR "${IMAGER_VERSION_MAJOR}.${IMAGER_VERSION_MINOR}.6")
set(IMAGER_VERSION_CSV "${IMAGER_VERSION_MAJOR},${IMAGER_VERSION_MINOR},6,0")
add_definitions(-DIMAGER_VERSION_STR="${IMAGER_VERSION_STR}")
add_definitions(-DIMAGER_VERSION_CSV=${IMAGER_VERSION_CSV})
set(CMAKE_INCLUDE_CURRENT_DIR ON)
#add_compile_options("-fsanitize=address")
#add_link_options("-fsanitize=address")

## Preferentially build the bundled code. Full vendoring is to follow in a later version.

# Bundled liblzma
set(XZ_MICROLZMA_DECODER OFF)
set(XZ_MICROLZMA_ENCODER OFF)
set(XZ_LZIP_DECODER OFF)
set(XZ_ENABLE_SANDBOX OFF)
set(XZ_BUILD_SHARED_LIBS OFF)
set(XZ_ENABLE_DOXYGEN OFF)
set(XZ_DECODERS
    lzma1
    lzma2
    delta
)
set(XZ_ENCODERS "")
set(CREATE_LZMA_SYMLINKS OFF)
set(CREATE_XZ_SYMLINKS OFF)
add_subdirectory(dependencies/xz-5.6.2)
set(LIBLZMA_FOUND true)
set(LIBLZMA_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/dependencies/xz-5.6.2/src/liblzma/api FORCE)
set(LIBLZMA_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR}/dependencies/xz-5.6.2/src/liblzma/api FORCE)
set(LIBLZMA_LIBRARY ${CMAKE_BINARY_DIR}/dependencies/xz-5.6.2/liblzma.a)
set(LIBLZMA_LIBRARIES ${CMAKE_BINARY_DIR}/dependencies/xz-5.6.2/liblzma.a)

# Bundled zstd
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_DICTBUILDER OFF CACHE BOOL "" FORCE)
add_subdirectory(dependencies/zstd-1.5.6/build/cmake)
set(Zstd_FOUND true)
set(ZSTD_FOUND true)
set(Zstd_VERSION "1.5.6")
set(ZSTD_VERSION "1.5.6")
set(Zstd_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/zstd-1.5.6/lib)
set(ZSTD_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/zstd-1.5.6/lib)
set(Zstd_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/zstd-1.5.6/lib)
set(ZSTD_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/zstd-1.5.6/lib)
set(Zstd_LIBRARIES ${CMAKE_BINARY_DIR}/dependencies/zstd-1.5.6/build/cmake/lib/libzstd.a)
set(ZSTD_LIBRARIES ${CMAKE_BINARY_DIR}/dependencies/zstd-1.5.6/build/cmake/lib/libzstd.a)

#message(FATAL_ERROR "Cmake binary dir: " ${CMAKE_BINARY_DIR})

# Bundled zlib
set(ZLIB_BUILD_EXAMPLES OFF)
add_subdirectory(dependencies/zlib-1.3.1)
set(ZLIB_FOUND TRUE)
set(ZLIB_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/zlib-1.3.1 CACHE PATH "zlib include dir")
set(ZLIB_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/zlib-1.3.1 CACHE PATH "zlib include dir")
set(ZLIB_LIBRARY ${CMAKE_BINARY_DIR}/dependencies/zlib-1.3.1/libz.a)
set(ZLIB_LIBRARIES ${CMAKE_BINARY_DIR}/dependencies/zlib-1.3.1/libz.a)

# Bundled libarchive
set(ARCHIVE_ENABLE_WERROR OFF CACHE BOOL "")
set(ARCHIVE_ENABLE_INSTALL OFF CACHE BOOL "")
set(ARCHIVE_ENABLE_TEST OFF CACHE BOOL "")
set(ARCHIVE_ENABLE_CNG OFF CACHE BOOL "")
set(ARCHIVE_ENABLE_MBEDTLS OFF CACHE BOOL "")
set(ARCHIVE_ENABLE_NETTLE OFF CACHE BOOL "")
set(ARCHIVE_ENABLE_OPENSSL OFF CACHE BOOL "")
set(ARCHIVE_ENABLE_BZip2 OFF CACHE BOOL "")
set(ARCHIVE_ENABLE_LZ4 OFF CACHE BOOL "")
set(ARCHIVE_ENABLE_LZO OFF CACHE BOOL "")
set(ARCHIVE_ENABLE_LIBB2 OFF CACHE BOOL "")
set(ARCHIVE_ENABLE_LIBXML2 OFF CACHE BOOL "")
set(ARCHIVE_ENABLE_EXPAT OFF CACHE BOOL "")
set(ARCHIVE_ENABLE_PCREPOSIX OFF CACHE BOOL "")
set(ARCHIVE_ENABLE_PCRE2POSIX OFF CACHE BOOL "")
set(ARCHIVE_ENABLE_LIBGCC OFF CACHE BOOL "")
set(ARCHIVE_ENABLE_TAR OFF CACHE BOOL "")
set(ARCHIVE_ENABLE_CPIO OFF CACHE BOOL "")
set(ARCHIVE_ENABLE_CAT OFF CACHE BOOL "")
set(ARCHIVE_BUILD_SHARED_LIBS OFF CACHE BOOL "")
add_subdirectory(dependencies/libarchive-3.7.4)
set(LibArchive_FOUND true)
set(LibArchive_LIBRARIES ${CMAKE_BINARY_DIR}/dependencies/libarchive-3.7.4/libarchive/libarchive.a)
set(LibArchive_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/dependencies/libarchive-3.7.4/libarchive)

# Bundled libcurl
set(CMAKE_CURL_INCLUDES)
set(BUILD_STATIC_CURL true)
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(ENABLE_CURL_MANUAL OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_ALTSVC ON)
set(CURL_DISABLE_KERBEROS_AUTH ON)
set(CURL_DISABLE_DICT ON)
set(CURL_DISABLE_DISABLE_FORM_API ON)
set(CURL_DISABLE_FTP ON)
set(CURL_DISABLE_GOPHER ON)
set(CURL_DISABLE_IMAP ON)
set(CURL_DISABLE_LDAP ON)
set(CURL_DISABLE_LDAPS ON)
set(CURL_DISABLE_MQTT ON)
set(CURL_DISABLE_NETRC ON)
set(CURL_DISABLE_POP3 ON)
set(CURL_DISABLE_RTSP ON)
set(CURL_DISABLE_SMTP ON)
set(CURL_DISABLE_TELNET ON)
set(CURL_DISABLE_TFTP ON)
set(CURL_ZSTD ON)
set(CURL_ENABLE_EXPORT_TARGET OFF CACHE BOOL "" FORCE)
if (APPLE)
    set(CURL_USE_SECTRANSP ON)
else()
    if (WIN32)
        set(CURL_USE_SCHANNEL ON)
    else ()
        set(CURL_USE_OPENSSL ON)
    endif(WIN32)
endif(APPLE)

add_subdirectory(dependencies/curl-8.8.0)
set(CURL_FOUND true)
set(CURL_LIBRARIES ${CMAKE_BINARY_DIR}/dependencies/curl-8.8.0/lib/libcurl-d.a)
set(CURL_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/curl-8.8.0/include)
set(CURL_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/curl-8.8.0/include)

# Adding headers explicity so they are displayed in Qt Creator
set(HEADERS config.h imagewriter.h networkaccessmanagerfactory.h nan.h drivelistitem.h drivelistmodel.h drivelistmodelpollthread.h driveformatthread.h powersaveblocker.h cli.h
    devicewrapper.h devicewrapperblockcacheentry.h devicewrapperpartition.h devicewrapperstructs.h devicewrapperfatpartition.h wlancredentials.h
    downloadthread.h downloadextractthread.h localfileextractthread.h downloadstatstelemetry.h dependencies/mountutils/src/mountutils.hpp dependencies/sha256crypt/sha256crypt.h)

# Add dependencies
if (APPLE)
    set_source_files_properties("icons/rpi-imager.icns" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    set(DEPENDENCIES acceleratedcryptographichash.cpp mac/macfile.cpp mac/macfile.h dependencies/mountutils/src/darwin/functions.cpp
        mac/macwlancredentials.h mac/macwlancredentials.cpp
        dependencies/drivelist/src/darwin/list.mm dependencies/drivelist/src/darwin/REDiskList.m icons/rpi-imager.icns)
    enable_language(OBJC C)
elseif (UNIX)
    set(DEPENDENCIES dependencies/mountutils/src/linux/functions.cpp linux/linuxdrivelist.cpp linux/networkmanagerapi.h linux/networkmanagerapi.cpp linux/stpanalyzer.h linux/stpanalyzer.cpp)
    find_package(GnuTLS)
    if (GnuTLS_FOUND)
        set(DEPENDENCIES ${DEPENDENCIES} acceleratedcryptographichash_gnutls.cpp)
        set(EXTRALIBS ${EXTRALIBS} GnuTLS::GnuTLS)
        add_definitions(-DHAVE_GNUTLS)
    else()
        find_package(OpenSSL REQUIRED)
        set(DEPENDENCIES ${DEPENDENCIES} acceleratedcryptographichash.cpp)
    endif()
elseif (WIN32)
    set(DEPENDENCIES acceleratedcryptographichash.cpp dependencies/mountutils/src/windows/functions.cpp dependencies/drivelist/src/windows/list.cpp
        windows/winfile.cpp windows/winfile.h windows/winwlancredentials.h windows/winwlancredentials.cpp
        windows/rpi-imager.rc wlanapi_delayed.lib)
    set(EXTRALIBS setupapi ${CMAKE_CURRENT_BINARY_DIR}/wlanapi_delayed.lib)
    add_custom_command(
        OUTPUT wlanapi_delayed.lib
        COMMAND ${CMAKE_DLLTOOL} --input-def "${CMAKE_CURRENT_SOURCE_DIR}/windows/wlanapi.def"
                --output-delaylib "wlanapi_delayed.lib" --dllname "wlanapi.dll"
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/windows/wlanapi.def
        VERBATIM
    )
endif()

include_directories(BEFORE .)

# Test if we need libatomic
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("
    #include <atomic>
    #include <stdint.h>
    int main() {
        std::atomic<int64_t> x;
        x = 1;
        return (int) x;
    }"
    atomicbuiltin)

if (NOT atomicbuiltin)
        find_library(ATOMIC_LIBRARY NAMES atomic libatomic.so.1)
        if (NOT ATOMIC_LIBRARY)
                message( FATAL_ERROR "Missing libatomic while architecture does need it" )
        endif()
endif()

include(TestBigEndian)
test_big_endian(IS_BIG_ENDIAN)
if( IS_BIG_ENDIAN )
    message( FATAL_ERROR "We currently only support 'little endian' CPU architectures" )
endif( IS_BIG_ENDIAN )

set(SOURCES "main.cpp" "imagewriter.cpp" "networkaccessmanagerfactory.cpp"
    "drivelistitem.cpp" "drivelistmodel.cpp" "drivelistmodelpollthread.cpp" "downloadthread.cpp" "downloadextractthread.cpp"
    "devicewrapper.cpp" "devicewrapperblockcacheentry.cpp" "devicewrapperpartition.cpp" "devicewrapperfatpartition.cpp"
    "driveformatthread.cpp" "localfileextractthread.cpp" "powersaveblocker.cpp" "downloadstatstelemetry.cpp" "qml.qrc" "dependencies/sha256crypt/sha256crypt.c" "cli.cpp")

find_package(Qt6 6.7 QUIET COMPONENTS Core Quick LinguistTools Svg OPTIONAL_COMPONENTS Widgets DBus WinExtras)
if (Qt6_FOUND)
    set(QT Qt6)
    if (APPLE)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11" CACHE STRING "" FORCE)
    endif()
else()
    message(FATAL_ERROR "Missing suitable Qt library (must be at least version 6.7)" )
endif()

if (${QT}Widgets_FOUND)
    set(EXTRALIBS ${EXTRALIBS} ${QT}::Widgets)
endif()
if(${QT}DBus_FOUND AND UNIX AND NOT APPLE)
    set(DEPENDENCIES ${DEPENDENCIES} linux/udisks2api.cpp linux/udisks2api.h)
    set(EXTRALIBS ${EXTRALIBS} ${QT}::DBus)
    message("udisks2 support enabled")
endif()
if (NOT ${QT}Widgets_FOUND AND UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBDRM REQUIRED libdrm)
endif()
if(${QT}WinExtras_FOUND)
    set(EXTRALIBS ${EXTRALIBS} ${QT}::WinExtras)
endif()

set(TRANSLATIONS i18n/rpi-imager_en.ts i18n/rpi-imager_nl.ts i18n/rpi-imager_zh.ts i18n/rpi-imager_tr.ts
    i18n/rpi-imager_fr.ts i18n/rpi-imager_de.ts i18n/rpi-imager_sk.ts i18n/rpi-imager_it.ts
    i18n/rpi-imager_ca.ts i18n/rpi-imager_sl.ts i18n/rpi-imager_ko.ts i18n/rpi-imager_ja.ts
    i18n/rpi-imager_ru.ts i18n/rpi-imager_es.ts i18n/rpi-imager_uk.ts i18n/rpi-imager_zh-TW.ts
    i18n/rpi-imager_pl.ts)
#qt6_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TRANSLATIONS})
qt_add_translation(QM_FILES  ${TRANSLATIONS})
configure_file(i18n/translations.qrc "${CMAKE_CURRENT_BINARY_DIR}" COPYONLY)
set(SOURCES ${SOURCES} ${CMAKE_CURRENT_BINARY_DIR}/translations.qrc ${QM_FILES})

if (WIN32)
    # Adding WIN32 prevents a console window being opened on Windows
    add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${HEADERS} ${DEPENDENCIES})
else()
    add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS} ${DEPENDENCIES})
endif()

set_property(TARGET ${PROJECT_NAME} PROPERTY AUTOMOC ON)
set_property(TARGET ${PROJECT_NAME} PROPERTY AUTORCC ON)
set_property(TARGET ${PROJECT_NAME} PROPERTY AUTOUIC ON)

if(ENABLE_TELEMETRY)
    add_definitions(-DTELEMETRY_ENABLED_DEFAULT=true)
else()
    add_definitions(-DTELEMETRY_ENABLED_DEFAULT=false)
endif()

if(ENABLE_CHECK_VERSION)
    add_definitions(-DCHECK_VERSION_DEFAULT=true)
else()
    add_definitions(-DCHECK_VERSION_DEFAULT=false)
endif()

if(DRIVELIST_FILTER_SYSTEM_DRIVES)
   # Hide system drives from list
   add_definitions(-DDRIVELIST_FILTER_SYSTEM_DRIVES=true)
else()
   add_definitions(-DDRIVELIST_FILTER_SYSTEM_DRIVES=false)
endif()

# Because dependencies are typically not available by default on Windows, build bundled code
if (WIN32)
    # Target Windows 10, in line with Qt requirements
    add_definitions(-DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 -DNTDDI_VERION=0x0A000000)

    find_package(OpenSSL REQUIRED)

    # Bundled fat32format
    add_subdirectory(dependencies/fat32format)
    add_dependencies(${PROJECT_NAME} fat32format)

    # Strip debug symbols
    add_custom_command(TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND ${CMAKE_STRIP} "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.exe")

    # Code signing
    find_program(SIGNTOOL "signtool.exe" PATHS
        "c:/Program Files (x86)/Microsoft SDKs/ClickOnce/SignTool"
        "c:/Program Files (x86)/Windows Kits/10/bin/10.0.22621.0/x64")
    if (NOT SIGNTOOL)
        message(FATAL_ERROR "Unable to locate signtool.exe used for code signing")
    endif()
    add_definitions(-DSIGNTOOL="${SIGNTOOL}")

    add_custom_command(TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND "${SIGNTOOL}" sign /tr http://timestamp.digicert.com /td sha256 /fd sha256 /a "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.exe")

    add_custom_command(TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND "${SIGNTOOL}" sign /tr http://timestamp.digicert.com /td sha256 /fd sha256 /a "${CMAKE_BINARY_DIR}/dependencies/fat32format/fat32format.exe")

    # Windeploy
    find_program(WINDEPLOYQT "windeployqt.exe" PATHS "${${QT}_DIR}/../../../bin")
    if (NOT WINDEPLOYQT)
        message(FATAL_ERROR "Unable to locate windeployqt.exe")
    endif()

    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/deploy")

    add_custom_command(TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.exe" "${CMAKE_BINARY_DIR}/dependencies/fat32format/fat32format.exe"
            "${CMAKE_SOURCE_DIR}/../license.txt" "${CMAKE_SOURCE_DIR}/windows/rpi-imager-cli.cmd"
            "${CMAKE_BINARY_DIR}/deploy")

    add_custom_command(TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "${${QT}_DIR}/../../../bin/libssl-1_1.dll" "${${QT}_DIR}/../../../bin/libcrypto-1_1.dll"
            "${CMAKE_BINARY_DIR}/deploy")

    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/windows/rpi-imager.nsi.in"
        "${CMAKE_CURRENT_BINARY_DIR}/rpi-imager.nsi"
        @ONLY)

    add_custom_command(TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND "${WINDEPLOYQT}" --no-translations --no-webkit2 --no-opengl-sw --angle --qmldir "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/deploy/rpi-imager.exe")

    # Remove excess files
    add_custom_command(TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E remove
            "${CMAKE_BINARY_DIR}/deploy/imageformats/qtiff.dll"
            "${CMAKE_BINARY_DIR}/deploy/imageformats/qwebp.dll"
            "${CMAKE_BINARY_DIR}/deploy/imageformats/qgif.dll")

elseif(APPLE)
    find_library(Cocoa Cocoa)
    find_library(CoreFoundation CoreFoundation)
    find_library(DiskArbitration DiskArbitration)
    find_library(Security Security)
    find_library(IOKit IOKit)
    find_library(SystemConfiguration SystemConfiguration)
    set(EXTRALIBS ${EXTRALIBS} ${CoreFoundation} ${DiskArbitration} ${Security} ${Cocoa} ${IOKit} ${SystemConfiguration} iconv)
    set_target_properties(${PROJECT_NAME} PROPERTIES MACOSX_BUNDLE YES MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/mac/Info.plist.in)

    find_program(MACDEPLOYQT "macdeployqt" PATHS "${${QT}_DIR}/../../../bin")
    if (NOT MACDEPLOYQT)
        message(FATAL_ERROR "Unable to locate macdeployqt")
    endif()

    add_custom_command(TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND "${MACDEPLOYQT}" "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.app" -qmldir="${CMAKE_CURRENT_SOURCE_DIR}")

else()
    # UNIX, Linux systems
    if (NOT CMAKE_CROSSCOMPILING)
        find_program(LSBLK "lsblk")
        if (NOT LSBLK)
            message(FATAL_ERROR "Unable to locate lsblk (used for disk enumeration)")
        endif()

        execute_process(COMMAND "${LSBLK}" "--json" OUTPUT_QUIET RESULT_VARIABLE ret)
        if (ret EQUAL "1")
            message(FATAL_ERROR "util-linux package too old. lsblk does not support --json (used for disk enumeration)")
        endif()
    endif()

    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/linux/rpi-imager.metainfo.xml.in"
        "${CMAKE_CURRENT_BINARY_DIR}/rpi-imager.metainfo.xml"
        @ONLY)

    install(TARGETS rpi-imager DESTINATION bin)
    install(FILES icons/rpi-imager.png DESTINATION share/icons/hicolor/128x128/apps)
    install(FILES linux/org.raspberrypi.rpi-imager.desktop DESTINATION share/applications)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/rpi-imager.metainfo.xml" DESTINATION share/metainfo)
endif()

include_directories(${CURL_INCLUDE_DIR} ${LibArchive_INCLUDE_DIR} ${OPENSSL_INCLUDE_DIR} ${LIBLZMA_INCLUDE_DIRS} ${LIBDRM_INCLUDE_DIRS} ${ZLIB_INCLUDE_DIRS} ${ZSTD_INCLUDE_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE ${QT}::Core ${QT}::Quick ${QT}::Svg ${ZSTD_LIBRARIES} ${ZLIB_LIBRARIES} ${LIBLZMA_LIBRARIES} ${CURL_LIBRARIES} ${LibArchive_LIBRARIES} ${OPENSSL_LIBRARIES} ${LIBDRM_LIBRARIES} ${ATOMIC_LIBRARY} ${EXTRALIBS})
