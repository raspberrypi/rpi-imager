# Linux packaging: installation and runtime checks

if (NOT CMAKE_CROSSCOMPILING)
    find_program(LSBLK "lsblk")
    if (NOT LSBLK)
        message(FATAL_ERROR "Unable to locate lsblk (used for disk enumeration)")
    endif()

    execute_process(COMMAND "${LSBLK}" "--json" OUTPUT_QUIET RESULT_VARIABLE ret)
    if (ret EQUAL "1")
        message(FATAL_ERROR "util-linux package too old. lsblk does not support --json (used for disk enumeration)")
    endif()
endif()

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

install(TARGETS ${PROJECT_NAME} DESTINATION bin)

if(BUILD_CLI_ONLY)
    # CLI-only build: install CLI-specific desktop file (marked as NoDisplay)
    # Icon is still required for AppImage tooling (linuxdeploy) even though NoDisplay=true
    install(FILES "${CMAKE_CURRENT_LIST_DIR}/icon/rpi-imager.svg" DESTINATION share/icons/hicolor/scalable/apps)
    install(FILES "${CMAKE_CURRENT_LIST_DIR}/../../debian/com.raspberrypi.rpi-imager-cli.desktop" DESTINATION share/applications)
else()
    # GUI build: install full desktop integration
    install(FILES "${CMAKE_CURRENT_LIST_DIR}/icon/rpi-imager.svg" DESTINATION share/icons/hicolor/scalable/apps)
    install(FILES "${CMAKE_CURRENT_LIST_DIR}/../../debian/com.raspberrypi.rpi-imager.desktop" DESTINATION share/applications)
    install(FILES "${CMAKE_CURRENT_LIST_DIR}/../../debian/com.raspberrypi.rpi-imager.metainfo.xml" DESTINATION share/metainfo)
endif()


