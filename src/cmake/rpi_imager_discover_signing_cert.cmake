# SPDX-License-Identifier: Apache-2.0
# Copyright (C) 2026 Raspberry Pi Ltd
#
# Discover Windows Authenticode code-signing certificates in the local store
# whose Organization matches the publisher org used for peer auth.

function(rpi_imager_discover_windows_signing_cert publisher_org out_thumbprints_var out_primary_var)
    set(${out_thumbprints_var} "" PARENT_SCOPE)
    set(${out_primary_var} "" PARENT_SCOPE)

    if(NOT CMAKE_HOST_WIN32)
        return()
    endif()

    set(_ps_script "${CMAKE_CURRENT_LIST_DIR}/../windows/discover_signing_cert_thumbprints.ps1")
    if(NOT EXISTS "${_ps_script}")
        message(WARNING "Windows signing cert discovery script not found: ${_ps_script}")
        return()
    endif()

    execute_process(
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${_ps_script}"
                -PublisherOrg "${publisher_org}"
        OUTPUT_VARIABLE _discover_output
        ERROR_VARIABLE _discover_error
        RESULT_VARIABLE _discover_rc
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT _discover_rc EQUAL 0)
        message(WARNING
            "Windows signing cert discovery failed (exit ${_discover_rc}): ${_discover_error}")
        return()
    endif()

    set(_thumbprints "")
    set(_primary "")
    foreach(_line IN LISTS _discover_output)
        if(_line MATCHES "^THUMBPRINTS=(.*)$")
            set(_thumbprints "${CMAKE_MATCH_1}")
        elseif(_line MATCHES "^PRIMARY=(.*)$")
            set(_primary "${CMAKE_MATCH_1}")
        endif()
    endforeach()

    set(${out_thumbprints_var} "${_thumbprints}" PARENT_SCOPE)
    set(${out_primary_var} "${_primary}" PARENT_SCOPE)
endfunction()
