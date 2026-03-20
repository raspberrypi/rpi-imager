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
    set(MACOSX_BUNDLE_ICON_FILE "AppIcon")
    string(TIMESTAMP CURRENT_YEAR "%Y")
    set(MACOSX_BUNDLE_COPYRIGHT "Copyright © 2020-${CURRENT_YEAR} Raspberry Pi Ltd")

    set_target_properties(${PROJECT_NAME} PROPERTIES MACOSX_BUNDLE YES)

    set(APP_BUNDLE_PATH "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.app")
    set(DMG_PATH "${CMAKE_BINARY_DIR}/${APP_NAME}.dmg")

    # Extra (non-version) variables for Info.plist.in — the MACOSX_BUNDLE_*_VERSION
    # vars are set at build time from IMAGER_VERSION_STR by the template itself.
    set(_plist_extra_vars "${CMAKE_CURRENT_BINARY_DIR}/plist_extra_vars.cmake")
    file(WRITE "${_plist_extra_vars}"
        "set(MACOSX_BUNDLE_BUNDLE_NAME \"${MACOSX_BUNDLE_BUNDLE_NAME}\")\n"
        "set(MACOSX_BUNDLE_EXECUTABLE_NAME \"${MACOSX_BUNDLE_EXECUTABLE_NAME}\")\n"
        "set(MACOSX_BUNDLE_GUI_IDENTIFIER \"${MACOSX_BUNDLE_GUI_IDENTIFIER}\")\n"
        "set(MACOSX_BUNDLE_BUNDLE_VERSION \"\${IMAGER_VERSION_STR}\")\n"
        "set(MACOSX_BUNDLE_SHORT_VERSION_STRING \"\${IMAGER_VERSION_STR}\")\n"
        "set(MACOSX_BUNDLE_LONG_VERSION_STRING \"\${IMAGER_VERSION_STR}\")\n"
        "set(MACOSX_BUNDLE_ICON_FILE \"${MACOSX_BUNDLE_ICON_FILE}\")\n"
        "set(MACOSX_BUNDLE_COPYRIGHT \"${MACOSX_BUNDLE_COPYRIGHT}\")\n"
    )

    # Generate Info.plist at build time so version is always fresh
    add_custom_command(
        OUTPUT "${CMAKE_BINARY_DIR}/Info.plist"
        COMMAND ${CMAKE_COMMAND}
            -DVERSION_VARS_FILE=${IMAGER_VERSION_VARS}
            -DEXTRA_VARS_FILE=${_plist_extra_vars}
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/mac/Info.plist.in
            -DOUTPUT=${CMAKE_BINARY_DIR}/Info.plist
            -P ${CONFIGURE_VERSIONED_SCRIPT}
        DEPENDS
            ${IMAGER_VERSION_VARS}
            ${CMAKE_CURRENT_SOURCE_DIR}/mac/Info.plist.in
        COMMENT "Configuring Info.plist with build-time version"
        VERBATIM
    )
    add_custom_target(generate_plist DEPENDS "${CMAKE_BINARY_DIR}/Info.plist")
    add_dependencies(generate_plist generate_version)
    add_dependencies(${PROJECT_NAME} generate_plist)

add_custom_command(TARGET ${PROJECT_NAME} PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Cleaning up previous macOS DMG files..."
    COMMAND ${CMAKE_COMMAND} -E remove -f "${DMG_PATH}"
    COMMAND sh -c "rm -f '${CMAKE_BINARY_DIR}/${APP_NAME}'-*.dmg"
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

# Install pre-compiled icon assets for dark mode + Liquid Glass support (macOS Tahoe+)
# These were compiled from app_icon_macos.icon using Xcode's actool via a helper project
# The pre-compiled Assets.car properly contains all appearance variants (light/dark/tinted)
set(PRECOMPILED_ASSETS_CAR "${CMAKE_CURRENT_SOURCE_DIR}/icons/AppIcon-compiled.car")
set(PRECOMPILED_ICNS "${CMAKE_CURRENT_SOURCE_DIR}/icons/AppIcon-compiled.icns")
if(EXISTS "${PRECOMPILED_ASSETS_CAR}" AND NOT BUILD_CLI_ONLY)
    message(STATUS "Found pre-compiled icon Assets.car: ${PRECOMPILED_ASSETS_CAR}")
    message(STATUS "Will install pre-compiled icons for Liquid Glass support on macOS Tahoe+")
    
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Installing pre-compiled icon assets for Liquid Glass..."
        # Remove Qt's Assets.car - replace with our pre-compiled version containing AppIcon
        COMMAND ${CMAKE_COMMAND} -E remove -f "${APP_BUNDLE_PATH}/Contents/Resources/Assets.car"
        # Remove any stale .icon copies from previous build attempts
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${APP_BUNDLE_PATH}/Contents/Resources/app_icon_macos.icon"
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${APP_BUNDLE_PATH}/Contents/Resources/AppIcon.icon"
        # Copy pre-compiled Assets.car (contains all icon variants for Liquid Glass)
        COMMAND ${CMAKE_COMMAND} -E copy 
            "${PRECOMPILED_ASSETS_CAR}"
            "${APP_BUNDLE_PATH}/Contents/Resources/Assets.car"
        # Copy pre-compiled icns for legacy fallback (named AppIcon.icns to match CFBundleIconFile)
        COMMAND ${CMAKE_COMMAND} -E copy 
            "${PRECOMPILED_ICNS}"
            "${APP_BUNDLE_PATH}/Contents/Resources/AppIcon.icns"
        COMMENT "Installing pre-compiled icon assets for Liquid Glass support"
    )
endif()

# Extra (non-version) variables needed by DMG shell scripts
set(_dmg_extra_vars "${CMAKE_CURRENT_BINARY_DIR}/dmg_extra_vars.cmake")
file(WRITE "${_dmg_extra_vars}"
    "set(CMAKE_PROJECT_NAME \"${CMAKE_PROJECT_NAME}\")\n"
    "set(CMAKE_BINARY_DIR \"${CMAKE_BINARY_DIR}\")\n"
    "set(CMAKE_CURRENT_SOURCE_DIR \"${CMAKE_CURRENT_SOURCE_DIR}\")\n"
    "set(CMAKE_COMMAND \"${CMAKE_COMMAND}\")\n"
    "set(Qt6_ROOT \"${Qt6_ROOT}\")\n"
    "set(QT_DIR \"${QT_DIR}\")\n"
    "set(CMAKE_PREFIX_PATH \"${CMAKE_PREFIX_PATH}\")\n"
    "set(IMAGER_SIGNING_IDENTITY \"${IMAGER_SIGNING_IDENTITY}\")\n"
    "set(IMAGER_NOTARIZE_APP \"${IMAGER_NOTARIZE_APP}\")\n"
    "set(IMAGER_NOTARIZE_KEYCHAIN_PROFILE \"${IMAGER_NOTARIZE_KEYCHAIN_PROFILE}\")\n"
)

if(IMAGER_SIGNED_APP)
    if(IMAGER_SIGNING_IDENTITY)
        # Generate shell scripts at build time so version is always fresh
        add_custom_command(
            OUTPUT "${CMAKE_BINARY_DIR}/create_styled_dmg.sh"
            COMMAND ${CMAKE_COMMAND}
                -DVERSION_VARS_FILE=${IMAGER_VERSION_VARS}
                -DEXTRA_VARS_FILE=${_dmg_extra_vars}
                -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/mac/create_styled_dmg.sh.in
                -DOUTPUT=${CMAKE_BINARY_DIR}/create_styled_dmg.sh
                -P ${CONFIGURE_VERSIONED_SCRIPT}
            COMMAND chmod +x "${CMAKE_BINARY_DIR}/create_styled_dmg.sh"
            DEPENDS
                ${IMAGER_VERSION_VARS}
                ${CMAKE_CURRENT_SOURCE_DIR}/mac/create_styled_dmg.sh.in
            COMMENT "Configuring create_styled_dmg.sh with build-time version"
            VERBATIM
        )
        add_custom_command(
            OUTPUT "${CMAKE_BINARY_DIR}/macos_post_build.sh"
            COMMAND ${CMAKE_COMMAND}
                -DVERSION_VARS_FILE=${IMAGER_VERSION_VARS}
                -DEXTRA_VARS_FILE=${_dmg_extra_vars}
                -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/mac/macos_post_build.sh.in
                -DOUTPUT=${CMAKE_BINARY_DIR}/macos_post_build.sh
                -P ${CONFIGURE_VERSIONED_SCRIPT}
            COMMAND chmod +x "${CMAKE_BINARY_DIR}/macos_post_build.sh"
            DEPENDS
                ${IMAGER_VERSION_VARS}
                ${CMAKE_CURRENT_SOURCE_DIR}/mac/macos_post_build.sh.in
            COMMENT "Configuring macos_post_build.sh with build-time version"
            VERBATIM
        )
        add_custom_target(dmg
            COMMAND "${CMAKE_BINARY_DIR}/macos_post_build.sh"
            DEPENDS ${PROJECT_NAME}
                "${CMAKE_BINARY_DIR}/create_styled_dmg.sh"
                "${CMAKE_BINARY_DIR}/macos_post_build.sh"
            COMMENT "Creating signed macOS DMG"
            VERBATIM)
    else()
        message(FATAL_ERROR "Signing requested, but no signing identity provided")
    endif()
else()
    # Unsigned DMG: use a CMake script so the filename gets the build-time version
    add_custom_target(dmg
        COMMAND ${CMAKE_COMMAND}
            "-DVERSION_VARS_FILE=${IMAGER_VERSION_VARS}"
            "-DAPP_NAME=${APP_NAME}"
            "-DAPP_BUNDLE_PATH=${APP_BUNDLE_PATH}"
            "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CreateUnsignedDMG.cmake"
        DEPENDS ${PROJECT_NAME}
        COMMENT "Creating macOS DMG"
        VERBATIM)
endif()

    message(STATUS "Added 'dmg' target to build the macOS DMG installer")
endif() # BUILD_CLI_ONLY


