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

# Generate metainfo.xml with current version
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/../../debian/com.raspberrypi.rpi-imager.metainfo.xml.in"
    "${CMAKE_CURRENT_BINARY_DIR}/com.raspberrypi.rpi-imager.metainfo.xml"
    @ONLY)

install(TARGETS ${PROJECT_NAME} DESTINATION bin)

if(BUILD_CLI_ONLY)
    # CLI-only build: install CLI-specific desktop file (marked as NoDisplay)
    install(FILES "${CMAKE_CURRENT_LIST_DIR}/../../debian/com.raspberrypi.rpi-imager-cli.desktop" DESTINATION share/applications)
    # Note: No icon or metainfo needed for CLI-only builds
else()
    # GUI build: install full desktop integration
    install(FILES "${CMAKE_CURRENT_LIST_DIR}/../../debian/rpi-imager.png" DESTINATION share/icons/hicolor/128x128/apps)
    install(FILES "${CMAKE_CURRENT_LIST_DIR}/../../debian/com.raspberrypi.rpi-imager.desktop" DESTINATION share/applications)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/com.raspberrypi.rpi-imager.metainfo.xml" DESTINATION share/metainfo)
endif()


