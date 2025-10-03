# SPDX-License-Identifier: Apache-2.0
# Yescrypt password hashing library
# From: https://github.com/openwall/yescrypt

set(YESCRYPT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dependencies/yescrypt")

# Yescrypt sources - using optimized implementation
set(YESCRYPT_SOURCES
    ${YESCRYPT_SOURCE_DIR}/yescrypt-opt.c
    ${YESCRYPT_SOURCE_DIR}/yescrypt-common.c
    ${YESCRYPT_SOURCE_DIR}/yescrypt-platform.c
    ${YESCRYPT_SOURCE_DIR}/sha256.c
    ${YESCRYPT_SOURCE_DIR}/insecure_memzero.c
    ${YESCRYPT_SOURCE_DIR}/yescrypt_wrapper.c
)

set(YESCRYPT_HEADERS
    ${YESCRYPT_SOURCE_DIR}/yescrypt.h
    ${YESCRYPT_SOURCE_DIR}/yescrypt_wrapper.h
    ${YESCRYPT_SOURCE_DIR}/sha256.h
    ${YESCRYPT_SOURCE_DIR}/sysendian.h
)

# Create a static library for yescrypt
add_library(yescrypt STATIC ${YESCRYPT_SOURCES} ${YESCRYPT_HEADERS})

# Set include directory
target_include_directories(yescrypt PUBLIC ${YESCRYPT_SOURCE_DIR})

# Make sure the library is position independent for linking
set_property(TARGET yescrypt PROPERTY POSITION_INDEPENDENT_CODE ON)

# Exclude from all builds by default (will be linked when needed)
set_target_properties(yescrypt PROPERTIES EXCLUDE_FROM_ALL TRUE)

set(YESCRYPT_INCLUDE_DIR ${YESCRYPT_SOURCE_DIR})
set(YESCRYPT_LIBRARIES yescrypt)

