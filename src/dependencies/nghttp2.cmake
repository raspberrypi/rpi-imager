# Remote nghttp2

set(NGHTTP2_VERSION "1.69.0")
rpi_imager_fetch_git_or_vendor(nghttp2
    VENDOR_DIR nghttp2
    VENDOR_MARKER lib/CMakeLists.txt
    GIT_REPOSITORY https://github.com/nghttp2/nghttp2.git
    GIT_TAG v${NGHTTP2_VERSION}
)
set(BUILD_EXAMPLES OFF)
set(ENABLE_LIB_ONLY ON)
set(ENABLE_FAILMALLOC OFF)
FetchContent_GetProperties(nghttp2)
if(NOT nghttp2_POPULATED)
    FetchContent_Populate(nghttp2)
    add_subdirectory(${nghttp2_SOURCE_DIR} ${nghttp2_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()
rpi_imager_sbom_add_component(nghttp2
    VERSION "${NGHTTP2_VERSION}"
    SUPPLIER "nghttp2 project"
    VCS_URL "https://github.com/nghttp2/nghttp2.git"
    GIT_TAG "v${NGHTTP2_VERSION}"
    SOURCE_DIR "${nghttp2_SOURCE_DIR}"
    LICENSE "MIT"
    PURL "pkg:github/nghttp2/nghttp2@v${NGHTTP2_VERSION}"
    CPE "cpe:2.3:a:nghttp2:nghttp2:${NGHTTP2_VERSION}:*:*:*:*:*:*:*"
    USED_BY curl)
unset(ENABLE_LIB_ONLY)
unset(ENABLE_FAILMALLOC)
unset(BUILD_EXAMPLES)
set(NGHTTP2_LIBRARIES nghttp2_static CACHE FILEPATH "" FORCE)
set(NGHTTP2_LIBRARY nghttp2_static CACHE FILEPATH "" FORCE)
set(NGHTTP2_INCLUDE_DIR ${nghttp2_SOURCE_DIR}/lib CACHE PATH "" FORCE)
set(NGHTTP2_INCLUDE_DIRS ${nghttp2_SOURCE_DIR}/lib CACHE PATH "" FORCE)
set(NGHTTP2_FOUND true CACHE BOOL "" FORCE)

