# Bundled libarchive configured for static zlib, zstd and bzip2

include(${CMAKE_CURRENT_LIST_DIR}/bzip2.cmake)

set(ENABLE_WERROR OFF CACHE BOOL "")
set(ENABLE_INSTALL OFF CACHE BOOL "")
set(ENABLE_TEST OFF CACHE BOOL "")
set(ENABLE_CNG OFF CACHE BOOL "")
set(ENABLE_MBEDTLS OFF CACHE BOOL "")
set(ENABLE_NETTLE OFF CACHE BOOL "")
set(ENABLE_OPENSSL OFF CACHE BOOL "")
set(ENABLE_ZLIB ON CACHE BOOL "")
# ON, and backed by the bundled libbz2 pulled in just above. OFF did not mean
# "no bzip2 support" -- it meant libarchive shelled out to a bzip2 on PATH,
# which works on Linux and macOS and hangs on Windows. See bzip2.cmake.
# FORCE, unlike its neighbours: those were written into the cache the first
# time any given build tree was configured, and a plain CACHE set never
# revisits an entry that already exists. Every tree configured before this
# change carries ENABLE_BZip2=OFF and would silently keep it.
set(ENABLE_BZip2 ON CACHE BOOL "" FORCE)
set(ENABLE_LZ4 OFF CACHE BOOL "")
set(ENABLE_LZO OFF CACHE BOOL "")
set(ENABLE_LIBB2 OFF CACHE BOOL "")
set(ENABLE_LIBXML2 OFF CACHE BOOL "")
set(ENABLE_EXPAT OFF CACHE BOOL "")
set(ENABLE_PCREPOSIX OFF CACHE BOOL "")
set(ENABLE_PCRE2POSIX OFF CACHE BOOL "")
set(ENABLE_LIBGCC OFF CACHE BOOL "")
set(ENABLE_TAR OFF CACHE BOOL "")
set(ENABLE_CPIO OFF CACHE BOOL "")
set(ENABLE_CAT OFF CACHE BOOL "")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "")
set(ARCHIVE_BUILD_STATIC_LIBS ON CACHE BOOL "")
set(ARCHIVE_BUILD_EXAMPLES OFF CACHE BOOL "")
set(ENABLE_ZSTD ON CACHE BOOL "")
set(POSIX_REGEX_LIB "libc" CACHE STRING "" FORCE)
set(LIBARCHIVE_VERSION "3.8.7")

rpi_imager_fetch_git_or_vendor(libarchive
    VENDOR_DIR libarchive
    VENDOR_MARKER CMakeLists.txt
    GIT_REPOSITORY https://github.com/libarchive/libarchive.git
    GIT_TAG v${LIBARCHIVE_VERSION}
)
FetchContent_GetProperties(libarchive)
if(NOT libarchive_POPULATED)
    FetchContent_Populate(libarchive)
    rpi_imager_patch_libarchive("${libarchive_SOURCE_DIR}")
    add_subdirectory(${libarchive_SOURCE_DIR} ${libarchive_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

if (TARGET archive_static AND TARGET ZLIB::ZLIB)
    add_dependencies(archive_static ZLIB::ZLIB)
endif()
if (TARGET archive_static AND TARGET bz2_static)
    # libarchive already links it: BZIP2_LIBRARIES goes into its ADDITIONAL_LIBS.
    # This only orders the build, and does not link it a second time -- libarchive
    # uses the plain target_link_libraries signature throughout, so adding the
    # keyword form against the same target is rejected outright.
    add_dependencies(archive_static bz2_static)
endif()

unset(POSIX_REGEX_LIB)
unset(ENABLE_WERROR)
unset(ENABLE_INSTALL)
unset(ENABLE_TEST)
unset(ENABLE_CNG)
unset(ENABLE_MBEDTLS)
unset(ENABLE_NETTLE)
unset(ENABLE_OPENSSL)
unset(ENABLE_ZLIB)
unset(ENABLE_BZip2)
unset(ENABLE_LZ4)
unset(ENABLE_LZO)
unset(ENABLE_LIBB2)
unset(ENABLE_LIBXML2)
unset(ENABLE_EXPAT)
unset(ENABLE_PCREPOSIX)
unset(ENABLE_PCRE2POSIX)
unset(ENABLE_LIBGCC)
unset(ENABLE_TAR)
unset(ENABLE_CPIO)
unset(ENABLE_CAT)
unset(ARCHIVE_BUILD_STATIC_LIBS)
unset(ENABLE_ZSTD)
set(LibArchive_FOUND true CACHE BOOL "" FORCE)
set(LibArchive_LIBRARIES archive_static CACHE FILEPATH "" FORCE)
set(LibArchive_INCLUDE_DIR ${libarchive_SOURCE_DIR}/libarchive CACHE PATH "" FORCE)
set(LibArchive_INCLUDE_DIRS ${libarchive_SOURCE_DIR}/libarchive CACHE PATH "" FORCE)

