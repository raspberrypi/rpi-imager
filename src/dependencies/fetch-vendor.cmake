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
		set_property(GLOBAL PROPERTY RPI_IMAGER_SBOM_FETCH_ORIGIN_${_name} "vendored source in src/dependencies/vendor")
		message(STATUS "Using vendored ${_name} from ${_vendor}")
		FetchContent_Declare(${_name}
			SOURCE_DIR "${_vendor}"
			${_declare_args}
			${USE_OVERRIDE_FIND_PACKAGE}
		)
	else()
		set_property(GLOBAL PROPERTY RPI_IMAGER_SBOM_FETCH_ORIGIN_${_name} "fetched from git at configure time")
		FetchContent_Declare(${_name}
			GIT_REPOSITORY ${_arg_GIT_REPOSITORY}
			GIT_TAG ${_arg_GIT_TAG}
			${_declare_args}
			${USE_OVERRIDE_FIND_PACKAGE}
		)
	endif()
endfunction()
