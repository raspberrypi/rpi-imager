# Remote nghttp2

set(NGHTTP2_VERSION "1.68.0")
FetchContent_Declare(nghttp2
    GIT_REPOSITORY https://github.com/nghttp2/nghttp2.git
    GIT_TAG        v${NGHTTP2_VERSION}
    ${USE_OVERRIDE_FIND_PACKAGE}
)
set(BUILD_EXAMPLES OFF)
set(ENABLE_LIB_ONLY ON)
set(ENABLE_FAILMALLOC OFF)
FetchContent_GetProperties(nghttp2)
if(NOT nghttp2_POPULATED)
    FetchContent_Populate(nghttp2)
    add_subdirectory(${nghttp2_SOURCE_DIR} ${nghttp2_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()
unset(ENABLE_LIB_ONLY)
unset(ENABLE_FAILMALLOC)
unset(BUILD_EXAMPLES)
set(NGHTTP2_LIBRARIES nghttp2_static CACHE FILEPATH "" FORCE)
set(NGHTTP2_LIBRARY nghttp2_static CACHE FILEPATH "" FORCE)
set(NGHTTP2_INCLUDE_DIR ${nghttp2_SOURCE_DIR}/lib CACHE PATH "" FORCE)
set(NGHTTP2_INCLUDE_DIRS ${nghttp2_SOURCE_DIR}/lib CACHE PATH "" FORCE)
set(NGHTTP2_FOUND true CACHE BOOL "" FORCE)


