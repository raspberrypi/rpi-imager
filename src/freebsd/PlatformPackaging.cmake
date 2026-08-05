# FreeBSD packaging: installation

install(TARGETS ${PROJECT_NAME} DESTINATION bin)
install(FILES "${CMAKE_CURRENT_LIST_DIR}/../linux/icon/rpi-imager.svg" DESTINATION share/icons/hicolor/scalable/apps)

if(NOT BUILD_CLI_ONLY)
    install(FILES "${CMAKE_CURRENT_LIST_DIR}/../../debian/com.raspberrypi.rpi-imager.metainfo.xml" DESTINATION share/metainfo)
    install(FILES "${CMAKE_CURRENT_LIST_DIR}/../../debian/com.raspberrypi.rpi-imager-manifest.xml" DESTINATION share/mime/packages)
    install(FILES "${CMAKE_CURRENT_LIST_DIR}/../../freebsd/com.raspberrypi.rpi-imager.desktop" DESTINATION share/applications)
    install(FILES "${CMAKE_CURRENT_LIST_DIR}/../../freebsd/com.raspberrypi.rpi-imager.policy" DESTINATION share/polkit-1/actions)
else()
    install(FILES "${CMAKE_CURRENT_LIST_DIR}/../../debian/com.raspberrypi.rpi-imager-cli.desktop" DESTINATION share/applications)
endif()
