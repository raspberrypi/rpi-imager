# CreateUnsignedDMG.cmake — build-time unsigned DMG creation
#
# Expected -D inputs:
#   VERSION_VARS_FILE — path to imager_version_vars.cmake
#   APP_NAME          — display name (e.g. "Raspberry Pi Imager")
#   APP_BUNDLE_PATH   — path to .app bundle
#   BUILD_DIR         — CMAKE_BINARY_DIR

include("${VERSION_VARS_FILE}")

set(DMG_PATH "${BUILD_DIR}/${APP_NAME}.dmg")
set(FINAL_DMG_PATH "${BUILD_DIR}/${APP_NAME}-${IMAGER_VERSION_STR}.dmg")

message(STATUS "Creating DMG...")
execute_process(
    COMMAND hdiutil create
        -volname "${APP_NAME}"
        -srcfolder "${APP_BUNDLE_PATH}"
        -ov -format UDBZ
        "${DMG_PATH}"
    RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "hdiutil create failed with exit code ${result}")
endif()

message(STATUS "Creating versioned DMG at ${FINAL_DMG_PATH}...")
file(COPY_FILE "${DMG_PATH}" "${FINAL_DMG_PATH}")
