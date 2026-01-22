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

# Add the customization generator test executable
add_executable(customization_generator_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../customization_generator.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../customization_generator.cpp
    customization_generator_test.cpp
)

# Link against Catch2 and Qt
target_link_libraries(customization_generator_test PRIVATE 
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Network
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
catch_discover_tests(customization_generator_test)

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

# Register with CTest for automatic test discovery
# Note: These tests are marked with [!mayfail] and [.destructive] tags
# Run with: ctest -C Debug --output-on-failure
# Or with environment variable: FAT_TEST_MOUNT_PATH="/Volumes/bootfs 1" ctest
catch_discover_tests(fat_partition_test)

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
catch_discover_tests(drivelist_test)

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
catch_discover_tests(platformquirks_test)

# Custom target for manual running
add_custom_target(test_platformquirks
    COMMAND platformquirks_test
    DEPENDS platformquirks_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running platformquirks tests"
)

