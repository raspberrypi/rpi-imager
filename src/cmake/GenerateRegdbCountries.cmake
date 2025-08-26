cmake_minimum_required(VERSION 3.22)

# GenerateRegdbCountries.cmake
# CMake script to download wireless-regdb db.txt and generate a list of
# two-letter regulatory domain codes. Falls back to countries.txt in source
# tree if download fails.
#
# Called with:
# -DREGDB_URL=<url> -DOUTPUT_FILE=<file> -DSOURCE_DIR=<dir>

if(NOT DEFINED REGDB_URL)
    message(FATAL_ERROR "REGDB_URL must be defined")
endif()

if(NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "OUTPUT_FILE must be defined")
endif()

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR must be defined")
endif()

set(TMP_DIR "${CMAKE_CURRENT_BINARY_DIR}")
set(REGDB_FILE "${TMP_DIR}/wireless-regdb-db.txt")

message(STATUS "Downloading wireless-regdb from ${REGDB_URL}")

file(DOWNLOAD "${REGDB_URL}" "${REGDB_FILE}"
     SHOW_PROGRESS
     STATUS dl_status
     LOG dl_log
     TIMEOUT 30)

list(GET dl_status 0 dl_error)
if(dl_error)
    list(GET dl_status 1 dl_message)
    message(WARNING "Failed to download wireless-regdb: ${dl_message}")
    message(STATUS "Using fallback countries list from source tree")

    set(FALLBACK_FILE "${SOURCE_DIR}/countries.txt")
    if(EXISTS "${FALLBACK_FILE}")
        file(READ "${FALLBACK_FILE}" fallback_content)
        file(WRITE "${OUTPUT_FILE}" "${fallback_content}")
        message(STATUS "Fallback countries list copied to ${OUTPUT_FILE}")
        return()
    else()
        message(FATAL_ERROR "No fallback countries file found at ${FALLBACK_FILE}")
    endif()
endif()

# Parse db.txt for lines like: country XX:
file(READ "${REGDB_FILE}" regdb_content)
string(REPLACE "\n" ";" regdb_lines "${regdb_content}")

set(code_list)
foreach(line ${regdb_lines})
    string(STRIP "${line}" line)
    if(line MATCHES "^country [A-Z0-9][A-Z0-9]:")
        # Extract the two-letter code after 'country '
        string(REGEX REPLACE "^country ([A-Z0-9][A-Z0-9]):.*$" "\\1" code "${line}")
        # Only keep strict A-Z A-Z pairs (avoid numeric/specials)
        if(code MATCHES "^[A-Z][A-Z]$")
            list(APPEND code_list "${code}")
        endif()
    endif()
endforeach()

list(REMOVE_DUPLICATES code_list)
list(SORT code_list)

set(output_content "")
foreach(cc IN LISTS code_list)
    set(output_content "${output_content}${cc}\n")
endforeach()

file(WRITE "${OUTPUT_FILE}" "${output_content}")

list(LENGTH code_list num_codes)
message(STATUS "Generated ${num_codes} wireless regulatory country codes into ${OUTPUT_FILE}")


