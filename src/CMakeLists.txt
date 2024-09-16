# SPDX-License-Identifier: Apache-2.0
# Copyright (C) 2020-2024 Raspberry Pi Ltd

# To build for Apple platforms you need to set cmake_minimum_required() to 3.21.1 or newer
# see: https://doc.qt.io/qt-6/macos-deployment.html#using-cmake
cmake_minimum_required(VERSION 3.21.1)

OPTION (ENABLE_CHECK_VERSION "Check for version updates" ON)
OPTION (ENABLE_TELEMETRY "Enable sending telemetry" ON)
OPTION (DRIVELIST_FILTER_SYSTEM_DRIVES "Filter System drives from displayed drives" ON)

set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Which macOS architectures to build for")

project(rpi-imager LANGUAGES CXX C)
set(IMAGER_VERSION_MAJOR 1)
set(IMAGER_VERSION_MINOR 9)
set(IMAGER_VERSION_STR "${IMAGER_VERSION_MAJOR}.${IMAGER_VERSION_MINOR}.0")
set(IMAGER_VERSION_CSV "${IMAGER_VERSION_MAJOR},${IMAGER_VERSION_MINOR},0,0")
add_definitions(-DIMAGER_VERSION_STR="${IMAGER_VERSION_STR}")
add_definitions(-DIMAGER_VERSION_CSV=${IMAGER_VERSION_CSV})
set(CMAKE_INCLUDE_CURRENT_DIR ON)
#add_compile_options("-fsanitize=address")
#add_link_options("-fsanitize=address")

# You _must_ set your Qt6 root for the build to progress.
set(Qt6_ROOT "/opt/Qt/6.7.2/gcc_arm64" CACHE PATH "Your Qt6 root path")

if (WIN32)
    set(MINGW64_ROOT "" CACHE PATH "Your MinGW64 root path, likely provided by QtCreator")
endif()

if (APPLE)
    set(IMAGER_SIGNED_APP OFF CACHE BOOL "Perform signing of the Imager .app as part of the build")
    set(IMAGER_SIGNING_IDENTITY "" CACHE STRING "The Developer Identity to use for signing.")
    set(IMAGER_NOTARIZE_APP OFF CACHE BOOL "Perform notarization of the Imager .dmg as part of the build")
    set(IMAGER_NOTARIZE_KEYCHAIN_PROFILE "" CACHE STRING "The name of the Keychain item containing your notarization credentials")
endif(APPLE)

## Preferentially build the bundled code. Full vendoring is to follow in a later version.

# Bundled code will occasionally use identical options - eg, BUILD_TESTING.
set(BUILD_TESTING OFF)

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
set(LIBLZMA_LIBRARY liblzma)
set(LIBLZMA_LIBRARIES liblzma)

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
set(Zstd_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/dependencies/zstd-1.5.6/lib)
set(ZSTD_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/dependencies/zstd-1.5.6/lib)
set(Zstd_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR}/dependencies/zstd-1.5.6/lib)
set(ZSTD_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR}/dependencies/zstd-1.5.6/lib)
set(Zstd_LIBRARIES libzstd_static)
set(ZSTD_LIBRARIES libzstd_static)

# Bundled zlib
set(ZLIB_BUILD_EXAMPLES OFF)
add_subdirectory(dependencies/zlib-1.3.1)
set(ZLIB_FOUND TRUE)
set(ZLIB_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/dependencies/zlib-1.3.1 CACHE PATH "zlib include dir")
set(ZLIB_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR}/dependencies/zlib-1.3.1 CACHE PATH "zlib include dir")
set(ZLIB_LIBRARY zlibstatic)
set(ZLIB_LIBRARIES zlibstatic)

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
set(LibArchive_LIBRARIES archive_static)
set(LibArchive_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/dependencies/libarchive-3.7.4/libarchive)

# Bundled libcurl
set(CMAKE_CURL_INCLUDES)
set(BUILD_STATIC_CURL true)
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "" FORCE)
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
    # TODO: SecureTransport is a deprecated API in macOS, supporting
    #       only up to TLS v1.2. cURL has not implemented the replacement,
    #       Network.framework, and so we will need to select an alternative.
    #       Best recommendation: Libressl, as used by Apple in the curl binary
    #       on macOS.
    set(CURL_USE_SECTRANSP ON)
    set(CURL_DEFAULT_SSL_BACKEND "secure-transport")
else()
    if (WIN32)
        set(CURL_USE_SCHANNEL ON)
        set(CURL_DEFAULT_SSL_BACKEND "schannel")
    else ()
        set(CURL_USE_GNUTLS ON)
        set(CURL_DEFAULT_SSL_BACKEND "gnutls")
    endif(WIN32)
endif(APPLE)

add_subdirectory(dependencies/curl-8.8.0)
set(CURL_FOUND true)
set(CURL_LIBRARIES libcurl_static)
set(CURL_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/dependencies/curl-8.8.0/include)
set(CURL_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR}/dependencies/curl-8.8.0/include)

# Adding headers explicity so they are displayed in Qt Creator
set(HEADERS config.h imagewriter.h networkaccessmanagerfactory.h nan.h drivelistitem.h drivelistmodel.h drivelistmodelpollthread.h driveformatthread.h powersaveblocker.h cli.h
    devicewrapper.h devicewrapperblockcacheentry.h devicewrapperpartition.h devicewrapperstructs.h devicewrapperfatpartition.h wlancredentials.h
    downloadthread.h downloadextractthread.h localfileextractthread.h downloadstatstelemetry.h dependencies/mountutils/src/mountutils.hpp dependencies/sha256crypt/sha256crypt.h)

# Add dependencies
if (APPLE)
    set_source_files_properties("icons/rpi-imager.icns" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    set(PLATFORM_SOURCES
        mac/acceleratedcryptographichash_commoncrypto.cpp
        mac/macfile.cpp
        mac/macfile.h
        dependencies/mountutils/src/darwin/functions.cpp
        mac/macwlancredentials.h
        mac/macwlancredentials.cpp
        dependencies/drivelist/src/darwin/list.mm
        dependencies/drivelist/src/darwin/REDiskList.m
    )
    set(DEPENDENCIES  icons/rpi-imager.icns)
    enable_language(OBJC C)
elseif (UNIX)
    find_package(GnuTLS REQUIRED)
    set(PLATFORM_SOURCES
        dependencies/mountutils/src/linux/functions.cpp
        linux/linuxdrivelist.cpp
        linux/networkmanagerapi.h
        linux/networkmanagerapi.cpp
        linux/stpanalyzer.h
        linux/stpanalyzer.cpp
        linux/acceleratedcryptographichash_gnutls.cpp
    )
    set(EXTRALIBS ${EXTRALIBS} GnuTLS::GnuTLS idn2 nettle)
    set(DEPENDENCIES "")
    add_definitions(-DHAVE_GNUTLS)
elseif (WIN32)
    set(PLATFORM_SOURCES
        windows/acceleratedcryptographichash_cng.cpp
        dependencies/mountutils/src/windows/functions.cpp
        dependencies/drivelist/src/windows/list.cpp
        windows/winfile.cpp
        windows/winfile.h
        windows/winwlancredentials.h
        windows/winwlancredentials.cpp
    )
    set(DEPENDENCIES windows/rpi-imager.rc)
    set(EXTRALIBS setupapi wlanapi Bcrypt.dll)
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

set(SOURCES ${PLATFORM_SOURCES} "main.cpp" "imagewriter.cpp" "networkaccessmanagerfactory.cpp"
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

    # Bundled fat32format
    add_subdirectory(dependencies/fat32format)
    add_dependencies(${PROJECT_NAME} fat32format)

    # Strip debug symbols
    add_custom_command(TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND ${CMAKE_STRIP} "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.exe")

    # Borrowed from the 'mstdlib' project: Code signing
    # First, determine our build architecture
    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(arch x64)
    else ()
            set(arch x86)
    endif ()
    #
    #  Now find signtool
    if (NOT SIGNTOOL)
            # Try to list all Windows 10 SDK versions, if any.
            set(win10_kit_versions)
            set(regkey "HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows Kits\\Installed Roots")
            set(regval "KitsRoot10")
            # Note: must be a cache operation in order to read from the registry.
            get_filename_component(w10_kits_path "[${regkey};${regval}]" ABSOLUTE CACHE)
            if (w10_kits_path)
                message(WARNING "Found Windows 10 kits path: ${w10_kits_path}")
                    file(GLOB w10_kit_versions "${w10_kits_path}/bin/10.*")
                    # Reverse list, so newer (higher-numbered) versions appear first.
                    list(REVERSE w10_kit_versions)
            endif ()
            unset(w10_kits_path CACHE)
            if (w10_kit_versions)
                    find_program(SIGNTOOL
                                    NAMES           signtool
                                    PATHS           ${w10_kit_versions}
                                    PATH_SUFFIXES   ${arch}
                                                    bin/${arch}
                                                    bin
                                    NO_DEFAULT_PATH
                    )
            endif ()
    endif ()

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
    find_program(WINDEPLOYQT "windeployqt.exe" PATHS "${Qt6_ROOT}/bin")
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

    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/windows/rpi-imager.nsi.in"
        "${CMAKE_CURRENT_BINARY_DIR}/rpi-imager.nsi"
        @ONLY)

    add_custom_command(TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "${MINGW64_ROOT}/bin/libgcc_s_seh-1.dll"
            "${MINGW64_ROOT}/bin/libstdc++-6.dll"
            "${MINGW64_ROOT}/bin/libwinpthread-1.dll"
            "${CMAKE_BINARY_DIR}/deploy")

    add_custom_command(TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND "${WINDEPLOYQT}" --no-translations --qmldir "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/deploy/rpi-imager.exe")

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

    message(STATUS "Looking for macdeployqt")
    find_program(MACDEPLOYQT "macdeployqt" PATHS "${Qt6_ROOT}/bin")
    if (NOT MACDEPLOYQT_EXECUTABLE)
        message(FATAL_ERROR "Unable to locate macdeployqt")
    else()
        message(STATUS "Found macdeployqt in ${Qt6_ROOT}/bin")
    endif()

    if(IMAGER_SIGNED_APP)
        if(IMAGER_SIGNING_IDENTITY)
            message(STATUS "Signing identity found")
            # Create the .app, leveraging macdeployqt's knowledge of Qt libraries and configuration files
            add_custom_command(TARGET ${PROJECT_NAME}
                POST_BUILD
                COMMAND "${MACDEPLOYQT_EXECUTABLE}" "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.app" -qmldir="${CMAKE_CURRENT_SOURCE_DIR}")
            add_custom_command(TARGET ${PROJECT_NAME}
                POST_BUILD
                COMMAND mv "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.app" "${CMAKE_BINARY_DIR}/Raspberry\ Pi\ Imager.app")
            # Sign the .app.
            add_custom_command(TARGET ${PROJECT_NAME}
                POST_BUILD
                COMMAND codesign -f --deep --digest-algorithm=sha1,sha256 -o runtime --timestamp -s "${IMAGER_SIGNING_IDENTITY}" "${CMAKE_BINARY_DIR}/Raspberry\ Pi\ Imager.app")

            # Create the .dmg for distribution
            message(STATUS "Creating .dmg file...")
            add_custom_command(TARGET ${PROJECT_NAME}
                POST_BUILD
                COMMAND hdiutil create -volname "Raspberry Pi Imager" -srcfolder "${CMAKE_BINARY_DIR}/Raspberry\ Pi\ Imager.app" -ov -format UDBZ "${CMAKE_BINARY_DIR}/Raspberry\ Pi\ Imager.dmg")

            # Sign the .dmg for distribution, but do not initialise notarisation
            message(STATUS "Signing .dmg file...")
            add_custom_command(TARGET ${PROJECT_NAME}
                POST_BUILD
                COMMAND codesign -f --digest-algorithm=sha1,sha256 -o runtime --timestamp -s "${IMAGER_SIGNING_IDENTITY}" "${CMAKE_BINARY_DIR}/Raspberry\ Pi\ Imager.dmg")

            if(IMAGER_NOTARIZE_APP)
                if(IMAGER_NOTARIZE_KEYCHAIN_PROFILE)
                    add_custom_command(TARGET ${PROJECT_NAME}
                        POST_BUILD
                        COMMAND xcrun notarytool submit "${CMAKE_BINARY_DIR}/Raspberry\ Pi\ Imager.dmg" --wait --keychain-profile "${IMAGER_NOTARIZE_KEYCHAIN_PROFILE}")
                    add_custom_command(TARGET ${PROJECT_NAME}
                        POST_BUILD
                        COMMAND xcrun stapler staple "${CMAKE_BINARY_DIR}/Raspberry\ Pi\ Imager.dmg")
                else()
                    message(FATAL_ERROR "Notarization requested, but no keychain profile is supplied")
                endif(IMAGER_NOTARIZE_KEYCHAIN_PROFILE)

            endif(IMAGER_NOTARIZE_APP)
        else()
            message(FATAL_ERROR "Signing requested, but no signing identity provided")
        endif(IMAGER_SIGNING_IDENTITY)
    else()
        # Unsigned application
        message(STATUS "App will be unsigned")
        add_custom_command(TARGET ${PROJECT_NAME}
            POST_BUILD
            COMMAND "${MACDEPLOYQT_EXECUTABLE}" "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.app" -qmldir="${CMAKE_CURRENT_SOURCE_DIR}" -always-overwrite -no-strip -dmg)
        # Rename .app file
        add_custom_command(TARGET ${PROJECT_NAME}
            POST_BUILD
            COMMAND mv "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.app" "${CMAKE_BINARY_DIR}/Raspberry\ Pi\ Imager.app")
        message(STATUS "Creating .dmg file...")
        add_custom_command(TARGET ${PROJECT_NAME}
            POST_BUILD
            COMMAND hdiutil create -volname "Raspberry Pi Imager" -srcfolder "${CMAKE_BINARY_DIR}/Raspberry\ Pi\ Imager.app" -ov -format UDBZ "${CMAKE_BINARY_DIR}/Raspberry\ Pi\ Imager.dmg")
    endif(IMAGER_SIGNED_APP)

    add_custom_command(TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND "mv" "${CMAKE_BINARY_DIR}/Raspberry\ Pi\ Imager.dmg" "${CMAKE_BINARY_DIR}/Raspberry\ Pi\ Imager-${IMAGER_VERSION_STR}.dmg")

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

    install(TARGETS ${PROJECT_NAME} DESTINATION bin)
    install(FILES "${CMAKE_CURRENT_LIST_DIR}/../debian/rpi-imager.png" DESTINATION share/icons/hicolor/128x128/apps)
    install(FILES "linux/org.raspberrypi.rpi-imager.desktop" DESTINATION share/applications)
    install(FILES "${CMAKE_CURRENT_LIST_DIR}/../debian/rpi-imager.metainfo.xml" DESTINATION share/metainfo)
endif()

include_directories(${CURL_INCLUDE_DIR} ${LibArchive_INCLUDE_DIR} ${LIBLZMA_INCLUDE_DIRS} ${LIBDRM_INCLUDE_DIRS} ${ZLIB_INCLUDE_DIRS} ${ZSTD_INCLUDE_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE ${QT}::Core ${QT}::Quick ${QT}::Svg ${CURL_LIBRARIES} ${LibArchive_LIBRARIES} ${ZSTD_LIBRARIES} ${ZLIB_LIBRARIES} ${LIBLZMA_LIBRARIES} ${LIBDRM_LIBRARIES} ${ATOMIC_LIBRARY} ${EXTRALIBS})