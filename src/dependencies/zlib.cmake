# Bundled zlib

set(ZLIB_VERSION "1.4.1.1")
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZLIB_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
FetchContent_Declare(zlib
    GIT_REPOSITORY https://github.com/madler/zlib.git
    GIT_TAG 5a82f71ed1dfc0bec044d9702463dbdf84ea3b71 # v1.4.1.1, as of 27/05/2025
    ${USE_OVERRIDE_FIND_PACKAGE}
)
FetchContent_GetProperties(zlib)
if(NOT zlib_POPULATED)
    FetchContent_Populate(zlib)
    add_subdirectory(${zlib_SOURCE_DIR} ${zlib_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()
unset(ZLIB_BUILD_EXAMPLES)
unset(ZLIB_BUILD_SHARED)
unset(ZLIB_BUILD_STATIC)
unset(ZLIB_BUILD_TESTS)
unset(SKIP_INSTALL_ALL)
# Set zlib variables that libarchive's CMake will use
set(ZLIB_USE_STATIC_LIBS ON CACHE BOOL "" FORCE) # Prefer static
set(ZLIB_ROOT ${zlib_SOURCE_DIR} CACHE PATH "" FORCE)

# Mingw vs others library naming
if (WIN32 AND CMAKE_COMPILER_IS_GNUCXX)
    set(ZLIB_LIBRARY ${zlib_BINARY_DIR}/libzsd.a CACHE FILEPATH "" FORCE)
    set(ZLIB_LIBRARIES ${zlib_BINARY_DIR}/libzsd.a CACHE STRING "" FORCE)
else()
    set(ZLIB_LIBRARY ${zlib_BINARY_DIR}/libz.a CACHE FILEPATH "" FORCE)
    set(ZLIB_LIBRARIES ${zlib_BINARY_DIR}/libz.a CACHE STRING "" FORCE)
endif()

set(ZLIB_INCLUDE_DIR ${zlib_SOURCE_DIR} CACHE PATH "" FORCE)
set(ZLIB_INCLUDE_DIRS ${zlib_SOURCE_DIR} CACHE PATH "" FORCE)
set(ZLIB_FOUND TRUE CACHE BOOL "" FORCE)
add_library(ZLIB::ZLIB STATIC IMPORTED)
if (WIN32 AND CMAKE_COMPILER_IS_GNUCXX)
    set_target_properties(ZLIB::ZLIB PROPERTIES
        IMPORTED_LOCATION "${zlib_BINARY_DIR}/libzsd.a"
        INTERFACE_INCLUDE_DIRECTORIES "${zlib_SOURCE_DIR};${zlib_BINARY_DIR}"
    )
    add_dependencies(ZLIB::ZLIB zlibstatic)
else()
    set_target_properties(ZLIB::ZLIB PROPERTIES
        IMPORTED_LOCATION "${zlib_BINARY_DIR}/libz.a"
        INTERFACE_INCLUDE_DIRECTORIES "${zlib_SOURCE_DIR};${zlib_BINARY_DIR}"
    )
    add_dependencies(ZLIB::ZLIB zlibstatic)
endif()


