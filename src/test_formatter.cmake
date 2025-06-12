# Simple test build for DiskFormatter
# This can be included in the main CMakeLists.txt or used standalone

# Add the disk formatter executable test
if(BUILD_TESTING)
    add_executable(disk_formatter_test
        disk_formatter.h
        disk_formatter.cpp
        disk_formatter_test.cpp
    )

    target_compile_features(disk_formatter_test PRIVATE cxx_std_20)
    target_compile_options(disk_formatter_test PRIVATE 
        -Wall -Wextra -Wpedantic
        $<$<CONFIG:Debug>:-g -O0>
        $<$<CONFIG:Release>:-O3 -DNDEBUG>
    )
    
    # For testing, we can run this manually
    add_custom_target(test_disk_formatter
        COMMAND disk_formatter_test
        DEPENDS disk_formatter_test
        COMMENT "Running disk formatter tests"
    )
endif() 