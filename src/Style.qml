/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

pragma Singleton

import QtQuick

Item {
    id: root

    // === COLORS ===
    readonly property color mainBackgroundColor: "#ffffff"
    readonly property color raspberryRed: "#ab1e3a"
    readonly property color transparent: "transparent"

    readonly property color buttonBackgroundColor: mainBackgroundColor
    readonly property color buttonForegroundColor: raspberryRed
    readonly property color buttonFocusedBackgroundColor: "#d1dcfb"
    readonly property color buttonHoveredBackgroundColor: "#f2f2f2"

    readonly property color button2BackgroundColor: raspberryRed
    readonly property color button2ForegroundColor: mainBackgroundColor
    // Focused: noticeably darker for strong state indication (keyboard focus)
    readonly property color button2FocusedBackgroundColor: "#8f122c"
    // Hovered: noticeably lighter to differentiate from base (≥4.5:1 contrast vs base)
    readonly property color button2HoveredBackgroundColor: "#eac7ce"
    // Hovered foreground should be Raspberry Red for ≥4.5:1 contrast on the light hover bg
    readonly property color button2HoveredForegroundColor: raspberryRed
    readonly property color raspberryRedHighlight: "#d64561"

    readonly property color titleBackgroundColor: "#f5f5f5"
    readonly property color titleSeparatorColor: "#afafaf"
    readonly property color popupBorderColor: "#dcdcdc"

    readonly property color listViewRowBackgroundColor: "#ffffff"
    readonly property color listViewHoverRowBackgroundColor: titleBackgroundColor
    // Selection highlight color for OS/device lists
    readonly property color listViewHighlightColor: "#BACCE7"

    // Utility translucent colors
    readonly property color translucentWhite10: Qt.rgba(255, 255, 255, 0.1)
    readonly property color translucentWhite30: Qt.rgba(255, 255, 255, 0.3)

    // descriptions in list views
    readonly property color textDescriptionColor: "#1a1a1a"
    // Sidebar colors
    readonly property color sidebarActiveBackgroundColor: raspberryRed
    readonly property color sidebarTextOnActiveColor: "#FFFFFF"
    readonly property color sidebarTextOnInactiveColor: raspberryRed
    readonly property color sidebarTextDisabledColor: "#E0E0E0"
    // Sidebar controls
    readonly property color sidebarControlBorderColor: "#767676"
    readonly property color sidebarBackgroundColour: mainBackgroundColor
    readonly property color sidebarBorderColour: raspberryRed

    // OS metadata
    readonly property color textMetadataColor: "#646464"

    // for the "device / OS / storage" titles
    readonly property color subtitleColor: "#ffffff"

    readonly property color progressBarTextColor: "white"
    readonly property color progressBarVerifyForegroundColor: "#6cc04a"
    readonly property color progressBarBackgroundColor: raspberryRed
    // New: distinct colors for writing vs verification phases
    readonly property color progressBarWritingForegroundColor: raspberryRed
    readonly property color progressBarTrackColor: titleBackgroundColor

    readonly property color lanbarBackgroundColor: "#ffffe3"

    /// the check-boxes/radio-buttons have labels that might be disabled
    readonly property color formLabelColor: "black"
    readonly property color formLabelErrorColor: "red"
    readonly property color formLabelDisabledColor: "grey"
    // Active color for radio buttons, checkboxes, and switches
    readonly property color formControlActiveColor: "#1955AE"

    readonly property color embeddedModeInfoTextColor: "#ffffff"

    // Focus/outline
    readonly property color focusOutlineColor: "#0078d4"
    readonly property int focusOutlineWidth: 2
    readonly property int focusOutlineRadius: 4
    readonly property int focusOutlineMargin: -4

    // === FONTS ===
    readonly property alias fontFamily: roboto.name
    readonly property alias fontFamilyLight: robotoLight.name
    readonly property alias fontFamilyBold: robotoBold.name

    // Font sizes
    // Base scale (single source of truth)
    readonly property int fontSizeXs: 12
    readonly property int fontSizeSm: 14
    readonly property int fontSizeMd: 16
    readonly property int fontSizeXl: 24

    // Role tokens mapped to base scale
    readonly property int fontSizeTitle: fontSizeXl
    readonly property int fontSizeHeading: fontSizeMd
    readonly property int fontSizeLargeHeading: fontSizeMd
    readonly property int fontSizeFormLabel: fontSizeSm
    readonly property int fontSizeSubtitle: fontSizeSm
    readonly property int fontSizeDescription: fontSizeXs
    readonly property int fontSizeInput: fontSizeXs
    readonly property int fontSizeCaption: fontSizeXs
    readonly property int fontSizeSmall: fontSizeXs
    readonly property int fontSizeSidebarItem: fontSizeSm

    // === SPACING ===
    readonly property int spacingXXSmall: 2
    readonly property int spacingXSmall: 5
    readonly property int spacingTiny: 8
    readonly property int spacingSmall: 10
    readonly property int spacingSmallPlus: 12
    readonly property int spacingMedium: 15
    readonly property int spacingLarge: 20
    readonly property int spacingExtraLarge: 30

    // === SIZES ===
    readonly property int buttonHeightStandard: 40
    readonly property int buttonWidthMinimum: 120
    readonly property int buttonWidthSkip: 150
    
    readonly property int sectionMaxWidth: 500
    readonly property int sectionMargins: 24
    readonly property int sectionPadding: 16
    readonly property int sectionBorderWidth: 1
    readonly property int sectionBorderRadius: 8
    readonly property int listItemBorderRadius: 5
    readonly property int listItemPadding: 15
    readonly property int cardPadding: 20
    readonly property int scrollBarWidth: 10
    readonly property int sidebarWidth: 200
    readonly property int sidebarItemBorderRadius: 4
    // Embedded-mode overrides (0 radius to avoid software renderer artifacts)
    readonly property int sectionBorderRadiusEmbedded: 0
    readonly property int listItemBorderRadiusEmbedded: 0
    readonly property int sidebarItemBorderRadiusEmbedded: 0
    readonly property int buttonBorderRadiusEmbedded: 0
    // Sidebar item heights
    readonly property int sidebarItemHeight: buttonHeightStandard
    readonly property int sidebarSubItemHeight: sidebarItemHeight - 12

    // === LAYOUT ===
    readonly property int formColumnSpacing: 20
    readonly property int formRowSpacing: 15
    readonly property int stepContentMargins: 24
    readonly property int stepContentSpacing: 16

    // Font loaders
    FontLoader { id: roboto;      source: "fonts/Roboto-Regular.ttf" }
    FontLoader { id: robotoLight; source: "fonts/Roboto-Light.ttf" }
    FontLoader { id: robotoBold;  source: "fonts/Roboto-Bold.ttf" }
}
