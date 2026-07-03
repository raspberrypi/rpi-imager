# Bundled zstd

set(ZSTD_VERSION "1.5.7")
rpi_imager_fetch_git_or_vendor(zstd
    VENDOR_DIR zstd
    VENDOR_MARKER build/cmake/CMakeLists.txt
    GIT_REPOSITORY https://github.com/facebook/zstd.git
    GIT_TAG v${ZSTD_VERSION}
    SOURCE_SUBDIR build/cmake
)
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_DICTBUILDER OFF CACHE BOOL "" FORCE)
FetchContent_GetProperties(zstd)
if(NOT zstd_POPULATED)
    FetchContent_Populate(zstd)
    add_subdirectory(${zstd_SOURCE_DIR}/build/cmake ${zstd_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()
unset(ZSTD_BUILD_PROGRAMS)
unset(ZSTD_BUILD_SHARED)
unset(ZSTD_BUILD_STATIC)
unset(ZSTD_BUILD_TESTS)
unset(ZSTD_BUILD_DICTBUILDER)
set(ZSTD_FOUND true CACHE BOOL "" FORCE)
set(Zstd_VERSION ${ZSTD_VERSION} CACHE STRING "" FORCE)
set(Zstd_INCLUDE_DIR ${zstd_SOURCE_DIR}/lib CACHE PATH "" FORCE)
set(ZSTD_INCLUDE_DIR ${zstd_SOURCE_DIR}/lib CACHE PATH "" FORCE)
set(Zstd_INCLUDE_DIRS ${zstd_SOURCE_DIR}/lib CACHE PATH "" FORCE)
set(ZSTD_INCLUDE_DIRS ${zstd_SOURCE_DIR}/lib CACHE PATH "" FORCE)
set(Zstd_LIBRARIES libzstd_static CACHE FILEPATH "" FORCE)
set(ZSTD_LIBRARIES libzstd_static CACHE FILEPATH "" FORCE)
set(ZSTD_LIBRARY ${zstd_BINARY_DIR}/lib/libzstd.a CACHE FILEPATH "" FORCE)

