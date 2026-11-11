# Windows platform-specific sources and link settings

set(PLATFORM_SOURCES
    windows/acceleratedcryptographichash_cng.cpp
    drivelist/drivelist_windows.cpp
    windows/winfile.cpp
    windows/winfile.h
    windows/secureboot_crypto_windows.cpp
    windows/diskpart_util.cpp
    windows/diskpart_util.h
    windows/file_operations_windows.cpp
    windows/platformquirks_windows.cpp
    windows/userfiles_windows.cpp
    windows/windows_suspend_inhibitor.cpp
)

# Native Windows privileged-helper adapter (§14.6). Omitted when
# RPI_IMAGER_DISABLE_WINDOWS_HELPER is set at configure time.
if(NOT RPI_IMAGER_DISABLE_WINDOWS_HELPER)
    list(APPEND PLATFORM_SOURCES
        windows/windows_helper_file_operations.h
        windows/windows_helper_file_operations.cpp
    )
endif()

# Only include GUI-specific components for non-CLI builds
if(NOT BUILD_CLI_ONLY)
    list(APPEND PLATFORM_SOURCES
        windows/winwlancredentials.h
        windows/winwlancredentials.cpp
        windows/nativefiledialog_windows.cpp
    )
else()
    # Use stub implementation for CLI builds
    list(APPEND PLATFORM_SOURCES
        linux/wlancredentials_stub.cpp
    )
endif()

# The rc path is absolute because src/test builds this same list into
# rpi_imager_testable, and a relative one resolves against that directory.
set(DEPENDENCIES
    ${CMAKE_BINARY_DIR}/rpi-imager.rc
)
# No wlanapi: winwlancredentials.cpp resolves it at run time.
set(EXTRALIBS setupapi Bcrypt.dll crypt32 wtsapi32 ole32 oleaut32 wbemuuid virtdisk)

# Add winusb for rpiboot support
set(EXTRALIBS ${EXTRALIBS} winusb)

# ---- Relay exe ----
add_executable(rpi-imager-callback-relay WIN32 windows/CallbackRelay.cpp)
target_compile_definitions(rpi-imager-callback-relay
    PRIVATE RPI_IMAGER_PORT=${IMAGER_CALLBACK_PORT}
            RPI_IMAGER_EXE_NAME=L"rpi-imager.exe"
            RPI_IMAGER_START_ON_FAIL=1)
target_link_libraries(rpi-imager-callback-relay PRIVATE ws2_32)
if (MINGW)
  target_link_options(rpi-imager-callback-relay PRIVATE -municode)
endif()
