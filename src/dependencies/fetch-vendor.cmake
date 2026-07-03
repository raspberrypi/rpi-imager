# Prefer vendored git submodules under dependencies/vendor/ for offline builds.
# Falls back to FetchContent git clone when a vendor tree is missing.
#
# Usage:
#   rpi_imager_fetch_git_or_vendor(<name>
#     VENDOR_DIR <subdir-under-vendor>
#     VENDOR_MARKER <file-that-must-exist>
#     GIT_REPOSITORY <url>
#     GIT_TAG <tag>
#     [SOURCE_SUBDIR <subdir>]
#     [PATCH_COMMAND <cmd...>]
#   )

function(rpi_imager_fetch_git_or_vendor _name)
	set(_options "")
	set(_oneValueArgs VENDOR_DIR VENDOR_MARKER GIT_REPOSITORY GIT_TAG SOURCE_SUBDIR)
	set(_multiValueArgs PATCH_COMMAND)
	cmake_parse_arguments(_arg "${_options}" "${_oneValueArgs}" "${_multiValueArgs}" ${ARGN})

	if(NOT _arg_VENDOR_DIR OR NOT _arg_VENDOR_MARKER OR NOT _arg_GIT_REPOSITORY OR NOT _arg_GIT_TAG)
		message(FATAL_ERROR "rpi_imager_fetch_git_or_vendor: missing required arguments")
	endif()

	set(_vendor "${CMAKE_CURRENT_LIST_DIR}/vendor/${_arg_VENDOR_DIR}")
	set(_use_vendor FALSE)
	if(EXISTS "${_vendor}/${_arg_VENDOR_MARKER}")
		set(_use_vendor TRUE)
	endif()

	set(_declare_args "")
	if(_arg_SOURCE_SUBDIR)
		list(APPEND _declare_args SOURCE_SUBDIR "${_arg_SOURCE_SUBDIR}")
	endif()
	if(_arg_PATCH_COMMAND)
		list(APPEND _declare_args PATCH_COMMAND ${_arg_PATCH_COMMAND})
	endif()

	if(_use_vendor)
		message(STATUS "Using vendored ${_name} from ${_vendor}")
		FetchContent_Declare(${_name}
			SOURCE_DIR "${_vendor}"
			${_declare_args}
			${USE_OVERRIDE_FIND_PACKAGE}
		)
	else()
		FetchContent_Declare(${_name}
			GIT_REPOSITORY ${_arg_GIT_REPOSITORY}
			GIT_TAG ${_arg_GIT_TAG}
			${_declare_args}
			${USE_OVERRIDE_FIND_PACKAGE}
		)
	endif()
endfunction()

function(rpi_imager_patch_libarchive source_dir)
	if(NOT EXISTS "${source_dir}/CMakeLists.txt")
		message(FATAL_ERROR "rpi_imager_patch_libarchive: missing ${source_dir}/CMakeLists.txt")
	endif()

	set(_patch "${CMAKE_CURRENT_BINARY_DIR}/libarchive_zstd_patch.cmake")
	file(WRITE "${_patch}" "
file(READ \"${source_dir}/CMakeLists.txt\" CONTENT)
string(FIND \"\${CONTENT}\" \"IF(ZSTD_FOUND)\" ZSTD_START)
string(FIND \"\${CONTENT}\" \"MARK_AS_ADVANCED(CLEAR ZSTD_INCLUDE_DIR)\" ZSTD_END)
if(ZSTD_START GREATER -1 AND ZSTD_END GREATER -1)
    math(EXPR ZSTD_END \"\${ZSTD_END} + 40\")
    string(SUBSTRING \"\${CONTENT}\" 0 \${ZSTD_START} BEFORE_ZSTD)
    string(SUBSTRING \"\${CONTENT}\" \${ZSTD_END} -1 AFTER_ZSTD)
    set(NEW_ZSTD_SECTION \"IF(ZSTD_FOUND)
  SET(HAVE_ZSTD_H 1)
  INCLUDE_DIRECTORIES(\${ZSTD_INCLUDE_DIR})
  LIST(APPEND ADDITIONAL_LIBS \${ZSTD_LIBRARY})
  get_property(ZSTD_LIB_IS_CACHE CACHE ZSTD_LIBRARY PROPERTY TYPE)
  get_property(ZSTD_INC_IS_CACHE CACHE ZSTD_INCLUDE_DIR PROPERTY TYPE)
  if(ZSTD_LIB_IS_CACHE AND ZSTD_INC_IS_CACHE)
    message(STATUS \\\"Using provided ZSTD library: \${ZSTD_LIBRARY}\\\")
    SET(HAVE_LIBZSTD 1)
    SET(HAVE_ZSTD_compressStream 1)
    SET(HAVE_ZSTD_minCLevel 1)
  else()
    CMAKE_PUSH_CHECK_STATE()
    SET(CMAKE_REQUIRED_LIBRARIES \${ZSTD_LIBRARY})
    SET(CMAKE_REQUIRED_INCLUDES \${ZSTD_INCLUDE_DIR})
    CHECK_FUNCTION_EXISTS(ZSTD_decompressStream HAVE_LIBZSTD)
    CHECK_FUNCTION_EXISTS(ZSTD_compressStream HAVE_ZSTD_compressStream)
    CHECK_FUNCTION_EXISTS(ZSTD_minCLevel HAVE_ZSTD_minCLevel)
    CMAKE_POP_CHECK_STATE()
  endif()
ENDIF(ZSTD_FOUND)
MARK_AS_ADVANCED(CLEAR ZSTD_INCLUDE_DIR)\")
    set(NEW_CONTENT \"\${BEFORE_ZSTD}\${NEW_ZSTD_SECTION}\")
    file(WRITE \"${source_dir}/CMakeLists.txt\" \"\${NEW_CONTENT}\${AFTER_ZSTD}\")
    message(STATUS \"Patched libarchive CMakeLists.txt for static ZSTD support\")
else()
    message(WARNING \"Could not find ZSTD section in libarchive CMakeLists.txt\")
endif()
")
	execute_process(COMMAND ${CMAKE_COMMAND} -P "${_patch}")
endfunction()
