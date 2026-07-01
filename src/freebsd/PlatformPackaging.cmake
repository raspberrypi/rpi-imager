# FreeBSD packaging: installation

# Generate metainfo.xml at build time so version stays in sync with the binary
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_LIST_DIR}/../../freebsd/com.raspberrypi.rpi-imager.metainfo.xml"
    COMMAND ${CMAKE_COMMAND}
        -DVERSION_VARS_FILE=${IMAGER_VERSION_VARS}
        -DINPUT=${CMAKE_CURRENT_LIST_DIR}/../../freebsd/com.raspberrypi.rpi-imager.metainfo.xml.in
        -DOUTPUT=${CMAKE_CURRENT_LIST_DIR}/../../freebsd/com.raspberrypi.rpi-imager.metainfo.xml
        -P ${CONFIGURE_VERSIONED_SCRIPT}
    DEPENDS
        ${IMAGER_VERSION_VARS}
        ${CMAKE_CURRENT_LIST_DIR}/../../freebsd/com.raspberrypi.rpi-imager.metainfo.xml.in
    COMMENT "Configuring metainfo.xml with build-time version"
    VERBATIM
)
add_custom_target(generate_metainfo
    DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../../freebsd/com.raspberrypi.rpi-imager.metainfo.xml")
add_dependencies(generate_metainfo generate_version)
add_dependencies(${PROJECT_NAME} generate_metainfo)

install(TARGETS ${PROJECT_NAME} DESTINATION bin)
install(FILES "${CMAKE_CURRENT_LIST_DIR}/icon/rpi-imager.svg" DESTINATION share/icons/hicolor/scalable/apps)

if(NOT BUILD_CLI_ONLY)
    # GUI build: install full desktop integration
    install(FILES "${CMAKE_CURRENT_LIST_DIR}/../../freebsd/com.raspberrypi.rpi-imager.desktop" DESTINATION share/applications)
    install(FILES "${CMAKE_CURRENT_LIST_DIR}/../../freebsd/com.raspberrypi.rpi-imager.metainfo.xml" DESTINATION share/metainfo)
    install(FILES "${CMAKE_CURRENT_LIST_DIR}/../../freebsd/com.raspberrypi.rpi-imager-manifest.xml" DESTINATION share/mime/packages)
    install(FILES "${CMAKE_CURRENT_LIST_DIR}/../../freebsd/com.raspberrypi.rpi-imager.policy" DESTINATION share/polkit-1/actions)
else()
    install(FILES "${CMAKE_CURRENT_LIST_DIR}/../../freebsd/com.raspberrypi.rpi-imager-cli.desktop" DESTINATION share/applications)
endif()
