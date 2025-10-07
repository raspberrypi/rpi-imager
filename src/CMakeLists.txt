# Common helper to generate build-time resources with fallback
include(cmake/GeneratedResource.cmake)
# SPDX-License-Identifier: Apache-2.0
# Copyright (C) 2020-2025 Raspberry Pi Ltd

cmake_minimum_required(VERSION 3.22)
OPTION (ENABLE_CHECK_VERSION "Check for version updates" ON)
OPTION (ENABLE_TELEMETRY "Enable sending telemetry" ON)
OPTION (BUILD_EMBEDDED "Build for Embedded Imager" OFF)
OPTION (BUILD_CLI_ONLY "Build CLI-only version without GUI components" OFF)

# We use FetchContent_Populate() instead of FetchContent_MakeAvailable() to allow EXCLUDE_FROM_ALL
# This prevents the dependencies from being built by default, which is our desired behavior
# CMP0169 warns about this usage, but we intentionally want the old behavior
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

# OVERRIDE_FIND_PACKAGE is only available in CMake 3.24+
# Create a variable to conditionally use it
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.24")
    set(USE_OVERRIDE_FIND_PACKAGE "OVERRIDE_FIND_PACKAGE")
else()
    set(USE_OVERRIDE_FIND_PACKAGE "")
endif()

set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Which macOS architectures to build for")

project(rpi-imager LANGUAGES CXX C)
set(IMAGER_VERSION_MAJOR 2)
set(IMAGER_VERSION_MINOR 0)
set(IMAGER_VERSION_PATCH 0)
set(IMAGER_VERSION_STR "${IMAGER_VERSION_MAJOR}.${IMAGER_VERSION_MINOR}.${IMAGER_VERSION_PATCH}")
set(IMAGER_VERSION_CSV "${IMAGER_VERSION_MAJOR},${IMAGER_VERSION_MINOR},${IMAGER_VERSION_PATCH},0")
add_definitions(-DIMAGER_VERSION_STR="${IMAGER_VERSION_STR}")
add_definitions(-DIMAGER_VERSION_CSV=${IMAGER_VERSION_CSV})
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Apply optimization flags globally to all targets (including bundled dependencies)
if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo" OR CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
    # Use -Os instead of -O3 for better code size vs performance balance
    # Enable function and data sections for dead code elimination
    add_compile_options(-Os -ffunction-sections -fdata-sections)
    
    # Enable dead code elimination at link time (for executables and shared libraries only)
    if(APPLE)
        # macOS uses -dead_strip instead of --gc-sections
        add_link_options(-Wl,-dead_strip)
    elseif(UNIX)
        # Linux uses --gc-sections
        add_link_options(-Wl,--gc-sections)
    elseif(WIN32)
        # Windows with MinGW uses --gc-sections
        add_link_options(-Wl,--gc-sections)
    endif()
    
    # Enable Link Time Optimization (LTO) for non-Windows targets
    if(NOT WIN32)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
    
    # Only add build-type specific flags (optimization and sections flags are already added globally above)
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -DNDEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -DNDEBUG")
    set(CMAKE_C_FLAGS_MINSIZEREL "${CMAKE_C_FLAGS_MINSIZEREL} -DNDEBUG")
    set(CMAKE_CXX_FLAGS_MINSIZEREL "${CMAKE_CXX_FLAGS_MINSIZEREL} -DNDEBUG")
    # RelWithDebInfo keeps debug info, so no -DNDEBUG for that build type
endif()

#add_compile_options("-fsanitize=address")
#add_link_options("-fsanitize=address")

# Leave empty by default so we don't inject the wrong path on other platforms. (changing on every QT version)
# Users/Kits can set Qt6_ROOT or CMAKE_PREFIX_PATH externally.
set(Qt6_ROOT "" CACHE PATH "Path to Qt6 (optional; leave empty if using CMAKE_PREFIX_PATH)")

# If provided, use it to help CMake find Qt
if(Qt6_ROOT)
  list(PREPEND CMAKE_PREFIX_PATH "${Qt6_ROOT}")
endif()

if (WIN32)
  # Only seed the cache if not already provided by the user/kit
  if(NOT DEFINED MINGW64_ROOT OR MINGW64_ROOT STREQUAL "")
    # Try to guess from the C compiler location (…/mingwXXXX_64/bin/gcc.exe)
    get_filename_component(_cc_bin_dir "${CMAKE_C_COMPILER}" DIRECTORY)
    get_filename_component(_guess_root  "${_cc_bin_dir}" DIRECTORY)  # one level up from bin/

    set(_candidate "")
    if(EXISTS "${_guess_root}/bin/g++.exe")
      set(_candidate "${_guess_root}")
    elseif(EXISTS "C:/Qt/Tools/mingw1310_64/bin/g++.exe")
      set(_candidate "C:/Qt/Tools/mingw1310_64")
    endif()

    set(MINGW64_ROOT "${_candidate}" CACHE PATH "Your MinGW64 root path, likely provided by QtCreator")
    unset(_cc_bin_dir)
    unset(_guess_root)
    unset(_candidate)
  endif()

  set(IMAGER_SIGNED_APP OFF CACHE BOOL "Sign Imager and its installer as part of the build. Requires a valid Code Signing certificate.")
endif()

if (APPLE)
    set(IMAGER_SIGNED_APP OFF CACHE BOOL "Perform signing of the Imager .app as part of the build")
    set(IMAGER_SIGNING_IDENTITY "" CACHE STRING "The Developer Identity to use for signing.")
    set(IMAGER_NOTARIZE_APP OFF CACHE BOOL "Perform notarization of the Imager .dmg as part of the build")
    set(IMAGER_NOTARIZE_KEYCHAIN_PROFILE "" CACHE STRING "The name of the Keychain item containing your notarization credentials")

    # Per CMake Bug 21918, if you do not use the following block, CMake will automatically include homebrew libraries.
    # This is undesirable, as on macOS/Apple Silicon, you may find newer versions of Qt will pull in new dependencies
    # - breaking your x86_64 build prematurely. Regardless, this is desirable behaviour to make the build more predictable.
    set(CMAKE_IGNORE_PATH)
    foreach(_prefix /sw /opt/local /usr/local /opt/homebrew)
      list(APPEND CMAKE_IGNORE_PATH ${_prefix}/bin ${_prefix}/include ${_prefix}/lib)
      list(APPEND CMAKE_SYSTEM_IGNORE_PATH ${_prefix}/bin ${_prefix}/include ${_prefix}/lib)
    endforeach()
endif(APPLE)

# Bundled code will occasionally use identical options - eg, BUILD_TESTING.
set(BUILD_TESTING OFF)
set(BUILD_STATIC_LIBS ON)
set(BUILD_SHARED_LIBS OFF)

include(FetchContent)

# Bundled liblzma
include(dependencies/xz.cmake)

# Bundled zstd
include(dependencies/zstd.cmake)

# Remote nghttp2
include(dependencies/nghttp2.cmake)

# Bundled yescrypt
include(dependencies/yescrypt.cmake)


# Bundled zlib
include(dependencies/zlib.cmake)

# Bundled libarchive
include(dependencies/libarchive.cmake)

# libcurl
if(APPLE)
    # In version 8.15.0, libcurl dropped support for Secure Transport, because it
    # does not implement TLS 1.3. Unfortunately, there was no replacement Network.framework implementation,
    # so on macOS we're forced to use the macOS SDK version of libcurl, including libcurl's own OpenSSL-analogue.
    find_package(CURL REQUIRED)
    # Normalize variable naming used later in include_directories
    set(CURL_INCLUDE_DIR ${CURL_INCLUDE_DIRS})
else()
    include(dependencies/curl.cmake)
endif()


# Add dependencies
if (APPLE)
    include(mac/Platform.cmake)
elseif (UNIX)
    include(linux/Platform.cmake)
elseif (WIN32)
    include(windows/Platform.cmake)
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

# Base sources common to all builds
set(SOURCES_BASE ${PLATFORM_SOURCES} "main.cpp"
    "drivelistitem.cpp" "drivelistmodel.cpp" "drivelistmodelpollthread.cpp" "downloadthread.cpp" "downloadextractthread.cpp"
    "devicewrapper.cpp" "devicewrapperblockcacheentry.cpp" "devicewrapperpartition.cpp" "devicewrapperfatpartition.cpp"
    "driveformatthread.cpp" "localfileextractthread.cpp" "powersaveblocker.cpp" "downloadstatstelemetry.cpp" "dependencies/sha256crypt/sha256crypt.c" "cli.cpp"
    "disk_formatter.cpp" "file_operations.cpp" "cachemanager.cpp" "systemmemorymanager.cpp" "imageadvancedoptions.cpp")

# Add GUI-specific sources only for non-CLI builds
if(BUILD_CLI_ONLY)
    # CLI builds need imagewriter but not the GUI components
    set(SOURCES ${SOURCES_BASE} "imagewriter.cpp" "hwlistmodel.cpp" "oslistmodel.cpp")
else()
    set(SOURCES ${SOURCES_BASE} "networkaccessmanagerfactory.cpp" "qml.qrc" "nativefiledialog.cpp" "iconimageprovider.cpp")
endif()

if(BUILD_EMBEDDED)
    add_definitions(-DBUILD_EMBEDDED=1)
    set(SOURCES ${SOURCES} "embedded/device_info.cpp")
else()
    set(SOURCES ${SOURCES} "device_info.cpp")
endif()

# Find Qt components - different requirements for CLI-only vs GUI builds
if(BUILD_CLI_ONLY)
    # CLI-only build: need Core and Network for downloading, optionally DBus
    if(UNIX AND NOT APPLE)
        find_package(Qt6 6.9 COMPONENTS Core Network OPTIONAL_COMPONENTS LinguistTools DBus)
    else()
        find_package(Qt6 6.9 COMPONENTS Core Network OPTIONAL_COMPONENTS LinguistTools)
    endif()
else()
    # Regular GUI build: need Core, Quick, Svg, Network - DBus only needed on Linux
    if(UNIX AND NOT APPLE)
        find_package(Qt6 6.9 COMPONENTS Core Quick Svg Network OPTIONAL_COMPONENTS LinguistTools DBus)
    else()
        find_package(Qt6 6.9 COMPONENTS Core Quick Svg Network OPTIONAL_COMPONENTS LinguistTools)
    endif()
endif()
if (Qt6_FOUND)
    set(QT Qt6)
    if (APPLE)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11" CACHE STRING "" FORCE)
    endif()
else()
    message(FATAL_ERROR "Missing suitable Qt library (must be at least version 6.9)" )
endif()

if(${QT}DBus_FOUND AND UNIX AND NOT APPLE)
    set(DEPENDENCIES ${DEPENDENCIES} linux/udisks2api.cpp linux/udisks2api.h)
    set(EXTRALIBS ${EXTRALIBS} ${QT}::DBus)
    message("udisks2 support enabled")
endif()
if(${QT}WinExtras_FOUND)
    set(EXTRALIBS ${EXTRALIBS} ${QT}::WinExtras)
endif()

# Find all translation files automatically
file(GLOB TRANSLATIONS CONFIGURE_DEPENDS "i18n/*.ts")

# Embedded mode does not have support for Korean
if (BUILD_EMBEDDED)
    list(REMOVE_ITEM TRANSLATIONS "${CMAKE_CURRENT_SOURCE_DIR}/i18n/rpi-imager_ko.ts")
    list(REMOVE_ITEM TRANSLATIONS "${CMAKE_CURRENT_SOURCE_DIR}/i18n/rpi-imager_bn.ts")
    list(REMOVE_ITEM TRANSLATIONS "${CMAKE_CURRENT_SOURCE_DIR}/i18n/rpi-imager_ja.ts")
    list(REMOVE_ITEM TRANSLATIONS "${CMAKE_CURRENT_SOURCE_DIR}/i18n/rpi-imager_zh.ts")
    list(REMOVE_ITEM TRANSLATIONS "${CMAKE_CURRENT_SOURCE_DIR}/i18n/rpi-imager_zh-TW.ts")
endif()
qt_add_translation(QM_FILES ${TRANSLATIONS})

# Auto Generate translations.qrc
set(QRC_FILE_ENTRIES "")
foreach(qm_file IN LISTS QM_FILES)
    get_filename_component(qm_basename "${qm_file}" NAME)
    list(APPEND QRC_FILE_ENTRIES "        <file>${qm_basename}</file>")
endforeach()
string(JOIN "\n" QM_FILES_FOR_QRC ${QRC_FILE_ENTRIES})

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/i18n/translations.qrc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/translations.qrc"
    @ONLY
)

set(SOURCES ${SOURCES} "${CMAKE_CURRENT_BINARY_DIR}/translations.qrc" ${QM_FILES})

# Generate timezone list from IANA data
# To disable automatic timezone generation, set -DGENERATE_TIMEZONES_FROM_IANA=OFF
# To update timezone data version, change TZDATA_VERSION below
option(GENERATE_TIMEZONES_FROM_IANA "Generate timezone list from latest IANA data" ON)

if(GENERATE_TIMEZONES_FROM_IANA)
    set(TZDATA_VERSION "2025b") # Update this to get newer timezone data
    set(TZDATA_URL "https://data.iana.org/time-zones/releases/tzdata${TZDATA_VERSION}.tar.gz")

    # Reuse the helper to produce a generated qrc aliasing timezones.txt
    # Always create/update a placeholder for clean builds so rcc can parse generated qrc
    set(GENERATED_TIMEZONES_FILE_PLACEHOLDER "${CMAKE_CURRENT_BINARY_DIR}/timezones_generated.txt")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/timezones.txt")
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/timezones.txt"
            "${GENERATED_TIMEZONES_FILE_PLACEHOLDER}"
            COPYONLY
        )
    else()
        file(WRITE "${GENERATED_TIMEZONES_FILE_PLACEHOLDER}" "# placeholder\n")
    endif()
    add_generated_resource_with_fallback(GENERATED_TZ_QRC GENERATE_TIMEZONES_TGT timezones
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateTimezones.cmake
        timezones_generated.txt timezones.txt
        ${CMAKE_CURRENT_SOURCE_DIR}/timezones.txt
        EXTRA_CMAKE_ARGS 
            -DTZDATA_URL=${TZDATA_URL}
            -DTZDATA_DIR=${CMAKE_CURRENT_BINARY_DIR}/tzdata
    )
    list(APPEND SOURCES "${GENERATED_TZ_QRC}")
endif()

option(GENERATE_COUNTRIES_FROM_REGDB "Generate countries list from latest wireless-regdb" ON)

if(GENERATE_COUNTRIES_FROM_REGDB)
    set(REGDB_URL "https://git.kernel.org/pub/scm/linux/kernel/git/wens/wireless-regdb.git/plain/db.txt")
    # Always create/update a placeholder for clean builds so rcc can parse generated qrc
    set(GENERATED_COUNTRIES_FILE_PLACEHOLDER "${CMAKE_CURRENT_BINARY_DIR}/countries_generated.txt")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/countries.txt")
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/countries.txt"
            "${GENERATED_COUNTRIES_FILE_PLACEHOLDER}"
            COPYONLY
        )
    else()
        file(WRITE "${GENERATED_COUNTRIES_FILE_PLACEHOLDER}" "# placeholder\n")
    endif()
    add_generated_resource_with_fallback(GENERATED_COUNTRIES_QRC GENERATE_COUNTRIES_TGT countries
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateRegdbCountries.cmake
        countries_generated.txt countries.txt
        ${CMAKE_CURRENT_SOURCE_DIR}/countries.txt
        EXTRA_CMAKE_ARGS -DREGDB_URL=${REGDB_URL}
    )
    list(APPEND SOURCES "${GENERATED_COUNTRIES_QRC}")
endif()

option(GENERATE_CAPITAL_CITIES "Generate capital cities list from REST Countries API" ON)

if(GENERATE_CAPITAL_CITIES)
    set(REST_COUNTRIES_URL "https://restcountries.com/v3.1/all?fields=name,capital,cca2,timezones")
    # Always create/update a placeholder for clean builds so rcc can parse generated qrc
    set(GENERATED_CAPITAL_CITIES_FILE_PLACEHOLDER "${CMAKE_CURRENT_BINARY_DIR}/capital-cities_generated.txt")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/capital-cities.txt")
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/capital-cities.txt"
            "${GENERATED_CAPITAL_CITIES_FILE_PLACEHOLDER}"
            COPYONLY
        )
    else()
        file(WRITE "${GENERATED_CAPITAL_CITIES_FILE_PLACEHOLDER}" "# placeholder\n")
    endif()
    add_generated_resource_with_fallback(GENERATED_CAPITAL_CITIES_QRC GENERATE_CAPITAL_CITIES_TGT capital-cities
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateCapitalCities.cmake
        capital-cities_generated.txt capital-cities.txt
        ${CMAKE_CURRENT_SOURCE_DIR}/capital-cities.txt
        EXTRA_CMAKE_ARGS 
            -DAPI_URL=${REST_COUNTRIES_URL}
            -DKEYMAP_FILE=${CMAKE_CURRENT_SOURCE_DIR}/keymap-layouts.txt
    )
    list(APPEND SOURCES "${GENERATED_CAPITAL_CITIES_QRC}")
endif()
# Create the application target before tools reference it
if (WIN32)
    # Adding WIN32 prevents a console window being opened on Windows
    add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${DEPENDENCIES})
else()
    add_executable(${PROJECT_NAME} ${SOURCES} ${DEPENDENCIES})
endif()

# Create an rpi-imager_lupdate target
qt_add_lupdate(TS_FILES ${TRANSLATIONS} SOURCE_TARGETS ${PROJECT_NAME} OPTIONS -no-obsolete -locations none)

set_property(TARGET ${PROJECT_NAME} PROPERTY AUTOMOC ON)
set_property(TARGET ${PROJECT_NAME} PROPERTY AUTORCC ON)
set_property(TARGET ${PROJECT_NAME} PROPERTY AUTOUIC ON)

# Add timezone generation dependency if enabled
if(GENERATE_TIMEZONES_FROM_IANA AND DEFINED GENERATE_TIMEZONES_TGT)
    add_dependencies(${PROJECT_NAME} ${GENERATE_TIMEZONES_TGT})
    # Make sure autogen (rcc) also waits on timezone generation
    set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY AUTOGEN_TARGET_DEPENDS ${GENERATE_TIMEZONES_TGT})
endif()

# Ensure countries list is generated before building resources
if(GENERATE_COUNTRIES_FROM_REGDB AND DEFINED GENERATE_COUNTRIES_TGT)
    add_dependencies(${PROJECT_NAME} ${GENERATE_COUNTRIES_TGT})
    # Make sure autogen (rcc) also waits on countries generation
    set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY AUTOGEN_TARGET_DEPENDS ${GENERATE_COUNTRIES_TGT})
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

if(BUILD_CLI_ONLY)
    add_definitions(-DCLI_ONLY_BUILD)
endif()

# Qt policies only needed for GUI builds
if(NOT BUILD_CLI_ONLY)
    qt_policy(SET QTP0001 NEW)

    if (QT_KNOWN_POLICY_QTP0004)
        qt_policy(SET QTP0004 NEW)
    endif()

    set(QT_QML_GENERATE_QMLLS_INI ON)

    set(IMAGER_QML_FILES
        main.qml
        CommonStrings.qml
        Style.qml
        wizard/dialogs/ConfirmUnfilterDialog.qml
        wizard/dialogs/ConfirmSystemDriveDialog.qml
        wizard/dialogs/KeychainPermissionDialog.qml
        wizard/dialogs/UpdateAvailableDialog.qml
        wizard/dialogs/AppOptionsDialog.qml
        wizard/LanguageSelectionStep.qml
        qmlcomponents/BaseDialog.qml
        qmlcomponents/ImButton.qml
        qmlcomponents/ImButtonRed.qml
        qmlcomponents/ImCheckBox.qml
        qmlcomponents/ImCloseButton.qml
        qmlcomponents/ImComboBox.qml
        qmlcomponents/ImFileDialog.qml
        qmlcomponents/ImPopup.qml
        qmlcomponents/ImRadioButton.qml
        qmlcomponents/ImOptionPill.qml
        qmlcomponents/ImTextField.qml
        qmlcomponents/SelectionListView.qml
        qmlcomponents/OSSelectionListView.qml
        wizard/WizardStepBase.qml
        wizard/WizardContainer.qml
        wizard/DeviceSelectionStep.qml
        wizard/OSSelectionStep.qml
        wizard/StorageSelectionStep.qml
        wizard/HostnameCustomizationStep.qml
        wizard/IfAndFeaturesCustomizationStep.qml
        wizard/LocaleCustomizationStep.qml
        wizard/UserCustomizationStep.qml
        wizard/WifiCustomizationStep.qml
        wizard/RemoteAccessStep.qml
        wizard/PiConnectCustomizationStep.qml
        wizard/WritingStep.qml
        wizard/DoneStep.qml
        wizard/components/WizardSectionContainer.qml
        wizard/components/WizardFormLabel.qml
        wizard/components/WizardDescriptionText.qml
    )

    set_source_files_properties(Style.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
    set_source_files_properties(CommonStrings.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

    # C++ types exposed to QML
    set(IMAGER_QML_CPP_TYPES
        imagewriter.cpp
        hwlistmodel.cpp
        oslistmodel.cpp
        urlfmt.cpp
    )
endif()

# Only create QML module for GUI builds
if(NOT BUILD_CLI_ONLY)
    qt_add_qml_module(${PROJECT_NAME}
        URI RpiImager
        VERSION 1.0
        QML_FILES ${IMAGER_QML_FILES}
        SOURCES ${IMAGER_QML_CPP_TYPES}
        NO_CACHEGEN
        OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/RpiImager
        DEPENDENCIES QtQuick
        NO_PLUGIN
        QML_FILES
    )
endif()

# Because dependencies are typically not available by default on Windows, build bundled code
if (WIN32)
    include(windows/PlatformPackaging.cmake)
elseif(APPLE)
    include(mac/PlatformPackaging.cmake)
else()
    include(linux/PlatformPackaging.cmake)
endif()

add_dependencies(${PROJECT_NAME} zlibstatic yescrypt)
include_directories(${CURL_INCLUDE_DIR} ${LibArchive_INCLUDE_DIR} ${LIBLZMA_INCLUDE_DIRS} ${LIBDRM_INCLUDE_DIRS} ${ZLIB_INCLUDE_DIRS} ${ZSTD_INCLUDE_DIR} ${YESCRYPT_INCLUDE_DIR})

# Link different Qt components based on build type
if(BUILD_CLI_ONLY)
    # CLI-only build: link Core, Network and essential libraries
    target_link_libraries(${PROJECT_NAME} PRIVATE ${QT}::Core ${QT}::Network ${CURL_LIBRARIES} ${LibArchive_LIBRARIES} ${ZSTD_LIBRARIES} ${LIBLZMA_LIBRARIES} ${ZLIB_LIBRARIES} ${YESCRYPT_LIBRARIES} ${LIBDRM_LIBRARIES} ${ATOMIC_LIBRARY} ${EXTRALIBS})
else()
    # Regular GUI build: link all GUI components
    target_link_libraries(${PROJECT_NAME} PRIVATE ${QT}::Core ${QT}::Quick ${QT}::Svg ${QT}::Network ${CURL_LIBRARIES} ${LibArchive_LIBRARIES} ${ZSTD_LIBRARIES} ${LIBLZMA_LIBRARIES} ${ZLIB_LIBRARIES} ${YESCRYPT_LIBRARIES} ${LIBDRM_LIBRARIES} ${ATOMIC_LIBRARY} ${EXTRALIBS})
endif()
