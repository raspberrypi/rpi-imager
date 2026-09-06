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

# Tests for the real libusb transport, run against an emulated USB device
# (see rpiboot_usb_device_test.cpp and data/usb_gadget_emulator.sh). Kept
# separate from rpiboot_integration_test because that one wants a Compute
# Module plugged in and these bring their own; they skip with a reason when
# the machine cannot emulate one.
add_executable(rpiboot_usb_device_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/rpiboot_types.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/usb_transport.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/libusb_transport.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/libusb_transport.cpp
    rpiboot_usb_device_test.cpp
)

target_link_libraries(rpiboot_usb_device_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    ${LIBUSB_LIBRARIES}
)

target_include_directories(rpiboot_usb_device_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${LIBUSB_INCLUDE_DIR}
)

target_compile_definitions(rpiboot_usb_device_test PRIVATE
    IMAGER_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")

target_compile_features(rpiboot_usb_device_test PRIVATE cxx_std_20)
target_compile_options(rpiboot_usb_device_test PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
)

# Not rpi_discover_tests: every case here brings the same emulated device up
# and takes it down again, so two of them running at once under `ctest -j`
# would pull the device out from under each other. The resource lock makes
# ctest run them one at a time while still allowing the rest of the suite to
# run alongside.
catch_discover_tests(rpiboot_usb_device_test
    PROPERTIES
        SKIP_RETURN_CODE 4
        RESOURCE_LOCK usb_gadget_emulator
        TIMEOUT 300
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
    # firmware_manager reaches the secure-boot provisioner, which reaches
    # SecureBoot and its crypto backends; curlnetworkconfig reaches
    # PlatformQuirks for the CA bundle. Without these the target did not link
    # at all, so CTest registered it as rpiboot_integration_test_NOT_BUILT and
    # reported a permanent failure, and RunCoverage.cmake warned on every run
    # that not everything had built.
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/secure_boot_provisioner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/bootloader_image.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../secureboot.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../linux/secureboot_crypto_linux.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../linux/bootimgcreator_linux.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../linux/acceleratedcryptographichash_gnutls.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../linux/platformquirks_linux.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperpartition.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperblockcacheentry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperfatpartition.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../file_operations.cpp
    ${PLATFORM_FILE_OPS}
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
# PLATFORM_FILE_OPS brings in the io_uring write path, so the target needs
# liburing on the link line as well as in its sources.
rpi_link_platform_file_ops(rpiboot_integration_test)

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
# FileOperations Write Path
# ============================================================================
# The write path is the most safety-critical code in the imager and, until this
# target, had no test that ran: file_operations.cpp and the platform
# implementation behind it were reachable only from fat_partition_test (tagged
# [.destructive]) and disk_formatter_test (unregistered until recently).
#
# Every case works against a scratch file under a per-process temp directory,
# so it is safe to run unattended and touches no real device. The loopback
# cases attach that scratch file to a loop device to reach the block-device
# paths -- O_DIRECT, real size queries, aligned buffers -- and skip themselves
# where the host will not allow one, which is the normal case in containerised
# CI. Verified against a KVM guest, where the loop cases do run.

add_executable(file_operations_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../file_operations.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../file_operations.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../aligned_buffer.h
    ${PLATFORM_FILE_OPS}
    file_operations_test.cpp
)

# Only the macOS set has a Q_OBJECT type in it (MacFile), so only that build
# needs the moc pass. The other test targets enable AUTOMOC unconditionally and
# pay for a scan that can never find anything.
if(APPLE)
    set_target_properties(file_operations_test PROPERTIES AUTOMOC ON)
endif()

target_include_directories(file_operations_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# Qt6::Core for the QFile/qDebug use inside the platform implementations.
target_link_libraries(file_operations_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
)

if(APPLE)
    target_link_libraries(file_operations_test PRIVATE
        "-framework Security"
        "-framework DiskArbitration"
        "-framework CoreFoundation"
    )
endif()

target_compile_features(file_operations_test PRIVATE cxx_std_20)
target_compile_options(file_operations_test PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
)

rpi_link_platform_file_ops(file_operations_test)
rpi_discover_tests(file_operations_test)

add_custom_target(test_file_operations
    COMMAND file_operations_test
    DEPENDS file_operations_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running file operations write-path tests"
)

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

# ============================================================================
# PerformanceStats
# ============================================================================
# The telemetry recorder: session lifecycle, event timing, per-phase
# throughput sampling and JSON export. It touches neither a device nor the
# network, so all of it is reachable from a plain unit test -- it had simply
# never had one, and its 735 branches were the largest block of genuinely
# testable uncovered code in the tree.

add_executable(performance_stats_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../performancestats.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../performancestats.cpp
    performance_stats_test.cpp
)

set_target_properties(performance_stats_test PROPERTIES AUTOMOC ON)

target_include_directories(performance_stats_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(performance_stats_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
)

target_compile_features(performance_stats_test PRIVATE cxx_std_20)
target_compile_options(performance_stats_test PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
)

rpi_discover_tests(performance_stats_test)

add_custom_target(test_performance_stats
    COMMAND performance_stats_test
    DEPENDS performance_stats_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running PerformanceStats tests"
)

# ============================================================================
# FAT driver, against a filesystem in a scratch file
# ============================================================================
# DeviceWrapperFatPartition is the imager's own FAT implementation -- what
# writes config.txt, cmdline.txt and firstrun.sh onto the boot partition after
# imaging. fat_partition_test above can only reach it through a real mounted
# partition named by FAT_TEST_MOUNT_PATH, so on an ordinary run it exercises
# almost none of it.
#
# This target builds a filesystem with mkfs.vfat in a per-process scratch file
# and drives the same code against that: no device, no mount, no privileges,
# and both FAT16 and FAT32. Cases skip themselves where mkfs.vfat is absent.

add_executable(fat_partition_image_test
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
    fat_partition_image_test.cpp
)

set_target_properties(fat_partition_image_test PROPERTIES AUTOMOC ON)

target_include_directories(fat_partition_image_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(fat_partition_image_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
)

if(APPLE)
    target_link_libraries(fat_partition_image_test PRIVATE
        "-framework Security"
        "-framework DiskArbitration"
        "-framework CoreFoundation"
    )
endif()

rpi_link_platform_file_ops(fat_partition_image_test)

target_compile_features(fat_partition_image_test PRIVATE cxx_std_20)

rpi_discover_tests(fat_partition_image_test)

add_custom_target(test_fat_partition_image
    COMMAND fat_partition_image_test
    DEPENDS fat_partition_image_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running FAT driver tests against a scratch filesystem"
)

# ============================================================================
# SystemMemoryManager
# ============================================================================
# Sizes every buffer in the write path. The memory-tier branches read the
# host's real RAM and so are not selectable from a test, but the
# parameterised sizing -- verify buffer by file size, ring buffer slots by
# slot size, async queue depth by block size -- is fully drivable.

add_executable(system_memory_manager_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../systemmemorymanager.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../systemmemorymanager.cpp
    system_memory_manager_test.cpp
)

target_include_directories(system_memory_manager_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(system_memory_manager_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
)

target_compile_features(system_memory_manager_test PRIVATE cxx_std_20)

rpi_discover_tests(system_memory_manager_test)

# ============================================================================
# SecureBoot
# ============================================================================
# Assembles and signs the boot image a locked-down Pi will accept. Testable
# without hardware because the Linux signing path shells out to openssl, so a
# throwaway key suffices, and extractFatPartitionFiles() takes a
# DeviceWrapperFatPartition that the FAT scratch-image fixture can build.
# Cases needing openssl or mkfs.vfat skip themselves.

add_executable(secureboot_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../secureboot.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../secureboot.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../linux/secureboot_crypto_linux.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../linux/bootimgcreator_linux.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../linux/acceleratedcryptographichash_gnutls.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperpartition.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperblockcacheentry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperfatpartition.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../file_operations.cpp
    ${PLATFORM_FILE_OPS}
    secureboot_test.cpp
)

set_target_properties(secureboot_test PROPERTIES AUTOMOC ON)

target_include_directories(secureboot_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(secureboot_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    ${GNUTLS_LIBRARIES}
)

target_include_directories(secureboot_test PRIVATE ${GNUTLS_INCLUDE_DIRS})

rpi_link_platform_file_ops(secureboot_test)

target_compile_features(secureboot_test PRIVATE cxx_std_20)

rpi_discover_tests(secureboot_test)

# ============================================================================
# DownloadThread
# ============================================================================
# The write path: curl in, hashed, onto the target. 1,958 branches, the
# largest uncovered block in the tree and the most safety-critical.
#
# Drivable without a device or a network because curl in this build carries
# the file:// protocol -- so a scratch file serves the image -- and
# _openAndPrepareDevice() only unmounts when the destination starts with
# "/dev/", so a scratch image takes the same open-and-write path a card
# would. Nothing is mocked; the real transfer loop runs end to end.

# QtConcurrent and QtTest are not among the components the app itself asks
# for, so this target finds them on its own.
find_package(Qt6 REQUIRED COMPONENTS Concurrent)

# QuickTest drives the QML controls. The Qt we ship is built without testlib
# (qt/features_exclude.list), so this is optional: found, the driven UI tests
# are built; absent, they are skipped and the rest of the suite is unaffected.
find_package(Qt6 QUIET COMPONENTS QuickTest)

# The write pipeline links as one unit -- DownloadThread reaches the device
# wrapper, the memory manager, platform quirks, the cache writer and the
# secure-boot helpers -- so both targets below share the list.
set(RPI_DOWNLOAD_PIPELINE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../downloadthread.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperpartition.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperblockcacheentry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperfatpartition.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../file_operations.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../systemmemorymanager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../imageadvancedoptions.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../secureboot.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../curlnetworkconfig.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../fastboot/sparse_encoder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../asynccachewriter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../linux/platformquirks_linux.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../linux/secureboot_crypto_linux.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../linux/bootimgcreator_linux.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../linux/acceleratedcryptographichash_gnutls.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../drivelist/drivelist_linux.cpp
    ${PLATFORM_FILE_OPS}
)

# ── bmap ────────────────────────────────────────────────────────────────────
# Decides which blocks get written. A block wrongly reported unmapped is
# never written and leaves a hole in the filesystem on the card.
add_executable(bmap_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../fastboot/bmap.cpp
    bmap_test.cpp
)

target_include_directories(bmap_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(bmap_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
)

target_compile_features(bmap_test PRIVATE cxx_std_20)

rpi_discover_tests(bmap_test)

# ── image size estimation ───────────────────────────────────────────────────
# Decides whether an image is accepted for a given card, so a wrong answer
# either rejects a good image or lets a doomed write start.
add_executable(image_size_parser_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../imagesizeparser.cpp
    image_size_parser_test.cpp
)

target_include_directories(image_size_parser_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${LibArchive_INCLUDE_DIR}
    ${LIBLZMA_INCLUDE_DIRS}
)

target_link_libraries(image_size_parser_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    ${LibArchive_LIBRARIES}
    ${LIBLZMA_LIBRARIES}
    ${ZSTD_LIBRARIES}
)

target_compile_features(image_size_parser_test PRIVATE cxx_std_20)

rpi_discover_tests(image_size_parser_test)

# ── drive format thread ─────────────────────────────────────────────────────
# The "Erase" entry: refusing safely, and the message shown when it fails.
add_executable(drive_format_thread_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../driveformatthread.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../disk_formatter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../file_operations.cpp
    ${PLATFORM_FILE_OPS}
    ${DRIVELIST_PLATFORM_SOURCES}
    ${PLATFORMQUIRKS_SOURCES}
    drive_format_thread_test.cpp
)

set_target_properties(drive_format_thread_test PROPERTIES AUTOMOC ON)

target_include_directories(drive_format_thread_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(drive_format_thread_test PRIVATE
    Catch2::Catch2
    Qt6::Core
    ${PLATFORMQUIRKS_LIBS}
)

rpi_link_platform_file_ops(drive_format_thread_test)

target_compile_features(drive_format_thread_test PRIVATE cxx_std_20)

rpi_discover_tests(drive_format_thread_test)

# ── ImageWriter and the models behind the pickers ───────────────────────────
#
# The QML-facing backend. It was excluded from coverage as unreachable, which
# was only true of the Qt that happened to be installed: it needs no QML
# engine -- it builds its models as members and leaves the engine null -- so a
# QGuiApplication on the offscreen platform reaches all of it.
#
# imagewriter.cpp, hwlistmodel.cpp and oslistmodel.cpp are compiled into the
# QML module rather than into SOURCES for a GUI build, so they are named here
# explicitly alongside the rest of the application's sources. main.cpp is the
# only one left out; this target supplies its own.
if(NOT BUILD_CLI_ONLY)
    # SOURCES is the application's own final list, so this target cannot
    # drift from what actually ships. main.cpp is dropped because this binary
    # supplies its own.
    set(IMAGE_WRITER_TEST_SOURCES)
    foreach(_src IN LISTS SOURCES)
        # .qrc is deliberately included: the timezone, country and keymap
        # lists the customisation dialog is built from are Qt resources, and
        # a library without them hands back empty lists that look like a
        # product bug rather than a missing resource.
        if(_src MATCHES "\\.(cpp|cc|c|mm|qrc)$" AND NOT _src MATCHES "(^|/)main\\.cpp$")
            if(IS_ABSOLUTE "${_src}")
                list(APPEND IMAGE_WRITER_TEST_SOURCES "${_src}")
            else()
                list(APPEND IMAGE_WRITER_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/../${_src}")
            endif()
        endif()
    endforeach()

    # ...and the QML-declared types, which a GUI build compiles into the QML
    # module rather than into SOURCES.
    set(IMAGER_QML_CPP_TYPES_ABS)
    foreach(_src IN LISTS IMAGER_QML_CPP_TYPES)
        list(APPEND IMAGER_QML_CPP_TYPES_ABS "${CMAKE_CURRENT_SOURCE_DIR}/../${_src}")
    endforeach()

    # One static library for the whole application, shared by every test
    # target that needs it.
    #
    # Listing the sources per target meant compiling the same hundred files
    # once for each -- and, under coverage instrumentation, meant several
    # objects for the same source each writing its own .gcda. That is the
    # "overwriting an existing profile data with a different checksum"
    # warning: the counts were being split across duplicate objects and then
    # merged back by gcovr. One object per source removes both problems.
    add_library(rpi_imager_testable STATIC
        ${IMAGE_WRITER_TEST_SOURCES}
        ${IMAGER_QML_CPP_TYPES_ABS}
        ${DEPENDENCIES}
    )

    set_target_properties(rpi_imager_testable PROPERTIES AUTOMOC ON AUTORCC ON)

    # PUBLIC throughout: consumers are test binaries that need the same
    # headers and the same libraries, and should not have to restate them.
    target_include_directories(rpi_imager_testable PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${GNUTLS_INCLUDE_DIRS}
        ${LIBLZMA_INCLUDE_DIRS}
        ${LibArchive_INCLUDE_DIR}
        ${LIBUSB_INCLUDE_DIR}
    )

    target_link_libraries(rpi_imager_testable PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::Svg
        Qt6::Network
        Qt6::Concurrent
        CURL::libcurl
        ${LibArchive_LIBRARIES}
        ${LIBLZMA_LIBRARIES}
        ${ZSTD_LIBRARIES}
        ${GNUTLS_LIBRARIES}
        ${LIBUSB_LIBRARIES}
        ${CMAKE_DL_LIBS}
    )

    # customization_generator hashes passwords through the bundled yescrypt.
    if(TARGET yescrypt)
        target_link_libraries(rpi_imager_testable PUBLIC yescrypt)
    endif()

    # The Linux URI handler is a DBus adaptor, so the same guard the
    # application uses applies here.
    if(TARGET Qt6::DBus)
        target_link_libraries(rpi_imager_testable PUBLIC Qt6::DBus)
    endif()

    rpi_link_platform_file_ops(rpi_imager_testable)

    target_compile_features(rpi_imager_testable PUBLIC cxx_std_20)

    add_executable(image_writer_test image_writer_test.cpp)
    set_target_properties(image_writer_test PROPERTIES AUTOMOC ON)
    target_link_libraries(image_writer_test PRIVATE Catch2::Catch2 rpi_imager_testable)
    target_compile_definitions(image_writer_test PRIVATE
        IMAGER_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
    rpi_discover_tests(image_writer_test)

    # The picker models: same library, its own cases.
    add_executable(picker_models_test picker_models_test.cpp)
    set_target_properties(picker_models_test PROPERTIES AUTOMOC ON)
    target_link_libraries(picker_models_test PRIVATE Catch2::Catch2 rpi_imager_testable)
    rpi_discover_tests(picker_models_test)

    # Does the user interface load at all?
    add_executable(qml_load_test qml_load_test.cpp)
    set_target_properties(qml_load_test PROPERTIES AUTOMOC ON)
    target_link_libraries(qml_load_test PRIVATE Catch2::Catch2 rpi_imager_testable Qt6::Quick)
    target_compile_definitions(qml_load_test PRIVATE
        IMAGER_QML_MODULE_PARENT="${CMAKE_BINARY_DIR}")
    rpi_discover_tests(qml_load_test)

    # Driven tests for the controls themselves: real clicks and keystrokes
    # against real instances. Needs a Qt built with testlib -- the Qt we ship
    # excludes it, so this target simply does not exist against that build.
    if(TARGET Qt6::QuickTest)
        add_executable(qml_ui_test qml_ui_test.cpp)
        set_target_properties(qml_ui_test PROPERTIES AUTOMOC ON)
        target_link_libraries(qml_ui_test PRIVATE
            rpi_imager_testable Qt6::Quick Qt6::QuickTest)
        target_compile_definitions(qml_ui_test PRIVATE
            IMAGER_QML_MODULE_PARENT="${CMAKE_BINARY_DIR}"
            QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml")
        add_test(NAME qml_ui_test
                 COMMAND qml_ui_test -platform offscreen)
        set_tests_properties(qml_ui_test PROPERTIES
            ENVIRONMENT "QT_QUICK_BACKEND=software")
    else()
        message(STATUS "Qt6::QuickTest not found -- skipping driven QML tests")
    endif()

    # Saving a copy of the image while it downloads.
    add_executable(async_cache_writer_test async_cache_writer_test.cpp)
    set_target_properties(async_cache_writer_test PROPERTIES AUTOMOC ON)
    target_link_libraries(async_cache_writer_test PRIVATE Catch2::Catch2 rpi_imager_testable)
    rpi_discover_tests(async_cache_writer_test)

    # The one place every libcurl handle is configured.
    add_executable(curl_network_config_test curl_network_config_test.cpp)
    set_target_properties(curl_network_config_test PROPERTIES AUTOMOC ON)
    target_link_libraries(curl_network_config_test PRIVATE Catch2::Catch2 rpi_imager_testable)
    rpi_discover_tests(curl_network_config_test)

    # Keeping the drive chooser up to date.
    add_executable(drive_poll_thread_test drive_poll_thread_test.cpp)
    set_target_properties(drive_poll_thread_test PROPERTIES AUTOMOC ON)
    target_link_libraries(drive_poll_thread_test PRIVATE Catch2::Catch2 rpi_imager_testable)
    rpi_discover_tests(drive_poll_thread_test)

    # Every icon in the OS chooser comes through this.
    add_executable(icon_fetcher_test icon_fetcher_test.cpp)
    set_target_properties(icon_fetcher_test PROPERTIES AUTOMOC ON)
    target_link_libraries(icon_fetcher_test PRIVATE Catch2::Catch2 rpi_imager_testable)
    rpi_discover_tests(icon_fetcher_test)

    # The stall detector behind "it stopped at 47%".
    add_executable(write_progress_watchdog_test write_progress_watchdog_test.cpp)
    set_target_properties(write_progress_watchdog_test PROPERTIES AUTOMOC ON)
    target_link_libraries(write_progress_watchdog_test PRIVATE Catch2::Catch2 rpi_imager_testable)
    rpi_discover_tests(write_progress_watchdog_test)
endif()

# ── OS list parsing ─────────────────────────────────────────────────────────
# Pure JSON transformations that decide what the user sees in the OS chooser.
add_executable(oslist_parser_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../oslistparser.cpp
    oslist_parser_test.cpp
)

target_include_directories(oslist_parser_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(oslist_parser_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
)

target_compile_features(oslist_parser_test PRIVATE cxx_std_20)

rpi_discover_tests(oslist_parser_test)

# ── config.txt merging ──────────────────────────────────────────────────────
# Header-only, and shared by both write paths so they cannot drift apart
# again -- drifting apart is how the substring bug came to exist in two
# places at once.
add_executable(config_txt_merge_test config_txt_merge_test.cpp)
target_link_libraries(config_txt_merge_test PRIVATE Catch2::Catch2WithMain Qt6::Core)
target_include_directories(config_txt_merge_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(config_txt_merge_test PRIVATE cxx_std_20)
rpi_discover_tests(config_txt_merge_test)

# ── rpiboot thread ──────────────────────────────────────────────────────────
# The CM4/CM5 sideload sequence. Reachable since the bus went behind
# IUsbContext; before that every line of it needed a compute module in boot
# mode plugged in.
# rpibootthread.cpp is already in rpi_imager_testable; listing it here too
# gives two copies of its moc output and a duplicate-symbol link failure.
add_executable(rpiboot_thread_test rpiboot_thread_test.cpp)
set_target_properties(rpiboot_thread_test PROPERTIES AUTOMOC ON)
target_link_libraries(rpiboot_thread_test PRIVATE Catch2::Catch2 rpi_imager_testable)
rpi_discover_tests(rpiboot_thread_test)

# ── fastboot flash thread ───────────────────────────────────────────────────
# Covers the erase path: the most destructive operation in the fastboot flow.
add_executable(fastboot_flash_thread_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../fastbootflashthread.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../fastboot/fastboot_protocol.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../fastboot/sparse_encoder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../fastboot/bmap.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../fastboot/pieeprom.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../fastboot/eeprom_signer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/libusb_transport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../connect_device_registrar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../curlnetworkconfig.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../systemmemorymanager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../linux/acceleratedcryptographichash_gnutls.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../linux/secureboot_crypto_linux.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../linux/platformquirks_linux.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../ringbuffer.cpp
    fastboot_flash_thread_test.cpp
)

set_target_properties(fastboot_flash_thread_test PROPERTIES AUTOMOC ON)

target_include_directories(fastboot_flash_thread_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${GNUTLS_INCLUDE_DIRS}
    ${LIBUSB_INCLUDE_DIR}
    ${LibArchive_INCLUDE_DIR}
)

# Catch2 without its main(): this target provides its own so a
# QCoreApplication exists before any case runs (see the bottom of the test).
target_link_libraries(fastboot_flash_thread_test PRIVATE
    Catch2::Catch2
    Qt6::Core
    Qt6::Network
    Qt6::Concurrent
    CURL::libcurl
    ${LibArchive_LIBRARIES}
    ${GNUTLS_LIBRARIES}
    ${LIBUSB_LIBRARIES}
)

target_compile_features(fastboot_flash_thread_test PRIVATE cxx_std_20)

rpi_discover_tests(fastboot_flash_thread_test)

add_executable(download_thread_test
    ${RPI_DOWNLOAD_PIPELINE_SOURCES}
    download_thread_test.cpp
)

set_target_properties(download_thread_test PROPERTIES AUTOMOC ON)

target_include_directories(download_thread_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${GNUTLS_INCLUDE_DIRS}
)

# Catch2 without its main(): this target provides its own so a
# QCoreApplication exists before any case runs (see the bottom of the test).
target_link_libraries(download_thread_test PRIVATE
    Catch2::Catch2
    Qt6::Core
    Qt6::Network
    Qt6::Concurrent
    CURL::libcurl
    ${GNUTLS_LIBRARIES}
)

rpi_link_platform_file_ops(download_thread_test)

target_compile_features(download_thread_test PRIVATE cxx_std_20)

rpi_discover_tests(download_thread_test)

# ============================================================================
# DownloadExtractThread
# ============================================================================
# The compressed-image path: the same write pipeline with libarchive in front
# of it, so the bytes going to the card are the decompressed ones. 759
# branches, none of them covered.
#
# Driven the same way as DownloadThread -- a file:// URL and a scratch image
# for a destination -- with the archives built at test time by xz, gzip and
# zip, so the decompressors are fed real containers rather than fixtures
# checked into the tree.

add_executable(download_extract_thread_test
    ${RPI_DOWNLOAD_PIPELINE_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/../downloadextractthread.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../localfileextractthread.cpp
    # The stub inhibitor rather than the DBus one: nothing here needs to stop
    # the machine suspending, and the real implementation wants a session bus.
    ${CMAKE_CURRENT_SOURCE_DIR}/../suspend_inhibitor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../suspend_inhibitor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../linux/suspend_inhibitor_stub.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../ringbuffer.cpp
    download_extract_thread_test.cpp
)

set_target_properties(download_extract_thread_test PROPERTIES AUTOMOC ON)

target_include_directories(download_extract_thread_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${GNUTLS_INCLUDE_DIRS}
    ${LibArchive_INCLUDE_DIR}
)

# Catch2 without its main(): this target provides its own so a
# QCoreApplication exists before any case runs.
target_link_libraries(download_extract_thread_test PRIVATE
    Catch2::Catch2
    Qt6::Core
    Qt6::Network
    Qt6::Concurrent
    CURL::libcurl
    ${LibArchive_LIBRARIES}
    ${ZSTD_LIBRARIES}
    ${LIBLZMA_LIBRARIES}
    ${GNUTLS_LIBRARIES}
)

rpi_link_platform_file_ops(download_extract_thread_test)

target_compile_features(download_extract_thread_test PRIVATE cxx_std_20)

rpi_discover_tests(download_extract_thread_test)

# ============================================================================
# CacheManager
# ============================================================================
# Decides whether a previously downloaded image can be reused. A false
# positive here writes the wrong image to a card, so the negative cases carry
# most of the weight.
#
# Runs under QStandardPaths test mode so the cache directory is a throwaway
# under the test tree: no case can find, trust or delete a real cached image.
# Verification happens on a worker thread, so this target supplies its own
# main() with a QCoreApplication.

add_executable(cache_manager_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../cachemanager.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../cachemanager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../systemmemorymanager.cpp
    cache_manager_test.cpp
)

set_target_properties(cache_manager_test PROPERTIES AUTOMOC ON)

target_include_directories(cache_manager_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(cache_manager_test PRIVATE
    Catch2::Catch2
    Qt6::Core
)

target_compile_features(cache_manager_test PRIVATE cxx_std_20)

rpi_discover_tests(cache_manager_test)

# ============================================================================
# CurlFetcher
# ============================================================================
# How the OS list and its sublists are retrieved. Reporting success on a 404
# hands the caller an HTML error page to parse as JSON, and never reporting at
# all leaves the UI waiting, so the failure cases carry the weight here.
#
# Driven against a throwaway HTTP server on 127.0.0.1 bound to port 0: real
# requests and real status handling, no network, no fixed port.

add_executable(curl_fetcher_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../curlfetcher.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../curlfetcher.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../curlnetworkconfig.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../linux/platformquirks_linux.cpp
    curl_fetcher_test.cpp
)

set_target_properties(curl_fetcher_test PROPERTIES AUTOMOC ON)

target_include_directories(curl_fetcher_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(curl_fetcher_test PRIVATE
    Catch2::Catch2
    Qt6::Core
    Qt6::Network
    CURL::libcurl
)

target_compile_features(curl_fetcher_test PRIVATE cxx_std_20)

rpi_discover_tests(curl_fetcher_test)

# ============================================================================
# SecureBootProvisioner
# ============================================================================
# Burns a customer key hash into a Compute Module's OTP and signs the firmware
# it will accept afterwards. OTP is write-once, so a wrong key hash bricks the
# module permanently -- which makes these the highest-consequence functions in
# the tree and they had no tests.
#
# provision() needs a device on the USB bus, but key generation, the OTP hash,
# boot-image signing and signed-recovery preparation all work on files and are
# covered here. The OTP hash is cross-checked against openssl rather than
# against a value this code produced.

add_executable(secure_boot_provisioner_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/secure_boot_provisioner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/bootloader_image.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../secureboot.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../linux/secureboot_crypto_linux.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../linux/bootimgcreator_linux.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../linux/acceleratedcryptographichash_gnutls.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperpartition.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperblockcacheentry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperfatpartition.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../file_operations.cpp
    ${PLATFORM_FILE_OPS}
    secure_boot_provisioner_test.cpp
)

set_target_properties(secure_boot_provisioner_test PROPERTIES AUTOMOC ON)

target_include_directories(secure_boot_provisioner_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${GNUTLS_INCLUDE_DIRS}
)

target_link_libraries(secure_boot_provisioner_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    ${GNUTLS_LIBRARIES}
)

rpi_link_platform_file_ops(secure_boot_provisioner_test)

target_compile_features(secure_boot_provisioner_test PRIVATE cxx_std_20)

rpi_discover_tests(secure_boot_provisioner_test)

# ============================================================================
# DownloadStatsTelemetry
# ============================================================================
# Reports which image was written. Two properties matter more than the
# reporting: it must send nothing when the user has opted out, and it must
# never be able to fail a write -- it runs fire-and-forget after the card is
# already done.
#
# Both are testable without the real endpoint: the opt-out is a QSettings
# value and the POST goes wherever it is pointed, so a throwaway server on
# 127.0.0.1 stands in. Nothing here contacts Raspberry Pi, and the settings
# are scoped to a test-mode file so no case can switch on a developer's real
# telemetry preference.

add_executable(download_stats_telemetry_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../downloadstatstelemetry.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../downloadstatstelemetry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../curlnetworkconfig.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../linux/platformquirks_linux.cpp
    download_stats_telemetry_test.cpp
)

set_target_properties(download_stats_telemetry_test PROPERTIES AUTOMOC ON)

target_include_directories(download_stats_telemetry_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(download_stats_telemetry_test PRIVATE
    Catch2::Catch2
    Qt6::Core
    Qt6::Network
    CURL::libcurl
)

target_compile_features(download_stats_telemetry_test PRIVATE cxx_std_20)

rpi_discover_tests(download_stats_telemetry_test)

# ============================================================================
# ConnectDeviceRegistrar
# ============================================================================
# Enrols a board into Raspberry Pi Connect. It looked like it needed hardware
# and the live API; it needs neither. The device side is an IUsbTransport, and
# rpiboot/test/mock_usb_transport.h already provides one with fault injection
# built in; the API side takes a baseUrl that can point at 127.0.0.1.
#
# Nothing here contacts Raspberry Pi Connect and no real credential is used.

add_executable(connect_device_registrar_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../connect_device_registrar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../fastboot/fastboot_protocol.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../curlnetworkconfig.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../linux/platformquirks_linux.cpp
    connect_device_registrar_test.cpp
)

set_target_properties(connect_device_registrar_test PROPERTIES AUTOMOC ON)

target_include_directories(connect_device_registrar_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(connect_device_registrar_test PRIVATE
    Catch2::Catch2
    Qt6::Core
    Qt6::Network
    CURL::libcurl
)

target_compile_features(connect_device_registrar_test PRIVATE cxx_std_20)

rpi_discover_tests(connect_device_registrar_test)

# ============================================================================
# FirmwareManager
# ============================================================================
# Decides which firmware a board needs and whether what is already cached can
# be trusted for it. The second question is the dangerous one: accepting a
# cache directory that is incomplete, zero-length, or populated for a
# different chip sideloads the wrong firmware to a Compute Module.
#
# The only public way in is ensureAvailable(), which downloads first, so the
# cache logic was unreachable. Those helpers are protected rather than private
# now and the test subclasses to reach them -- all filesystem work against a
# scratch cache, no network.

add_executable(firmware_manager_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/firmware_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/bootcode_loader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/bootfiles.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/secure_boot_provisioner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/bootloader_image.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../rpiboot/rpiboot_protocol.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../secureboot.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../curlnetworkconfig.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../linux/secureboot_crypto_linux.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../linux/bootimgcreator_linux.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../linux/acceleratedcryptographichash_gnutls.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../linux/platformquirks_linux.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperpartition.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperblockcacheentry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../devicewrapperfatpartition.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../file_operations.cpp
    ${PLATFORM_FILE_OPS}
    firmware_manager_test.cpp
)

set_target_properties(firmware_manager_test PROPERTIES AUTOMOC ON)

target_include_directories(firmware_manager_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${GNUTLS_INCLUDE_DIRS}
    ${LibArchive_INCLUDE_DIR}
)

target_link_libraries(firmware_manager_test PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    CURL::libcurl
    ${LibArchive_LIBRARIES}
    ${ZSTD_LIBRARIES}
    ${LIBLZMA_LIBRARIES}
    ${GNUTLS_LIBRARIES}
)

rpi_link_platform_file_ops(firmware_manager_test)

target_compile_features(firmware_manager_test PRIVATE cxx_std_20)

rpi_discover_tests(firmware_manager_test)
