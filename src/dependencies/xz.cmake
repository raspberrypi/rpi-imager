# Bundled liblzma (xz)

set(LIBLZMA_VERSION "5.8.3")
rpi_imager_fetch_git_or_vendor(xz
    VENDOR_DIR xz
    VENDOR_MARKER CMakeLists.txt
    GIT_REPOSITORY https://github.com/tukaani-project/xz.git
    GIT_TAG v${LIBLZMA_VERSION}
)
set(XZ_MICROLZMA_DECODER OFF CACHE BOOL "" FORCE)
set(XZ_MICROLZMA_ENCODER OFF CACHE BOOL "" FORCE)
set(XZ_LZIP_DECODER OFF CACHE BOOL "" FORCE)
set(XZ_ENABLE_SANDBOX OFF CACHE BOOL "" FORCE)
set(XZ_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(XZ_ENABLE_DOXYGEN OFF CACHE BOOL "" FORCE)
set(XZ_DECODERS
    lzma1
    lzma2
    delta
)
set(XZ_ENCODERS
    lzma1
    lzma2
    delta
)
set(CREATE_LZMA_SYMLINKS OFF CACHE BOOL "" FORCE)
set(CREATE_XZ_SYMLINKS OFF CACHE BOOL "" FORCE)
FetchContent_GetProperties(xz)
if(NOT xz_POPULATED)
    FetchContent_Populate(xz)
    add_subdirectory(${xz_SOURCE_DIR} ${xz_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()
rpi_imager_sbom_add_component(xz
    NAME "liblzma (XZ Utils)"
    VERSION "${LIBLZMA_VERSION}"
    SUPPLIER "Tukaani Project"
    VCS_URL "https://github.com/tukaani-project/xz.git"
    GIT_TAG "v${LIBLZMA_VERSION}"
    SOURCE_DIR "${xz_SOURCE_DIR}"
    LICENSE "0BSD"
    PURL "pkg:github/tukaani-project/xz@v${LIBLZMA_VERSION}"
    CPE "cpe:2.3:a:tukaani:xz:${LIBLZMA_VERSION}:*:*:*:*:*:*:*"
    USED_BY rpi-imager libarchive)
unset(XZ_MICROLZMA_DECODER)
unset(XZ_MICROLZMA_ENCODER)
unset(XZ_LZIP_DECODER)
unset(XZ_ENABLE_SANDBOX)
unset(XZ_BUILD_SHARED_LIBS)
unset(XZ_ENABLE_DOXYGEN)
unset(CREATE_LZMA_SYMLINKS)
unset(CREATE_XZ_SYMLINKS)
set(LIBLZMA_FOUND true CACHE BOOL "" FORCE)
set(LIBLZMA_INCLUDE_DIR ${xz_SOURCE_DIR}/src/liblzma/api CACHE PATH "" FORCE)
set(LIBLZMA_INCLUDE_DIRS ${xz_SOURCE_DIR}/src/liblzma/api CACHE PATH "" FORCE)
set(LIBLZMA_LIBRARY liblzma CACHE FILEPATH "" FORCE)
set(LIBLZMA_LIBRARIES ${xz_BINARY_DIR}/liblzma.a CACHE FILEPATH "" FORCE)
set(LIBLZMA_HAS_AUTO_DECODER true CACHE BOOL "" FORCE)
set(LIBLZMA_HAS_EASY_ENCODER true CACHE BOOL "" FORCE)
set(LIBLZMA_HAS_LZMA_PRESET true CACHE BOOL "" FORCE)

