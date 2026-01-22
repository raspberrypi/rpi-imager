# Windows platform-specific sources and link settings

# Find dlltool for creating import library
if(NOT CMAKE_DLLTOOL)
    find_program(CMAKE_DLLTOOL NAMES dlltool x86_64-w64-mingw32-dlltool)
    if(NOT CMAKE_DLLTOOL)
        # Try to find it in the same directory as the compiler
        get_filename_component(COMPILER_DIR ${CMAKE_C_COMPILER} DIRECTORY)
        find_program(CMAKE_DLLTOOL NAMES dlltool x86_64-w64-mingw32-dlltool PATHS ${COMPILER_DIR} NO_DEFAULT_PATH)
    endif()
    if(NOT CMAKE_DLLTOOL)
        message(FATAL_ERROR "Could not find dlltool, needed for creating wlanapi import library")
    endif()
    message(STATUS "Found dlltool: ${CMAKE_DLLTOOL}")
endif()

add_custom_command(
    OUTPUT wlanapi_delayed.lib
    COMMAND ${CMAKE_DLLTOOL} --input-def "${CMAKE_CURRENT_SOURCE_DIR}/windows/wlanapi.def"
            --output-delaylib "${CMAKE_BINARY_DIR}/wlanapi_delayed.lib" --dllname "wlanapi.dll"
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/windows/wlanapi.def
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Creating wlanapi delay-load library"
    VERBATIM
)

set(PLATFORM_SOURCES
    windows/acceleratedcryptographichash_cng.cpp
    drivelist/drivelist_windows.cpp
    windows/winfile.cpp
    windows/winfile.h
    windows/bootimgcreator_windows.cpp
    windows/rsakeyfingerprint_windows.cpp
    windows/diskpart_util.cpp
    windows/diskpart_util.h
    windows/file_operations_windows.cpp
    windows/platformquirks_windows.cpp
    windows/windows_suspend_inhibitor.cpp
)

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

set(DEPENDENCIES
    ${CMAKE_BINARY_DIR}/rpi-imager.rc
    wlanapi_delayed.lib
)
set(EXTRALIBS setupapi ${CMAKE_BINARY_DIR}/wlanapi_delayed.lib Bcrypt.dll ole32 oleaut32 wbemuuid)

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
