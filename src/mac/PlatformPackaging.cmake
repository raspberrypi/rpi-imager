# macOS packaging and deployment steps

if(BUILD_CLI_ONLY)
    # CLI-only build: simple executable, no app bundle
    message(STATUS "Building CLI-only version for macOS")
else()
    # GUI build: create app bundle
    # Set application name with proper spacing
    set(APP_NAME "Raspberry Pi Imager")

    # Set all required bundle properties
    set(MACOSX_BUNDLE_BUNDLE_NAME "${APP_NAME}")
    set(MACOSX_BUNDLE_EXECUTABLE_NAME "${PROJECT_NAME}")
    set(MACOSX_BUNDLE_GUI_IDENTIFIER "com.raspberrypi.rpi-imager")
    set(MACOSX_BUNDLE_BUNDLE_VERSION "${IMAGER_VERSION_STR}")
    set(MACOSX_BUNDLE_SHORT_VERSION_STRING "${IMAGER_VERSION_STR}")
    set(MACOSX_BUNDLE_LONG_VERSION_STRING "${IMAGER_VERSION_STR}")
    set(MACOSX_BUNDLE_ICON_FILE "rpi-imager.icns")
    string(TIMESTAMP CURRENT_YEAR "%Y")
    set(MACOSX_BUNDLE_COPYRIGHT "Copyright © 2020-${CURRENT_YEAR} Raspberry Pi Ltd")

    set_target_properties(${PROJECT_NAME} PROPERTIES MACOSX_BUNDLE YES)

    set(APP_BUNDLE_PATH "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.app")
    set(DMG_PATH "${CMAKE_BINARY_DIR}/${APP_NAME}.dmg")
    set(FINAL_DMG_PATH "${CMAKE_BINARY_DIR}/${APP_NAME}-${IMAGER_VERSION_STR}.dmg")

    configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/mac/Info.plist.in"
    "${CMAKE_BINARY_DIR}/Info.plist"
    @ONLY
)

add_custom_command(TARGET ${PROJECT_NAME} PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Cleaning up previous macOS DMG files..."
    COMMAND ${CMAKE_COMMAND} -E remove -f "${DMG_PATH}"
    COMMAND ${CMAKE_COMMAND} -E remove -f "${FINAL_DMG_PATH}"
    COMMENT "Cleaning previous macOS build artifacts"
)

add_custom_command(TARGET ${PROJECT_NAME} PRE_LINK
    COMMAND ${CMAKE_COMMAND} -E echo "Ensuring app bundle directories exist..."
    COMMAND ${CMAKE_COMMAND} -E make_directory "${APP_BUNDLE_PATH}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${APP_BUNDLE_PATH}/Contents"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${APP_BUNDLE_PATH}/Contents/MacOS"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${APP_BUNDLE_PATH}/Contents/Resources"
    COMMENT "Creating app bundle directory structure"
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/Info.plist" "${APP_BUNDLE_PATH}/Contents/Info.plist"
    COMMENT "Installing custom Info.plist"
)

# Copy Icon Composer .icon file for dark mode support (macOS Tahoe+)
set(ICON_COMPOSER_OUTPUT "${CMAKE_BINARY_DIR}/AppIcon.icon")
if(EXISTS "${ICON_COMPOSER_OUTPUT}" OR EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/icons/app_icon_macos.icon")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory 
            "${CMAKE_CURRENT_SOURCE_DIR}/icons/app_icon_macos.icon"
            "${APP_BUNDLE_PATH}/Contents/Resources/AppIcon.icon"
        COMMENT "Installing Icon Composer file for dark mode support"
    )
endif()

find_program(MACDEPLOYQT "macdeployqt" PATHS "${Qt6_ROOT}/bin")
if (NOT MACDEPLOYQT)
    message(FATAL_ERROR "Unable to locate macdeployqt in ${Qt6_ROOT}/bin - ensure Qt was built with tools")
endif()
message(STATUS "Using macdeployqt: ${MACDEPLOYQT}")

add_custom_command(TARGET ${PROJECT_NAME}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Deploying Qt dependencies..."
    COMMAND "${MACDEPLOYQT}" "${APP_BUNDLE_PATH}" -qmldir="${CMAKE_CURRENT_SOURCE_DIR}" -always-overwrite
    COMMENT "Deploying Qt dependencies"
)

# Pruning commands kept verbatim from original CMakeLists
add_custom_command(TARGET ${PROJECT_NAME}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/PlugIns/quick/libqtquickcontrols2fusionstyleplugin.dylib"
    COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/PlugIns/quick/libqtquickcontrols2fusionstyleimplplugin.dylib"
    COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/PlugIns/quick/libqtquickcontrols2universalstyleplugin.dylib"
    COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/PlugIns/quick/libqtquickcontrols2universalstyleimplplugin.dylib"
    COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/PlugIns/quick/libqtquickcontrols2imaginestyleimplplugin.dylib"
    COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/PlugIns/quick/libqtquickcontrols2imaginestyleplugin.dylib"
    COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/PlugIns/quick/libqtquickcontrols2iosstyleimplplugin.dylib"
    COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/PlugIns/quick/libqtquickcontrols2iosstyleplugin.dylib"
    COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/PlugIns/quick/libqtquickcontrols2macosstyleimplplugin.dylib"
    COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/PlugIns/quick/libqtquickcontrols2macosstyleplugin.dylib"
    COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/PlugIns/quick/libqtquickcontrols2fluentwinui3styleimplplugin.dylib"
    COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/PlugIns/quick/libqtquickcontrols2fluentwinui3styleplugin.dylib"
    COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/PlugIns/quick/libqtquickcontrols2nativestyleplugin.dylib"
    COMMENT "Removing unused QuickControls2 style plugins (keeping Material + Basic fallback)"
)

add_custom_command(TARGET ${PROJECT_NAME}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/PlugIns/quick/libqtvkbstylesplugin.dylib"
    COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/PlugIns/quick/libqtvkbcomponentsplugin.dylib"
    COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/PlugIns/quick/libqtvkbsettingsplugin.dylib"
    COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/PlugIns/quick/libqtvkblayoutsplugin.dylib"
    COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/PlugIns/quick/libqtvkbpluginsplugin.dylib"
    COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/PlugIns/quick/libqtvkbhangulplugin.dylib"
    COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/PlugIns/quick/libqtvkbtcimeplugin.dylib"
    COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/PlugIns/quick/libqtvkbbuiltinstylesplugin.dylib"
    COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/PlugIns/quick/libqtvkbpinyinplugin.dylib"
    COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/PlugIns/quick/libqtvkbplugin.dylib"
    COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/PlugIns/quick/libvirtualkeyboardplugin.dylib"
    COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/PlugIns/quick/libqtvkbthaiplugin.dylib"
    COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/PlugIns/quick/libqtvkbopenwnnplugin.dylib"
    COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/PlugIns/platforminputcontexts/libqtvirtualkeyboardplugin.dylib"
    COMMENT "Removing virtual keyboard plugins (already disabled at runtime)"
)

add_custom_command(TARGET ${PROJECT_NAME}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${APP_BUNDLE_PATH}/Contents/PlugIns/sqldrivers"
    COMMENT "Removing SQL driver plugins directory (not needed for imager)"
)

add_custom_command(TARGET ${PROJECT_NAME}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/PlugIns/tls/libqopensslbackend.dylib"
    COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/PlugIns/tls/libqcertonlybackend.dylib"
    COMMENT "Removing OpenSSL TLS backends (keeping native macOS SecureTransport only)"
)

add_custom_command(TARGET ${PROJECT_NAME}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/PlugIns/imageformats/libqtiff.dylib"
    COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/PlugIns/imageformats/libqwebp.dylib"
    COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/PlugIns/imageformats/libqgif.dylib"
    COMMENT "Removing unused image format plugins if present"
)

# Remove QtWidgets and dependent style/platform plugins (not used by this QML app)
add_custom_command(TARGET ${PROJECT_NAME}
    POST_BUILD
    # QWidget style plugin (unused in Qt Quick app)
    COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/PlugIns/styles/libqmacstyle.dylib"
    # Qt Labs Platform QML plugin and backing library (pulls in QtWidgets)
    COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/PlugIns/quick/liblabsplatformplugin.dylib"
    COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/Frameworks/libQt6LabsPlatform.6.dylib"
    COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/Frameworks/libQt6LabsPlatform.dylib"
    # Other Qt Labs QML plugins and their backing libraries (unused)
    COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/PlugIns/quick/liblabsanimationplugin.dylib"
    COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/PlugIns/quick/libqmlsettingsplugin.dylib"
    COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/Frameworks/libQt6LabsAnimation.6.dylib"
    COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/Frameworks/libQt6LabsAnimation.dylib"
    COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/Frameworks/libQt6LabsSettings.6.dylib"
    COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/Frameworks/libQt6LabsSettings.dylib"
    # Finally remove QtWidgets dylibs themselves
    COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/Frameworks/libQt6Widgets.6.dylib"
    COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/Frameworks/libQt6Widgets.dylib"
    COMMENT "Removing QtWidgets and dependent plugins"
)

add_custom_command(TARGET ${PROJECT_NAME}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${APP_BUNDLE_PATH}/Contents/Resources/qml/QtQuick/Controls/Fusion"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${APP_BUNDLE_PATH}/Contents/Resources/qml/QtQuick/Controls/Universal"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${APP_BUNDLE_PATH}/Contents/Resources/qml/QtQuick/Controls/Imagine"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${APP_BUNDLE_PATH}/Contents/Resources/qml/QtQuick/Controls/iOS"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${APP_BUNDLE_PATH}/Contents/Resources/qml/QtQuick/Controls/macOS"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${APP_BUNDLE_PATH}/Contents/Resources/qml/QtQuick/Controls/FluentWinUI3"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${APP_BUNDLE_PATH}/Contents/Resources/qml/QtQuick/Controls/designer"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${APP_BUNDLE_PATH}/Contents/Resources/qml/QtQuick/VirtualKeyboard"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${APP_BUNDLE_PATH}/Contents/Resources/qml/QtTest"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${APP_BUNDLE_PATH}/Contents/Resources/qml/Qt/test"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${APP_BUNDLE_PATH}/Contents/Resources/qml/Assets"
    COMMAND /bin/sh -c "find \"${APP_BUNDLE_PATH}/Contents/Resources/qml\" -type d -name 'objects-*' -prune -exec rm -rf {} +"
    COMMENT "Removing unused QML theme directories and modules (keeping Material theme + Basic fallback)"
)

if(IMAGER_SIGNED_APP)
    if(IMAGER_SIGNING_IDENTITY)
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/mac/create_styled_dmg.sh.in"
            "${CMAKE_BINARY_DIR}/create_styled_dmg.sh"
            @ONLY
        )
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/mac/macos_post_build.sh.in"
            "${CMAKE_BINARY_DIR}/macos_post_build.sh"
            @ONLY
        )
        execute_process(COMMAND chmod +x "${CMAKE_BINARY_DIR}/macos_post_build.sh")
        add_custom_target(dmg
            COMMAND "${CMAKE_BINARY_DIR}/macos_post_build.sh"
            DEPENDS ${PROJECT_NAME}
            COMMENT "Creating signed macOS DMG"
            VERBATIM)
    else()
        message(FATAL_ERROR "Signing requested, but no signing identity provided")
    endif()
else()
    add_custom_target(dmg
        COMMAND ${CMAKE_COMMAND} -E echo "Creating DMG..."
        COMMAND hdiutil create -volname "${APP_NAME}" -srcfolder "${APP_BUNDLE_PATH}" -ov -format UDBZ "${DMG_PATH}"
        COMMAND ${CMAKE_COMMAND} -E echo "Creating versioned DMG at ${FINAL_DMG_PATH}..."
        COMMAND ${CMAKE_COMMAND} -E copy "${DMG_PATH}" "${FINAL_DMG_PATH}"
        DEPENDS ${PROJECT_NAME}
        COMMENT "Creating macOS DMG"
        VERBATIM)
endif()

    message(STATUS "Added 'dmg' target to build the macOS DMG installer")
endif() # BUILD_CLI_ONLY


