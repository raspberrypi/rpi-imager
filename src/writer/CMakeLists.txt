# SPDX-License-Identifier: Apache-2.0
# Copyright (C) 2026 Raspberry Pi Ltd
#
# rpi-imager-writer: the privileged helper.
#
# Per design doc §8a, this binary is intentionally Qt-free so that a future
# port to Rust (or any non-C++ language) does not require ripping Qt out
# of the codebase first. Only the Cocoa/Foundation frameworks are linked.
#
# macOS is the shipping helper. The native Windows helper (phase 3, §14) is
# built only when the experimental option is enabled. Linux is phase 2.

if(NOT APPLE)
    if(WIN32 AND RPI_IMAGER_ENABLE_WINDOWS_HELPER)
        # rpi-imager-writer.exe: the elevated Windows helper (§14.5).
        #
        # NOTE: built as a console-subsystem exe so the standard int main()
        # entry works under MinGW. Production should move to the windows
        # subsystem (wWinMain) to avoid a transient console window when the
        # helper is launched elevated; deferred while the helper is brought up.
        add_executable(rpi-imager-writer
            main_win.cpp
            server/win/service_impl.h
            server/win/service_impl.cpp
            server/win/peer_auth_win.h
            server/win/peer_auth_win.cpp
            # §14.5 - the helper performs device I/O with the SAME Qt-free
            # WindowsFileOperations the client uses in-process today. These TUs
            # carry no Qt (verified) so they satisfy the Qt-free rule (§8a).
            # NOTE: compiled WITHOUT RPI_IMAGER_ENABLE_WINDOWS_HELPER on this
            # target, so file_operations_windows.cpp's factory yields a plain
            # WindowsFileOperations and never references the client adapter.
            ${CMAKE_CURRENT_SOURCE_DIR}/../file_operations.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/../windows/file_operations_windows.cpp
        )
        target_include_directories(rpi-imager-writer PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/..
        )
        # Peer authentication (§14.4): WinVerifyTrust + publisher org and
        # optional thumbprint allowlist pinning uses wintrust; CertGetNameStringW
        # and CertGetCertificateContextProperty use crypt32.
        target_link_libraries(rpi-imager-writer PRIVATE
            privileged_io
            advapi32
            bcrypt
            crypt32
            wintrust
        )
        target_compile_features(rpi-imager-writer PRIVATE cxx_std_20)
        set_target_properties(rpi-imager-writer PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        )
    endif()
    return()
endif()

add_executable(rpi-imager-writer
    main.mm
    server/mac/RpiImagerWriterProtocol.h
    server/mac/RpiImagerWriterProtocol.m
    server/mac/disk_arbitration.h
    server/mac/disk_arbitration.mm
    server/mac/service_impl.mm
    # §5 - block-device enumeration runs on the helper side. The same
    # DiskArbitration/IOKit source the client compiles in is reused
    # here verbatim; the helper's listDrivesNow XPC entry point
    # marshals the resulting std::vector<DeviceDescriptor> to NSArray.
    ${CMAKE_CURRENT_SOURCE_DIR}/../drivelist/drivelist.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../drivelist/drivelist_darwin.mm
)

target_include_directories(rpi-imager-writer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/server/mac
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(rpi-imager-writer PRIVATE
    "-framework Foundation"
    "-framework CoreFoundation"
    "-framework DiskArbitration"
    "-framework Security"
    "-framework IOKit"
    "-framework Cocoa"
)

target_compile_features(rpi-imager-writer PRIVATE cxx_std_20)
target_compile_options(rpi-imager-writer PRIVATE
    -Wall -Wextra -Wpedantic -fobjc-arc
    $<$<CONFIG:Debug>:-g -O0>
)

# drivelist_darwin.mm uses manual retain/release patterns (CFRetain /
# bridged-cast omissions) that the helper's default -fobjc-arc rejects.
# It compiles cleanly under MRR; rather than rewrite the file (it's
# shared with the main app's MRR build) just disable ARC for this
# translation unit inside the helper target.
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/../drivelist/drivelist_darwin.mm
    TARGET_DIRECTORY rpi-imager-writer
    PROPERTIES COMPILE_FLAGS "-fno-objc-arc"
)

# Embed an Info.plist into the helper binary's __TEXT __info_plist section.
# launchd / SMAppService read the helper's bundle identifier and version
# directly from the Mach-O - no separate .plist file needed alongside.
set(_helper_info_plist "${CMAKE_CURRENT_BINARY_DIR}/Info.plist")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    "${_helper_info_plist}"
    @ONLY
)
target_link_options(rpi-imager-writer PRIVATE
    "-Wl,-sectcreate,__TEXT,__info_plist,${_helper_info_plist}"
)
set_target_properties(rpi-imager-writer PROPERTIES
    LINK_DEPENDS "${_helper_info_plist}"
)

# Place the helper next to the main app's executable inside the bundle.
# Production placement is Contents/MacOS/rpi-imager-writer alongside the
# main rpi-imager binary; for dev builds we put it in the same build dir
# so a launchctl plist can point at it via absolute path.
set_target_properties(rpi-imager-writer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# Generate a launchd plist for dev installation. See dev/...plist.in
# for installation instructions.
set(RPI_IMAGER_WRITER_PATH "${CMAKE_BINARY_DIR}/rpi-imager-writer")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/dev/com.raspberrypi.rpi-imager.writer.plist.in"
    "${CMAKE_BINARY_DIR}/com.raspberrypi.rpi-imager.writer.plist"
    @ONLY
)

# -----------------------------------------------------------------------
# Bundle the helper into the main RpiImager.app
# -----------------------------------------------------------------------
#
# Production layout:
#   RpiImager.app/
#     Contents/
#       MacOS/
#         rpi-imager
#         rpi-imager-writer        <- our helper
#       Library/
#         LaunchDaemons/
#           com.raspberrypi.rpi-imager.writer.plist
#
# SMAppService.daemonServiceWithPlistName: looks up the plist by name in
# Contents/Library/LaunchDaemons/ relative to the calling bundle. Both the
# main app and the helper must be signed with the same Team ID.

set(_smapp_plist_in
    "${CMAKE_CURRENT_SOURCE_DIR}/com.raspberrypi.rpi-imager.writer.plist.in")
set(_smapp_plist_generated
    "${CMAKE_CURRENT_BINARY_DIR}/com.raspberrypi.rpi-imager.writer.smapp.plist")
configure_file("${_smapp_plist_in}" "${_smapp_plist_generated}" @ONLY)

# `rpi-imager` is the main app target name; APP_BUNDLE_PATH is set by
# mac/PlatformPackaging.cmake. We add a custom command on the helper
# itself - the main app target then DEPENDS on this so the helper is
# in place before macdeployqt and signing run.
set(_app_bundle_helper_path
    "${CMAKE_BINARY_DIR}/rpi-imager.app/Contents/MacOS/rpi-imager-writer")
set(_app_bundle_smapp_plist
    "${CMAKE_BINARY_DIR}/rpi-imager.app/Contents/Library/LaunchDaemons/com.raspberrypi.rpi-imager.writer.plist")

# Note: the actual `add_custom_command` that copies the helper + plist
# into the main app's bundle lives in src/CMakeLists.txt, because the
# `rpi-imager` executable target isn't defined until after this
# subdirectory has been processed. We expose the paths it needs as
# CACHE-internal variables so the parent scope can pick them up.
set(RPI_IMAGER_WRITER_BUNDLED_HELPER_PATH "${_app_bundle_helper_path}"
    CACHE INTERNAL "Path inside RpiImager.app where the helper binary lands")
set(RPI_IMAGER_WRITER_BUNDLED_PLIST_PATH "${_app_bundle_smapp_plist}"
    CACHE INTERNAL "Path inside RpiImager.app where the SMAppService plist lands")
set(RPI_IMAGER_WRITER_SMAPP_PLIST_GENERATED "${_smapp_plist_generated}"
    CACHE INTERNAL "Generated SMAppService plist source path")
