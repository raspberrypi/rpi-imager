# SPDX-License-Identifier: Apache-2.0
# Copyright (C) 2026 Raspberry Pi Ltd
#
# Privileged I/O Platform Abstraction Layer.
#
# Builds a static library `privileged_io` that gets linked into:
#   - the unprivileged client (rpi-imager)
#   - the privileged helper (rpi-imager-writer, future)
#   - test executables
#
# The library is intentionally Qt-free (per design doc §8a) so that the
# helper-side use case can be ported to Rust without dragging Qt out of
# the codebase first.

# -----------------------------------------------------------------------
# Protobuf
# -----------------------------------------------------------------------

# Vendor protobuf via FetchContent, matching the project convention for
# all major C/C++ dependencies (libcurl, libarchive, zstd, xz, zlib).
#
# Why vendor rather than rely on a system install:
#   * macOS ships universal (arm64+x86_64) binaries. Homebrew only
#     installs protobuf for the host arch, so a universal link fails
#     to find the cross-arch libraries.
#   * Pinning the version isolates us from upstream protobuf API churn.
#   * Matches the pattern documented in CLAUDE.md.
#
# We pin to v21.12 - the last protobuf release that does NOT require
# Abseil. Pulling in Abseil's full CMake tree across two architectures
# would dramatically inflate build time and binary size for what is
# (in our use case) a small structured-message serialisation library.
# Our .proto schema uses no features beyond proto3 basics, so 21.12 is
# sufficient.
include(FetchContent)

set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(protobuf_INSTALL OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_PROTOC_BINARIES ON CACHE BOOL "" FORCE)
set(protobuf_BUILD_LIBPROTOC ON CACHE BOOL "" FORCE)
set(protobuf_MSVC_STATIC_RUNTIME OFF CACHE BOOL "" FORCE)
set(protobuf_DISABLE_RTTI OFF CACHE BOOL "" FORCE)

# Minor warning hygiene: protobuf's generated code triggers strict
# warnings; we suppress at the consumer level (target_compile_options
# below) rather than disabling the project-wide flags here.

FetchContent_Declare(
    protobuf
    GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
    GIT_TAG v21.12
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(protobuf)

# Provide a clean alias for the runtime library, matching the namespace
# used by modern config-mode protobuf (so call sites can write
# `protobuf::libprotobuf` regardless of how protobuf was acquired).
if(NOT TARGET protobuf::libprotobuf)
    add_library(protobuf::libprotobuf ALIAS libprotobuf)
endif()
if(NOT TARGET protobuf::protoc)
    add_executable(protobuf::protoc ALIAS protoc)
endif()

set(PROTO_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/proto/imager.proto
)

# Run protoc as a custom command. Protobuf 21.x doesn't expose the
# `protobuf_generate` macro that 22+ provides via its config-mode
# package, but the `protoc` executable target is reachable directly,
# so we drive code generation explicitly. This pattern works across
# every protobuf version we'd realistically pin to.
set(PROTO_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(PROTO_GENERATED_HDR ${PROTO_GENERATED_DIR}/proto/imager.pb.h)
set(PROTO_GENERATED_SRC ${PROTO_GENERATED_DIR}/proto/imager.pb.cc)

add_custom_command(
    OUTPUT  ${PROTO_GENERATED_HDR} ${PROTO_GENERATED_SRC}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PROTO_GENERATED_DIR}/proto
    COMMAND $<TARGET_FILE:protoc>
            --cpp_out=${PROTO_GENERATED_DIR}
            --proto_path=${CMAKE_CURRENT_SOURCE_DIR}
            ${PROTO_FILES}
    DEPENDS protoc ${PROTO_FILES}
    COMMENT "Generating C++ from imager.proto"
    VERBATIM
)

# -----------------------------------------------------------------------
# Library target
# -----------------------------------------------------------------------

set(PRIVILEGED_IO_SOURCES
    privileged_writer.h
    factory.cpp
    backends/in_process_test.h
    backends/in_process_test.cpp
    backends/fault_injecting.h
    backends/fault_injecting.cpp
    # Named-pipe transport framing (§14.3). Qt-free and platform-agnostic;
    # used by the Windows backend and shared with the helper.
    wire/frame.h
    wire/frame.cpp
    ${PROTO_GENERATED_SRC}
    ${PROTO_GENERATED_HDR}
)

# Native Windows privileged backend (§14). Omitted when
# RPI_IMAGER_DISABLE_WINDOWS_HELPER is set; otherwise Windows uses the
# LocalShimBackend when RPI_IMAGER_USE_WINDOWS_HELPER is unset.
if(WIN32 AND NOT RPI_IMAGER_DISABLE_WINDOWS_HELPER)
    list(APPEND PRIVILEGED_IO_SOURCES
        backends/windows_uac.h
        backends/windows_uac.cpp
        wire/win_shared_memory.h
        wire/win_shared_memory.cpp
        wire/drive_descriptor_json.h
        wire/drive_descriptor_json.cpp
        wire/duplex_connection.h
        wire/duplex_connection.cpp
        wire/server_message.h
        wire/server_message.cpp
        wire/protocol.h
    )
endif()

# Linux backends (phase 2). LinuxEmbedded is always built on Linux (root
# in-process path). LinuxPolkit + socket transport are omitted when
# RPI_IMAGER_DISABLE_LINUX_HELPER is set.
if(UNIX AND NOT APPLE)
    list(APPEND PRIVILEGED_IO_SOURCES
        backends/linux_embedded.h
        backends/linux_embedded.cpp
        wire/linux_shared_memory.h
        wire/linux_shared_memory.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../linux/helper_maintenance.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../dependencies/yescrypt/sha256.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../dependencies/yescrypt/sha256.h
    )
    if(NOT RPI_IMAGER_DISABLE_LINUX_HELPER)
        list(APPEND PRIVILEGED_IO_SOURCES
            backends/linux_polkit.h
            backends/linux_polkit.cpp
            wire/linux_socket_io.h
            wire/linux_socket_io.cpp
            wire/drive_descriptor_json.h
            wire/drive_descriptor_json.cpp
            wire/duplex_connection.h
            wire/duplex_connection.cpp
            wire/server_message.h
            wire/server_message.cpp
            wire/protocol.h
        )
    endif()
endif()

if(APPLE)
    list(APPEND PRIVILEGED_IO_SOURCES
        backends/macos_xpc.h
        backends/macos_xpc.mm
        # The protocol header + constants are shared with the helper
        # binary (under src/writer/). Compiling the .m file into both
        # targets gives each process its own copy of the constant; the
        # alternative would be a third tiny shared library. Two
        # compilations of a one-line .m is the cheaper option.
        ../writer/server/mac/RpiImagerWriterProtocol.h
        ../writer/server/mac/RpiImagerWriterProtocol.m
    )
endif()

add_library(privileged_io STATIC ${PRIVILEGED_IO_SOURCES})

if(APPLE)
    # The XPC backend uses NSXPC (Foundation) and SMAppService for
    # helper installation. ServiceManagement (the framework that
    # provides SMAppService on macOS 13+) lives alongside Foundation;
    # both are platform-native, not Qt - this does not violate §8a.
    target_link_libraries(privileged_io PUBLIC
        "-framework Foundation"
        "-framework ServiceManagement"
    )
    target_compile_options(privileged_io PRIVATE -fobjc-arc)
    target_include_directories(privileged_io PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    )
endif()

# Generated protobuf headers live in the binary dir; consumers need both
# the source dir (for privileged_writer.h) and the binary dir (for
# proto/imager.pb.h).
target_include_directories(privileged_io
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(privileged_io
    PUBLIC
        protobuf::libprotobuf
)

# The Windows backend launches the elevated helper (ShellExecuteExW -> shell32)
# and mints a unique pipe-name GUID (CoCreateGuid/StringFromGUID2 -> ole32).
#
# The RPI_IMAGER_ENABLE_WINDOWS_HELPER define is PRIVATE so it reaches the
# factory here but does NOT propagate to every consumer. The client adapter
# selection (in the main app's file_operations_windows.cpp) is enabled by the
# main app defining it on itself; the helper target deliberately does not, so
# the helper never pulls in the client-side adapter. The shell32/ole32 link
# deps stay PUBLIC so the final client executable resolves them.
if(WIN32 AND NOT RPI_IMAGER_DISABLE_WINDOWS_HELPER)
    target_compile_definitions(privileged_io PRIVATE RPI_IMAGER_ENABLE_WINDOWS_HELPER)
    target_link_libraries(privileged_io PUBLIC shell32 ole32)
    target_include_directories(privileged_io PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
endif()

if(UNIX AND NOT APPLE AND NOT RPI_IMAGER_DISABLE_LINUX_HELPER)
    target_compile_definitions(privileged_io PRIVATE RPI_IMAGER_ENABLE_LINUX_HELPER)
    target_include_directories(privileged_io PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
endif()

target_compile_features(privileged_io PUBLIC cxx_std_20)

# Strict warnings - this is new code; keep it clean.
if(MSVC)
    target_compile_options(privileged_io PRIVATE /W4)
else()
    target_compile_options(privileged_io PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Generated protobuf code triggers strict-warning noise; relax for the
# library as a whole rather than per-file (since `protobuf_generate` doesn't
# expose the file list in a way we can attach properties to).
if(NOT MSVC)
    target_compile_options(privileged_io PRIVATE
        -Wno-unused-parameter
    )
endif()
