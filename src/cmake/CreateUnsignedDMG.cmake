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

# macOS 26 deprecated hdiutil create for diskutil image create.  Prefer the
# replacement where it exists; build machines on earlier releases have only the
# original.  ULMO on both paths: diskutil cannot write the UDBZ this asked for
# before, and LZMA is both smaller and what the signed release now ships, so a
# DMG does not change shape with the machine that built it.  Neither tool
# overwrites, so clear the path first rather than relying on hdiutil's -ov.
file(REMOVE "${DMG_PATH}")

execute_process(COMMAND diskutil image
                RESULT_VARIABLE has_diskutil_image
                OUTPUT_QUIET ERROR_QUIET)

if(has_diskutil_image EQUAL 0)
    set(DMG_TOOL "diskutil image create")
    execute_process(
        COMMAND diskutil image create from
            --volumeName "${APP_NAME}"
            --format ULMO
            "${APP_BUNDLE_PATH}"
            "${DMG_PATH}"
        RESULT_VARIABLE result
    )
else()
    set(DMG_TOOL "hdiutil create")
    execute_process(
        COMMAND hdiutil create
            -volname "${APP_NAME}"
            -srcfolder "${APP_BUNDLE_PATH}"
            -format ULMO
            "${DMG_PATH}"
        RESULT_VARIABLE result
    )
endif()
if(NOT result EQUAL 0)
    message(FATAL_ERROR "${DMG_TOOL} failed with exit code ${result}")
endif()

message(STATUS "Creating versioned DMG at ${FINAL_DMG_PATH}...")
file(COPY_FILE "${DMG_PATH}" "${FINAL_DMG_PATH}")
