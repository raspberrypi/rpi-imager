# SPDX-License-Identifier: Apache-2.0
# Copyright (C) 2026 Raspberry Pi Ltd
#
# SBOM component registry and build target; usage in doc/sbom.md.

include_guard(GLOBAL)

# A version pkg-config did not find arrives as an empty argument.
if(POLICY CMP0174)
    cmake_policy(SET CMP0174 NEW)
endif()

set(RPI_IMAGER_SBOM_DIR "${CMAKE_BINARY_DIR}/sbom")
# The CRA's vulnerability reporting point.
set(RPI_IMAGER_SBOM_SECURITY_CONTACT "mailto:security@raspberrypi.com" CACHE STRING
    "URL or mailto: for vulnerability reports, recorded in the SBOM")
set(RPI_IMAGER_SBOM_INPUTS "${RPI_IMAGER_SBOM_DIR}/sbom-inputs.cmake")
set(RPI_IMAGER_SBOM_GENERATOR "${CMAKE_CURRENT_LIST_DIR}/GenerateSbom.cmake")

set_property(GLOBAL PROPERTY RPI_IMAGER_SBOM_FIELDS
    NAME VERSION SUPPLIER DOWNLOAD_LOCATION VCS_URL GIT_TAG COMMIT LICENSE
    LICENSE_TEXT PURL CPE TYPE DESCRIPTION ORIGIN RELATION USED_BY FILE_NAME)

# Only a checkout rooted at <dir>: a plain vendored tree would otherwise
# report rpi-imager's own commit.
function(_rpi_imager_sbom_git_commit out dir)
    set(${out} "" PARENT_SCOPE)
    if(NOT GIT_EXECUTABLE OR NOT IS_DIRECTORY "${dir}")
        return()
    endif()
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${dir}" rev-parse --show-prefix HEAD
        RESULT_VARIABLE _rc
        OUTPUT_VARIABLE _rev
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(_rc EQUAL 0 AND _rev MATCHES "^[\r\n]*([0-9a-f]+)$")
        set(${out} "${CMAKE_MATCH_1}" PARENT_SCOPE)
    endif()
endfunction()

# Identifiers for a dynamically linked system library, as arguments for
# rpi_imager_sbom_add_component: "PURL <purl>" for the distro package that
# provides the first of <libs> when dpkg knows it, else "CPE <cpe>". A distro
# package carries backported fixes, so its upstream CPE would draw false
# matches; the purl's distro qualifier lets scanners use the distro's own
# security data. The same purl from the bundled-library manifest merges with
# this component in GenerateSbom.cmake.
function(rpi_imager_sbom_system_ids out libs cpe)
    set(${out} CPE "${cpe}" PARENT_SCOPE)
    find_program(RPI_IMAGER_DPKG_QUERY dpkg-query)
    mark_as_advanced(RPI_IMAGER_DPKG_QUERY)
    if(NOT RPI_IMAGER_DPKG_QUERY)
        return()
    endif()
    set(_lib "")
    foreach(_l IN LISTS libs)
        if(IS_ABSOLUTE "${_l}" AND EXISTS "${_l}")
            set(_lib "${_l}")
            break()
        endif()
    endforeach()
    if(NOT _lib)
        return()
    endif()
    file(REAL_PATH "${_lib}" _real)
    # dpkg records whichever side of the /usr merge the package shipped.
    if(_real MATCHES "^/usr(/.*)$")
        set(_paths "${_real}" "${CMAKE_MATCH_1}")
    else()
        set(_paths "${_real}" "/usr${_real}")
    endif()
    set(_owner "")
    foreach(_p IN LISTS _paths)
        execute_process(COMMAND "${RPI_IMAGER_DPKG_QUERY}" -S "${_p}"
            RESULT_VARIABLE _rc OUTPUT_VARIABLE _o ERROR_QUIET)
        if(_rc EQUAL 0 AND _o MATCHES "^([^ ,]+): /")
            set(_owner "${CMAKE_MATCH_1}")
            break()
        endif()
    endforeach()
    if(NOT _owner)
        return()
    endif()
    execute_process(COMMAND "${RPI_IMAGER_DPKG_QUERY}" -W
            "-f=\${Package}\t\${Version}\t\${Architecture}" "${_owner}"
        RESULT_VARIABLE _rc OUTPUT_VARIABLE _o ERROR_QUIET)
    if(NOT _rc EQUAL 0 OR NOT _o MATCHES "^([^\t]+)\t([^\t]+)\t([^\t\n]+)")
        return()
    endif()
    set(_pkg "${CMAKE_MATCH_1}")
    set(_ver "${CMAKE_MATCH_2}")
    set(_arch "${CMAKE_MATCH_3}")
    _rpi_imager_sbom_os_release(_id _version_id)
    string(REPLACE "%" "%25" _ver "${_ver}")
    string(REPLACE ":" "%3A" _ver "${_ver}")
    string(REPLACE "+" "%2B" _ver "${_ver}")
    set(_purl "pkg:deb/${_id}/${_pkg}@${_ver}?arch=${_arch}")
    if(_version_id)
        string(APPEND _purl "&distro=${_id}-${_version_id}")
    endif()
    set(${out} PURL "${_purl}" PARENT_SCOPE)
endfunction()

function(_rpi_imager_sbom_os_release out_id out_version_id)
    set(_id debian)
    set(_vid "")
    if(EXISTS /etc/os-release)
        file(STRINGS /etc/os-release _lines)
        foreach(_line IN LISTS _lines)
            if(_line MATCHES "^ID=\"?([^\"]*)\"?$")
                set(_id "${CMAKE_MATCH_1}")
            elseif(_line MATCHES "^VERSION_ID=\"?([^\"]*)\"?$")
                set(_vid "${CMAKE_MATCH_1}")
            endif()
        endforeach()
    endif()
    set(${out_id} "${_id}" PARENT_SCOPE)
    set(${out_version_id} "${_vid}" PARENT_SCOPE)
endfunction()

function(rpi_imager_sbom_add_component id)
    cmake_parse_arguments(PARSE_ARGV 1 _a ""
        "NAME;VERSION;SUPPLIER;DOWNLOAD_LOCATION;VCS_URL;GIT_TAG;COMMIT;SOURCE_DIR;LICENSE;LICENSE_TEXT;PURL;CPE;TYPE;DESCRIPTION;ORIGIN;RELATION;FILE_NAME"
        "USED_BY")
    if(_a_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "rpi_imager_sbom_add_component(${id}): unexpected arguments: ${_a_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT id MATCHES "^[A-Za-z0-9.-]+$")
        message(FATAL_ERROR "rpi_imager_sbom_add_component: id '${id}' may use only letters, digits, '.' and '-'")
    endif()

    if(NOT _a_NAME)
        set(_a_NAME "${id}")
    endif()
    if(NOT _a_TYPE)
        set(_a_TYPE library)
    endif()
    if(NOT _a_LICENSE)
        set(_a_LICENSE NOASSERTION)
    endif()
    if(NOT _a_RELATION)
        set(_a_RELATION DEPENDS_ON)
    endif()
    if(NOT _a_USED_BY)
        set(_a_USED_BY rpi-imager)
    endif()
    if(NOT _a_COMMIT AND _a_SOURCE_DIR)
        _rpi_imager_sbom_git_commit(_a_COMMIT "${_a_SOURCE_DIR}")
    endif()
    if(NOT _a_ORIGIN)
        get_property(_a_ORIGIN GLOBAL PROPERTY RPI_IMAGER_SBOM_FETCH_ORIGIN_${id})
    endif()

    set_property(GLOBAL APPEND PROPERTY RPI_IMAGER_SBOM_COMPONENTS ${id})
    get_property(_fields GLOBAL PROPERTY RPI_IMAGER_SBOM_FIELDS)
    foreach(_f IN LISTS _fields)
        set_property(GLOBAL PROPERTY RPI_IMAGER_SBOM_${id}_${_f} "${_a_${_f}}")
    endforeach()
endfunction()

function(rpi_imager_sbom_add_qt)
    foreach(_m IN LISTS ARGN)
        if(_m MATCHES "^(Qml|Quick)$")
            set(_repo qtdeclarative)
        elseif(_m STREQUAL "Svg")
            set(_repo qtsvg)
        else()
            set(_repo qtbase)
        endif()
        string(TOLOWER "${_m}" _lower)
        rpi_imager_sbom_add_component(qt6-${_lower}
            NAME "Qt6${_m}"
            VERSION "${Qt6_VERSION}"
            SUPPLIER "The Qt Company"
            DOWNLOAD_LOCATION "https://download.qt.io/official_releases/qt/"
            VCS_URL "https://code.qt.io/qt/${_repo}.git"
            GIT_TAG "v${Qt6_VERSION}"
            LICENSE "LGPL-3.0-only"
            PURL "pkg:github/qt/${_repo}@v${Qt6_VERSION}"
            CPE "cpe:2.3:a:qt:qt:${Qt6_VERSION}:*:*:*:*:*:*:*"
            TYPE framework
            ORIGIN "Qt installation at configure time"
            USED_BY rpi-imager)
    endforeach()
endfunction()

# <os>[-embedded]-<arch>; the CLI differs by product name instead.
# The arch is the compiler's target, not the host's: 32-bit Pi OS runs a
# 64-bit kernel, so CMAKE_SYSTEM_PROCESSOR says aarch64 for an armhf build.
function(_rpi_imager_sbom_platform out)
    if(APPLE)
        set(_os macos)
        list(LENGTH CMAKE_OSX_ARCHITECTURES _n)
        if(_n GREATER 1)
            set(_arch universal)
        elseif(_n EQUAL 1)
            set(_arch "${CMAKE_OSX_ARCHITECTURES}")
        else()
            set(_arch "${CMAKE_SYSTEM_PROCESSOR}")
        endif()
    else()
        if(WIN32)
            set(_os windows)
        else()
            set(_os linux)
        endif()
        set(_arch "${CMAKE_SYSTEM_PROCESSOR}")
        foreach(_cand CMAKE_LIBRARY_ARCHITECTURE CMAKE_CXX_COMPILER_ARCHITECTURE_ID)
            if(${_cand})
                set(_arch "${${_cand}}")
                break()
            endif()
        endforeach()
        string(TOLOWER "${_arch}" _arch)
        if(_arch MATCHES "^(x86_64|amd64|x64)")
            set(_arch x86_64)
        elseif(_arch MATCHES "^(aarch64|arm64)" AND NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
            set(_arch aarch64)
        elseif(_arch MATCHES "^(arm|aarch64)")
            set(_arch armhf)
        endif()
    endif()
    if(BUILD_EMBEDDED)
        string(APPEND _os "-embedded")
    endif()
    set(${out} "${_os}-${_arch}" PARENT_SCOPE)
endfunction()

function(_rpi_imager_sbom_setting key value)
    set_property(GLOBAL APPEND PROPERTY RPI_IMAGER_SBOM_SETTINGS ${key})
    set_property(GLOBAL PROPERTY RPI_IMAGER_SBOM_SETTING_${key} "${value}")
    if("${ARGN}" STREQUAL "OVERRIDABLE")
        set_property(GLOBAL PROPERTY RPI_IMAGER_SBOM_SETTING_${key}_OVERRIDABLE TRUE)
    endif()
endfunction()

function(rpi_imager_sbom_add_target app)
    get_target_property(_app_name ${app} OUTPUT_NAME)
    if(NOT _app_name)
        set(_app_name "${app}")
    endif()
    set(_imager_common
        VERSION "@IMAGER_VERSION@"
        SUPPLIER "Raspberry Pi Ltd"
        DOWNLOAD_LOCATION "https://github.com/raspberrypi/rpi-imager"
        VCS_URL "https://github.com/raspberrypi/rpi-imager.git"
        LICENSE "Apache-2.0"
        PURL "pkg:github/raspberrypi/rpi-imager@@IMAGER_VERSION@"
        TYPE application)
    rpi_imager_sbom_add_component(rpi-imager NAME "${_app_name}" ${_imager_common}
        FILE_NAME "${_app_name}${CMAKE_EXECUTABLE_SUFFIX}")
    set(_deps ${app})
    foreach(_t rpi-imager-writer rpi-imager-callback-relay)
        if(TARGET ${_t})
            get_target_property(_t_name ${_t} OUTPUT_NAME)
            if(NOT _t_name)
                set(_t_name "${_t}")
            endif()
            rpi_imager_sbom_add_component(${_t} ${_imager_common} RELATION CONTAINS
                FILE_NAME "${_t_name}${CMAKE_EXECUTABLE_SUFFIX}")
            list(APPEND _deps ${_t})
        endif()
    endforeach()

    # macdeployqt and windeployqt run after the link, so the deployed tree is
    # scanned once the app target is complete.
    set(_hash_root "")
    set(_scan_root "")
    set(_scan_dirs "")
    set(_copy_dirs "")
    if(APPLE AND NOT BUILD_CLI_ONLY)
        set(_hash_root "${CMAKE_BINARY_DIR}/${app}.app")
        set(_scan_root "${_hash_root}/Contents")
        set(_scan_dirs Frameworks PlugIns Resources/qml)
        set(_copy_dirs "${_scan_root}/Resources/sbom")
    elseif(WIN32)
        set(_hash_root "${CMAKE_BINARY_DIR}/deploy")
        set(_scan_root "${_hash_root}")
        set(_scan_dirs .)
        set(_copy_dirs "${_scan_root}/sbom")
    endif()

    _rpi_imager_sbom_platform(_platform)
    _rpi_imager_sbom_setting(SBOM_PLATFORM "${_platform}")
    _rpi_imager_sbom_setting(SBOM_ROOT rpi-imager)
    _rpi_imager_sbom_setting(SBOM_VERSION_VARS "${IMAGER_VERSION_VARS}")
    _rpi_imager_sbom_setting(SBOM_SOURCE_DIR "${CMAKE_SOURCE_DIR}")
    _rpi_imager_sbom_setting(SBOM_GIT_EXECUTABLE "${GIT_EXECUTABLE}")
    _rpi_imager_sbom_setting(SBOM_QT_VERSION "${Qt6_VERSION}")
    _rpi_imager_sbom_setting(SBOM_CXX_COMPILER_ID "${CMAKE_CXX_COMPILER_ID}")
    _rpi_imager_sbom_setting(SBOM_CXX_COMPILER_VERSION "${CMAKE_CXX_COMPILER_VERSION}")
    _rpi_imager_sbom_setting(SBOM_SECURITY_CONTACT "${RPI_IMAGER_SBOM_SECURITY_CONTACT}")
    if(NOT RPI_IMAGER_SBOM_SECURITY_CONTACT)
        message(WARNING "RPI_IMAGER_SBOM_SECURITY_CONTACT is unset; the SBOM will name no security contact")
    endif()
    # Linux packaging overrides these with -D for a staged tree.
    _rpi_imager_sbom_setting(SBOM_OUTPUT_DIR "${RPI_IMAGER_SBOM_DIR}" OVERRIDABLE)
    _rpi_imager_sbom_setting(SBOM_SCAN_ROOT "${_scan_root}" OVERRIDABLE)
    _rpi_imager_sbom_setting(SBOM_SCAN_DIRS "${_scan_dirs}" OVERRIDABLE)
    _rpi_imager_sbom_setting(SBOM_COPY_DIRS "${_copy_dirs}" OVERRIDABLE)
    _rpi_imager_sbom_setting(SBOM_BUNDLE_MANIFEST "" OVERRIDABLE)
    # Packaging re-runs the generator once signing is done; see doc/sbom.md.
    _rpi_imager_sbom_setting(SBOM_HASH_ROOT "${_hash_root}" OVERRIDABLE)
    _rpi_imager_sbom_setting(SBOM_HASH_EXCLUDE "" OVERRIDABLE)
    _rpi_imager_sbom_setting(SBOM_ARTEFACT "" OVERRIDABLE)
    _rpi_imager_sbom_setting(SBOM_OUTPUT_STEM "" OVERRIDABLE)
    _rpi_imager_sbom_setting(SBOM_LIFECYCLE build OVERRIDABLE)

    set(_stamp "${RPI_IMAGER_SBOM_DIR}/sbom.stamp")
    add_custom_command(
        OUTPUT "${_stamp}"
        COMMAND ${CMAKE_COMMAND}
            -DSBOM_INPUTS=${RPI_IMAGER_SBOM_INPUTS}
            -DSBOM_STAMP=${_stamp}
            -P ${RPI_IMAGER_SBOM_GENERATOR}
        DEPENDS
            ${RPI_IMAGER_SBOM_INPUTS}
            ${RPI_IMAGER_SBOM_GENERATOR}
            ${IMAGER_VERSION_VARS}
            ${_deps}
        COMMENT "Generating SPDX and CycloneDX SBOMs"
        VERBATIM
    )
    add_custom_target(rpi_imager_sbom ALL DEPENDS "${_stamp}")
    add_dependencies(rpi_imager_sbom generate_version)
endfunction()

function(_rpi_imager_sbom_write_inputs)
    get_property(_settings GLOBAL PROPERTY RPI_IMAGER_SBOM_SETTINGS)
    if(NOT _settings)
        return()
    endif()
    get_property(_ids GLOBAL PROPERTY RPI_IMAGER_SBOM_COMPONENTS)
    list(REMOVE_DUPLICATES _ids)
    list(SORT _ids)
    get_property(_fields GLOBAL PROPERTY RPI_IMAGER_SBOM_FIELDS)

    # Bracket arguments keep values literal.
    set(_c "# Generated by src/cmake/sbom.cmake; do not edit.\n")
    foreach(_k IN LISTS _settings)
        get_property(_v GLOBAL PROPERTY RPI_IMAGER_SBOM_SETTING_${_k})
        get_property(_o GLOBAL PROPERTY RPI_IMAGER_SBOM_SETTING_${_k}_OVERRIDABLE)
        if(_o)
            string(APPEND _c "if(NOT DEFINED ${_k})\n  set(${_k} [==[${_v}]==])\nendif()\n")
        else()
            string(APPEND _c "set(${_k} [==[${_v}]==])\n")
        endif()
    endforeach()
    string(APPEND _c "set(SBOM_COMPONENTS [==[${_ids}]==])\n")
    foreach(_id IN LISTS _ids)
        foreach(_f IN LISTS _fields)
            get_property(_v GLOBAL PROPERTY RPI_IMAGER_SBOM_${_id}_${_f})
            string(APPEND _c "set(SBOM_${_id}_${_f} [==[${_v}]==])\n")
        endforeach()
    endforeach()

    # Unchanged content keeps its mtime, so nothing regenerates.
    if(EXISTS "${RPI_IMAGER_SBOM_INPUTS}")
        file(READ "${RPI_IMAGER_SBOM_INPUTS}" _old)
        if(_old STREQUAL _c)
            return()
        endif()
    endif()
    file(WRITE "${RPI_IMAGER_SBOM_INPUTS}" "${_c}")
endfunction()

# Deferred so registrations from anywhere in configure are included.
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL _rpi_imager_sbom_write_inputs)
