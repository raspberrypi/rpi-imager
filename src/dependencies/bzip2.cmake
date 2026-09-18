# Bundled bzip2
#
# libarchive's bzip2 filter has two implementations. With libbz2 it
# decompresses in process; without it the filter falls back to running whatever
# bzip2 is on PATH and piping the stream through that.
#
# The fallback is why ENABLE_BZip2=OFF looked harmless: Linux and macOS nearly
# always have the binary installed. On Windows the child answers "Invalid
# argument" and the extract then hangs until the caller times out, so .bz2 and
# .tbz2 were advertised but unreadable. Building it in also stops us piping
# untrusted downloads through a PATH-resolved executable.
#
# 1.0.8 ships a Makefile and no CMake build, so the library is compiled here
# from its sources rather than through add_subdirectory().

set(BZIP2_VERSION "1.0.8")
rpi_imager_fetch_git_or_vendor(bzip2
    VENDOR_DIR bzip2
    VENDOR_MARKER bzlib.h
    GIT_REPOSITORY https://gitlab.com/bzip2/bzip2.git
    GIT_TAG bzip2-${BZIP2_VERSION}
)
FetchContent_GetProperties(bzip2)
if(NOT bzip2_POPULATED)
    FetchContent_Populate(bzip2)
endif()

# bzip2.c and bzip2recover.c carry main() and are deliberately left out.
add_library(bz2_static STATIC
    ${bzip2_SOURCE_DIR}/blocksort.c
    ${bzip2_SOURCE_DIR}/huffman.c
    ${bzip2_SOURCE_DIR}/crctable.c
    ${bzip2_SOURCE_DIR}/randtable.c
    ${bzip2_SOURCE_DIR}/compress.c
    ${bzip2_SOURCE_DIR}/decompress.c
    ${bzip2_SOURCE_DIR}/bzlib.c
)
target_include_directories(bz2_static PUBLIC ${bzip2_SOURCE_DIR})
# _FILE_OFFSET_BITS=64 is what lets a 32-bit build read an image over 2 GB.
# BZ_NO_STDIO would remove the error paths libarchive reports through.
target_compile_definitions(bz2_static PRIVATE _FILE_OFFSET_BITS=64)
if(NOT MSVC)
    target_compile_options(bz2_static PRIVATE -Wno-unused-but-set-variable)
endif()
set_target_properties(bz2_static PROPERTIES
    OUTPUT_NAME bz2
    POSITION_INDEPENDENT_CODE ON
)

# What libarchive's FIND_PACKAGE(BZip2) reads. The target name rather than a
# path to the archive, so the include directories and the build ordering come
# with it.
#
# libarchive then try_compile()s against BZIP2_LIBRARIES to decide whether the
# header wants a dllimport macro. A target means nothing in that context, so
# the probe fails and neither USE_BZIP2_DLL nor USE_BZIP2_STATIC is defined --
# correct for a static libbz2, and a path would fare no better, the archive
# not existing at configure time either.
set(BZIP2_LIBRARIES bz2_static CACHE STRING "" FORCE)
set(BZIP2_LIBRARY  bz2_static CACHE STRING "" FORCE)
set(BZIP2_INCLUDE_DIR ${bzip2_SOURCE_DIR} CACHE PATH "" FORCE)
set(BZIP2_INCLUDE_DIRS ${bzip2_SOURCE_DIR} CACHE PATH "" FORCE)
set(BZIP2_VERSION_STRING ${BZIP2_VERSION} CACHE STRING "" FORCE)
set(BZIP2_NEED_PREFIX "" CACHE STRING "" FORCE)
set(BZIP2_FOUND TRUE CACHE BOOL "" FORCE)

if(NOT TARGET BZip2::BZip2)
    add_library(BZip2::BZip2 ALIAS bz2_static)
endif()
