# SPDX-License-Identifier: Apache-2.0
# Copyright (C) 2020 Raspberry Pi Ltd

cmake_minimum_required(VERSION 3.9.4)
OPTION (ENABLE_CHECK_VERSION "Check for version updates" ON)
OPTION (ENABLE_TELEMETRY "Enable sending telemetry" ON)

project(rpi-imager LANGUAGES CXX C)
set(IMAGER_VERSION_MAJOR 1)
set(IMAGER_VERSION_MINOR 8)
set(IMAGER_VERSION_STR "${IMAGER_VERSION_MAJOR}.${IMAGER_VERSION_MINOR}.1")
set(IMAGER_VERSION_CSV "${IMAGER_VERSION_MAJOR},${IMAGER_VERSION_MINOR},1,0")
add_definitions(-DIMAGER_VERSION_STR="${IMAGER_VERSION_STR}")
add_definitions(-DIMAGER_VERSION_CSV=${IMAGER_VERSION_CSV})
set(CMAKE_INCLUDE_CURRENT_DIR ON)

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
    set(DEPENDENCIES dependencies/mountutils/src/linux/functions.cpp linux/linuxdrivelist.cpp linux/networkmanagerapi.h linux/networkmanagerapi.cpp)
    find_package(ZLIB)
    if(ZLIB_FOUND)
        set(EXTRALIBS ${EXTRALIBS} ZLIB::ZLIB)
    endif()
    find_package(LibLZMA)
    if(LIBLZMA_FOUND)
        set(EXTRALIBS ${EXTRALIBS} LibLZMA::LibLZMA)
    endif()
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

find_package(Qt5 5.14 QUIET COMPONENTS Core Quick LinguistTools Svg OPTIONAL_COMPONENTS Widgets DBus WinExtras)
if (Qt5_FOUND)
    set(QT Qt5)
    if (APPLE)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
    endif()
else()
    find_package(Qt6 QUIET COMPONENTS Core Quick LinguistTools Svg OPTIONAL_COMPONENTS Widgets DBus WinExtras)
    if (Qt6_FOUND)
        set(QT Qt6)
        if (APPLE)
            set(CMAKE_OSX_DEPLOYMENT_TARGET "10.14" CACHE STRING "" FORCE)
            set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "" FORCE)
        endif()
    else()
        message( FATAL_ERROR "Missing suitable Qt library (must be at least version 5.14)" )
    endif()
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
    i18n/rpi-imager_ru.ts i18n/rpi-imager_es.ts i18n/rpi-imager_uk.ts i18n/rpi-imager_zh-TW.ts)
#qt5_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TRANSLATIONS})
if (Qt5_FOUND)
    qt5_add_translation(QM_FILES ${TRANSLATIONS})
else()
    qt_add_translation(QM_FILES  ${TRANSLATIONS})
endif()
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

if (NOT WIN32)
    # Enable link time optimization if available
    include(CheckIPOSupported)
    check_ipo_supported(RESULT iposupported OUTPUT ipoerror)

    if(iposupported)
        message("Enabled LTO")
        set_property(TARGET ${PROJECT_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
        message(STATUS "LTO not supported: <${ipoerror}>")
    endif()
endif()

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

# Because dependencies are typically not available by default on Windows, build bundled code
if (WIN32)
    # Target Windows 7 (needed for drivelist module)
    add_definitions(-DWINVER=0x0601 -D_WIN32_WINNT=0x0601)

    find_package(OpenSSL REQUIRED)

    # Bundled zlib
    add_subdirectory(dependencies/zlib-1.2.13)
    set(ZLIB_LIBRARY zlibstatic)
    set(ZLIB_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/zlib-1.2.13 CACHE PATH "zlib include dir")

    # Bundled libcurl
    set(CMAKE_CURL_INCLUDES)
    set(CURL_LIBRARIES cmcurl)
    add_subdirectory(dependencies/cmcurl)
    set(CURL_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/cmcurl/include)

    # Bundled liblzma
    add_subdirectory(dependencies/cmliblzma)
    set(LIBLZMA_HAS_AUTO_DECODER 1)
    set(LIBLZMA_HAS_EASY_ENCODER 1)
    set(LIBLZMA_HAS_LZMA_PRESET 1)
    set(LIBLZMA_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/cmliblzma/liblzma/api)
    set(LIBLZMA_LIBRARY cmliblzma)

    # Bundled zstd
    set(ZSTD_BUILD_PROGRAMS OFF)
    set(ZSTD_BUILD_SHARED OFF)
    add_subdirectory(dependencies/zstd-1.5.4/build/cmake)
    set(ZSTD_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/zstd-1.5.4/lib CACHE PATH "zstd include dir")
    set(ZSTD_LIBRARY libzstd_static)

    # Bundled libarchive
    set(ENABLE_TEST OFF CACHE BOOL "")
    set(ENABLE_TAR OFF CACHE BOOL "")
    set(ENABLE_CPIO OFF CACHE BOOL "")
    set(ENABLE_CAT OFF CACHE BOOL "")
    add_subdirectory(dependencies/libarchive-3.6.2)
    # Disable shared libarchive (we only want static)
    set_target_properties(archive PROPERTIES EXCLUDE_FROM_ALL 1)
    set(LibArchive_LIBRARIES archive_static)
    set(LibArchive_INCLUDE_DIR dependencies/libarchive-3.6.2/libarchive)

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
    find_package(ZLIB REQUIRED)
    find_package(CURL REQUIRED)

    # Bundled liblzma
    add_subdirectory(dependencies/cmliblzma)
    set(LIBLZMA_HAS_AUTO_DECODER 1)
    set(LIBLZMA_HAS_EASY_ENCODER 1)
    set(LIBLZMA_HAS_LZMA_PRESET 1)
    set(LIBLZMA_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/cmliblzma/liblzma/api)
    set(LIBLZMA_LIBRARY cmliblzma)

    # Bundled zstd
    set(ZSTD_BUILD_PROGRAMS OFF)
    set(ZSTD_BUILD_SHARED OFF)
    add_subdirectory(dependencies/zstd-1.5.4/build/cmake)
    set(ZSTD_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/zstd-1.5.4/lib)
    set(ZSTD_LIBRARY libzstd_static)

    # Bundled libarchive
    set(ENABLE_TEST OFF CACHE BOOL "")
    set(ENABLE_TAR OFF CACHE BOOL "")
    set(ENABLE_CPIO OFF CACHE BOOL "")
    set(ENABLE_CAT OFF CACHE BOOL "")
    add_subdirectory(dependencies/libarchive-3.6.2)
    # Disable shared libarchive (we only want static)
    set_target_properties(archive PROPERTIES EXCLUDE_FROM_ALL 1)
    set(LibArchive_LIBRARIES archive_static)
    set(LibArchive_INCLUDE_DIR dependencies/libarchive-3.6.2/libarchive)

    find_library(Cocoa Cocoa)
    find_library(CoreFoundation CoreFoundation)
    find_library(DiskArbitration DiskArbitration)
    find_library(Security Security)
    find_library(IOKit IOKit)
    set(EXTRALIBS ${EXTRALIBS} ${CoreFoundation} ${DiskArbitration} ${Security} ${Cocoa} ${IOKit})
    set_target_properties(${PROJECT_NAME} PROPERTIES MACOSX_BUNDLE YES MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/mac/Info.plist.in)

    find_program(MACDEPLOYQT "macdeployqt" PATHS "${${QT}_DIR}/../../../bin")
    if (NOT MACDEPLOYQT)
        message(FATAL_ERROR "Unable to locate macdeployqt")
    endif()

    add_custom_command(TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND "${MACDEPLOYQT}" "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.app" -qmldir="${CMAKE_CURRENT_SOURCE_DIR}")

else()
    find_package(CURL 7.32.0 REQUIRED)
    find_package(LibArchive 3.2.0 REQUIRED)

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
    install(FILES linux/rpi-imager.desktop DESTINATION share/applications)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/rpi-imager.metainfo.xml" DESTINATION share/metainfo)
endif()

include_directories(${CURL_INCLUDE_DIR} ${LibArchive_INCLUDE_DIR} ${OPENSSL_INCLUDE_DIR} ${LIBLZMA_INCLUDE_DIR} ${LIBDRM_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${QT}::Core ${QT}::Quick ${QT}::Svg ${CURL_LIBRARIES} ${LibArchive_LIBRARIES} ${OPENSSL_LIBRARIES} ${LIBDRM_LIBRARIES} ${ATOMIC_LIBRARY} ${EXTRALIBS})
