cmake_minimum_required(VERSION 3.22)

# GenerateCapitalCities.cmake
# CMake script to download capital cities data from REST Countries API and generate
# a mapping file with capital cities, country codes, timezones, and keyboard layouts.
# Falls back to capital-cities.txt in source tree if download fails.
#
# Called with:
# -DAPI_URL=<url> -DOUTPUT_FILE=<file> -DSOURCE_DIR=<dir> -DKEYMAP_FILE=<file>

if(NOT DEFINED API_URL)
    message(FATAL_ERROR "API_URL must be defined")
endif()

if(NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "OUTPUT_FILE must be defined")
endif()

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR must be defined")
endif()

if(NOT DEFINED KEYMAP_FILE)
    message(FATAL_ERROR "KEYMAP_FILE must be defined")
endif()

set(TMP_DIR "${CMAKE_CURRENT_BINARY_DIR}")
set(JSON_FILE "${TMP_DIR}/restcountries.json")
set(FALLBACK_FILE "${SOURCE_DIR}/capital-cities.txt")

message(STATUS "Downloading capital cities data from ${API_URL}")

# Download REST Countries data with user agent and better timeout
file(DOWNLOAD "${API_URL}" "${JSON_FILE}"
     SHOW_PROGRESS
     STATUS dl_status
     LOG dl_log
     TIMEOUT 60
     USERPWD ""
     HTTPHEADER "User-Agent: CMake-RaspberryPi-Imager-Build"
     TLS_VERIFY ON)

list(GET dl_status 0 dl_error)
if(dl_error)
    list(GET dl_status 1 dl_message)
    message(WARNING "Failed to download capital cities data: ${dl_message}")
    message(STATUS "Log: ${dl_log}")
    message(STATUS "Using fallback capital cities list from source tree")

    if(EXISTS "${FALLBACK_FILE}")
        file(READ "${FALLBACK_FILE}" fallback_content)
        file(WRITE "${OUTPUT_FILE}" "${fallback_content}")
        message(STATUS "Fallback capital cities list copied to ${OUTPUT_FILE}")
        return()
    else()
        message(FATAL_ERROR "No fallback capital cities file found at ${FALLBACK_FILE}")
    endif()
endif()

# Read the JSON file
file(READ "${JSON_FILE}" json_content)

# The JSON is an array of objects, we need to split it properly
# First, remove the outer array brackets
string(REGEX REPLACE "^\\[" "" json_content "${json_content}")
string(REGEX REPLACE "\\]$" "" json_content "${json_content}")

# Split on "},{"  to get individual country objects
string(REPLACE "},{" "}|||{" json_content "${json_content}")
string(REPLACE "\n" " " json_content "${json_content}")
string(REPLACE "\r" "" json_content "${json_content}")

# Function to find best keyboard match for country code
function(find_keyboard_for_country country_code result_var)
    set(kb_result "us")  # default fallback
    
    # Direct lowercase match
    string(TOLOWER "${country_code}" cc_lower)
    if("${cc_lower}" IN_LIST keymap_list)
        set(kb_result "${cc_lower}")
    else()
        # Special mappings for common cases
        if(country_code STREQUAL "GB")
            set(kb_result "gb")
        elseif(country_code STREQUAL "US")
            set(kb_result "us")
        elseif(country_code STREQUAL "DE")
            set(kb_result "de")
        elseif(country_code STREQUAL "FR")
            set(kb_result "fr")
        elseif(country_code STREQUAL "ES")
            set(kb_result "es")
        elseif(country_code STREQUAL "IT")
            set(kb_result "it")
        elseif(country_code STREQUAL "PT")
            set(kb_result "pt")
        elseif(country_code STREQUAL "NL")
            set(kb_result "nl")
        elseif(country_code STREQUAL "PL")
            set(kb_result "pl")
        elseif(country_code STREQUAL "RU")
            set(kb_result "ru")
        elseif(country_code STREQUAL "JP")
            set(kb_result "jp")
        elseif(country_code STREQUAL "CN")
            set(kb_result "cn")
        elseif(country_code STREQUAL "KR")
            set(kb_result "kr")
        elseif(country_code STREQUAL "IN")
            set(kb_result "in")
        elseif(country_code STREQUAL "BR")
            set(kb_result "br")
        elseif(country_code STREQUAL "AR")
            set(kb_result "ar")
        elseif(country_code STREQUAL "MX")
            set(kb_result "mx")
        elseif(country_code STREQUAL "TR")
            set(kb_result "tr")
        elseif(country_code STREQUAL "GR")
            set(kb_result "gr")
        elseif(country_code STREQUAL "IL")
            set(kb_result "il")
        elseif(country_code STREQUAL "SE")
            set(kb_result "se")
        elseif(country_code STREQUAL "NO")
            set(kb_result "no")
        elseif(country_code STREQUAL "DK")
            set(kb_result "dk")
        elseif(country_code STREQUAL "FI")
            set(kb_result "fi")
        endif()
    endif()
    
    set(${result_var} "${kb_result}" PARENT_SCOPE)
endfunction()

# Function to determine default language for country
function(get_default_language country_code result_var)
    set(lang "English")  # default
    
    # Language mappings
    if(country_code STREQUAL "ES" OR country_code STREQUAL "MX" OR 
       country_code STREQUAL "AR" OR country_code STREQUAL "CO" OR
       country_code STREQUAL "PE" OR country_code STREQUAL "VE" OR
       country_code STREQUAL "CL" OR country_code STREQUAL "EC" OR
       country_code STREQUAL "GT" OR country_code STREQUAL "CU" OR
       country_code STREQUAL "BO" OR country_code STREQUAL "DO" OR
       country_code STREQUAL "HN" OR country_code STREQUAL "PY" OR
       country_code STREQUAL "SV" OR country_code STREQUAL "NI" OR
       country_code STREQUAL "CR" OR country_code STREQUAL "PA" OR
       country_code STREQUAL "UY")
        set(lang "Spanish")
    elseif(country_code STREQUAL "FR" OR country_code STREQUAL "BE" OR
           country_code STREQUAL "CH" OR country_code STREQUAL "LU" OR
           country_code STREQUAL "MC")
        set(lang "French")
    elseif(country_code STREQUAL "DE" OR country_code STREQUAL "AT")
        set(lang "German")
    elseif(country_code STREQUAL "PT" OR country_code STREQUAL "BR")
        set(lang "Portuguese")
    elseif(country_code STREQUAL "IT")
        set(lang "Italian")
    elseif(country_code STREQUAL "RU" OR country_code STREQUAL "BY")
        set(lang "Russian")
    elseif(country_code STREQUAL "CN" OR country_code STREQUAL "TW")
        set(lang "Chinese")
    elseif(country_code STREQUAL "JP")
        set(lang "Japanese")
    elseif(country_code STREQUAL "KR")
        set(lang "Korean")
    elseif(country_code STREQUAL "NL")
        set(lang "Dutch")
    elseif(country_code STREQUAL "PL")
        set(lang "Polish")
    elseif(country_code STREQUAL "TR")
        set(lang "Turkish")
    elseif(country_code STREQUAL "GR" OR country_code STREQUAL "CY")
        set(lang "Greek")
    elseif(country_code STREQUAL "IL")
        set(lang "Hebrew")
    elseif(country_code STREQUAL "SE")
        set(lang "Swedish")
    elseif(country_code STREQUAL "NO")
        set(lang "Norwegian")
    elseif(country_code STREQUAL "DK")
        set(lang "Danish")
    elseif(country_code STREQUAL "FI")
        set(lang "Finnish")
    elseif(country_code STREQUAL "HU")
        set(lang "Hungarian")
    elseif(country_code STREQUAL "CZ")
        set(lang "Czech")
    elseif(country_code STREQUAL "SK")
        set(lang "Slovak")
    elseif(country_code STREQUAL "RO")
        set(lang "Romanian")
    elseif(country_code STREQUAL "BG")
        set(lang "Bulgarian")
    elseif(country_code STREQUAL "HR")
        set(lang "Croatian")
    elseif(country_code STREQUAL "SI")
        set(lang "Slovenian")
    elseif(country_code STREQUAL "UA")
        set(lang "Ukrainian")
    elseif(country_code STREQUAL "TH")
        set(lang "Thai")
    elseif(country_code STREQUAL "VN")
        set(lang "Vietnamese")
    elseif(country_code STREQUAL "ID")
        set(lang "Indonesian")
    elseif(country_code STREQUAL "BD")
        set(lang "Bengali")
    elseif(country_code MATCHES "^(SA|AE|EG|IQ|JO|KW|LB|OM|QA|SY|YE|BH|DZ|MA|TN|LY|SD)$")
        set(lang "Arabic")
    endif()
    
    set(${result_var} "${lang}" PARENT_SCOPE)
endfunction()

# Function to get IANA timezone for country code
function(get_timezone_for_country country_code result_var)
    set(tz "UTC")  # default
    
    # Timezone mappings
    if(country_code STREQUAL "GB")
        set(tz "Europe/London")
    elseif(country_code STREQUAL "US")
        set(tz "America/New_York")
    elseif(country_code STREQUAL "CA")
        set(tz "America/Toronto")
    elseif(country_code STREQUAL "DE")
        set(tz "Europe/Berlin")
    elseif(country_code STREQUAL "FR")
        set(tz "Europe/Paris")
    elseif(country_code STREQUAL "ES")
        set(tz "Europe/Madrid")
    elseif(country_code STREQUAL "IT")
        set(tz "Europe/Rome")
    elseif(country_code STREQUAL "PT")
        set(tz "Europe/Lisbon")
    elseif(country_code STREQUAL "NL")
        set(tz "Europe/Amsterdam")
    elseif(country_code STREQUAL "BE")
        set(tz "Europe/Brussels")
    elseif(country_code STREQUAL "CH")
        set(tz "Europe/Zurich")
    elseif(country_code STREQUAL "AT")
        set(tz "Europe/Vienna")
    elseif(country_code STREQUAL "PL")
        set(tz "Europe/Warsaw")
    elseif(country_code STREQUAL "CZ")
        set(tz "Europe/Prague")
    elseif(country_code STREQUAL "HU")
        set(tz "Europe/Budapest")
    elseif(country_code STREQUAL "RO")
        set(tz "Europe/Bucharest")
    elseif(country_code STREQUAL "BG")
        set(tz "Europe/Sofia")
    elseif(country_code STREQUAL "GR")
        set(tz "Europe/Athens")
    elseif(country_code STREQUAL "TR")
        set(tz "Europe/Istanbul")
    elseif(country_code STREQUAL "RU")
        set(tz "Europe/Moscow")
    elseif(country_code STREQUAL "UA")
        set(tz "Europe/Kiev")
    elseif(country_code STREQUAL "SE")
        set(tz "Europe/Stockholm")
    elseif(country_code STREQUAL "NO")
        set(tz "Europe/Oslo")
    elseif(country_code STREQUAL "DK")
        set(tz "Europe/Copenhagen")
    elseif(country_code STREQUAL "FI")
        set(tz "Europe/Helsinki")
    elseif(country_code STREQUAL "IE")
        set(tz "Europe/Dublin")
    elseif(country_code STREQUAL "MX")
        set(tz "America/Mexico_City")
    elseif(country_code STREQUAL "BR")
        set(tz "America/Sao_Paulo")
    elseif(country_code STREQUAL "AR")
        set(tz "America/Argentina/Buenos_Aires")
    elseif(country_code STREQUAL "CL")
        set(tz "America/Santiago")
    elseif(country_code STREQUAL "CO")
        set(tz "America/Bogota")
    elseif(country_code STREQUAL "PE")
        set(tz "America/Lima")
    elseif(country_code STREQUAL "VE")
        set(tz "America/Caracas")
    elseif(country_code STREQUAL "CN")
        set(tz "Asia/Shanghai")
    elseif(country_code STREQUAL "JP")
        set(tz "Asia/Tokyo")
    elseif(country_code STREQUAL "KR")
        set(tz "Asia/Seoul")
    elseif(country_code STREQUAL "IN")
        set(tz "Asia/Kolkata")
    elseif(country_code STREQUAL "TH")
        set(tz "Asia/Bangkok")
    elseif(country_code STREQUAL "VN")
        set(tz "Asia/Ho_Chi_Minh")
    elseif(country_code STREQUAL "ID")
        set(tz "Asia/Jakarta")
    elseif(country_code STREQUAL "PH")
        set(tz "Asia/Manila")
    elseif(country_code STREQUAL "MY")
        set(tz "Asia/Kuala_Lumpur")
    elseif(country_code STREQUAL "SG")
        set(tz "Asia/Singapore")
    elseif(country_code STREQUAL "IL")
        set(tz "Asia/Jerusalem")
    elseif(country_code STREQUAL "SA")
        set(tz "Asia/Riyadh")
    elseif(country_code STREQUAL "AE")
        set(tz "Asia/Dubai")
    elseif(country_code STREQUAL "AU")
        set(tz "Australia/Sydney")
    elseif(country_code STREQUAL "NZ")
        set(tz "Pacific/Auckland")
    elseif(country_code STREQUAL "ZA")
        set(tz "Africa/Johannesburg")
    elseif(country_code STREQUAL "EG")
        set(tz "Africa/Cairo")
    elseif(country_code STREQUAL "TW")
        set(tz "Asia/Taipei")
    elseif(country_code STREQUAL "HK")
        set(tz "Asia/Hong_Kong")
    endif()
    
    set(${result_var} "${tz}" PARENT_SCOPE)
endfunction()

# Parse JSON manually (CMake doesn't have built-in JSON parser in 3.22)
# We'll extract country data using regex
set(output_content "# Capital Cities Database\n")
set(output_content "${output_content}# Format: CityName|CountryName|CountryCode|Timezone|Language|KeyboardLayout\n")
set(output_content "${output_content}# Generated from REST Countries API (https://restcountries.com/)\n")
set(output_content "${output_content}# Sorted alphabetically by capital city name\n")

# Split by our custom delimiter
string(REPLACE "|||" ";" country_objects "${json_content}")

set(all_entries "")

foreach(country_obj ${country_objects})
    # Skip empty entries
    if(NOT country_obj)
        continue()
    endif()
    # Extract fields using improved regex
    # Country code (cca2)
    if(country_obj MATCHES "\"cca2\" *: *\"([A-Z][A-Z])\"")
        set(country_code "${CMAKE_MATCH_1}")
    else()
        continue()
    endif()
    
    # Country name (common name from nested structure)
    if(country_obj MATCHES "\"common\" *: *\"([^\"]+)\"")
        set(country_name "${CMAKE_MATCH_1}")
    else()
        continue()
    endif()
    
    # Capital city (first capital if multiple, inside array)
    if(country_obj MATCHES "\"capital\" *: *\\[ *\"([^\"]+)\"")
        set(capital "${CMAKE_MATCH_1}")
    else()
        # Skip countries without capitals
        continue()
    endif()
    
    # Get keyboard and language using helper functions
    find_keyboard_for_country("${country_code}" keyboard)
    get_default_language("${country_code}" language)
    get_timezone_for_country("${country_code}" timezone)
    
    # Clean up names (replace pipe characters that would break our format)
    string(REPLACE "|" "/" capital "${capital}")
    string(REPLACE "|" "/" country_name "${country_name}")
    
    # Add to list: "Capital|CountryName|Code|Timezone|Language|Keyboard"
    list(APPEND all_entries "${capital}|${country_name}|${country_code}|${timezone}|${language}|${keyboard}")
endforeach()

# Sort by capital city name
list(SORT all_entries)

# Remove duplicates (some countries might have been listed multiple times)
list(REMOVE_DUPLICATES all_entries)

# Write to output file
foreach(entry ${all_entries})
    set(output_content "${output_content}${entry}\n")
endforeach()

file(WRITE "${OUTPUT_FILE}" "${output_content}")

list(LENGTH all_entries num_entries)
message(STATUS "Generated ${num_entries} capital city entries into ${OUTPUT_FILE}")

