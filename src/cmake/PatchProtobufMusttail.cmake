# SPDX-License-Identifier: Apache-2.0
# Copyright (C) 2026 Raspberry Pi Ltd
#
# Run as protobuf's FetchContent PATCH_COMMAND, in its source directory.
#
# GCC 15 honours [[clang::musttail]], and on Win64 some of protobuf's tail
# calls need more stack slots than their caller has, which GCC makes a hard
# error. Neither GCC nor protobuf offers a switch, so GCC on Windows is
# excluded from the musttail condition. Only the fetched copy is edited.

set(_file "src/google/protobuf/port_def.inc")
set(_anchor "(defined(__aarch64__) || defined(__x86_64__) || defined(_M_X64))")
set(_guard "!(defined(_WIN32) && defined(__GNUC__) && !defined(__clang__))")

file(READ "${_file}" _text)
string(FIND "${_text}" "${_guard}" _done)
if(NOT _done EQUAL -1)
    return()
endif()
string(FIND "${_text}" "${_anchor}" _at)
if(_at EQUAL -1)
    message(FATAL_ERROR "PatchProtobufMusttail: '${_anchor}' not in ${_file}; "
                        "check whether this protobuf still needs the patch")
endif()

set(_eol "\n")
string(FIND "${_text}" "\r\n" _crlf)
if(NOT _crlf EQUAL -1)
    set(_eol "\r\n")
endif()
string(REPLACE "${_anchor}" "${_anchor} && \\${_eol}    ${_guard}" _text "${_text}")
file(WRITE "${_file}" "${_text}")
