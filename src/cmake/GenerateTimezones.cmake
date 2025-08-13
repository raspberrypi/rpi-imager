# GenerateTimezones.cmake
# CMake script to download IANA timezone data and generate timezone list
# Called with:
# -DTZDATA_URL=<url> -DTZDATA_DIR=<dir> -DOUTPUT_FILE=<file> -DSOURCE_DIR=<dir>

cmake_minimum_required(VERSION 3.22)

# Validate required parameters
if(NOT DEFINED TZDATA_URL)
    message(FATAL_ERROR "TZDATA_URL must be defined")
endif()

if(NOT DEFINED TZDATA_DIR)
    message(FATAL_ERROR "TZDATA_DIR must be defined")
endif()

if(NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "OUTPUT_FILE must be defined")
endif()

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR must be defined")
endif()

set(TZDATA_ARCHIVE "${TZDATA_DIR}/tzdata.tar.gz")
set(ZONE_TAB_FILE "${TZDATA_DIR}/zone.tab")

message(STATUS "Downloading IANA timezone data from ${TZDATA_URL}")

# Download the timezone data
file(DOWNLOAD "${TZDATA_URL}" "${TZDATA_ARCHIVE}" 
     SHOW_PROGRESS
     STATUS download_status
     LOG download_log
     TIMEOUT 30)

# Check if download was successful
list(GET download_status 0 download_error)
if(download_error)
    list(GET download_status 1 download_message)
    message(WARNING "Failed to download timezone data: ${download_message}")
    message(STATUS "Attempting to use fallback timezone list...")
    
    # Try to find a fallback timezone file
    set(FALLBACK_FILE "${SOURCE_DIR}/timezones.txt.fallback")
    if(EXISTS "${FALLBACK_FILE}")
        message(STATUS "Using fallback timezone file: ${FALLBACK_FILE}")
        file(READ "${FALLBACK_FILE}" fallback_content)
        file(WRITE "${OUTPUT_FILE}" "${fallback_content}")
        message(STATUS "Fallback timezone list copied successfully")
        return()
    else()
        message(FATAL_ERROR "No fallback timezone file found at ${FALLBACK_FILE}")
    endif()
endif()

message(STATUS "Extracting timezone data...")

# Extract the archive
execute_process(
    COMMAND ${CMAKE_COMMAND} -E tar xzf "${TZDATA_ARCHIVE}"
    WORKING_DIRECTORY "${TZDATA_DIR}"
    RESULT_VARIABLE extract_result
    OUTPUT_VARIABLE extract_output
    ERROR_VARIABLE extract_error
)

if(extract_result)
    message(FATAL_ERROR "Failed to extract timezone data: ${extract_error}")
endif()

# Check if zone.tab exists
if(NOT EXISTS "${ZONE_TAB_FILE}")
    message(FATAL_ERROR "zone.tab file not found in extracted data")
endif()

message(STATUS "Parsing timezone names from zone.tab...")

# Read zone.tab file
file(READ "${ZONE_TAB_FILE}" zone_tab_content)

# Split into lines
string(REPLACE "\n" ";" zone_lines "${zone_tab_content}")

set(timezone_list "")

# Process each line
foreach(line ${zone_lines})
    # Skip empty lines and comments
    string(STRIP "${line}" line)
    if(line AND NOT line MATCHES "^#")
        # zone.tab format: country_code coordinates timezone_name [comments]
        # We want the third column (timezone_name)
        string(REPLACE "\t" ";" line_parts "${line}")
        list(LENGTH line_parts num_parts)
        
        if(num_parts GREATER_EQUAL 3)
            list(GET line_parts 2 timezone_name)
            # Remove any trailing comments after the timezone name
            string(REGEX REPLACE "[ \t].*$" "" timezone_name "${timezone_name}")
            string(STRIP "${timezone_name}" timezone_name)
            
            if(timezone_name)
                list(APPEND timezone_list "${timezone_name}")
            endif()
        endif()
    endif()
endforeach()

# Add some additional timezones that might not be in zone.tab but are commonly used
list(APPEND timezone_list 
    "UTC"
    "GMT"
    "GMT+0"
    "GMT-0"
    "GMT0"
    "Greenwich"
    "Universal"
    "Zulu"
    "Etc/GMT"
    "Etc/GMT+0"
    "Etc/GMT-0"
    "Etc/UTC"
    "Etc/Universal"
    "Etc/Zulu"
)

# Remove duplicates and sort
list(REMOVE_DUPLICATES timezone_list)
list(SORT timezone_list)

# Write to output file
set(output_content "")
foreach(timezone ${timezone_list})
    set(output_content "${output_content}${timezone}\n")
endforeach()

file(WRITE "${OUTPUT_FILE}" "${output_content}")

list(LENGTH timezone_list num_timezones)
message(STATUS "Generated ${num_timezones} timezone entries in ${OUTPUT_FILE}")

# Compare with fallback file if it exists
set(FALLBACK_FILE "${SOURCE_DIR}/timezones.txt.fallback")
if(EXISTS "${FALLBACK_FILE}")
    message(STATUS "Comparing with existing timezone list...")
    
    # Read fallback file
    file(READ "${FALLBACK_FILE}" fallback_content)
    string(REPLACE "\n" ";" fallback_lines "${fallback_content}")
    
    # Clean up fallback list (remove empty lines)
    set(fallback_list "")
    foreach(line ${fallback_lines})
        string(STRIP "${line}" line)
        if(line)
            list(APPEND fallback_list "${line}")
        endif()
    endforeach()
    
    # Sort fallback list for comparison
    list(SORT fallback_list)
    
    # Find differences
    set(added_timezones "")
    set(removed_timezones "")
    
    # Find timezones in new list but not in fallback (added)
    foreach(tz ${timezone_list})
        list(FIND fallback_list "${tz}" found_index)
        if(found_index EQUAL -1)
            list(APPEND added_timezones "${tz}")
        endif()
    endforeach()
    
    # Find timezones in fallback but not in new list (removed)
    foreach(tz ${fallback_list})
        list(FIND timezone_list "${tz}" found_index)
        if(found_index EQUAL -1)
            list(APPEND removed_timezones "${tz}")
        endif()
    endforeach()
    
    # Print comparison results
    list(LENGTH fallback_list fallback_count)
    list(LENGTH added_timezones added_count)
    list(LENGTH removed_timezones removed_count)
    
    message(STATUS "")
    message(STATUS "=== TIMEZONE LIST COMPARISON ===")
    message(STATUS "Fallback file: ${fallback_count} timezones")
    message(STATUS "Generated:     ${num_timezones} timezones")
    message(STATUS "Added:         ${added_count} timezones")
    message(STATUS "Removed:       ${removed_count} timezones")
    
    if(added_count GREATER 0)
        message(STATUS "")
        message(STATUS "ADDED TIMEZONES:")
        foreach(tz ${added_timezones})
            message(STATUS "  + ${tz}")
        endforeach()
    endif()
    
    if(removed_count GREATER 0)
        message(STATUS "")
        message(STATUS "REMOVED TIMEZONES:")
        foreach(tz ${removed_timezones})
            message(STATUS "  - ${tz}")
        endforeach()
    endif()
    
    if(added_count EQUAL 0 AND removed_count EQUAL 0)
        message(STATUS "✅ No differences found - timezone lists are identical!")
    else()
        math(EXPR net_change "${added_count} - ${removed_count}")
        if(net_change GREATER 0)
            message(STATUS "📈 Net change: +${net_change} timezones")
        elseif(net_change LESS 0)
            math(EXPR abs_change "0 - ${net_change}")
            message(STATUS "📉 Net change: -${abs_change} timezones")
        else()
            message(STATUS "🔄 Net change: 0 timezones (equal additions and removals)")
        endif()
    endif()
    message(STATUS "=================================")
    message(STATUS "")
else()
    message(STATUS "No fallback file found for comparison")
endif()

# Clean up downloaded archive to save space
file(REMOVE "${TZDATA_ARCHIVE}")

message(STATUS "Timezone generation completed successfully")