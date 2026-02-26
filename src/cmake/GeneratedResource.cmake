cmake_minimum_required(VERSION 3.22)

# add_generated_resource_with_fallback
#
# Creates a custom command to generate a file at build time by invoking a CMake script,
# with a fallback to a given source-tree file when the download fails (handled by the script).
# It then generates a QRC that aliases the output file to a given resource name, so it
# overrides the same file embedded elsewhere (e.g., in qml.qrc).
#
# Usage:
#   add_generated_resource_with_fallback(
#       OUT_QRC_VAR           <var to receive generated qrc path>
#       OUT_TARGET_VAR        <var to receive custom target name>
#       NAME                  <identifier, e.g., countries or timezones>
#       SCRIPT                <path to CMake -P script to run>
#       OUTPUT_BASENAME       <filename for generated data in binary dir>
#       ALIAS                 <alias inside resource, e.g., countries.txt>
#       FALLBACK_SOURCE       <path to fallback file in source tree>
#       EXTRA_CMAKE_ARGS      <list of -DNAME=VALUE to pass to the script>
#   )
#
# Example:
#   add_generated_resource_with_fallback(
#       GEN_QRC GEN_TGT countries
#       ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateRegdbCountries.cmake
#       countries_generated.txt countries.txt
#       ${CMAKE_CURRENT_SOURCE_DIR}/countries.txt
#       -DREGDB_URL=https://...
#   )
function(add_generated_resource_with_fallback OUT_QRC_VAR OUT_TARGET_VAR NAME SCRIPT OUTPUT_BASENAME ALIAS FALLBACK_SOURCE)
    set(options)
    set(oneValueArgs)
    set(multiValueArgs EXTRA_CMAKE_ARGS)
    cmake_parse_arguments(AGR "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    set(output_file "${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT_BASENAME}")
    set(tmp_file "${output_file}.tmp")
    set(gen_target "generate_${NAME}")
    set(gen_qrc    "${CMAKE_CURRENT_BINARY_DIR}/${NAME}_generated.qrc")

    # Custom targets always run, so we re-fetch from the API on every build.
    # Generate to a temp file first, then copy_if_different so downstream
    # targets (rcc, compile, link) only rebuild when content actually changes.
    add_custom_target(${gen_target}
        COMMAND ${CMAKE_COMMAND}
            -DOUTPUT_FILE=${tmp_file}
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            ${AGR_EXTRA_CMAKE_ARGS}
            -P ${SCRIPT}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${tmp_file} ${output_file}
        COMMAND ${CMAKE_COMMAND} -E remove -f ${tmp_file}
        BYPRODUCTS ${output_file}
        COMMENT "Generating ${NAME} data (downloads from API, falls back to source if offline)"
        VERBATIM
    )

    # Generate a QRC that aliases the generated file to the desired resource path
    file(GENERATE OUTPUT "${gen_qrc}" CONTENT
"<RCC>\n  <qresource prefix=\"/\">\n    <file alias=\"${ALIAS}\">${output_file}</file>\n  </qresource>\n</RCC>\n"
        NEWLINE_STYLE UNIX)

    # Ensure RCC rebuilds when the generated data changes by generating a .cpp via rcc with explicit DEPENDS
    set(rcc_cpp "${CMAKE_CURRENT_BINARY_DIR}/qrc_${NAME}_generated.cpp")
    add_custom_command(
        OUTPUT ${rcc_cpp}
        COMMAND Qt6::rcc --no-zstd -name ${NAME}_generated -o ${rcc_cpp} ${gen_qrc}
        DEPENDS ${gen_qrc} ${output_file}
        VERBATIM
        COMMENT "Running rcc for ${NAME}_generated"
    )
    set_source_files_properties("${rcc_cpp}" PROPERTIES GENERATED TRUE)

    # Return values to caller
    set(${OUT_QRC_VAR} "${rcc_cpp}" PARENT_SCOPE)
    set(${OUT_TARGET_VAR} "${gen_target}" PARENT_SCOPE)
endfunction()




