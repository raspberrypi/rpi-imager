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

