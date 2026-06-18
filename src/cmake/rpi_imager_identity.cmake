# SPDX-License-Identifier: Apache-2.0
# Copyright (C) 2026 Raspberry Pi Ltd
#
# Single source of truth for shipping identity strings used in:
#   - rpi_imager_identity.h (C++ peer auth, packaging checks, …)
#   - windows/rpi-imager.rc.in (VERSIONINFO CompanyName)
#   - windows/rpi-imager.iss.in (Inno Setup publisher)
#
# Downstream FOSS developers testing the Windows privileged helper locally
# override RPI_IMAGER_PUBLISHER_ORG at CMake configure time to match a
# self-signed code-signing cert; thumbprints are auto-discovered from the
# Windows cert store when RPI_IMAGER_AUTO_TRUST_SIGNING_CERT is ON (default).
# See doc/windows-privileged-helper-dev.md.

set(RPI_IMAGER_PUBLISHER_ORG "Raspberry Pi Ltd" CACHE STRING
    "Authenticode publisher Organization (O=) pinned by the Windows helper (§14.4)")
set(RPI_IMAGER_WINDOWS_CLIENT_EXE "rpi-imager.exe" CACHE STRING
    "Client executable basename accepted by the Windows helper peer auth")
set(RPI_IMAGER_TRUSTED_SIGNER_THUMBPRINTS "" CACHE STRING
    "Extra SHA-1 hex thumbprints accepted by Windows helper peer auth (§14.4), in addition to any auto-discovered signing cert. Semicolon-separated.")
set(IMAGER_SIGNING_CERT_SHA1 "" CACHE STRING
    "SHA-1 thumbprint passed to signtool (/sha1). Auto-discovered on Windows when empty and RPI_IMAGER_AUTO_TRUST_SIGNING_CERT is ON.")
option(RPI_IMAGER_AUTO_TRUST_SIGNING_CERT
    "On Windows hosts, discover code-signing cert thumbprints from the cert store at configure time (matched by RPI_IMAGER_PUBLISHER_ORG)"
    ON)

set(_rpi_discovered_thumbs "")
set(_rpi_discovered_primary "")
if(RPI_IMAGER_AUTO_TRUST_SIGNING_CERT AND CMAKE_HOST_WIN32)
    include(${CMAKE_CURRENT_LIST_DIR}/rpi_imager_discover_signing_cert.cmake)
    rpi_imager_discover_windows_signing_cert(
        "${RPI_IMAGER_PUBLISHER_ORG}"
        _rpi_discovered_thumbs
        _rpi_discovered_primary)
    if(_rpi_discovered_thumbs)
        message(STATUS
            "Auto-discovered Windows code-signing thumbprint(s) for "
            "'${RPI_IMAGER_PUBLISHER_ORG}': ${_rpi_discovered_thumbs}")
    endif()
    if(_rpi_discovered_primary AND IMAGER_SIGNING_CERT_SHA1 STREQUAL "")
        set(IMAGER_SIGNING_CERT_SHA1 "${_rpi_discovered_primary}" CACHE STRING
            "SHA-1 thumbprint passed to signtool (/sha1). Auto-discovered on Windows when empty and RPI_IMAGER_AUTO_TRUST_SIGNING_CERT is ON.")
        message(STATUS "Auto-selected IMAGER_SIGNING_CERT_SHA1=${IMAGER_SIGNING_CERT_SHA1}")
    endif()
endif()

# Merge manual and auto-discovered thumbprints (deduplicated later).
set(_rpi_all_thumbs "${RPI_IMAGER_TRUSTED_SIGNER_THUMBPRINTS}")
if(_rpi_discovered_thumbs)
    if(_rpi_all_thumbs STREQUAL "")
        set(_rpi_all_thumbs "${_rpi_discovered_thumbs}")
    else()
        set(_rpi_all_thumbs "${_rpi_all_thumbs};${_rpi_discovered_thumbs}")
    endif()
endif()
if(IMAGER_SIGNING_CERT_SHA1 AND NOT IMAGER_SIGNING_CERT_SHA1 STREQUAL "")
    if(_rpi_all_thumbs STREQUAL "")
        set(_rpi_all_thumbs "${IMAGER_SIGNING_CERT_SHA1}")
    else()
        set(_rpi_all_thumbs "${_rpi_all_thumbs};${IMAGER_SIGNING_CERT_SHA1}")
    endif()
endif()

# Normalize thumbprint list for rpi_imager_identity.h (40-char uppercase hex).
string(REPLACE "," ";" _rpi_thumb_str "${_rpi_all_thumbs}")
string(REGEX REPLACE "[ \t]" "" _rpi_thumb_str "${_rpi_thumb_str}")
set(_rpi_thumb_list ${_rpi_thumb_str})
set(_rpi_thumb_entries "")
set(RPI_IMAGER_TRUSTED_SIGNER_THUMBPRINT_COUNT 0)
set(_rpi_thumb_seen "")
if(NOT _rpi_thumb_str STREQUAL "")
    foreach(_rpi_tp IN LISTS _rpi_thumb_list)
        if(_rpi_tp STREQUAL "")
            continue()
        endif()
        string(TOUPPER "${_rpi_tp}" _rpi_tp)
        list(FIND _rpi_thumb_seen "${_rpi_tp}" _rpi_tp_seen)
        if(NOT _rpi_tp_seen EQUAL -1)
            continue()
        endif()
        string(LENGTH "${_rpi_tp}" _rpi_tp_len)
        if(NOT _rpi_tp_len EQUAL 40 OR NOT _rpi_tp MATCHES "^[0-9A-F]+$")
            message(WARNING
                "Trusted signer thumbprint: ignoring invalid entry "
                "'${_rpi_tp}' (expected 40 hex characters)")
            continue()
        endif()
        list(APPEND _rpi_thumb_seen "${_rpi_tp}")
        string(APPEND _rpi_thumb_entries "    L\"${_rpi_tp}\",\n")
        math(EXPR RPI_IMAGER_TRUSTED_SIGNER_THUMBPRINT_COUNT
            "${RPI_IMAGER_TRUSTED_SIGNER_THUMBPRINT_COUNT} + 1")
    endforeach()
endif()
if(RPI_IMAGER_TRUSTED_SIGNER_THUMBPRINT_COUNT GREATER 0)
    set(RPI_IMAGER_TRUSTED_SIGNER_THUMBPRINT_ARRAY
        "inline constexpr wchar_t kTrustedSignerThumbprints[][41] = {\n${_rpi_thumb_entries}};")
else()
    set(RPI_IMAGER_TRUSTED_SIGNER_THUMBPRINT_ARRAY
        "inline constexpr wchar_t kTrustedSignerThumbprints[][41] = {};")
endif()

if(CMAKE_HOST_WIN32 AND RPI_IMAGER_ENABLE_WINDOWS_HELPER
   AND RPI_IMAGER_TRUSTED_SIGNER_THUMBPRINT_COUNT EQUAL 0)
    message(WARNING
        "Windows helper peer auth has no trusted signer thumbprints configured. "
        "Install a code-signing cert whose O= matches RPI_IMAGER_PUBLISHER_ORG, "
        "set RPI_IMAGER_TRUSTED_SIGNER_THUMBPRINTS, or enable "
        "RPI_IMAGER_AUTO_TRUST_SIGNING_CERT.")
endif()
