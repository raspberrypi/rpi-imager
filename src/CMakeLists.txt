# SPDX-License-Identifier: Apache-2.0
# Copyright (C) 2020 Raspberry Pi Ltd

cmake_minimum_required(VERSION 3.9.4)
if (APPLE)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
endif()

project(rpi-imager LANGUAGES CXX C)
set(IMAGER_VERSION_MAJOR 1)
set(IMAGER_VERSION_MINOR 7)
set(IMAGER_VERSION_STR "${IMAGER_VERSION_MAJOR}.${IMAGER_VERSION_MINOR}.1")
set(IMAGER_VERSION_CSV "${IMAGER_VERSION_MAJOR},${IMAGER_VERSION_MINOR},1,0")
add_definitions(-DIMAGER_VERSION_STR="${IMAGER_VERSION_STR}")
add_definitions(-DIMAGER_VERSION_CSV=${IMAGER_VERSION_CSV})

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Adding headers explicity so they are displayed in Qt Creator
set(HEADERS config.h imagewriter.h networkaccessmanagerfactory.h nan.h drivelistitem.h drivelistmodel.h drivelistmodelpollthread.h driveformatthread.h powersaveblocker.h cli.h
    downloadthread.h downloadextractthread.h localfileextractthread.h downloadstatstelemetry.h dependencies/mountutils/src/mountutils.hpp dependencies/sha256crypt/sha256crypt.h)

# Add dependencies
if (APPLE)
    set_source_files_properties("icons/rpi-imager.icns" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    set(DEPENDENCIES acceleratedcryptographichash.cpp mac/macfile.cpp mac/macfile.h dependencies/mountutils/src/darwin/functions.cpp
        dependencies/drivelist/src/darwin/list.mm dependencies/drivelist/src/darwin/REDiskList.m icons/rpi-imager.icns)
    enable_language(OBJC C)
elseif (UNIX)
    set(DEPENDENCIES acceleratedcryptographichash.cpp dependencies/mountutils/src/linux/functions.cpp linux/linuxdrivelist.cpp)
    find_package(Qt5DBus)
    if(Qt5DBus_FOUND)
        set(DEPENDENCIES ${DEPENDENCIES} linux/udisks2api.cpp linux/udisks2api.h)
        set(EXTRALIBS Qt5::DBus)
        message("udisks2 support enabled")
    else()
        message("DBUS not found. Disabling udisks2 support")
    endif()
    find_package(ZLIB)
    if(ZLIB_FOUND)
        set(EXTRALIBS ${EXTRALIBS} ZLIB::ZLIB)
    endif()
    find_package(LibLZMA)
    if(LIBLZMA_FOUND)
        set(EXTRALIBS ${EXTRALIBS} LibLZMA::LibLZMA)
    endif()
elseif (WIN32)
    set(DEPENDENCIES acceleratedcryptographichash.cpp dependencies/mountutils/src/windows/functions.cpp dependencies/drivelist/src/windows/list.cpp
        windows/winfile.cpp windows/winfile.h
        windows/rpi-imager.rc)
    find_package(Qt5WinExtras REQUIRED)
    set(EXTRALIBS setupapi wlanapi Qt5::WinExtras)
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

set(SOURCES "main.cpp" "imagewriter.cpp" "networkaccessmanagerfactory.cpp"
    "drivelistitem.cpp" "drivelistmodel.cpp" "drivelistmodelpollthread.cpp" "downloadthread.cpp" "downloadextractthread.cpp"
     "driveformatthread.cpp" "localfileextractthread.cpp" "powersaveblocker.cpp" "downloadstatstelemetry.cpp" "qml.qrc" "dependencies/sha256crypt/sha256crypt.c" "cli.cpp")

find_package(Qt5 COMPONENTS Core Quick LinguistTools Svg OPTIONAL_COMPONENTS Widgets)
if (Qt5Widgets_FOUND)
    set(EXTRALIBS ${EXTRALIBS} Qt5::Widgets)
endif()

#qt5_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} i18n/rpi-imager_en.ts i18n/rpi-imager_nl.ts i18n/rpi-imager_zh.ts i18n/rpi-imager_tr.ts i18n/rpi-imager_fr.ts i18n/rpi-imager_de.ts i18n/rpi-imager_sk.ts i18n/rpi-imager_it.ts i18n/rpi-imager_ca.ts i18n/rpi-imager_sl.ts)
qt5_add_translation(QM_FILES i18n/rpi-imager_en.ts i18n/rpi-imager_nl.ts i18n/rpi-imager_zh.ts i18n/rpi-imager_tr.ts i18n/rpi-imager_fr.ts i18n/rpi-imager_de.ts i18n/rpi-imager_sk.ts i18n/rpi-imager_it.ts i18n/rpi-imager_ca.ts i18n/rpi-imager_sl.ts)
configure_file(i18n/translations.qrc "${CMAKE_CURRENT_BINARY_DIR}" COPYONLY)
set(SOURCES ${SOURCES} ${CMAKE_CURRENT_BINARY_DIR}/translations.qrc ${QM_FILES})

if (WIN32)
    # Adding WIN32 prevents a console window being opened on Windows
    add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${HEADERS} ${DEPENDENCIES})
else()
    add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS} ${DEPENDENCIES})
endif()

# Enable link time optimization if available
include(CheckIPOSupported)
check_ipo_supported(RESULT iposupported OUTPUT ipoerror)

if(iposupported)
    message("Enabled LTO")
    set_property(TARGET ${PROJECT_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
else()
    message(STATUS "LTO not supported: <${ipoerror}>")
endif()

# Because dependencies are typically not available by default on Windows, build bundled code
if (WIN32)
    # Target Windows 7 (needed for drivelist module)
    add_definitions(-DWINVER=0x0601 -D_WIN32_WINNT=0x0601)

    find_package(OpenSSL REQUIRED)

    # Bundled zlib
    add_subdirectory(dependencies/zlib-1.2.11)
    set(ZLIB_LIBRARY zlibstatic)
    set(ZLIB_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/zlib-1.2.11)

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
    add_subdirectory(dependencies/zstd-1.5.0/build/cmake)
    set(ZSTD_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/zstd-1.5.0/lib)
    set(ZSTD_LIBRARY libzstd_static)

    # Bundled libarchive
    set(ENABLE_TEST OFF CACHE BOOL "")
    set(ENABLE_TAR OFF CACHE BOOL "")
    set(ENABLE_CPIO OFF CACHE BOOL "")
    set(ENABLE_CAT OFF CACHE BOOL "")
    add_subdirectory(dependencies/libarchive-3.5.2)
    set(LibArchive_LIBRARIES archive_static)
    set(LibArchive_INCLUDE_DIR dependencies/libarchive-3.5.2/libarchive)

    # Bundled fat32format
    add_subdirectory(dependencies/fat32format)
    add_dependencies(${PROJECT_NAME} fat32format)

    # Strip debug symbols
    add_custom_command(TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND ${CMAKE_STRIP} "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.exe")

    # Code signing
    find_program(SIGNTOOL "signtool.exe" PATHS "c:/Program Files (x86)/Microsoft SDKs/ClickOnce/SignTool")
    if (NOT SIGNTOOL)
        message(FATAL_ERROR "Unable to locate signtool.exe used for code signing")
    endif()

    add_custom_command(TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND "${SIGNTOOL}" sign /tr http://timestamp.digicert.com /td sha256 /fd sha256 /a "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.exe")

    add_custom_command(TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND "${SIGNTOOL}" sign /tr http://timestamp.digicert.com /td sha256 /fd sha256 /a "${CMAKE_BINARY_DIR}/dependencies/fat32format/fat32format.exe")

    # Windeploy
    find_program(WINDEPLOYQT "windeployqt.exe" PATHS "${Qt5_DIR}/../../../bin")
    if (NOT WINDEPLOYQT)
        message(FATAL_ERROR "Unable to locate windeployqt.exe")
    endif()

    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/deploy")

    add_custom_command(TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.exe" "${CMAKE_BINARY_DIR}/dependencies/fat32format/fat32format.exe"
            "${CMAKE_SOURCE_DIR}/license.txt" "${CMAKE_SOURCE_DIR}/windows/rpi-imager-cli.cmd"
            "${CMAKE_BINARY_DIR}/deploy")

    add_custom_command(TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "${Qt5_DIR}/../../../bin/libssl-1_1.dll" "${Qt5_DIR}/../../../bin/libcrypto-1_1.dll"
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
    add_subdirectory(dependencies/zstd-1.5.0/build/cmake)
    set(ZSTD_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/zstd-1.5.0/lib)
    set(ZSTD_LIBRARY libzstd_static)

    # Bundled libarchive
    set(ENABLE_TEST OFF CACHE BOOL "")
    set(ENABLE_TAR OFF CACHE BOOL "")
    set(ENABLE_CPIO OFF CACHE BOOL "")
    set(ENABLE_CAT OFF CACHE BOOL "")
    add_subdirectory(dependencies/libarchive-3.5.2)
    set(LibArchive_LIBRARIES archive_static)
    set(LibArchive_INCLUDE_DIR dependencies/libarchive-3.5.2/libarchive)

    find_library(Cocoa Cocoa)
    find_library(CoreFoundation CoreFoundation)
    find_library(DiskArbitration DiskArbitration)
    find_library(Security Security)
    set(EXTRALIBS ${EXTRALIBS} ${CoreFoundation} ${DiskArbitration} ${Security} ${Cocoa})
    set_target_properties(${PROJECT_NAME} PROPERTIES MACOSX_BUNDLE YES MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/mac/Info.plist.in)

    find_program(MACDEPLOYQT "macdeployqt" PATHS "${Qt5_DIR}/../../../bin")
    if (NOT MACDEPLOYQT)
        message(FATAL_ERROR "Unable to locate macdeployqt")
    endif()

    add_custom_command(TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND "${MACDEPLOYQT}" "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.app" -qmldir="${CMAKE_CURRENT_SOURCE_DIR}")

else()
    find_package(CURL 7.32.0 REQUIRED)
    find_package(LibArchive 3.2.0 REQUIRED)
    find_package(OpenSSL REQUIRED)
    if (NOT CMAKE_CROSSCOMPILING)
        find_program(LSBLK "lsblk")
        if (NOT LSBLK)
            message(FATAL_ERROR "Unable to locate lsblk (used for disk enumeration)")
        endif()

        execute_process(COMMAND "${LSBLK}" "--json" RESULT_VARIABLE ret)
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

get_target_property(QT_TARGET_TYPE Qt5::Core TYPE)
if(${QT_TARGET_TYPE} STREQUAL "STATIC_LIBRARY")
    find_package(Qt5QmlImportScanner REQUIRED)
    qt5_import_qml_plugins(${PROJECT_NAME})
    qt5_import_plugins(${PROJECT_NAME} INCLUDE Qt5::QSvgPlugin)
endif()

include_directories(${CURL_INCLUDE_DIR} ${LibArchive_INCLUDE_DIR} ${OPENSSL_INCLUDE_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE Qt5::Core Qt5::Quick Qt5::Svg ${CURL_LIBRARIES} ${LibArchive_LIBRARIES} ${OPENSSL_LIBRARIES} ${ATOMIC_LIBRARY} ${EXTRALIBS})
