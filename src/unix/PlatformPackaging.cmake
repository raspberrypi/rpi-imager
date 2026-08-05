# Common commands used by both FreeBSD and Linux

# Generate metainfo.xml at build time so version stays in sync with the binary
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_LIST_DIR}/../../debian/com.raspberrypi.rpi-imager.metainfo.xml"
    COMMAND ${CMAKE_COMMAND}
        -DVERSION_VARS_FILE=${IMAGER_VERSION_VARS}
        -DINPUT=${CMAKE_CURRENT_LIST_DIR}/../../debian/com.raspberrypi.rpi-imager.metainfo.xml.in
        -DOUTPUT=${CMAKE_CURRENT_LIST_DIR}/../../debian/com.raspberrypi.rpi-imager.metainfo.xml
        -P ${CONFIGURE_VERSIONED_SCRIPT}
    DEPENDS
        ${IMAGER_VERSION_VARS}
        ${CMAKE_CURRENT_LIST_DIR}/../../debian/com.raspberrypi.rpi-imager.metainfo.xml.in
    COMMENT "Configuring metainfo.xml with build-time version"
    VERBATIM
)
add_custom_target(generate_metainfo
    DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../../debian/com.raspberrypi.rpi-imager.metainfo.xml")
add_dependencies(generate_metainfo generate_version)
add_dependencies(${PROJECT_NAME} generate_metainfo)
