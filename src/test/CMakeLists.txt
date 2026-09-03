# Test configuration for rpi-imager
# Uses Catch2 and CTest for testing

# Fetch Catch2 for testing
include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.2
    FIND_PACKAGE_ARGS NAMES Catch2
)

FetchContent_MakeAvailable(Catch2)

# Include Catch2 CMake utilities for test discovery
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)

# Register a Catch2 binary's cases with CTest.
#
# Wraps catch_discover_tests purely to pin SKIP_RETURN_CODE. Catch2 exits 4 when
# every selected case was skipped, and because discovery registers one CTest test
# per case, a case that calls SKIP() is always the only one selected — so without
# this CTest reports every skip as a failure. Tests that skip when optional
# fixtures or hardware are absent (the pieeprom and eeprom_signer ones) were
# accounting for six phantom failures. Use this in place of
# catch_discover_tests() so a new test binary cannot reintroduce them.
function(rpi_discover_tests target)
    catch_discover_tests(${target} PROPERTIES SKIP_RETURN_CODE 4)
endfunction()

# Link liburing into a target that compiles the platform file_operations.
#
# linux/Platform.cmake adds -DHAVE_LIBURING with add_definitions(), which every
# target in this directory inherits, but it only puts the library itself into
# EXTRALIBS -- and EXTRALIBS reaches the app target alone. Any test binary
# compiling file_operations_linux.cpp therefore got the io_uring code paths
# compiled in with nothing to resolve them against, and failed to link on any
# machine with liburing-dev >= 2.2 installed. Call this for every target that
# pulls in PLATFORM_FILE_OPS.
function(rpi_link_platform_file_ops target)
    if(LIBURING_USABLE)
        target_include_directories(${target} PRIVATE ${LIBURING_INCLUDE_DIRS})
        target_link_libraries(${target} PRIVATE ${LIBURING_LIBRARIES})
    endif()
endfunction()

# Add the customization generator test executable
# Includes the bundled crypto sources the generator now depends on for
# credential derivation (yescrypt/sha256crypt password hashing).
add_executable(customization_generator_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../customization_generator.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../customization_generator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../dependencies/sha256crypt/sha256crypt.c
    customization_generator_test.cpp
)

# Link against Catch2, Qt and the bundled yescrypt static library
# (yescrypt is defined in the parent scope via dependencies/yescrypt.cmake and
# carries its include directory as a PUBLIC usage requirement).
target_link_libraries(customization_generator_test PRIVATE 
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Network
    yescrypt
)

# Include parent directory for headers
target_include_directories(customization_generator_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(customization_generator_test PRIVATE cxx_std_20)
target_compile_options(customization_generator_test PRIVATE 
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
)

# Register with CTest for automatic test discovery
rpi_discover_tests(customization_generator_test)

# Also add a custom target for manual running
add_custom_target(test_customization_generator
    COMMAND customization_generator_test
    DEPENDS customization_generator_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running customization generator tests"
)

# Determine platform-specific file operations implementation for FAT partition test
if(WIN32)
    set(PLATFORM_FILE_OPS
        ${CMAKE_CURRENT_SOURCE_DIR}/../windows/file_operations_windows.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../windows/file_operations_windows.cpp
    )
elseif(APPLE)
    set(PLATFORM_FILE_OPS
        ${CMAKE_CURRENT_SOURCE_DIR}/../mac/file_operations_macos.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../mac/file_operations_macos.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../mac/macfile.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../mac/macfile.cpp
    )
else()
    set(PLATFORM_FILE_OPS
        ${CMAKE_CURRENT_SOURCE_DIR}/../linux/file_operations_linux.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../linux/file_operations_linux.cpp
    )
endif()

# Add the FAT partition test executable (manual test against real filesystem)
add_executable(fat_partition_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapper.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperpartition.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperpartition.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperblockcacheentry.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperblockcacheentry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperfatpartition.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperfatpartition.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../file_operations.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../file_operations.cpp
    ${PLATFORM_FILE_OPS}
    ${CMAKE_CURRENT_SOURCE_DIR}/../fat_partition_test.cpp
)

# Enable Qt MOC for Q_OBJECT classes
set_target_properties(fat_partition_test PROPERTIES
    AUTOMOC ON
)

# Link Catch2 and Qt libraries
target_link_libraries(fat_partition_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
)

# Link platform-specific libraries
if(APPLE)
    target_link_libraries(fat_partition_test PRIVATE
        "-framework Security"
        "-framework DiskArbitration"
        "-framework CoreFoundation"
    )
endif()

# Include parent directory for headers
target_include_directories(fat_partition_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(fat_partition_test PRIVATE cxx_std_20)
target_compile_options(fat_partition_test PRIVATE 
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
)

rpi_link_platform_file_ops(fat_partition_test)

# Register with CTest for automatic test discovery
# Note: These tests are marked with [!mayfail] and [.destructive] tags
# Run with: ctest -C Debug --output-on-failure
# Or with environment variable: FAT_TEST_MOUNT_PATH="/Volumes/bootfs 1" ctest
rpi_discover_tests(fat_partition_test)

# Custom target for manual running with mount path argument
# Usage: FAT_TEST_MOUNT_PATH="/Volumes/bootfs 1" cmake --build . --target test_fat_partition
add_custom_target(test_fat_partition
    COMMAND echo "Set FAT_TEST_MOUNT_PATH environment variable and run: ./test/fat_partition_test"
    COMMAND echo "Example: FAT_TEST_MOUNT_PATH=\"/Volumes/bootfs 1\" ./test/fat_partition_test"
    DEPENDS fat_partition_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "FAT partition test built - set FAT_TEST_MOUNT_PATH to run"
)

# ============================================================================
# Drivelist Tests
# ============================================================================

# Determine platform-specific drivelist implementation
if(WIN32)
    set(DRIVELIST_PLATFORM_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/../drivelist/drivelist_windows.cpp
    )
    set(DRIVELIST_PLATFORM_LIBS setupapi)
elseif(APPLE)
    set(DRIVELIST_PLATFORM_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/../drivelist/drivelist_darwin.mm
    )
    set(DRIVELIST_PLATFORM_LIBS
        "-framework DiskArbitration"
        "-framework IOKit"
        "-framework Cocoa"
        "-framework CoreFoundation"
    )
else()
    set(DRIVELIST_PLATFORM_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/../drivelist/drivelist_linux.cpp
    )
    set(DRIVELIST_PLATFORM_LIBS "")
endif()

# Add the drivelist test executable
add_executable(drivelist_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../drivelist/drivelist.h
    ${DRIVELIST_PLATFORM_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/../drivelist/drivelist_test.cpp
)

# Enable test API for access to parsing functions
target_compile_definitions(drivelist_test PRIVATE DRIVELIST_ENABLE_TEST_API)

# Link against Catch2, Qt, and platform libraries
target_link_libraries(drivelist_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    ${DRIVELIST_PLATFORM_LIBS}
)

# Include parent directory for headers
target_include_directories(drivelist_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(drivelist_test PRIVATE cxx_std_20)
target_compile_options(drivelist_test PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
)

# Register with CTest for automatic test discovery
rpi_discover_tests(drivelist_test)

# Custom target for manual running
add_custom_target(test_drivelist
    COMMAND drivelist_test
    DEPENDS drivelist_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running drivelist tests"
)

# ============================================================================
# PlatformQuirks Tests
# ============================================================================

# Determine platform-specific platformquirks implementation
if(WIN32)
    set(PLATFORMQUIRKS_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/../windows/platformquirks_windows.cpp
    )
    set(PLATFORMQUIRKS_LIBS
        ws2_32
        iphlpapi
        wbemuuid
        ole32
        oleaut32
    )
elseif(APPLE)
    set(PLATFORMQUIRKS_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/../mac/platformquirks_macos.mm
    )
    set(PLATFORMQUIRKS_LIBS
        "-framework AppKit"
        "-framework SystemConfiguration"
        "-framework DiskArbitration"
        "-framework CoreFoundation"
        "-framework CoreServices"  # LSSetDefaultHandlerForURLScheme (registerUriScheme)
    )
else()
    set(PLATFORMQUIRKS_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/../linux/platformquirks_linux.cpp
    )
    set(PLATFORMQUIRKS_LIBS "")
endif()

# Add the platformquirks test executable
add_executable(platformquirks_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../platformquirks.h
    ${PLATFORMQUIRKS_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/platformquirks_test.cpp
)

# Enable test API for Windows parseDeviceNumber
target_compile_definitions(platformquirks_test PRIVATE PLATFORMQUIRKS_ENABLE_TEST_API)

# Link against Catch2, Qt, and platform libraries
target_link_libraries(platformquirks_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    ${PLATFORMQUIRKS_LIBS}
)

# Include parent directory for headers
target_include_directories(platformquirks_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(platformquirks_test PRIVATE cxx_std_20)
target_compile_options(platformquirks_test PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
)

# Register with CTest for automatic test discovery
rpi_discover_tests(platformquirks_test)

# Custom target for manual running
add_custom_target(test_platformquirks
    COMMAND platformquirks_test
    DEPENDS platformquirks_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running platformquirks tests"
)

# ============================================================================
# rpiboot Tests
# ============================================================================

# rpiboot protocol tests (bootcode loader, file server, types)
add_executable(rpiboot_protocol_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/rpiboot_types.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/usb_transport.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/bootcode_loader.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/bootcode_loader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/file_server.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/file_server.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/bootfiles.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/bootfiles.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/rpiboot_protocol.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/rpiboot_protocol.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/test/mock_usb_transport.h
    rpiboot_protocol_test.cpp
)

target_link_libraries(rpiboot_protocol_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    ${LibArchive_LIBRARIES}
    ${ZLIB_LIBRARIES}
    ${LIBLZMA_LIBRARIES}
    ${ZSTD_LIBRARIES}
)

target_include_directories(rpiboot_protocol_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${LibArchive_INCLUDE_DIR}
    ${ZLIB_INCLUDE_DIRS}
    ${LIBLZMA_INCLUDE_DIRS}
    ${ZSTD_INCLUDE_DIR}
)

target_compile_features(rpiboot_protocol_test PRIVATE cxx_std_20)
target_compile_options(rpiboot_protocol_test PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
)
rpi_discover_tests(rpiboot_protocol_test)

add_custom_target(test_rpiboot_protocol
    COMMAND rpiboot_protocol_test
    DEPENDS rpiboot_protocol_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running rpiboot protocol tests"
)

# Bootfiles (tar extraction) tests
add_executable(rpiboot_bootfiles_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/bootfiles.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/bootfiles.cpp
    rpiboot_bootfiles_test.cpp
)

target_link_libraries(rpiboot_bootfiles_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    ${LibArchive_LIBRARIES}
    ${ZLIB_LIBRARIES}
    ${LIBLZMA_LIBRARIES}
    ${ZSTD_LIBRARIES}
)

target_include_directories(rpiboot_bootfiles_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${LibArchive_INCLUDE_DIR}
    ${ZLIB_INCLUDE_DIRS}
    ${LIBLZMA_INCLUDE_DIRS}
    ${ZSTD_INCLUDE_DIR}
)

target_compile_features(rpiboot_bootfiles_test PRIVATE cxx_std_20)
target_compile_options(rpiboot_bootfiles_test PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
)
rpi_discover_tests(rpiboot_bootfiles_test)

add_custom_target(test_rpiboot_bootfiles
    COMMAND rpiboot_bootfiles_test
    DEPENDS rpiboot_bootfiles_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running rpiboot bootfiles tests"
)

# Fastboot protocol tests
add_executable(fastboot_protocol_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/usb_transport.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/rpiboot_types.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/test/mock_usb_transport.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../fastboot/fastboot_protocol.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../fastboot/fastboot_protocol.cpp
    fastboot_protocol_test.cpp
)

target_link_libraries(fastboot_protocol_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
)

target_include_directories(fastboot_protocol_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(fastboot_protocol_test PRIVATE cxx_std_20)
target_compile_options(fastboot_protocol_test PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
)
rpi_discover_tests(fastboot_protocol_test)

add_custom_target(test_fastboot_protocol
    COMMAND fastboot_protocol_test
    DEPENDS fastboot_protocol_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running fastboot protocol tests"
)

# Sparse encoder tests
add_executable(sparse_encoder_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../fastboot/sparse_encoder.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../fastboot/sparse_encoder.cpp
    sparse_encoder_test.cpp
)

target_link_libraries(sparse_encoder_test PRIVATE
    Catch2::Catch2WithMain
)

target_include_directories(sparse_encoder_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(sparse_encoder_test PRIVATE cxx_std_20)
target_compile_options(sparse_encoder_test PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
)
rpi_discover_tests(sparse_encoder_test)

add_custom_target(test_sparse_encoder
    COMMAND sparse_encoder_test
    DEPENDS sparse_encoder_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running sparse encoder tests"
)

# Pieeprom format tests (parse/edit pieeprom.bin, BOOT_ORDER helpers)
add_executable(pieeprom_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../fastboot/pieeprom.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../fastboot/pieeprom.cpp
    pieeprom_test.cpp
)
target_link_libraries(pieeprom_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
)
target_include_directories(pieeprom_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_compile_features(pieeprom_test PRIVATE cxx_std_20)
target_compile_options(pieeprom_test PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
)
rpi_discover_tests(pieeprom_test)

add_custom_target(test_pieeprom
    COMMAND pieeprom_test
    DEPENDS pieeprom_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running pieeprom format tests"
)

# BootloaderImage tests (AB-capable pieeprom: bootsys signing/extraction)
add_executable(bootloader_image_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/bootloader_image.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/bootloader_image.cpp
    bootloader_image_test.cpp
)
target_link_libraries(bootloader_image_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
)
target_include_directories(bootloader_image_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_compile_features(bootloader_image_test PRIVATE cxx_std_20)
target_compile_options(bootloader_image_test PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
)
rpi_discover_tests(bootloader_image_test)

add_custom_target(test_bootloader_image
    COMMAND bootloader_image_test
    DEPENDS bootloader_image_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running BootloaderImage (AB pieeprom) tests"
)

# Fastboot EEPROM wire-protocol tests
add_executable(fastboot_eeprom_wire_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/usb_transport.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/rpiboot_types.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/test/mock_usb_transport.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../fastboot/fastboot_protocol.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../fastboot/fastboot_protocol.cpp
    fastboot_eeprom_wire_test.cpp
)
target_link_libraries(fastboot_eeprom_wire_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
)
target_include_directories(fastboot_eeprom_wire_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_compile_features(fastboot_eeprom_wire_test PRIVATE cxx_std_20)
target_compile_options(fastboot_eeprom_wire_test PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
)
rpi_discover_tests(fastboot_eeprom_wire_test)

add_custom_target(test_fastboot_eeprom_wire
    COMMAND fastboot_eeprom_wire_test
    DEPENDS fastboot_eeprom_wire_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running fastboot EEPROM wire tests"
)

# EEPROM signer tests (in-process .sig builder + cross-check vs rpi-eeprom-digest)
if(WIN32)
    set(EEPROM_SIGNER_PLATFORM_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/../windows/secureboot_crypto_windows.cpp
    )
elseif(APPLE)
    set(EEPROM_SIGNER_PLATFORM_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/../mac/secureboot_crypto_macos.cpp
    )
else()
    set(EEPROM_SIGNER_PLATFORM_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/../linux/secureboot_crypto_linux.cpp
    )
endif()

add_executable(eeprom_signer_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../fastboot/eeprom_signer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../fastboot/eeprom_signer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../secureboot_crypto.h
    ${EEPROM_SIGNER_PLATFORM_SRC}
    eeprom_signer_test.cpp
)
target_link_libraries(eeprom_signer_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
)
if(APPLE)
    target_link_libraries(eeprom_signer_test PRIVATE
        "-framework Security" "-framework CoreFoundation"
    )
elseif(WIN32)
    target_link_libraries(eeprom_signer_test PRIVATE
        crypt32 bcrypt ncrypt
    )
endif()
target_include_directories(eeprom_signer_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_compile_features(eeprom_signer_test PRIVATE cxx_std_20)
target_compile_options(eeprom_signer_test PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
)
rpi_discover_tests(eeprom_signer_test)

add_custom_target(test_eeprom_signer
    COMMAND eeprom_signer_test
    DEPENDS eeprom_signer_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running EEPROM signer tests"
)

# Hardware integration tests (gated by RPIBOOT_TEST_DEVICE env var)
add_executable(rpiboot_integration_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/rpiboot_types.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/usb_transport.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/libusb_transport.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/libusb_transport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/bootcode_loader.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/bootcode_loader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/file_server.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/file_server.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/bootfiles.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/bootfiles.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/rpiboot_protocol.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/rpiboot_protocol.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/firmware_manager.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/firmware_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../curlnetworkconfig.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../curlnetworkconfig.cpp
    rpiboot_integration_test.cpp
)

target_link_libraries(rpiboot_integration_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    ${LIBUSB_LIBRARIES}
    ${CURL_LIBRARIES}
    ${LibArchive_LIBRARIES}
    ${ZLIB_LIBRARIES}
    ${LIBLZMA_LIBRARIES}
    ${ZSTD_LIBRARIES}
)

target_include_directories(rpiboot_integration_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${LIBUSB_INCLUDE_DIR}
    ${CURL_INCLUDE_DIR}
    ${LibArchive_INCLUDE_DIR}
    ${ZLIB_INCLUDE_DIRS}
    ${LIBLZMA_INCLUDE_DIRS}
    ${ZSTD_INCLUDE_DIR}
)

target_compile_features(rpiboot_integration_test PRIVATE cxx_std_20)
target_compile_options(rpiboot_integration_test PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
)
rpi_discover_tests(rpiboot_integration_test)

add_custom_target(test_rpiboot_integration
    COMMAND rpiboot_integration_test
    DEPENDS rpiboot_integration_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running rpiboot integration tests (requires hardware)"
)

# Combined target for all rpiboot tests
add_custom_target(test_rpiboot
    DEPENDS rpiboot_protocol_test rpiboot_bootfiles_test fastboot_protocol_test sparse_encoder_test
    COMMENT "Running all rpiboot unit tests"
)

# ============================================================================
# Disk Formatter Tests
# ============================================================================

# Standalone test (not Catch2) — uses POSIX fork/exec for system tool validation
if(NOT WIN32)
    add_executable(disk_formatter_test
        ${CMAKE_CURRENT_SOURCE_DIR}/../disk_formatter.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../disk_formatter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../file_operations.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../file_operations.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../aligned_buffer.h
        ${PLATFORM_FILE_OPS}
        disk_formatter_test.cpp
    )

    # Enable Qt MOC for Q_OBJECT classes (e.g. MacFile on macOS)
    set_target_properties(disk_formatter_test PROPERTIES AUTOMOC ON)

    target_include_directories(disk_formatter_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    )

    # Qt6::Core needed by platform file_operations (QFile, QDebug)
    target_link_libraries(disk_formatter_test PRIVATE
        Qt6::Core
    )

    target_compile_features(disk_formatter_test PRIVATE cxx_std_20)
    target_compile_options(disk_formatter_test PRIVATE
        -Wall -Wextra -Wpedantic
        $<$<CONFIG:Debug>:-g -O0>
    )

    if(APPLE)
        target_link_libraries(disk_formatter_test PRIVATE
            "-framework Security"
            "-framework DiskArbitration"
            "-framework CoreFoundation"
        )
    endif()

    rpi_link_platform_file_ops(disk_formatter_test)

    # Not Catch2, so rpi_discover_tests does not apply -- register it directly.
    # Until now this binary was built but never registered, which left
    # file_operations.cpp and the platform file_operations implementation with
    # no test that actually runs. The TIMEOUT is a backstop: the loop-device
    # section shells out, and while every sudo in it now passes -n, a wedged
    # external tool should fail the suite rather than hang it.
    add_test(NAME disk_formatter COMMAND disk_formatter_test)
    set_tests_properties(disk_formatter PROPERTIES TIMEOUT 120)

    add_custom_target(test_disk_formatter
        COMMAND disk_formatter_test
        DEPENDS disk_formatter_test
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Running disk formatter tests"
    )
endif()

# ============================================================================
# Device I/O Limits Diagnostic Test
# ============================================================================
# Reports raw DeviceIOLimits values for all connected drives.
# Validates the queue-depth and max-transfer heuristics against real hardware.

add_executable(device_io_limits_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../file_operations.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../file_operations.cpp
    ${PLATFORM_FILE_OPS}
    device_io_limits_test.cpp
)

set_target_properties(device_io_limits_test PROPERTIES AUTOMOC ON)

target_include_directories(device_io_limits_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(device_io_limits_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
)

if(APPLE)
    target_link_libraries(device_io_limits_test PRIVATE
        "-framework Security"
        "-framework DiskArbitration"
        "-framework CoreFoundation"
    )
endif()

rpi_link_platform_file_ops(device_io_limits_test)

target_compile_features(device_io_limits_test PRIVATE cxx_std_20)

rpi_discover_tests(device_io_limits_test)

add_custom_target(test_device_io_limits
    COMMAND device_io_limits_test
    DEPENDS device_io_limits_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running device I/O limits diagnostic"
)
# BlockBatcher coalesces libarchive data blocks into larger writes during
# multi-file extraction. Header-only and free of Qt/libarchive, so it tests without
# a download, a ring buffer or a device attached.
add_executable(block_batcher_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../block_batcher.h
    block_batcher_test.cpp
)

target_link_libraries(block_batcher_test PRIVATE
    Catch2::Catch2WithMain
)

target_include_directories(block_batcher_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(block_batcher_test PRIVATE cxx_std_20)
target_compile_options(block_batcher_test PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
)

rpi_discover_tests(block_batcher_test)

add_custom_target(test_block_batcher
    COMMAND block_batcher_test
    DEPENDS block_batcher_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running block batcher tests"
)

# ============================================================================
# RingBuffer Slot Recycling Tests
# ============================================================================
# The write path uses the ring buffer as a slot pool for asynchronous I/O, whose
# completions release slots out of order. Covers that a released slot -- and only
# that slot -- is recycled, so a buffer still being read from is never handed
# back to the producer. Needs Qt6::Core for qMallocAligned/qDebug.

add_executable(ringbuffer_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../ringbuffer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../ringbuffer.cpp
    ringbuffer_test.cpp
)

target_include_directories(ringbuffer_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(ringbuffer_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
)

target_compile_features(ringbuffer_test PRIVATE cxx_std_20)
target_compile_options(ringbuffer_test PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
)

rpi_discover_tests(ringbuffer_test)

add_custom_target(test_ringbuffer
    COMMAND ringbuffer_test
    DEPENDS ringbuffer_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running ring buffer tests"
)

# ============================================================================
# runWithTimeout Lifetime Hazards
# ============================================================================
# runWithTimeout() detaches its worker thread on both the timeout and the
# cancellation path, abandoning a thread that still holds references to the
# promise and completion flag on the frame being returned from. These cases
# provoke that deliberately so a sanitiser can be pointed at it.
#
# The cases are tagged [.timeout-hazard] -- hidden, so catch_discover_tests
# does not register them and an ordinary `ctest` run never executes them. That
# is deliberate: without instrumentation the detached worker writes through a
# dangling reference and segfaults the binary, which is the behaviour under
# test but is not a useful CI signal. Only the [timeout-utils] control case,
# which takes the joined path, runs by default.
#
# Build with RPI_IMAGER_TEST_SANITIZER and select the hidden tag explicitly:
#
#   cmake -B build-asan -DBUILD_TESTING=ON -DRPI_IMAGER_TEST_SANITIZER=address
#   cmake --build build-asan --target timeout_utils_test
#   ASAN_OPTIONS=detect_stack_use_after_return=1 \
#       ./build-asan/src/test/timeout_utils_test "[timeout-hazard]"
#
# ASan reports stack-use-after-return at timeout_utils.h:100 and TSan reports
# heap-use-after-free between timeout_utils.h:101 and the promise destructor.
# Note that ASan stops at the first report, so run the two hazard cases
# separately to see both.

set(RPI_IMAGER_TEST_SANITIZER "none" CACHE STRING
    "Sanitizer to build timeout_utils_test with: none, address or thread")
set_property(CACHE RPI_IMAGER_TEST_SANITIZER PROPERTY STRINGS none address thread)

# Header-only and free of Qt, so this builds without a device or a download --
# which also means nothing else has pulled in the thread library for us.
find_package(Threads REQUIRED)

add_executable(timeout_utils_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../timeout_utils.h
    timeout_utils_test.cpp
)

target_link_libraries(timeout_utils_test PRIVATE
    Catch2::Catch2WithMain
    Threads::Threads
)

target_include_directories(timeout_utils_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(timeout_utils_test PRIVATE cxx_std_20)
target_compile_options(timeout_utils_test PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
)

if(NOT RPI_IMAGER_TEST_SANITIZER STREQUAL "none")
    if(MSVC)
        message(FATAL_ERROR "RPI_IMAGER_TEST_SANITIZER needs a GCC/Clang-style driver")
    endif()
    # Optimisation level is chosen per sanitiser, because it changes which
    # diagnosis you get:
    #
    #   ASan at -O0  -- runWithTimeout stays out of line, so the report is
    #                   stack-use-after-return against runWithTimeout's own
    #                   frame, naming `completed` at timeout_utils.h:100.
    #                   Inlined at -O1+ it still fires, but as
    #                   stack-use-after-scope against the caller's frame.
    #   TSan at -O1  -- reports heap-use-after-free between set_value() at
    #                   timeout_utils.h:101 and ~promise() at
    #                   timeout_utils.h:129, which states the bug outright.
    #                   At -O0 it reports instead as a plain data race on the
    #                   dead frame; still a true positive, but it fingers the
    #                   test's own clobber helper rather than the destructor.
    if(RPI_IMAGER_TEST_SANITIZER STREQUAL "thread")
        set(_hazard_opt -O1)
    else()
        set(_hazard_opt -O0)
    endif()
    target_compile_options(timeout_utils_test PRIVATE
        -fsanitize=${RPI_IMAGER_TEST_SANITIZER} -fno-omit-frame-pointer -g ${_hazard_opt})
    target_link_options(timeout_utils_test PRIVATE
        -fsanitize=${RPI_IMAGER_TEST_SANITIZER})
    # Catch2 itself is not instrumented by this option. Harmless here -- it
    # runs single-threaded and owns none of the memory under test.
    message(STATUS "timeout_utils_test: building with -fsanitize=${RPI_IMAGER_TEST_SANITIZER}")
endif()

rpi_discover_tests(timeout_utils_test)

add_custom_target(test_timeout_utils
    COMMAND timeout_utils_test
    DEPENDS timeout_utils_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running timeout utils tests (hazard cases excluded)"
)

# ============================================================================
# Embedded Display Scaling Matrix
# ============================================================================
# applyEmbeddedDisplayScaling() chooses the embedded UI scale by reading the
# connected display out of /sys/class/drm, so the cases that matter -- a
# 43-inch-class 4K TV on a desk, the same set with no usable EDID, a DSI panel
# that reports no physical size -- are unreachable from an ordinary unit test,
# which only ever sees the build host's own display.
#
# embedded_scaling/run.sh drives the real code instead: per case it builds a
# synthetic /sys/class/drm, bind-mounts it over the real one inside an
# unprivileged mount namespace, and checks the factor chosen. This probe is
# what it runs inside that namespace. Linux-only, and it skips itself (exit 4)
# where the host cannot provide the namespace.

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(embedded_scaling_probe
        ${CMAKE_CURRENT_SOURCE_DIR}/../platformquirks.h
        ${PLATFORMQUIRKS_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/embedded_scaling_probe.cpp
    )

    target_link_libraries(embedded_scaling_probe PRIVATE
        Qt6::Core
        ${PLATFORMQUIRKS_LIBS}
    )

    target_include_directories(embedded_scaling_probe PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    )

    target_compile_features(embedded_scaling_probe PRIVATE cxx_std_20)
    target_compile_options(embedded_scaling_probe PRIVATE
        -Wall -Wextra -Wpedantic
        $<$<CONFIG:Debug>:-g -O0>
    )

    add_test(NAME embedded_scaling_matrix
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/embedded_scaling/run.sh
                $<TARGET_FILE:embedded_scaling_probe>
    )
    set_tests_properties(embedded_scaling_matrix PROPERTIES SKIP_RETURN_CODE 4)

    add_custom_target(test_embedded_scaling
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/embedded_scaling/run.sh
                $<TARGET_FILE:embedded_scaling_probe>
        DEPENDS embedded_scaling_probe
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Running embedded display scaling matrix"
    )

    # The matrix above proves which factor is chosen; screenshots.sh renders the
    # UI at it. That needs the app itself, so the target only appears in a GUI
    # build; only an embedded one lays out the way the netboot imager does; and
    # it needs the screenshot hook, which is compiled out unless a test build
    # asks for it (-DENABLE_TEST_HOOKS=ON). Not a CTest test: it wants fonts and
    # several seconds per profile, and its output is meant to be looked at.
    if(TARGET rpi-imager AND BUILD_EMBEDDED AND ENABLE_TEST_HOOKS)
        add_custom_target(screenshots_embedded_scaling
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/embedded_scaling/screenshots.sh
                    $<TARGET_FILE:rpi-imager>
                    ${CMAKE_CURRENT_BINARY_DIR}/embedded_scaling_screenshots
            DEPENDS rpi-imager
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Rendering the embedded UI at each display profile"
        )
    endif()
endif()

# ============================================================================
# Coverage Report
# ============================================================================
# `cmake --build <dir> --target coverage` with -DRPI_IMAGER_COVERAGE=ON runs
# the CTest suite and renders a gcovr HTML report of the headless core.
#
#   cmake -B build-cov -DBUILD_TESTING=ON -DRPI_IMAGER_COVERAGE=ON
#   cmake --build build-cov --target coverage
#   xdg-open build-cov/coverage/index.html
#
# The instrumentation itself is switched on in ../CMakeLists.txt; what lands in
# the report is decided by the exclusion list in RunCoverage.cmake. The report
# is branch-oriented on purpose -- line coverage alone would have called
# runWithTimeout's timeout path "reached" as soon as any test entered its poll
# loop, when in fact neither detach() had ever executed.

if(RPI_IMAGER_COVERAGE)
    find_program(GCOVR_EXECUTABLE gcovr)
    if(NOT GCOVR_EXECUTABLE)
        message(WARNING
            "RPI_IMAGER_COVERAGE is on but gcovr was not found, so no `coverage` target "
            "was created. Install it (apt install gcovr / pip install gcovr) and "
            "re-run CMake.")
    else()
        # No DEPENDS on the test binaries: RunCoverage.cmake builds them
        # itself, best-effort, so one unbuildable target (rpiboot_integration_test
        # does not currently link) degrades the report rather than blocking it.
        # Expressing it as DEPENDS made that single failure fatal to the run.
        add_custom_target(coverage
            COMMAND ${CMAKE_COMMAND}
                    -DCOVERAGE_BINARY_DIR=${CMAKE_BINARY_DIR}
                    -DCOVERAGE_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/..
                    -DCOVERAGE_OUTPUT_DIR=${CMAKE_BINARY_DIR}/coverage
                    -DGCOVR_EXECUTABLE=${GCOVR_EXECUTABLE}
                    -DCTEST_EXECUTABLE=${CMAKE_CTEST_COMMAND}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/RunCoverage.cmake
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Running the suite under gcov and rendering a coverage report"
            USES_TERMINAL
            VERBATIM
        )
    endif()
endif()
