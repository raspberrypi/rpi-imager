# Bundled libusb for rpiboot USB communication

set(LIBUSB_VERSION "1.0.27")
FetchContent_Declare(libusb
    GIT_REPOSITORY https://github.com/libusb/libusb.git
    GIT_TAG v${LIBUSB_VERSION}
)
FetchContent_GetProperties(libusb)
if(NOT libusb_POPULATED)
    FetchContent_Populate(libusb)
endif()

# libusb doesn't have native CMake support, so we use our own wrapper
add_subdirectory(dependencies/libusb-cmake EXCLUDE_FROM_ALL)

set(LIBUSB_FOUND true CACHE BOOL "" FORCE)
set(LIBUSB_INCLUDE_DIR ${libusb_SOURCE_DIR}/libusb CACHE PATH "" FORCE)
set(LIBUSB_LIBRARIES usb-1.0-static CACHE STRING "" FORCE)
